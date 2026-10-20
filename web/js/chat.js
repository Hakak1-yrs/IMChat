// 聊天页：私信和群聊。消息按序号排列，没确认的消息排在最后（发送中 / 发送失败可重发）
import { S, conv, markRead, sendText, transmit, canRevoke, revoke, nameOf, titleOf } from './store.js';
import { h, avatar, pushPage, render, stampTime, parseTime, confirmSheet, toast } from './ui.js';
import { openProfile } from './friends.js';
import { openGroup } from './groups.js';
import { openRoom } from './room.js';
import { inviteP2P } from './p2p.js';

export function openChat(key) {
  conv(key);
  pushPage(chatPage(key));
}

function chatPage(key) {
  let box = null;
  let input = null;
  let composer = null;
  let notice = null;
  let firstRender = true;
  const c = conv(key);

  return {
    key: 'chat:' + key,
    title: () => titleOf(c),
    subtitle: () => {
      if (!S.online) return '未连接，正在重连…';
      if (c.group) { const g = S.groups.get(c.id); return g ? `${(g.members || []).length} 人` : ''; }
      const f = S.friends.get(c.id);
      return f ? (f.online ? '在线' : '离线') : '不是好友';
    },
    buttons: () => c.group
      ? [{ text: '群聊室', onClick: () => openRoom(Number(c.id)) }, { text: '⋯', onClick: () => openGroup(Number(c.id)) }]
      : [{ text: '直连', onClick: () => inviteP2P(c.id) }, { text: '⋯', onClick: () => openProfile(c.id) }],
    mount(entry) {
      S.current = key;
      notice = h('div', { class: 'notice-bar hidden' });
      box = h('div', { class: 'messages' });
      entry.body.replaceWith(h('div', { class: 'page', style: 'display:flex;flex-direction:column;padding:0;min-height:0;overflow:hidden' }, notice, box));
      entry.body = box.parentNode;
      input = h('textarea', { rows: 1, placeholder: '发消息', enterkeyhint: 'send' });
      const send = h('button', {}, '发送');
      send.addEventListener('mousedown', e => e.preventDefault());   // 点“发送”时输入框不失去焦点：手机上键盘不会收起
      composer = h('form', {
        class: 'composer',
        onsubmit: e => {
          e.preventDefault();
          const text = input.value.trim();
          if (!text) return;
          input.value = '';
          autosize();
          sendText(key, text);
          render();
          requestAnimationFrame(() => { box.scrollTop = box.scrollHeight; });
        },
      }, input, send);
      const autosize = () => { input.style.height = 'auto'; input.style.height = Math.min(120, input.scrollHeight + 2) + 'px'; };
      input.addEventListener('input', autosize);
      input.addEventListener('keydown', e => {   // 回车发送，Shift+回车换行；输入法选字时的回车不算
        if (e.key === 'Enter' && !e.shiftKey && !e.isComposing && e.keyCode !== 229) { e.preventDefault(); send.click(); }
      });
      entry.footer.append(composer);
      markRead(c);
    },
    render() {
      const g = c.group ? S.groups.get(c.id) : null;
      notice.textContent = g && g.notice ? '📢 ' + g.notice : '';
      notice.classList.toggle('hidden', !(g && g.notice));
      const nearEnd = firstRender || box.scrollHeight - box.scrollTop - box.clientHeight < 120;
      box.textContent = '';
      let lastTime = null;
      const stamp = time => {
        const d = parseTime(time);
        if (d && (!lastTime || d - lastTime > 5 * 60 * 1000)) box.append(h('div', { class: 'stamp' }, stampTime(time)));
        if (d) lastTime = d;
      };
      let lastMine = null;
      for (const m of c.messages) if (m.from === S.me && !m.revoked && !m.system) lastMine = m;
      for (const m of c.messages) {
        stamp(m.time);
        if (m.system || m.revoked) {
          const text = m.revoked ? `${m.revokedBy === S.me ? '你' : nameOf(m.revokedBy || m.from, m.groupNo)} 撤回了一条消息` : m.text;
          box.append(h('div', { class: 'tip' }, h('span', {}, text)));
          continue;
        }
        const mine = m.from === S.me;
        const state = !c.group && mine && m === lastMine ? (c.peerRead >= m.seq ? '已读' : '未读') : '';
        box.append(messageRow(c, m, mine, state));
      }
      for (const item of S.outbox) {
        if (item.key !== key) continue;
        stamp(item.time);
        const failed = item.state === 'failed';
        const node = messageRow(c, { from: S.me, text: item.text }, true, failed ? '⚠ 发送失败，点这里重发' : '发送中…');
        if (failed) {
          const state = node.querySelector('.state');
          state.classList.add('failed');
          state.onclick = () => { item.state = 'sending'; render(); if (S.online) transmit(item); };
        }
        box.append(node);
      }
      if (nearEnd) requestAnimationFrame(() => { box.scrollTop = box.scrollHeight; });
      firstRender = false;

      // 不能发言时把输入框换成原因
      const blocked = c.group ? (!g ? '你已不在这个群里' : g.canSpeak === false ? '你已被禁言' : '') : '';
      const reason = composer.querySelector('.blocked');
      if (reason) reason.remove();
      for (const child of composer.children) child.classList.toggle('hidden', !!blocked);
      if (blocked) composer.append(h('div', { class: 'blocked' }, blocked));
    },
    onClose() {
      if (S.current === key) S.current = null;
      render();
    },
  };
}

function messageRow(c, m, mine, state) {
  const bubble = h('div', { class: 'bubble' }, m.text);
  const row = h('div', { class: 'msg' + (mine ? ' mine' : '') + ((m.mentions || []).includes(S.me) ? ' at' : '') },
    avatar(nameOf(m.from, m.groupNo), 'p:' + m.from, 'small'),
    h('div', { class: 'body' },
      c.group && !mine ? h('div', { class: 'who' }, nameOf(m.from, m.groupNo)) : null,
      bubble,
      state ? h('div', { class: 'state' }, state) : null));
  if (!mine) row.firstChild.addEventListener('click', () => openProfile(m.from));
  if (m.seq && canRevoke(c, m)) {   // 长按（或右键）撤回
    const ask = async () => {
      if (!(await confirmSheet('撤回这条消息？', m.text.length > 40 ? m.text.slice(0, 40) + '…' : m.text, '撤回', true))) return;
      try { await revoke(m); } catch (e) { toast(e.message); }
    };
    let timer = 0;
    bubble.addEventListener('contextmenu', e => { e.preventDefault(); ask(); });
    bubble.addEventListener('touchstart', () => { timer = setTimeout(() => { timer = 0; ask(); }, 600); }, { passive: true });
    const cancel = () => { clearTimeout(timer); timer = 0; };
    bubble.addEventListener('touchend', cancel);
    bubble.addEventListener('touchmove', cancel, { passive: true });
  }
  return row;
}
