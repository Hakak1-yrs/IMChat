#include <gtest/gtest.h>
#include <filesystem>
#include <memory>
#include "FriendRequestBook.h"
#include "MessageCenter.h"
#include "SqliteMessageStore.h"

// 好友申请的内存索引：增量应用申请和处理结果、账号注销后重建，两种消息存储上结果相同
namespace {

namespace fs = std::filesystem;

enum class Kind { TEXT, SQLITE };

class FriendRequestBookTest : public ::testing::TestWithParam<Kind> {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "imchat_unit_tests" /
               ("request_book_" + std::string(GetParam() == Kind::TEXT ? "text" : "sqlite"));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        if (GetParam() == Kind::TEXT) store_ = std::make_unique<MessageCenter>((dir_ / "messages.txt").u8string());
        else store_ = std::make_unique<SqliteMessageStore>((dir_ / "messages.db").u8string());
        book_ = std::make_unique<FriendRequestBook>(*store_);
    }
    void TearDown() override {
        book_.reset();
        store_.reset();
        fs::remove_all(dir_);
    }

    int request(const std::string& from, const std::string& to, const std::string& remark = "-") {
        store_->appendRecord('F', ServiceType::QQ, { to, from, remark }, true, "我是" + from);
        return store_->lastSeq();
    }
    void respond(int seq, bool accept, const std::string& by) {
        store_->appendRecord('V', ServiceType::QQ, { std::to_string(seq), accept ? "A" : "X", by, "-" }, false);
    }

    fs::path dir_;
    std::unique_ptr<MessageStore> store_;
    std::unique_ptr<FriendRequestBook> book_;
};

}  // namespace

TEST_P(FriendRequestBookTest, IndexesRequestsByBothSidesAndTracksStatus) {
    const int a = request("10001", "10002", "老李");
    store_->appendRecord('B', ServiceType::QQ, { "10002", "10009", "1" }, false);   // 别的记录夹在中间
    const int b = request("10003", "10001");
    book_->refresh();
    ASSERT_NE(book_->find(a), nullptr);
    EXPECT_EQ(book_->find(a)->from, "10001");
    EXPECT_EQ(book_->find(a)->to, "10002");
    EXPECT_EQ(book_->find(a)->remark, "老李");
    EXPECT_EQ(book_->find(b)->remark, "");   // "-" 表示没有备注
    EXPECT_EQ(book_->find(a)->greeting, "我是10001");
    EXPECT_EQ(book_->find(a)->status, FriendRequest::PENDING);
    ASSERT_EQ(book_->of(ServiceType::QQ, "10001").size(), 2u);   // 发出的和收到的
    EXPECT_EQ(book_->of(ServiceType::QQ, "10001")[1]->seq, b);
    EXPECT_EQ(book_->of(ServiceType::QQ, "10002").size(), 1u);
    EXPECT_TRUE(book_->of(ServiceType::WECHAT, "10001").empty());

    respond(a, true, "10002");
    respond(b, false, "10001");
    book_->refresh();
    EXPECT_EQ(book_->find(a)->status, FriendRequest::ACCEPTED);
    EXPECT_EQ(book_->find(b)->status, FriendRequest::REJECTED);
}

TEST_P(FriendRequestBookTest, RebuildsAfterAnAccountIsCleared) {
    const int a = request("10001", "10002");
    const int b = request("10003", "10002");
    book_->refresh();
    store_->clearAccount(ServiceType::QQ, "10003");   // 申请人注销：他的申请作废
    book_->refresh();
    EXPECT_NE(book_->find(a), nullptr);
    EXPECT_EQ(book_->find(b), nullptr);
    EXPECT_EQ(book_->of(ServiceType::QQ, "10002").size(), 1u);
    EXPECT_TRUE(book_->of(ServiceType::QQ, "10003").empty());
}

INSTANTIATE_TEST_SUITE_P(BothStores, FriendRequestBookTest, ::testing::Values(Kind::TEXT, Kind::SQLITE),
                         [](const ::testing::TestParamInfo<Kind>& info) { return info.param == Kind::TEXT ? "TextLog" : "Sqlite"; });
