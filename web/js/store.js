// 登录状态和数据：登录与断线重连、增量同步、消息与待确认队列、联系人和群等列表。
// 可靠性规则和原生客户端（src/client/ChatClient）相同，见 docs/PROTOCOL.md 的“消息可靠性”
import { T } from './protocol.js';
import * as net from './net.js';
import { render, notifyTopics, nowText, toast, buzz } from './ui.js';

export const SERVICES = {
  QQ: { name: 'QQ', accent: '#1d9bf0' },
  WECHAT: { name: '微信', accent: '#07c160' },
  WEIBO: { name: '微博', accent: '#e6162d' },
  WEISHANG: { name: '微商', accent: '#f08a00' },
};

export const S = {
  service: 'QQ',
  me: '',
  nickname: '',
  location: '',
  momentsName: '动态',
  token: '',
  creds: null,          // 重连用的 {service, account, token}
  otherServices: [],    // 本人开通的其他服务 [{service, account, nickname}]
  session: false,       // 已经登录过（断线重连期间也为 true）
  online: false,        // 当前连接已登录
  kicked: false,
  friends: new Map(),   // 账号 → {id, nickname, remark, online, location, mutual}
  groups: new Map(),    // 群号（字符串）→ 群
  convs: new Map(),     // 'p:账号' / 'g:群号' → 会话
  seen: new Set(),      // 已经收到的消息序号（推送和同步可能重叠，按序号去重）
  syncedSeq: 0,         // 同步位置：这个序号以前的消息都已收到
  maxSeq: 0,
  syncing: false,
  outbox: [],           // 待确认的消息，收到回执才移除；重连后按原顺序、原 clientMsgId 重发
  social: { requests: [], verify: true, blocked: [] },
  momentNotices: { all: [], unread: [] },
  momentsVersion: 0,    // 朋友圈有变化时加一：看着朋友圈的页面重新获取
  current: null,        // 打开着的聊天
};

// 其他模块挂上来的处理函数（避免模块之间循环依赖）
export const hooks = { signal: null, room: null, loggedIn: null, loggedOut: null };

export function store(key, value) {
  try { value === undefined ? localStorage.removeItem(key) : localStorage.setItem(key, JSON.stringify(value)); } catch (e) { /* 隐私模式等 */ }
}
export function restore(key) {
  try { return JSON.parse(localStorage.getItem(key)); } catch (e) { return null; }
}

function resetData() {
  S.friends = new Map(); S.groups = new Map(); S.convs = new Map(); S.seen = new Set();
  S.syncedSeq = 0; S.maxSeq = 0; S.outbox = []; S.current = null;
  S.social = { requests: [], verify: true, blocked: [] };
  S.momentNotices = { all: [], unread: [] };
  S.momentsVersion++;
}

export function conv(key) {
  let c = S.convs.get(key);
  if (!c) {
    c = { key, group: key[0] === 'g', id: key.slice(2), messages: [], myRead: 0, peerRead: 0 };
    S.convs.set(key, c);
  }
  return c;
}
const keyOf = m => m.groupNo !== undefined ? 'g:' + m.groupNo : 'p:' + (m.from === S.me ? m.to : m.from);

export function applyAccent(service) {
  document.documentElement.style.setProperty('--accent', (SERVICES[service] || SERVICES.QQ).accent);
}

// ---------------- 登录与重连 ----------------

let reconnectTimer = 0;
let retries = 0;

function reason(r) { return (r.error || '登录失败') + (r.retryAfter ? `（${r.retryAfter} 秒后再试）` : ''); }

// 在登录页上用密码登录：失败时抛出原因
export async function loginWithPassword(service, account, password) {
  clearTimeout(reconnectTimer);
  try {
    await net.open();
  } catch (e) {
    throw new Error('连不上服务器 ' + net.serverAddress() + '，请检查网络（手机和电脑要连同一个 Wi-Fi）');
  }
  const r = await net.request(T.LOGIN_REQ, { service, account, password });
  if (!r.ok) {
    net.close();
    throw new Error(reason(r));
  }
  await startSession(r, { service, account }, true);
}

// 凭令牌登录：页面刷新后、断线重连、切换到本人的其他服务
export async function resume(creds) {
  clearTimeout(reconnectTimer);
  if (creds) S.creds = creds;
  if (!S.creds) return;
  S.session = true;
  render();
  try {
    await net.open();
  } catch (e) {
    scheduleReconnect();
    return;
  }
  let r;
  try {
    r = await net.request(T.LOGIN_REQ, S.creds);
  } catch (e) {
    return;   // 连接又断了：closed 回调负责重连
  }
  if (!r.ok) {
    if (r.retryAfter) { net.close(); scheduleReconnect(); return; }   // 暂时锁定：按退避稍后再试
    logout('登录已失效，请重新输入密码');   // 令牌过期，或者服务端换了密钥
    return;
  }
  await startSession(r, S.creds, false);
}

function scheduleReconnect() {
  if (!S.session || S.kicked) return;
  clearTimeout(reconnectTimer);
  const delay = Math.min(30000, 500 * 2 ** retries++);   // 指数退避：0.5 秒起，最长 30 秒
  reconnectTimer = setTimeout(() => resume(), delay);
  render();
}

net.handlers.closed = () => {
  S.online = false;
  render();
  scheduleReconnect();
};

// 手机浏览器切到后台时连接常被断掉：回到前台立即重连，不等退避
export function reconnectNow() {
  if (S.session && !S.kicked && !net.isOpen()) {
    retries = 0;
    resume();
  }
}

async function startSession(r, creds, fresh) {
  const switched = creds.service !== S.service || creds.account !== S.me;
  if (fresh || switched) resetData();
  S.session = true;
  S.online = true;
  S.kicked = false;
  S.me = creds.account;
  S.service = creds.service;
  S.nickname = r.nickname || creds.account;
  S.location = r.location || '';
  S.momentsName = r.momentsName || '动态';
  S.token = r.token || S.token;
  S.otherServices = r.services || [];
  S.creds = { service: creds.service, account: creds.account, token: S.token };
  store('imchat.session', S.creds);
  store('imchat.last', { service: creds.service, account: creds.account });
  retries = 0;
  applyAccent(S.service);
  if (hooks.loggedIn) hooks.loggedIn(fresh || switched);
  render();
  S.syncing = true;
  try {
    await Promise.all([loadContacts(), loadGroups(), loadReads(), loadSocial(), loadMomentNotices()]);
    await sync();
  } catch (e) {
    return;   // 同步途中断线：重连后从同步位置继续
  }
  flushOutbox();
  render();
}

// 切换到本人开通的另一个服务（一处登录，凭令牌登录其他服务）
export function switchService(service, account) {
  net.close();
  resetData();
  S.service = service;
  S.me = account;
  S.online = false;
  return resume({ service, account, token: S.token });
}

export function logout(message) {
  clearTimeout(reconnectTimer);
  S.session = false;
  S.online = false;
  net.close();
  store('imchat.session', undefined);
  S.creds = null;
  resetData();
  if (hooks.loggedOut) hooks.loggedOut(message || '');
}

// 同步：取同步位置之后的消息，hasMore 时从这一批最后一条继续要。
// 途中推送来的更新的消息和当前这一批之间可能还缺着，所以同步途中同步位置只前进到每一批的末尾
async function sync() {
  S.syncing = true;
  let after = S.syncedSeq;
  while (true) {
    const r = await net.request(T.SYNC_REQ, { afterSeq: after, limit: 200 });
    const messages = r.messages || [];
    for (const m of messages) addMessage(m, false);
    if (messages.length) after = messages[messages.length - 1].seq;
    S.syncedSeq = Math.max(S.syncedSeq, after);
    if (!r.hasMore || !messages.length) break;
  }
  S.syncing = false;
  S.syncedSeq = Math.max(S.syncedSeq, S.maxSeq);
}

// ---------------- 列表数据 ----------------

export async function loadContacts() {
  const data = await net.call('contacts.list');
  S.friends = new Map((data.friends || []).map(f => [f.id, f]));
  render();
}

export async function loadGroups() {
  const data = await net.call('groups.list');
  S.groups = new Map((data.groups || []).map(g => [String(g.groupNo), g]));
  render();
}

async function loadReads() {
  const data = await net.call('reads.get');
  for (const [peer, seq] of Object.entries(data.private || {})) conv('p:' + peer).myRead = Math.max(conv('p:' + peer).myRead, seq);
  for (const [peer, seq] of Object.entries(data.peerRead || {})) conv('p:' + peer).peerRead = Math.max(conv('p:' + peer).peerRead, seq);
  for (const [no, seq] of Object.entries(data.groups || {})) conv('g:' + no).myRead = Math.max(conv('g:' + no).myRead, seq);
}

export async function loadSocial() {
  const data = await net.call('social.get');
  S.social = { requests: data.requests || [], verify: data.verify !== false, blocked: data.blocked || [] };
  render();
}

export async function loadMomentNotices() {
  const data = await net.call('moments.notices');
  S.momentNotices = { all: data.all || [], unread: data.unread || [] };
  render();
}

export function pendingRequestCount() {
  return S.social.requests.filter(r => r.to === S.me && r.status === 'pending').length;
}

// ---------------- 推送 ----------------

net.handlers.push = (type, body) => {
  switch (type) {
  case T.MSG_PUSH:
    addMessage(body, true);
    break;
  case T.READ_RECEIPT: {
    const c = conv('p:' + body.reader);
    c.peerRead = Math.max(c.peerRead, body.seq || 0);
    render();
    break;
  }
  case T.PRESENCE: {
    const f = S.friends.get(body.account);
    if (f) { f.online = !!body.online; render(); }
    break;
  }
  case T.MSG_REVOKED:
    for (const c of S.convs.values()) {
      const m = c.messages.find(x => x.seq === body.seq);
      if (m) { m.revoked = true; m.revokedBy = body.revokedBy; m.text = ''; render(); break; }
    }
    break;
  case T.DATA_CHANGED: {
    // 别人的操作影响了我：重新获取对应的数据，打开着的页面也按需要刷新
    const topics = body.topics || [];
    const ignore = () => {};
    if (topics.includes('contacts')) loadContacts().catch(ignore);
    if (topics.includes('social')) loadSocial().catch(ignore);
    if (topics.includes('groups')) loadGroups().catch(ignore);
    if (topics.includes('moments')) { S.momentsVersion++; loadMomentNotices().catch(ignore); }
    notifyTopics(topics);
    break;
  }
  case T.P2P_SIGNAL:
    if (hooks.signal) hooks.signal(body);
    break;
  case T.ROOM_EVENT:
    if (hooks.room) hooks.room(body);
    break;
  case T.KICKED:
    S.kicked = true;
    logout('你的账号在别处登录了' + (body.reason ? '：' + body.reason : ''));
    break;
  case T.ERROR_RESP:
    toast(body.message || '服务器报告了一个错误');
    break;
  default:
    break;
  }
};

// ---------------- 消息 ----------------

export function addMessage(m, pushed) {
  if (typeof m.seq !== 'number' || S.seen.has(m.seq)) return;
  S.seen.add(m.seq);
  S.maxSeq = Math.max(S.maxSeq, m.seq);
  if (!S.syncing) S.syncedSeq = Math.max(S.syncedSeq, m.seq);
  const c = conv(keyOf(m));
  const list = c.messages;
  let i = list.length;
  while (i > 0 && list[i - 1].seq > m.seq) i--;   // 按序号插入：推送和同步交错到达时也保持顺序
  list.splice(i, 0, m);
  if (pushed && m.from !== S.me && S.current !== c.key) buzz();
  if (S.current === c.key) markRead(c);
  render();
}

function newClientMsgId() {
  return 'web-' + Date.now().toString(36) + '-' + Math.random().toString(36).slice(2, 10);
}

export function sendText(key, text) {
  const c = conv(key);
  const item = { clientMsgId: newClientMsgId(), key, text, state: 'sending', time: nowText() };
  if (c.group) item.groupNo = Number(c.id); else item.to = c.id;
  S.outbox.push(item);
  render();
  if (S.online) transmit(item);
  else toast('暂时没有连上服务器，连上后会自动发出');
}

export function transmit(item) {
  item.state = 'sending';
  const body = { clientMsgId: item.clientMsgId, text: item.text };
  if (item.groupNo !== undefined) body.groupNo = item.groupNo; else body.to = item.to;
  net.request(T.SEND_MSG_REQ, body).then(ack => {
    if (ack.ok) {
      S.outbox = S.outbox.filter(x => x !== item);
      const m = { seq: ack.seq, service: S.service, from: S.me, text: item.text, time: ack.time || item.time };
      if (item.groupNo !== undefined) m.groupNo = item.groupNo; else m.to = item.to;
      addMessage(m, false);
    } else if (ack.retryable) {   // 发得太快被限流：稍后用同一个编号重发
      setTimeout(() => { if (S.online && S.outbox.includes(item)) transmit(item); }, 1000);
    } else {
      item.state = 'failed';
      item.error = ack.error || '发送失败';
      toast(item.error);
      render();
    }
  }, () => { /* 断线：留在队列里，重连后重发 */ });
}

function flushOutbox() {
  for (const item of S.outbox) if (item.state === 'sending') transmit(item);
}

export function markRead(c) {
  if (document.visibilityState !== 'visible') return;
  const last = c.messages.length ? c.messages[c.messages.length - 1].seq : 0;
  if (last <= c.myRead) return;
  c.myRead = last;
  if (S.online) net.request(T.READ_REQ, c.group ? { groupNo: Number(c.id), seq: last } : { peer: c.id, seq: last }).catch(() => {});
  render();
}

export function unreadOf(c) {
  let n = 0;
  for (let i = c.messages.length - 1; i >= 0 && c.messages[i].seq > c.myRead; i--) if (c.messages[i].from !== S.me) n++;
  return n;
}

export function mentionedIn(c) {
  for (let i = c.messages.length - 1; i >= 0 && c.messages[i].seq > c.myRead; i--) {
    if ((c.messages[i].mentions || []).includes(S.me)) return true;
  }
  return false;
}

export function canRevoke(c, m) {
  if (m.revoked || m.system) return false;
  if (m.from === S.me) return true;
  const g = c.group && S.groups.get(c.id);
  return !!(g && (g.revocable || []).includes(m.from));
}

export async function revoke(m) {
  await net.call('message.revoke', { seq: m.seq });
  m.revoked = true;
  m.revokedBy = S.me;
  m.text = '';
  render();
}

// ---------------- 名称 ----------------

export function nameOf(id, groupNo) {
  if (id === S.me) return S.nickname || id;
  const f = S.friends.get(id);
  if (f) return f.remark || f.nickname || id;
  const groups = groupNo !== undefined ? [S.groups.get(String(groupNo))] : S.groups.values();
  for (const g of groups) {
    const member = g && (g.members || []).find(x => x.id === id);
    if (member) return member.nickname || id;
  }
  return id;
}

export function titleOf(c) {
  if (!c.group) return nameOf(c.id);
  const g = S.groups.get(c.id);
  return g ? g.name : '群 ' + c.id;
}

export function serviceName(code) { return (SERVICES[code] || { name: code }).name; }
