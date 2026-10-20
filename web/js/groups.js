// 群：全部群（申请加入、认领、新建）、群资料与管理（成员、审批、管理员、禁言、转让、改名、公告、切换管理模式、讨论组、退群、解散）。
// 权限全部由服务端按群当前的管理策略判断，页面只按服务端给的 can* 字段决定显示哪些按钮
import * as net from './net.js';
import { S, loadGroups, serviceName } from './store.js';
import { h, avatar, tag, row, pushPage, popPage, render, form, confirmSheet, actionSheet, attempt, info, toast } from './ui.js';
import { openChat } from './chat.js';
import { openProfile } from './friends.js';
import { openRoom } from './room.js';

export function createGroupForm() {
  form({
    title: '新建群聊',
    message: `你是群主，管理模式取${serviceName(S.service)}的默认模式`,
    fields: [{ name: 'name', label: '群名称', placeholder: '给群起个名字' }],
    submitText: '创建',
    onSubmit: async v => {
      const r = await net.call('group.create', { name: v.name });
      toast(r.message);
      await loadGroups();
      openChat('g:' + r.groupNo);
    },
  });
}

// ---------------- 全部群 ----------------

export function openSquare() {
  let groups = null;
  let error = '';
  const load = async () => {
    try { groups = (await net.call('groups.all')).groups || []; error = ''; } catch (e) { error = e.message; }
    render();
  };
  const join = async g => {
    if (await attempt(() => net.call('group.exec', { kind: 'join', groupNo: g.groupNo }))) { await Promise.all([load(), loadGroups()]); }
  };
  pushPage({
    key: 'square',
    title: () => '全部群',
    subtitle: () => serviceName(S.service) + '的所有群',
    buttons: () => [{ text: '新建', accent: true, onClick: createGroupForm }],
    render(body) {
      body.textContent = '';
      if (!groups) { body.append(h('p', { class: 'empty' }, error || '正在获取……')); return; }
      if (!groups.length) { body.append(h('p', { class: 'empty' }, '还没有群')); return; }
      body.append(h('ul', { class: 'list' }, groups.map(g => {
        let right;
        if (g.joined) right = h('button', { class: 'mini accent', onclick: e => { e.stopPropagation(); openChat('g:' + g.groupNo); } }, '进入');
        else if (g.applied) right = h('button', { class: 'mini', disabled: true }, '已申请');
        else if (!g.owner) right = h('button', { class: 'mini fill', onclick: e => { e.stopPropagation(); join(g); } }, '认领');
        else if (g.allowApplyJoin) right = h('button', { class: 'mini fill', onclick: e => { e.stopPropagation(); join(g); } }, '申请加入');
        else right = h('button', { class: 'mini', disabled: true }, '仅限邀请');
        return row({
          icon: avatar(g.name, 'g:' + g.groupNo), title: g.name,
          sub: `${g.groupNo} · ${g.modeName} · ${g.memberCount} 人 · 群主 ${g.owner ? g.ownerName : '无（可认领）'}`,
          right, onClick: () => openGroup(g.groupNo),
        });
      })));
    },
    onTopics(topics) { if (topics.includes('groups')) load(); },
  });
  load();
}

// ---------------- 群资料与管理 ----------------

export function openGroup(groupNo) {
  let g = null;
  let error = '';
  const load = async () => {
    try { g = await net.call('group.get', { groupNo }); error = ''; } catch (e) { error = e.message; }
    render();
  };
  const exec = async (kind, extra) => {
    const r = await attempt(() => net.call('group.exec', Object.assign({ kind, groupNo }, extra || {})));
    if (r) await Promise.all([loadGroups(), load()]);
    return r;
  };
  const memberName = id => { const m = g && [...g.members, ...g.pending].find(x => x.id === id); return m ? m.nickname : id; };

  const memberMenu = m => {
    actionSheet(`${m.nickname}（${m.id}）`, [
      { text: '查看资料', onClick: () => openProfile(m.id) },
      m.canSetAdmin && { text: m.admin ? '取消管理员' : '设为管理员', onClick: () => exec('admin', { target: m.id, on: !m.admin }) },
      m.canMute && { text: m.muted ? '解除禁言' : '禁言', onClick: () => exec('mute', { target: m.id, on: !m.muted }) },
      m.canTransfer && { text: '转让群主', danger: true, onClick: async () => {
        if (await confirmSheet(`把群主转让给 ${m.nickname}？`, '转让后你将成为普通成员。', '转让', true)) exec('transfer', { target: m.id });
      } },
      m.canKick && { text: '移出群聊', danger: true, onClick: async () => {
        if (await confirmSheet(`把 ${m.nickname} 移出群聊？`, '', '移出', true)) exec('kick', { target: m.id });
      } },
    ]);
  };

  const invite = () => {
    const inGroup = new Set([...g.members, ...g.pending].map(m => m.id));
    const candidates = [...S.friends.values()].filter(f => !inGroup.has(f.id));
    if (!candidates.length) { toast('你的好友都已经在群里了'); return; }
    actionSheet('邀请好友入群', candidates.map(f => ({ text: `${f.remark || f.nickname}（${f.id}）`, onClick: () => exec('invite', { target: f.id }) })));
  };

  const switchMode = async () => {
    const next = g.mode === 'QQ' ? 'WECHAT' : 'QQ';
    if (!(await confirmSheet(`切换为${next === 'QQ' ? 'QQ群' : '微信群'}模式？`, '管理员、禁言等记录会保留，切回来时恢复。', '切换'))) return;
    try {
      const r = await net.call('group.exec', { kind: 'mode', groupNo, text: next });
      info('管理模式已切换', '', String(r.message || '').split('\n').filter(Boolean));   // 结果、切换前后的数据比对、新模式的特色
      await Promise.all([loadGroups(), load()]);
    } catch (e) { toast(e.message); }
  };

  const othersOptions = exclude => g.members.filter(m => m.id !== S.me && !exclude.includes(m.id)).map(m => ({ value: m.id, label: m.nickname }));

  pushPage({
    key: 'group:' + groupNo,
    title: () => (g && g.exists ? g.name : '群资料'),
    subtitle: () => (g && g.exists ? `群号 ${groupNo} · ${g.modeName}` : ''),
    render(body) {
      body.textContent = '';
      if (!g) { body.append(h('p', { class: 'empty' }, error || '正在获取……')); return; }
      if (!g.exists) { body.append(h('p', { class: 'empty' }, `群 ${groupNo} 不存在或已解散`)); return; }

      const roles = [];
      if (g.isOwner) roles.push(tag('群主', 'owner'));
      else if (g.isAdmin) roles.push(tag('管理员', 'admin'));
      body.append(h('div', { class: 'profile' },
        avatar(g.name, 'g:' + groupNo, 'large'),
        h('div', {},
          h('h3', {}, g.name, roles),
          h('p', {}, `${g.members.length} 人 · 群主 ${g.owner ? memberName(g.owner) : '无（可认领）'}`),
          h('p', {}, g.preset ? '平台预置群（不能解散）' : ''))));
      if (g.notice) body.append(h('p', { class: 'note' }, '📢 群公告：' + g.notice));

      const buttons = h('div', { class: 'buttons' });
      const button = (text, onClick, kind) => buttons.append(h('button', { class: 'mini ' + (kind || ''), onclick: onClick }, text));
      if (g.member) {
        button('发消息', () => openChat('g:' + groupNo), 'fill');
        button('实时群聊室', () => openRoom(groupNo), 'accent');
        button('邀请好友', invite);
        if (g.canEditInfo) {
          button('修改群名', () => form({ title: '修改群名', fields: [{ name: 'name', label: '群名称', value: g.name }],
            onSubmit: async v => { if (!(await exec('rename', { text: v.name }))) throw new Error('修改失败'); } }));
          button('发布公告', () => form({ title: '发布群公告', message: '公告会同时作为一条群消息发出；留空表示清除公告',
            fields: [{ name: 'text', label: '公告', type: 'textarea', value: g.notice || '' }],
            onSubmit: async v => { if (!(await exec('notice', { text: v.text }))) throw new Error('发布失败'); } }));
        }
        if (g.isOwner) button(g.mode === 'QQ' ? '切换为微信群模式' : '切换为QQ群模式', switchMode);
        if (g.allowSubGroup) button('新建讨论组', () => form({
          title: '新建讨论组', fields: [{ name: 'name', label: '名称' }, { name: 'members', label: '成员', type: 'checks', options: othersOptions([]) }],
          onSubmit: async v => { if (!(await exec('subNew', { text: v.name, members: v.members }))) throw new Error('创建失败'); },
        }));
      } else if (g.applied) {
        button('已申请，等待审批', null);
        buttons.lastChild.disabled = true;
      } else if (!g.owner) {
        button('认领这个无主群', () => exec('join'), 'fill');
      } else if (g.allowApplyJoin) {
        button('申请加入', () => exec('join'), 'fill');
      } else {
        buttons.append(h('span', { class: 'hint' }, '本群只能由成员邀请加入'));
      }
      body.append(buttons);

      if ((g.features || []).length) {
        body.append(h('div', { class: 'section' }, `管理规则（${g.modeName}）`));
        body.append(h('p', { class: 'note' }, g.features.join('\n')));
      }
      if (g.frozenAdmins || g.frozenMuted)
        body.append(h('p', { class: 'note' }, `当前模式下不生效、但保留着的记录：管理员 ${g.frozenAdmins} 人，禁言 ${g.frozenMuted} 人（切回原模式时恢复）`));

      if (g.canApprove && g.pending.length) {
        body.append(h('div', { class: 'section' }, `入群申请（${g.pending.length}）`));
        body.append(h('ul', { class: 'list' }, g.pending.map(m => row({
          icon: avatar(m.nickname, 'p:' + m.id), title: m.nickname, sub: m.id,
          right: h('div', { style: 'display:flex;gap:6px' },
            h('button', { class: 'mini fill', onclick: e => { e.stopPropagation(); exec('approve', { target: m.id, on: true }); } }, '同意'),
            h('button', { class: 'mini', onclick: e => { e.stopPropagation(); exec('approve', { target: m.id, on: false }); } }, '拒绝')),
          onClick: () => openProfile(m.id),
        }))));
      } else if (g.pending.length) {
        body.append(h('p', { class: 'note' }, `${g.pending.length} 人申请加入，等待审批`));
      }

      body.append(h('div', { class: 'section' }, `群成员（${g.members.length}）`));
      body.append(h('ul', { class: 'list' }, g.members.map(m => {
        const friend = S.friends.get(m.id);
        return row({
          icon: avatar(m.nickname, 'p:' + m.id, '', friend && friend.online),
          title: m.nickname + (m.id === S.me ? '（我）' : ''),
          titleExtra: [m.owner ? tag('群主', 'owner') : null, m.admin ? tag('管理员', 'admin') : null, m.muted ? tag('禁言', 'muted') : null],
          sub: m.id,
          onClick: () => (m.id === S.me ? openProfile(m.id) : memberMenu(m)),
        });
      })));

      if (g.subGroups.length) {
        body.append(h('div', { class: 'section' }, `讨论组（${g.subGroups.length}）`));
        body.append(h('ul', { class: 'list' }, g.subGroups.map(s => h('li', {},
          h('div', { class: 'item' },
            h('div', { class: 'top' }, h('span', { class: 'name' }, s.name)),
            h('div', { class: 'preview wrap' }, `创建者 ${memberName(s.creator)} · ${s.members.map(memberName).join('、')}`)),
          s.canAdd ? h('button', { class: 'mini', onclick: () => form({
            title: `往“${s.name}”里拉人`, fields: [{ name: 'members', label: '群成员', type: 'checks', options: othersOptions(s.members), emptyText: '群成员都已在讨论组里' }],
            onSubmit: async v => { if (!(await exec('subAdd', { target: s.name, members: v.members }))) throw new Error('操作失败'); },
          }) }, '拉人') : null,
          s.canDissolve ? h('button', { class: 'mini danger', onclick: async () => {
            if (await confirmSheet(`解散讨论组“${s.name}”？`, '', '解散', true)) exec('subDissolve', { target: s.name });
          } }, '解散') : null))));
      }

      if (g.member) {
        body.append(h('button', { class: 'plain danger', onclick: async () => {
          if (!(await confirmSheet('退出群聊？', g.isOwner ? '群主退群前一般需要先转让群主。' : '', '退出', true))) return;
          if (await exec('quit')) popPage();
        } }, '退出群聊'));
      }
      if (g.canDissolve && !g.preset) {
        body.append(h('button', { class: 'plain danger', onclick: async () => {
          if (!(await confirmSheet('解散这个群？', '解散后群聊记录一并作废，无法恢复。', '解散', true))) return;
          if (await exec('dissolve')) popPage();
        } }, '解散群'));
      }
    },
    onTopics(topics) { if (topics.includes('groups')) load(); },
  });
  load();
}
