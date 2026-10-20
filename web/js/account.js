// 个人中心（资料、密码、绑定 QQ、开通 / 关闭服务、注销）、注册新用户、平台概况、服务器运行状态
import * as net from './net.js';
import { S, SERVICES, serviceName, switchService, logout } from './store.js';
import { h, avatar, row, pushPage, render, form, info, confirmSheet, attempt, toast, nowText } from './ui.js';

const today = () => nowText().slice(0, 10);

// 资料表单的公共字段（与单机版的检查规则相同，服务端会再检查一遍）
function profileFields() {
  return [
    { name: 'nickname', label: '昵称', placeholder: '请输入昵称' },
    { name: 'birthday', label: '出生日期', type: 'date', value: '2004-01-01' },
    { name: 'location', label: '所在地', placeholder: '如 北京' },
    { name: 'password', label: '密码（至少 6 位）', type: 'password' },
    { name: 'password2', label: '再次输入密码', type: 'password' },
  ];
}

function checkProfile(v) {
  if (!v.nickname.trim() || !v.location.trim()) throw new Error('昵称和所在地不能为空。');
  if (!v.birthday || v.birthday > today()) throw new Error('出生日期不正确（不能晚于今天）。');
  if (v.password.length < 6) throw new Error('密码太短，请至少输入 6 位。');
  if (v.password !== v.password2) throw new Error('两次输入的密码不一致。');
}

// ---------------- 注册（登录页） ----------------

export function registerForm(done) {
  form({
    title: '注册新用户',
    message: '注册后开通第一个服务，账号由平台分配',
    fields: [
      { name: 'name', label: '真实姓名', placeholder: '请输入真实姓名' },
      { name: 'service', label: '首个开通的服务', type: 'select', value: 'QQ',
        options: Object.entries(SERVICES).map(([value, s]) => ({ value, label: s.name })) },
      ...profileFields(),
    ],
    submitText: '注册',
    onSubmit: async v => {
      if (!v.name.trim()) throw new Error('真实姓名不能为空。');
      checkProfile(v);
      // 注册不需要登录：临时连一下服务端
      const temporary = !net.isOpen();
      if (temporary) {
        try { await net.open(); } catch (e) { throw new Error('连不上服务器 ' + net.serverAddress()); }
      }
      try {
        const r = await net.call('account.register', v);
        info('注册成功', r.message + '\n已为你填好账号，输入密码即可登录。');
        if (done) done(r.service, r.account);
      } finally {
        if (temporary) net.close();
      }
    },
  });
}

// ---------------- 个人中心 ----------------

export function openAccount() {
  let data = null;
  let error = '';
  const load = async () => {
    try { data = await net.call('account.get'); error = ''; } catch (e) { error = e.message; }
    render();
  };
  const update = (title, field, label, type, value) => form({
    title, fields: [{ name: field, label, type, value }],
    onSubmit: async v => {
      const r = await net.call('account.update', { [field]: v[field] });
      toast(r.message);
      if (field === 'nickname') S.nickname = v[field].trim();
      if (field === 'location') S.location = v[field].trim();
      await load();
    },
  });
  pushPage({
    key: 'account',
    title: () => '个人中心',
    subtitle: () => data ? `${data.serviceName} · 真实姓名 ${data.name}` : '',
    render(body) {
      body.textContent = '';
      if (!data) { body.append(h('p', { class: 'empty' }, error || '正在获取……')); return; }
      body.append(h('div', { class: 'profile' }, avatar(data.nickname, 'p:' + data.id, 'large'),
        h('div', {}, h('h3', {}, data.nickname), h('p', {}, `${data.idLabel}：${data.id}`), h('p', {}, `T龄 ${data.tAge} 年 · 注册于 ${data.registerDate}`))));
      // 资料：由服务端账号的虚函数 showInfo() 输出，QQ、微信、微博、微商各自追加特有的信息（多态）
      body.append(h('div', { class: 'rows' }, (data.info || []).map(line => {
        const i = line.indexOf('：');
        return i < 0 ? h('div', {}, h('span', {}, line)) : h('div', {}, h('span', {}, line.slice(0, i)), h('span', {}, line.slice(i + 1)));
      })));
      const buttons = h('div', { class: 'buttons' });
      const button = (text, onClick, kind) => buttons.append(h('button', { class: 'mini ' + (kind || ''), onclick: onClick }, text));
      button('修改昵称', () => update('修改昵称', 'nickname', '新昵称', 'text', data.nickname));
      button('修改所在地', () => update('修改所在地', 'location', '新所在地', 'text', data.location));
      button('修改出生日期', () => update('修改出生日期', 'birthday', '出生日期', 'date', data.birthday));
      button('修改密码', () => form({
        title: `修改密码（${data.serviceName}）`, message: '各服务账号的密码相互独立',
        fields: [{ name: 'old', label: '原密码', type: 'password' }, { name: 'new', label: '新密码（至少 6 位）', type: 'password' },
          { name: 'new2', label: '再次输入新密码', type: 'password' }],
        onSubmit: async v => {
          if (v.new !== v.new2) throw new Error('两次输入的密码不一致。');
          toast((await net.call('account.password', { old: v.old, new: v.new })).message);
        },
      }));
      if (data.independent) {
        button(data.boundQQ ? `解绑 QQ ${data.boundQQ}` : '绑定 QQ', async () => {
          if (data.boundQQ && !(await confirmSheet('解绑 QQ？', `解绑后，${data.serviceName}将不能再与 QQ、微博互认好友（共同好友、推荐好友）。`, '解绑', true))) return;
          const r = await attempt(() => net.call('account.bindQQ', { on: !data.boundQQ }));
          if (r) await load();
        });
      }
      body.append(buttons);
      if (!data.independent && data.service !== 'QQ') body.append(h('p', { class: 'note' }, `${data.serviceName}与 QQ 共享号码，本来就对应你的 QQ 号，不需要绑定。`));
      else if (data.independent && !data.hasQQ) body.append(h('p', { class: 'note' }, '你还没有开通 QQ。开通 QQ 后可以绑定，与 QQ、微博互认好友。'));

      body.append(h('div', { class: 'section' }, `已开通的服务（${data.services.length}）`));
      body.append(h('ul', { class: 'list' }, data.services.map(s => row({
        icon: avatar(serviceName(s.service), 's:' + s.service, 'small'),
        title: `${serviceName(s.service)}  ${s.nickname}`, sub: s.account,
        right: s.service === S.service ? h('span', { class: 'time' }, '当前') : h('button', { class: 'mini', onclick: () => switchService(s.service, s.account) }, '切换'),
      }))));
      if (data.available.length) body.append(h('button', { class: 'plain accent', onclick: () => openServiceForm(data) }, '开通新服务'));
      body.append(h('button', { class: 'plain danger', onclick: () => closeServiceForm(data) }, `关闭本服务（${data.serviceName}）`));
      body.append(h('button', { class: 'plain danger', onclick: deregisterForm }, '注销整个账号'));
    },
  });
  load();
}

export async function openServiceForm(known) {
  let data = known;
  if (!data) {
    try { data = await net.call('account.get'); } catch (e) { toast(e.message); return; }
  }
  if (!data.available.length) { info('开通新服务', '你已开通平台上的全部服务。'); return; }
  form({
    title: '开通新服务',
    fields: [
      { name: 'service', label: '服务', type: 'select', value: data.available[0], options: data.available.map(code => ({ value: code, label: serviceName(code) })) },
      ...profileFields(),
      { name: 'bindQQ', label: '与本人的 QQ 号绑定（仅微信、微商有效）', type: 'select', value: 'yes', options: [{ value: 'yes', label: '绑定' }, { value: 'no', label: '不绑定' }] },
    ],
    submitText: '开通',
    onSubmit: async v => {
      checkProfile(v);
      const r = await net.call('account.openService', { service: v.service, nickname: v.nickname, birthday: v.birthday,
        location: v.location, password: v.password, bindQQ: v.bindQQ === 'yes' });
      S.otherServices = [...S.otherServices, { service: r.service, account: r.account, nickname: v.nickname }];
      render();
      if (await confirmSheet('开通成功', r.message + '\n现在切换过去吗？', '切换到' + serviceName(r.service))) switchService(r.service, r.account);
    },
  });
}

function closeServiceForm(data) {
  if (data.services.length === 1) {
    info('关闭本服务', '这是你唯一开通的服务，关闭后将无法登录。如需离开平台，请选择“注销整个账号”。');
    return;
  }
  form({
    title: '关闭' + data.serviceName,
    message: '关闭后将删除该服务的账号、好友关系和群成员身份（群主身份会自动移交），无法恢复。',
    fields: [{ name: 'password', label: '请输入该服务的密码确认', type: 'password' }],
    submitText: '确认关闭',
    danger: true,
    onSubmit: async v => {
      const r = await net.call('account.closeService', { password: v.password });
      const next = (r.services || [])[0];
      toast(r.message);
      if (next) switchService(next.service, next.account);   // 这条连接已回到未登录状态：凭令牌改登录本人的其他服务
      else logout(r.message);
    },
  });
}

function deregisterForm() {
  form({
    title: '注销整个账号',
    message: '注销将删除你在平台上的全部服务账号、好友关系和群成员身份，无法恢复。',
    fields: [{ name: 'password', label: '请输入当前服务的密码确认', type: 'password' }],
    submitText: '确认注销',
    danger: true,
    onSubmit: async v => {
      const r = await net.call('account.deregister', { password: v.password });
      logout(r.message);
    },
  });
}

// ---------------- 平台概况、服务器状态 ----------------

export function openOverview() {
  let data = null;
  let error = '';
  pushPage({
    key: 'overview',
    title: () => '平台概况',
    subtitle: () => (data ? `共 ${data.users.length} 位用户` : ''),
    render(body) {
      body.textContent = '';
      if (!data) { body.append(h('p', { class: 'empty' }, error || '正在获取……')); return; }
      body.append(h('ul', { class: 'list' }, data.users.map(u => row({
        icon: avatar(u.name, 'u:' + u.id), title: `用户${u.id}  ${u.name}`,
        sub: u.services.map(s => `${serviceName(s.service)}[${s.account}]`).join('  ') || '没有开通服务',
      }))));
      body.append(h('p', { class: 'note' }, '群数量：' + Object.entries(data.groups).map(([code, n]) => `${serviceName(code)} ${n} 个`).join('，')));
      body.append(h('p', { class: 'note' }, '（预置演示账号的密码均为 123456）'));
    },
  });
  net.call('platform.overview').then(d => { data = d; render(); }, e => { error = e.message; render(); });
}

export function openStats() {
  let data = null;
  let error = '';
  const load = () => net.call('server.stats').then(d => { data = d; render(); }, e => { error = e.message; render(); });
  pushPage({
    key: 'stats',
    title: () => '服务器运行状态',
    subtitle: () => net.serverAddress(),
    buttons: () => [{ text: '刷新', accent: true, onClick: load }],
    render(body) {
      body.textContent = '';
      if (!data) { body.append(h('p', { class: 'empty' }, error || '正在获取……')); return; }
      const items = [['运行时间', `${Math.floor(data.uptimeSeconds / 3600)} 小时 ${Math.floor(data.uptimeSeconds / 60) % 60} 分`], ['I/O 线程', data.ioThreads],
        ['当前连接', data.connections], ['在线账号', data.online], ['累计登录', data.logins], ['保存的消息', data.messages],
        ['推送', data.pushes], ['通用请求', data.calls], ['因发送过快被拒', data.rateLimited], ['登录锁定', data.loginLockouts],
        ['数据文件保存', data.dataSaves], ['因连接数已满被拒', data.rejectedConnections]];
      body.append(h('div', { class: 'rows' }, items.map(([label, value]) => h('div', {}, h('span', {}, label), h('span', {}, String(value))))));
    },
  });
  load();
}
