#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "Poller.h"

// 事件循环（Reactor）：在一个线程里反复“等待 I/O 事件 → 调用对应的处理函数 → 执行到期的定时器 → 执行其他线程投递的任务”。
// 套接字和处理函数只在事件循环线程里访问，所以不需要加锁；其他线程想操作连接（比如发送消息、关闭服务），
// 用 runInLoop 把任务投递过来，再写一个字节到唤醒套接字，让阻塞在 poll 上的线程立即醒来
class EventLoop {
public:
    using Task = std::function<void()>;
    using Handler = std::function<void(unsigned events)>;
    using TimerId = std::uint64_t;
    using Clock = std::chrono::steady_clock;

    EventLoop();
    ~EventLoop();
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void loop();   // 在调用它的线程里运行，直到 quit()；这个线程就是事件循环线程
    void quit();   // 任何线程都可以调用
    bool isInLoopThread() const;

    // ---- 以下三个只能在事件循环线程里调用 ----
    void watch(SocketHandle socket, unsigned interest, Handler handler);
    void setInterest(SocketHandle socket, unsigned interest);
    void unwatch(SocketHandle socket);

    void runInLoop(Task task);     // 在事件循环线程里执行：当前就是该线程则立即执行，否则投递
    void queueInLoop(Task task);   // 总是投递，在本轮事件处理完之后执行

    // 定时器（只能在事件循环线程里调用）
    TimerId runAfter(std::chrono::milliseconds delay, Task task);
    TimerId runEvery(std::chrono::milliseconds interval, Task task);
    void cancel(TimerId id);

private:
    struct Timer {
        Task task;
        std::chrono::milliseconds interval;   // 0 表示只执行一次
    };
    using TimerKey = std::pair<Clock::time_point, TimerId>;   // 按到期时间排序，时间相同按编号

    void wakeup();
    void drainWakeup();
    void runPendingTasks();
    void runDueTimers();
    int pollTimeoutMs();
    TimerId addTimer(std::chrono::milliseconds delay, std::chrono::milliseconds interval, Task task);

    std::unique_ptr<Poller> poller_;
    std::unordered_map<SocketHandle, Handler> handlers_;
    std::atomic<bool> quit_{ false };
    std::atomic<std::thread::id> threadId_;
    SocketHandle wakeupRead_ = kInvalidSocket;
    SocketHandle wakeupWrite_ = kInvalidSocket;

    std::mutex mutex_;
    std::vector<Task> pending_;   // 其他线程投递的任务，受 mutex_ 保护

    std::map<TimerKey, Timer> timers_;
    std::unordered_map<TimerId, Clock::time_point> timerDue_;
    TimerId nextTimerId_ = 1;
};
