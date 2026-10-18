#pragma once
#include <winsock2.h>
#include <string>

// 网络工具类：点对点聊天（TcpChatYD）和群聊室（GroupChatRoomYD）共用的收发函数。
// 只在网络相关的 .cpp 中包含——winsock2.h 必须在 windows.h 之前被包含，不能放进其他头文件
class NetUtilYD {
public:
    // send 不保证一次发完，循环发送直到全部发出
    static bool sendAll(SOCKET sock, const std::string& data);
    // TCP 是字节流，一次 recv 可能收到半行或好几行：从接收缓冲区取出一整行（不含换行符），没有完整的一行时返回 false
    static bool takeLine(std::string& buffer, std::string& line);
};
