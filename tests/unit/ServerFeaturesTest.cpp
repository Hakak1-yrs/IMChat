#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include "Log.h"
#include "TestClient.h"
#include "TestServer.h"

// 个人中心、平台概况、跨服务好友、点对点聊天的信令转发、实时群聊室（单机版有、联网后由服务端提供的功能）
using nlohmann::json;

namespace {

int nextReqId = 100;

// 发一个通用请求，等它的响应（途中插进来的推送放进 pushes）
json call(TestClient& client, const std::string& op, const json& args = json::object(), std::vector<Packet>* pushes = nullptr) {
    const int reqId = ++nextReqId;
    client.send(MessageType::CALL_REQ, { { "reqId", reqId }, { "op", op }, { "args", args } });
    while (std::optional<Packet> packet = client.receive()) {
        if ((packet->type == MessageType::CALL_RESP || packet->type == MessageType::ERROR_RESP) && packet->body.value("reqId", 0) == reqId)
            return packet->body;
        if (pushes) pushes->push_back(*packet);
    }
    return json();
}

// 下一条指定类型的推送（跳过其他的）
std::optional<Packet> next(TestClient& client, MessageType type, int timeoutMs = 2000) {
    while (std::optional<Packet> packet = client.receive(timeoutMs)) {
        if (packet->type == type) return packet;
    }
    return std::nullopt;
}

json profile(const std::string& nickname, const std::string& password = "secret1") {
    return { { "nickname", nickname }, { "birthday", "2004-05-20" }, { "location", "南京" }, { "password", password } };
}

}  // namespace

class ServerFeaturesTest : public ::testing::Test {
protected:
    void SetUp() override {
        Log::setLevel(Log::Level::WARN);
        dataDir_ = TestServer::freshDataDir(std::string("features_") + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        startServer();
    }

    void startServer() {
        ChatServer::Options options;
        options.port = 0;
        server_ = std::make_unique<TestServer>(dataDir_, options);
        port_ = server_->port();
    }

    std::string dataDir_;
    std::unique_ptr<TestServer> server_;
    std::uint16_t port_ = 0;
};

TEST_F(ServerFeaturesTest, RegisterBeforeLoginThenLogIn) {
    TestClient client(port_);
    // 其他操作要先登录，注册不用
    EXPECT_EQ(call(client, "account.get")["code"], static_cast<int>(ErrorCode::NOT_LOGGED_IN));
    json args = profile("新同学");
    args["name"] = "钱九";
    args["service"] = "WEIBO";   // 微博与 QQ 共享号码，不能作为第一个服务
    EXPECT_EQ(call(client, "account.register", args)["ok"], false);
    args["service"] = "QQ";
    args["birthday"] = "2999-01-01";
    EXPECT_EQ(call(client, "account.register", args)["ok"], false);
    args["birthday"] = "2004-05-20";
    const json registered = call(client, "account.register", args);
    ASSERT_EQ(registered["ok"], true) << registered.dump();
    const std::string account = registered["data"]["account"];
    EXPECT_FALSE(account.empty());

    EXPECT_EQ(client.login("QQ", account, "secret1")["ok"], true);
    const json me = call(client, "account.get");
    ASSERT_EQ(me["ok"], true);
    EXPECT_EQ(me["data"]["name"], "钱九");
    EXPECT_EQ(me["data"]["nickname"], "新同学");
    EXPECT_EQ(me["data"]["location"], "南京");
    EXPECT_EQ(me["data"]["services"].size(), 1u);

    // 重启后仍在
    server_.reset();
    startServer();
    TestClient again(port_);
    EXPECT_EQ(again.login("QQ", account, "secret1")["ok"], true);
}

TEST_F(ServerFeaturesTest, EditProfileAndPassword) {
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    EXPECT_EQ(call(zhang, "account.update", { { "nickname", "  " } })["ok"], false);
    EXPECT_EQ(call(zhang, "account.update", { { "birthday", "2004-13-40" } })["ok"], false);
    EXPECT_EQ(call(zhang, "account.update", { { "nickname", "大张" }, { "location", "苏州" } })["ok"], true);
    // 好友收到通知，重新获取后看到新昵称
    ASSERT_TRUE(next(li, MessageType::DATA_CHANGED));
    const json contacts = call(li, "contacts.list");
    bool found = false;
    for (const json& f : contacts["data"]["friends"]) found = found || (f["id"] == "10001" && f["nickname"] == "大张" && f["location"] == "苏州");
    EXPECT_TRUE(found);

    EXPECT_EQ(call(zhang, "account.password", { { "old", "wrong" }, { "new", "abcdef" } })["ok"], false);
    EXPECT_EQ(call(zhang, "account.password", { { "old", "123456" }, { "new", "abc" } })["ok"], false);
    EXPECT_EQ(call(zhang, "account.password", { { "old", "123456" }, { "new", "abcdef" } })["ok"], true);
    TestClient other(port_);
    EXPECT_EQ(other.login("QQ", "10001", "123456")["ok"], false);
    EXPECT_EQ(other.login("QQ", "10001", "abcdef")["ok"], true);
}

TEST_F(ServerFeaturesTest, OpenServiceBindQQAndCloseIt) {
    TestClient zhou(port_);   // 周八：只有微信和微商，没有 QQ
    ASSERT_EQ(zhou.login("WECHAT", "wx2006")["ok"], true);
    const json before = call(zhou, "account.get");
    EXPECT_EQ(before["data"]["independent"], true);
    EXPECT_EQ(before["data"]["hasQQ"], false);
    EXPECT_EQ(call(zhou, "account.bindQQ", { { "on", true } })["ok"], false);   // 没有 QQ 可以绑定

    json args = profile("周八的QQ", "qqpass1");
    args["service"] = "QQ";
    const json opened = call(zhou, "account.openService", args);
    ASSERT_EQ(opened["ok"], true) << opened.dump();
    const std::string qq = opened["data"]["account"];
    EXPECT_EQ(call(zhou, "account.openService", args)["ok"], false);   // 已经开通过

    // 微信绑定刚开通的 QQ，再解绑
    const json bound = call(zhou, "account.bindQQ", { { "on", true } });
    ASSERT_EQ(bound["ok"], true) << bound.dump();
    EXPECT_EQ(bound["data"]["boundQQ"], qq);
    EXPECT_EQ(call(zhou, "account.bindQQ", { { "on", false } })["data"]["boundQQ"], "");

    // 凭登录令牌直接登录新开通的服务（一处登录、多个服务）：令牌从本人另一个服务的登录响应里拿
    TestClient qqClient(port_);
    TestClient tokenSource(port_);
    const json first = tokenSource.login("WEISHANG", "ws3001");
    ASSERT_EQ(first["ok"], true);
    qqClient.send(MessageType::LOGIN_REQ, { { "reqId", 1 }, { "service", "QQ" }, { "account", qq }, { "token", first["token"] } });
    std::optional<Packet> viaToken = qqClient.receive();
    ASSERT_TRUE(viaToken);
    EXPECT_EQ(viaToken->body["ok"], true);

    // 关闭 QQ：密码不对不行；关闭后这条连接回到未登录状态
    EXPECT_EQ(call(qqClient, "account.closeService", { { "password", "wrong" } })["ok"], false);
    const json closed = call(qqClient, "account.closeService", { { "password", "qqpass1" } });
    ASSERT_EQ(closed["ok"], true) << closed.dump();
    EXPECT_EQ(closed["data"]["services"].size(), 2u);
    EXPECT_EQ(call(qqClient, "account.get")["code"], static_cast<int>(ErrorCode::NOT_LOGGED_IN));
    TestClient gone(port_);
    EXPECT_EQ(gone.login("QQ", qq, "qqpass1")["ok"], false);
}

TEST_F(ServerFeaturesTest, OnlyServiceCannotBeClosedButAccountCanBeDeregistered) {
    TestClient zhao(port_), zhaoWeibo(port_), zhang(port_);
    // 赵六：QQ 10004 和微博 10004；张三是他的 QQ 好友
    ASSERT_EQ(zhao.login("QQ", "10004")["ok"], true);
    const json weibo = zhaoWeibo.login("WEIBO", "10004");
    ASSERT_EQ(weibo["ok"], true);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);

    EXPECT_EQ(call(zhao, "account.deregister", { { "password", "nope" } })["ok"], false);
    const json result = call(zhao, "account.deregister", { { "password", "123456" } });
    ASSERT_EQ(result["ok"], true) << result.dump();
    // 他在别处登录的微博被下线
    std::optional<Packet> kicked = next(zhaoWeibo, MessageType::KICKED);
    ASSERT_TRUE(kicked);
    EXPECT_EQ(kicked->body["reason"], "账号已注销");
    // 张三的好友列表里没有他了
    ASSERT_TRUE(next(zhang, MessageType::DATA_CHANGED));
    for (const json& f : call(zhang, "contacts.list")["data"]["friends"]) EXPECT_NE(f["id"], "10004");
    TestClient again(port_);
    EXPECT_EQ(again.login("QQ", "10004")["ok"], false);

    // 只开通了一个服务的用户不能关闭它（否则再也登录不了），只能注销
    TestClient sun(port_);
    json args = profile("单服务");
    args["name"] = "吴十";
    args["service"] = "QQ";
    const std::string account = call(sun, "account.register", args)["data"]["account"];
    ASSERT_EQ(sun.login("QQ", account, "secret1")["ok"], true);
    EXPECT_EQ(call(sun, "account.closeService", { { "password", "secret1" } })["ok"], false);
}

TEST_F(ServerFeaturesTest, OverviewAndCrossServiceFriends) {
    TestClient zhang(port_);
    ASSERT_EQ(zhang.login("WECHAT", "wx2001")["ok"], true);   // 张三的微信，绑定了 QQ 10001
    const json overview = call(zhang, "platform.overview");
    ASSERT_EQ(overview["ok"], true);
    EXPECT_EQ(overview["data"]["users"].size(), 6u);
    EXPECT_GT(overview["data"]["groups"]["QQ"].get<int>(), 0);

    const json sources = call(zhang, "friends.cross");
    ASSERT_EQ(sources["ok"], true);
    EXPECT_EQ(sources["data"]["sources"], json({ "QQ", "WEIBO" }));
    const json cross = call(zhang, "friends.cross", { { "source", "QQ" } });
    ASSERT_EQ(cross["ok"], true) << cross.dump();
    // 李四（QQ 10002、微信 wx2002 绑定了 10002）在两边都是张三的好友：共同好友
    bool liCommon = false;
    for (const json& c : cross["data"]["common"]) liCommon = liCommon || (c["id"] == "wx2002" && c["sourceId"] == "10002");
    EXPECT_TRUE(liCommon) << cross.dump();
    // 王五（QQ 10003 是张三的 QQ 好友，微信 wx2003 绑定了 10003，但还不是张三的微信好友）：推荐添加
    bool wangRecommended = false;
    for (const json& r : cross["data"]["recommend"]) wangRecommended = wangRecommended || r["id"] == "wx2003";
    EXPECT_TRUE(wangRecommended) << cross.dump();
}

TEST_F(ServerFeaturesTest, P2PSignalsAreRelayedOnlyBetweenOnlineFriends) {
    TestClient zhang(port_), li(port_), zhao(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    ASSERT_EQ(zhao.login("QQ", "10004")["ok"], true);
    const json invite = { { "kind", "invite" }, { "session", "s1" }, { "host", { { "port", 50123 }, { "ips", { "192.168.1.5" } } } } };
    EXPECT_EQ(call(zhang, "p2p.signal", { { "to", "10002" }, { "data", invite } })["ok"], true);
    std::optional<Packet> signal = next(li, MessageType::P2P_SIGNAL);
    ASSERT_TRUE(signal);
    EXPECT_EQ(signal->body["from"], "10001");
    EXPECT_EQ(signal->body["nickname"], "小张");
    EXPECT_EQ(signal->body["data"], invite);   // 原样转发

    EXPECT_EQ(call(li, "p2p.signal", { { "to", "10004" }, { "data", invite } })["ok"], false);   // 不是好友
    EXPECT_EQ(call(zhang, "p2p.signal", { { "to", "10005" }, { "data", invite } })["error"], "对方不在线");
    EXPECT_EQ(call(zhang, "p2p.signal", { { "to", "10002" }, { "data", "text" } })["ok"], false);   // data 必须是对象
    EXPECT_EQ(call(zhang, "p2p.signal", { { "to", "10002" }, { "data", { { "x", std::string(20000, 'a') } } } })["ok"], false);
}

TEST_F(ServerFeaturesTest, ChatRoomJoinSayLeaveAndDisconnect) {
    // 1001 编程交流群的成员有 10001、10002……
    TestClient zhang(port_), li(port_);
    ASSERT_EQ(zhang.login("QQ", "10001")["ok"], true);
    ASSERT_EQ(li.login("QQ", "10002")["ok"], true);
    const json joined = call(zhang, "room.join", { { "groupNo", 1001 } });
    ASSERT_EQ(joined["ok"], true) << joined.dump();
    EXPECT_EQ(joined["data"]["members"].size(), 1u);
    EXPECT_EQ(call(zhang, "room.say", { { "groupNo", 1002 }, { "text", "hi" } })["ok"], false);   // 不在那个群聊室

    ASSERT_EQ(call(li, "room.join", { { "groupNo", 1001 } })["data"]["members"].size(), 2u);
    std::optional<Packet> join = next(zhang, MessageType::ROOM_EVENT);
    ASSERT_TRUE(join);
    EXPECT_EQ(join->body["event"], "join");
    EXPECT_EQ(join->body["from"], "10002");

    EXPECT_EQ(call(li, "room.say", { { "groupNo", 1001 }, { "text", "大家好" } })["ok"], true);
    std::optional<Packet> say = next(zhang, MessageType::ROOM_EVENT);
    ASSERT_TRUE(say);
    EXPECT_EQ(say->body["event"], "say");
    EXPECT_EQ(say->body["text"], "大家好");
    EXPECT_EQ(say->body["nickname"], "李小四");

    // 群聊室的内容不保存：不会出现在同步的消息里
    const int reqId = 77;
    zhang.send(MessageType::SYNC_REQ, { { "reqId", reqId }, { "afterSeq", 0 } });
    std::optional<Packet> sync = next(zhang, MessageType::SYNC_RESP);
    ASSERT_TRUE(sync);
    for (const json& m : sync->body["messages"]) EXPECT_NE(m["text"], "大家好");

    // 掉线也算离开
    {
        TestClient wang(port_);
        ASSERT_EQ(wang.login("QQ", "10003")["ok"], true);
        ASSERT_EQ(call(wang, "room.join", { { "groupNo", 1001 } })["ok"], true);
        ASSERT_TRUE(next(zhang, MessageType::ROOM_EVENT));   // 王五进来
    }
    std::optional<Packet> left = next(zhang, MessageType::ROOM_EVENT);
    ASSERT_TRUE(left);
    EXPECT_EQ(left->body["event"], "leave");
    EXPECT_EQ(left->body["from"], "10003");

    EXPECT_EQ(call(li, "room.leave", { { "groupNo", 1001 } })["ok"], true);
    std::optional<Packet> liLeft = next(zhang, MessageType::ROOM_EVENT);
    ASSERT_TRUE(liLeft);
    EXPECT_EQ(liLeft->body["from"], "10002");

    TestClient outsider(port_);   // 孙七只是申请了加入 1001，还不是成员
    ASSERT_EQ(outsider.login("QQ", "10005")["ok"], true);
    EXPECT_EQ(call(outsider, "room.join", { { "groupNo", 1001 } })["ok"], false);
}
