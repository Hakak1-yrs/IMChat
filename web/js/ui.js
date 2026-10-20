// 界面工具：建 DOM、头像、提示、底部弹层（表单、确认、操作菜单），以及页面栈（手机式的“进入下一页 / 返回”）

// h('div', {class: 'x', onclick: f}, '文字', 子节点…)：文字一律作为文本节点插入，不解析 HTML
export function h(tag, props, ...children) {
  const e = document.createElement(tag);
  for (const [key, value] of Object.entries(props || {})) {
    if (value === undefined || value === null || value === false) continue;
    if (key === 'class') e.className = value;
    else if (key === 'text') e.textContent = value;
    else if (key.startsWith('on')) e.addEventListener(key.slice(2), value);
    else if (key in e && typeof value !== 'string') e[key] = value;
    else e.setAttribute(key, value === true ? '' : value);
  }
  for (const child of children.flat(Infinity)) {
    if (child === null || child === undefined || child === false) continue;
    e.append(child instanceof Node ? child : String(child));
  }
  return e;
}

// 头像底色：从一组彼此差别明显的颜色里按账号挑（连续色相的话，相邻账号的颜色太接近）
const AVATAR_COLORS = ['#e8615a', '#f0a03c', '#d4b23a', '#5cb85c', '#2fb5a8', '#3d9be9', '#5b6ee1', '#9b6bd6', '#d0679d', '#8d8d8d'];

export function avatar(name, key, size, online) {
  let hash = 0;
  for (const ch of String(key)) hash = Math.imul(hash ^ ch.codePointAt(0), 2654435761);
  hash ^= hash >>> 15;   // 再充分打散一次：10001 和 10002 只差最后一位，颜色也要差得开
  hash = Math.imul(hash, 0x2c1b3c6d);
  hash = (hash ^ (hash >>> 12)) >>> 0;
  const a = h('div', { class: 'avatar' + (size ? ' ' + size : '') }, Array.from(name || '?')[0]);
  a.style.background = AVATAR_COLORS[hash % AVATAR_COLORS.length];
  if (online) a.append(h('i', { class: 'dot' }));
  return a;
}

export function tag(text, kind) { return h('i', { class: 'tag' + (kind ? ' ' + kind : '') }, text); }

// 来消息时轻轻振动一下（用户还没点过页面时浏览器不允许振动，就不振）
export function buzz() {
  if (!navigator.vibrate || (navigator.userActivation && !navigator.userActivation.hasBeenActive)) return;
  try { navigator.vibrate(30); } catch (e) { /* 不支持 */ }
}

let toastTimer = 0;
export function toast(text) {
  if (!text) return;
  const node = document.getElementById('toast');
  node.textContent = text;
  node.classList.add('show');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => node.classList.remove('show'), 2600);
}

// 操作的统一包装：成功时提示服务端返回的 message，失败时提示原因
export async function attempt(action, success) {
  try {
    const data = await action();
    toast(success || (data && data.message) || '');
    return data || {};
  } catch (e) {
    toast(e.message);
    return null;
  }
}

// ---------------- 底部弹层 ----------------

// build(close) 返回弹层里的内容；点遮罩关闭。onClosed 在关闭时（不论怎么关的）调用
export function openSheet(build, onClosed) {
  const layer = document.getElementById('layer');
  const mask = h('div', { class: 'sheet-mask' });
  const sheet = h('div', { class: 'sheet' });
  let closed = false;
  const close = () => {
    if (closed) return;
    closed = true;
    mask.classList.remove('in');
    setTimeout(() => mask.remove(), 150);
    if (onClosed) onClosed();
  };
  mask.addEventListener('click', e => { if (e.target === mask) close(); });
  sheet.append(build(close));
  mask.append(sheet);
  layer.append(mask);
  requestAnimationFrame(() => mask.classList.add('in'));
  return close;
}

// 表单。fields: [{name, label, type: text|password|date|textarea|select|checks, value, placeholder, options: [{value, label}]}]
// onSubmit(values) 抛出错误时把原因显示在表单上，不关闭
export function form({ title, message, fields = [], submitText = '确定', danger = false, onSubmit }) {
  return openSheet(close => {
    const inputs = {};
    const error = h('p', { class: 'error' });
    const button = h('button', { class: 'primary' + (danger ? ' danger' : ''), type: 'submit' }, submitText);
    const body = h('form', {
      onsubmit: async e => {
        e.preventDefault();
        const values = {};
        for (const [name, read] of Object.entries(inputs)) values[name] = read();
        button.disabled = true;
        error.textContent = '';
        try {
          if (onSubmit) await onSubmit(values);
          close();
        } catch (err) {
          error.textContent = err.message;
        } finally {
          button.disabled = false;
        }
      },
    });
    body.append(h('h3', {}, title));
    if (message) body.append(h('p', { class: 'message' }, message));
    for (const f of fields) {
      let control;
      if (f.type === 'select') {
        control = h('select', {}, (f.options || []).map(o => h('option', { value: o.value, selected: o.value === f.value }, o.label)));
        inputs[f.name] = () => control.value;
        control.addEventListener('change', () => f.onChange && f.onChange(control.value, body));
      } else if (f.type === 'checks') {
        const boxes = (f.options || []).map(o => h('input', { type: 'checkbox', value: o.value, checked: (f.value || []).includes(o.value) }));
        control = h('div', { class: 'checks' }, (f.options || []).map((o, i) => h('label', {}, boxes[i], o.label)));
        if (!boxes.length) control.append(h('span', { class: 'hint' }, f.emptyText || '（没有可选的）'));
        inputs[f.name] = () => boxes.filter(b => b.checked).map(b => b.value);
      } else if (f.type === 'textarea') {
        control = h('textarea', { placeholder: f.placeholder || '', maxlength: f.maxlength || null }, f.value || '');
        inputs[f.name] = () => control.value;
      } else {
        control = h('input', { type: f.type || 'text', value: f.value || '', placeholder: f.placeholder || '',
          autocomplete: f.type === 'password' ? 'new-password' : 'off', inputmode: f.inputmode || null });
        inputs[f.name] = () => control.value;
      }
      const wrapper = h('label', { class: 'field' + (f.hidden ? ' hidden' : ''), 'data-name': f.name }, h('span', {}, f.label), control);
      body.append(wrapper);
    }
    body.append(button, error);
    body.append(h('button', { class: 'secondary', type: 'button', onclick: close }, '取消'));
    setTimeout(() => { const first = body.querySelector('input:not([type=checkbox]), textarea'); if (first && fields.length <= 2) first.focus(); }, 160);
    return body;
  });
}

// 确认：选了确定 resolve(true)；取消或点遮罩关闭 resolve(false)
export function confirmSheet(title, message, okText = '确定', danger = false, cancelText = '取消') {
  return new Promise(resolve => {
    let answer = false;
    openSheet(close => h('div', {},
      h('h3', {}, title),
      message ? h('p', { class: 'message' }, message) : null,
      h('button', { class: 'primary' + (danger ? ' danger' : ''), onclick: () => { answer = true; close(); } }, okText),
      h('button', { class: 'secondary', onclick: close }, cancelText)),
    () => resolve(answer));
  });
}

// 操作菜单：items [{text, danger, onClick}]
export function actionSheet(title, items) {
  openSheet(close => h('div', {},
    title ? h('h3', {}, title) : null,
    h('div', { class: 'menu' }, items.filter(Boolean).map(item =>
      h('button', { class: item.danger ? 'danger' : '', onclick: () => { close(); item.onClick(); } }, item.text))),
    h('button', { class: 'secondary', onclick: close }, '取消')));
}

export function info(title, text, lines) {
  openSheet(close => h('div', {},
    h('h3', {}, title),
    text ? h('p', { class: 'message' }, text) : null,
    lines && lines.length ? h('div', { class: 'lines' }, lines.map(line => h('div', {}, line))) : null,
    h('button', { class: 'primary', onclick: close }, '知道了')));
}

// ---------------- 渲染调度与页面栈 ----------------

let mainRenderer = null;
export function setMainRenderer(fn) { mainRenderer = fn; }

let queued = false;
// 数据变了就调用：下一帧统一重画主界面和最上面一页（同一帧里多次调用只画一次）
export function render() {
  if (queued) return;
  queued = true;
  requestAnimationFrame(() => {
    queued = false;
    if (mainRenderer) mainRenderer();
    const top = stack[stack.length - 1];
    if (top) renderEntry(top);
  });
}

const stack = [];   // 主界面之上打开的页面

// page: {key, title(), subtitle?(), buttons?() → [{text, onClick, accent}], mount?(entry), render(body, entry), onClose?(), onTopics?(topics)}
export function pushPage(page) {
  const top = stack[stack.length - 1];
  if (top && page.key && top.page.key === page.key) { renderEntry(top); return top; }
  const titleEl = h('h1');
  const subEl = h('small');
  const right = h('div', { class: 'side right' });
  const body = h('div', { class: 'page' });
  const footer = h('div', { class: 'page-footer' });
  const screen = h('section', { class: 'screen page-screen' },
    h('header', { class: 'bar' },
      h('div', { class: 'side' }, h('button', { class: 'icon big', 'aria-label': '返回', onclick: () => popPage() }, '‹')),
      h('div', { class: 'grow' }, titleEl, subEl), right),
    body, footer);
  const entry = { page, screen, titleEl, subEl, right, body, footer };
  document.getElementById('app').append(screen);
  stack.push(entry);
  history.pushState({ depth: stack.length }, '');   // 手机的返回键 / 返回手势关闭这一页
  if (page.mount) page.mount(entry);
  renderEntry(entry);
  requestAnimationFrame(() => requestAnimationFrame(() => screen.classList.add('in')));
  return entry;
}

export function popPage() {
  if (stack.length) history.back();
}

window.addEventListener('popstate', e => {
  const depth = (e.state && e.state.depth) || 0;
  while (stack.length > depth) closeTop();
  render();
});

function closeTop() {
  const entry = stack.pop();
  if (entry.page.onClose) entry.page.onClose();
  entry.screen.remove();
}

// 退出登录、切换服务时关掉所有页面
export function closeAllPages() {
  while (stack.length) closeTop();
}

export function topPage() { const top = stack[stack.length - 1]; return top ? top.page : null; }

export function findPage(key) { const entry = stack.find(e => e.page.key === key); return entry ? entry.page : null; }

// 别人的操作影响了我（DATA_CHANGED）：打开着的页面按需要重新获取
export function notifyTopics(topics) {
  for (const entry of stack) if (entry.page.onTopics) entry.page.onTopics(topics);
}

function renderEntry(entry) {
  const { page } = entry;
  entry.titleEl.textContent = page.title ? page.title() : '';
  const sub = page.subtitle ? page.subtitle() : '';
  entry.subEl.textContent = sub || '';
  entry.subEl.classList.toggle('hidden', !sub);
  entry.right.textContent = '';
  for (const b of page.buttons ? page.buttons() : []) {
    entry.right.append(h('button', { class: 'icon' + (b.accent ? ' accent' : ''), onclick: b.onClick }, b.text));
  }
  const scroll = entry.body.scrollTop;   // 清空重画会把滚动位置归零：画完恢复，不然每来一条消息列表都跳回顶部
  page.render(entry.body, entry);
  entry.body.scrollTop = scroll;
}

// 列表里的一行：图标/头像、标题、说明、右侧内容；onClick 时可以点
export function row({ icon, title, sub, right, onClick, withAvatar = true, titleExtra }) {
  return h('li', { class: (onClick ? 'tap ' : '') + (withAvatar && icon ? 'with-avatar' : ''), onclick: onClick || null },
    icon || null,
    h('div', { class: 'item' },
      h('div', { class: 'top' }, h('span', { class: 'name' }, title, titleExtra || null)),
      sub ? h('div', { class: 'preview' }, sub) : null),
    right || (onClick ? h('span', { class: 'chevron' }, '›') : null));
}

export function cellIcon(glyph, color) {
  const node = h('div', { class: 'cell-icon' }, glyph);
  node.style.background = color;
  return node;
}

// ---------------- 时间 ----------------

export function pad(n) { return String(n).padStart(2, '0'); }
export function nowText() {
  const d = new Date();
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`;
}
export function parseTime(text) {
  const d = new Date(String(text || '').replace(' ', 'T'));
  return isNaN(d.getTime()) ? null : d;
}
function sameDay(a, b) { return a.toDateString() === b.toDateString(); }
export function listTime(text) {
  const d = parseTime(text);
  if (!d) return '';
  const now = new Date();
  if (sameDay(d, now)) return `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  const yesterday = new Date(now);
  yesterday.setDate(now.getDate() - 1);
  if (sameDay(d, yesterday)) return '昨天';
  return d.getFullYear() === now.getFullYear() ? `${d.getMonth() + 1}/${d.getDate()}` : `${d.getFullYear()}/${d.getMonth() + 1}/${d.getDate()}`;
}
export function stampTime(text) {
  const d = parseTime(text);
  if (!d) return text || '';
  const hm = `${pad(d.getHours())}:${pad(d.getMinutes())}`;
  const day = listTime(text);
  return day.includes(':') ? hm : `${day} ${hm}`;
}
