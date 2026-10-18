#pragma once
#include <mutex>
#include <string>
#include <vector>

// 聊天记录：把 TCP 聊天中收发的每一条消息追加到 data\chat_log.txt，并支持按关键字查询。
// 发送（主线程）和接收（接收线程）会同时写记录，因此追加操作用互斥锁保护。
class ChatLogYD {
public:
    explicit ChatLogYD(const std::string& filePath);

    void append(const std::string& direction, const std::string& peer, const std::string& text);
    // 包含 keyword 的记录（keyword 为空时取最近 count 条）；控制台版和图形版共用
    std::vector<std::string> query(const std::string& keyword, int count) const;
    // 显示包含 keyword 的记录（keyword 为空时显示最近 count 条），返回显示的条数
    int show(const std::string& keyword, int count) const;

private:
    std::string filePath_;
    mutable std::mutex mutex_;
};
