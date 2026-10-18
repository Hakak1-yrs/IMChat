#pragma once
#include <memory>
#include <vector>
#include "SocketOps.h"

// 关心 / 发生的 I/O 事件，可以按位组合
enum IoEvent : unsigned {
    IO_READ = 1,    // 可读（包括对方关闭连接：此时读到 0 字节）
    IO_WRITE = 2,   // 可写
    IO_ERROR = 4,   // 出错（只出现在返回的事件里）
};

// I/O 多路复用的抽象接口，事件循环只依赖这个接口。
// 现在的实现是 PollPoller（Windows 的 WSAPoll 与 POSIX 的 poll 用法相同，一份代码两边通用）；
// Linux 上可以再实现 EpollPoller，事件循环不需要任何改动
class Poller {
public:
    struct Event {
        SocketHandle socket;
        unsigned events;
    };

    virtual ~Poller() = default;
    virtual void update(SocketHandle socket, unsigned interest) = 0;   // 新增，或修改关心的事件
    virtual void remove(SocketHandle socket) = 0;
    // 等待事件，最多 timeoutMs 毫秒（-1 表示一直等）；就绪的套接字追加到 active
    virtual void poll(int timeoutMs, std::vector<Event>& active) = 0;

    static std::unique_ptr<Poller> create();   // 当前平台默认的实现
};
