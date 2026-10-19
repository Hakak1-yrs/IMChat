#include <gtest/gtest.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include "Buffer.h"
#include "ChatClient.h"
#include "Frame.h"
#include "Log.h"
#include "LoopThread.h"
#include "TcpServer.h"
#include "TestServer.h"

using namespace std::chrono_literals;
using nlohmann::json;

namespace {

// 记录一个 ChatClient 收到的所有事件（回调在网络线程里执行，测试线程等待并检查）
class Recorder {
public:
    ChatClient::Callbacks callbacks() {
        ChatClient::Callbacks c;
        c.onState = [this](ChatClient::State s) { add([&] { states.push_back(s); }); };
        c.onLogin = [this](bool ok, const json&) { add([&] { logins.push_back(ok); }); };
        c.onMessage = [this](const json& m, bool history) { add([&] { messages.push_back({ m, history }); }); };
        c.onAck = [this](const json& a) { add([&] { acks.push_back(a); }); };
        c.onReadReceipt = [this](const json& r) { add([&] { receipts.push_back(r); }); };
        c.onKicked = [this](const std::string&) { add([&] { ++kicked; }); };
        return c;
    }

    // 等到条件成立（条件在锁内求值），超时返回 false
    template <typename Condition>
    bool waitFor(Condition condition, std::chrono::milliseconds timeout = 5s) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, timeout, [&] { return condition(); });
    }

    template <typename Read>
    auto read(Read r) {
        std::lock_guard<std::mutex> lock(mutex_);
        return r();
    }

    std::size_t countText(const std::string& text) {
        std::size_t n = 0;
        for (const auto& m : messages) n += m.first.value("text", "") == text ? 1 : 0;
        return n;
    }

    std::vector<ChatClient::State> states;
    std::vector<bool> logins;
    std::vector<std::pair<json, bool>> messages;
    std::vector<json> acks;
    std::vector<json> receipts;
    int kicked = 0;

private:
    template <typename F>
    void add(F f) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            f();
        }
        changed_.notify_all();
    }

    std::mutex mutex_;
    std::condition_variable changed_;
};

ChatClient::Options fastOptions() {
    ChatClient::Options options;
    options.heartbeat = 200ms;
    options.retryInitial = 20ms;
    options.retryMax = 200ms;
    return options;
}

}  // namespace

class ChatClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        Log::setLevel(Log::Level::WARN);
        dataDir_ = TestServer::freshDataDir(::testing::UnitTest::GetInstance()->current_test_info()->name());
        startServer(0);
    }

    void TearDown() override { server_.reset(); }

    void startServer(std::uint16_t port) {
        ChatServer::Options options;
        options.port = port;
        server_ = std::make_unique<TestServer>(dataDir_, options);
        port_ = server_->port();
    }

    // 登录并等待成功
    std::unique_ptr<ChatClient> loggedIn(Recorder& recorder, const std::string& account) {
        auto client = std::make_unique<ChatClient>(recorder.callbacks(), fastOptions());
        client->start("127.0.0.1", port_);
        client->login("QQ", account, "123456");
        EXPECT_TRUE(recorder.waitFor([&] { return !recorder.logins.empty() && recorder.logins.back(); }));
        return client;
    }

    std::string dataDir_;
    std::unique_ptr<TestServer> server_;
    std::uint16_t port_ = 0;
};

TEST_F(ChatClientTest, SendsAndReceivesInRealTime) {
    Recorder zhangEvents, liEvents;
    auto zhang = loggedIn(zhangEvents, "10001");
    auto li = loggedIn(liEvents, "10002");
    const std::string id = zhang->sendText("10002", "实时消息");
    ASSERT_TRUE(zhangEvents.waitFor([&] { return zhangEvents.acks.size() == 1; }));
    EXPECT_EQ(zhangEvents.read([&] { return zhangEvents.acks[0]["clientMsgId"]; }), id);
    EXPECT_EQ(zhangEvents.read([&] { return zhangEvents.acks[0]["ok"]; }), true);
    ASSERT_TRUE(liEvents.waitFor([&] { return liEvents.countText("实时消息") == 1; }));
    EXPECT_EQ(zhang->pendingCount(), 0u);
    const bool history = liEvents.read([&] {
        for (const auto& m : liEvents.messages)
            if (m.first.value("text", "") == "实时消息") return m.second;
        return true;
    });
    EXPECT_FALSE(history);   // 推送来的，不是同步来的
}

TEST_F(ChatClientTest, OfflineMessagesArriveAsHistoryOnLogin) {
    Recorder zhangEvents;
    auto zhang = loggedIn(zhangEvents, "10001");
    zhang->sendText("10002", "你不在线时发的");
    ASSERT_TRUE(zhangEvents.waitFor([&] { return zhangEvents.acks.size() == 1; }));

    Recorder liEvents;
    auto li = loggedIn(liEvents, "10002");
    ASSERT_TRUE(liEvents.waitFor([&] { return liEvents.countText("你不在线时发的") == 1; }));
    EXPECT_TRUE(liEvents.read([&] { return liEvents.messages.back().second; }));   // 标记为历史消息
}

TEST_F(ChatClientTest, MessagesSentWhileServerIsDownAreDeliveredExactlyOnce) {
    Recorder zhangEvents, liEvents;
    auto zhang = loggedIn(zhangEvents, "10001");
    auto li = loggedIn(liEvents, "10002");

    server_.reset();   // 服务端停机
    ASSERT_TRUE(zhangEvents.waitFor([&] { return zhangEvents.states.back() == ChatClient::State::CONNECTING; }));
    zhang->sendText("10002", "断线期间发的");   // 进入待确认队列
    EXPECT_EQ(zhang->pendingCount(), 1u);

    startServer(port_);   // 同一端口重启：两个客户端自动重连、重新登录；张三重发，李四同步
    ASSERT_TRUE(zhangEvents.waitFor([&] { return zhangEvents.acks.size() == 1; }));
    EXPECT_EQ(zhangEvents.read([&] { return zhangEvents.acks[0]["ok"]; }), true);
    ASSERT_TRUE(liEvents.waitFor([&] { return liEvents.countText("断线期间发的") == 1; }));
    std::this_thread::sleep_for(300ms);   // 再等一会儿，确认没有重复
    EXPECT_EQ(liEvents.read([&] { return liEvents.countText("断线期间发的"); }), 1u);
    EXPECT_EQ(zhangEvents.read([&] { return zhangEvents.acks.size(); }), 1u);
    EXPECT_EQ(zhang->pendingCount(), 0u);

    zhang.reset();
    li.reset();
    server_.reset();   // 落盘后检查：只保存了一次
    const std::string text = TestServer::readText(dataDir_ + "messages.txt");
    EXPECT_EQ(text.find("断线期间发的"), text.rfind("断线期间发的"));
}

TEST_F(ChatClientTest, ReadReceiptAndGroupMessages) {
    Recorder zhangEvents, liEvents;
    auto zhang = loggedIn(zhangEvents, "10001");
    auto li = loggedIn(liEvents, "10002");
    zhang->sendGroupText(1001, "群消息也走同一条通道");
    ASSERT_TRUE(liEvents.waitFor([&] { return liEvents.countText("群消息也走同一条通道") == 1; }));
    zhang->sendText("10002", "私信");
    ASSERT_TRUE(liEvents.waitFor([&] { return liEvents.countText("私信") == 1; }));
    li->markRead("10001", li->lastSeq());
    ASSERT_TRUE(zhangEvents.waitFor([&] { return zhangEvents.receipts.size() == 1; }));
    EXPECT_EQ(zhangEvents.read([&] { return zhangEvents.receipts[0]["reader"]; }), "10002");
}

// 分批同步的途中推送来一条更新的消息，然后断线：下次登录要从同步到的位置接着要，
// 而不是从推送来的那条（否则中间的消息永远收不到了）。
// 用按脚本应答的假服务端，才能精确地在这个时刻断线
TEST(ChatClientSyncTest, ResumesFromTheLastSyncedBatchNotFromANewerPush) {
    Log::setLevel(Log::Level::WARN);
    std::mutex mutex;
    std::vector<int> firstSyncs;   // 每次登录后第一次同步请求的 afterSeq（测试线程读取）
    int logins = 0;                // 以下只在服务端线程里访问
    bool awaitingFirstSync = false;
    bool roundDone = false;
    std::unique_ptr<TcpServer> server;
    std::uint16_t port = 0;
    auto reply = [](const TcpConnection::Ptr& c, MessageType type, json body) {
        Packet packet;
        packet.type = type;
        packet.body = std::move(body);
        c->send(packet.encode());
    };
    auto message = [](int seq) {
        return json{ { "seq", seq }, { "service", "QQ" }, { "from", "10001" }, { "to", "10002" }, { "text", "第 " + std::to_string(seq) + " 条" },
                     { "time", "2026-10-18 12:00:00" } };
    };
    auto onPacket = [&](const TcpConnection::Ptr& c, const Packet& packet) {
        json body = { { "reqId", packet.body.value("reqId", 0) } };
        if (packet.type == MessageType::LOGIN_REQ) {
            ++logins;
            awaitingFirstSync = true;
            roundDone = false;
            body["ok"] = true;
            body["token"] = "token";
            reply(c, MessageType::LOGIN_RESP, body);
            return;
        }
        if (packet.type == MessageType::PING && roundDone) {   // 这一轮的推送已经发出：断开，让客户端重连
            c->shutdownAfterFlush();
            return;
        }
        if (packet.type != MessageType::SYNC_REQ) return;
        const int afterSeq = packet.body.value("afterSeq", 0);
        if (!awaitingFirstSync) {   // 第一轮中途的第二批请求：不回答，直接断开（推送已经在它之前送到）
            c->shutdownAfterFlush();
            return;
        }
        awaitingFirstSync = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            firstSyncs.push_back(afterSeq);
        }
        if (logins == 1) {          // 第一批只给第 1 条，说还有；紧接着推送一条新消息 10
            body["messages"] = json::array({ message(1) });
            body["hasMore"] = true;
            reply(c, MessageType::SYNC_RESP, body);
            reply(c, MessageType::MSG_PUSH, message(10));
        } else if (logins == 2) {   // 补齐 2～9，同步完成；之后又推送一条 11
            json messages = json::array();
            for (int seq = 2; seq <= 9; ++seq) messages.push_back(message(seq));
            body["messages"] = messages;
            body["hasMore"] = false;
            reply(c, MessageType::SYNC_RESP, body);
            reply(c, MessageType::MSG_PUSH, message(11));
            roundDone = true;
        } else {
            body["messages"] = json::array();
            body["hasMore"] = false;
            reply(c, MessageType::SYNC_RESP, body);
        }
    };
    LoopThread thread(
        [&](EventLoop& loop) {
            server = std::make_unique<TcpServer>(loop);
            server->setMessageCallback([&](const TcpConnection::Ptr& c, Buffer& buffer) {
                Frame frame;
                std::size_t consumed = 0;
                while (Frame::decode(buffer.peek(), buffer.readableBytes(), frame, consumed) == Frame::DecodeResult::OK) {
                    buffer.retrieve(consumed);
                    if (std::optional<Packet> packet = Packet::fromFrame(frame)) onPacket(c, *packet);
                }
            });
            std::string error;
            if (!server->listen("127.0.0.1", 0, error)) throw std::runtime_error(error);
            port = server->port();
        },
        [&] { server.reset(); });

    Recorder events;
    ChatClient client(events.callbacks(), fastOptions());
    client.start("127.0.0.1", port);
    client.login("QQ", "10002", "123456");
    // 1～11 全部收到
    ASSERT_TRUE(events.waitFor([&] { return events.messages.size() == 11; }));
    for (int i = 0; i < 100; ++i) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (firstSyncs.size() >= 3) break;
        }
        std::this_thread::sleep_for(20ms);
    }
    client.stop();
    std::lock_guard<std::mutex> lock(mutex);
    // 第一次从头；第二次从第一批的末尾 1（不是推送来的 10）；第三次同步完成后，推送来的 11 之前都齐了
    ASSERT_GE(firstSyncs.size(), 3u);
    EXPECT_EQ(firstSyncs[0], 0);
    EXPECT_EQ(firstSyncs[1], 1);
    EXPECT_EQ(firstSyncs[2], 11);
}

TEST_F(ChatClientTest, KickedClientDoesNotReconnect) {
    Recorder firstEvents, secondEvents;
    auto first = loggedIn(firstEvents, "10001");
    auto second = loggedIn(secondEvents, "10001");
    ASSERT_TRUE(firstEvents.waitFor([&] { return firstEvents.kicked == 1; }));
    std::this_thread::sleep_for(500ms);   // 远超重连间隔：被挤下线的一方不能自动连回来把对方挤掉
    EXPECT_EQ(first->state(), ChatClient::State::DISCONNECTED);
    EXPECT_EQ(second->state(), ChatClient::State::LOGGED_IN);
    EXPECT_EQ(secondEvents.read([&] { return secondEvents.kicked; }), 0);
}
