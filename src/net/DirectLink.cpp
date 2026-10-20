#include "DirectLink.h"
#include "NetUtil.h"   // 内含 winsock2.h，必须先于其他 Windows 头文件
#include <ws2tcpip.h>
#include <algorithm>
#include "WebSocket.h"

namespace {

const std::string kHello = "IMCHAT-P2P ";      // 电脑客户端连上后发的第一行：IMCHAT-P2P 口令
const std::string kWelcome = "IMCHAT-P2P OK";  // 口令正确时等待方的回答
constexpr std::size_t kMaxHello = 8192;        // 这么多数据还没证明身份就断开
constexpr std::size_t kMaxMessage = 64 * 1024;

void setNonBlocking(SOCKET s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}

std::string addressOf(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
}

}  // namespace

DirectLink::DirectLink() : listen_(INVALID_SOCKET), sock_(INVALID_SOCKET) {
    WSADATA wsaData;
    wsaReady_ = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

DirectLink::~DirectLink() {
    close();
    if (wsaReady_) WSACleanup();
}

DirectLink::State DirectLink::state() const { return state_; }
unsigned short DirectLink::port() const { return port_; }
const std::string& DirectLink::peer() const { return peer_; }
const std::string& DirectLink::error() const { return error_; }
bool DirectLink::webSocket() const { return stage_ == WEBSOCKET; }

bool DirectLink::host(const std::string& key, std::string& error) {
    close();
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = 0;   // 由系统分配
    int length = sizeof(addr);
    if (!wsaReady_ || s == INVALID_SOCKET || ::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        ::listen(s, 4) == SOCKET_ERROR || getsockname(s, reinterpret_cast<sockaddr*>(&addr), &length) == SOCKET_ERROR) {
        error = "开启端口失败，错误码 " + std::to_string(WSAGetLastError());
        if (s != INVALID_SOCKET) closesocket(s);
        return false;
    }
    setNonBlocking(s);
    listen_ = s;
    port_ = ntohs(addr.sin_port);
    key_ = key;
    error_.clear();
    state_ = LISTENING;
    stage_ = HELLO;
    return true;
}

bool DirectLink::connect(const std::vector<std::string>& ips, unsigned short port, const std::string& key, std::string& error) {
    close();
    for (const std::string& ip : ips) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) continue;
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) continue;
        setNonBlocking(s);   // 非阻塞连接：结果在 poll() 中检查，界面不会因为等待而卡住
        if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) {
            closesocket(s);
            continue;
        }
        attempts_.push_back(Attempt{ s, ip });
    }
    if (!wsaReady_ || attempts_.empty()) {
        error = "对方的地址无效。";
        return false;
    }
    port_ = port;
    key_ = key;
    error_.clear();
    state_ = CONNECTING;
    stage_ = HELLO;
    return true;
}

std::vector<std::string> DirectLink::poll() {
    std::vector<std::string> lines;
    if (state_ == LISTENING && sock_ == INVALID_SOCKET) acceptOne();
    if (state_ == CONNECTING) checkAttempts();
    if (sock_ == INVALID_SOCKET) return lines;
    bool closed = false;
    for (int round = 0; round < 16; ++round) {   // 一次最多读 16 块，避免对方狂发时界面没有机会刷新
        char buf[4096];
        const int n = recv(static_cast<SOCKET>(sock_), buf, sizeof(buf), 0);
        if (n > 0) {
            buffer_.append(buf, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && WSAGetLastError() == WSAEWOULDBLOCK) break;
        closed = true;   // 对方关闭或连接出错
        break;
    }
    if (stage_ == HELLO && state_ == LISTENING) handshake();
    if (stage_ == LINES) {
        std::string line;
        while (NetUtil::takeLine(buffer_, line)) lines.push_back(line);
    } else if (stage_ == WEBSOCKET && state_ == CONNECTED) {
        readFrames(lines);
    }
    // 先交出已经收到的完整行（对方断开前发的最后几句），再处理断开
    if (closed && sock_ != INVALID_SOCKET) {
        if (state_ == CONNECTED) fail("对方已断开连接。");
        else dropProbe();
    }
    return lines;
}

void DirectLink::acceptOne() {
    sockaddr_in addr{};
    int length = sizeof(addr);
    SOCKET s = accept(static_cast<SOCKET>(listen_), reinterpret_cast<sockaddr*>(&addr), &length);
    if (s == INVALID_SOCKET) return;   // 还没有人来连
    setNonBlocking(s);
    sock_ = s;
    peer_ = addressOf(addr);
    buffer_.clear();
    stage_ = HELLO;
}

void DirectLink::checkAttempts() {
    // 第一步：TCP 连上了就发口令
    fd_set writeSet, errorSet;
    FD_ZERO(&writeSet);
    FD_ZERO(&errorSet);
    int waiting = 0;
    for (const Attempt& a : attempts_) {
        if (a.helloSent) continue;
        FD_SET(static_cast<SOCKET>(a.sock), &writeSet);
        FD_SET(static_cast<SOCKET>(a.sock), &errorSet);
        ++waiting;
    }
    timeval zero{ 0, 0 };
    const bool ready = waiting > 0 && select(0, nullptr, &writeSet, &errorSet, &zero) > 0;
    for (std::size_t i = 0; i < attempts_.size();) {
        Attempt& a = attempts_[i];
        const SOCKET s = static_cast<SOCKET>(a.sock);
        bool drop = false;
        if (!a.helloSent && ready && FD_ISSET(s, &errorSet)) {
            drop = true;   // 这个地址连不上
        } else if (!a.helloSent && ready && FD_ISSET(s, &writeSet)) {
            const std::string hello = kHello + key_ + "\n";   // 很短，非阻塞套接字也一次发得完
            a.helloSent = ::send(s, hello.data(), static_cast<int>(hello.size()), 0) == static_cast<int>(hello.size());
            drop = !a.helloSent;
        } else if (a.helloSent) {
            // 第二步：等对方回答“IMCHAT-P2P OK”。连到的不是对方（比如被 VPN 接下来的）就等不到，最后被关掉
            char buf[512];
            const int n = recv(s, buf, sizeof(buf), 0);
            if (n > 0) a.buffer.append(buf, static_cast<std::size_t>(n));
            else if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK) drop = true;
            const std::size_t newline = a.buffer.find('\n');
            if (newline != std::string::npos) {
                std::string line = a.buffer.substr(0, newline);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line == kWelcome) {   // 就是它：用这条连接，其他的关掉
                    sock_ = a.sock;
                    peer_ = a.ip + ":" + std::to_string(port_);
                    buffer_ = a.buffer.substr(newline + 1);
                    attempts_.erase(attempts_.begin() + static_cast<std::ptrdiff_t>(i));
                    for (const Attempt& other : attempts_) closesocket(static_cast<SOCKET>(other.sock));
                    attempts_.clear();
                    stage_ = LINES;
                    state_ = CONNECTED;
                    return;
                }
                drop = true;
            } else if (a.buffer.size() > kMaxHello) {
                drop = true;
            }
        }
        if (drop) {
            closesocket(s);
            attempts_.erase(attempts_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        ++i;
    }
    if (attempts_.empty()) fail("连不上对方（可能不在同一个网络，或者被防火墙拦住了）。");
}

// 等待验证的连接：浏览器发 WebSocket 握手（口令在地址里），电脑客户端发一行“IMCHAT-P2P 口令”
void DirectLink::handshake() {
    if (buffer_.size() < 4 && std::string("GET ").compare(0, buffer_.size(), buffer_) == 0) return;   // 还看不出是哪种
    if (buffer_.compare(0, 4, "GET ") == 0) {
        HttpRequest request;
        std::size_t consumed = 0;
        const HttpRequest::ParseResult result = HttpRequest::parse(buffer_.data(), buffer_.size(), request, consumed);
        if (result == HttpRequest::ParseResult::NEED_MORE) return;
        if (result == HttpRequest::ParseResult::OK && request.isWebSocketUpgrade() && request.path == "/p2p" &&
            request.query == "key=" + key_) {
            buffer_.erase(0, consumed);
            stage_ = WEBSOCKET;
            connected();
            sendRaw("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                    WebSocketFrame::acceptKey(request.header("sec-websocket-key")) + "\r\n\r\n");
            return;
        }
        sendRaw("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        dropProbe();
        return;
    }
    const std::size_t newline = buffer_.find('\n');
    if (newline == std::string::npos) {
        if (buffer_.size() > kMaxHello) dropProbe();
        return;
    }
    std::string line = buffer_.substr(0, newline);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != kHello + key_) {
        dropProbe();
        return;
    }
    buffer_.erase(0, newline + 1);
    stage_ = LINES;
    connected();
    sendRaw(kWelcome + "\n");
}

void DirectLink::readFrames(std::vector<std::string>& lines) {
    using Opcode = WebSocketFrame::Opcode;
    while (true) {
        WebSocketFrame frame;
        std::size_t consumed = 0;
        const WebSocketFrame::DecodeResult result = WebSocketFrame::decode(buffer_.data(), buffer_.size(), kMaxMessage, frame, consumed);
        if (result == WebSocketFrame::DecodeResult::NEED_MORE) return;
        if (result == WebSocketFrame::DecodeResult::BAD) {
            fail("对方发来的数据不符合协议。");
            return;
        }
        buffer_.erase(0, consumed);
        if (frame.opcode == Opcode::PING) {
            sendRaw(WebSocketFrame::encode(Opcode::PONG, frame.payload.data(), frame.payload.size()));
            continue;
        }
        if (frame.opcode == Opcode::PONG) continue;
        if (frame.opcode == Opcode::CLOSE) {
            sendRaw(WebSocketFrame::encode(Opcode::CLOSE, frame.payload.data(), (std::min)(frame.payload.size(), std::size_t(2))));
            fail("对方已断开连接。");
            return;
        }
        message_ += frame.payload;   // 文本、二进制消息，或分片消息的后续部分
        if (message_.size() > kMaxMessage) {
            fail("对方发来的消息太长。");
            return;
        }
        if (!frame.fin) continue;
        std::string line;
        message_ += "\n";
        while (NetUtil::takeLine(message_, line)) {
            if (!line.empty()) lines.push_back(line);
        }
        message_.clear();
    }
}

void DirectLink::connected() {
    state_ = CONNECTED;
    if (listen_ != INVALID_SOCKET) {   // 点对点聊天只接受一个连接
        closesocket(static_cast<SOCKET>(listen_));
        listen_ = INVALID_SOCKET;
    }
}

void DirectLink::dropProbe() {
    if (sock_ != INVALID_SOCKET) closesocket(static_cast<SOCKET>(sock_));
    sock_ = INVALID_SOCKET;
    buffer_.clear();
    stage_ = HELLO;
}

bool DirectLink::send(const std::string& line) {
    if (state_ != CONNECTED) return false;
    if (stage_ == WEBSOCKET) return sendRaw(WebSocketFrame::encode(WebSocketFrame::Opcode::TEXT, line.data(), line.size()));
    return sendRaw(line + "\n");
}

bool DirectLink::sendRaw(const std::string& data) {
    // 套接字是非阻塞的：发不动时等它可写（聊天消息很短，几乎不会发生），最多等 2 秒
    std::size_t sent = 0;
    while (sent < data.size()) {
        const int n = ::send(static_cast<SOCKET>(sock_), data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            fail("发送失败，连接已断开。");
            return false;
        }
        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(static_cast<SOCKET>(sock_), &writeSet);
        timeval wait{ 2, 0 };
        if (select(0, nullptr, &writeSet, nullptr, &wait) <= 0) {
            fail("发送超时，对方可能已经断开。");
            return false;
        }
    }
    return true;
}

void DirectLink::fail(const std::string& error) {
    close();
    error_ = error;
    state_ = CLOSED;
}

void DirectLink::close() {
    for (const Attempt& a : attempts_) closesocket(static_cast<SOCKET>(a.sock));
    attempts_.clear();
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
    message_.clear();
    if (state_ != CLOSED) state_ = IDLE;
}

std::vector<std::string> DirectLink::localAddresses() {
    std::vector<std::string> result;
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return { "127.0.0.1" };
    char name[256] = {};
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* list = nullptr;
    if (gethostname(name, sizeof(name)) == 0 && getaddrinfo(name, nullptr, &hints, &list) == 0) {
        for (addrinfo* p = list; p; p = p->ai_next) {
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in*>(p->ai_addr)->sin_addr, ip, sizeof(ip));
            const std::string text = ip;
            // 回环地址放到最后；169.254 是没有拿到地址时系统自己配的，别人连不过来
            if (text.rfind("127.", 0) == 0 || text.rfind("169.254.", 0) == 0) continue;
            if (std::find(result.begin(), result.end(), text) == result.end()) result.push_back(text);
        }
        freeaddrinfo(list);
    }
    WSACleanup();
    result.push_back("127.0.0.1");
    return result;
}
