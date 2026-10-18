#pragma once
#include <cstdint>

// 协议中的消息类型（帧头里的 2 字节）。请求和它的响应编号相邻；推送由服务端主动发出
enum class MessageType : std::uint16_t {
    PING = 1,             // 心跳（客户端 → 服务端），服务端回 PONG
    PONG = 2,
    LOGIN_REQ = 100,      // {reqId, service, account, password}
    LOGIN_RESP = 101,     // {reqId, ok, error | nickname, unreadPrivate, unreadGroup}
    KICKED = 102,         // 推送：同一账号在别处登录，本连接被下线 {reason}
    SEND_MSG_REQ = 200,   // {reqId, clientMsgId, to, text}
    SEND_MSG_ACK = 201,   // {reqId, clientMsgId, ok, error | seq, time}
    MSG_PUSH = 202,       // 推送：新消息 {seq, service, from, to, text, time}
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
