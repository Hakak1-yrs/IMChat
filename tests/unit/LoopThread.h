#pragma once
#include <functional>
#include <future>
#include <thread>
#include <type_traits>
#include "EventLoop.h"

// 在后台线程里运行事件循环，供集成测试使用。
// init 在事件循环线程里执行（用来创建服务端），构造函数返回时 init 已经完成；
// cleanup 在事件循环停止后、仍在该线程里执行（销毁服务端），析构时停止事件循环并等待线程结束
class LoopThread {
public:
    explicit LoopThread(std::function<void(EventLoop&)> init, std::function<void()> cleanup = {}) {
        std::promise<void> ready;
        thread_ = std::thread([this, &ready, init = std::move(init), cleanup = std::move(cleanup)] {
            EventLoop loop;
            loop_ = &loop;
            try {
                init(loop);
            } catch (...) {
                ready.set_exception(std::current_exception());
                return;
            }
            ready.set_value();
            loop.loop();
            if (cleanup) cleanup();
        });
        try {
            ready.get_future().get();
        } catch (...) {
            thread_.join();
            throw;
        }
    }

    ~LoopThread() {
        loop_->quit();
        thread_.join();
    }

    LoopThread(const LoopThread&) = delete;
    LoopThread& operator=(const LoopThread&) = delete;

    // 在事件循环线程里执行 f 并等待结果（服务端的状态只能在那个线程里读）
    template <typename F>
    auto call(F f) -> decltype(f()) {
        using Result = decltype(f());
        std::promise<Result> promise;
        loop_->runInLoop([&] {
            if constexpr (std::is_void_v<Result>) {
                f();
                promise.set_value();
            } else {
                promise.set_value(f());
            }
        });
        return promise.get_future().get();
    }

private:
    std::thread thread_;
    EventLoop* loop_ = nullptr;
};
