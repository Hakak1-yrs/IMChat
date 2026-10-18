#include "TcpConnection.h"
#include "Log.h"

TcpConnection::TcpConnection(EventLoop& loop, SocketHandle socket, std::uint64_t id)
    : loop_(loop), socket_(socket), id_(id), peer_(SocketOps::peerAddress(socket)),
      lastReceive_(std::chrono::steady_clock::now()) {}

TcpConnection::~TcpConnection() {
    // 正常情况下已经在 handleClose 里关闭；这里兜底（比如服务端整体停止时连接还没断开）
    if (socket_ != kInvalidSocket) {
        if (loop_.isInLoopThread()) loop_.unwatch(socket_);
        SocketOps::close(socket_);
    }
}

void TcpConnection::start() {
    std::weak_ptr<TcpConnection> weak = shared_from_this();   // 用弱引用：事件循环不应该延长连接的寿命
    loop_.watch(socket_, IO_READ, [weak](unsigned events) {
        if (Ptr self = weak.lock()) self->handleEvents(events);
    });
}

void TcpConnection::send(std::string data) {
    if (loop_.isInLoopThread()) {
        sendInLoop(data.data(), data.size());
        return;
    }
    Ptr self = shared_from_this();
    loop_.queueInLoop([self, data = std::move(data)] { self->sendInLoop(data.data(), data.size()); });
}

void TcpConnection::send(const char* data, std::size_t size) {
    if (loop_.isInLoopThread()) sendInLoop(data, size);
    else send(std::string(data, size));   // 跨线程：复制一份带过去
}

void TcpConnection::shutdownAfterFlush() {
    Ptr self = shared_from_this();
    loop_.runInLoop([self] {
        if (self->state_ != State::CONNECTED) return;
        if (self->output_.readableBytes() == 0) self->handleClose();
        else self->state_ = State::CLOSING;   // 等 handleWrite 把缓冲区写空后关闭
    });
}

void TcpConnection::forceClose() {
    Ptr self = shared_from_this();
    loop_.runInLoop([self] {
        if (self->state_ != State::CLOSED) self->handleClose();
    });
}

bool TcpConnection::connected() const { return state_ == State::CONNECTED; }

std::uint64_t TcpConnection::id() const { return id_; }

const std::string& TcpConnection::peer() const { return peer_; }

EventLoop& TcpConnection::loop() const { return loop_; }

std::size_t TcpConnection::pendingOutput() const { return output_.readableBytes(); }

std::chrono::steady_clock::time_point TcpConnection::lastReceive() const { return lastReceive_.load(); }

std::any& TcpConnection::context() { return context_; }

void TcpConnection::setMessageCallback(MessageCallback callback) { onMessage_ = std::move(callback); }

void TcpConnection::setCloseCallback(CloseCallback callback) { onClose_ = std::move(callback); }

void TcpConnection::handleEvents(unsigned events) {
    Ptr self = shared_from_this();   // 处理过程中可能触发关闭、上层释放连接，先保住自己
    if (events & IO_ERROR) {
        handleClose();
        return;
    }
    if (events & IO_READ) handleRead();
    if ((events & IO_WRITE) && state_ != State::CLOSED) handleWrite();
}

void TcpConnection::handleRead() {
    char buffer[64 * 1024];
    bool peerClosed = false;
    while (true) {
        const long n = SocketOps::recvSome(socket_, buffer, sizeof(buffer));
        if (n > 0) {
            input_.append(buffer, static_cast<std::size_t>(n));
            if (static_cast<std::size_t>(n) < sizeof(buffer)) break;   // 没读满：内核缓冲区大概率已经空了
            continue;
        }
        if (n == 0) {
            peerClosed = true;
            break;
        }
        const int error = SocketOps::lastError();
        if (SocketOps::interrupted(error)) continue;
        if (SocketOps::wouldBlock(error)) break;
        handleClose();   // 连接被重置等错误
        return;
    }
    lastReceive_ = std::chrono::steady_clock::now();
    // 对方发完数据后立即关闭时，先把已收到的数据交给上层，再关闭
    if (input_.readableBytes() > 0 && state_ == State::CONNECTED && onMessage_) onMessage_(shared_from_this(), input_);
    if (peerClosed && state_ != State::CLOSED) handleClose();
}

void TcpConnection::handleWrite() {
    while (output_.readableBytes() > 0) {
        const long n = SocketOps::sendSome(socket_, output_.peek(), output_.readableBytes());
        if (n > 0) {
            output_.retrieve(static_cast<std::size_t>(n));
            continue;
        }
        const int error = SocketOps::lastError();
        if (n < 0 && SocketOps::interrupted(error)) continue;
        if (n < 0 && SocketOps::wouldBlock(error)) break;   // 内核发送缓冲区又满了，等下一次可写
        handleClose();
        return;
    }
    if (output_.readableBytes() == 0 && state_ == State::CLOSING) {
        handleClose();
        return;
    }
    updateInterest();
}

void TcpConnection::sendInLoop(const char* data, std::size_t size) {
    if (state_ != State::CONNECTED) return;
    // 先放进输出缓冲区，本轮事件处理完之后再统一写：同一轮里发给这条连接的多条消息（比如群消息的推送）
    // 合并成一次 send 系统调用。压测中群消息的吞吐因此明显提高，见 docs/BENCHMARK.md
    output_.append(data, size);
    if (output_.readableBytes() > kHighWaterMark) {
        Log::warn("连接 " + peer_ + " 的待发送数据超过上限，对方可能已停止接收，断开连接");
        handleClose();
        return;
    }
    if (flushQueued_ || watchingWrite_) return;   // 已经安排了写；或者正在等可写事件，到时会一起写出去
    flushQueued_ = true;
    Ptr self = shared_from_this();
    loop_.queueInLoop([self] {
        self->flushQueued_ = false;
        if (self->state_ != State::CLOSED) self->handleWrite();
    });
}

void TcpConnection::updateInterest() {
    const bool needWrite = output_.readableBytes() > 0;
    if (needWrite == watchingWrite_) return;
    watchingWrite_ = needWrite;
    // 只在有数据要发时才关心可写事件：套接字几乎总是可写的，一直关心会让 poll 不停地返回
    loop_.setInterest(socket_, IO_READ | (needWrite ? IO_WRITE : 0u));
}

void TcpConnection::handleClose() {
    if (state_ == State::CLOSED) return;
    state_ = State::CLOSED;
    loop_.unwatch(socket_);
    SocketOps::close(socket_);
    socket_ = kInvalidSocket;
    input_.retrieveAll();
    output_.retrieveAll();
    if (onClose_) onClose_(shared_from_this());
}
