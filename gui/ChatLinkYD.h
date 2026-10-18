#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 图形界面使用的一条 TCP 连接：QQ 点对点聊天（服务端等待 / 客户端连接）和加入实时群聊室都用它。
// 采用非阻塞“轮询”方式：界面定时器每 0.1 秒调用一次 poll()，处理连接建立和数据接收，界面不会卡住，也不需要额外的线程。
// 传输内容与控制台版完全相同（一行一条消息），所以图形版和控制台版之间可以互相聊天
class ChatLinkYD {
public:
    enum StateYD { IDLE, LISTENING, CONNECTING, CONNECTED, CLOSED };

    ChatLinkYD();
    ~ChatLinkYD();
    ChatLinkYD(const ChatLinkYD&) = delete;
    ChatLinkYD& operator=(const ChatLinkYD&) = delete;

    bool listen(unsigned short port, std::string& error);                      // 服务端：socket → bind → listen，等待一个连接
    bool connect(const std::string& ip, unsigned short port, std::string& error);   // 客户端：socket → connect（不等待结果）
    std::vector<std::string> poll();          // 处理一轮，返回这一轮收到的完整行
    bool send(const std::string& line);       // 发送一行（自动加换行）
    void close();
    StateYD state() const;
    const std::string& peer() const;          // 对方地址
    const std::string& error() const;         // 连接失败或断开的原因
    unsigned short port() const;

private:
    void fail(const std::string& error);

    std::uintptr_t listen_;   // SOCKET，头文件中不包含 Winsock，用整数保存
    std::uintptr_t sock_;
    StateYD state_ = IDLE;
    bool wsaReady_ = false;
    unsigned short port_ = 0;
    std::string buffer_;
    std::string peer_;
    std::string error_;
};
