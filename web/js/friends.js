// 好友：资料页（备注、删除、拉黑）、新的朋友（好友申请与验证）、添加好友、跨服务好友、黑名单
import * as net from './net.js';
import { S, loadContacts, loadSocial, serviceName } from './store.js';
import { h, avatar, tag, row, pushPage, render, form, confirmSheet, attempt, toast, listTime } from './ui.js';
import { openChat } from './chat.js';
import { openUserMoments } from './moments.js';
import { inviteP2P } from './p2p.js';
import { openAccount } from './account.js';

// 资料里一行“键：值”拆成两列
function infoRows(lines) {
  return h('div', { class: 'rows' }, (lines || []).map(line => {
    const i = line.indexOf('：');
    return i < 0 ? h('div', {}, h('span', {}, line)) : h('div', {}, h('span', {}, line.slice(0, i)), h('span', {}, line.slice(i + 1)));
  }));
}

// ---------------- 资料页 ----------------

export function openProfile(id) {
  let data = null;
  let error = '';
  const load = async () => {
    try { data = await net.call('profile.get', { id }); error = ''; } catch (e) { error = e.message; }
    render();
  };
  pushPage({
    key: 'profile:' + id,
    title: () => '详细资料',
    render(body) {
      body.textContent = '';
      if (!data) { body.append(h('p', { class: 'empty' }, error || '正在获取资料……')); return; }
      if (!data.exists) { body.append(h('p', { class: 'empty' }, `${serviceName(S.service)}账号 ${id} 不存在`)); return; }
      const name = data.remark || data.nickname;
      body.append(h('div', { class: 'profile' },
        avatar(name, 'p:' + id, 'large', data.online),
        h('div', {},
          h('h3', {}, name, data.online ? tag('在线', 'online') : null, data.blocked ? tag('已拉黑', 'muted') : null),
          h('p', {}, `${data.idLabel || '账号'}：${id}`),
          data.remark ? h('p', {}, `昵称：${data.nickname}`) : null)));
      if (data.isFriend && !data.peerHasMe) body.append(h('p', { class: 'note warn' }, '对方已把你从好友列表中删除，你发的消息会被拒绝。'));
      body.append(infoRows(data.info));
      if ((data.mutual || []).length) {
        body.append(h('div', { class: 'section' }, `共同好友（${data.mutual.length}）`));
        body.append(h('p', { class: 'note' }, data.mutual.map(m => m.nickname).join('、')));
      }
      if (id === S.me) { body.append(h('p', { class: 'note' }, '这是你自己。')); return; }
      const buttons = h('div', { class: 'buttons' });
      const button = (text, onClick, kind) => buttons.append(h('button', { class: 'mini ' + (kind || ''), onclick: onClick }, text));
      if (data.isFriend) {
        button('发消息', () => openChat('p:' + id), 'fill');
        button('点对点聊天', () => inviteP2P(id), 'accent');
        button('查看' + S.momentsName, () => openUserMoments(id, name));
        button('修改备注', () => form({
          title: '修改备注', fields: [{ name: 'remark', label: '备注', value: data.remark || data.nickname }],
          onSubmit: async v => { await net.call('friends.remark', { id, remark: v.remark }); await loadContacts(); await load(); },
        }));
      } else {
        button('加为好友', () => friendRequestForm(id, data.nickname, load), 'fill');
      }
      button(data.blocked ? '取消拉黑' : '拉黑', async () => {
        if (!data.blocked && !(await confirmSheet('拉黑 ' + name + '？', '拉黑后对方不能给你发消息、不能加你为好友。', '拉黑', true))) return;
        if (await attempt(() => net.call('friends.block', { id, on: !data.blocked }))) { await loadSocial(); await load(); }
      }, data.blocked ? '' : 'danger');
      if (data.isFriend) {
        button('删除好友', async () => {
          if (!(await confirmSheet('删除好友 ' + name + '？', '删除是单向的：只从你的列表中删除，对方列表里仍有你。', '删除', true))) return;
          if (await attempt(() => net.call('friends.remove', { id }))) { await loadContacts(); await load(); }
        }, 'danger');
      }
      body.append(buttons);
    },
    onTopics(topics) { if (topics.includes('contacts') || topics.includes('social')) load(); },
  });
  load();
}

export function friendRequestForm(id, nickname, done) {
  form({
    title: '添加好友 ' + (nickname || id),
    message: '对方开启了好友验证时，需要对方同意；否则直接成为好友。',
    fields: [
      { name: 'greeting', label: '验证消息', value: `我是${S.nickname}`, placeholder: '打个招呼' },
      { name: 'remark', label: '备注（可不填）', value: '' },
    ],
    submitText: '发送',
    onSubmit: async v => {
      const r = await net.call('friends.request', { target: id, greeting: v.greeting, remark: v.remark });
      toast(r.message);
      await loadSocial();
      if (r.becameFriends) await loadContacts();
      if (done) await done();
    },
  });
}

export function addFriendForm() {
  form({
    title: '添加好友',
    message: `输入对方的${serviceName(S.service)}账号，查看资料后添加`,
    fields: [{ name: 'id', label: '账号', placeholder: '如 10003' }],
    submitText: '查找',
    onSubmit: async v => {
      const id = v.id.trim();
      if (!id) throw new Error('请输入账号');
      openProfile(id);
    },
  });
}

// ---------------- 新的朋友 ----------------

const STATUS = { pending: '等待验证', accepted: '已同意', rejected: '已拒绝' };

export function openNewFriends() {
  pushPage({
    key: 'new-friends',
    title: () => '新的朋友',
    buttons: () => [{ text: '添加', accent: true, onClick: addFriendForm }],
    render(body) {
      body.textContent = '';
      body.append(h('ul', { class: 'list' }, row({
        title: '加我为好友时需要验证',
        sub: S.social.verify ? '已开启：别人加你要经过你同意' : '已关闭：别人加你直接成为好友',
        right: h('button', {
          class: 'mini ' + (S.social.verify ? 'fill' : ''),
          onclick: async () => { if (await attempt(() => net.call('settings.verify', { on: !S.social.verify }))) await loadSocial(); },
        }, S.social.verify ? '已开启' : '已关闭'),
      })));
      const requests = S.social.requests;
      body.append(h('div', { class: 'section' }, '好友申请'));
      if (!requests.length) { body.append(h('p', { class: 'note' }, '没有好友申请')); return; }
      body.append(h('ul', { class: 'list' }, requests.map(r => {
        const received = r.to === S.me;
        const other = received ? r.from : r.to;
        const otherName = received ? r.fromNickname : r.toNickname;
        let right;
        if (received && r.status === 'pending') {
          right = h('div', { style: 'display:flex;gap:6px' },
            h('button', { class: 'mini fill', onclick: e => { e.stopPropagation(); acceptForm(r); } }, '同意'),
            h('button', { class: 'mini', onclick: async e => {
              e.stopPropagation();
              if (await attempt(() => net.call('friends.respond', { seq: r.seq, accept: false }))) await loadSocial();
            } }, '拒绝'));
        } else {
          right = h('span', { class: 'time' }, received ? STATUS[r.status] : (r.status === 'pending' ? '等待对方验证' : STATUS[r.status]));
        }
        return h('li', { class: 'tap with-avatar', onclick: () => openProfile(other) },
          avatar(otherName, 'p:' + other),
          h('div', { class: 'item' },
            h('div', { class: 'top' }, h('span', { class: 'name' }, received ? otherName : '我 → ' + otherName), h('span', { class: 'time' }, listTime(r.time))),
            h('div', { class: 'preview' }, r.greeting || (received ? '请求添加你为好友' : '已发送申请'))),
          right);
      })));
    },
    onTopics(topics) { if (topics.includes('social')) render(); },
  });
  loadSocial().catch(() => {});
}

function acceptForm(r) {
  form({
    title: '通过 ' + r.fromNickname + ' 的好友申请',
    fields: [{ name: 'remark', label: '备注（可不填）', value: '' }],
    submitText: '同意',
    onSubmit: async v => {
      await attempt(() => net.call('friends.respond', { seq: r.seq, accept: true, remark: v.remark }));
      await Promise.all([loadSocial(), loadContacts()]);
    },
  });
}

// ---------------- 跨服务好友 ----------------

export function openCross() {
  let source = S.otherServices.length ? S.otherServices[0].service : '';
  let data = null;
  let error = '';
  const load = async () => {
    data = null;
    render();
    try { data = await net.call('friends.cross', { source }); error = ''; } catch (e) { error = e.message; }
    render();
  };
  pushPage({
    key: 'cross',
    title: () => '跨服务好友',
    subtitle: () => `依据你在其他服务中的好友，在${serviceName(S.service)}中加好友`,
    render(body) {
      body.textContent = '';
      if (!S.otherServices.length) { body.append(h('p', { class: 'empty' }, '你只开通了一个服务，请先开通其他服务。')); return; }
      body.append(h('div', { class: 'buttons' }, h('div', { class: 'segments', style: 'flex:1;margin:0' },
        S.otherServices.map(s => h('button', { class: s.service === source ? 'on' : '', onclick: () => { source = s.service; load(); } }, serviceName(s.service))))));
      if (!data) { body.append(h('p', { class: 'empty' }, error || '正在查询……')); return; }
      const sourceName = serviceName(source);
      const here = serviceName(S.service);
      body.append(h('div', { class: 'section' }, `推荐添加：你的${sourceName}好友，在${here}中还不是好友`));
      if (data.recommendError) {
        body.append(h('p', { class: 'note warn' }, data.recommendError));
        body.append(h('div', { class: 'buttons' }, h('button', { class: 'mini accent', onclick: openAccount }, '去个人中心绑定 QQ')));
      } else if (!data.recommend.length) {
        body.append(h('p', { class: 'note' }, `暂无可推荐的好友（你的${sourceName}好友没有开通${here}、已经是你的好友，或无法确认是同一人）。`));
      } else {
        body.append(h('ul', { class: 'list' }, data.recommend.map(r => row({
          icon: avatar(r.nickname, 'p:' + r.id), title: `${r.nickname}（${r.id}）`, sub: `← ${sourceName}好友 ${r.sourceNickname}（${r.sourceId}）`,
          right: h('button', { class: 'mini fill', onclick: () => friendRequestForm(r.id, r.nickname, load) }, '加好友'),
        }))));
      }
      for (const s of data.recommendSkipped || []) body.append(h('p', { class: 'note' }, '（未推荐）' + s));
      body.append(h('div', { class: 'section' }, `共同好友：在${here}和${sourceName}中都是你的好友`));
      if (data.commonError) body.append(h('p', { class: 'note warn' }, data.commonError));
      else if (!data.common.length) body.append(h('p', { class: 'note' }, '暂无'));
      else body.append(h('ul', { class: 'list' }, data.common.map(c => row({
        icon: avatar(c.nickname, 'p:' + c.id), title: c.name,
        sub: `${here} ${c.nickname}（${c.id}） / ${sourceName} ${c.sourceNickname}（${c.sourceId}）`, onClick: () => openProfile(c.id),
      }))));
      for (const s of data.commonSkipped || []) body.append(h('p', { class: 'note' }, '（未计入）' + s));
    },
    onTopics(topics) { if (topics.includes('contacts')) load(); },
  });
  load();
}

// ---------------- 黑名单 ----------------

export function openBlocked() {
  pushPage({
    key: 'blocked',
    title: () => '黑名单',
    render(body) {
      body.textContent = '';
      const list = S.social.blocked;
      if (!list.length) { body.append(h('p', { class: 'empty' }, '没有拉黑任何人')); return; }
      body.append(h('ul', { class: 'list' }, list.map(b => row({
        icon: avatar(b.nickname, 'p:' + b.id), title: b.nickname, sub: b.id,
        right: h('button', { class: 'mini', onclick: async () => { if (await attempt(() => net.call('friends.block', { id: b.id, on: false }))) await loadSocial(); } }, '取消拉黑'),
      }))));
    },
  });
}
