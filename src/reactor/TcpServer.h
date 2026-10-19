#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "TcpConnection.h"

class EventLoopThread;

// TCP 服务端：监听端口、接受新连接，并持有所有连接，直到它们关闭。
//
// 线程：默认只有一个事件循环，接受连接和所有连接的收发都在它里面。
// setThreadCount(n) 之后另开 n 个 I/O 线程（每个线程一个事件循环，one loop per thread）：
// 主循环只负责接受连接，新连接按轮转分给某个 I/O 线程，此后这条连接的读写、回调都在那个线程里执行。
// 连接的建立、收到数据、关闭三个回调在连接所属的线程里调用
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
    void setThreadCount(int count);                                     // I/O 线程数，必须在 listen 之前设置
    // 连接数上限（0 表示不限）：满了以后新连接接受后立即关闭，而不是一直排在系统的队列里。
    // 防止大量连接（恶意的或者客户端出错）耗尽文件描述符和内存
    void setMaxConnections(std::size_t count);

    bool listen(const std::string& ip, std::uint16_t port, std::string& error);   // port 为 0 时由系统分配
    // 关闭所有连接（不再回调）并停止 I/O 线程；在主循环线程里调用。析构时会自动调用
    void stop();
    std::uint16_t port() const;
    std::size_t connectionCount() const;
    std::uint64_t rejectedCount() const;   // 因为连接数已满而拒绝的连接数（主循环线程里读）

private:
    void handleAccept();
    void pauseAccepting();   // accept 持续出错（如文件描述符用完）：暂停一会儿再接，避免事件循环空转
    void removeConnection(const TcpConnection::Ptr& connection);
    EventLoop& nextLoop();

    EventLoop& loop_;
    SocketHandle listener_ = kInvalidSocket;
    std::uint16_t port_ = 0;
    std::uint64_t nextId_ = 1;
    int threadCount_ = 0;
    std::size_t maxConnections_ = 0;
    std::uint64_t rejected_ = 0;
    bool acceptPaused_ = false;
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::size_t nextThread_ = 0;
    std::unordered_map<std::uint64_t, TcpConnection::Ptr> connections_;   // 只在主循环线程里访问
    ConnectionCallback onConnection_;
    TcpConnection::MessageCallback onMessage_;
    TcpConnection::CloseCallback onClose_;
    std::shared_ptr<char> alive_ = std::make_shared<char>();   // 投递到主循环的任务用它判断服务端是否还在
};
