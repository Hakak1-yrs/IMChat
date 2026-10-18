#include <gtest/gtest.h>
#include <algorithm>
#include "Poller.h"

// 平台默认的 Poller（Linux 上是 epoll，其他平台是 poll / WSAPoll）的基本行为：
// 关心可读时，对端写入后报告可读；关心可写时立即报告可写；修改、删除登记后不再报告
namespace {

bool hasEvent(const std::vector<Poller::Event>& events, SocketHandle s, unsigned flag) {
    return std::any_of(events.begin(), events.end(), [&](const Poller::Event& e) { return e.socket == s && (e.events & flag); });
}

}  // namespace

TEST(PollerTest, ReportsReadableWritableAndHonoursRemoval) {
    SocketHandle a = kInvalidSocket, b = kInvalidSocket;
    ASSERT_TRUE(SocketOps::createPair(a, b));
    std::unique_ptr<Poller> poller = Poller::create();
    SCOPED_TRACE(Poller::defaultName());

    poller->update(a, IO_READ);
    std::vector<Poller::Event> events;
    poller->poll(50, events);
    EXPECT_FALSE(hasEvent(events, a, IO_READ));   // 还没有数据

    ASSERT_EQ(SocketOps::sendSome(b, "x", 1), 1);
    events.clear();
    poller->poll(1000, events);
    EXPECT_TRUE(hasEvent(events, a, IO_READ));
    EXPECT_FALSE(hasEvent(events, a, IO_WRITE));   // 没关心可写，就不报告

    poller->update(a, IO_READ | IO_WRITE);         // 修改关心的事件
    events.clear();
    poller->poll(1000, events);
    EXPECT_TRUE(hasEvent(events, a, IO_WRITE));

    poller->remove(a);                             // 删除登记后不再报告（数据仍未读走）
    events.clear();
    poller->poll(50, events);
    EXPECT_FALSE(hasEvent(events, a, IO_READ));

    SocketOps::close(b);                           // 对端关闭：按可读报告（读到 0 字节）
    poller->update(a, IO_READ);
    events.clear();
    poller->poll(1000, events);
    EXPECT_TRUE(hasEvent(events, a, IO_READ));
    SocketOps::close(a);
}
