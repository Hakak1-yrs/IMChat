#pragma once
#include <cstdint>

// 协议中的消息类型（帧头里的 2 字节）。请求和它的响应编号相邻；推送由服务端主动发出。
// 各消息的正文格式见 docs/PROTOCOL.md
enum class MessageType : std::uint16_t {
    PING = 1,             // 心跳（客户端 → 服务端），服务端回 PONG
    PONG = 2,
    LOGIN_REQ = 100,      // {reqId, service, account, password}
    LOGIN_RESP = 101,     // {reqId, ok, error | nickname, unreadPrivate, unreadGroup}
    KICKED = 102,         // 推送：同一账号在别处登录，本连接被下线 {reason}
    SEND_MSG_REQ = 200,   // {reqId, clientMsgId, to | groupNo, text}
    SEND_MSG_ACK = 201,   // {reqId, clientMsgId, ok, error | seq, time}
    MSG_PUSH = 202,       // 推送：新消息（格式同 SYNC_RESP 中的一条消息）
    SYNC_REQ = 300,       // {reqId, afterSeq, limit?}：取序号大于 afterSeq 的、与我有关的消息
    SYNC_RESP = 301,      // {reqId, messages: [...], hasMore}
    READ_REQ = 400,       // {reqId, peer | groupNo, seq}：这个会话已读到 seq
    READ_RESP = 401,      // {reqId, ok}
    READ_RECEIPT = 402,   // 推送：私信的对方已读 {reader, seq}
    CALL_REQ = 500,       // 通用请求 {reqId, op, args}：查询联系人、群资料、撤回消息……（op 列表见 docs/PROTOCOL.md）
    CALL_RESP = 501,      // {reqId, ok, error | data}
    PRESENCE = 510,       // 推送：好友上线 / 下线 {account, online}
    MSG_REVOKED = 511,    // 推送：一条消息被撤回 {seq, revokedBy}
    DATA_CHANGED = 512,   // 推送：与我有关的数据变了，需要重新获取 {topics: ["contacts", "social"]}
    P2P_SIGNAL = 513,     // 推送：好友发来的点对点聊天信令（邀请、连接地址……），服务端只转发 {from, nickname, data}
    ROOM_EVENT = 514,     // 推送：实时群聊室里有人进出、发言 {groupNo, event, from, nickname, text?, time}
    ERROR_RESP = 900,     // {reqId?, code, message}
};

// ERROR_RESP 中的错误码
enum class ErrorCode : int {
    BAD_REQUEST = 1,      // 正文不是合法的 JSON，或缺少必要字段
    UNKNOWN_TYPE = 2,     // 不认识的消息类型
    NOT_LOGGED_IN = 3,    // 需要先登录
    ALREADY_LOGGED_IN = 4,
};

const char* messageTypeName(MessageType type);
