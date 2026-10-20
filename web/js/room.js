// 实时群聊室（单机版“开设 / 加入实时群聊室”）：在线的群成员进来即时聊天，内容不保存，由服务端主持。
// 离开页面就退出群聊室；断线重连后自动重新进入
import * as net from './net.js';
import { S, nameOf } from './store.js';
import { h, avatar, pushPage, render, info, toast, nowText, stampTime } from './ui.js';

const rooms = new Map();   // 群号 → 打开着的群聊室

async function join(room) {
  try {
    const r = await net.call('room.join', { groupNo: room.groupNo });
    room.members = r.members || [];
    room.name = r.name || room.name;
    if (!room.joined) room.lines.push({ kind: 'tip', text: `你进入了群聊室，现在室内 ${room.members.length} 人。这里的消息不保存，离开后就看不到了` });
    room.joined = true;
  } catch (e) {
    room.joined = false;
    room.lines.push({ kind: 'tip', text: e.message });
  }
  render();
}

export function openRoom(groupNo) {
  if (rooms.has(groupNo)) { toast('这个群聊室已经打开着，返回就能看到'); return; }
  const g = S.groups.get(String(groupNo));
  const room = { groupNo, name: g ? g.name : '群 ' + groupNo, members: [], lines: [], joined: false, page: null };
  let box = null;
  room.page = {
    key: 'room:' + groupNo,
    title: () => room.name + ' · 群聊室',
    subtitle: () => (!S.online ? '未连接，正在重连…' : room.joined ? `${room.members.length} 人在室内 · 内容不保存` : '正在进入…'),
    buttons: () => [{ text: '成员', onClick: () => info('群聊室里的成员', '', room.members.map(m => `${m.nickname}（${m.id}）`)) }],
    mount(entry) {
      box = h('div', { class: 'messages' });
      entry.body.replaceWith(h('div', { class: 'page', style: 'display:flex;flex-direction:column;padding:0;min-height:0;overflow:hidden' }, box));
      entry.body = box.parentNode;
      const input = h('textarea', { rows: 1, placeholder: '在群聊室里说点什么', enterkeyhint: 'send' });
      const send = h('button', {}, '发送');
      send.addEventListener('mousedown', e => e.preventDefault());
      entry.footer.append(h('form', {
        class: 'composer',
        onsubmit: async e => {
          e.preventDefault();
          const text = input.value.trim();
          if (!text) return;
          input.value = '';
          try {
            const r = await net.call('room.say', { groupNo, text });
            room.lines.push({ kind: 'mine', text, time: r.time || nowText() });
          } catch (err) {
            toast(err.message);
            input.value = text;
          }
          render();
        },
      }, input, send));
      input.addEventListener('keydown', e => {
        if (e.key === 'Enter' && !e.shiftKey && !e.isComposing && e.keyCode !== 229) { e.preventDefault(); send.click(); }
      });
    },
    render() {
      box.textContent = '';
      for (const line of room.lines) {
        if (line.kind === 'tip') { box.append(h('div', { class: 'tip' }, h('span', {}, line.text))); continue; }
        const mine = line.kind === 'mine';
        const name = mine ? S.nickname : line.name;
        box.append(h('div', { class: 'msg' + (mine ? ' mine' : '') },
          avatar(name, 'p:' + (mine ? S.me : line.from), 'small'),
          h('div', { class: 'body' }, mine ? null : h('div', { class: 'who' }, `${name} · ${stampTime(line.time)}`), h('div', { class: 'bubble' }, line.text))));
      }
      requestAnimationFrame(() => { box.scrollTop = box.scrollHeight; });
    },
    onClose() {
      rooms.delete(groupNo);
      if (room.joined && S.online) net.call('room.leave', { groupNo }).catch(() => {});
    },
  };
  rooms.set(groupNo, room);
  pushPage(room.page);
  join(room);
}

// 推送 ROOM_EVENT：有人进出、发言
export function handleRoomEvent(e) {
  const room = rooms.get(e.groupNo);
  if (!room) return;
  const name = e.nickname || nameOf(e.from, e.groupNo);
  if (e.event === 'join') {
    if (!room.members.some(m => m.id === e.from)) room.members.push({ id: e.from, nickname: name });
    room.lines.push({ kind: 'tip', text: `${name} 进入了群聊室` });
  } else if (e.event === 'leave') {
    room.members = room.members.filter(m => m.id !== e.from);
    room.lines.push({ kind: 'tip', text: `${name} 离开了群聊室` });
  } else if (e.event === 'say') {
    room.lines.push({ kind: 'other', from: e.from, name, text: e.text, time: e.time });
  }
  render();
}

// 重新登录后（断线重连）：服务端已经把我们移出群聊室，重新进入
export function rejoinRooms() {
  for (const room of rooms.values()) {
    room.lines.push({ kind: 'tip', text: '连接恢复，重新进入群聊室（断线期间的发言收不到）' });
    join(room);
  }
}
