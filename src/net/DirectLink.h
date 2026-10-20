#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 点对点聊天（联网版）的直连：服务端只负责让双方交换连接信息，聊天内容走这条连接，不经过服务端。
// - host(key)：在系统分配的端口上等待对方连接。对方可以是另一个电脑客户端（TCP 连上后先发一行“IMCHAT-P2P 口令”），
//   也可以是浏览器（WebSocket，ws://地址:端口/p2p?key=口令）——浏览器不能开端口等别人连，所以总是电脑这边等待。
//   口令不对的连接直接关掉、接着等：别人扫到这个端口也连不进来；
// - connect(ips, port, key)：连到对方电脑开的端口。对方可能有好几个地址（有线、无线、虚拟网卡），同时都试，
//   对方回了“IMCHAT-P2P OK”的那个才算连上——TCP 连上了不代表连到了对方：开着 VPN、代理时，
//   发往任何地址的连接都可能被本机的虚拟网卡先接下来。
// 连上后两边收发的都是一行一条的“[昵称] 内容”，和单机版、控制台版的点对点聊天相同。
// 与 ChatLink 一样采用非阻塞轮询：界面定时器每 0.1 秒调用 poll()，不需要额外的线程
class DirectLink {
public:
    enum State { IDLE, LISTENING, CONNECTING, CONNECTED, CLOSED };

    DirectLink();
    ~DirectLink();
    DirectLink(const DirectLink&) = delete;
    DirectLink& operator=(const DirectLink&) = delete;

    bool host(const std::string& key, std::string& error);   // 成功后 port() 是系统分配的端口
    bool connect(const std::vector<std::string>& ips, unsigned short port, const std::string& key, std::string& error);
    std::vector<std::string> poll();   // 处理一轮，返回这一轮收到的完整行
    bool send(const std::string& line);
    void close();

    State state() const;
    unsigned short port() const;
    const std::string& peer() const;    // 对方地址
    const std::string& error() const;   // 连接失败或断开的原因
    bool webSocket() const;             // 对方是浏览器

    // 本机的 IPv4 地址（发给对方，让它来连）；127.0.0.1 排在最后（同一台电脑上演示时用）
    static std::vector<std::string> localAddresses();

private:
    enum Stage { HELLO, LINES, WEBSOCKET };   // 等待对方证明身份 / 按行收发 / WebSocket

    void acceptOne();
    void checkAttempts();
    void handshake();
    void readFrames(std::vector<std::string>& lines);
    void connected();
    void dropProbe();                         // 口令不对：关掉这个连接，接着等
    bool sendRaw(const std::string& data);
    void fail(const std::string& error);

    struct Attempt {                          // 一个地址上的主动连接
        std::uintptr_t sock;
        std::string ip;
        bool helloSent = false;               // TCP 已连上、发了口令，等对方确认
        std::string buffer;
    };

    std::uintptr_t listen_;                   // SOCKET，头文件中不包含 Winsock，用整数保存
    std::uintptr_t sock_;                     // 正在验证或已经连上的连接
    std::vector<Attempt> attempts_;
    State state_ = IDLE;
    Stage stage_ = HELLO;
    bool wsaReady_ = false;
    unsigned short port_ = 0;
    std::string key_;
    std::string buffer_;
    std::string message_;                     // WebSocket 分片消息已收到的部分
    std::string peer_;
    std::string error_;
};
