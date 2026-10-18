#include "GroupChatRoomYD.h"
#include "NetUtilYD.h"   // 内含 winsock2.h，必须先于其他 Windows 头文件
#include <ws2tcpip.h>
#include <atomic>
#include <iostream>
#include <thread>
#include "ChatRoomServerYD.h"
#include "PlatformYD.h"
#include "UtilYD.h"

GroupChatRoomYD::GroupChatRoomYD(PlatformYD& platform, GroupYD& group, ServiceBaseYD& me)
    : platform_(platform), group_(group), me_(me), wsaReady_(false) {
    WSADATA wsaData;
    wsaReady_ = (WSAStartup(MAKEWORD(2, 2), &wsaData) == 0);
    if (!wsaReady_) std::cout << "[系统] Winsock 初始化失败。\n";
}

GroupChatRoomYD::~GroupChatRoomYD() {
    if (wsaReady_) WSACleanup();
}

std::string GroupChatRoomYD::title() const {
    return ServiceTypeHelperYD::name(group_.service()) + "群 " + std::to_string(group_.groupNo()) + "（" +
           group_.name() + "）";
}

// 服务端的网络处理由 ChatRoomServerYD 完成（与图形版共用）：网络线程循环调用 poll()，主线程读键盘
bool GroupChatRoomYD::host(unsigned short port) {
    if (!wsaReady_) return false;
    // 别人的消息显示在主持人屏幕上；主线程此时正等待键盘输入，显示后重新打印提示符
    ChatRoomServerYD server(platform_, group_, me_,
                            [](const std::string& text) { std::cout << "\n" << text << "\n> " << std::flush; });
    std::string error;
    if (!server.start(port, error)) {
        std::cout << "[系统] " << error << "\n";
        return false;
    }
    std::cout << "[系统] " << title() << "的群聊室已开设，端口 " << port << "。\n"
              << "       其他群成员进入“群聊天 → 加入实时群聊室”，填本机 IP（本机演示填 127.0.0.1）和这个端口。\n"
              << "       输入消息回车发送，/who 查看在线成员，/quit 关闭群聊室。聊天内容自动存入群聊记录。\n";

    std::atomic<bool> running(true);
    std::thread worker([&] {
        while (running) server.poll(100);   // 每 0.1 秒至少醒来一次，处理主持人的发言、检查是否要关闭
    });
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        line = UtilYD::lineText(line);
        if (line == "/quit") break;
        if (!line.empty()) server.say(line);
    }
    running = false;
    worker.join();
    server.stop();   // 通知所有成员后断开
    std::cout << "[系统] 群聊室已关闭。\n";
    return true;
}

bool GroupChatRoomYD::join(const std::string& ip, unsigned short port) {
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
        std::cout << "[系统] 连接失败，请确认对方已开设群聊室、端口一致。错误码 " << WSAGetLastError() << "\n";
        closesocket(sock);
        return false;
    }

    // 申请进入，并等待应答最多 5 秒（连到的如果不是群聊室，例如点对点聊天的服务端，就不会有应答）
    const std::string hello = "HELLO " + ServiceTypeHelperYD::code(group_.service()) + " " +
                              std::to_string(group_.groupNo()) + " " + me_.id() + "\n";
    DWORD waitMs = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&waitMs), sizeof(waitMs));
    std::string buffer, reply;
    bool gotReply = NetUtilYD::sendAll(sock, hello);
    char buf[1024];
    while (gotReply && !NetUtilYD::takeLine(buffer, reply)) {
        int n = recv(sock, buf, sizeof(buf), 0);
        if (n <= 0) gotReply = false;
        else buffer.append(buf, n);
    }
    if (!gotReply || (reply.compare(0, 3, "OK ") != 0 && reply.compare(0, 3, "NO ") != 0)) {
        std::cout << "[系统] 没有收到群聊室的应答，请确认对方开设的是" << title() << "的群聊室。\n";
        closesocket(sock);
        return false;
    }
    if (reply.compare(0, 3, "NO ") == 0) {
        std::cout << "[系统] 无法进入：" << reply.substr(3) << "\n";
        closesocket(sock);
        return false;
    }
    waitMs = 0;   // 进入后恢复为一直等待
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&waitMs), sizeof(waitMs));
    std::cout << "[系统] " << reply.substr(3) << "\n[系统] 输入消息回车发送，/who 查看在线成员，/quit 离开群聊室。\n";

    std::atomic<bool> running(true);
    std::thread receiver([&] {
        std::string line;
        while (running) {
            while (NetUtilYD::takeLine(buffer, line)) std::cout << "\n" << line << "\n> " << std::flush;
            int n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) break;
            buffer.append(buf, n);
        }
        if (running) std::cout << "\n[系统] 与群聊室的连接已断开，按回车返回。" << std::endl;
        running = false;
    });
    std::string line;
    while (running) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line) || !running) break;
        line = UtilYD::lineText(line);
        if (line == "/quit") break;
        if (line.empty()) continue;
        if (!NetUtilYD::sendAll(sock, line == "/who" ? "WHO\n" : "MSG " + line + "\n")) {
            std::cout << "[系统] 发送失败，错误码 " << WSAGetLastError() << "\n";
            break;
        }
    }
    running = false;
    shutdown(sock, SD_BOTH);   // 让接收线程的 recv 返回
    closesocket(sock);
    receiver.join();
    std::cout << "[系统] 已离开群聊室。\n";
    return true;
}
