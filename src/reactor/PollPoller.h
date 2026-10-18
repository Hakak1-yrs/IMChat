#pragma once
#include <unordered_map>
#include "Poller.h"

#ifndef _WIN32
#include <poll.h>
#endif

// 基于 poll 的实现。pollfd 数组和“套接字 → 下标”的映射配合使用：
// 修改关心的事件 O(1)；删除时把最后一项挪到被删的位置，也是 O(1)。
// 每次等待需要把整个数组交给内核，连接数很多时不如 epoll，但实现简单、各平台通用
class PollPoller : public Poller {
public:
    void update(SocketHandle socket, unsigned interest) override;
    void remove(SocketHandle socket) override;
    void poll(int timeoutMs, std::vector<Event>& active) override;

private:
#ifdef _WIN32
    using PollFd = WSAPOLLFD;
#else
    using PollFd = pollfd;
#endif
    std::vector<PollFd> fds_;
    std::unordered_map<SocketHandle, std::size_t> index_;
};
