#include <gtest/gtest.h>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>
#include "ChatClient.h"
#include "Log.h"
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
