#include "TcpClient.h"
#include <algorithm>
#include "Log.h"

TcpClient::TcpClient(EventLoop& loop, std::string ip, std::uint16_t port) : loop_(loop), ip_(std::move(ip)), port_(port) {}

TcpClient::~TcpClient() {
    active_ = false;
    cancelConnecting();
    if (retryTimer_ != 0) loop_.cancel(retryTimer_);
    if (connection_) {
        connection_->setCloseCallback(nullptr);   // 自己正在销毁，不要再回调
        connection_->forceClose();
    }
}

void TcpClient::setConnectionCallback(ConnectionCallback callback) { onConnection_ = std::move(callback); }

void TcpClient::setDisconnectCallback(ConnectionCallback callback) { onDisconnect_ = std::move(callback); }

void TcpClient::setMessageCallback(TcpConnection::MessageCallback callback) { onMessage_ = std::move(callback); }

void TcpClient::setRetryDelay(std::chrono::milliseconds initial, std::chrono::milliseconds max) {
    initialDelay_ = retryDelay_ = initial;
    maxDelay_ = max;
}

void TcpClient::setConnectTimeout(std::chrono::milliseconds timeout) { connectTimeout_ = timeout; }

void TcpClient::connect() {
    loop_.runInLoop([this] {
        if (active_) return;
        active_ = true;
        retryDelay_ = initialDelay_;
        startConnect();
    });
}

void TcpClient::disconnect() {
    loop_.runInLoop([this] {
        active_ = false;
        cancelConnecting();
        if (retryTimer_ != 0) loop_.cancel(retryTimer_);
        retryTimer_ = 0;
        if (connection_) connection_->forceClose();   // 触发 handleClose：active_ 已为 false，不会重连
    });
}

TcpConnection::Ptr TcpClient::connection() const { return connection_; }

void TcpClient::startConnect() {
    retryTimer_ = 0;
    if (!active_ || connection_ || connecting_ != kInvalidSocket) return;
    bool inProgress = false;
    SocketHandle socket = SocketOps::startConnect(ip_, port_, inProgress);
    if (socket == kInvalidSocket) {
        connectFailed("无法连接 " + ip_ + ":" + std::to_string(port_));
        return;
    }
    connecting_ = socket;
    // 连接的结果通过“可写”（成功）或“出错”报告；有的系统失败时只报可读或挂断，所以读写都关心
    loop_.watch(socket, IO_WRITE | IO_READ, [this](unsigned events) { handleConnectEvent(events); });
    // 有些情况下失败不会产生任何事件（比如对方主机不存在、数据包被丢弃），靠超时兜底
    timeoutTimer_ = loop_.runAfter(connectTimeout_, [this] {
        timeoutTimer_ = 0;
        connectFailed("连接 " + ip_ + ":" + std::to_string(port_) + " 超时");
    });
}

void TcpClient::handleConnectEvent(unsigned events) {
    SocketHandle socket = connecting_;
    const int error = SocketOps::socketError(socket);
    if (error != 0 || (events & IO_ERROR) || !(events & IO_WRITE)) {
        connectFailed("连接 " + ip_ + ":" + std::to_string(port_) + " 失败：" + SocketOps::errorText(error));
        return;
    }
    // 连接成功：从“正在连接”转交给 TcpConnection 管理
    loop_.unwatch(socket);
    connecting_ = kInvalidSocket;
    if (timeoutTimer_ != 0) loop_.cancel(timeoutTimer_);
    timeoutTimer_ = 0;
    retryDelay_ = initialDelay_;
    connection_ = std::make_shared<TcpConnection>(loop_, socket, nextId_++);
    connection_->setMessageCallback(onMessage_);
    connection_->setCloseCallback([this](const TcpConnection::Ptr& c) { handleClose(c); });
    connection_->start();
    if (onConnection_) onConnection_(connection_);
}

void TcpClient::connectFailed(const std::string& reason) {
    Log::debug(reason);
    cancelConnecting();
    scheduleRetry();
}

void TcpClient::cancelConnecting() {
    if (timeoutTimer_ != 0) loop_.cancel(timeoutTimer_);
    timeoutTimer_ = 0;
    if (connecting_ == kInvalidSocket) return;
    loop_.unwatch(connecting_);
    SocketOps::close(connecting_);
    connecting_ = kInvalidSocket;
}

void TcpClient::scheduleRetry() {
    if (!active_ || retryTimer_ != 0) return;
    retryTimer_ = loop_.runAfter(retryDelay_, [this] { startConnect(); });
    retryDelay_ = std::min(retryDelay_ * 2, maxDelay_);
}

void TcpClient::handleClose(const TcpConnection::Ptr& connection) {
    if (connection != connection_) return;
    connection_.reset();
    if (onDisconnect_) onDisconnect_(connection);
    scheduleRetry();
}
