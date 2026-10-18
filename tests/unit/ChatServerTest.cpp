#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <thread>
#include "Log.h"
#include "TestClient.h"
#include "TestServer.h"

using namespace std::chrono_literals;
using nlohmann::json;

namespace {

std::size_t occurrences(const std::string& text, const std::string& part) {
    std::size_t count = 0;
    for (std::size_t pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + part.size())) ++count;
    return count;
}

json sendRequest(const std::string& clientMsgId, const std::string& to, const std::string& text, int reqId = 2) {
    return { { "reqId", reqId }, { "clientMsgId", clientMsgId }, { "to", to }, { "text", text } };
}

json groupRequest(const std::string& clientMsgId, int groupNo, const std::string& text) {
    return { { "reqId", 2 }, { "clientMsgId", clientMsgId }, { "groupNo", groupNo }, { "text", text } };
}

std::string readText(const std::string& utf8Path) { return TestServer::readText(utf8Path); }

}  // namespace

class ChatServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Log::setLevel(Log::Level::WARN);
        dataDir_ = TestServer::freshDataDir(::testing::UnitTest::GetInstance()->current_test_info()->name());
    }

    void TearDown() override { server_.reset(); }

    void startServer(ChatServer::Options options = {}) {
        options.port = 0;
        server_ = std::make_unique<TestServer>(dataDir_, options);
        port_ = server_->port();
    }

    std::size_t onlineCount() { return server_->onlineCount(); }

    std::string dataDir_;
    std::unique_ptr<TestServer> server_;
    std::uint16_t port_ = 0;
};

TEST_F(ChatServerTest, AnswersPingWithoutLogin) {
    startServer();
    TestClient client(port_);
    client.send(MessageType::PING);
    std::optional<Packet> pong = client.receive();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->type, MessageType::PONG);
}

TEST_F(ChatServerTest, LoginChecksPassword) {
    startServer();
    TestClient client(port_);
    json wrong = client.login("QQ", "10001", "wrong");
    EXPECT_EQ(wrong["ok"], false);
    EXPECT_EQ(wrong["reqId"], 1);
    EXPECT_EQ(client.login("QQ", "99999")["ok"], false);
    EXPECT_EQ(client.login("ICQ", "10001")["ok"], false);
    json ok = client.login("QQ", "10001");
    EXPECT_EQ(ok["ok"], true);
    EXPECT_EQ(ok["nickname"], "小张");
    EXPECT_TRUE(ok.contains("unreadPrivate") && ok.contains("unreadGroup"));
    EXPECT_EQ(onlineCount(), 1u);
}

TEST_F(ChatServerTest, SendingRequiresLogin) {
    startServer();
    TestClient client(port_);
    client.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10002", "hi", 5));
    std::optional<Packet> error = client.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->type, MessageType::ERROR_RESP);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::NOT_LOGGED_IN));
    EXPECT_EQ(error->body["reqId"], 5);
}

TEST_F(ChatServerTest, MessageIsAckedAndPushedToOnlineFriend) {
    startServer();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);

    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10002", "晚上一起写服务端 💻"));
    std::optional<Packet> ack = zhang.receive();
    ASSERT_TRUE(ack);
    ASSERT_EQ(ack->type, MessageType::SEND_MSG_ACK);
    EXPECT_EQ(ack->body["ok"], true);
    EXPECT_EQ(ack->body["clientMsgId"], "m1");
    EXPECT_EQ(ack->body["reqId"], 2);
    ASSERT_TRUE(ack->body["seq"].is_number());

    std::optional<Packet> push = li.receive();
    ASSERT_TRUE(push);
    ASSERT_EQ(push->type, MessageType::MSG_PUSH);
    EXPECT_EQ(push->body["from"], "10001");
    EXPECT_EQ(push->body["text"], "晚上一起写服务端 💻");
    EXPECT_EQ(push->body["seq"], ack->body["seq"]);
    EXPECT_NE(readText(dataDir_ + "messages.txt").find("晚上一起写服务端 💻"), std::string::npos);   // 已持久化
}

TEST_F(ChatServerTest, RetriedMessageIsStoredAndPushedOnce) {
    // 客户端没收到回执时会用同一个 clientMsgId 重发：服务端只保存一次，回执相同，对方也只收到一次
    startServer();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("same-id", "10002", "只该出现一次", 2));
    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("same-id", "10002", "只该出现一次", 3));
    std::optional<Packet> first = zhang.receive(), second = zhang.receive();
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->body["seq"], second->body["seq"]);
    EXPECT_EQ(second->body["reqId"], 3);   // 回执内容相同，但 reqId 对应各自的请求
    ASSERT_TRUE(li.receive());
    EXPECT_FALSE(li.receive(300));
    EXPECT_EQ(occurrences(readText(dataDir_ + "messages.txt"), "只该出现一次"), 1u);
}

TEST_F(ChatServerTest, BusinessRuleFailuresComeBackInAck) {
    startServer();
    TestClient zhao(port_);
    ASSERT_EQ(zhao.login("QQ", "10004")["ok"], true);
    zhao.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10005", "你好"));   // 10004 和 10005 不是好友
    std::optional<Packet> ack = zhao.receive();
    ASSERT_TRUE(ack);
    EXPECT_EQ(ack->body["ok"], false);
    EXPECT_FALSE(ack->body["error"].get<std::string>().empty());
}

TEST_F(ChatServerTest, SecondLoginKicksFirstConnection) {
    startServer();
    TestClient first(port_), second(port_), li(port_);
    ASSERT_EQ(first.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(second.login("QQ", "10001")["ok"], true);
    std::optional<Packet> kicked = first.receive();
    ASSERT_TRUE(kicked);
    EXPECT_EQ(kicked->type, MessageType::KICKED);
    EXPECT_TRUE(first.closedByPeer());
    EXPECT_EQ(onlineCount(), 1u);

    // 新消息推送到新的连接
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    li.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10001", "推给新连接"));
    ASSERT_TRUE(li.receive());
    std::optional<Packet> push = second.receive();
    ASSERT_TRUE(push);
    EXPECT_EQ(push->body["text"], "推给新连接");
}

TEST_F(ChatServerTest, IdleConnectionsAreClosedButHeartbeatsKeepThemAlive) {
    ChatServer::Options options;
    options.idleTimeout = 300ms;
    options.idleCheckInterval = 50ms;
    startServer(options);
    TestClient silent(port_), alive(port_);
    for (int i = 0; i < 8; ++i) {   // 每 100ms 一次心跳，共 800ms，远超 300ms 的超时
        alive.send(MessageType::PING);
        ASSERT_TRUE(alive.receive());
        std::this_thread::sleep_for(100ms);
    }
    EXPECT_TRUE(silent.closedByPeer(500));
    alive.send(MessageType::PING);
    std::optional<Packet> pong = alive.receive();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->type, MessageType::PONG);
}

TEST_F(ChatServerTest, InvalidLengthClosesConnection) {
    startServer();
    TestClient client(port_);
    client.sendRaw(std::string("\xFF\xFF\xFF\xFF\x00\x01", 6));   // 声称有 4 GB
    EXPECT_TRUE(client.closedByPeer());
}

TEST_F(ChatServerTest, InvalidJsonGetsErrorAndConnectionStaysUsable) {
    startServer();
    TestClient client(port_);
    client.sendRaw(Frame::encode(MessageType::LOGIN_REQ, "{not json"));
    std::optional<Packet> error = client.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->type, MessageType::ERROR_RESP);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::BAD_REQUEST));
    client.send(MessageType::PING);
    std::optional<Packet> pong = client.receive();
    ASSERT_TRUE(pong);
    EXPECT_EQ(pong->type, MessageType::PONG);
}

TEST_F(ChatServerTest, HandlesStickyAndHalfPacketsOverTheWire) {
    startServer();
    TestClient client(port_);
    Packet ping;
    ping.type = MessageType::PING;
    Packet login;
    login.type = MessageType::LOGIN_REQ;
    login.body = { { "reqId", 9 }, { "service", "QQ" }, { "account", "10001" }, { "password", "123456" } };
    const std::string third = ping.encode();
    // 两帧半一起发出（粘包），剩下的半帧过一会儿再发（半包）
    client.sendRaw(ping.encode() + login.encode() + third.substr(0, 3));
    std::this_thread::sleep_for(50ms);
    client.sendRaw(third.substr(3));
    std::optional<Packet> a = client.receive(), b = client.receive(), c = client.receive();
    ASSERT_TRUE(a && b && c);
    EXPECT_EQ(a->type, MessageType::PONG);
    EXPECT_EQ(b->type, MessageType::LOGIN_RESP);
    EXPECT_EQ(b->body["reqId"], 9);
    EXPECT_EQ(c->type, MessageType::PONG);
}

TEST_F(ChatServerTest, UnknownMessageTypeIsReported) {
    startServer();
    TestClient client(port_);
    client.sendRaw(Frame::encode(static_cast<MessageType>(4242), "{\"reqId\":3}"));
    std::optional<Packet> error = client.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::UNKNOWN_TYPE));
    EXPECT_EQ(error->body["reqId"], 3);
}

TEST_F(ChatServerTest, GroupMessageIsPushedToOnlineMembersOnly) {
    // QQ 群 1001：成员 10001、10002、10003、10004；10005 只是入群申请人
    startServer();
    TestClient zhang(port_), li(port_), wang(port_), sun(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    ASSERT_EQ(wang.login("QQ", "10003")["ok"], true);
    ASSERT_EQ(sun.login("QQ", "10005")["ok"], true);

    zhang.send(MessageType::SEND_MSG_REQ, groupRequest("g1", 1001, "今晚八点群里对一下进度"));
    std::optional<Packet> ack = zhang.receive();
    ASSERT_TRUE(ack);
    ASSERT_EQ(ack->body["ok"], true);
    for (TestClient* member : { &li, &wang }) {
        std::optional<Packet> push = member->receive();
        ASSERT_TRUE(push);
        EXPECT_EQ(push->type, MessageType::MSG_PUSH);
        EXPECT_EQ(push->body["groupNo"], 1001);
        EXPECT_EQ(push->body["from"], "10001");
        EXPECT_EQ(push->body["text"], "今晚八点群里对一下进度");
        EXPECT_EQ(push->body["seq"], ack->body["seq"]);
        EXPECT_FALSE(push->body.contains("to"));
    }
    EXPECT_FALSE(sun.receive(300));     // 不是成员，收不到
    EXPECT_FALSE(zhang.receive(100));   // 发送者自己不会再收到一遍

    sun.send(MessageType::SEND_MSG_REQ, groupRequest("g2", 1001, "我能发吗"));
    std::optional<Packet> rejected = sun.receive();
    ASSERT_TRUE(rejected);
    EXPECT_EQ(rejected->body["ok"], false);
}

TEST_F(ChatServerTest, SendRequiresExactlyOneTarget) {
    startServer();
    TestClient zhang(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, { { "clientMsgId", "x" }, { "to", "10002" }, { "groupNo", 1001 }, { "text", "?" } });
    std::optional<Packet> error = zhang.receive();
    ASSERT_TRUE(error);
    EXPECT_EQ(error->type, MessageType::ERROR_RESP);
    EXPECT_EQ(error->body["code"], static_cast<int>(ErrorCode::BAD_REQUEST));
}

TEST_F(ChatServerTest, SyncReturnsMessagesAfterGivenSeq) {
    startServer();
    std::vector<int> sent;
    {
        TestClient zhang(port_);   // 李四不在线时，张三发了三条
        ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
        for (int i = 1; i <= 3; ++i) {
            zhang.send(MessageType::SEND_MSG_REQ, sendRequest("off-" + std::to_string(i), "10002", "离线消息 " + std::to_string(i)));
            std::optional<Packet> ack = zhang.receive();
            ASSERT_TRUE(ack);
            sent.push_back(ack->body["seq"].get<int>());
        }
    }
    TestClient li(port_);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    li.send(MessageType::SYNC_REQ, { { "reqId", 3 }, { "afterSeq", 0 } });
    std::optional<Packet> all = li.receive();
    ASSERT_TRUE(all);
    ASSERT_EQ(all->type, MessageType::SYNC_RESP);
    EXPECT_EQ(all->body["reqId"], 3);
    EXPECT_EQ(all->body["hasMore"], false);
    const json& messages = all->body["messages"];
    ASSERT_GE(messages.size(), 3u);   // 演示数据里原有的消息 + 刚才的三条
    int previous = 0;
    for (const json& m : messages) {   // 按序号递增
        EXPECT_GT(m["seq"].get<int>(), previous);
        previous = m["seq"].get<int>();
    }
    EXPECT_EQ(messages.back()["seq"], sent.back());
    EXPECT_EQ(messages.back()["text"], "离线消息 3");
    EXPECT_EQ(messages[messages.size() - 3]["seq"], sent.front());

    li.send(MessageType::SYNC_REQ, { { "afterSeq", sent[0] } });   // 只要第一条之后的
    std::optional<Packet> tail = li.receive();
    ASSERT_TRUE(tail);
    ASSERT_EQ(tail->body["messages"].size(), 2u);
    EXPECT_EQ(tail->body["messages"][0]["seq"], sent[1]);

    li.send(MessageType::SYNC_REQ, { { "afterSeq", 0 }, { "limit", 2 } });   // 分批
    std::optional<Packet> page = li.receive();
    ASSERT_TRUE(page);
    EXPECT_EQ(page->body["messages"].size(), 2u);
    EXPECT_EQ(page->body["hasMore"], true);

    li.send(MessageType::SYNC_REQ, { { "afterSeq", sent.back() } });   // 已经是最新的
    std::optional<Packet> none = li.receive();
    ASSERT_TRUE(none);
    EXPECT_TRUE(none->body["messages"].empty());
}

TEST_F(ChatServerTest, SyncIncludesGroupMessagesOfMyGroupsOnly) {
    startServer();
    {
        TestClient li(port_);
        ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
        li.send(MessageType::SEND_MSG_REQ, groupRequest("g1", 1002, "C++ 群的消息"));   // 1002：10002、10001、10005
        ASSERT_TRUE(li.receive());
    }
    TestClient wang(port_);   // 10003 不在 1002 群里
    ASSERT_EQ(wang.login("QQ", "10003")["ok"], true);
    wang.send(MessageType::SYNC_REQ, { { "afterSeq", 0 } });
    std::optional<Packet> sync = wang.receive();
    ASSERT_TRUE(sync);
    for (const json& m : sync->body["messages"]) EXPECT_NE(m.value("text", ""), "C++ 群的消息");
    TestClient sun(port_);    // 10005 在 1002 群里
    ASSERT_EQ(sun.login("QQ", "10005")["ok"], true);
    sun.send(MessageType::SYNC_REQ, { { "afterSeq", 0 } });
    std::optional<Packet> sunSync = sun.receive();
    ASSERT_TRUE(sunSync);
    ASSERT_FALSE(sunSync->body["messages"].empty());
    EXPECT_EQ(sunSync->body["messages"].back()["text"], "C++ 群的消息");
    EXPECT_EQ(sunSync->body["messages"].back()["groupNo"], 1002);
}

TEST_F(ChatServerTest, ReadReceiptReachesSenderAndClearsUnread) {
    startServer();
    TestClient zhang(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("r1", "10002", "看到回我"));
    std::optional<Packet> ack = zhang.receive();
    ASSERT_TRUE(ack);
    const int seq = ack->body["seq"].get<int>();

    auto li = std::make_unique<TestClient>(port_);
    json login = li->login("QQ", "10002");
    ASSERT_EQ(login["ok"], true);
    EXPECT_EQ(login["unreadPrivate"], 1);
    li->send(MessageType::READ_REQ, { { "reqId", 4 }, { "peer", "10001" }, { "seq", seq } });
    std::optional<Packet> readOk = li->receive();
    ASSERT_TRUE(readOk);
    EXPECT_EQ(readOk->type, MessageType::READ_RESP);
    EXPECT_EQ(readOk->body["ok"], true);

    std::optional<Packet> receipt = zhang.receive();
    ASSERT_TRUE(receipt);
    EXPECT_EQ(receipt->type, MessageType::READ_RECEIPT);
    EXPECT_EQ(receipt->body["reader"], "10002");
    EXPECT_EQ(receipt->body["seq"], seq);

    li.reset();   // 重新登录：未读已经清零（已读位置是持久化的）
    TestClient again(port_);
    EXPECT_EQ(again.login("QQ", "10002")["unreadPrivate"], 0);
}

namespace {

// 发一个通用请求，返回 CALL_RESP 的正文。等待期间到达的推送（如 DATA_CHANGED）放进 pushes
std::vector<Packet> pushes;
json call(TestClient& client, const std::string& op, const json& args = json::object()) {
    client.send(MessageType::CALL_REQ, { { "reqId", 77 }, { "op", op }, { "args", args } });
    while (std::optional<Packet> response = client.receive()) {
        if (response->type == MessageType::CALL_RESP) return response->body;
        pushes.push_back(*response);
    }
    return json();
}

// 等一条指定类型的推送（之前在 call 里顺带收到的也算）
std::optional<Packet> waitPush(TestClient& client, MessageType type) {
    for (auto it = pushes.begin(); it != pushes.end(); ++it) {
        if (it->type == type) {
            Packet packet = *it;
            pushes.erase(it);
            return packet;
        }
    }
    while (std::optional<Packet> packet = client.receive()) {
        if (packet->type == type) return packet;
    }
    return std::nullopt;
}

const json* findById(const json& list, const std::string& key, const json& value) {
    for (const json& item : list)
        if (item.value(key, json()) == value) return &item;
    return nullptr;
}

}  // namespace

TEST_F(ChatServerTest, PresenceIsPushedToOnlineFriends) {
    startServer();
    TestClient zhang(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    {
        TestClient li(port_);
        ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
        zhang.receive(300);   // 让上线通知到达
        ASSERT_EQ(zhang.presences.size(), 1u);
        EXPECT_EQ(zhang.presences[0]["account"], "10002");
        EXPECT_EQ(zhang.presences[0]["online"], true);
    }
    zhang.receive(300);
    ASSERT_EQ(zhang.presences.size(), 2u);   // 李四断开：下线通知
    EXPECT_EQ(zhang.presences[1]["online"], false);
}

TEST_F(ChatServerTest, ContactsListHasRemarksOnlineStateAndMutualFriends) {
    startServer();
    TestClient li(port_), zhang(port_);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    json response = call(zhang, "contacts.list");
    ASSERT_EQ(response["ok"], true);
    EXPECT_EQ(response["reqId"], 77);
    const json& friends = response["data"]["friends"];
    ASSERT_EQ(friends.size(), 4u);   // 10002 10003 10004 10005
    const json* li4 = findById(friends, "id", "10002");
    ASSERT_TRUE(li4);
    EXPECT_EQ((*li4)["remark"], "李四");
    EXPECT_EQ((*li4)["nickname"], "李小四");
    EXPECT_EQ((*li4)["online"], true);
    EXPECT_EQ((*li4)["location"], "上海");
    ASSERT_EQ((*li4)["mutual"].size(), 1u);   // 张三和李四的共同好友：王五（10003）
    EXPECT_EQ((*li4)["mutual"][0]["id"], "10003");
    EXPECT_EQ((*findById(friends, "id", "10003"))["online"], false);
}

TEST_F(ChatServerTest, GroupsListComesWithPermissionsComputedByPolicy) {
    startServer();
    TestClient li(port_), wang(port_);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);    // 群 1001 的管理员
    ASSERT_EQ(wang.login("QQ", "10003")["ok"], true);  // 群 1001 的普通成员
    json liGroups = call(li, "groups.list");
    ASSERT_EQ(liGroups["ok"], true);
    const json* g1001 = findById(liGroups["data"]["groups"], "groupNo", 1001);
    ASSERT_TRUE(g1001);
    EXPECT_EQ((*g1001)["name"], "编程交流群");
    EXPECT_EQ((*g1001)["owner"], "10001");
    EXPECT_EQ((*g1001)["mode"], "QQ");
    EXPECT_EQ((*g1001)["members"].size(), 4u);
    EXPECT_EQ((*g1001)["canSpeak"], true);
    EXPECT_EQ((*g1001)["canMentionAll"], true);   // QQ 群模式下管理员也可以 @所有人
    // 管理员能撤回普通成员的消息，不能撤回群主的
    const json& revocable = (*g1001)["revocable"];
    EXPECT_NE(std::find(revocable.begin(), revocable.end(), "10003"), revocable.end());
    EXPECT_EQ(std::find(revocable.begin(), revocable.end(), "10001"), revocable.end());

    const json wangGroups = call(wang, "groups.list");
    const json* wang1001 = findById(wangGroups["data"]["groups"], "groupNo", 1001);
    ASSERT_TRUE(wang1001);
    EXPECT_EQ((*wang1001)["canMentionAll"], false);
    EXPECT_TRUE((*wang1001)["revocable"].empty());
}

TEST_F(ChatServerTest, ReadsGetReturnsBothSidesOfPrivateChats) {
    startServer();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, sendRequest("m1", "10002", "读到这里"));
    const int seq = zhang.receive()->body["seq"].get<int>();
    ASSERT_TRUE(li.receive());
    li.send(MessageType::READ_REQ, { { "peer", "10001" }, { "seq", seq } });
    ASSERT_TRUE(li.receive());   // READ_RESP
    ASSERT_TRUE(zhang.receive());   // READ_RECEIPT
    json reads = call(zhang, "reads.get");
    ASSERT_EQ(reads["ok"], true);
    EXPECT_EQ(reads["data"]["peerRead"]["10002"], seq);     // 李四读到了我的这条
    EXPECT_EQ(reads["data"]["private"]["10002"], 0);        // 我还没读过李四发来的
    EXPECT_TRUE(reads["data"]["groups"].contains("1001"));
}

TEST_F(ChatServerTest, RevokeNotifiesParticipantsAndRespectsPermissions) {
    startServer();
    TestClient zhang(port_), li(port_), wang(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    ASSERT_EQ(wang.login("QQ", "10003")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, groupRequest("g1", 1001, "这句说错了"));
    const int seq = zhang.receive()->body["seq"].get<int>();
    ASSERT_TRUE(li.receive());
    ASSERT_TRUE(wang.receive());

    json denied = call(wang, "message.revoke", { { "seq", seq } });   // 普通成员不能撤回群主的消息
    EXPECT_EQ(denied["ok"], false);
    EXPECT_FALSE(denied["error"].get<std::string>().empty());

    json done = call(zhang, "message.revoke", { { "seq", seq } });
    ASSERT_EQ(done["ok"], true);
    for (TestClient* member : { &li, &wang }) {
        std::optional<Packet> push = member->receive();
        ASSERT_TRUE(push);
        EXPECT_EQ(push->type, MessageType::MSG_REVOKED);
        EXPECT_EQ(push->body["seq"], seq);
        EXPECT_EQ(push->body["revokedBy"], "10001");
    }
    li.send(MessageType::SYNC_REQ, { { "afterSeq", seq - 1 } });   // 同步到的也是撤回状态，不含原文
    const json synced = li.receive()->body["messages"][0];
    EXPECT_EQ(synced["revoked"], true);
    EXPECT_EQ(synced["text"], "");
}

TEST_F(ChatServerTest, GroupMessagesCarryMentions) {
    startServer();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    zhang.send(MessageType::SEND_MSG_REQ, groupRequest("g1", 1001, "@李小四 看一下这个"));
    ASSERT_TRUE(zhang.receive());
    std::optional<Packet> push = li.receive();
    ASSERT_TRUE(push);
    EXPECT_EQ(push->body["mentions"], json::array({ "10002" }));
    zhang.send(MessageType::SEND_MSG_REQ, groupRequest("g2", 1001, "@所有人 开会"));   // 群主发的 @所有人
    ASSERT_TRUE(zhang.receive());
    EXPECT_EQ(li.receive()->body["mentions"].size(), 3u);   // 除群主外的三位成员
}

TEST_F(ChatServerTest, UnknownCallOperationIsRejected) {
    startServer();
    TestClient zhang(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    json response = call(zhang, "no.such.op");
    EXPECT_EQ(response["ok"], false);
}

TEST_F(ChatServerTest, FriendRequestNeedsVerificationThenBothSidesBecomeFriends) {
    // 赵六六（10004）和孙小七（10005）还不是好友；默认开启好友验证
    startServer();
    pushes.clear();
    TestClient zhao(port_), sun(port_);
    ASSERT_EQ(zhao.login("QQ", "10004")["ok"], true);
    ASSERT_EQ(sun.login("QQ", "10005")["ok"], true);
    json sent = call(zhao, "friends.request", { { "target", "10005" }, { "greeting", "我是六六" }, { "remark", "七七" } });
    ASSERT_EQ(sent["ok"], true);
    EXPECT_EQ(sent["data"]["becameFriends"], false);

    std::optional<Packet> changed = waitPush(sun, MessageType::DATA_CHANGED);   // 对方收到通知，去取申请列表
    ASSERT_TRUE(changed);
    json social = call(sun, "social.get");
    ASSERT_EQ(social["ok"], true);
    const json& request = social["data"]["requests"][0];
    EXPECT_EQ(request["from"], "10004");
    EXPECT_EQ(request["greeting"], "我是六六");
    EXPECT_EQ(request["status"], "pending");
    EXPECT_EQ(social["data"]["verify"], true);

    json accepted = call(sun, "friends.respond", { { "seq", request["seq"] }, { "accept", true }, { "remark", "六六" } });
    ASSERT_EQ(accepted["ok"], true);
    ASSERT_TRUE(waitPush(zhao, MessageType::DATA_CHANGED));
    const json zhaoFriends = call(zhao, "contacts.list")["data"]["friends"];
    const json* seven = findById(zhaoFriends, "id", "10005");
    ASSERT_TRUE(seven);
    EXPECT_EQ((*seven)["remark"], "七七");
    EXPECT_EQ(call(sun, "friends.respond", { { "seq", request["seq"] }, { "accept", true } })["ok"], false);   // 不能重复处理

    // 立即保存：服务端重启后仍然是好友
    server_.reset();
    startServer();
    TestClient again(port_);
    ASSERT_EQ(again.login("QQ", "10005")["ok"], true);
    const json sunFriends = call(again, "contacts.list")["data"]["friends"];
    const json* six = findById(sunFriends, "id", "10004");
    ASSERT_TRUE(six);
    EXPECT_EQ((*six)["remark"], "六六");
}

TEST_F(ChatServerTest, WithoutVerificationRequestAddsFriendDirectly) {
    startServer();
    pushes.clear();
    TestClient zhao(port_), sun(port_);
    ASSERT_EQ(zhao.login("QQ", "10004")["ok"], true);
    ASSERT_EQ(sun.login("QQ", "10005")["ok"], true);
    EXPECT_EQ(call(sun, "settings.verify", { { "on", false } })["data"]["verify"], false);
    json sent = call(zhao, "friends.request", { { "target", "10005" } });
    ASSERT_EQ(sent["ok"], true);
    EXPECT_EQ(sent["data"]["becameFriends"], true);
    EXPECT_EQ(call(zhao, "friends.request", { { "target", "10004" } })["ok"], false);   // 不能加自己
    EXPECT_EQ(call(zhao, "friends.request", { { "target", "99999" } })["ok"], false);   // 不存在的账号
}

TEST_F(ChatServerTest, ProfileRemarkOneWayDeleteAndBlacklist) {
    startServer();
    pushes.clear();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);

    EXPECT_EQ(call(zhang, "friends.remark", { { "id", "10002" }, { "remark", "四哥" } })["ok"], true);
    EXPECT_EQ(call(zhang, "friends.remark", { { "id", "10002" }, { "remark", " " } })["ok"], false);   // 备注不能为空
    json profile = call(zhang, "profile.get", { { "id", "10002" } });
    ASSERT_EQ(profile["ok"], true);
    const json& p = profile["data"];
    EXPECT_EQ(p["exists"], true);
    EXPECT_EQ(p["remark"], "四哥");
    EXPECT_EQ(p["isFriend"], true);
    EXPECT_EQ(p["peerHasMe"], true);
    EXPECT_EQ(p["online"], true);
    EXPECT_FALSE(p["info"].empty());   // showInfo() 的输出（多态）
    EXPECT_EQ(p["mutual"].size(), 1u);
    EXPECT_EQ(call(zhang, "profile.get", { { "id", "99999" } })["data"]["exists"], false);

    // 李四单向删除张三：张三收到通知，资料页显示“对方已把你删除”，张三的列表里李四还在
    ASSERT_EQ(call(li, "friends.remove", { { "id", "10001" } })["ok"], true);
    ASSERT_TRUE(waitPush(zhang, MessageType::DATA_CHANGED));
    json after = call(zhang, "profile.get", { { "id", "10002" } })["data"];
    EXPECT_EQ(after["isFriend"], true);
    EXPECT_EQ(after["peerHasMe"], false);

    ASSERT_EQ(call(zhang, "friends.block", { { "id", "10002" }, { "on", true } })["ok"], true);
    const json blocked = call(zhang, "social.get")["data"]["blocked"];
    ASSERT_EQ(blocked.size(), 1u);
    EXPECT_EQ(blocked[0]["id"], "10002");
    EXPECT_EQ(call(zhang, "profile.get", { { "id", "10002" } })["data"]["blocked"], true);
    EXPECT_EQ(call(zhang, "friends.block", { { "id", "10002" }, { "on", true } })["ok"], false);   // 已经在黑名单里
}
// ======================== 群管理 ========================

TEST_F(ChatServerTest, GroupDetailCarriesViewerPermissionsComputedByPolicy) {
    startServer();
    pushes.clear();
    TestClient li(port_), sun(port_);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);    // 群 1001 的管理员
    ASSERT_EQ(sun.login("QQ", "10005")["ok"], true);   // 群 1001 的入群申请人
    json detail = call(li, "group.get", { { "groupNo", 1001 } });
    ASSERT_EQ(detail["ok"], true);
    const json& g = detail["data"];
    EXPECT_EQ(g["exists"], true);
    EXPECT_EQ(g["member"], true);
    EXPECT_EQ(g["isAdmin"], true);
    EXPECT_EQ(g["canEditInfo"], true);
    EXPECT_EQ(g["canApprove"], true);
    EXPECT_EQ(g["canDissolve"], false);   // 不是群主；1001 还是预置群
    EXPECT_FALSE(g["features"].empty());   // 当前策略 showFeatures() 的输出
    ASSERT_EQ(g["pending"].size(), 1u);
    EXPECT_EQ(g["pending"][0]["id"], "10005");
    const json* wang = findById(g["members"], "id", "10003");
    const json* owner = findById(g["members"], "id", "10001");
    ASSERT_TRUE(wang && owner);
    EXPECT_EQ((*wang)["canMute"], true);       // 管理员可以禁言、踢出普通成员
    EXPECT_EQ((*wang)["canKick"], true);
    EXPECT_EQ((*wang)["canSetAdmin"], false);  // 只有群主能设管理员
    EXPECT_EQ((*owner)["canKick"], false);     // 不能动群主
    EXPECT_EQ((*owner)["owner"], true);

    const json outsider = call(sun, "group.get", { { "groupNo", 1001 } })["data"];
    EXPECT_EQ(outsider["member"], false);
    EXPECT_EQ(outsider["applied"], true);
    EXPECT_EQ(call(sun, "group.get", { { "groupNo", 4242 } })["data"]["exists"], false);

    const json all = call(sun, "groups.all")["data"]["groups"];
    EXPECT_EQ(all.size(), 6u);   // QQ 服务的 6 个预置群
    const json* g1001 = findById(all, "groupNo", 1001);
    ASSERT_TRUE(g1001);
    EXPECT_EQ((*g1001)["joined"], false);
    EXPECT_EQ((*g1001)["applied"], true);
    EXPECT_EQ((*g1001)["memberCount"], 4);
}

TEST_F(ChatServerTest, ApprovingJoinPushesSystemTipAndNotifiesEveryone) {
    startServer();
    pushes.clear();
    TestClient li(port_), sun(port_);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    ASSERT_EQ(sun.login("QQ", "10005")["ok"], true);
    json approved = call(li, "group.exec", { { "kind", "approve" }, { "groupNo", 1001 }, { "target", "10005" }, { "on", true } });
    ASSERT_EQ(approved["ok"], true);
    EXPECT_FALSE(approved["data"]["message"].get<std::string>().empty());
    std::optional<Packet> tip = waitPush(li, MessageType::MSG_PUSH);   // 操作者自己也收到系统提示（不是他发的消息）
    ASSERT_TRUE(tip);
    EXPECT_EQ(tip->body["system"], true);
    EXPECT_EQ(tip->body["groupNo"], 1001);

    // 申请人：先收到新成员能看到的系统提示，再收到“群数据变了”的通知
    std::optional<Packet> sunTip = waitPush(sun, MessageType::MSG_PUSH);
    ASSERT_TRUE(sunTip);
    EXPECT_EQ(sunTip->body["seq"], tip->body["seq"]);
    std::optional<Packet> changed = waitPush(sun, MessageType::DATA_CHANGED);
    ASSERT_TRUE(changed);
    EXPECT_EQ(changed->body["topics"], json::array({ "groups" }));
    const json sunGroups = call(sun, "groups.list")["data"]["groups"];
    EXPECT_TRUE(findById(sunGroups, "groupNo", 1001));
    EXPECT_EQ(call(li, "group.exec", { { "kind", "approve" }, { "groupNo", 1001 }, { "target", "10005" } })["ok"], false);   // 已经处理过
}

TEST_F(ChatServerTest, GroupCommandsAreCheckedByPolicyOnTheServer) {
    startServer();
    pushes.clear();
    TestClient wang(port_), zhang(port_);
    ASSERT_EQ(wang.login("QQ", "10003")["ok"], true);   // 普通成员
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);  // 群主
    // 普通成员没有这些权限：请求即使是伪造的，服务端也会拒绝
    EXPECT_EQ(call(wang, "group.exec", { { "kind", "kick" }, { "groupNo", 1001 }, { "target", "10004" } })["ok"], false);
    EXPECT_EQ(call(wang, "group.exec", { { "kind", "rename" }, { "groupNo", 1001 }, { "text", "随便改" } })["ok"], false);
    EXPECT_EQ(call(wang, "group.exec", { { "kind", "mode" }, { "groupNo", 1001 }, { "text", "WECHAT" } })["ok"], false);
    EXPECT_EQ(call(zhang, "group.exec", { { "kind", "invite" }, { "groupNo", 1003 }, { "target", "10005" } })["ok"], false);   // 不是该群成员
    EXPECT_EQ(call(zhang, "group.exec", { { "kind", "invite" }, { "groupNo", 1001 }, { "target", "99999" } })["ok"], false);   // 不是好友
    EXPECT_EQ(call(zhang, "group.exec", { { "kind", "nuke" }, { "groupNo", 1001 } })["ok"], false);                          // 未知命令
    // 参数类型不对：返回失败，连接和服务端都不受影响
    json bad = call(zhang, "group.exec", { { "kind", "admin" }, { "groupNo", 1001 }, { "target", "10003" }, { "on", "yes" } });
    EXPECT_EQ(bad["ok"], false);
    EXPECT_EQ(call(zhang, "groups.list")["ok"], true);

    // 群主踢出王五：王五收到通知，群列表里不再有这个群
    ASSERT_EQ(call(zhang, "group.exec", { { "kind", "kick" }, { "groupNo", 1001 }, { "target", "10003" } })["ok"], true);
    ASSERT_TRUE(waitPush(wang, MessageType::DATA_CHANGED));
    EXPECT_FALSE(findById(call(wang, "groups.list")["data"]["groups"], "groupNo", 1001));
}

TEST_F(ChatServerTest, SwitchingModeKeepsMemberDataAndSurvivesRestart) {
    startServer();
    pushes.clear();
    {
        TestClient zhang(port_);
        ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
        json switched = call(zhang, "group.exec", { { "kind", "mode" }, { "groupNo", 1001 }, { "text", "WECHAT" } });
        ASSERT_EQ(switched["ok"], true);
        EXPECT_NE(switched["data"]["message"].get<std::string>().find("完全一致"), std::string::npos);
        const json g = call(zhang, "group.get", { { "groupNo", 1001 } })["data"];
        EXPECT_EQ(g["mode"], "WECHAT");
        EXPECT_EQ(g["allowAdmin"], false);
        EXPECT_EQ(g["frozenAdmins"], 1);   // 李四的管理员身份保留着，只是当前模式下不生效
        ASSERT_EQ(call(zhang, "group.exec", { { "kind", "notice" }, { "groupNo", 1001 }, { "text", "周五交课设" } })["ok"], true);
        std::optional<Packet> notice = waitPush(zhang, MessageType::MSG_PUSH);   // 公告同时作为一条群消息发出
        while (notice && notice->body.value("system", false)) notice = waitPush(zhang, MessageType::MSG_PUSH);
        ASSERT_TRUE(notice);
        EXPECT_EQ(notice->body["text"], "【群公告】周五交课设");
    }
    server_.reset();   // 改完立即保存：重启后模式、公告都还在
    startServer();
    TestClient again(port_);
    ASSERT_EQ(again.login("QQ", "10002")["ok"], true);
    const json g = call(again, "group.get", { { "groupNo", 1001 } })["data"];
    EXPECT_EQ(g["mode"], "WECHAT");
    EXPECT_EQ(g["notice"], "周五交课设");
    EXPECT_EQ(g["isAdmin"], false);
    EXPECT_EQ(g["members"].size(), 4u);
}

TEST_F(ChatServerTest, CreateAndDissolveGroup) {
    startServer();
    pushes.clear();
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    EXPECT_EQ(call(zhang, "group.create", { { "name", " " } })["ok"], false);
    json created = call(zhang, "group.create", { { "name", "周末爬山" } });
    ASSERT_EQ(created["ok"], true);
    const int groupNo = created["data"]["groupNo"].get<int>();
    EXPECT_GE(groupNo, 1007);
    std::optional<Packet> tip = waitPush(zhang, MessageType::MSG_PUSH);   // “张三 创建了群聊”
    ASSERT_TRUE(tip);
    EXPECT_EQ(tip->body["groupNo"], groupNo);

    ASSERT_EQ(call(zhang, "group.exec", { { "kind", "invite" }, { "groupNo", groupNo }, { "target", "10002" } })["ok"], true);
    ASSERT_TRUE(waitPush(li, MessageType::DATA_CHANGED));
    EXPECT_TRUE(findById(call(li, "groups.list")["data"]["groups"], "groupNo", groupNo));
    EXPECT_EQ(call(li, "group.exec", { { "kind", "dissolve" }, { "groupNo", groupNo } })["ok"], false);   // 只有群主能解散

    ASSERT_EQ(call(zhang, "group.exec", { { "kind", "dissolve" }, { "groupNo", groupNo } })["ok"], true);
    ASSERT_TRUE(waitPush(li, MessageType::DATA_CHANGED));
    EXPECT_EQ(call(li, "group.get", { { "groupNo", groupNo } })["data"]["exists"], false);
    EXPECT_EQ(call(zhang, "group.exec", { { "kind", "dissolve" }, { "groupNo", 1001 } })["ok"], false);   // 预置群不能解散
}
