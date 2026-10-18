#include <gtest/gtest.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include "LoopThread.h"
#include "TcpClient.h"
#include "TcpServer.h"

using namespace std::chrono_literals;

namespace {

// 找一个当前空闲的端口：先让系统分配一个，再关掉
std::uint16_t freePort() {
    std::uint16_t port = 0;
    std::string error;
    SocketHandle s = SocketOps::listenTcp("127.0.0.1", 0, 1, port, error);
    SocketOps::close(s);
    return port;
}

std::unique_ptr<LoopThread> echoServer(std::unique_ptr<TcpServer>& server, std::uint16_t port) {
    return std::make_unique<LoopThread>(
        [&server, port](EventLoop& loop) {
            server = std::make_unique<TcpServer>(loop);
            server->setMessageCallback([](const TcpConnection::Ptr& c, Buffer& b) { c->send(b.retrieveAllAsString()); });
            std::string error;
            if (!server->listen("127.0.0.1", port, error)) throw std::runtime_error(error);
        },
        [&server] { server.reset(); });
}

template <typename Condition>
bool eventually(Condition condition, std::chrono::milliseconds timeout = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) return true;
        std::this_thread::sleep_for(10ms);
    }
    return condition();
}

}  // namespace

TEST(TcpClientTest, KeepsRetryingUntilServerIsUpAndReconnectsAfterRestart) {
    const std::uint16_t port = freePort();
    std::atomic<int> connects{ 0 }, disconnects{ 0 };
    std::mutex mutex;
    std::string echoed;

    std::unique_ptr<TcpClient> client;
    LoopThread clientThread(
        [&](EventLoop& loop) {
            client = std::make_unique<TcpClient>(loop, "127.0.0.1", port);
            client->setRetryDelay(20ms, 100ms);
            client->setConnectionCallback([&](const TcpConnection::Ptr& c) {
                ++connects;
                c->send("hello");
            });
            client->setDisconnectCallback([&](const TcpConnection::Ptr&) { ++disconnects; });
            client->setMessageCallback([&](const TcpConnection::Ptr&, Buffer& b) {
                std::lock_guard<std::mutex> lock(mutex);
                echoed += b.retrieveAllAsString();
            });
            client->connect();
        },
        [&] { client.reset(); });

    std::this_thread::sleep_for(200ms);   // 服务端还没启动：一直连不上，但会按退避间隔不停重试
    EXPECT_EQ(connects, 0);

    std::unique_ptr<TcpServer> server;
    auto serverThread = echoServer(server, port);
    ASSERT_TRUE(eventually([&] { return connects == 1; }));
    ASSERT_TRUE(eventually([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return echoed == "hello";
    }));

    serverThread.reset();   // 服务端停机：客户端发现连接断开
    ASSERT_TRUE(eventually([&] { return disconnects == 1; }));
    serverThread = echoServer(server, port);   // 同一端口重新启动：客户端自动重连
    EXPECT_TRUE(eventually([&] { return connects == 2; }));
    EXPECT_TRUE(eventually([&] {
        std::lock_guard<std::mutex> lock(mutex);
        return echoed == "hellohello";
    }));
}

TEST(TcpClientTest, DisconnectStopsRetrying) {
    const std::uint16_t port = freePort();
    std::atomic<int> connects{ 0 };
    std::unique_ptr<TcpClient> client;
    LoopThread clientThread(
        [&](EventLoop& loop) {
            client = std::make_unique<TcpClient>(loop, "127.0.0.1", port);
            client->setRetryDelay(20ms, 50ms);
            client->setConnectionCallback([&](const TcpConnection::Ptr&) { ++connects; });
            client->connect();
        },
        [&] { client.reset(); });
    client->disconnect();   // 在别的线程调用：投递到事件循环里执行
    std::this_thread::sleep_for(100ms);
    std::unique_ptr<TcpServer> server;
    auto serverThread = echoServer(server, port);
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(connects, 0);
}
