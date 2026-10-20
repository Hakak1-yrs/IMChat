#include "WebTransport.h"
#include <algorithm>
#include <any>
#include "Frame.h"
#include "Log.h"
#include "WebFiles.h"
#include "WebSocket.h"

namespace {

using Opcode = WebSocketFrame::Opcode;

// 一条 WebSocket 消息最多装一帧协议帧；分片的消息合起来也不能超过它
constexpr std::size_t kMaxMessage = Frame::kHeaderSize + Frame::kMaxBodySize;

struct WireState {
    enum class Kind { UNKNOWN, NATIVE, HTTP, WEBSOCKET, CLOSING };
    Kind kind = Kind::UNKNOWN;
    bool websocket = false;   // 握手成功后为 true，关闭途中也保持（之后的发送仍要按 WebSocket 打包）
    Buffer stream;          // WebSocket 消息拆开后的协议帧字节流
    std::string partial;    // 分片消息已经收到的部分
    bool fragmented = false;
};

WireState* stateOf(const TcpConnection::Ptr& connection) { return std::any_cast<WireState>(&connection->context()); }

bool isWebSocket(const TcpConnection::Ptr& connection) {
    const WireState* state = stateOf(connection);
    return state && state->websocket;
}

std::string httpResponse(const char* status, const char* contentType, const char* body, std::size_t size) {
    std::string out = std::string("HTTP/1.1 ") + status + "\r\nContent-Type: " + contentType +
                      "\r\nContent-Length: " + std::to_string(size) + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n";
    out.append(body, size);
    return out;
}

void replyHttp(const TcpConnection::Ptr& connection, WireState& state, const char* status, const std::string& text) {
    connection->send(httpResponse(status, "text/plain; charset=utf-8", text.data(), text.size()));
    connection->shutdownAfterFlush();
    state.kind = WireState::Kind::CLOSING;
}

// 按协议关闭：先发关闭帧（带状态码），发完再断开
void closeWebSocket(const TcpConnection::Ptr& connection, WireState& state, int code) {
    const char payload[2] = { static_cast<char>((code >> 8) & 0xFF), static_cast<char>(code & 0xFF) };
    connection->send(WebSocketFrame::encode(Opcode::CLOSE, payload, sizeof(payload)));
    connection->shutdownAfterFlush();
    state.kind = WireState::Kind::CLOSING;
}

// 处理 HTTP 请求。返回 true 表示已经升级成 WebSocket（input 里剩下的数据是 WebSocket 帧）
bool handleHttp(const TcpConnection::Ptr& connection, WireState& state, Buffer& input) {
    HttpRequest request;
    std::size_t consumed = 0;
    const HttpRequest::ParseResult result = HttpRequest::parse(input.peek(), input.readableBytes(), request, consumed);
    if (result == HttpRequest::ParseResult::NEED_MORE) return false;
    if (result == HttpRequest::ParseResult::BAD) {
        input.retrieveAll();
        replyHttp(connection, state, "400 Bad Request", "Bad Request");
        return false;
    }
    input.retrieve(consumed);
    if (request.isWebSocketUpgrade()) {
        connection->send("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                         WebSocketFrame::acceptKey(request.header("sec-websocket-key")) + "\r\n\r\n");
        state.kind = WireState::Kind::WEBSOCKET;
        state.websocket = true;
        Log::debug("连接 " + connection->peer() + " 升级为 WebSocket");
        return true;
    }
    input.retrieveAll();   // 每个连接只回答一个请求（Connection: close），后面的数据不再理会
    const WebFile* file = request.method == "GET" ? findWebFile(request.path) : nullptr;
    if (file) {   // 网页客户端的页面、样式、脚本
        connection->send(httpResponse("200 OK", file->contentType, reinterpret_cast<const char*>(file->data), file->size));
        connection->shutdownAfterFlush();
        state.kind = WireState::Kind::CLOSING;
    } else if (request.path == "/ws") {
        replyHttp(connection, state, "426 Upgrade Required", "请用 WebSocket 连接这个地址");
    } else {
        replyHttp(connection, state, "404 Not Found", "Not Found");
    }
    return false;
}

// 拆开 WebSocket 帧：数据消息的载荷追加到 state.stream，控制帧就地处理。出错时按协议关闭，返回 false
bool unwrapFrames(const TcpConnection::Ptr& connection, WireState& state, Buffer& input) {
    while (true) {
        WebSocketFrame frame;
        std::size_t consumed = 0;
        const WebSocketFrame::DecodeResult result =
            WebSocketFrame::decode(input.peek(), input.readableBytes(), kMaxMessage, frame, consumed);
        if (result == WebSocketFrame::DecodeResult::NEED_MORE) return true;
        if (result == WebSocketFrame::DecodeResult::BAD) {
            Log::warn("连接 " + connection->peer() + " 发来的 WebSocket 帧不合法，断开");
            input.retrieveAll();
            closeWebSocket(connection, state, 1002);   // 1002：违反协议
            return false;
        }
        input.retrieve(consumed);
        switch (frame.opcode) {
        case Opcode::PING:
            connection->send(WebSocketFrame::encode(Opcode::PONG, frame.payload.data(), frame.payload.size()));
            continue;
        case Opcode::PONG:
            continue;
        case Opcode::CLOSE:   // 对方要关闭：回一个关闭帧（带回对方的状态码），发完断开
            connection->send(WebSocketFrame::encode(Opcode::CLOSE, frame.payload.data(), std::min<std::size_t>(frame.payload.size(), 2)));
            connection->shutdownAfterFlush();
            state.kind = WireState::Kind::CLOSING;
            input.retrieveAll();
            return false;
        case Opcode::TEXT:   // 协议帧是二进制的，网页客户端只发二进制消息
            input.retrieveAll();
            closeWebSocket(connection, state, 1003);   // 1003：不支持的数据类型
            return false;
        default:
            break;
        }
        // 二进制消息，或者分片消息的后续部分
        const bool continuation = frame.opcode == Opcode::CONTINUATION;
        if (continuation != state.fragmented || state.partial.size() + frame.payload.size() > kMaxMessage) {
            input.retrieveAll();
            closeWebSocket(connection, state, continuation != state.fragmented ? 1002 : 1009);   // 1009：消息太大
            return false;
        }
        if (!frame.fin) {
            state.partial += frame.payload;
            state.fragmented = true;
            continue;
        }
        if (state.fragmented) {
            state.partial += frame.payload;
            state.stream.append(state.partial);
            state.partial.clear();
            state.fragmented = false;
        } else {
            state.stream.append(frame.payload);
        }
    }
}

}  // namespace

Buffer* WebTransport::unwrap(const TcpConnection::Ptr& connection, Buffer& input) {
    std::any& context = connection->context();
    if (!context.has_value()) context = WireState{};
    WireState& state = *std::any_cast<WireState>(&context);
    if (state.kind == WireState::Kind::UNKNOWN) {
        state.kind = input.peek()[0] == 'G' ? WireState::Kind::HTTP : WireState::Kind::NATIVE;
    }
    switch (state.kind) {
    case WireState::Kind::NATIVE:
        return &input;
    case WireState::Kind::HTTP:
        if (!handleHttp(connection, state, input)) return nullptr;
        [[fallthrough]];   // 刚升级成 WebSocket：浏览器可能紧跟着握手请求就发了第一条消息
    case WireState::Kind::WEBSOCKET:
        unwrapFrames(connection, state, input);
        // 出错时也把出错之前收全的消息交出去处理；连接已经在关闭，回复发不出去也没有关系
        return state.stream.readableBytes() > 0 ? &state.stream : nullptr;
    default:   // 正在关闭：不再处理收到的数据
        input.retrieveAll();
        return nullptr;
    }
}

void WebTransport::send(const TcpConnection::Ptr& connection, std::string bytes) {
    if (isWebSocket(connection)) connection->send(WebSocketFrame::encode(Opcode::BINARY, bytes.data(), bytes.size()));
    else connection->send(std::move(bytes));
}

void WebTransport::send(const TcpConnection::Ptr& connection, const char* data, std::size_t size) {
    if (isWebSocket(connection)) connection->send(WebSocketFrame::encode(Opcode::BINARY, data, size));
    else connection->send(data, size);
}
