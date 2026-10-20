// 点对点聊天的直连（DirectLink 在 Windows 的 imchat_net 库里，和图形客户端一样只在 Windows 上编译）
#ifdef _WIN32
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "DirectLink.h"
#include "SocketOps.h"
#include "WebSocket.h"

namespace {

using Opcode = WebSocketFrame::Opcode;

// 轮询两端直到 done() 成立，最多 2 秒；收到的行分别放进 a、b
template <typename Done>
bool pump(DirectLink& x, DirectLink* y, std::vector<std::string>& a, std::vector<std::string>& b, Done done) {
    for (int i = 0; i < 200; ++i) {
        for (const std::string& line : x.poll()) a.push_back(line);
        if (y) for (const std::string& line : y->poll()) b.push_back(line);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// 模拟浏览器：阻塞的套接字，发 WebSocket 握手和带掩码的帧
class FakeBrowser {
public:
    explicit FakeBrowser(unsigned short port) : socket_(SocketOps::connectTcp("127.0.0.1", port)) {}
    ~FakeBrowser() { SocketOps::close(socket_); }
    void sendRaw(const std::string& bytes) { SocketOps::sendSome(socket_, bytes.data(), bytes.size()); }
    // 读到至少 atLeast 个字节（同时轮询 host，它收到的行放进 hostLines）
    std::string read(DirectLink& host, std::size_t atLeast, std::vector<std::string>& hostLines) {
        std::vector<std::string> none;
        u_long nonBlocking = 1;
        ioctlsocket(socket_, FIONBIO, &nonBlocking);
        pump(host, nullptr, hostLines, none, [&] {
            char chunk[4096];
            const long n = SocketOps::recvSome(socket_, chunk, sizeof(chunk));
            if (n > 0) received_.append(chunk, static_cast<std::size_t>(n));
            return received_.size() >= atLeast || n == 0;
        });
        return received_;
    }

private:
    SocketHandle socket_;
    std::string received_;
};

std::string upgrade(const std::string& key) {
    return "GET /p2p?key=" + key + " HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
}

}  // namespace

TEST(DirectLinkTest, TwoDesktopClientsChatOverTcp) {
    DirectLink host, guest;
    std::string error;
    ASSERT_TRUE(host.host("secret42", error)) << error;
    EXPECT_GT(host.port(), 0);
    ASSERT_TRUE(guest.connect({ "127.0.0.1" }, host.port(), "secret42", error)) << error;
    std::vector<std::string> a, b;
    ASSERT_TRUE(pump(host, &guest, a, b, [&] { return host.state() == DirectLink::CONNECTED && guest.state() == DirectLink::CONNECTED; }));
    EXPECT_FALSE(host.webSocket());
    ASSERT_TRUE(guest.send("[小张] 你好"));
    ASSERT_TRUE(host.send("[李小四] 收到"));
    ASSERT_TRUE(pump(host, &guest, a, b, [&] { return !a.empty() && !b.empty(); }));
    EXPECT_EQ(a, std::vector<std::string>{ "[小张] 你好" });
    EXPECT_EQ(b, std::vector<std::string>{ "[李小四] 收到" });

    guest.close();   // 一方结束，另一方看到断开
    ASSERT_TRUE(pump(host, nullptr, a, b, [&] { return host.state() == DirectLink::CLOSED; }));
    EXPECT_FALSE(host.error().empty());
}

TEST(DirectLinkTest, WrongKeyIsDroppedAndTheHostKeepsWaiting) {
    DirectLink host, intruder, guest;
    std::string error;
    ASSERT_TRUE(host.host("right", error));
    ASSERT_TRUE(intruder.connect({ "127.0.0.1" }, host.port(), "wrong", error));
    std::vector<std::string> a, b;
    ASSERT_TRUE(pump(host, &intruder, a, b, [&] { return intruder.state() == DirectLink::CLOSED; }));   // 被关掉
    EXPECT_EQ(host.state(), DirectLink::LISTENING);
    ASSERT_TRUE(guest.connect({ "127.0.0.1" }, host.port(), "right", error));
    ASSERT_TRUE(pump(host, &guest, a, b, [&] { return host.state() == DirectLink::CONNECTED; }));
}

TEST(DirectLinkTest, BrowserConnectsWithWebSocket) {
    DirectLink host;
    std::string error;
    ASSERT_TRUE(host.host("k9", error));
    FakeBrowser browser(host.port());
    browser.sendRaw(upgrade("k9") + WebSocketFrame::encodeMasked(Opcode::TEXT, "[网页] 你好", 0x01020304));
    std::vector<std::string> a, b;
    const std::string head = browser.read(host, 20, a);
    EXPECT_EQ(head.rfind("HTTP/1.1 101", 0), 0u) << head;
    ASSERT_TRUE(pump(host, nullptr, a, b, [&] { return !a.empty(); }));
    EXPECT_TRUE(host.webSocket());
    EXPECT_EQ(a.front(), "[网页] 你好");

    ASSERT_TRUE(host.send("[电脑] 回复"));
    const std::string all = browser.read(host, head.size() + 2 + std::string("[电脑] 回复").size(), a);
    const std::string frame = all.substr(all.find("\r\n\r\n") + 4);
    ASSERT_GE(frame.size(), 2u);
    EXPECT_EQ(static_cast<unsigned char>(frame[0]), 0x81u);   // FIN + 文本，服务端发的帧不加掩码
    EXPECT_EQ(frame.substr(2), "[电脑] 回复");
}

TEST(DirectLinkTest, BrowserWithWrongKeyGets403) {
    DirectLink host;
    std::string error;
    ASSERT_TRUE(host.host("k9", error));
    FakeBrowser browser(host.port());
    browser.sendRaw(upgrade("nope"));
    std::vector<std::string> lines;
    EXPECT_EQ(browser.read(host, 12, lines).rfind("HTTP/1.1 403", 0), 0u);
    EXPECT_EQ(host.state(), DirectLink::LISTENING);
}

TEST(DirectLinkTest, TriesEveryAddressAndUsesTheOneThatWorks) {
    DirectLink host, guest;
    std::string error;
    ASSERT_TRUE(host.host("multi", error));
    // 第一个地址无效（被跳过），第二个连不上的保留地址会一直等着，第三个是本机
    ASSERT_TRUE(guest.connect({ "not-an-ip", "192.0.2.1", "127.0.0.1" }, host.port(), "multi", error));
    std::vector<std::string> a, b;
    ASSERT_TRUE(pump(host, &guest, a, b, [&] { return guest.state() == DirectLink::CONNECTED && host.state() == DirectLink::CONNECTED; }))
        << "guest " << guest.state() << " " << guest.error() << ", host " << host.state() << " " << host.error();
    EXPECT_EQ(guest.peer(), "127.0.0.1:" + std::to_string(host.port()));
    EXPECT_FALSE(DirectLink::localAddresses().empty());
    EXPECT_EQ(DirectLink::localAddresses().back(), "127.0.0.1");
}
#endif
