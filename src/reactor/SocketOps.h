#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>   // accept、SOMAXCONN 等（使用网络库的代码会直接用到）
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

// 套接字的平台差异集中在这里：Winsock 与 POSIX 套接字在初始化、关闭、非阻塞设置、错误码上写法不同，
// 网络库的其他代码只调用这些函数，不直接写 #ifdef
class SocketOps {
public:
    static void startup();   // Windows 需要先 WSAStartup；可以重复调用，只初始化一次

    static void close(SocketHandle s);
    static bool setNonBlocking(SocketHandle s);
    static void setNoDelay(SocketHandle s);   // 关闭 Nagle 算法：聊天消息小而频繁，不希望被攒起来延迟发送

    // 在 ip:port 上监听（port 为 0 时由系统分配，实际端口写入 boundPort）；失败返回 kInvalidSocket
    static SocketHandle listenTcp(const std::string& ip, std::uint16_t port, int backlog, std::uint16_t& boundPort,
                                  std::string& error);
    static SocketHandle connectTcp(const std::string& ip, std::uint16_t port);   // 阻塞方式连接（测试和工具使用）
    // 非阻塞方式发起连接：立即返回，inProgress 为 true 表示正在连接，之后套接字可写时用 socketError 检查结果。
    // 地址无效或立即失败时返回 kInvalidSocket
    static SocketHandle startConnect(const std::string& ip, std::uint16_t port, bool& inProgress);
    static int socketError(SocketHandle s);   // SO_ERROR：非阻塞连接的结果，0 表示成功
    // 一对互相连通的套接字：事件循环用它唤醒阻塞在 poll 上的线程。
    // POSIX 用 socketpair；Windows 没有，就在 127.0.0.1 上临时监听、自己连自己
    static bool createPair(SocketHandle& a, SocketHandle& b);

    static long sendSome(SocketHandle s, const char* data, std::size_t size);   // 出错返回 -1
    static long recvSome(SocketHandle s, char* buffer, std::size_t size);       // 对方关闭返回 0，出错返回 -1
    static std::string peerAddress(SocketHandle s);                             // "ip:port"

    static int lastError();
    static bool wouldBlock(int error);    // 非阻塞套接字暂时没有数据可读 / 缓冲区已满
    static bool interrupted(int error);   // 被信号打断，重试即可
    static std::string errorText(int error);
};
