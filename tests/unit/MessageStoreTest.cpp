#include <gtest/gtest.h>
#include <filesystem>
#include <functional>
#include <memory>
#include "MessageCenter.h"
#include "SqliteMessageStore.h"

// 消息存储的一致性测试：同一组用例分别跑在文本日志（MessageCenter）和 SQLite（SqliteMessageStore）上，
// 两种实现的行为必须完全相同
namespace {

namespace fs = std::filesystem;

enum class Kind { TEXT, SQLITE };

class MessageStoreTest : public ::testing::TestWithParam<Kind> {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "imchat_unit_tests" /
               ("message_store_" + std::string(GetParam() == Kind::TEXT ? "text" : "sqlite"));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        store_ = open();
    }
    void TearDown() override {
        store_.reset();
        fs::remove_all(dir_);
    }

    std::unique_ptr<MessageStore> open() const {
        if (GetParam() == Kind::TEXT) return std::make_unique<MessageCenter>((dir_ / "messages.txt").u8string());
        return std::make_unique<SqliteMessageStore>((dir_ / "messages.db").u8string());
    }

    fs::path dir_;
    std::unique_ptr<MessageStore> store_;
};

Message lastOf(MessageStore& store, const std::string& sender) {
    Message m(0, ServiceType::QQ, false, "", "", "", "");
    store.lastFrom(ServiceType::QQ, sender, m);
    return m;
}

}  // namespace

TEST_P(MessageStoreTest, MessagesAndEntityRecordsShareOneSequence) {
    MessageStore& s = *store_;
    ASSERT_TRUE(s.postPrivate(ServiceType::QQ, "10001", "10002", "第一条"));             // 序号 1
    ASSERT_TRUE(s.appendRecord('F', ServiceType::QQ, { "10003", "10001", "王五" }, true, "我是张三"));   // 好友申请：序号 2
    ASSERT_TRUE(s.appendRecord('B', ServiceType::QQ, { "10001", "10009", "1" }, false));   // 拉黑：不占序号
    ASSERT_TRUE(s.postGroup(ServiceType::QQ, 1001, "10002", "群里一句"));                  // 序号 3
    EXPECT_EQ(lastOf(s, "10001").seq(), 1);
    EXPECT_EQ(lastOf(s, "10002").seq(), 3);
    const std::vector<Record> requests = s.records("F");
    ASSERT_EQ(requests.size(), 1u);
    EXPECT_EQ(requests[0].seq, 2);
    EXPECT_EQ(requests[0].fields, (std::vector<std::string>{ "10003", "10001", "王五" }));
    EXPECT_EQ(requests[0].text, "我是张三");
    EXPECT_FALSE(requests[0].time.empty());
    const std::vector<Record> both = s.records("FB");   // 按先后顺序
    ASSERT_EQ(both.size(), 2u);
    EXPECT_EQ(both[0].kind, 'F');
    EXPECT_EQ(both[1].kind, 'B');
    EXPECT_LT(both[0].line, both[1].line);
    EXPECT_TRUE(both[1].time.empty());
}

TEST_P(MessageStoreTest, RecordsAfterReturnsOnlyNewerRecords) {
    MessageStore& s = *store_;
    s.appendRecord('F', ServiceType::QQ, { "10002", "10001", "-" }, true, "一");
    s.appendRecord('B', ServiceType::QQ, { "10001", "10009", "1" }, false);
    s.postPrivate(ServiceType::QQ, "10001", "10002", "消息不是记录");
    s.appendRecord('P', ServiceType::QQ, { "10001", "all" }, true, "动态：不在要的类型里");
    s.appendRecord('D', ServiceType::QQ, { "10001", "10002" }, false);
    const std::vector<Record> all = s.recordsAfter("FBD", 0);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(s.records("FBD").size(), 3u);
    const std::vector<Record> newer = s.recordsAfter("FBD", all[0].line);   // 从第一条之后
    ASSERT_EQ(newer.size(), 2u);
    EXPECT_EQ(newer[0].kind, 'B');
    EXPECT_EQ(newer[1].kind, 'D');
    EXPECT_TRUE(s.recordsAfter("FBD", all[2].line).empty());
    s.appendRecord('V', ServiceType::QQ, { std::to_string(all[0].seq), "A", "-", "-" }, false);
    const std::vector<Record> latest = s.recordsAfter("FVBD", all[2].line);   // 之后追加的
    ASSERT_EQ(latest.size(), 1u);
    EXPECT_EQ(latest[0].kind, 'V');
}

TEST_P(MessageStoreTest, QueriesFilterAndOrderMessages) {
    MessageStore& s = *store_;
    s.postPrivate(ServiceType::QQ, "10001", "10002", "a");
    s.postPrivate(ServiceType::QQ, "10002", "10001", "b");
    s.postPrivate(ServiceType::QQ, "10001", "10003", "c");
    s.postGroup(ServiceType::QQ, 1001, "10003", "d");
    s.postGroup(ServiceType::QQ, 1002, "10003", "e");
    s.postPrivate(ServiceType::WECHAT, "10001", "10002", "别的服务");
    const auto chat = s.conversation(ServiceType::QQ, "10002", "10001");
    ASSERT_EQ(chat.size(), 2u);
    EXPECT_EQ(chat[0].text(), "a");
    EXPECT_EQ(chat[1].text(), "b");
    EXPECT_FALSE(chat[0].isGroup());
    const auto group = s.groupHistory(ServiceType::QQ, 1001);
    ASSERT_EQ(group.size(), 1u);
    EXPECT_TRUE(group[0].isGroup());
    EXPECT_EQ(group[0].target(), "1001");
    const auto mine = s.messagesFor(ServiceType::QQ, "10001", { 1001 });   // 我的私信 + 1001 群（不含 1002 群）
    ASSERT_EQ(mine.size(), 4u);
    EXPECT_EQ(mine[3].text(), "d");
    Message found(0, ServiceType::QQ, false, "", "", "", "");
    ASSERT_TRUE(s.findMessage(ServiceType::QQ, 3, found));
    EXPECT_EQ(found.text(), "c");
    EXPECT_FALSE(s.findMessage(ServiceType::WECHAT, 3, found));   // 序号对，服务不对
    EXPECT_EQ(s.messagesFor(ServiceType::QQ, "10002", {}).size(), 2u);
}

TEST_P(MessageStoreTest, MessagesAfterReturnsOnlyTheNextPage) {
    MessageStore& s = *store_;
    s.postPrivate(ServiceType::QQ, "10001", "10002", "1");   // 1
    s.postGroup(ServiceType::QQ, 1002, "10003", "别的群");    // 2：不是我的群
    s.postGroup(ServiceType::QQ, 1001, "10003", "3");        // 3
    s.postPrivate(ServiceType::WECHAT, "10001", "10002", "别的服务");   // 4
    s.postPrivate(ServiceType::QQ, "10003", "10001", "5");   // 5
    s.postPrivate(ServiceType::QQ, "10002", "10003", "别人的私信");      // 6
    s.postGroup(ServiceType::QQ, 1001, "10001", "7");        // 7
    auto texts = [](const std::vector<Message>& list) {
        std::string joined;
        for (const Message& m : list) joined += m.text();
        return joined;
    };
    EXPECT_EQ(texts(s.messagesAfter(ServiceType::QQ, "10001", { 1001 }, 0, 0)), "1357");
    EXPECT_EQ(texts(s.messagesAfter(ServiceType::QQ, "10001", { 1001 }, 0, 2)), "13");
    EXPECT_EQ(texts(s.messagesAfter(ServiceType::QQ, "10001", { 1001 }, 3, 2)), "57");
    EXPECT_EQ(texts(s.messagesAfter(ServiceType::QQ, "10001", { 1001 }, 4, 1)), "5");
    EXPECT_EQ(texts(s.messagesAfter(ServiceType::QQ, "10001", { 1001 }, 7, 0)), "");
    EXPECT_EQ(texts(s.messagesAfter(ServiceType::QQ, "10001", {}, 1, 0)), "5");   // 没有群：只有私信
    EXPECT_EQ(s.lastSeq(), 7);
    s.clearGroup(ServiceType::QQ, 1001);
    EXPECT_EQ(s.lastSeq(), 7);   // 清除不会让序号回退
    s.postPrivate(ServiceType::QQ, "10001", "10002", "8");
    EXPECT_EQ(s.lastSeq(), 8);
}

TEST_P(MessageStoreTest, RevokeUnreadAndReadMarks) {
    MessageStore& s = *store_;
    s.postPrivate(ServiceType::QQ, "10002", "10001", "一");       // 1
    s.postPrivate(ServiceType::QQ, "10002", "10001", "二");       // 2
    s.postPrivate(ServiceType::QQ, "10001", "10002", "自己发的");  // 3：不算未读
    s.postGroup(ServiceType::QQ, 1001, "#", "系统提示");           // 4：不算未读
    s.postGroup(ServiceType::QQ, 1001, "10003", "群里");           // 5
    s.postGroup(ServiceType::QQ, 1001, "10003", "撤回的");         // 6：撤回后不算
    ASSERT_TRUE(s.revoke(ServiceType::QQ, 6, "10003"));
    EXPECT_EQ(s.unreadPrivate(ServiceType::QQ, "10001"), (std::map<std::string, int>{ { "10002", 2 } }));
    EXPECT_EQ(s.unreadGroups(ServiceType::QQ, "10001", { 1001 }), (std::map<int, int>{ { 1001, 1 } }));
    s.markPrivateRead(ServiceType::QQ, "10001", "10002", 1);
    s.markPrivateRead(ServiceType::QQ, "10001", "10002", 0);   // 已读位置不会后退
    EXPECT_EQ(s.unreadPrivate(ServiceType::QQ, "10001"), (std::map<std::string, int>{ { "10002", 1 } }));
    s.markGroupRead(ServiceType::QQ, 1001, "10001", 6);
    EXPECT_TRUE(s.unreadGroups(ServiceType::QQ, "10001", { 1001 }).empty());
    EXPECT_EQ(s.groupReadMark(ServiceType::QQ, 1001, "10001"), 6);
    EXPECT_EQ(s.readMark(ServiceType::QQ, "P", "10002", "10001"), 1);
    const auto group = s.groupHistory(ServiceType::QQ, 1001);
    ASSERT_EQ(group.size(), 3u);
    EXPECT_TRUE(group[0].isSystem());
    EXPECT_TRUE(group[2].revoked());
    EXPECT_EQ(group[2].revokedBy(), "10003");
}

TEST_P(MessageStoreTest, ClearingNeverReusesSequenceNumbersAndSurvivesReopen) {
    store_->postPrivate(ServiceType::QQ, "10001", "10002", "保留");          // 1
    store_->postPrivate(ServiceType::QQ, "10003", "10001", "注销者发的");    // 2
    store_->postGroup(ServiceType::QQ, 1007, "10001", "要解散的群");          // 3
    store_->appendRecord('F', ServiceType::QQ, { "10001", "10003", "-" }, true, "加个好友");   // 4
    store_->appendRecord('S', ServiceType::QQ, { "10001", "verify", "0" }, false);
    store_->clearAccount(ServiceType::QQ, "10003");   // 注销 10003：与之有关的私信和记录作废
    store_->clearGroup(ServiceType::QQ, 1007);        // 解散 1007：群消息作废
    store_->postGroup(ServiceType::QQ, 1007, "10009", "群号被新群复用");      // 5：序号不会回到 2、3、4
    store_.reset();
    store_ = open();   // 重新打开：数据和序号都还在
    MessageStore& s = *store_;
    EXPECT_EQ(s.conversation(ServiceType::QQ, "10001", "10002").size(), 1u);
    EXPECT_TRUE(s.conversation(ServiceType::QQ, "10001", "10003").empty());
    const auto group = s.groupHistory(ServiceType::QQ, 1007);
    ASSERT_EQ(group.size(), 1u);
    EXPECT_EQ(group[0].seq(), 5);
    EXPECT_TRUE(s.records("F").empty());
    EXPECT_EQ(s.records("S").size(), 1u);
    s.postPrivate(ServiceType::QQ, "10001", "10002", "重启后");
    EXPECT_EQ(lastOf(s, "10001").seq(), 6);
}

INSTANTIATE_TEST_SUITE_P(Stores, MessageStoreTest, ::testing::Values(Kind::TEXT, Kind::SQLITE),
                         [](const ::testing::TestParamInfo<Kind>& info) { return info.param == Kind::TEXT ? "TextLog" : "Sqlite"; });

// 旧的文本日志第一次用 SQLite 打开时整体导入：序号、撤回、已读位置、记录的先后都保持不变，之后接着分配序号
TEST(SqliteMigrationTest, ImportsExistingTextLogOnce) {
    const fs::path dir = fs::temp_directory_path() / "imchat_unit_tests" / "sqlite_migration";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string log = (dir / "messages.txt").u8string(), db = (dir / "messages.db").u8string();
    {
        MessageCenter text(log);
        text.postPrivate(ServiceType::QQ, "10001", "10002", "旧消息 1");
        text.appendRecord('P', ServiceType::QQ, { "10001", "all" }, true, "一条动态");
        text.postPrivate(ServiceType::QQ, "10002", "10001", "旧消息 2");
        text.revoke(ServiceType::QQ, 3, "10002");
        text.markPrivateRead(ServiceType::QQ, "10002", "10001", 1);
        text.postPrivate(ServiceType::QQ, "10004", "10001", "注销者的");
        text.clearAccount(ServiceType::QQ, "10004");
    }
    {
        SqliteMessageStore sqlite(db, log);
        ASSERT_TRUE(sqlite.isOpen());
        EXPECT_EQ(sqlite.imported(), 2);   // 注销者的那条已经作废，不导入
        const auto chat = sqlite.conversation(ServiceType::QQ, "10001", "10002");
        ASSERT_EQ(chat.size(), 2u);
        EXPECT_EQ(chat[0].seq(), 1);
        EXPECT_EQ(chat[1].seq(), 3);
        EXPECT_TRUE(chat[1].revoked());
        EXPECT_EQ(sqlite.readMark(ServiceType::QQ, "P", "10001", "10002"), 1);
        ASSERT_EQ(sqlite.records("P").size(), 1u);
        EXPECT_EQ(sqlite.records("P")[0].seq, 2);
        sqlite.postPrivate(ServiceType::QQ, "10001", "10002", "新消息");
        EXPECT_EQ(sqlite.conversation(ServiceType::QQ, "10001", "10002").back().seq(), 5);   // 接在旧日志的序号后面
    }
    {
        SqliteMessageStore again(db, log);   // 已经有数据了：不会重复导入
        EXPECT_EQ(again.imported(), 0);
        EXPECT_EQ(again.conversation(ServiceType::QQ, "10001", "10002").size(), 3u);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
