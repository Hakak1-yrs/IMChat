#include <gtest/gtest.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include "LoopThread.h"
#include "TcpServer.h"

#ifndef _WIN32
#include <sys/select.h>
#endif

using namespace std::chrono_literals;

namespace {

// 回显服务端：收到什么就发回什么
class EchoFixture : public ::testing::Test {
protected:
    void SetUp() override {
        thread_ = std::make_unique<LoopThread>(
            [this](EventLoop& loop) {
                server_ = std::make_unique<TcpServer>(loop);
                server_->setMessageCallback([](const TcpConnection::Ptr& c, Buffer& b) { c->send(b.retrieveAllAsString()); });
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

TEST_F(EchoFixture, EchoesDataArrivingInPieces) {
    SocketHandle client = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(client, kInvalidSocket);
    sendAll(client, "hel");
    std::this_thread::sleep_for(20ms);
    sendAll(client, "lo, 世界");
    EXPECT_EQ(receiveExactly(client, std::string("hello, 世界").size(), 2s), "hello, 世界");
    SocketOps::close(client);
}

TEST_F(EchoFixture, LargePayloadSurvivesPartialWrites) {
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

TEST_F(EchoFixture, ServerForgetsConnectionWhenPeerCloses) {
    SocketHandle client = SocketOps::connectTcp("127.0.0.1", port_);
    ASSERT_NE(client, kInvalidSocket);
    EXPECT_TRUE(eventually([&] { return connectionCount() == 1; }));
    SocketOps::close(client);
    EXPECT_TRUE(eventually([&] { return connectionCount() == 0; }));
}

TEST_F(EchoFixture, ManyClientsAtOnce) {
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
