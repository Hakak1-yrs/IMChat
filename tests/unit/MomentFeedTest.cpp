#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include "MessageCenter.h"
#include "MomentFeed.h"
#include "SqliteMessageStore.h"

// 朋友圈内存索引：增量应用新记录、账号注销后重建，两种消息存储上结果相同
namespace {

namespace fs = std::filesystem;

enum class Kind { TEXT, SQLITE };

class MomentFeedTest : public ::testing::TestWithParam<Kind> {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "imchat_unit_tests" /
               ("moment_feed_" + std::string(GetParam() == Kind::TEXT ? "text" : "sqlite"));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        if (GetParam() == Kind::TEXT) store_ = std::make_unique<MessageCenter>((dir_ / "messages.txt").u8string());
        else store_ = std::make_unique<SqliteMessageStore>((dir_ / "messages.db").u8string());
        feed_ = std::make_unique<MomentFeed>(*store_);
    }
    void TearDown() override {
        feed_.reset();
        store_.reset();
        fs::remove_all(dir_);
    }

    int post(const std::string& author, const std::string& text) {
        store_->appendRecord('P', ServiceType::QQ, { author, "all" }, true, text);
        return store_->lastSeq();
    }
    void like(int postSeq, const std::string& who, bool on) {
        store_->appendRecord('L', ServiceType::QQ, { std::to_string(postSeq), who, on ? "1" : "0" }, false);
    }
    int comment(int postSeq, const std::string& who, const std::string& replyTo, const std::string& text) {
        store_->appendRecord('K', ServiceType::QQ, { std::to_string(postSeq), who, replyTo.empty() ? "-" : replyTo }, true, text);
        return store_->lastSeq();
    }

    fs::path dir_;
    std::unique_ptr<MessageStore> store_;
    std::unique_ptr<MomentFeed> feed_;
};

}  // namespace

TEST_P(MomentFeedTest, AppliesNewRecordsIncrementally) {
    const int first = post("10001", "第一条");
    like(first, "10002", true);
    feed_->refresh();
    ASSERT_NE(feed_->post(first), nullptr);
    EXPECT_EQ(feed_->post(first)->likes, (std::vector<std::string>{ "10002" }));

    // 之后追加的记录：下次 refresh 时应用
    like(first, "10003", true);
    like(first, "10002", false);   // 取消
    const int c1 = comment(first, "10002", "", "沙发");
    const int c2 = comment(first, "10003", "10002", "回复乙");
    const int second = post("10001", "第二条");
    EXPECT_EQ(feed_->post(second), nullptr);   // 还没有 refresh
    feed_->refresh();
    const MomentFeed::Post* p = feed_->post(first);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->likes, (std::vector<std::string>{ "10003" }));
    EXPECT_EQ(p->comments, (std::vector<int>{ c1, c2 }));
    ASSERT_NE(feed_->comment(c2), nullptr);
    EXPECT_EQ(feed_->comment(c2)->replyTo, "10002");
    EXPECT_EQ(feed_->comment(c2)->postSeq, first);
    ASSERT_EQ(feed_->postsOf(ServiceType::QQ, "10001").size(), 2u);
    EXPECT_EQ(feed_->postsOf(ServiceType::QQ, "10001")[1]->seq, second);
    EXPECT_TRUE(feed_->postsOf(ServiceType::WECHAT, "10001").empty());

    // 提醒：作者收到两次点赞（含后来取消的那次）和两条评论；被回复的乙收到一条
    const auto& forAuthor = feed_->interactionsOf(ServiceType::QQ, "10001");
    ASSERT_EQ(forAuthor.size(), 4u);
    EXPECT_EQ(forAuthor[0].kind, 'L');
    EXPECT_EQ(forAuthor[3].seq, c2);
    const auto& forReplied = feed_->interactionsOf(ServiceType::QQ, "10002");
    ASSERT_EQ(forReplied.size(), 1u);
    EXPECT_EQ(forReplied[0].actor, "10003");
    EXPECT_EQ(feed_->lastInteraction(ServiceType::QQ), c2);

    // 删除动态、删除评论只做标记
    store_->appendRecord('Z', ServiceType::QQ, { std::to_string(c1) }, false);
    store_->appendRecord('Y', ServiceType::QQ, { std::to_string(second) }, false);
    feed_->refresh();
    EXPECT_TRUE(feed_->comment(c1)->deleted);
    EXPECT_TRUE(feed_->post(second)->deleted);
    EXPECT_FALSE(feed_->post(first)->deleted);
}

TEST_P(MomentFeedTest, RebuildsAfterAnAccountIsCleared) {
    const int seq = post("10001", "动态");
    like(seq, "10002", true);
    const int c = comment(seq, "10002", "", "评论");
    like(seq, "10003", true);
    feed_->refresh();
    ASSERT_EQ(feed_->post(seq)->likes.size(), 2u);

    store_->clearAccount(ServiceType::QQ, "10002");   // 乙注销：他的点赞、评论作废
    feed_->refresh();
    ASSERT_NE(feed_->post(seq), nullptr);
    EXPECT_EQ(feed_->post(seq)->likes, (std::vector<std::string>{ "10003" }));
    EXPECT_TRUE(feed_->post(seq)->comments.empty());
    EXPECT_EQ(feed_->comment(c), nullptr);
    ASSERT_EQ(feed_->interactionsOf(ServiceType::QQ, "10001").size(), 1u);

    store_->clearAccount(ServiceType::QQ, "10001");   // 作者注销：动态也没了
    feed_->refresh();
    EXPECT_EQ(feed_->post(seq), nullptr);
    EXPECT_TRUE(feed_->postsOf(ServiceType::QQ, "10001").empty());
}

INSTANTIATE_TEST_SUITE_P(BothStores, MomentFeedTest, ::testing::Values(Kind::TEXT, Kind::SQLITE),
                         [](const ::testing::TestParamInfo<Kind>& info) { return info.param == Kind::TEXT ? "TextLog" : "Sqlite"; });

// 单机模式下多个窗口共享同一个消息文件：别的窗口写的记录，refresh 后也能看到
TEST(MomentFeedSharedLogTest, SeesRecordsWrittenByAnotherWindow) {
    const fs::path dir = fs::temp_directory_path() / "imchat_unit_tests" / "moment_feed_shared";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string path = (dir / "messages.txt").u8string();
    {
        MessageCenter mine(path), other(path);
        MomentFeed feed(mine);
        feed.refresh();
        other.appendRecord('P', ServiceType::QQ, { "10002", "all" }, true, "另一个窗口发的");
        const int seq = other.lastSeq();
        feed.refresh();
        ASSERT_NE(feed.post(seq), nullptr);
        EXPECT_EQ(feed.post(seq)->text, "另一个窗口发的");
        other.clearAccount(ServiceType::QQ, "10002");   // 另一个窗口里注销：这边也重建
        feed.refresh();
        EXPECT_EQ(feed.post(seq), nullptr);
    }
    fs::remove_all(dir);
}
