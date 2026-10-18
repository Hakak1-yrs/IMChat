#include <gtest/gtest.h>
#include <future>
#include <thread>
#include <vector>
#include "EventLoop.h"

using namespace std::chrono_literals;

TEST(EventLoopTest, TasksFromOtherThreadsRunInLoopThread) {
    EventLoop* loopPtr = nullptr;
    std::promise<void> ready;
    std::thread worker([&] {
        EventLoop loop;
        loopPtr = &loop;
        ready.set_value();
        loop.loop();
    });
    ready.get_future().wait();

    std::promise<std::thread::id> ranOn;
    loopPtr->runInLoop([&] { ranOn.set_value(std::this_thread::get_id()); });
    EXPECT_EQ(ranOn.get_future().get(), worker.get_id());   // 投递的任务在事件循环线程里执行

    const auto start = std::chrono::steady_clock::now();
    loopPtr->quit();   // 事件循环正阻塞在 poll 上：quit 必须能立即唤醒它
    worker.join();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1s);
}

TEST(EventLoopTest, TimersFireInDueOrderAndCanBeCancelled) {
    EventLoop loop;
    std::vector<std::string> fired;
    loop.runAfter(40ms, [&] { fired.push_back("40ms"); });
    loop.runAfter(10ms, [&] { fired.push_back("10ms"); });
    const EventLoop::TimerId cancelled = loop.runAfter(20ms, [&] { fired.push_back("cancelled"); });
    loop.cancel(cancelled);
    int ticks = 0;
    EventLoop::TimerId every = 0;
    every = loop.runEvery(5ms, [&] {
        if (++ticks == 3) loop.cancel(every);   // 周期定时器可以在自己的回调里取消自己
    });
    loop.runAfter(80ms, [&] { loop.quit(); });
    loop.loop();
    EXPECT_EQ(fired, (std::vector<std::string>{ "10ms", "40ms" }));
    EXPECT_EQ(ticks, 3);
}

TEST(EventLoopTest, TaskQueuedFromLoopThreadRunsAfterCurrentTask) {
    EventLoop loop;
    std::vector<int> order;
    loop.runAfter(1ms, [&] {
        loop.queueInLoop([&] { order.push_back(2); });
        order.push_back(1);
    });
    loop.runAfter(30ms, [&] { loop.quit(); });
    loop.loop();
    EXPECT_EQ(order, (std::vector<int>{ 1, 2 }));
}
