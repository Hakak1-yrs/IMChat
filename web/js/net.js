// 与服务端的一条 WebSocket 连接：请求与响应（按 reqId 对应）、推送、心跳。
// 断线后要不要重连、怎么重新登录，由 store.js 决定
import { T, encodeFrame, decodeFrames } from './protocol.js';

const HEARTBEAT_MS = 15000;   // 和原生客户端一样：15 秒一次心跳，45 秒没收到任何数据就认为连接已经失效
const DEAD_MS = 45000;

let ws = null;
let connecting = null;   // 正在建立的连接的 reject：被主动关闭时让等待它的人知道
let nextReqId = 1;
let lastReceive = 0;
const waiting = new Map();   // reqId → {resolve, reject, timer}

// 推送（不是对请求的响应）和连接断开（非主动关闭）的回调
export const handlers = { push: null, closed: null };

export function serverAddress() { return location.host || '127.0.0.1:9000'; }

export function isOpen() { return !!ws && ws.readyState === WebSocket.OPEN; }

// 建立连接：resolve 表示已连上，reject 表示连不上
export function open() {
  close();
  return new Promise((resolve, reject) => {
    let socket;
    try {
      socket = new WebSocket((location.protocol === 'https:' ? 'wss://' : 'ws://') + serverAddress() + '/ws');
    } catch (e) {
      reject(e);
      return;
    }
    socket.binaryType = 'arraybuffer';
    ws = socket;
    connecting = reject;
    let opened = false;
    socket.onopen = () => {
      opened = true;
      connecting = null;
      lastReceive = Date.now();
      resolve();
    };
    socket.onmessage = e => {
      lastReceive = Date.now();
      if (e.data instanceof ArrayBuffer) for (const frame of decodeFrames(e.data)) dispatch(frame.type, frame.body);
    };
    socket.onclose = () => {
      if (ws !== socket) return;
      cleanup();
      if (!opened) reject(new Error('连接失败'));
      else if (handlers.closed) handlers.closed();
    };
  });
}

function cleanup() {
  ws = null;
  for (const w of waiting.values()) { clearTimeout(w.timer); w.reject(new Error('连接已断开')); }
  waiting.clear();
}

// 主动关闭（退出登录、切换服务）：不触发 closed 回调
export function close() {
  const socket = ws;
  if (connecting) { const reject = connecting; connecting = null; reject(new Error('已取消')); }
  if (!socket) return;
  socket.onclose = null;
  socket.onmessage = null;
  try { socket.close(); } catch (e) { /* 已经关闭 */ }
  cleanup();
}

// 判定连接已经失效：立即当作断开处理，不等关闭握手（网络已经断了的连接，close 事件可能很久以后才来）
function drop() {
  if (!ws) return;
  close();
  if (handlers.closed) handlers.closed();
}

export function send(type, body) {
  if (!isOpen()) return false;
  ws.send(encodeFrame(type, body));
  return true;
}

// 请求和响应靠 reqId 对应：同一条连接上可以同时有多个请求在等响应
export function request(type, body, timeoutMs = 15000) {
  return new Promise((resolve, reject) => {
    const reqId = nextReqId++;
    if (!send(type, Object.assign({}, body, { reqId }))) { reject(new Error('还没有连上服务器')); return; }
    const timer = setTimeout(() => { waiting.delete(reqId); reject(new Error('服务器没有响应')); }, timeoutMs);
    waiting.set(reqId, { resolve, reject, timer });
  });
}

// 通用请求：成功返回 data，失败抛出带服务端原因的错误
export async function call(op, args) {
  const r = await request(T.CALL_REQ, { op, args: args || {} });
  if (!r.ok) throw new Error(r.error || '操作失败');
  return r.data || {};
}

function dispatch(type, body) {
  const w = body.reqId !== undefined && waiting.get(body.reqId);
  if (w) {
    waiting.delete(body.reqId);
    clearTimeout(w.timer);
    if (type === T.ERROR_RESP) w.reject(new Error(body.message || '请求出错'));
    else w.resolve(body);
    return;
  }
  if (handlers.push) handlers.push(type, body);
}

// 心跳：定期发 PING；太久没收到任何数据就断开（比如手机切换了网络），由上层重连
setInterval(() => {
  if (!isOpen()) return;
  if (Date.now() - lastReceive > DEAD_MS) drop();
  else send(T.PING, {});
}, HEARTBEAT_MS);
