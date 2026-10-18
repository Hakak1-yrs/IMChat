#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include "TcpConnection.h"

// TCP 服务端：监听端口、接受新连接，并持有所有连接，直到它们关闭。
// 所有回调都在事件循环线程里执行
class TcpServer {
public:
    using ConnectionCallback = std::function<void(const TcpConnection::Ptr&)>;

    explicit TcpServer(EventLoop& loop);
    ~TcpServer();
    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    void setConnectionCallback(ConnectionCallback callback);           // 新连接建立
    void setMessageCallback(TcpConnection::MessageCallback callback);  // 收到数据
    void setCloseCallback(TcpConnection::CloseCallback callback);      // 连接关闭

    bool listen(const std::string& ip, std::uint16_t port, std::string& error);   // port 为 0 时由系统分配
    std::uint16_t port() const;
    std::size_t connectionCount() const;

private:
    void handleAccept();
    void removeConnection(const TcpConnection::Ptr& connection);

    EventLoop& loop_;
    SocketHandle listener_ = kInvalidSocket;
    std::uint16_t port_ = 0;
    std::uint64_t nextId_ = 1;
    std::unordered_map<std::uint64_t, TcpConnection::Ptr> connections_;
    ConnectionCallback onConnection_;
    TcpConnection::MessageCallback onMessage_;
    TcpConnection::CloseCallback onClose_;
};
