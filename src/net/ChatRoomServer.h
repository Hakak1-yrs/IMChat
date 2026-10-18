#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Group;
class Platform;
class ServiceBase;

// 实时群聊室的服务端（控制台版和图形版共用）。
// 采用“轮询”方式：每调用一次 poll()，就用 select 处理一轮——接受新连接、核对身份（必须是本群成员）、
// 收消息并转发给其他所有人、写入群聊记录、发出主持人的发言。
// 控制台版在一个网络线程里循环调用 poll()（主线程读键盘）；图形版在界面的定时器里调用，不需要额外的线程。
// 通信协议（每条一行）：客户端 → HELLO 服务 群号 账号 / MSG 内容 / WHO；服务端 → OK 欢迎 / NO 原因，之后每行是一条要显示的文字
class ChatRoomServer {
public:
    using Show = std::function<void(const std::string&)>;   // 要显示在主持人屏幕上的文字

    ChatRoomServer(Platform& platform, Group& group, ServiceBase& host, Show show);
    ~ChatRoomServer();
    ChatRoomServer(const ChatRoomServer&) = delete;
    ChatRoomServer& operator=(const ChatRoomServer&) = delete;

    bool start(unsigned short port, std::string& error);   // socket → bind → listen
    void poll(int timeoutMs);                               // 处理一轮网络事件，最多等待 timeoutMs 毫秒
    void say(const std::string& text);                      // 主持人发言（线程安全，下一轮 poll 发出）；"/who" 显示在线成员
    void stop();                                            // 通知所有成员后关闭
    bool running() const;
    std::vector<std::string> online() const;                // 在线成员（主持人在第一个），只能在调用 poll 的线程中使用
    std::string title() const;                              // 如“QQ群 1001（编程交流群）”

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
