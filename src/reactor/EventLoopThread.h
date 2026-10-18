#pragma once
#include <thread>
#include "EventLoop.h"

// 一个运行着事件循环的线程（one loop per thread）。构造时启动线程，等事件循环建好后返回；
// 析构时让事件循环退出并等线程结束。TcpServer 用一组这样的线程处理连接的收发
class EventLoopThread {
public:
    EventLoopThread();
    ~EventLoopThread();
    EventLoopThread(const EventLoopThread&) = delete;
    EventLoopThread& operator=(const EventLoopThread&) = delete;

    EventLoop& loop();

private:
    std::thread thread_;
    EventLoop* loop_ = nullptr;   // 在线程里创建，线程结束时销毁
};
