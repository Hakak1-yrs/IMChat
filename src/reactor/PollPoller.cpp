#include "PollPoller.h"

namespace {

short toPollEvents(unsigned interest) {
    short events = 0;
    if (interest & IO_READ) events |= POLLIN;
    if (interest & IO_WRITE) events |= POLLOUT;
    return events;
}

}  // namespace

std::unique_ptr<Poller> Poller::create() { return std::make_unique<PollPoller>(); }

void PollPoller::update(SocketHandle socket, unsigned interest) {
    auto it = index_.find(socket);
    if (it != index_.end()) {
        fds_[it->second].events = toPollEvents(interest);
        return;
    }
    PollFd fd{};
    fd.fd = socket;
    fd.events = toPollEvents(interest);
    index_[socket] = fds_.size();
    fds_.push_back(fd);
}

void PollPoller::remove(SocketHandle socket) {
    auto it = index_.find(socket);
    if (it == index_.end()) return;
    const std::size_t pos = it->second;
    index_.erase(it);
    if (pos != fds_.size() - 1) {   // 用最后一项填补空位
        fds_[pos] = fds_.back();
        index_[fds_[pos].fd] = pos;
    }
    fds_.pop_back();
}

void PollPoller::poll(int timeoutMs, std::vector<Event>& active) {
    if (fds_.empty()) return;
#ifdef _WIN32
    const int ready = WSAPoll(fds_.data(), static_cast<ULONG>(fds_.size()), timeoutMs);
#else
    const int ready = ::poll(fds_.data(), static_cast<nfds_t>(fds_.size()), timeoutMs);
#endif
    if (ready <= 0) return;   // 超时，或被信号打断（下一轮重试即可）
    for (const PollFd& fd : fds_) {
        if (fd.revents == 0) continue;
        unsigned events = 0;
        // 对方关闭时可能只报 POLLHUP 而不报 POLLIN：按可读处理，读到 0 字节就知道连接已关闭
        if (fd.revents & (POLLIN | POLLHUP)) events |= IO_READ;
        if (fd.revents & POLLOUT) events |= IO_WRITE;
        if (fd.revents & (POLLERR | POLLNVAL)) events |= IO_ERROR;
        active.push_back({ fd.fd, events });
    }
}
