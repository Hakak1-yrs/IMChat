#include <gtest/gtest.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "Log.h"
#include "TestClient.h"
#include "TestServer.h"

// 模糊测试：向服务端发送大量随机的请求——随机的消息类型、随机的操作名，字段的类型和取值都是随机的
// （该是数字的给字符串、该是字符串的给数组、超长的文本、负数和超大的数……）。
// 服务端对每个请求可以拒绝，但不能崩溃、不能卡住、不能断开这条连接：
// 每发一批就发一个 PING，必须按顺序收到 PONG。
// 随机种子固定，失败可以重现；环境变量 IMCHAT_FUZZ_SEED 可以换一个种子多跑几次
using nlohmann::json;

namespace {

class RandomRequests {
public:
    explicit RandomRequests(unsigned seed) : rng_(seed) {}

    // 一个随机的帧：大多是服务端认识的类型，少数是随机类型
    Packet next() {
        static const MessageType known[] = { MessageType::LOGIN_REQ, MessageType::SEND_MSG_REQ, MessageType::SYNC_REQ,
                                             MessageType::READ_REQ, MessageType::CALL_REQ, MessageType::CALL_REQ,
                                             MessageType::CALL_REQ, MessageType::CALL_REQ };
        Packet packet;
        packet.type = chance(10) ? static_cast<MessageType>(pick(0, 65535)) : known[pick(0, 7)];
        packet.body = object(2);
        if (packet.type == MessageType::CALL_REQ) {
            static const char* ops[] = { "contacts.list", "groups.list", "reads.get", "message.revoke", "social.get", "profile.get",
                                         "friends.request", "friends.respond", "friends.remove", "friends.remark", "friends.block",
                                         "settings.verify", "groups.all", "group.get", "group.create", "group.exec", "server.stats",
                                         "moments.list", "moments.notices", "moments.markRead", "moments.post", "moments.like",
                                         "moments.comment", "moments.delete", "moments.deleteComment" };
            packet.body["op"] = chance(10) ? value(1) : json(ops[pick(0, 24)]);
            packet.body["args"] = chance(10) ? value(2) : object(2);
        }
        return packet;
    }

private:
    int pick(int low, int high) { return std::uniform_int_distribution<int>(low, high)(rng_); }
    bool chance(int percent) { return pick(1, 100) <= percent; }

    std::string text() {
        static const char* samples[] = { "", "10001", "10002", "10003", "1001", "1002", "0", "-1", "99999999999999999999",
                                         "abc", "QQ", "WX", "WB", "WS", "@所有人", "@李四 你好", "dissolve", "join", "kick",
                                         "invite", "admin", "mute", "transfer", "rename", "notice", "mode", "subNew",
                                         "all", "self", "only:10002", "except:", "only:", "-", "\t\n", "中文", "😀" };
        if (chance(5)) return std::string(static_cast<std::size_t>(pick(1000, 70000)), 'x');   // 超长
        if (chance(10)) {                                                                     // 随机字节（可能不是合法的 UTF-8）
            std::string s;
            for (int i = pick(1, 20); i > 0; --i) s += static_cast<char>(pick(1, 255));
            return s;
        }
        return samples[pick(0, 35)];
    }

    json value(int depth) {
        switch (pick(0, depth > 0 ? 8 : 6)) {
        case 0: return nullptr;
        case 1: return chance(50);
        case 2:   // 偏向真实存在的群号（1001～1008）和序号（前面的请求产生的消息、动态），才能走到更深的业务逻辑
            if (chance(30)) return pick(1001, 1008);
            if (chance(40)) return pick(1, 80);
            return pick(-5, 2000);
        case 3: return chance(50) ? static_cast<long long>(9223372036854775807LL) : static_cast<long long>(-2147483649LL);
        case 4: return pick(-1000, 1000) / 7.0;
        case 5:
        case 6: return text();
        case 7: {
            json a = json::array();
            for (int i = pick(0, 4); i > 0; --i) a.push_back(value(depth - 1));
            return a;
        }
        default: return object(depth - 1);
        }
    }

    json object(int depth) {
        static const char* keys[] = { "reqId", "service", "account", "password", "token", "clientMsgId", "to", "groupNo", "text",
                                      "afterSeq", "limit", "peer", "seq", "op", "args", "id", "remark", "on", "target",
                                      "greeting", "accept", "kind", "members", "name", "scope", "replyTo", "author" };
        json o = json::object();
        for (int i = pick(0, 6); i > 0; --i) o[keys[pick(0, 26)]] = value(depth);
        return o;
    }

    std::mt19937 rng_;
};

unsigned fuzzSeed() {
    const std::string seed = TestServer::environment("IMCHAT_FUZZ_SEED");
    return seed.empty() ? 20261018u : static_cast<unsigned>(std::strtoul(seed.c_str(), nullptr, 10));
}

// 发一批随机请求，再发 PING：中间的响应只数一数成功了几个，直到收到 PONG。收不到（超时或连接被关）就失败
void flood(TestClient& client, RandomRequests& requests, int batches, int perBatch, int& succeeded) {
    for (int batch = 0; batch < batches; ++batch) {
        for (int i = 0; i < perBatch; ++i) {
            Packet packet = requests.next();
            client.sendRaw(packet.encode());
        }
        client.send(MessageType::PING);
        bool pong = false;
        while (std::optional<Packet> response = client.receive(5000)) {
            if (response->type == MessageType::PONG) {
                pong = true;
                break;
            }
            if (response->body.value("ok", false)) ++succeeded;
        }
        ASSERT_TRUE(pong) << "第 " << batch << " 批之后没有收到 PONG（服务端崩溃、卡住或断开了连接）";
    }
}

}  // namespace

TEST(ServerFuzzTest, SurvivesRandomRequestsBeforeAndAfterLogin) {
    Log::setLevel(Log::Level::ERROR_);
    const std::string dataDir = TestServer::freshDataDir("ServerFuzz");
    ChatServer::Options options;
    options.port = 0;
    TestServer server(dataDir, options);
    RandomRequests requests(fuzzSeed());

    int succeeded = 0;
    TestClient anonymous(server.port());   // 没有登录：除 PING、登录外都应当被拒绝
    flood(anonymous, requests, 10, 50, succeeded);
    EXPECT_EQ(succeeded, 0);

    TestClient client(server.port());
    ASSERT_EQ(client.login("QQ", "10001")["ok"], true);
    flood(client, requests, 60, 50, succeeded);
    // 随机请求里也要有一部分真的成功了（走到了业务逻辑深处），否则这个测试只测到了参数检查
    EXPECT_GT(succeeded, 100);
    std::printf("  3000 个随机请求中成功 %d 个\n", succeeded);

    // 服务端仍然正常工作：新连接可以登录、发消息
    TestClient other(server.port());
    EXPECT_EQ(other.login("QQ", "10002")["ok"], true);
    other.send(MessageType::SEND_MSG_REQ, { { "reqId", 1 }, { "clientMsgId", "fuzz-after" }, { "to", "10001" }, { "text", "还活着" } });
    std::optional<Packet> ack = other.receive();
    ASSERT_TRUE(ack);
    EXPECT_EQ(ack->type, MessageType::SEND_MSG_ACK);
}
