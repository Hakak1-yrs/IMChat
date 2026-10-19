#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include "LoopThread.h"
#include "TcpServer.h"

#ifndef _WIN32
#include <sys/select.h>
#endif

using namespace std::chrono_literals;

namespace {

// 回显服务端：收到什么就发回什么。参数是 I/O 线程数：每个用例分别在单线程和多线程（one loop per thread）下运行
class EchoFixture : public ::testing::TestWithParam<int> {
protected:
    void SetUp() override {
        thread_ = std::make_unique<LoopThread>(
            [this](EventLoop& loop) {
                server_ = std::make_unique<TcpServer>(loop);
                server_->setThreadCount(GetParam());
                server_->setMessageCallback([this](const TcpConnection::Ptr& c, Buffer& b) {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        callbackThreads_.insert(std::this_thread::get_id());
                    }
                    c->send(b.retrieveAllAsString());
                });
                std::string error;
                if (!server_->listen("127.0.0.1", 0, error)) throw std::runtime_error(error);
                port_ = server_->port();
            },
            [this] { server_.reset(); });
    }
    void TearDown() override { thread_.reset(); }

    std::size_t connectionCount() { return thread_->call([this] { return server_->connectionCount(); }); }

    // 等待条件成立（最多 2 秒）
    template <typename Condition>
    bool eventually(Condition condition) {
        for (int i = 0; i < 200; ++i) {
            if (condition()) return true;
            std::this_thread::sleep_for(10ms);
        }
        return false;
    }

    std::unique_ptr<TcpServer> server_;
    std::unique_ptr<LoopThread> thread_;
    std::uint16_t port_ = 0;
    std::mutex mutex_;
    std::set<std::thread::id> callbackThreads_;   // 收到数据的回调在哪些线程里执行过
};

void sendAll(SocketHandle s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const long n = SocketOps::sendSome(s, data.data() + sent, data.size() - sent);
        ASSERT_GT(n, 0);
        sent += static_cast<std::size_t>(n);
    }
}

// 收满 size 字节，或超时
std::string receiveExactly(SocketHandle s, std::size_t size, std::chrono::milliseconds timeout) {
    std::string data;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    char chunk[64 * 1024];
    while (data.size() < size && std::chrono::steady_clock::now() < deadline) {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval wait{ 0, 100 * 1000 };
        if (select(static_cast<int>(s + 1), &set, nullptr, nullptr, &wait) <= 0) continue;
        const long n = SocketOps::recvSome(s, chunk, sizeof(chunk));
        if (n <= 0) break;
        data.append(chunk, static_cast<std::size_t>(n));
    }
    return data;
}

}  // namespace

TEST_P(EchoFixture, EchoesDataArrivingInPieces) {
    SocketHandle client = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(client, kInvalidSocket);
    sendAll(client, "hel");
    std::this_thread::sleep_for(20ms);
    sendAll(client, "lo, 世界");
    EXPECT_EQ(receiveExactly(client, std::string("hello, 世界").size(), 2s), "hello, 世界");
    SocketOps::close(client);
}

TEST_P(EchoFixture, LargePayloadSurvivesPartialWrites) {
    // 4 MiB 远超套接字缓冲区：服务端一次写不完，必须靠输出缓冲区和可写事件分多次发完，且顺序不乱
    std::string payload(4u << 20, '\0');
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>('a' + i % 26);
    SocketHandle client = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(client, kInvalidSocket);
    std::thread writer([&] { sendAll(client, payload); });   // 边发边收，否则两边的缓冲区都满了会互相等待
    const std::string echoed = receiveExactly(client, payload.size(), 20s);
    writer.join();
    EXPECT_EQ(echoed.size(), payload.size());
    EXPECT_TRUE(echoed == payload);
    SocketOps::close(client);
}

TEST_P(EchoFixture, ServerForgetsConnectionWhenPeerCloses) {
    SocketHandle client = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(client, kInvalidSocket);
    EXPECT_TRUE(eventually([&] { return connectionCount() == 1; }));
    SocketOps::close(client);
    EXPECT_TRUE(eventually([&] { return connectionCount() == 0; }));
}

TEST_P(EchoFixture, ManyClientsAtOnce) {
    const int kClients = 50;
    std::vector<SocketHandle> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.push_back(SocketOps::connectTcp("127.0.0.1", port_));
        ASSERT_NE(clients.back(), kInvalidSocket);
    }
    for (int i = 0; i < kClients; ++i) sendAll(clients[i], "client " + std::to_string(i));
    for (int i = 0; i < kClients; ++i) {
        const std::string expected = "client " + std::to_string(i);
        EXPECT_EQ(receiveExactly(clients[i], expected.size(), 2s), expected);
    }
    EXPECT_EQ(connectionCount(), static_cast<std::size_t>(kClients));
    for (SocketHandle c : clients) SocketOps::close(c);
}

TEST_P(EchoFixture, ConnectionsAreSpreadOverIoThreads) {
    const int kClients = 8;
    std::vector<SocketHandle> clients;
    for (int i = 0; i < kClients; ++i) {
        clients.push_back(SocketOps::connectTcp("127.0.0.1", port_));
        ASSERT_NE(clients.back(), kInvalidSocket);
        sendAll(clients.back(), "x");
        EXPECT_EQ(receiveExactly(clients.back(), 1, 2s), "x");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // 单线程：都在同一个线程；4 个 I/O 线程：8 条连接轮转分配，正好用到全部 4 个线程
    EXPECT_EQ(callbackThreads_.size(), GetParam() == 0 ? 1u : static_cast<std::size_t>(GetParam()));
    for (SocketHandle c : clients) SocketOps::close(c);
}

TEST_P(EchoFixture, ConnectionsBeyondTheLimitAreClosedImmediately) {
    thread_->call([this] {
        server_->setMaxConnections(2);
        return 0;
    });
    SocketHandle a = SocketOps::connectTcp("127.0.0.1", port_);
    SocketHandle b = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(a, kInvalidSocket);
    ASSERT_NE(b, kInvalidSocket);
    sendAll(a, "a");
    sendAll(b, "b");
    ASSERT_EQ(receiveExactly(a, 1, 2s), "a");
    ASSERT_EQ(receiveExactly(b, 1, 2s), "b");

    // 第三条：TCP 连接能建立（系统替服务端完成了握手），但服务端接受后立即关闭，对方读到 EOF
    SocketHandle c = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(c, kInvalidSocket);
    EXPECT_EQ(receiveExactly(c, 1, 2s), "");
    EXPECT_TRUE(eventually([&] { return thread_->call([this] { return server_->rejectedCount(); }) == 1; }));
    SocketOps::close(c);

    sendAll(a, "still");   // 已有的连接不受影响
    EXPECT_EQ(receiveExactly(a, 5, 2s), "still");
    SocketOps::close(b);   // 关掉一条后，又能接受新连接了
    EXPECT_TRUE(eventually([&] { return connectionCount() == 1; }));
    SocketHandle d = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(d, kInvalidSocket);
    sendAll(d, "d");
    EXPECT_EQ(receiveExactly(d, 1, 2s), "d");
    SocketOps::close(a);
    SocketOps::close(d);
}

INSTANTIATE_TEST_SUITE_P(IoThreads, EchoFixture, ::testing::Values(0, 4),
                         [](const ::testing::TestParamInfo<int>& info) { return info.param == 0 ? std::string("SingleLoop") : "FourIoThreads"; });