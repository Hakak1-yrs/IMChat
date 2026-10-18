#include "EpollPoller.h"
#ifdef __linux__
#include <unistd.h>
#include <cstdint>
#include <stdexcept>

namespace {

std::uint32_t toEpollEvents(unsigned interest) {
    std::uint32_t events = 0;
    if (interest & IO_READ) events |= EPOLLIN | EPOLLRDHUP;
    if (interest & IO_WRITE) events |= EPOLLOUT;
    return events;
}

}  // namespace

EpollPoller::EpollPoller() : epoll_(epoll_create1(EPOLL_CLOEXEC)), events_(64) {
    if (epoll_ < 0) throw std::runtime_error("epoll_create1 失败");
}

EpollPoller::~EpollPoller() { ::close(epoll_); }

void EpollPoller::update(SocketHandle socket, unsigned interest) {
    epoll_event event{};
    event.events = toEpollEvents(interest);
    event.data.fd = socket;
    auto it = registered_.find(socket);
    if (it == registered_.end()) {
        if (epoll_ctl(epoll_, EPOLL_CTL_ADD, socket, &event) == 0) registered_[socket] = interest;
    } else if (it->second != interest) {
        if (epoll_ctl(epoll_, EPOLL_CTL_MOD, socket, &event) == 0) it->second = interest;
    }
}

void EpollPoller::remove(SocketHandle socket) {
    if (registered_.erase(socket) == 0) return;
    // 套接字关闭时内核会自动把它从 epoll 中去掉；这里在关闭之前调用（见 TcpConnection::handleClose），显式删除
    epoll_event unused{};
    epoll_ctl(epoll_, EPOLL_CTL_DEL, socket, &unused);
}

void EpollPoller::poll(int timeoutMs, std::vector<Event>& active) {
    const int ready = epoll_wait(epoll_, events_.data(), static_cast<int>(events_.size()), timeoutMs);
    if (ready <= 0) return;   // 超时，或被信号打断（下一轮重试即可）
    for (int i = 0; i < ready; ++i) {
        const std::uint32_t revents = events_[i].events;
        unsigned events = 0;
        // 对方关闭（EPOLLRDHUP / EPOLLHUP）按可读处理：读到 0 字节就知道连接已关闭，与 PollPoller 一致
        if (revents & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) events |= IO_READ;
        if (revents & EPOLLOUT) events |= IO_WRITE;
        if (revents & EPOLLERR) events |= IO_ERROR;
        active.push_back({ events_[i].data.fd, events });
    }
    if (ready == static_cast<int>(events_.size())) events_.resize(events_.size() * 2);   // 输出数组满了：下一轮多取一些
}
#endif
