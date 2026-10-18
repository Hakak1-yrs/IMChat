#include "TcpChat.h"
#include "NetUtil.h"   // 内含 winsock2.h，必须先于其他 Windows 头文件
#include <ws2tcpip.h>
#include <conio.h>
#include <atomic>
#include <iostream>
#include <thread>
#include "Util.h"

namespace {

// 接收线程：持续 recv 对方数据，按行显示
void receiveLoop(SOCKET sock, std::atomic<bool>* running, ChatLog* log, std::string peer) {
    char buf[1024];
    std::string pending;
    while (*running) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) break;   // 对方关闭连接或出错
        pending.append(buf, n);
        std::string message;
        while (NetUtil::takeLine(pending, message)) {
            std::cout << "\n" << message << "\n> " << std::flush;
            log->append("收", peer, message);
        }
    }
    if (*running) std::cout << "\n[系统] 对方已断开连接，按回车返回。" << std::endl;
    *running = false;
}

// 连接建立后的聊天会话：主线程发送，子线程接收
void chatSession(SOCKET sock, const std::string& nickname, ChatLog& log, const std::string& peer) {
    std::cout << "[系统] 连接成功！输入消息后回车发送，输入 /quit 结束聊天。\n";
    std::atomic<bool> running(true);
    std::thread receiver(receiveLoop, sock, &running, &log, peer);
    std::string line;
    while (running) {
        std::cout << "> " << std::flush;
        if (!Util::getLine(line) || !running || line == "/quit") break;
        if (line.empty()) continue;
        std::string message = "[" + nickname + "] " + line + "\n";
        if (!NetUtil::sendAll(sock, message)) {
            std::cout << "[系统] 发送失败，错误码 " << WSAGetLastError() << "\n";
            break;
        }
        log.append("发", peer, line);
    }
    running = false;
    shutdown(sock, SD_BOTH);   // 让接收线程的 recv 返回
    closesocket(sock);
    receiver.join();
    std::cout << "[系统] 聊天结束。\n";
}

}  // namespace

TcpChat::TcpChat(const std::string& nickname, ChatLog& log) : nickname_(nickname), log_(log), wsaReady_(false) {
    WSADATA wsaData;
    wsaReady_ = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    if (!wsaReady_) std::cout << "[系统] Winsock 初始化失败。\n";
}

TcpChat::~TcpChat() {
    if (wsaReady_) WSACleanup();
}

bool TcpChat::runServer(unsigned short port) {
    if (!wsaReady_) return false;
    SOCKET listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSock == INVALID_SOCKET) {
        std::cout << "[系统] 创建套接字失败。\n";
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(listenSock, 1) == SOCKET_ERROR) {
        std::cout << "[系统] 端口 " << port << " 绑定/监听失败（可能已被占用），错误码 " << WSAGetLastError() << "\n";
        closesocket(listenSock);
        return false;
    }
    std::cout << "[系统] 正在端口 " << port << " 等待好友连接……（按 Esc 取消）\n";
    // 用 select 每隔 0.3 秒检查一次是否有连接到来，期间可以按 Esc 取消等待
    while (true) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSock, &readSet);
        timeval timeout{ 0, 300000 };
        int ready = select(0, &readSet, nullptr, nullptr, &timeout);
        if (ready != 0) break;   // 有连接到来（或出错，交给 accept 处理）
        if (_kbhit() && _getch() == 27) {
            std::cout << "[系统] 已取消等待。\n";
            closesocket(listenSock);
            return false;
        }
    }
    sockaddr_in peer{};
    int peerLen = sizeof(peer);
    SOCKET conn = accept(listenSock, reinterpret_cast<sockaddr*>(&peer), &peerLen);
    closesocket(listenSock);   // 点对点聊天只接受一个连接
    if (conn == INVALID_SOCKET) {
        std::cout << "[系统] 接受连接失败。\n";
        return false;
    }
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof(ip));
    std::cout << "[系统] 好友 " << ip << ":" << ntohs(peer.sin_port) << " 已连接。\n";
    chatSession(conn, nickname_, log_, std::string(ip));
    return true;
}

bool TcpChat::runClient(const std::string& ip, unsigned short port) {
    if (!wsaReady_) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        std::cout << "[系统] IP 地址格式不正确。\n";
        return false;
    }
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) {
        std::cout << "[系统] 创建套接字失败。\n";
        return false;
    }
    std::cout << "[系统] 正在连接 " << ip << ":" << port << " ……\n";
    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        std::cout << "[系统] 连接失败，请确认对方已在该端口等待连接。错误码 " << WSAGetLastError() << "\n";
        closesocket(sock);
        return false;
    }
    chatSession(sock, nickname_, log_, ip);
    return true;
}
