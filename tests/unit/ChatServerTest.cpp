#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <thread>
#include "ChatServer.h"
#include "Log.h"
#include "LoopThread.h"
#include "Platform.h"
#include "TestClient.h"

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using nlohmann::json;

namespace {

// 每个测试一份全新的演示数据，放在系统临时目录下（用户名含中文时，路径也含中文）
std::string freshDataDir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / "imchat_unit_tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    for (const auto& entry : fs::directory_iterator(fs::u8path(IMCHAT_SOURCE_DIR) / "data_demo"))
        fs::copy_file(entry.path(), dir / entry.path().filename());
    return dir.u8string() + "/";
}

std::string readText(const std::string& utf8Path) {
    std::ifstream in(fs::u8path(utf8Path), std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::size_t occurrences(const std::string& text, const std::string& part) {
    std::size_t count = 0;
    for (std::size_t pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + part.size())) ++count;
    return count;
}

json sendRequest(const std::string& clientMsgId, const std::string& to, const std::string& text, int reqId = 2) {
    return { { "reqId", reqId }, { "clientMsgId", clientMsgId }, { "to", to }, { "text", text } };
}

}  // namespace

class ChatServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Log::setLevel(Log::Level::WARN);
        dataDir_ = freshDataDir(::testing::UnitTest::GetInstance()->current_test_info()->name());
    }

    void TearDown() override { thread_.reset(); }

    void startServer(ChatServer::Options options = {}) {
        options.ip = "127.0.0.1";
        options.port = 0;
        thread_ = std::make_unique<LoopThread>(
            [this, options](EventLoop& loop) {
                platform_ = std::make_unique<Platform>(dataDir_);
                server_ = std::make_unique<ChatServer>(loop, *platform_, options);
                std::string error;
                if (!server_->start(error)) throw std::runtime_error(error);
                port_ = server_->port();
            },
            [this] {
                server_.reset();
                platform_.reset();
            });
    }

    std::size_t onlineCount() { return thread_->call([this] { return server_->onlineCount(); }); }

    std::string dataDir_;
    std::unique_ptr<Platform> platform_;
    std::unique_ptr<ChatServer> server_;
    std::unique_ptr<LoopThread> thread_;
    std::uint16_t port_ = 0;
};

TEST_F(ChatServerTest, AnswersPingWithoutLogin) {
    startServer();
    TestClient client(port_);
    client.send(MessageType::PING);
    std::optional<Packet> pong = client.receive();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->type, MessageType::PONG);
}

TEST_F(ChatServerTest, LoginChecksPassword) {
    startServer();
    TestClient client(port_);
    json wrong = client.login("QQ", "10001", "wrong");
    EXPECT_EQ(wrong["ok"], false);
    EXPECT_EQ(wrong["reqId"], 1);
    EXPECT_EQ(client.login("QQ", "99999")["ok"], false);
    EXPECT_EQ(client.login("ICQ", "10001")["ok"], false);
    json ok = client.login("QQ", "10001");
    EXPECT_EQ(ok["ok"], true);
    EXPECT_EQ(ok["nickname"], "小张");
    EXPECT_TRUE(ok.contains("unreadPrivate") && ok.contains("unreadGroup"));
    EXPECT_EQ(onlineCount(), 1u);
}

TEST_F(ChatServerTest, SendingRequiresLogin) {
    startServer();
    TestClient client(port_);
    client.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10002", "hi", 5));
    std::optional<Packet> error = client.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->type, MessageType::ERROR_RESP);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::NOT_LOGGED_IN));
    EXPECT_EQ(error->body["reqId"], 5);
}

TEST_F(ChatServerTest, MessageIsAckedAndPushedToOnlineFriend) {
    startServer();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);

    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10002", "晚上一起写服务端 💻"));
    std::optional<Packet> ack = zhang.receive();
    ASSERT_TRUE(ack);
    ASSERT_EQ(ack->type, MessageType::SEND_MSG_ACK);
    EXPECT_EQ(ack->body["ok"], true);
    EXPECT_EQ(ack->body["clientMsgId"], "m1");
    EXPECT_EQ(ack->body["reqId"], 2);
    ASSERT_TRUE(ack->body["seq"].is_number());

    std::optional<Packet> push = li.receive();
    ASSERT_TRUE(push);
    ASSERT_EQ(push->type, MessageType::MSG_PUSH);
    EXPECT_EQ(push->body["from"], "10001");
    EXPECT_EQ(push->body["text"], "晚上一起写服务端 💻");
    EXPECT_EQ(push->body["seq"], ack->body["seq"]);
    EXPECT_NE(readText(dataDir_ + "messages.txt").find("晚上一起写服务端 💻"), std::string::npos);   // 已持久化
}

TEST_F(ChatServerTest, RetriedMessageIsStoredAndPushedOnce) {
    // 客户端没收到回执时会用同一个 clientMsgId 重发：服务端只保存一次，回执相同，对方也只收到一次
    startServer();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("same-id", "10002", "只该出现一次", 2));
    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("same-id", "10002", "只该出现一次", 3));
    std::optional<Packet> first = zhang.receive(), second = zhang.receive();
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->body["seq"], second->body["seq"]);
    EXPECT_EQ(second->body["reqId"], 3);   // 回执内容相同，但 reqId 对应各自的请求
    ASSERT_TRUE(li.receive());
    EXPECT_FALSE(li.receive(300));
    EXPECT_EQ(occurrences(readText(dataDir_ + "messages.txt"), "只该出现一次"), 1u);
}

TEST_F(ChatServerTest, BusinessRuleFailuresComeBackInAck) {
    startServer();
    TestClient zhao(port_);
    ASSERT_EQ(zhao.login("QQ", "10004")["ok"], true);
    zhao.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10005", "你好"));   // 10004 和 10005 不是好友
    std::optional<Packet> ack = zhao.receive();
    ASSERT_TRUE(ack);
    EXPECT_EQ(ack->body["ok"], false);
    EXPECT_FALSE(ack->body["error"].get<std::string>().empty());
}

TEST_F(ChatServerTest, SecondLoginKicksFirstConnection) {
    startServer();
    TestClient first(port_), second(port_), li(port_);
    ASSERT_EQ(first.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(second.login("QQ", "10001")["ok"], true);
    std::optional<Packet> kicked = first.receive();
    ASSERT_TRUE(kicked);
    EXPECT_EQ(kicked->type, MessageType::KICKED);
    EXPECT_TRUE(first.closedByPeer());
    EXPECT_EQ(onlineCount(), 1u);

    // 新消息推送到新的连接
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    li.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10001", "推给新连接"));
    ASSERT_TRUE(li.receive());
    std::optional<Packet> push = second.receive();
    ASSERT_TRUE(push);
    EXPECT_EQ(push->body["text"], "推给新连接");
}

TEST_F(ChatServerTest, IdleConnectionsAreClosedButHeartbeatsKeepThemAlive) {
    ChatServer::Options options;
    options.idleTimeout = 300ms;
    options.idleCheckInterval = 50ms;
    startServer(options);
    TestClient silent(port_), alive(port_);
    for (int i = 0; i < 8; ++i) {   // 每 100ms 一次心跳，共 800ms，远超 300ms 的超时
        alive.send(MessageType::PING);
        ASSERT_TRUE(alive.receive());
        std::this_thread::sleep_for(100ms);
    }
    EXPECT_TRUE(silent.closedByPeer(500));
    alive.send(MessageType::PING);
    std::optional<Packet> pong = alive.receive();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->type, MessageType::PONG);
}

TEST_F(ChatServerTest, InvalidLengthClosesConnection) {
    startServer();
    TestClient client(port_);
    client.sendRaw(std::string("\xFF\xFF\xFF\xFF\x00\x01", 6));   // 声称有 4 GB
    EXPECT_TRUE(client.closedByPeer());
}

TEST_F(ChatServerTest, InvalidJsonGetsErrorAndConnectionStaysUsable) {
    startServer();
    TestClient client(port_);
    client.sendRaw(Frame::encode(MessageType::LOGIN_REQ, "{not json"));
    std::optional<Packet> error = client.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->type, MessageType::ERROR_RESP);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::BAD_REQUEST));
    client.send(MessageType::PING);
    std::optional<Packet> pong = client.receive();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->type, MessageType::PONG);
}

TEST_F(ChatServerTest, HandlesStickyAndHalfPacketsOverTheWire) {
    startServer();
    TestClient client(port_);
    Packet ping;
    ping.type = MessageType::PING;
    Packet login;
    login.type = MessageType::LOGIN_REQ;
    login.body = { { "reqId", 9 }, { "service", "QQ" }, { "account", "10001" }, { "password", "123456" } };
    const std::string third = ping.encode();
    // 两帧半一起发出（粘包），剩下的半帧过一会儿再发（半包）
    client.sendRaw(ping.encode() + login.encode() + third.substr(0, 3));
    std::this_thread::sleep_for(50ms);
    client.sendRaw(third.substr(3));
    std::optional<Packet> a = client.receive(), b = client.receive(), c = client.receive();
    ASSERT_TRUE(a && b && c);
    EXPECT_EQ(a->type, MessageType::PONG);
    EXPECT_EQ(b->type, MessageType::LOGIN_RESP);
    EXPECT_EQ(b->body["reqId"], 9);
    EXPECT_EQ(c->type, MessageType::PONG);
}

TEST_F(ChatServerTest, UnknownMessageTypeIsReported) {
    startServer();
    TestClient client(port_);
    client.sendRaw(Frame::encode(static_cast<MessageType>(4242), "{\"reqId\":3}"));
    std::optional<Packet> error = client.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::UNKNOWN_TYPE));
    EXPECT_EQ(error->body["reqId"], 3);
}
