#include "Poller.h"
#include <cstdlib>
#include <cstring>
#include "PollPoller.h"
#ifdef __linux__
#include "EpollPoller.h"
#endif

const char* Poller::defaultName() {
#ifdef __linux__
    // 环境变量 IMCHAT_POLLER=poll 时改用 poll：便于对比两种实现，也是 epoll 出问题时的退路
    const char* chosen = std::getenv("IMCHAT_POLLER");
    if (chosen && std::strcmp(chosen, "poll") == 0) return "poll";
    return "epoll";
#elif defined(_WIN32)
    return "WSAPoll";
#else
    return "poll";
#endif
}

std::unique_ptr<Poller> Poller::create() {
#ifdef __linux__
    if (std::strcmp(defaultName(), "epoll") == 0) return std::make_unique<EpollPoller>();
#endif
    return std::make_unique<PollPoller>();
}
