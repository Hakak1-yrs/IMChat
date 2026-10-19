#include "TcpServer.h"
#include <future>
#include <map>
#include "EventLoopThread.h"
#include "Log.h"

TcpServer::TcpServer(EventLoop& loop) : loop_(loop) {}

TcpServer::~TcpServer() { stop(); }

void TcpServer::setConnectionCallback(ConnectionCallback callback) { onConnection_ = std::move(callback); }

void TcpServer::setMessageCallback(TcpConnection::MessageCallback callback) { onMessage_ = std::move(callback); }

void TcpServer::setCloseCallback(TcpConnection::CloseCallback callback) { onClose_ = std::move(callback); }

void TcpServer::setThreadCount(int count) { threadCount_ = count > 0 ? count : 0; }

void TcpServer::setMaxConnections(std::size_t count) { maxConnections_ = count; }

std::uint64_t TcpServer::rejectedCount() const { return rejected_; }

bool TcpServer::listen(const std::string& ip, std::uint16_t port, std::string& error) {
    listener_ = SocketOps::listenTcp(ip, port, SOMAXCONN, port_, error);
    if (listener_ == kInvalidSocket) return false;
    for (int i = 0; i < threadCount_; ++i) threads_.push_back(std::make_unique<EventLoopThread>());
    loop_.watch(listener_, IO_READ, [this](unsigned) { handleAccept(); });
    return true;
}

void TcpServer::stop() {
    if (listener_ != kInvalidSocket) {
        loop_.unwatch(listener_);
        SocketOps::close(listener_);
        listener_ = kInvalidSocket;
    }
    // 在每条连接自己的线程里关闭它（不再回调上层），全部关完之后才能停 I/O 线程：
    // 连接对象引用着所属的事件循环，必须赶在事件循环销毁之前处理完
    std::map<EventLoop*, std::vector<TcpConnection::Ptr>> byLoop;
    for (auto& entry : connections_) byLoop[&entry.second->loop()].push_back(entry.second);
    connections_.clear();
    for (auto& [owner, connections] : byLoop) {
        auto closeAll = [&connections] {
            for (const TcpConnection::Ptr& c : connections) {
                c->setCloseCallback(nullptr);
                c->forceClose();
            }
        };
        if (owner == &loop_) {
            closeAll();
            continue;
        }
        std::promise<void> done;
        owner->runInLoop([&closeAll, &done] {
            closeAll();
            done.set_value();
        });
        done.get_future().wait();
    }
    byLoop.clear();
    threads_.clear();   // 让 I/O 线程的事件循环退出，并等线程结束
}

std::uint16_t TcpServer::port() const { return port_; }

std::size_t TcpServer::connectionCount() const { return connections_.size(); }

EventLoop& TcpServer::nextLoop() {
    if (threads_.empty()) return loop_;
    EventLoop& loop = threads_[nextThread_]->loop();
    nextThread_ = (nextThread_ + 1) % threads_.size();
    return loop;
}

void TcpServer::handleAccept() {
    // 一次可读事件可能对应多个排队的新连接：一直 accept 到“暂时没有了”为止
    while (true) {
        SocketHandle socket = accept(listener_, nullptr, nullptr);
        if (socket == kInvalidSocket) {
            const int error = SocketOps::lastError();
            if (SocketOps::interrupted(error)) continue;
            if (!SocketOps::wouldBlock(error)) {
                // 典型的是文件描述符用完（EMFILE）：那条连接还排在队列里，监听套接字一直可读，
                // 立即返回的话下一轮又会失败，事件循环空转、日志刷屏
                Log::warn("accept 失败：" + SocketOps::errorText(error) + "，暂停 100 毫秒");
                pauseAccepting();
            }
            return;
        }
        if (maxConnections_ != 0 && connections_.size() >= maxConnections_) {   // 满了：接受后立即关闭，对方马上知道连不上
            SocketOps::close(socket);
            if (rejected_++ % 1000 == 0) Log::warn("连接数已达上限 " + std::to_string(maxConnections_) + "，拒绝新连接");
            continue;
        }
        if (!SocketOps::setNonBlocking(socket)) {
            SocketOps::close(socket);
            continue;
        }
        SocketOps::setNoDelay(socket);
        EventLoop& ioLoop = nextLoop();   // 轮转分配：这条连接此后一直由这个线程负责
        auto connection = std::make_shared<TcpConnection>(ioLoop, socket, nextId_++);
        connection->setMessageCallback(onMessage_);
        connection->setCloseCallback([this](const TcpConnection::Ptr& c) { removeConnection(c); });
        connections_[connection->id()] = connection;
        // 在连接所属的线程里通知上层并开始收数据（只有一个线程时就是立即执行）
        ioLoop.runInLoop([this, connection] {
            if (onConnection_) onConnection_(connection);
            connection->start();
        });
    }
}

void TcpServer::pauseAccepting() {
    if (acceptPaused_) return;
    acceptPaused_ = true;
    loop_.unwatch(listener_);
    std::weak_ptr<char> alive = alive_;
    loop_.runAfter(std::chrono::milliseconds(100), [this, alive] {
        if (!alive.lock()) return;
        acceptPaused_ = false;
        if (listener_ != kInvalidSocket) loop_.watch(listener_, IO_READ, [this](unsigned) { handleAccept(); });
    });
}

void TcpServer::removeConnection(const TcpConnection::Ptr& connection) {
    // 在连接所属的线程里调用；连接表属于主循环线程，删除要转过去做
    if (onClose_) onClose_(connection);
    std::weak_ptr<char> alive = alive_;   // 转过去的任务执行时，服务端可能已经销毁了
    loop_.runInLoop([this, alive, connection] {
        if (alive.lock()) connections_.erase(connection->id());
    });
}
