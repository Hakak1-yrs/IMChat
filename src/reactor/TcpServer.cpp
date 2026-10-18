#include "TcpServer.h"
#include "Log.h"

TcpServer::TcpServer(EventLoop& loop) : loop_(loop) {}

TcpServer::~TcpServer() {
    if (listener_ != kInvalidSocket) {
        loop_.unwatch(listener_);
        SocketOps::close(listener_);
    }
    for (auto& entry : connections_) entry.second->setCloseCallback(nullptr);   // 服务端在销毁，不再回调
    connections_.clear();
}

void TcpServer::setConnectionCallback(ConnectionCallback callback) { onConnection_ = std::move(callback); }

void TcpServer::setMessageCallback(TcpConnection::MessageCallback callback) { onMessage_ = std::move(callback); }

void TcpServer::setCloseCallback(TcpConnection::CloseCallback callback) { onClose_ = std::move(callback); }

bool TcpServer::listen(const std::string& ip, std::uint16_t port, std::string& error) {
    listener_ = SocketOps::listenTcp(ip, port, SOMAXCONN, port_, error);
    if (listener_ == kInvalidSocket) return false;
    loop_.watch(listener_, IO_READ, [this](unsigned) { handleAccept(); });
    return true;
}

std::uint16_t TcpServer::port() const { return port_; }

std::size_t TcpServer::connectionCount() const { return connections_.size(); }

void TcpServer::handleAccept() {
    // 一次可读事件可能对应多个排队的新连接：一直 accept 到“暂时没有了”为止
    while (true) {
        SocketHandle socket = accept(listener_, nullptr, nullptr);
        if (socket == kInvalidSocket) {
            const int error = SocketOps::lastError();
            if (SocketOps::interrupted(error)) continue;
            if (!SocketOps::wouldBlock(error)) Log::warn("accept 失败：" + SocketOps::errorText(error));
            return;
        }
        if (!SocketOps::setNonBlocking(socket)) {
            SocketOps::close(socket);
            continue;
        }
        SocketOps::setNoDelay(socket);
        auto connection = std::make_shared<TcpConnection>(loop_, socket, nextId_++);
        connection->setMessageCallback(onMessage_);
        connection->setCloseCallback([this](const TcpConnection::Ptr& c) { removeConnection(c); });
        connections_[connection->id()] = connection;
        if (onConnection_) onConnection_(connection);
        connection->start();
    }
}

void TcpServer::removeConnection(const TcpConnection::Ptr& connection) {
    if (onClose_) onClose_(connection);
    connections_.erase(connection->id());   // 调用者手里还有一份 shared_ptr，连接对象要等这次回调结束后才销毁
}
