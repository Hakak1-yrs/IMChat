#include "EventLoopThread.h"
#include <future>

EventLoopThread::EventLoopThread() {
    std::promise<EventLoop*> ready;
    thread_ = std::thread([&ready] {
        EventLoop loop;   // 在新线程里创建：这个线程就是它的事件循环线程
        // 等 loop() 真正跑起来再通知：loop() 开头会清除退出标志，太早返回的话，紧接着的 quit() 可能被它抹掉
        loop.runAfter(std::chrono::milliseconds(0), [&ready, &loop] { ready.set_value(&loop); });
        loop.loop();
    });
    loop_ = ready.get_future().get();
}

EventLoopThread::~EventLoopThread() {
    loop_->quit();
    thread_.join();
}

EventLoop& EventLoopThread::loop() { return *loop_; }
