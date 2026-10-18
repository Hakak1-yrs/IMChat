#pragma once
#include <string>
#include "ChatLogYD.h"

// QQ 点对点 TCP 聊天：一方作为服务端等待连接，另一方作为客户端主动连接。
// 连接建立后，主线程负责读取键盘输入并发送，接收线程负责显示对方消息，实现全双工收发
class TcpChatYD {
public:
    TcpChatYD(const std::string& nickname, ChatLogYD& log);   // 收发的消息都会写入聊天记录
    ~TcpChatYD();
    TcpChatYD(const TcpChatYD&) = delete;
    TcpChatYD& operator=(const TcpChatYD&) = delete;

    bool runServer(unsigned short port);                         // socket → bind → listen → accept
    bool runClient(const std::string& ip, unsigned short port);  // socket → connect

private:
    std::string nickname_;
    ChatLogYD& log_;
    bool wsaReady_;
};
