// 入口：把各模块连起来，决定一打开显示登录页还是直接凭令牌登录
import { S, hooks, restore, resume, reconnectNow, markRead, conv, applyAccent } from './store.js';
import { closeAllPages } from './ui.js';
import { showLogin, showMain, setTab } from './main.js';
import { handleSignal, endP2P } from './p2p.js';
import { handleRoomEvent, rejoinRooms } from './room.js';

hooks.signal = handleSignal;
hooks.room = handleRoomEvent;

hooks.loggedIn = fresh => {
  if (fresh) {   // 新登录或者换了账号：之前打开的页面属于原来的账号
    endP2P();
    closeAllPages();
    setTab('chats');
  } else {
    rejoinRooms();   // 断线重连：服务端已经把我们移出了群聊室
  }
  showMain();
};

hooks.loggedOut = message => {
  endP2P();
  closeAllPages();
  showLogin(message);
};

// 手机浏览器切到后台时连接常被断掉：回到前台立即重连；正看着的聊天标记已读
document.addEventListener('visibilitychange', () => {
  if (document.visibilityState !== 'visible') return;
  reconnectNow();
  if (S.current) markRead(conv(S.current));
});
window.addEventListener('online', reconnectNow);

const session = restore('imchat.session');
if (session && session.token && session.service && session.account) {   // 上次没退出：凭令牌直接登录
  S.service = session.service;
  S.me = session.account;
  S.session = true;
  applyAccent(session.service);
  showMain();
  resume(session);
} else {
  showLogin('');
}
