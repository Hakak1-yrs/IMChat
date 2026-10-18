#include "ChatRoomServer.h"
#include "NetUtil.h"   // 内含 winsock2.h，必须先于其他 Windows 头文件
#include <deque>
#include <mutex>
#include <sstream>
#include "Platform.h"
#include "Util.h"

namespace {
const size_t kMaxMembers = 50;           // select 一次最多照看 64 个套接字（FD_SETSIZE），留出余量
const size_t kMaxLineBytes = 64 * 1024;  // 一直不换行的连接视为异常，断开
}  // namespace

// 具体实现放在 .cpp 中，头文件里不出现 SOCKET 等 Winsock 类型
struct ChatRoomServer::Impl {
    // 群聊室中的一个连接
    struct Client {
        SOCKET sock;
        std::string buffer;          // 还没凑成整行的数据
        ServiceBase* account;      // 通过身份核对后才有值
        bool closed;
    };

    Platform& platform;
    Group& group;
    ServiceBase& host;
    Show show;
    std::string title;
    bool wsaReady = false;
    SOCKET listenSock = INVALID_SOCKET;
    std::vector<Client> clients;
    std::mutex inboxMutex;
    std::deque<std::string> inbox;   // 主持人的发言

    Impl(Platform& p, Group& g, ServiceBase& h, Show s)
        : platform(p), group(g), host(h), show(std::move(s)) {
        title = ServiceTypeHelper::name(g.service()) + "群 " + std::to_string(g.groupNo()) + "（" + g.name() + "）";
        WSADATA wsaData;
        wsaReady = WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
    }

    ~Impl() {
        if (wsaReady) WSACleanup();
    }

    void acceptOne() {
        SOCKET sock = accept(listenSock, nullptr, nullptr);
        if (sock == INVALID_SOCKET) return;
        if (clients.size() >= kMaxMembers) {
            NetUtil::sendAll(sock, "NO 群聊室人数已满。\n");
            closesocket(sock);
            return;
        }
        clients.push_back({ sock, "", nullptr, false });
    }

    void receiveFrom(size_t index) {
        char buf[1024];
        int n = recv(clients[index].sock, buf, sizeof(buf), 0);
        if (n <= 0) {   // 对方离开或连接出错
            clients[index].closed = true;
            return;
        }
        clients[index].buffer.append(buf, n);
        std::string line;
        // 处理一行时可能向其他成员发送数据，但不会增删 clients，所以按下标访问是安全的
        while (!clients[index].closed && NetUtil::takeLine(clients[index].buffer, line)) handleLine(clients[index], line);
        if (clients[index].buffer.size() > kMaxLineBytes) clients[index].closed = true;
    }

    void handleLine(Client& c, const std::string& line) {
        if (!c.account) {
            verify(c, line);
            return;
        }
        if (line.compare(0, 4, "MSG ") == 0) {
            const std::string text = Util::lineText(line.substr(4));
            std::string msg;
            if (!platform.sendGroupMessage(*c.account, group, text, msg)) {   // 不是群成员、被禁言、内容为空等
                sendTo(c, "[系统] " + msg);
                return;
            }
            const std::string out = "[" + c.account->displayName() + "] " + text;
            broadcast(out, &c);
            show(out);
        } else if (line == "WHO") {
            sendTo(c, "[系统] 当前在线：" + onlineText());
        }
    }

    // 新连接的第一行必须是 HELLO 服务 群号 账号；只有本群成员才能进入
    void verify(Client& c, const std::string& line) {
        std::istringstream iss(line);
        std::string command, code, id;
        int groupNo = 0;
        ServiceType type;
        if (!(iss >> command >> code >> groupNo >> id) || command != "HELLO") {
            reject(c, "这里是" + title + "的群聊室，请从“群聊天 → 加入实时群聊室”进入。");
            return;
        }
        if (!ServiceTypeHelper::parse(code, type) || type != group.service() || groupNo != group.groupNo()) {
            reject(c, "这里是" + title + "的群聊室，与你要加入的群不一致。");
            return;
        }
        ServiceBase* account = platform.findAccount(type, id);
        if (!account || !group.isMember(id)) {
            reject(c, "你不是" + title + "的成员（以开设群聊室一方的数据为准）。");
            return;
        }
        if (id == host.id() || findOnline(id)) {
            reject(c, "账号 " + id + " 已经在群聊室中。");
            return;
        }
        c.account = account;
        sendTo(c, "OK 已进入" + title + "的群聊室。当前在线：" + onlineText());
        const std::string notice = "[系统] " + account->displayName() + " 进入了群聊室";
        broadcast(notice, &c);
        show(notice);
    }

    void reject(Client& c, const std::string& reason) {
        NetUtil::sendAll(c.sock, "NO " + reason + "\n");
        c.closed = true;
    }

    void hostSays(const std::string& text) {
        if (text == "/who") {
            show("[系统] 当前在线：" + onlineText());
            return;
        }
        std::string msg;
        if (!platform.sendGroupMessage(host, group, text, msg)) {
            show("[系统] " + msg);
            return;
        }
        broadcast("[" + host.displayName() + "] " + text, nullptr);
    }

    void dropClosed() {
        for (size_t i = 0; i < clients.size();) {
            if (!clients[i].closed) {
                ++i;
                continue;
            }
            closesocket(clients[i].sock);
            ServiceBase* left = clients[i].account;
            clients.erase(clients.begin() + static_cast<std::ptrdiff_t>(i));
            if (left) {
                const std::string notice = "[系统] " + left->displayName() + " 离开了群聊室";
                broadcast(notice, nullptr);
                show(notice);
            }
        }
    }

    // 发给所有已进入群聊室的成员（except 除外）
    void broadcast(const std::string& text, const Client* except) {
        for (Client& c : clients) {
            if (&c != except && c.account && !c.closed) sendTo(c, text);
        }
    }

    void sendTo(Client& c, const std::string& text) {
        if (!NetUtil::sendAll(c.sock, text + "\n")) c.closed = true;
    }

    bool findOnline(const std::string& id) const {
        for (const Client& c : clients) {
            if (c.account && !c.closed && c.account->id() == id) return true;
        }
        return false;
    }

    std::vector<std::string> onlineNames() const {
        std::vector<std::string> names{ host.displayName() };
        for (const Client& c : clients) {
            if (c.account && !c.closed) names.push_back(c.account->displayName());
        }
        return names;
    }

    std::string onlineText() const {
        std::vector<std::string> names = onlineNames();
        std::string text = names[0] + "（主持人）";
        for (size_t i = 1; i < names.size(); ++i) text += "、" + names[i];
        return text + "，共 " + std::to_string(names.size()) + " 人";
    }
};

ChatRoomServer::ChatRoomServer(Platform& platform, Group& group, ServiceBase& host, Show show)
    : impl_(std::make_unique<Impl>(platform, group, host, std::move(show))) {}

ChatRoomServer::~ChatRoomServer() { stop(); }

std::string ChatRoomServer::title() const { return impl_->title; }

bool ChatRoomServer::running() const { return impl_->listenSock != INVALID_SOCKET; }

bool ChatRoomServer::start(unsigned short port, std::string& error) {
    Impl& m = *impl_;
    if (!m.wsaReady) {
        error = "Winsock 初始化失败。";
        return false;
    }
    m.listenSock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m.listenSock == INVALID_SOCKET) {
        error = "创建套接字失败。";
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(m.listenSock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(m.listenSock, SOMAXCONN) == SOCKET_ERROR) {
        error = "端口 " + std::to_string(port) + " 绑定/监听失败（可能已被占用），错误码 " + std::to_string(WSAGetLastError());
        closesocket(m.listenSock);
        m.listenSock = INVALID_SOCKET;
        return false;
    }
    return true;
}

void ChatRoomServer::poll(int timeoutMs) {
    Impl& m = *impl_;
    if (m.listenSock == INVALID_SOCKET) return;
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(m.listenSock, &readSet);
    for (const Impl::Client& c : m.clients) FD_SET(c.sock, &readSet);
    timeval timeout{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    int ready = select(0, &readSet, nullptr, nullptr, &timeout);

    std::deque<std::string> said;
    {
        std::lock_guard<std::mutex> lock(m.inboxMutex);
        said.swap(m.inbox);
    }
    for (const std::string& text : said) m.hostSays(text);
    if (ready <= 0) return;
    if (FD_ISSET(m.listenSock, &readSet)) m.acceptOne();
    for (size_t i = 0; i < m.clients.size(); ++i) {
        if (FD_ISSET(m.clients[i].sock, &readSet)) m.receiveFrom(i);
    }
    m.dropClosed();
}

void ChatRoomServer::say(const std::string& text) {
    std::lock_guard<std::mutex> lock(impl_->inboxMutex);
    impl_->inbox.push_back(text);
}

void ChatRoomServer::stop() {
    Impl& m = *impl_;
    if (m.listenSock == INVALID_SOCKET) return;
    m.broadcast("[系统] 主持人 " + m.host.displayName() + " 关闭了群聊室。", nullptr);
    for (Impl::Client& c : m.clients) {
        shutdown(c.sock, SD_BOTH);
        closesocket(c.sock);
    }
    m.clients.clear();
    closesocket(m.listenSock);
    m.listenSock = INVALID_SOCKET;
}

std::vector<std::string> ChatRoomServer::online() const { return impl_->onlineNames(); }
