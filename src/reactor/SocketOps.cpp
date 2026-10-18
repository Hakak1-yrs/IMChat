#include "SocketOps.h"
#include <cstring>
#include <mutex>

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using SockLen = int;
#else
using SockLen = socklen_t;
#endif

bool fillAddress(const std::string& ip, std::uint16_t port, sockaddr_in& address) {
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    return inet_pton(AF_INET, ip.c_str(), &address.sin_addr) == 1;
}

}  // namespace

void SocketOps::startup() {
#ifdef _WIN32
    static std::once_flag once;
    std::call_once(once, [] {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif
}

void SocketOps::close(SocketHandle s) {
    if (s == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(s);
#else
    ::close(s);
#endif
}

bool SocketOps::setNonBlocking(SocketHandle s) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
#else
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

void SocketOps::setNoDelay(SocketHandle s) {
    int on = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof(on));
}

SocketHandle SocketOps::listenTcp(const std::string& ip, std::uint16_t port, int backlog, std::uint16_t& boundPort,
                                  std::string& error) {
    startup();
    sockaddr_in address;
    if (!fillAddress(ip, port, address)) {
        error = "无效的 IP 地址：" + ip;
        return kInvalidSocket;
    }
    SocketHandle s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket) {
        error = "创建套接字失败：" + errorText(lastError());
        return kInvalidSocket;
    }
#ifdef _WIN32
    // Windows 上 SO_REUSEADDR 的含义是“允许抢占别人正在用的端口”，应改用独占方式
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof(on));
#else
    int on = 1;   // 服务端重启时不必等 TIME_WAIT 结束就能重新绑定端口
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
    if (bind(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(s, backlog) != 0 ||
        !setNonBlocking(s)) {
        error = "监听 " + ip + ":" + std::to_string(port) + " 失败：" + errorText(lastError());
        close(s);
        return kInvalidSocket;
    }
    SockLen length = sizeof(address);
    getsockname(s, reinterpret_cast<sockaddr*>(&address), &length);
    boundPort = ntohs(address.sin_port);
    return s;
}

SocketHandle SocketOps::connectTcp(const std::string& ip, std::uint16_t port) {
    startup();
    sockaddr_in address;
    if (!fillAddress(ip, port, address)) return kInvalidSocket;
    SocketHandle s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalidSocket) return kInvalidSocket;
    if (connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        close(s);
        return kInvalidSocket;
    }
    setNoDelay(s);
    return s;
}

bool SocketOps::createPair(SocketHandle& a, SocketHandle& b) {
    startup();
#ifdef _WIN32
    std::uint16_t port = 0;
    std::string error;
    SocketHandle listener = listenTcp("127.0.0.1", 0, 1, port, error);
    if (listener == kInvalidSocket) return false;
    u_long off = 0;   // listenTcp 设成了非阻塞，这里要阻塞地 accept
    ioctlsocket(listener, FIONBIO, &off);
    a = connectTcp("127.0.0.1", port);
    b = a == kInvalidSocket ? kInvalidSocket : accept(listener, nullptr, nullptr);
    close(listener);
    if (b == kInvalidSocket) {
        close(a);
        return false;
    }
    setNoDelay(b);
#else
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) return false;
    a = fds[0];
    b = fds[1];
#endif
    return setNonBlocking(a) && setNonBlocking(b);
}

long SocketOps::sendSome(SocketHandle s, const char* data, std::size_t size) {
#ifdef _WIN32
    const int chunk = static_cast<int>(size > 0x7FFFFFFF ? 0x7FFFFFFF : size);
    return send(s, data, chunk, 0);
#else
    // MSG_NOSIGNAL：对方已关闭时返回错误，而不是收到 SIGPIPE 把整个进程杀掉
    return static_cast<long>(::send(s, data, size, MSG_NOSIGNAL));
#endif
}

long SocketOps::recvSome(SocketHandle s, char* buffer, std::size_t size) {
#ifdef _WIN32
    return recv(s, buffer, static_cast<int>(size > 0x7FFFFFFF ? 0x7FFFFFFF : size), 0);
#else
    return static_cast<long>(::recv(s, buffer, size, 0));
#endif
}

std::string SocketOps::peerAddress(SocketHandle s) {
    sockaddr_in address;
    SockLen length = sizeof(address);
    if (getpeername(s, reinterpret_cast<sockaddr*>(&address), &length) != 0) return "?";
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &address.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(address.sin_port));
}

int SocketOps::lastError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool SocketOps::wouldBlock(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

bool SocketOps::interrupted(int error) {
#ifdef _WIN32
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

std::string SocketOps::errorText(int error) {
#ifdef _WIN32
    return "错误码 " + std::to_string(error);
#else
    return std::strerror(error);
#endif
}
