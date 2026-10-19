// IMChat 压测工具。
//   IMChatBench prepare <数据目录> [用户数=200]
//       用业务接口注册一批 QQ 账号：相邻两人互为好友（1-2、3-4……），前 100 人在同一个群里，账号列表写入 bench_accounts.txt
//   IMChatBench run --data <数据目录> [--host 127.0.0.1] [--port 9000] [--clients 200] [--messages 100]
//                   [--threads 4] [--window 1] [--group | --remark]
//       每个客户端一条连接，登录后给好友连发 messages 条私信（--group 时发到群里；--remark 时改为反复修改好友备注），
//       统计吞吐量、回执延迟（p50 / p90 / p99 / 最大），以及对方实际收到的推送条数。
//   IMChatBench store <目录> text|sqlite [次数]   存储层微基准
//   IMChatBench save <数据目录> [次数]             加好友后保存数据文件的耗时
//   IMChatBench social <目录> text|sqlite [记录数]  社交记录很多时发一条私信的耗时
// 客户端用的是项目自己的网络库（EventLoop + TcpClient）：threads 个事件循环线程，每个线程负责一部分连接
#include "EventLoop.h"   // 网络头文件（winsock2.h）必须在其他 Windows 头文件之前
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "Date.h"
#include "Frame.h"
#include "Group.h"
#include "Log.h"
#include "MessageCenter.h"
#include "Packet.h"
#include "Platform.h"
#include "SqliteMessageStore.h"
#include "TcpClient.h"
#include "Util.h"

#ifdef _WIN32
#include <shellapi.h>
#ifdef _MSC_VER
#pragma comment(lib, "shell32.lib")
#endif
#endif

using json = nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

const char* kAccountsFile = "bench_accounts.txt";
const int kGroupLimit = 100;   // 压测群的人数上限

// ======================== prepare：准备压测数据 ========================

int prepare(std::string dataDir, int users) {
    if (dataDir.back() != '/' && dataDir.back() != '\\') dataDir += '/';
    Platform platform(dataDir);
    std::vector<ServiceBase*> accounts;
    std::string msg;
    for (int i = 0; i < users; ++i) {
        ServiceBase* account = platform.registerUser("压测用户" + std::to_string(i + 1), ServiceType::QQ,
                                                     "bench" + std::to_string(i + 1), Date(2000, 1, 1), "上海", "123456", msg);
        if (!account) {
            std::cerr << "注册失败：" << msg << "\n";
            return 1;
        }
        accounts.push_back(account);
    }
    for (int i = 0; i + 1 < users; i += 2) platform.addFriend(*accounts[i], accounts[i + 1]->id(), "", msg);
    Group* group = platform.createGroup(*accounts[0], "压测群", msg);
    for (int i = 1; group && i < std::min(users, kGroupLimit); ++i) group->invite(accounts[0]->id(), accounts[i]->id(), msg);
    if (!platform.save()) {
        std::cerr << "保存失败\n";
        return 1;
    }
    std::ofstream out(Util::pathOf(dataDir + kAccountsFile));
    if (group) out << "# group " << group->groupNo() << "\n";
    for (ServiceBase* account : accounts) out << account->id() << "\n";
    std::cout << "已注册 " << users << " 个账号（两两互为好友），压测群 " << (group ? group->groupNo() : 0) << "，账号列表："
              << dataDir << kAccountsFile << "\n";
    return 0;
}

// ======================== run：压测 ========================

struct Config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 9000;
    int clients = 200;
    int messages = 100;
    int threads = 4;
    int window = 1;        // 每个客户端同时在途（发出还没收到回执）的消息数上限
    bool group = false;
    bool remark = false;   // 不发消息，改为反复修改好友备注（每次都要保存数据文件）
    int groupNo = 0;
    std::string runId;     // 本次运行的随机编号，拼进 clientMsgId
};

// 全部客户端共享的计数
struct Stats {
    std::atomic<int> loggedIn{ 0 };
    std::atomic<int> loginFailed{ 0 };
    std::atomic<int> acked{ 0 };
    std::atomic<int> rejected{ 0 };
    std::atomic<long long> pushes{ 0 };
    std::mutex mutex;
    std::vector<double> latenciesMs;   // 每条消息从发出到收到回执的时间
};

// 一个压测客户端：一条连接，登录 → 等开始信号 → 按窗口大小连续发消息，记录每条的回执延迟
class BenchClient {
public:
    BenchClient(EventLoop& loop, const Config& config, Stats& stats, std::string account, std::string peer)
        : config_(config), stats_(stats), account_(std::move(account)), peer_(std::move(peer)),
          client_(loop, config.host, config.port) {
        client_.setConnectTimeout(std::chrono::seconds(10));
        client_.setConnectionCallback([this](const TcpConnection::Ptr&) {
            send(MessageType::LOGIN_REQ, { { "service", "QQ" }, { "account", account_ }, { "password", "123456" } });
        });
        client_.setMessageCallback([this](const TcpConnection::Ptr& c, Buffer& b) { onBytes(c, b); });
    }

    void connect() { client_.connect(); }
    void disconnect() { client_.disconnect(); }

    void startSending() {
        for (int i = 0; i < config_.window; ++i) sendNext();
    }

    void flushLatencies() {
        std::lock_guard<std::mutex> lock(stats_.mutex);
        stats_.latenciesMs.insert(stats_.latenciesMs.end(), latencies_.begin(), latencies_.end());
    }

private:
    void send(MessageType type, json body) {
        TcpConnection::Ptr connection = client_.connection();
        if (!connection) return;
        Packet packet;
        packet.type = type;
        packet.body = std::move(body);
        connection->send(packet.encode());
    }

    void sendNext() {
        if (sent_ >= config_.messages) return;
        if (config_.remark) {   // 通用请求，按 reqId 对应回执
            const std::string id = std::to_string(++sent_);
            inflight_[id] = Clock::now();
            send(MessageType::CALL_REQ, { { "reqId", sent_ }, { "op", "friends.remark" },
                                          { "args", { { "id", peer_ }, { "remark", "备注" + id } } } });
            return;
        }
        // clientMsgId 每次运行都不同：服务端按它去重，重复的编号会被当成重发，直接返回原来的回执、不再保存和推送
        const std::string id = config_.runId + "-" + account_ + "-" + std::to_string(++sent_);
        json request = { { "clientMsgId", id }, { "text", "压测消息 " + id } };
        if (config_.group) request["groupNo"] = config_.groupNo;
        else request["to"] = peer_;
        inflight_[id] = Clock::now();
        send(MessageType::SEND_MSG_REQ, std::move(request));
    }

    void onBytes(const TcpConnection::Ptr& connection, Buffer& buffer) {
        while (connection->connected()) {
            Frame frame;
            std::size_t consumed = 0;
            if (Frame::decode(buffer.peek(), buffer.readableBytes(), frame, consumed) != Frame::DecodeResult::OK) return;
            buffer.retrieve(consumed);
            if (frame.type == MessageType::MSG_PUSH) {   // 推送只计数，不解析 JSON：让压测客户端本身尽量不成为瓶颈
                ++stats_.pushes;
                continue;
            }
            if (std::optional<Packet> packet = Packet::fromFrame(frame)) handle(*packet);
        }
    }

    void handle(const Packet& packet) {
        if (packet.type == MessageType::LOGIN_RESP) {
            if (packet.body.value("ok", false)) ++stats_.loggedIn;
            else ++stats_.loginFailed;
        } else if (packet.type == MessageType::SEND_MSG_ACK || packet.type == MessageType::CALL_RESP) {
            const std::string key = packet.type == MessageType::CALL_RESP ? std::to_string(packet.body.value("reqId", 0))
                                                                         : packet.body.value("clientMsgId", "");
            auto it = inflight_.find(key);
            if (it == inflight_.end()) return;
            latencies_.push_back(std::chrono::duration<double, std::milli>(Clock::now() - it->second).count());
            inflight_.erase(it);
            if (!packet.body.value("ok", false)) ++stats_.rejected;
            ++stats_.acked;
            sendNext();
        }
    }

    const Config& config_;
    Stats& stats_;
    std::string account_;
    std::string peer_;
    TcpClient client_;
    int sent_ = 0;
    std::unordered_map<std::string, Clock::time_point> inflight_;
    std::vector<double> latencies_;
};

// 一个事件循环线程及其负责的客户端
class Worker {
public:
    void start(const Config& config, Stats& stats, std::vector<std::pair<std::string, std::string>> pairs) {
        std::atomic<bool> ready{ false };
        thread_ = std::thread([this, &config, &stats, pairs, &ready] {
            EventLoop loop;
            loop_ = &loop;
            for (const auto& [account, peer] : pairs) clients_.push_back(std::make_unique<BenchClient>(loop, config, stats, account, peer));
            ready = true;
            for (auto& client : clients_) client->connect();
            loop.loop();
            for (auto& client : clients_) client->flushLatencies();
            clients_.clear();   // TcpClient 必须在事件循环线程里销毁
        });
        while (!ready) std::this_thread::yield();
    }
    void run(std::function<void(BenchClient&)> action) {
        loop_->runInLoop([this, action] {
            for (auto& client : clients_) action(*client);
        });
    }
    void stop() {
        run([](BenchClient& c) { c.disconnect(); });
        loop_->runInLoop([this] { loop_->quit(); });
        thread_.join();
    }

private:
    std::thread thread_;
    EventLoop* loop_ = nullptr;
    std::vector<std::unique_ptr<BenchClient>> clients_;
};

double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0;
    const std::size_t index = static_cast<std::size_t>(p * (sorted.size() - 1) + 0.5);
    return sorted[std::min(index, sorted.size() - 1)];
}

template <typename Predicate>
bool waitFor(Predicate done, std::chrono::seconds timeout) {
    const auto deadline = Clock::now() + timeout;
    while (!done()) {
        if (Clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

int run(Config config, const std::string& dataDir) {
    std::ifstream in(Util::pathOf(dataDir + kAccountsFile));
    std::vector<std::string> accounts;
    for (std::string line; std::getline(in, line);) {
        line = Util::trim(line);
        if (line.rfind("# group ", 0) == 0) config.groupNo = std::atoi(line.c_str() + 8);
        else if (!line.empty()) accounts.push_back(line);
    }
    if (config.group) config.clients = std::min(config.clients, kGroupLimit);   // 只有前 100 人在群里
    config.clients = std::min<int>(config.clients, static_cast<int>(accounts.size()) / 2 * 2);
    if (config.clients <= 0) {
        std::cerr << "没有找到压测账号，请先运行 IMChatBench prepare <数据目录>\n";
        return 1;
    }
    config.threads = std::max(1, std::min(config.threads, config.clients));
    config.runId = std::to_string(std::random_device{}());
    std::cout << "服务器 " << config.host << ":" << config.port << "，" << config.clients << " 个客户端，" << config.threads
              << " 个线程，每个客户端发 " << config.messages << " 条" << (config.remark ? "改备注请求" : config.group ? "群消息" : "私信") << "，窗口 "
              << config.window << "\n";

    Stats stats;
    std::vector<Worker> workers(config.threads);
    std::vector<std::vector<std::pair<std::string, std::string>>> assignment(config.threads);
    for (int i = 0; i < config.clients; ++i) assignment[i % config.threads].push_back({ accounts[i], accounts[i ^ 1] });

    const auto loginStart = Clock::now();
    for (int t = 0; t < config.threads; ++t) workers[t].start(config, stats, assignment[t]);
    const bool allIn = waitFor([&] { return stats.loggedIn + stats.loginFailed >= config.clients; }, std::chrono::seconds(60));
    const double loginSeconds = std::chrono::duration<double>(Clock::now() - loginStart).count();
    std::printf("登录：%d 成功，%d 失败，用时 %.2f s%s\n", stats.loggedIn.load(), stats.loginFailed.load(), loginSeconds,
                allIn ? "" : "（超时）");

    const int expected = stats.loggedIn * config.messages;
    const auto sendStart = Clock::now();
    for (Worker& w : workers) w.run([](BenchClient& c) { c.startSending(); });
    const bool finished = waitFor([&] { return stats.acked >= expected; }, std::chrono::seconds(600));
    const double seconds = std::chrono::duration<double>(Clock::now() - sendStart).count();
    // 推送可能比回执晚一点到：再等一会儿
    const long long expectedPushes = config.remark ? 0 : config.group ? static_cast<long long>(expected) * (stats.loggedIn - 1) : expected;
    waitFor([&] { return stats.pushes >= expectedPushes; }, std::chrono::seconds(5));
    for (Worker& w : workers) w.stop();

    std::vector<double> latencies = std::move(stats.latenciesMs);
    std::sort(latencies.begin(), latencies.end());
    std::printf("消息：%d 条，用时 %.2f s，吞吐 %.0f 条/秒%s\n", stats.acked.load(), seconds, stats.acked / seconds,
                finished ? "" : "（超时，未全部完成）");
    std::printf("回执延迟：p50 %.2f ms，p90 %.2f ms，p99 %.2f ms，最大 %.2f ms\n", percentile(latencies, 0.5),
                percentile(latencies, 0.9), percentile(latencies, 0.99), latencies.empty() ? 0.0 : latencies.back());
    std::printf("推送：收到 %lld / 应收 %lld；被服务端拒绝 %d 条\n", stats.pushes.load(), expectedPushes, stats.rejected.load());
    return finished && stats.pushes >= expectedPushes && stats.rejected == 0 ? 0 : 2;
}

// ======================== store：存储层微基准 ========================

// 不经过网络，直接测消息存储：服务端处理一条私信时对存储做的操作（读社交记录、保存、取回刚保存的那条）
int storeBench(std::string dataDir, const std::string& kind, int count) {
    if (dataDir.back() != '/' && dataDir.back() != '\\') dataDir += '/';
    std::error_code ec;
    std::filesystem::create_directories(Util::pathOf(dataDir), ec);
    std::unique_ptr<MessageStore> store;
    if (kind == "sqlite") store = std::make_unique<SqliteMessageStore>(dataDir + "store_bench.db");
    else store = std::make_unique<MessageCenter>(dataDir + "store_bench.txt");
    Message last(0, ServiceType::QQ, false, "", "", "", "");
    auto timeIt = [&](const char* name, const std::function<void(int)>& step, int times = 0) {
        if (times == 0) times = count;
        const auto start = Clock::now();
        for (int i = 0; i < times; ++i) step(i);
        const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / times;
        std::printf("  %-28s %8.1f 微秒/次\n", name, us);
    };
    std::printf("%s，%d 次：\n", kind == "sqlite" ? "SQLite" : "文本日志", count);
    timeIt("保存一条私信", [&](int i) { store->postPrivate(ServiceType::QQ, "10001", "10002", "压测 " + std::to_string(i)); });
    timeIt("取回刚保存的那条（lastFrom）", [&](int) { store->lastFrom(ServiceType::QQ, "10001", last); });
    timeIt("读社交记录（records）", [&](int) { store->records("FVDBS"); });
    timeIt("保存 + 取回 + 读记录", [&](int i) {
        store->records("FVDBS");
        store->postPrivate(ServiceType::QQ, "10001", "10002", "压测 " + std::to_string(i));
        store->lastFrom(ServiceType::QQ, "10001", last);
    });
    // 客户端重连后的增量同步：只差最后 20 条。旧做法读出全部历史再跳过，新做法只读需要的部分
    for (int i = 0; i < count / 2; ++i) store->postGroup(ServiceType::QQ, 1001, "10003", "群消息 " + std::to_string(i));
    store->postPrivate(ServiceType::QQ, "10001", "10002", "最后一条");
    store->lastFrom(ServiceType::QQ, "10001", last);
    const int afterSeq = last.seq() - 20;
    const std::size_t total = store->messagesFor(ServiceType::QQ, "10002", { 1001 }).size();
    std::printf("  （同步：这个人共有 %zu 条消息，只差最后 20 条）\n", total);
    timeIt("同步：全部读出再跳过", [&](int) {
        std::size_t n = 0;
        for (const Message& m : store->messagesFor(ServiceType::QQ, "10002", { 1001 })) n += m.seq() > afterSeq;
    }, 100);
    timeIt("同步：只读之后的（messagesAfter）", [&](int) {
        store->messagesAfter(ServiceType::QQ, "10002", { 1001 }, afterSeq, 201);
    }, 100);
    timeIt("同步：第一次（从头取 200 条）", [&](int) {
        store->messagesAfter(ServiceType::QQ, "10002", { 1001 }, 0, 201);
    }, 100);
    // 群操作（邀请、改公告……）之后要推送新产生的系统提示：原来读出整个群的历史找新的，现在只读 lastSeq 之后的
    timeIt("群操作后：读整个群的历史", [&](int) { store->groupHistory(ServiceType::QQ, 1001); }, 20);
    timeIt("群操作后：只读 lastSeq 之后的", [&](int) {
        store->messagesAfter(ServiceType::QQ, "10003", { 1001 }, store->lastSeq(), 0);
    }, 100);
    // 未读数（单机模式取联系人、群列表时要算）：已读到只差最后 20 条
    store->markPrivateRead(ServiceType::QQ, "10002", "10001", afterSeq);
    store->markGroupRead(ServiceType::QQ, 1001, "10002", afterSeq);
    timeIt("未读：私信（unreadPrivate）", [&](int) { store->unreadPrivate(ServiceType::QQ, "10002"); }, 100);
    timeIt("未读：群（unreadGroups）", [&](int) { store->unreadGroups(ServiceType::QQ, "10002", { 1001 }); }, 100);
    return 0;
}

// ======================== save：改一次好友关系后保存数据文件 ========================

// 服务端每次修改好友、群之后都要保存（ChatServer::persist），这里测一次保存要多久：数据目录用 prepare 准备
int saveBench(std::string dataDir, int times) {
    if (dataDir.back() != '/' && dataDir.back() != '\\') dataDir += '/';
    Platform platform(dataDir);
    std::vector<ServiceBase*> accounts;
    std::ifstream in(Util::pathOf(dataDir + kAccountsFile));
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        if (ServiceBase* account = platform.findAccount(ServiceType::QQ, line)) accounts.push_back(account);
    }
    if (accounts.size() < static_cast<std::size_t>(times) * 2 + 2) {
        std::cerr << "账号不够：先用 prepare 准备至少 " << times * 2 + 2 << " 个账号\n";
        return 1;
    }
    std::string msg;
    const auto start = Clock::now();
    for (int i = 0; i < times; ++i) {   // prepare 让 (0,1) (2,3)…… 互为好友，这里再加 (1,2) (3,4)……
        platform.addFriend(*accounts[2 * i + 1], accounts[2 * i + 2]->id(), "", msg);
        if (!platform.save()) {
            std::cerr << "保存失败\n";
            return 1;
        }
    }
    const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count() / times;
    std::printf("%zu 个账号，加好友 + 保存 %d 次：%.2f 毫秒/次\n", accounts.size(), times, ms);
    return 0;
}

// ======================== social：社交记录很多时发私信 ========================

// 发私信前要先应用新的社交记录（好友申请的结果、删除、拉黑、验证设置），这里测记录很多时一条私信要多久
int socialBench(std::string dataDir, const std::string& kind, int records) {
    if (dataDir.back() != '/' && dataDir.back() != '\\') dataDir += '/';
    std::error_code ec;
    std::filesystem::remove_all(Util::pathOf(dataDir), ec);
    Platform platform(dataDir, kind == "sqlite" ? Platform::Storage::SQLITE : Platform::Storage::TEXT);
    std::string msg;
    ServiceBase* a = platform.registerUser("甲", ServiceType::QQ, "a1", Date(2000, 1, 1), "上海", "123456", msg);
    ServiceBase* b = platform.registerUser("乙", ServiceType::QQ, "b1", Date(2000, 1, 1), "上海", "123456", msg);
    if (!a || !b || !platform.addFriend(*a, b->id(), "", msg)) {
        std::cerr << "准备账号失败：" << msg << "\n";
        return 1;
    }
    // 直接写入一批好友申请记录（别人之间的，还没有处理），模拟运行了一段时间的服务端
    for (int i = 0; i < records; ++i)
        platform.messages().appendRecord('F', ServiceType::QQ, { std::to_string(20000 + i), std::to_string(30000 + i), "-" }, true, "你好");
    platform.sendPrivate(*a, b->id(), "第一条：应用全部记录", msg);
    const int times = 1000;
    const auto start = Clock::now();
    for (int i = 0; i < times; ++i) platform.sendPrivate(*a, b->id(), "压测 " + std::to_string(i), msg);
    const double us = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / times;
    std::printf("%s，%d 条社交记录：发一条私信 %.1f 微秒\n", kind == "sqlite" ? "SQLite" : "文本日志", records, us);
    return 0;
}

void usage() {
    std::cout << "用法：\n"
                 "  IMChatBench prepare <数据目录> [用户数=200]\n"
                 "  IMChatBench store <目录> text|sqlite [次数=10000]   存储层微基准\n"
                 "  IMChatBench save <数据目录> [次数=100]   加好友后保存数据文件的耗时（数据目录用 prepare 准备）\n"
                 "  IMChatBench social <目录> text|sqlite [记录数=10000]   社交记录很多时发一条私信的耗时（目录会被清空）\n"
                 "  IMChatBench run --data <数据目录> [--host 127.0.0.1] [--port 9000] [--clients 200] [--messages 100]\n"
                 "                  [--threads 4] [--window 1] [--group | --remark]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    // 命令行参数统一转成 UTF-8（数据目录可能含中文，Windows 的 argv 是本地代码页）
    (void)argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 1; wide && i < count; ++i) args.push_back(Util::narrow(wide[i]));
    if (wide) LocalFree(wide);
#else
    args.assign(argv + 1, argv + argc);
#endif
    (void)argc;
    Log::setLevel(Log::Level::WARN);
    if (args.empty()) {
        usage();
        return 1;
    }
    try {
        if (args[0] == "prepare" && args.size() >= 2) return prepare(args[1], args.size() >= 3 ? std::stoi(args[2]) : 200);
        if (args[0] == "social" && args.size() >= 3) return socialBench(args[1], args[2], args.size() >= 4 ? std::stoi(args[3]) : 10000);
        if (args[0] == "save" && args.size() >= 2) return saveBench(args[1], args.size() >= 3 ? std::stoi(args[2]) : 100);
        if (args[0] == "store" && args.size() >= 3) return storeBench(args[1], args[2], args.size() >= 4 ? std::stoi(args[3]) : 10000);
        if (args[0] == "run") {
            Config config;
            std::string dataDir;
            for (std::size_t i = 1; i < args.size(); ++i) {
                const bool hasValue = i + 1 < args.size();
                if (args[i] == "--data" && hasValue) dataDir = args[++i];
                else if (args[i] == "--host" && hasValue) config.host = args[++i];
                else if (args[i] == "--port" && hasValue) config.port = static_cast<std::uint16_t>(std::stoi(args[++i]));
                else if (args[i] == "--clients" && hasValue) config.clients = std::stoi(args[++i]);
                else if (args[i] == "--messages" && hasValue) config.messages = std::stoi(args[++i]);
                else if (args[i] == "--threads" && hasValue) config.threads = std::stoi(args[++i]);
                else if (args[i] == "--window" && hasValue) config.window = std::max(1, std::stoi(args[++i]));
                else if (args[i] == "--group") config.group = true;
        else if (args[i] == "--remark") config.remark = true;
                else {
                    usage();
                    return 1;
                }
            }
            if (dataDir.empty()) {
                usage();
                return 1;
            }
            if (dataDir.back() != '/' && dataDir.back() != '\\') dataDir += '/';
            return run(config, dataDir);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    usage();
    return 1;
}
