#include "EventLoop.h"
#include <stdexcept>

namespace {
const int kMaxPollMs = 10000;   // 没有定时器时最多等这么久（quit 会唤醒，这里只是兜底）
}

EventLoop::EventLoop() : poller_(Poller::create()), threadId_(std::this_thread::get_id()) {
    if (!SocketOps::createPair(wakeupRead_, wakeupWrite_)) throw std::runtime_error("创建事件循环的唤醒套接字失败");
    watch(wakeupRead_, IO_READ, [this](unsigned) { drainWakeup(); });
}

EventLoop::~EventLoop() {
    SocketOps::close(wakeupRead_);
    SocketOps::close(wakeupWrite_);
}

void EventLoop::loop() {
    threadId_ = std::this_thread::get_id();
    // 不在这里清除退出标志：别的线程可能在 loop() 开始之前就调用了 quit()（比如刚创建就要销毁），
    // 那样的话应当立即返回，而不是把这次 quit() 抹掉、永远等下去。标志在退出时清除，之后可以再次调用 loop()
    std::vector<Poller::Event> active;
    while (!quit_) {
        active.clear();
        poller_->poll(pollTimeoutMs(), active);
        for (const Poller::Event& event : active) {
            auto it = handlers_.find(event.socket);
            if (it == handlers_.end()) continue;   // 本轮前面的处理函数已经把它关掉了
            // 先复制一份：处理函数里可能 unwatch 自己，map 中的那份会被销毁
            Handler handler = it->second;
            handler(event.events);
        }
        runDueTimers();
        runPendingTasks();
    }
    quit_ = false;
}

void EventLoop::quit() {
    quit_ = true;
    if (!isInLoopThread()) wakeup();
}

bool EventLoop::isInLoopThread() const { return threadId_.load() == std::this_thread::get_id(); }

void EventLoop::watch(SocketHandle socket, unsigned interest, Handler handler) {
    handlers_[socket] = std::move(handler);
    poller_->update(socket, interest);
}

void EventLoop::setInterest(SocketHandle socket, unsigned interest) { poller_->update(socket, interest); }

void EventLoop::unwatch(SocketHandle socket) {
    handlers_.erase(socket);
    poller_->remove(socket);
}

void EventLoop::runInLoop(Task task) {
    if (isInLoopThread()) task();
    else queueInLoop(std::move(task));
}

void EventLoop::queueInLoop(Task task) {
    bool wasEmpty = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wasEmpty = pending_.empty();
        pending_.push_back(std::move(task));
    }
    // 事件循环线程自己投递的任务本轮结束时就会执行，不必唤醒；其他线程投递的必须唤醒。
    // 队列原来就不空时也不必再唤醒：事件循环每次进入等待之前都会在锁内检查队列，不空就不等待；
    // 而队列从空变为不空的那一次已经唤醒过它。多线程大量互相投递任务时，省掉了绝大部分唤醒用的系统调用
    if (!isInLoopThread() && wasEmpty) wakeup();
}

void EventLoop::wakeup() {
    const char one = 1;
    SocketOps::sendSome(wakeupWrite_, &one, 1);   // 写满了也无所谓：说明已经有未处理的唤醒
}

void EventLoop::drainWakeup() {
    char buffer[256];
    while (SocketOps::recvSome(wakeupRead_, buffer, sizeof(buffer)) > 0) {
    }
}

void EventLoop::runPendingTasks() {
    std::vector<Task> tasks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pending_);   // 交换出来再执行：执行任务时不持有锁，任务里也可以继续投递
    }
    for (Task& task : tasks) task();
}

EventLoop::TimerId EventLoop::runAfter(std::chrono::milliseconds delay, Task task) {
    return addTimer(delay, std::chrono::milliseconds(0), std::move(task));
}

EventLoop::TimerId EventLoop::runEvery(std::chrono::milliseconds interval, Task task) {
    return addTimer(interval, interval, std::move(task));
}

EventLoop::TimerId EventLoop::addTimer(std::chrono::milliseconds delay, std::chrono::milliseconds interval, Task task) {
    const TimerId id = nextTimerId_++;
    const Clock::time_point due = Clock::now() + delay;
    timers_.emplace(TimerKey(due, id), Timer{ std::move(task), interval });
    timerDue_[id] = due;
    return id;
}

void EventLoop::cancel(TimerId id) {
    auto it = timerDue_.find(id);
    if (it == timerDue_.end()) return;
    timers_.erase(TimerKey(it->second, id));
    timerDue_.erase(it);
}

void EventLoop::runDueTimers() {
    const Clock::time_point now = Clock::now();
    while (!timers_.empty() && timers_.begin()->first.first <= now) {
        auto node = timers_.extract(timers_.begin());   // 先取出再执行：任务里可能取消或新增定时器
        const TimerId id = node.key().second;
        Timer& timer = node.mapped();
        if (timer.interval.count() > 0) {   // 周期定时器：按间隔重新排队（从“现在”算起，不追赶错过的次数）
            const Clock::time_point due = now + timer.interval;
            node.key() = TimerKey(due, id);
            timerDue_[id] = due;
            Task task = timer.task;
            timers_.insert(std::move(node));
            task();
        } else {
            timerDue_.erase(id);
            timer.task();
        }
    }
}

int EventLoop::pollTimeoutMs() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!pending_.empty()) return 0;
    }
    if (timers_.empty()) return kMaxPollMs;
    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(timers_.begin()->first.first - Clock::now());
    if (wait.count() <= 0) return 0;
    return static_cast<int>(std::min<long long>(wait.count() + 1, kMaxPollMs));   // +1：避免因取整提前醒来、空转一轮
}
