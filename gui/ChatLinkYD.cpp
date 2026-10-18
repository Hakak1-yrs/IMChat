#include "ChatLinkYD.h"
#include "NetUtilYD.h"   // 内含 winsock2.h，必须先于其他 Windows 头文件
#include <ws2tcpip.h>

ChatLinkYD::ChatLinkYD() : listen_(INVALID_SOCKET), sock_(INVALID_SOCKET) {
    WSADATA wsaData;
    wsaReady_ = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

ChatLinkYD::~ChatLinkYD() {
    close();
    if (wsaReady_) WSACleanup();
}

ChatLinkYD::StateYD ChatLinkYD::state() const { return state_; }

const std::string& ChatLinkYD::peer() const { return peer_; }

const std::string& ChatLinkYD::error() const { return error_; }

unsigned short ChatLinkYD::port() const { return port_; }

void ChatLinkYD::fail(const std::string& error) {
    close();
    error_ = error;
    state_ = CLOSED;
}

bool ChatLinkYD::listen(unsigned short port, std::string& error) {
    close();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (!wsaReady_ || s == INVALID_SOCKET || ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        ::listen(s, 1) == SOCKET_ERROR) {
        error = "端口 " + std::to_string(port) + " 绑定/监听失败（可能已被占用），错误码 " + std::to_string(WSAGetLastError());
        if (s != INVALID_SOCKET) closesocket(s);
        return false;
    }
    listen_ = s;
    port_ = port;
    error_.clear();
    state_ = LISTENING;
    return true;
}

bool ChatLinkYD::connect(const std::string& ip, unsigned short port, std::string& error) {
    close();
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        error = "IP 地址格式不正确。";
        return false;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (!wsaReady_ || s == INVALID_SOCKET) {
        error = "创建套接字失败。";
        return false;
    }
    u_long nonBlocking = 1;   // 非阻塞连接：连接结果在 poll() 中检查，界面不会因为等待而卡住
    ioctlsocket(s, FIONBIO, &nonBlocking);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
        error = "连接失败，错误码 " + std::to_string(WSAGetLastError());
        closesocket(s);
        return false;
    }
    sock_ = s;
    port_ = port;
    peer_ = ip + ":" + std::to_string(port);
    error_.clear();
    state_ = CONNECTING;
    return true;
}

std::vector<std::string> ChatLinkYD::poll() {
    std::vector<std::string> lines;
    timeval zero{ 0, 0 };
    if (state_ == LISTENING) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(static_cast<SOCKET>(listen_), &readSet);
        if (select(0, &readSet, nullptr, nullptr, &zero) > 0) {
            sockaddr_in addr{};
            int length = sizeof(addr);
            SOCKET s = accept(static_cast<SOCKET>(listen_), reinterpret_cast<sockaddr*>(&addr), &length);
            closesocket(static_cast<SOCKET>(listen_));   // 点对点聊天只接受一个连接
            listen_ = INVALID_SOCKET;
            if (s == INVALID_SOCKET) {
                fail("接受连接失败。");
                return lines;
            }
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            peer_ = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
            sock_ = s;
            state_ = CONNECTED;
        }
    } else if (state_ == CONNECTING) {
        fd_set writeSet, errorSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errorSet);
        FD_SET(static_cast<SOCKET>(sock_), &writeSet);
        FD_SET(static_cast<SOCKET>(sock_), &errorSet);
        if (select(0, nullptr, &writeSet, &errorSet, &zero) > 0) {
            if (FD_ISSET(static_cast<SOCKET>(sock_), &errorSet)) {
                fail("连接失败，请确认对方已在该端口等待连接。");
                return lines;
            }
            u_long blocking = 0;   // 连上以后恢复为阻塞方式：发送时保证整条发完；接收前先用 select 判断有没有数据
            ioctlsocket(static_cast<SOCKET>(sock_), FIONBIO, &blocking);
            state_ = CONNECTED;
        }
    }
    if (state_ != CONNECTED) return lines;
    bool disconnected = false;
    for (int round = 0; round < 16; ++round) {   // 一次最多读 16 块，避免对方狂发时界面没有机会刷新
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(static_cast<SOCKET>(sock_), &readSet);
        if (select(0, &readSet, nullptr, nullptr, &zero) <= 0) break;
        char buf[1024];
        int n = recv(static_cast<SOCKET>(sock_), buf, sizeof(buf), 0);
        if (n <= 0) {
            disconnected = true;
            break;
        }
        buffer_.append(buf, n);
    }
    // 先取出已经收到的完整行（对方断开前发的最后几句，如“主持人关闭了群聊室”），再处理断开
    std::string line;
    while (NetUtilYD::takeLine(buffer_, line)) lines.push_back(line);
    if (disconnected) fail("对方已断开连接。");
    return lines;
}

bool ChatLinkYD::send(const std::string& line) {
    if (state_ != CONNECTED) return false;
    if (!NetUtilYD::sendAll(static_cast<SOCKET>(sock_), line + "\n")) {
        fail("发送失败，连接已断开。");
        return false;
    }
    return true;
}

void ChatLinkYD::close() {
    if (sock_ != INVALID_SOCKET) {
        shutdown(static_cast<SOCKET>(sock_), SD_BOTH);
        closesocket(static_cast<SOCKET>(sock_));
        sock_ = INVALID_SOCKET;
    }
    if (listen_ != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(listen_));
        listen_ = INVALID_SOCKET;
    }
    buffer_.clear();
    if (state_ != CLOSED) state_ = IDLE;
}
