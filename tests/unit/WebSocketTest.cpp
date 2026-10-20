#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <optional>
#include <utility>
#include "Log.h"
#include "TestClient.h"
#include "TestServer.h"
#include "WebSocket.h"

#ifndef _WIN32
#include <sys/select.h>
#endif

using nlohmann::json;
using Opcode = WebSocketFrame::Opcode;

// ---------------- 编解码 ----------------

TEST(WebSocketCodecTest, AcceptKeyMatchesRfcExample) {
    // RFC 6455 第 1.3 节的例子
    EXPECT_EQ(WebSocketFrame::acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(WebSocketCodecTest, ParsesUpgradeRequest) {
    const std::string text =
        "GET /ws?x=1 HTTP/1.1\r\nHost: 192.168.1.5:9000\r\nUpgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\nextra";
    HttpRequest request;
    std::size_t consumed = 0;
    ASSERT_EQ(HttpRequest::parse(text.data(), text.size(), request, consumed), HttpRequest::ParseResult::OK);
    EXPECT_EQ(consumed, text.size() - 5);   // 请求头后面的数据不算
    EXPECT_EQ(request.method, "GET");
    EXPECT_EQ(request.path, "/ws");
    EXPECT_EQ(request.header("host"), "192.168.1.5:9000");
    EXPECT_TRUE(request.isWebSocketUpgrade());

    // 每次只多给一个字节：没到空行之前都是 NEED_MORE
    for (std::size_t n = 0; n + 5 < text.size(); ++n)
        EXPECT_EQ(HttpRequest::parse(text.data(), n, request, consumed), HttpRequest::ParseResult::NEED_MORE) << n;
}

TEST(WebSocketCodecTest, RejectsMalformedOrHugeRequests) {
    HttpRequest request;
    std::size_t consumed = 0;
    const std::string noVersion = "GET /\r\n\r\n";
    EXPECT_EQ(HttpRequest::parse(noVersion.data(), noVersion.size(), request, consumed), HttpRequest::ParseResult::BAD);
    const std::string badHeader = "GET / HTTP/1.1\r\nno colon here\r\n\r\n";
    EXPECT_EQ(HttpRequest::parse(badHeader.data(), badHeader.size(), request, consumed), HttpRequest::ParseResult::BAD);
    const std::string huge = "GET / HTTP/1.1\r\nX: " + std::string(HttpRequest::kMaxHeaderSize, 'a');
    EXPECT_EQ(HttpRequest::parse(huge.data(), huge.size(), request, consumed), HttpRequest::ParseResult::BAD);
    const std::string plainGet = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
    ASSERT_EQ(HttpRequest::parse(plainGet.data(), plainGet.size(), request, consumed), HttpRequest::ParseResult::OK);
    EXPECT_FALSE(request.isWebSocketUpgrade());
}

TEST(WebSocketCodecTest, DecodesMaskedFramesOfEveryLengthForm) {
    for (std::size_t size : { 0u, 5u, 125u, 126u, 300u, 65535u, 65536u, 70000u }) {
        std::string payload(size, '\0');
        for (std::size_t i = 0; i < size; ++i) payload[i] = static_cast<char>(i * 7);
        const std::string bytes = WebSocketFrame::encodeMasked(Opcode::BINARY, payload, 0x12345678);
        WebSocketFrame frame;
        std::size_t consumed = 0;
        ASSERT_EQ(WebSocketFrame::decode(bytes.data(), bytes.size(), 1 << 20, frame, consumed), WebSocketFrame::DecodeResult::OK) << size;
        EXPECT_EQ(consumed, bytes.size());
        EXPECT_EQ(frame.payload, payload);
        EXPECT_TRUE(frame.fin);
        EXPECT_EQ(frame.opcode, Opcode::BINARY);
        // 缺最后一个字节：还要等
        EXPECT_EQ(WebSocketFrame::decode(bytes.data(), bytes.size() - 1, 1 << 20, frame, consumed), WebSocketFrame::DecodeResult::NEED_MORE);
    }
}

TEST(WebSocketCodecTest, RejectsFramesThatBreakTheRules) {
    WebSocketFrame frame;
    std::size_t consumed = 0;
    const auto decode = [&](const std::string& bytes, std::size_t max = 1 << 20) {
        return WebSocketFrame::decode(bytes.data(), bytes.size(), max, frame, consumed);
    };
    const std::string unmasked = WebSocketFrame::encode(Opcode::BINARY, "hi", 2);   // 客户端的帧必须加掩码
    EXPECT_EQ(decode(unmasked), WebSocketFrame::DecodeResult::BAD);
    EXPECT_EQ(decode(WebSocketFrame::encodeMasked(Opcode::PING, "x", 1, false)), WebSocketFrame::DecodeResult::BAD);   // 分片的控制帧
    EXPECT_EQ(decode(WebSocketFrame::encodeMasked(Opcode::PING, std::string(126, 'x'), 1)), WebSocketFrame::DecodeResult::BAD);
    std::string reserved = WebSocketFrame::encodeMasked(Opcode::BINARY, "x", 1);
    reserved[0] = static_cast<char>(reserved[0] | 0x40);   // 扩展位
    EXPECT_EQ(decode(reserved), WebSocketFrame::DecodeResult::BAD);
    std::string unknown = WebSocketFrame::encodeMasked(Opcode::BINARY, "x", 1);
    unknown[0] = static_cast<char>(0x83);   // 操作码 3 未定义
    EXPECT_EQ(decode(unknown), WebSocketFrame::DecodeResult::BAD);
    // 声称 2^63 字节：只看帧头就拒绝，不等数据
    const std::string huge = std::string("\x82\xFF\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF", 10);
    EXPECT_EQ(decode(huge), WebSocketFrame::DecodeResult::BAD);
    EXPECT_EQ(decode(WebSocketFrame::encodeMasked(Opcode::BINARY, std::string(101, 'x'), 1), 100), WebSocketFrame::DecodeResult::BAD);
}

// ---------------- 服务端 ----------------

namespace {

// 模拟浏览器：HTTP 握手，然后收发 WebSocket 帧（发出的帧加掩码，收到的不带掩码）
class BrowserClient {
public:
    explicit BrowserClient(std::uint16_t port) : socket_(SocketOps::connectTcp("127.0.0.1", port)) {}
    ~BrowserClient() { SocketOps::close(socket_); }
    BrowserClient(const BrowserClient&) = delete;
    BrowserClient& operator=(const BrowserClient&) = delete;

    void sendRaw(const std::string& bytes) {
        for (std::size_t sent = 0; sent < bytes.size();) {
            const long n = SocketOps::sendSome(socket_, bytes.data() + sent, bytes.size() - sent);
            if (n <= 0) return;
            sent += static_cast<std::size_t>(n);
        }
    }

    // 读到对方关闭或超时为止（回复网页后服务端会关闭连接）
    std::string readAll(int timeoutMs = 2000) {
        while (fill(timeoutMs)) {}
        return std::exchange(buffer_, std::string());
    }

    // 握手（trailing 紧跟在请求后面一起发出），返回服务端的响应头
    std::string handshake(const std::string& trailing = "") {
        sendRaw("GET /ws HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n" + trailing);
        while (buffer_.find("\r\n\r\n") == std::string::npos)
            if (!fill(2000)) return "";
        const std::size_t end = buffer_.find("\r\n\r\n") + 4;
        std::string head = buffer_.substr(0, end);
        buffer_.erase(0, end);
        return head;
    }

    void send(MessageType type, const json& body) {
        Packet packet;
        packet.type = type;
        packet.body = body;
        sendRaw(WebSocketFrame::encodeMasked(Opcode::BINARY, packet.encode(), 0xA1B2C3D4));
    }

    // 下一个 WebSocket 帧
    std::optional<WebSocketFrame> receiveFrame(int timeoutMs = 2000) {
        while (true) {
            if (buffer_.size() >= 2) {
                const unsigned b1 = static_cast<unsigned char>(buffer_[1]) & 0x7F;
                const std::size_t headerSize = b1 == 126 ? 4 : b1 == 127 ? 10 : 2;
                if (buffer_.size() >= headerSize) {
                    std::uint64_t length = b1;
                    if (headerSize > 2) {
                        length = 0;
                        for (std::size_t i = 2; i < headerSize; ++i) length = (length << 8) | static_cast<unsigned char>(buffer_[i]);
                    }
                    if (buffer_.size() >= headerSize + length) {
                        WebSocketFrame frame;
                        frame.fin = (buffer_[0] & 0x80) != 0;
                        frame.opcode = static_cast<Opcode>(buffer_[0] & 0x0F);
                        frame.payload = buffer_.substr(headerSize, static_cast<std::size_t>(length));
                        buffer_.erase(0, headerSize + static_cast<std::size_t>(length));
                        return frame;
                    }
                }
            }
            if (!fill(timeoutMs)) return std::nullopt;
        }
    }

    // 下一条协议消息（跳过好友上下线的推送）：每条 WebSocket 消息里正好是一帧协议帧
    std::optional<Packet> receive(int timeoutMs = 2000) {
        while (std::optional<WebSocketFrame> frame = receiveFrame(timeoutMs)) {
            EXPECT_EQ(frame->opcode, Opcode::BINARY);
            Frame inner;
            std::size_t consumed = 0;
            if (Frame::decode(frame->payload.data(), frame->payload.size(), inner, consumed) != Frame::DecodeResult::OK) return std::nullopt;
            EXPECT_EQ(consumed, frame->payload.size());
            std::optional<Packet> packet = Packet::fromFrame(inner);
            if (packet && packet->type == MessageType::PRESENCE) continue;
            return packet;
        }
        return std::nullopt;
    }

private:
    bool fill(int timeoutMs) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(socket_, &set);
        timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        if (select(static_cast<int>(socket_ + 1), &set, nullptr, nullptr, &timeout) <= 0) return false;
        char chunk[4096];
        const long n = SocketOps::recvSome(socket_, chunk, sizeof(chunk));
        if (n <= 0) return false;
        buffer_.append(chunk, static_cast<std::size_t>(n));
        return true;
    }

    SocketHandle socket_;
    std::string buffer_;
};

json loginRequest(const std::string& account) {
    return { { "reqId", 1 }, { "service", "QQ" }, { "account", account }, { "password", "123456" } };
}

}  // namespace

class WebSocketServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Log::setLevel(Log::Level::WARN);
        server_ = std::make_unique<TestServer>(
            TestServer::freshDataDir(std::string("ws_") + ::testing::UnitTest::GetInstance()->current_test_info()->name()),
            ChatServer::Options{});
        port_ = server_->port();
    }

    std::unique_ptr<TestServer> server_;
    std::uint16_t port_ = 0;
};

TEST_F(WebSocketServerTest, ServesTheWebClientPage) {
    BrowserClient browser(port_);
    browser.sendRaw("GET / HTTP/1.1\r\nHost: localhost\r\nAccept: text/html\r\n\r\n");
    const std::string response = browser.readAll();
    EXPECT_EQ(response.rfind("HTTP/1.1 200 OK\r\n", 0), 0u) << response.substr(0, 200);
    EXPECT_NE(response.find("text/html; charset=utf-8"), std::string::npos);
    EXPECT_NE(response.find("js/app.js"), std::string::npos);   // 确实是网页客户端
    const std::size_t body = response.find("\r\n\r\n") + 4;
    EXPECT_NE(response.find("Content-Length: " + std::to_string(response.size() - body) + "\r\n"), std::string::npos);

    // 页面引用的脚本、样式也由服务端提供，类型正确
    BrowserClient script(port_);
    script.sendRaw("GET /js/net.js HTTP/1.1\r\nHost: localhost\r\n\r\n");
    const std::string js = script.readAll();
    EXPECT_EQ(js.rfind("HTTP/1.1 200 OK\r\n", 0), 0u);
    EXPECT_NE(js.find("text/javascript"), std::string::npos);
    EXPECT_NE(js.find("new WebSocket("), std::string::npos);
    BrowserClient style(port_);
    style.sendRaw("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_NE(style.readAll().find("text/css"), std::string::npos);
}

TEST_F(WebSocketServerTest, UnknownPathGets404AndPlainHttpOnWsGets426) {
    BrowserClient a(port_);
    a.sendRaw("GET /secret.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(a.readAll().rfind("HTTP/1.1 404", 0), 0u);
    BrowserClient b(port_);
    b.sendRaw("GET /ws HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(b.readAll().rfind("HTTP/1.1 426", 0), 0u);
    BrowserClient c(port_);
    c.sendRaw("GARBAGE\r\n\r\n");
    EXPECT_EQ(c.readAll().rfind("HTTP/1.1 400", 0), 0u);
}

TEST_F(WebSocketServerTest, HandshakeThenChatsWithANativeClient) {
    BrowserClient phone(port_);
    const std::string head = phone.handshake();
    EXPECT_EQ(head.rfind("HTTP/1.1 101 Switching Protocols\r\n", 0), 0u) << head;
    EXPECT_NE(head.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"), std::string::npos);

    phone.send(MessageType::LOGIN_REQ, loginRequest("10002"));
    std::optional<Packet> login = phone.receive();
    ASSERT_TRUE(login);
    EXPECT_EQ(login->type, MessageType::LOGIN_RESP);
    EXPECT_EQ(login->body["ok"], true);

    TestClient pc(port_);   // 电脑上的原生客户端，和网页客户端在同一个端口上
    ASSERT_EQ(pc.login("QQ", "10001")["ok"], true);

    pc.send(MessageType::SEND_MSG_REQ, { { "reqId", 2 }, { "clientMsgId", "pc-1" }, { "to", "10002" }, { "text", "电脑发给手机" } });
    std::optional<Packet> ack = pc.receive();
    ASSERT_TRUE(ack);
    EXPECT_EQ(ack->body["ok"], true);
    std::optional<Packet> push = phone.receive();
    ASSERT_TRUE(push);
    EXPECT_EQ(push->type, MessageType::MSG_PUSH);
    EXPECT_EQ(push->body["text"], "电脑发给手机");

    phone.send(MessageType::SEND_MSG_REQ, { { "reqId", 3 }, { "clientMsgId", "web-1" }, { "to", "10001" }, { "text", "手机回复电脑" } });
    std::optional<Packet> phoneAck = phone.receive();
    ASSERT_TRUE(phoneAck);
    EXPECT_EQ(phoneAck->type, MessageType::SEND_MSG_ACK);
    EXPECT_EQ(phoneAck->body["reqId"], 3);
    EXPECT_EQ(phoneAck->body["ok"], true);
    std::optional<Packet> pcPush = pc.receive();
    ASSERT_TRUE(pcPush);
    EXPECT_EQ(pcPush->body["text"], "手机回复电脑");
}

TEST_F(WebSocketServerTest, FirstMessageMayArriveWithTheHandshakeAndInPieces) {
    BrowserClient phone(port_);
    Packet ping;
    ping.type = MessageType::PING;
    // 握手请求后面紧跟一帧
    const std::string head = phone.handshake(WebSocketFrame::encodeMasked(Opcode::BINARY, ping.encode(), 7));
    ASSERT_EQ(head.rfind("HTTP/1.1 101", 0), 0u);
    // 一条协议帧拆成三个 WebSocket 分片，分片之间插入一个控制帧
    Packet login;
    login.type = MessageType::LOGIN_REQ;
    login.body = loginRequest("10003");
    const std::string bytes = login.encode();
    phone.sendRaw(WebSocketFrame::encodeMasked(Opcode::BINARY, bytes.substr(0, 3), 1, false) +
                  WebSocketFrame::encodeMasked(Opcode::PING, "hb", 2) +
                  WebSocketFrame::encodeMasked(Opcode::CONTINUATION, bytes.substr(3, 10), 3, false) +
                  WebSocketFrame::encodeMasked(Opcode::CONTINUATION, bytes.substr(13), 4, true));

    // 协议的 PONG 由业务线程回复，WebSocket 的 pong 由 I/O 线程直接回复：多线程时两者先后不定
    bool protocolPong = false, socketPong = false;
    for (int i = 0; i < 2; ++i) {
        std::optional<WebSocketFrame> frame = phone.receiveFrame();
        ASSERT_TRUE(frame);
        if (frame->opcode == Opcode::PONG) {
            EXPECT_EQ(frame->payload, "hb");   // 带回 ping 的载荷
            socketPong = true;
        } else {
            Frame inner;
            std::size_t consumed = 0;
            ASSERT_EQ(Frame::decode(frame->payload.data(), frame->payload.size(), inner, consumed), Frame::DecodeResult::OK);
            EXPECT_EQ(inner.type, MessageType::PONG);
            protocolPong = true;
        }
    }
    EXPECT_TRUE(protocolPong && socketPong);
    std::optional<Packet> response = phone.receive();
    ASSERT_TRUE(response);
    EXPECT_EQ(response->type, MessageType::LOGIN_RESP);
    EXPECT_EQ(response->body["ok"], true);
}
TEST_F(WebSocketServerTest, CloseAndProtocolErrorsEndTheConnection) {
    BrowserClient a(port_);
    ASSERT_FALSE(a.handshake().empty());
    a.sendRaw(WebSocketFrame::encodeMasked(Opcode::CLOSE, std::string("\x03\xE8", 2), 9));   // 1000：正常关闭
    std::optional<WebSocketFrame> reply = a.receiveFrame();
    ASSERT_TRUE(reply);
    EXPECT_EQ(reply->opcode, Opcode::CLOSE);
    EXPECT_EQ(reply->payload, std::string("\x03\xE8", 2));
    EXPECT_FALSE(a.receiveFrame(500));   // 然后断开

    BrowserClient b(port_);
    ASSERT_FALSE(b.handshake().empty());
    b.sendRaw(WebSocketFrame::encode(Opcode::BINARY, "x", 1));   // 没加掩码
    std::optional<WebSocketFrame> error = b.receiveFrame();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->opcode, Opcode::CLOSE);
    EXPECT_EQ(error->payload, std::string("\x03\xEA", 2));   // 1002：违反协议

    BrowserClient c(port_);
    ASSERT_FALSE(c.handshake().empty());
    c.sendRaw(WebSocketFrame::encodeMasked(Opcode::TEXT, "{}", 5));   // 只收二进制消息
    std::optional<WebSocketFrame> text = c.receiveFrame();
    ASSERT_TRUE(text);
    EXPECT_EQ(text->payload, std::string("\x03\xEB", 2));   // 1003
}

TEST_F(WebSocketServerTest, KickedWebClientGetsTheNoticeInsideAWebSocketFrame) {
    BrowserClient phone(port_);
    ASSERT_FALSE(phone.handshake().empty());
    phone.send(MessageType::LOGIN_REQ, loginRequest("10004"));
    ASSERT_TRUE(phone.receive());
    TestClient pc(port_);
    ASSERT_EQ(pc.login("QQ", "10004")["ok"], true);   // 同一账号在电脑上登录，把手机挤下线
    std::optional<Packet> kicked = phone.receive();
    ASSERT_TRUE(kicked);
    EXPECT_EQ(kicked->type, MessageType::KICKED);
}
