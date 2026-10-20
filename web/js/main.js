// 登录页和主界面：消息、联系人、动态（朋友圈）、我 四个标签页
import * as net from './net.js';
import { S, SERVICES, loginWithPassword, restore, applyAccent, unreadOf, mentionedIn, titleOf, nameOf,
  pendingRequestCount, serviceName, logout, switchService } from './store.js';
import { h, avatar, tag, row, cellIcon, render, setMainRenderer, actionSheet, confirmSheet, listTime } from './ui.js';
import { openChat } from './chat.js';
import { openProfile, openNewFriends, addFriendForm, openCross, openBlocked } from './friends.js';
import { openGroup, openSquare, createGroupForm } from './groups.js';
import { MomentsView, postMomentForm, openNotices } from './moments.js';
import { openAccount, openOverview, openStats, registerForm, openServiceForm } from './account.js';
import { chooseP2PFriend, openP2PHistory } from './p2p.js';

let loginScreen = null;
let mainScreen = null;
let tab = 'chats';
let loginService = 'QQ';
const tabPages = {};
let netState = null;
let mainTitle = null;
let rightButtons = null;
let tabButtons = null;
let feed = null;   // 动态标签页里的朋友圈

const TABS = [
  { key: 'chats', title: '消息', glyph: '💬' },
  { key: 'contacts', title: '联系人', glyph: '👥' },
  { key: 'moments', title: '动态', glyph: '🌟' },
  { key: 'me', title: '我', glyph: '👤' },
];

// ---------------- 登录页 ----------------

export function showLogin(message) {
  if (mainScreen) mainScreen.classList.add('hidden');
  if (!loginScreen) buildLogin();
  loginScreen.classList.remove('hidden');
  loginScreen.querySelector('.error').textContent = message || '';
  loginScreen.querySelector('input[type=password]').value = '';
  resetLoginButton();
}

function resetLoginButton() {
  const button = loginScreen.querySelector('.primary');
  button.disabled = false;
  button.textContent = '登录';
}

function buildLogin() {
  const last = restore('imchat.last');
  if (last && SERVICES[last.service]) loginService = last.service;
  applyAccent(loginService);
  const segments = h('div', { class: 'segments' });
  const drawSegments = () => {
    segments.textContent = '';
    for (const [code, info] of Object.entries(SERVICES)) {
      segments.append(h('button', {
        type: 'button', class: code === loginService ? 'on' : '',
        onclick: () => { loginService = code; applyAccent(code); drawSegments(); },
      }, info.name));
    }
  };
  drawSegments();
  const account = h('input', { name: 'username', autocomplete: 'username', required: true, value: last ? last.account || '' : '' });
  const password = h('input', { name: 'password', type: 'password', autocomplete: 'current-password', required: true });
  const button = h('button', { class: 'primary' }, '登录');
  const error = h('p', { class: 'error' });
  const formNode = h('form', {
    class: 'card', autocomplete: 'on',
    onsubmit: async e => {
      e.preventDefault();
      if (!account.value.trim() || !password.value) return;
      error.textContent = '';
      button.disabled = true;
      button.textContent = '登录中…';
      try {
        await loginWithPassword(loginService, account.value.trim(), password.value);
      } catch (err) {
        error.textContent = err.message;
      }
      resetLoginButton();
    },
  },
  h('div', { class: 'logo' }, 'IM'),
  h('h2', {}, 'IMChat'),
  h('p', { class: 'sub' }, '服务器 ' + net.serverAddress()),
  segments,
  h('label', { class: 'field' }, h('span', {}, '账号'), account),
  h('label', { class: 'field' }, h('span', {}, '密码'), password),
  button, error,
  h('p', { class: 'hint' }, '演示账号：QQ 10001～10006，微信 wx2001，密码都是 123456'),
  h('p', { class: 'hint' }, h('button', {
    type: 'button', class: 'link',
    onclick: () => registerForm((service, id) => { loginService = service; applyAccent(service); drawSegments(); account.value = id; password.focus(); }),
  }, '注册新用户')));
  loginScreen = h('section', { id: 'login', class: 'screen' }, formNode);
  document.getElementById('app').append(loginScreen);
}

// ---------------- 主界面 ----------------

export function showMain() {
  if (loginScreen) loginScreen.classList.add('hidden');
  if (!mainScreen) buildMain();
  mainScreen.classList.remove('hidden');
  render();
}

function buildMain() {
  mainTitle = h('h1');
  netState = h('small');
  rightButtons = h('div', { class: 'side right' });
  tabButtons = h('nav', { class: 'tabs' });
  for (const t of TABS) {
    tabButtons.append(h('button', { 'data-tab': t.key, onclick: () => { tab = t.key; render(); } },
      h('span', { class: 'glyph' }, t.glyph), t.title, h('i', { class: 'badge hidden' })));
    tabPages[t.key] = h('div', { class: 'page hidden' });
  }
  mainScreen = h('section', { id: 'main', class: 'screen' },
    h('header', { class: 'bar' }, h('div', { class: 'side' }), h('div', { class: 'grow' }, mainTitle, netState), rightButtons),
    TABS.map(t => tabPages[t.key]), tabButtons);
  document.getElementById('app').append(mainScreen);
  feed = new MomentsView('');
  setMainRenderer(renderMain);
}

function renderMain() {
  if (!mainScreen || mainScreen.classList.contains('hidden') || !S.session) return;
  const current = TABS.find(t => t.key === tab);
  mainTitle.textContent = tab === 'moments' ? S.momentsName : current.title;
  netState.textContent = !S.online ? '未连接，正在重连…' : `${serviceName(S.service)} ${S.nickname}`;
  netState.classList.toggle('bad', !S.online);
  for (const t of TABS) tabPages[t.key].classList.toggle('hidden', t.key !== tab);
  for (const b of tabButtons.querySelectorAll('button')) b.classList.toggle('on', b.dataset.tab === tab);

  // 角标：未读消息、待处理的好友申请、朋友圈的新互动
  let unread = 0;
  for (const c of S.convs.values()) unread += unreadOf(c);
  setBadge('chats', unread);
  setBadge('contacts', pendingRequestCount());
  setBadge('moments', S.momentNotices.unread.length);
  document.title = unread ? `(${unread}) IMChat` : 'IMChat';

  rightButtons.textContent = '';
  const button = (text, onClick) => rightButtons.append(h('button', { class: 'icon big', onclick: onClick }, text));
  if (tab === 'chats') button('＋', plusMenu);
  if (tab === 'contacts') button('＋', () => addFriendForm());
  if (tab === 'moments') button('✎', () => postMomentForm());

  const page = tabPages[tab];
  const scroll = page.scrollTop;   // 清空重画会把滚动位置归零，画完恢复
  if (tab === 'chats') renderChats(page);
  else if (tab === 'contacts') renderContacts(page);
  else if (tab === 'moments') feed.render(page, true);
  else renderMe(page);
  page.scrollTop = scroll;
}

function setBadge(key, count) {
  const badge = tabButtons.querySelector(`button[data-tab=${key}] .badge`);
  badge.textContent = count > 99 ? '99+' : String(count);
  badge.classList.toggle('hidden', !count);
}

function plusMenu() {
  actionSheet('', [
    { text: '添加好友', onClick: () => addFriendForm() },
    { text: '新建群聊', onClick: () => createGroupForm() },
    { text: '全部群（申请加入）', onClick: () => openSquare() },
    { text: '点对点聊天（直连好友）', onClick: () => chooseP2PFriend() },
  ]);
}

// ---------------- 消息 ----------------

function previewOf(c, last) {
  if (last.revoked) return `${last.from === S.me ? '你' : nameOf(last.from, last.groupNo)} 撤回了一条消息`;
  if (last.system || !c.group) return last.text;
  return `${last.from === S.me ? '我' : nameOf(last.from, last.groupNo)}：${last.text}`;
}

function renderChats(page) {
  const rows = [];
  for (const c of S.convs.values()) {
    const pending = S.outbox.filter(x => x.key === c.key);
    const last = c.messages[c.messages.length - 1];
    if (!last && !pending.length) continue;
    const lastPending = pending[pending.length - 1];
    rows.push({ c, last, lastPending, unread: unreadOf(c), order: lastPending ? Infinity : last.seq });
  }
  rows.sort((a, b) => b.order - a.order);
  page.textContent = '';
  if (!rows.length) {
    page.append(h('p', { class: 'empty' }, '还没有消息，去“联系人”里找个人聊聊吧'));
    return;
  }
  page.append(h('ul', { class: 'list' }, rows.map(({ c, last, lastPending, unread }) => {
    const friend = !c.group && S.friends.get(c.id);
    const preview = h('div', { class: 'preview' });
    if (unread && mentionedIn(c)) preview.append(h('b', {}, '[有人@我] '));
    preview.append(lastPending ? (lastPending.state === 'failed' ? '[发送失败] ' : '[发送中] ') + lastPending.text : previewOf(c, last));
    return h('li', { class: 'tap with-avatar', onclick: () => openChat(c.key) },
      avatar(titleOf(c), c.key, '', friend && friend.online),
      h('div', { class: 'item' },
        h('div', { class: 'top' }, h('span', { class: 'name' }, titleOf(c)), h('span', { class: 'time' }, listTime(lastPending ? lastPending.time : last.time))),
        preview),
      unread ? h('i', { class: 'badge' }, unread > 99 ? '99+' : String(unread)) : null);
  })));
}

// ---------------- 联系人 ----------------

function renderContacts(page) {
  page.textContent = '';
  const pending = pendingRequestCount();
  page.append(h('ul', { class: 'list' },
    row({ icon: cellIcon('➕', '#f0a03c'), title: '新的朋友', sub: S.social.verify ? '加我为好友时需要验证' : '已关闭好友验证',
      right: pending ? h('i', { class: 'badge' }, String(pending)) : null, onClick: openNewFriends }),
    row({ icon: cellIcon('#', '#3d9be9'), title: '全部群', sub: '浏览本服务的所有群，申请加入或新建', onClick: openSquare }),
    S.otherServices.length ? row({ icon: cellIcon('⇄', '#5b6ee1'), title: '跨服务好友', sub: '从其他服务的好友中添加、查看共同好友', onClick: openCross }) : null,
    row({ icon: cellIcon('⛔', '#8d8d8d'), title: '黑名单', sub: S.social.blocked.length ? `${S.social.blocked.length} 人` : '没有拉黑任何人', onClick: openBlocked })));

  const groups = [...S.groups.values()];
  page.append(h('div', { class: 'section' }, `群聊（${groups.length}）`));
  if (!groups.length) page.append(h('p', { class: 'note' }, '还没有加入任何群'));
  else page.append(h('ul', { class: 'list' }, groups.map(g => row({
    icon: avatar(g.name, 'g:' + g.groupNo),
    title: g.name,
    sub: `${(g.members || []).length} 人 · ${g.modeName || ''} · 群号 ${g.groupNo}`,
    onClick: () => openChat('g:' + g.groupNo),
    right: h('button', { class: 'mini', onclick: e => { e.stopPropagation(); openGroup(g.groupNo); } }, '群资料'),
  }))));

  const friends = [...S.friends.values()].sort((a, b) => (b.online ? 1 : 0) - (a.online ? 1 : 0));
  page.append(h('div', { class: 'section' }, `好友（${friends.length}，在线 ${friends.filter(f => f.online).length}）`));
  if (!friends.length) page.append(h('p', { class: 'note' }, '还没有好友，点右上角的＋添加'));
  else page.append(h('ul', { class: 'list' }, friends.map(f => {
    const name = f.remark || f.nickname || f.id;
    return row({
      icon: avatar(name, 'p:' + f.id, '', f.online),
      title: name,
      titleExtra: f.online ? tag('在线', 'online') : null,
      sub: [f.remark && f.nickname ? f.nickname : '', f.id, f.location].filter(Boolean).join(' · '),
      onClick: () => openProfile(f.id),
    });
  })));
}

// ---------------- 我 ----------------

function renderMe(page) {
  page.textContent = '';
  page.append(h('div', { class: 'profile' },
    avatar(S.nickname, 'p:' + S.me, 'large'),
    h('div', {}, h('h3', {}, S.nickname), h('p', {}, `${serviceName(S.service)} 账号：${S.me}`), h('p', {}, S.location || ''))));
  page.append(h('ul', { class: 'list' },
    row({ icon: cellIcon('👤', '#3d9be9'), title: '个人中心', sub: '资料、密码、绑定 QQ、关闭服务、注销', onClick: openAccount }),
    row({ icon: cellIcon('⇆', '#5cb85c'), title: '切换服务', sub: S.otherServices.length ? S.otherServices.map(s => serviceName(s.service)).join('、') : '只开通了一个服务', onClick: chooseService }),
    row({ icon: cellIcon('＋', '#f0a03c'), title: '开通新服务', onClick: () => openServiceForm() }),
    row({ icon: cellIcon('🔔', '#d0679d'), title: S.momentsName + '互动消息', sub: S.momentNotices.unread.length ? `${S.momentNotices.unread.length} 条新消息` : '点赞、评论和回复',
      onClick: openNotices })));
  page.append(h('ul', { class: 'list' },
    row({ icon: cellIcon('⇋', '#2fb5a8'), title: '点对点聊天记录', sub: '直连聊天的内容只保存在本机', onClick: openP2PHistory }),
    row({ icon: cellIcon('🏠', '#9b6bd6'), title: '平台概况', sub: '所有用户和他们开通的服务', onClick: openOverview }),
    row({ icon: cellIcon('📊', '#8d8d8d'), title: '服务器运行状态', sub: `${net.serverAddress()} · ${S.online ? 'WebSocket 已连接' : '未连接'}`, onClick: openStats })));
  page.append(h('button', { class: 'plain danger', onclick: async () => { if (await confirmSheet('退出登录？', '', '退出', true)) logout(''); } }, '退出登录'));
}

export function chooseService() {
  if (!S.otherServices.length) {
    openServiceForm();
    return;
  }
  actionSheet('切换到本人的其他服务', S.otherServices.map(s => ({
    text: `${serviceName(s.service)}  ${s.nickname}（${s.account}）`,
    onClick: () => { tab = 'chats'; switchService(s.service, s.account); },
  })));
}

export function setTab(key) { tab = key; render(); }
