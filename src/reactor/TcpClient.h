#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include "TcpConnection.h"

// TCP 客户端：非阻塞地连接服务端，连接失败或断开后自动重连。
// 重连间隔按指数退避（0.5s、1s、2s……最长 maxRetryDelay）：服务端宕机时，大量客户端不会一起高频重试把它刚恢复就压垮。
// 所有回调都在事件循环线程里执行；connect / disconnect 可以在任何线程调用
class TcpClient {
public:
    using ConnectionCallback = std::function<void(const TcpConnection::Ptr&)>;

    TcpClient(EventLoop& loop, std::string ip, std::uint16_t port);
    ~TcpClient();   // 必须在事件循环线程里销毁
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    void setConnectionCallback(ConnectionCallback callback);           // 连接成功
    void setDisconnectCallback(ConnectionCallback callback);           // 已建立的连接断开（随后会自动重连）
    void setMessageCallback(TcpConnection::MessageCallback callback);
    void setRetryDelay(std::chrono::milliseconds initial, std::chrono::milliseconds max);
    void setConnectTimeout(std::chrono::milliseconds timeout);

    void connect();      // 开始连接；之后一直保持连接（断了就重连），直到 disconnect
    void disconnect();   // 断开并停止重连

    TcpConnection::Ptr connection() const;   // 当前连接（未连上时为空），只能在事件循环线程里调用

private:
    void startConnect();
    void handleConnectEvent(unsigned events);
    void connectFailed(const std::string& reason);
    void cancelConnecting();
    void scheduleRetry();
    void handleClose(const TcpConnection::Ptr& connection);

    EventLoop& loop_;
    const std::string ip_;
    const std::uint16_t port_;
    bool active_ = false;   // connect() 之后、disconnect() 之前为 true
    SocketHandle connecting_ = kInvalidSocket;
    EventLoop::TimerId timeoutTimer_ = 0;
    EventLoop::TimerId retryTimer_ = 0;
    std::chrono::milliseconds initialDelay_{ 500 };
    std::chrono::milliseconds maxDelay_{ 30000 };
    std::chrono::milliseconds retryDelay_{ 500 };
    std::chrono::milliseconds connectTimeout_{ 5000 };
    std::uint64_t nextId_ = 1;
    TcpConnection::Ptr connection_;
    ConnectionCallback onConnection_;
    ConnectionCallback onDisconnect_;
    TcpConnection::MessageCallback onMessage_;
};
