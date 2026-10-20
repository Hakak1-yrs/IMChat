// 朋友圈（QQ 空间、微博动态……）：发表（公开 / 仅自己 / 部分可见 / 不给谁看）、点赞、评论与回复、删除，别人给我的互动消息。
// 能看到哪些动态、哪些点赞和评论，都由服务端按查看者过滤好了
import * as net from './net.js';
import { S, loadMomentNotices } from './store.js';
import { h, avatar, pushPage, render, form, confirmSheet, actionSheet, attempt, toast, listTime } from './ui.js';
import { openProfile } from './friends.js';

const changed = () => { S.momentsVersion++; render(); };

function scopeText(scope) {
  if (!scope || scope === 'all') return '公开';
  if (scope === 'self') return '仅自己可见';
  if (scope.startsWith('only:')) return '部分可见';
  if (scope.startsWith('except:')) return '不给谁看';
  return scope;
}

export class MomentsView {
  constructor(author) {
    this.author = author;   // 为空表示全部
    this.data = null;
    this.error = '';
    this.version = -1;
    this.loading = false;
  }

  async load() {
    this.loading = true;
    try {
      this.data = await net.call('moments.list', this.author ? { author: this.author } : {});
      this.error = '';
    } catch (e) {
      this.error = e.message;
    }
    this.loading = false;
    render();
  }

  render(container, isTab) {
    if (this.version !== S.momentsVersion && S.online && !this.loading) {   // 有变化（自己或别人发表、点赞、评论）：重新获取
      this.version = S.momentsVersion;
      this.load();
    }
    container.textContent = '';
    if (isTab && S.momentNotices.unread.length) {
      container.append(h('button', { class: 'plain accent', onclick: openNotices }, `🔔 ${S.momentNotices.unread.length} 条新的互动消息`));
    }
    if (!this.data) { container.append(h('p', { class: 'empty' }, this.error || '正在获取……')); return; }
    const names = this.data.names || {};
    const name = id => (id === S.me ? S.nickname : (S.friends.get(id) && S.friends.get(id).remark) || names[id] || id);
    const moments = this.data.moments || [];
    if (!moments.length) {
      container.append(h('p', { class: 'empty' }, this.author ? '还没有能看到的动态' : '还没有动态。只能看到互为好友的人发的动态，点右上角的 ✎ 发表一条吧'));
      return;
    }
    for (const m of moments) container.append(postNode(m, name));
  }
}

function postNode(m, name) {
  const mine = m.author === S.me;
  const liked = (m.likes || []).includes(S.me);
  const social = h('div', { class: 'social' });
  if ((m.likes || []).length) social.append(h('div', { class: 'likes' }, '♥ ' + m.likes.map(name).join('，')));
  for (const c of m.comments || []) {
    const node = h('div', { class: 'comment' }, h('b', {}, name(c.author)), c.replyTo ? [' 回复 ', h('b', {}, name(c.replyTo))] : null, '：' + c.text);
    node.addEventListener('click', () => {
      if (c.author === S.me) {
        actionSheet('', [{ text: '删除这条评论', danger: true, onClick: async () => {
          if (await attempt(() => net.call('moments.deleteComment', { seq: c.seq }))) changed();
        } }]);
      } else {
        commentForm(m, c.author, name(c.author));
      }
    });
    social.append(node);
  }
  return h('div', { class: 'post' },
    h('div', { onclick: () => openProfile(m.author) }, avatar(name(m.author), 'p:' + m.author, 'small')),
    h('div', { class: 'content' },
      h('div', { class: 'author', onclick: () => openProfile(m.author) }, name(m.author)),
      h('div', { class: 'text' }, m.text),
      h('div', { class: 'meta' },
        h('span', {}, listTime(m.time)),
        mine ? h('span', {}, scopeText(m.scope)) : null,
        mine ? h('button', { class: 'del', onclick: async () => {
          if (!(await confirmSheet('删除这条动态？', '', '删除', true))) return;
          if (await attempt(() => net.call('moments.delete', { seq: m.seq }))) changed();
        } }, '删除') : null,
        h('span', { class: 'spacer' }),
        h('button', { class: liked ? 'liked' : '', onclick: async () => {
          if (await attempt(() => net.call('moments.like', { seq: m.seq }), liked ? '已取消赞' : '已赞')) changed();
        } }, liked ? '♥ 取消' : '♡ 赞'),
        h('button', { onclick: () => commentForm(m, '', '') }, '评论')),
      social.childNodes.length ? social : null));
}

function commentForm(m, replyTo, replyName) {
  form({
    title: replyTo ? '回复 ' + replyName : '评论',
    fields: [{ name: 'text', label: '内容', type: 'textarea', placeholder: replyTo ? '回复 ' + replyName : '说点什么' }],
    submitText: '发送',
    onSubmit: async v => {
      if (!v.text.trim()) throw new Error('内容不能为空');
      await net.call('moments.comment', { seq: m.seq, replyTo, text: v.text });
      changed();
    },
  });
}

export function postMomentForm() {
  const friends = [...S.friends.values()].map(f => ({ value: f.id, label: f.remark || f.nickname || f.id }));
  form({
    title: '发表' + S.momentsName,
    fields: [
      { name: 'text', label: '这一刻的想法', type: 'textarea', placeholder: '说点什么…' },
      { name: 'scope', label: '谁可以看', type: 'select', value: 'all',
        options: [{ value: 'all', label: '公开（所有好友）' }, { value: 'self', label: '仅自己可见' },
          { value: 'only', label: '部分可见（选中的好友）' }, { value: 'except', label: '不给谁看（选中的好友看不到）' }],
        onChange: (value, body) => body.querySelector('[data-name=who]').classList.toggle('hidden', value !== 'only' && value !== 'except') },
      { name: 'who', label: '选择好友', type: 'checks', options: friends, hidden: true, emptyText: '还没有好友' },
    ],
    submitText: '发表',
    onSubmit: async v => {
      if (!v.text.trim()) throw new Error('内容不能为空');
      let scope = v.scope;
      if (scope === 'only' || scope === 'except') {
        if (!v.who.length) throw new Error('请选择好友');
        scope += ':' + v.who.join(',');
      }
      const r = await net.call('moments.post', { text: v.text, scope });
      toast(r.message || '已发表');
      changed();
    },
  });
}

export function openUserMoments(id, name) {
  const view = new MomentsView(id);
  pushPage({
    key: 'moments:' + id,
    title: () => (name || id) + '的' + S.momentsName,
    render(body) { view.render(body, false); },
  });
}

export function openNotices() {
  const unread = new Set(S.momentNotices.unread);
  pushPage({
    key: 'moment-notices',
    title: () => S.momentsName + '互动消息',
    render(body) {
      body.textContent = '';
      const all = S.momentNotices.all;
      if (!all.length) { body.append(h('p', { class: 'empty' }, '还没有人给你点赞、评论')); return; }
      body.append(h('ul', { class: 'list' }, all.map(text => h('li', {},
        h('div', { class: 'item' }, h('div', { class: 'preview wrap', style: 'color:var(--text);font-size:15px' }, text)),
        unread.has(text) ? h('i', { class: 'badge' }, '新') : null))));
    },
  });
  if (S.momentNotices.unread.length) net.call('moments.markRead').then(() => loadMomentNotices()).catch(() => {});
}
