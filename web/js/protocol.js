// 协议（docs/PROTOCOL.md）：帧 = [4 字节长度][2 字节类型][JSON 正文]，整数是大端。
// 每条 WebSocket 消息里装一帧，和原生客户端在 TCP 上发的完全相同

export const T = {
  PING: 1, PONG: 2, LOGIN_REQ: 100, LOGIN_RESP: 101, KICKED: 102,
  SEND_MSG_REQ: 200, SEND_MSG_ACK: 201, MSG_PUSH: 202, SYNC_REQ: 300, SYNC_RESP: 301,
  READ_REQ: 400, READ_RESP: 401, READ_RECEIPT: 402, CALL_REQ: 500, CALL_RESP: 501,
  PRESENCE: 510, MSG_REVOKED: 511, DATA_CHANGED: 512, P2P_SIGNAL: 513, ROOM_EVENT: 514, ERROR_RESP: 900,
};

const encoder = new TextEncoder();
const decoder = new TextDecoder();

export function encodeFrame(type, body) {
  const json = encoder.encode(JSON.stringify(body || {}));
  const bytes = new Uint8Array(6 + json.length);
  const view = new DataView(bytes.buffer);
  view.setUint32(0, 2 + json.length);
  view.setUint16(4, type);
  bytes.set(json, 6);
  return bytes;
}

// 一条 WebSocket 消息里的帧（通常正好一帧）。正文不是合法 JSON 的帧跳过
export function decodeFrames(buffer) {
  const frames = [];
  const view = new DataView(buffer);
  for (let offset = 0; offset + 6 <= buffer.byteLength;) {
    const length = view.getUint32(offset);
    const type = view.getUint16(offset + 4);
    const raw = new Uint8Array(buffer, offset + 6, Math.max(0, Math.min(length - 2, buffer.byteLength - offset - 6)));
    offset += 4 + Math.max(length, 2);
    try {
      frames.push({ type, body: raw.length ? JSON.parse(decoder.decode(raw)) : {} });
    } catch (e) { /* 跳过 */ }
  }
  return frames;
}
