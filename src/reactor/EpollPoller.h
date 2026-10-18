#pragma once
#ifdef __linux__
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>
#include "Poller.h"

// 基于 epoll 的实现（Linux）。与 poll 不同，关心的套接字登记在内核里，每次等待不必把整个列表交给内核，
// 只返回就绪的套接字：连接数很多、但每轮只有少数活跃时，开销与连接总数无关。
// 使用水平触发（与 PollPoller 语义相同），事件循环和连接的代码不需要任何改动
class EpollPoller : public Poller {
public:
    EpollPoller();
    ~EpollPoller() override;
    EpollPoller(const EpollPoller&) = delete;
    EpollPoller& operator=(const EpollPoller&) = delete;

    void update(SocketHandle socket, unsigned interest) override;
    void remove(SocketHandle socket) override;
    void poll(int timeoutMs, std::vector<Event>& active) override;

private:
    int epoll_;
    std::unordered_map<SocketHandle, unsigned> registered_;   // 已登记的套接字及其关心的事件
    std::vector<epoll_event> events_;                          // epoll_wait 的输出，满了就加倍
};
#endif
