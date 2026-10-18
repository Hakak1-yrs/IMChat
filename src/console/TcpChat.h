#pragma once
#include <string>
#include "ChatLog.h"

// QQ 点对点 TCP 聊天：一方作为服务端等待连接，另一方作为客户端主动连接。
// 连接建立后，主线程负责读取键盘输入并发送，接收线程负责显示对方消息，实现全双工收发
class TcpChat {
public:
    TcpChat(const std::string& nickname, ChatLog& log);   // 收发的消息都会写入聊天记录
    ~TcpChat();
    TcpChat(const TcpChat&) = delete;
    TcpChat& operator=(const TcpChat&) = delete;

    bool runServer(unsigned short port);                         // socket → bind → listen → accept
    bool runClient(const std::string& ip, unsigned short port);  // socket → connect

private:
    std::string nickname_;
    ChatLog& log_;
    bool wsaReady_;
};
