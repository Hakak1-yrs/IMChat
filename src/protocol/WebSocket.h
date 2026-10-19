#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

// 浏览器接入用到的两样东西：HTTP 请求头的解析，和 WebSocket 协议（RFC 6455）的握手与分帧。
// 浏览器里的网页不能直接用 TCP 套接字，只能用 WebSocket：先发一个 HTTP 请求要求“升级”，服务端同意后，
// 这条 TCP 连接改为收发 WebSocket 消息。服务端在每条 WebSocket 消息里装一帧原来的协议帧（见 Frame.h），
// 拆开以后和原生客户端发来的字节流完全一样

struct HttpRequest {
    std::string method;
    std::string path;                             // 不含查询参数
    std::map<std::string, std::string> headers;   // 名称都转成小写

    static constexpr std::size_t kMaxHeaderSize = 8192;
    enum class ParseResult { NEED_MORE, OK, BAD };
    // 从 data 开头解析请求行和请求头（到空行为止），consumed 是它们占用的字节数。
    // 超过 kMaxHeaderSize 还没有结束、或者格式不对，返回 BAD
    static ParseResult parse(const char* data, std::size_t size, HttpRequest& request, std::size_t& consumed);

    std::string header(const std::string& lowerName) const;   // 没有这个请求头时返回空串
    bool isWebSocketUpgrade() const;   // GET，Upgrade: websocket，带 Sec-WebSocket-Key，版本 13
};

struct WebSocketFrame {
    enum class Opcode : std::uint8_t { CONTINUATION = 0, TEXT = 1, BINARY = 2, CLOSE = 8, PING = 9, PONG = 10 };

    bool fin = true;   // 消息的最后一片
    Opcode opcode = Opcode::BINARY;
    std::string payload;

    enum class DecodeResult { NEED_MORE, OK, BAD };
    // 解码客户端发来的一帧（去掉掩码）。协议规定客户端的帧必须加掩码，没加的、保留位不为 0 的、
    // 不认识的操作码、分片的或超过 125 字节的控制帧、载荷超过 maxPayload 的，都返回 BAD（应断开连接）
    static DecodeResult decode(const char* data, std::size_t size, std::size_t maxPayload, WebSocketFrame& frame,
                               std::size_t& consumed);
    // 编码服务端发出的一帧（服务端的帧不加掩码）
    static std::string encode(Opcode opcode, const char* data, std::size_t size);
    // 编码客户端发出的一帧（加掩码）：测试里模拟浏览器
    static std::string encodeMasked(Opcode opcode, const std::string& payload, std::uint32_t mask, bool fin = true);

    // 握手响应里的 Sec-WebSocket-Accept = Base64(SHA-1(客户端的 Sec-WebSocket-Key + 协议规定的固定 GUID))
    static std::string acceptKey(const std::string& clientKey);
};
