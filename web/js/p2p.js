// 点对点聊天（单机版“QQ点对点聊天（TCP）”的联网版）：消息直接在两台设备之间传送，不经过服务器，聊天记录只保存在本机。
// 服务端只负责让双方找到对方（p2p.signal 转发信令，见 docs/PROTOCOL.md 的“点对点聊天”）：
// - 对方是电脑客户端：它在本机开一个端口等待连接，把地址发过来，浏览器用 WebSocket 直接连过去；
// - 双方都是浏览器（浏览器不能开端口等别人连）：用浏览器自带的 WebRTC 数据通道直连；
// - 几秒内直连不上（不在同一个网络、防火墙拦住了……）：改由服务器中转，只转发、不保存。
// 直连时每条消息是一行“[昵称] 内容”，和单机版、控制台版的点对点聊天格式相同
import * as net from './net.js';
import { S, nameOf, store, restore } from './store.js';
import { h, avatar, pushPage, render, confirmSheet, actionSheet, toast, nowText, stampTime, buzz } from './ui.js';

const DIRECT_TIMEOUT_MS = 8000;    // 这么久还没直连上就改由服务器中转
const ANSWER_TIMEOUT_MS = 60000;   // 邀请这么久没有回应就取消

let session = null;   // 当前的点对点会话（和单机版一样，同时只有一个）

const hasRtc = () => typeof RTCPeerConnection === 'function';
const active = () => !!session && session.state !== 'ended';

function signal(peer, data) { return net.call('p2p.signal', { to: peer, data }); }

function newSession(peer, role, id) {
  return { id: id || Math.random().toString(36).slice(2, 10) + Date.now().toString(36), peer, peerName: nameOf(peer), role,
    state: 'new', via: '', lines: [], ws: null, pc: null, channel: null, candidates: [], timer: 0, directTimer: 0 };
}

function tip(text) {
  if (session) session.lines.push({ kind: 'tip', text, time: nowText() });
  render();
}

// ---------------- 发起与应答 ----------------

export function inviteP2P(peer) {
  if (active()) {
    if (session.peer !== peer) toast(`正在和 ${session.peerName} 点对点聊天，请先结束`);
    openPage();
    return;
  }
  const f = S.friends.get(peer);
  if (!f) { toast('只能和好友点对点聊天'); return; }
  if (!f.online) { toast(`${f.remark || f.nickname} 不在线，没法直连`); return; }
  session = newSession(peer, 'inviter');
  session.state = 'inviting';
  const s = session;
  tip(`正在邀请 ${s.peerName} 点对点聊天，等待对方接受……`);
  signal(peer, { kind: 'invite', session: s.id, rtc: hasRtc() }).catch(e => end(e.message, false));
  s.timer = setTimeout(() => { if (session === s && s.state === 'inviting') end('对方没有回应', true); }, ANSWER_TIMEOUT_MS);
  openPage();
}

export function chooseP2PFriend() {
  const online = [...S.friends.values()].filter(f => f.online);
  if (!online.length) { toast('现在没有在线的好友'); return; }
  actionSheet('选择在线的好友直连聊天', online.map(f => ({ text: `${f.remark || f.nickname}（${f.id}）`, onClick: () => inviteP2P(f.id) })));
}

// 推送 P2P_SIGNAL
export function handleSignal({ from, nickname, data }) {
  if (!data || typeof data !== 'object') return;
  if (data.kind === 'invite') { incoming(from, nickname, data); return; }
  const s = session;
  if (!s || s.id !== data.session || s.peer !== from) return;   // 不是当前的会话（比如已经结束的）
  switch (data.kind) {
  case 'accept': accepted(data); break;
  case 'reject': end(data.reason || '对方拒绝了邀请', false); break;
  case 'rtc': onRtc(s, data); break;
  case 'relay': if (s.state === 'connecting') useRelay('对方那边直连没有成功'); break;
  case 'msg': if (s.state === 'relay') receive(s, String(data.text || ''), false); break;
  case 'end': end('对方结束了聊天', false); break;
  default: break;
  }
}

async function incoming(from, nickname, data) {
  if (active()) {   // 和单机版一样，同时只聊一个
    signal(from, { kind: 'reject', session: data.session, reason: '对方正在和别人点对点聊天' }).catch(() => {});
    return;
  }
  const s = newSession(from, 'invitee', data.session);
  s.peerName = nameOf(from) !== from ? nameOf(from) : nickname || from;
  s.state = 'incoming';
  session = s;
  const ok = await confirmSheet(`${s.peerName} 邀请你点对点聊天`, '消息直接在两台设备之间传送，不经过服务器；聊天记录只保存在本机。', '接受', false, '拒绝');
  if (session !== s || s.state !== 'incoming') return;   // 等你回答的时候对方已经取消了
  if (!ok) {
    signal(from, { kind: 'reject', session: s.id, reason: '对方拒绝了邀请' }).catch(() => {});
    s.state = 'ended';
    return;
  }
  s.state = 'connecting';
  openPage();
  try {
    await signal(from, { kind: 'accept', session: s.id, rtc: hasRtc() });
  } catch (e) {
    end(e.message, false);
    return;
  }
  // 双方按同样的规则选择直连方式：对方开了端口就连过去；都是浏览器就用 WebRTC（由邀请方发起）；都不行就中转
  if (data.host) connectToHost(s, data.host);
  else if (data.rtc && hasRtc()) tip('正在建立直连（WebRTC）……');
  else { useRelay('双方都无法直接连接'); return; }
  startDirectTimer(s);
}

function accepted(data) {
  const s = session;
  if (s.state !== 'inviting') return;
  clearTimeout(s.timer);
  s.state = 'connecting';
  if (data.host) connectToHost(s, data.host);
  else if (data.rtc && hasRtc()) startOffer(s);
  else { useRelay('双方都无法直接连接'); return; }
  startDirectTimer(s);
}

function startDirectTimer(s) {
  clearTimeout(s.directTimer);
  s.directTimer = setTimeout(() => {
    if (session !== s || s.state !== 'connecting') return;
    signal(s.peer, { kind: 'relay', session: s.id }).catch(() => {});
    useRelay('直连没有成功（可能不在同一个网络，或者被防火墙拦住了）');
  }, DIRECT_TIMEOUT_MS);
}

function connected(s, via) {
  if (session !== s || s.state !== 'connecting') return false;
  clearTimeout(s.directTimer);
  s.state = 'direct';
  s.via = via;
  tip(`已直连（${via}），消息不经过服务器`);
  return true;
}

function useRelay(reason) {
  const s = session;
  closeTransports(s);
  clearTimeout(s.directTimer);
  s.state = 'relay';
  s.via = '服务器中转';
  tip(`${reason}。改由服务器中转（只转发、不保存）`);
}

// ---------------- 直连：WebSocket 连到对方电脑 ----------------

function connectToHost(s, host) {
  const ips = (host.ips || []).filter(Boolean);
  tip(`正在直连对方的电脑（${ips.join(' / ')}，端口 ${host.port}）……`);
  // 对方电脑可能有好几个地址（有线、无线、虚拟网卡）：同时都试，先连上的用，其余关掉
  for (const ip of ips) {
    let ws;
    try {
      ws = new WebSocket(`ws://${ip.includes(':') ? '[' + ip + ']' : ip}:${host.port}/p2p?key=${encodeURIComponent(host.key || '')}`);
    } catch (e) {
      continue;
    }
    ws.onopen = () => {
      if (session !== s || s.state !== 'connecting' || s.ws) { ws.close(); return; }
      s.ws = ws;
      connected(s, `WebSocket ${ip}:${host.port}`);
      ws.onmessage = e => { for (const line of String(e.data).split('\n')) if (line) receive(s, line, true); };
      ws.onclose = () => { if (session === s && s.ws === ws && s.state === 'direct') end('直连断开了', false); };
    };
  }
}

// ---------------- 直连：浏览器之间用 WebRTC ----------------

function newPeerConnection(s) {
  const pc = new RTCPeerConnection({ iceServers: [] });   // 只用本机地址：同一个局域网里直连，连不上就中转
  pc.onicecandidate = e => {
    if (e.candidate) signal(s.peer, { kind: 'rtc', session: s.id, candidate: e.candidate.toJSON() }).catch(() => {});
  };
  pc.onconnectionstatechange = () => {
    if (['failed', 'disconnected', 'closed'].includes(pc.connectionState) && session === s && s.pc === pc && s.state === 'direct') end('直连断开了', false);
  };
  s.pc = pc;
  return pc;
}

function setupChannel(s, channel) {
  s.channel = channel;
  channel.onopen = () => connected(s, 'WebRTC 数据通道');
  channel.onmessage = e => receive(s, String(e.data), true);
  channel.onclose = () => { if (session === s && s.channel === channel && s.state === 'direct') end('直连断开了', false); };
}

async function startOffer(s) {
  tip('正在建立直连（WebRTC）……');
  try {
    const pc = newPeerConnection(s);
    setupChannel(s, pc.createDataChannel('chat'));
    await pc.setLocalDescription(await pc.createOffer());
    await signal(s.peer, { kind: 'rtc', session: s.id, sdp: pc.localDescription.toJSON() });
  } catch (e) { /* 直连失败：等超时后改由服务器中转 */ }
}

async function onRtc(s, data) {
  if (s.state !== 'connecting') return;
  try {
    if (data.sdp && data.sdp.type === 'offer') {
      const pc = newPeerConnection(s);
      pc.ondatachannel = e => setupChannel(s, e.channel);
      await pc.setRemoteDescription(data.sdp);
      await pc.setLocalDescription(await pc.createAnswer());
      await signal(s.peer, { kind: 'rtc', session: s.id, sdp: pc.localDescription.toJSON() });
    } else if (data.sdp && s.pc) {
      await s.pc.setRemoteDescription(data.sdp);
    } else if (data.candidate) {
      s.candidates.push(data.candidate);   // 对方的地址可能比 sdp 先到：先存着
    }
    if (s.pc && s.pc.remoteDescription) for (const c of s.candidates.splice(0)) await s.pc.addIceCandidate(c);
  } catch (e) { /* 同上 */ }
}

function closeTransports(s) {
  if (!s) return;
  for (const thing of [s.channel, s.ws, s.pc]) { try { if (thing) thing.close(); } catch (e) { /* 已关闭 */ } }
  s.ws = null;
  s.channel = null;
  s.pc = null;
}

// ---------------- 收发、结束 ----------------

function receive(s, raw, direct) {
  if (session !== s) return;
  let name = s.peerName;
  let text = raw;
  const m = direct && /^\[([^\]]*)\] ([\s\S]*)$/.exec(raw);   // “[昵称] 内容”
  if (m) { name = m[1] || name; text = m[2]; }
  s.lines.push({ kind: 'other', name, text, time: nowText() });
  log('收', s, text);
  buzz();
  render();
}

function sendText(text) {
  const s = session;
  if (!s || (s.state !== 'direct' && s.state !== 'relay')) return;
  try {
    if (s.state === 'relay') signal(s.peer, { kind: 'msg', session: s.id, text }).catch(e => toast('发送失败：' + e.message));
    else if (s.ws) s.ws.send(`[${S.nickname}] ${text}`);
    else if (s.channel) s.channel.send(`[${S.nickname}] ${text}`);
  } catch (e) {
    toast('发送失败：' + e.message);
    return;
  }
  s.lines.push({ kind: 'mine', text, time: nowText() });
  log('发', s, text);
  render();
}

function end(reason, notify) {
  const s = session;
  if (!s || s.state === 'ended') return;
  if (notify) signal(s.peer, { kind: 'end', session: s.id }).catch(() => {});
  clearTimeout(s.timer);
  clearTimeout(s.directTimer);
  closeTransports(s);
  s.state = 'ended';
  tip(reason);
}

// 退出登录、切换服务时结束
export function endP2P() {
  if (active()) end('已退出登录', false);
}

// ---------------- 本机聊天记录 ----------------

const logKey = () => `imchat.p2plog.${S.service}.${S.me}`;

function log(direction, s, text) {
  const records = restore(logKey()) || [];
  records.push({ time: nowText(), direction, peer: s.peer, peerName: s.peerName, text, via: s.via });
  store(logKey(), records.slice(-1000));
}

export function openP2PHistory() {
  let keyword = '';
  pushPage({
    key: 'p2p-history',
    title: () => '点对点聊天记录',
    subtitle: () => '只保存在这台设备上',
    mount(entry) {
      const input = h('input', { placeholder: '按关键字查找（对方账号、昵称、内容）', oninput: () => { keyword = input.value.trim(); render(); } });
      entry.footer.append(h('div', { class: 'composer' }, input));
      input.style.cssText = 'flex:1;height:40px;border:1px solid var(--line);border-radius:8px;padding:0 10px;background:var(--bg);font-size:16px;outline:none';
    },
    render(body) {
      body.textContent = '';
      const records = (restore(logKey()) || []).filter(r => !keyword || [r.peer, r.peerName, r.text].some(x => String(x).includes(keyword))).reverse();
      if (!records.length) { body.append(h('p', { class: 'empty' }, keyword ? `没有找到包含“${keyword}”的记录` : '还没有点对点聊天记录')); return; }
      body.append(h('p', { class: 'note' }, keyword ? `包含“${keyword}”的记录共 ${records.length} 条（最新的在前）` : `共 ${records.length} 条（最新的在前）`));
      body.append(h('ul', { class: 'list' }, records.slice(0, 200).map(r => h('li', {},
        h('div', { class: 'item' },
          h('div', { class: 'top' }, h('span', { class: 'name' }, `${r.direction === '发' ? '我 → ' : ''}${r.peerName}（${r.peer}）`),
            h('span', { class: 'time' }, stampTime(r.time))),
          h('div', { class: 'preview wrap' }, r.text + (r.via ? `  · ${r.via}` : '')))))));
    },
  });
}

// ---------------- 页面 ----------------

const STATE_TEXT = { inviting: '等待对方接受', incoming: '对方邀请你', connecting: '正在直连', relay: '服务器中转（不保存）', ended: '已结束' };

function openPage() {
  let box = null;
  let status = null;
  let composer = null;
  pushPage({
    key: 'p2p',
    title: () => (session ? '点对点 · ' + session.peerName : '点对点聊天'),
    subtitle: () => (session ? (session.state === 'direct' ? '已直连 · 不经过服务器' : STATE_TEXT[session.state] || '') : ''),
    buttons: () => (active() ? [{ text: '结束', onClick: () => end('你结束了聊天', true) }] : []),
    mount(entry) {
      status = h('div', { class: 'status-line' });
      box = h('div', { class: 'messages' });
      entry.body.replaceWith(h('div', { class: 'page', style: 'display:flex;flex-direction:column;padding:0;min-height:0;overflow:hidden' }, status, box));
      entry.body = box.parentNode;
      const input = h('textarea', { rows: 1, placeholder: '直接发给对方', enterkeyhint: 'send' });
      const send = h('button', {}, '发送');
      send.addEventListener('mousedown', e => e.preventDefault());
      composer = h('form', {
        class: 'composer',
        onsubmit: e => {
          e.preventDefault();
          const text = input.value.trim();
          if (!text) return;
          input.value = '';
          sendText(text);
        },
      }, input, send);
      input.addEventListener('keydown', e => {
        if (e.key === 'Enter' && !e.shiftKey && !e.isComposing && e.keyCode !== 229) { e.preventDefault(); send.click(); }
      });
      entry.footer.append(composer);
    },
    render() {
      const s = session;
      status.textContent = !s ? '' : s.state === 'direct' ? `● 已直连：${s.via}` : s.state === 'relay' ? '● 直连不通，经服务器中转（只转发、不保存）' : STATE_TEXT[s.state] || '';
      status.className = 'status-line' + (s && s.state === 'direct' ? ' good' : s && s.state === 'relay' ? ' bad' : '');
      box.textContent = '';
      for (const line of s ? s.lines : []) {
        if (line.kind === 'tip') { box.append(h('div', { class: 'tip' }, h('span', {}, line.text))); continue; }
        const mine = line.kind === 'mine';
        const name = mine ? S.nickname : line.name;
        box.append(h('div', { class: 'msg' + (mine ? ' mine' : '') },
          avatar(name, 'p:' + (mine ? S.me : s.peer), 'small'),
          h('div', { class: 'body' }, h('div', { class: 'bubble' }, line.text))));
      }
      if (s && s.state === 'ended') {
        box.append(h('div', { class: 'buttons', style: 'justify-content:center;background:none' },
          h('button', { class: 'mini fill', onclick: () => inviteP2P(s.peer) }, '再次邀请'),
          h('button', { class: 'mini', onclick: openP2PHistory }, '聊天记录')));
      }
      requestAnimationFrame(() => { box.scrollTop = box.scrollHeight; });
      composer.classList.toggle('hidden', !s || (s.state !== 'direct' && s.state !== 'relay'));
    },
  });
}
