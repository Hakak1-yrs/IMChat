#pragma once
#include <any>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include "Buffer.h"
#include "EventLoop.h"

// 一条 TCP 连接（非阻塞）。由 TcpServer 创建，用 shared_ptr 管理：上层可以先保存它，连接关闭后再释放也安全。
// 发送：先直接写套接字，写不完的部分放进输出缓冲区，等可写事件再继续写——调用 send 的线程永远不会被阻塞。
// 接收：读到的数据追加到输入缓冲区，交给上层按协议切分（上层没取走的半帧留在缓冲区里，等下次数据到来）
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using Ptr = std::shared_ptr<TcpConnection>;
    using MessageCallback = std::function<void(const Ptr&, Buffer&)>;
    using CloseCallback = std::function<void(const Ptr&)>;

    // 输出缓冲区超过这个大小，说明对方长时间不读数据（客户端卡死或者恶意不收），直接断开，避免服务端内存被撑爆
    static constexpr std::size_t kHighWaterMark = 8u << 20;   // 8 MiB

    TcpConnection(EventLoop& loop, SocketHandle socket, std::uint64_t id);
    ~TcpConnection();
    TcpConnection(const TcpConnection&) = delete;
    TcpConnection& operator=(const TcpConnection&) = delete;

    void start();                      // 开始接收数据（TcpServer 设置好回调后调用）
    void send(std::string data);       // 任何线程都可以调用
    void shutdownAfterFlush();         // 把已经要发的数据发完再关闭（如“你已在别处登录”的通知）
    void forceClose();                 // 立即关闭

    bool connected() const;
    std::uint64_t id() const;
    const std::string& peer() const;
    EventLoop& loop() const;
    std::size_t pendingOutput() const;           // 输出缓冲区中还没发出去的字节数
    std::chrono::steady_clock::time_point lastReceive() const;
    std::any& context();                         // 上层附加的数据（如登录会话）

    void setMessageCallback(MessageCallback callback);
    void setCloseCallback(CloseCallback callback);

private:
    enum class State { CONNECTED, CLOSING, CLOSED };

    void handleEvents(unsigned events);
    void handleRead();
    void handleWrite();
    void handleClose();
    void sendInLoop(const std::string& data);
    void updateInterest();

    EventLoop& loop_;
    SocketHandle socket_;
    const std::uint64_t id_;
    std::string peer_;
    State state_ = State::CONNECTED;
    Buffer input_;
    Buffer output_;
    bool watchingWrite_ = false;
    std::chrono::steady_clock::time_point lastReceive_;
    std::any context_;
    MessageCallback onMessage_;
    CloseCallback onClose_;
};
