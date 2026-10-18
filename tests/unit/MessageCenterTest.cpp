#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include "MessageCenter.h"

// MessageCenter 的增量读取：解析结果缓存在内存里，每次查询只读取新追加的部分。
// 这些用例检查它与“每次都完整解析一遍”的结果一致，并且多个实例（相当于多个进程）共享一个文件时互相看得见
namespace {

namespace fs = std::filesystem;

class MessageCenterTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir_ = fs::temp_directory_path() / "imchat_unit_tests" / "message_center";
        fs::remove_all(dir_);
        fs::create_directories(dir_);
        path_ = (dir_ / "messages.txt").u8string();
    }
    void TearDown() override { fs::remove_all(dir_); }

    // 绕过 MessageCenter，直接往文件末尾写原始字节（模拟另一个程序正在写）
    void appendRaw(const std::string& bytes) {
        std::ofstream out(fs::u8path(path_), std::ios::binary | std::ios::app);
        out << bytes;
    }

    fs::path dir_;
    std::string path_;
};

}  // namespace

TEST_F(MessageCenterTest, SecondInstanceSeesAppendsOfTheFirst) {
    MessageCenter a(path_), b(path_);
    ASSERT_TRUE(a.postPrivate(ServiceType::QQ, "10001", "10002", "你好"));
    ASSERT_EQ(b.conversation(ServiceType::QQ, "10001", "10002").size(), 1u);   // b 第一次读：完整解析
    ASSERT_TRUE(a.postPrivate(ServiceType::QQ, "10002", "10001", "在呢"));
    const auto messages = b.conversation(ServiceType::QQ, "10001", "10002");   // b 第二次读：只读新增的一行
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[1].text(), "在呢");
    EXPECT_EQ(messages[1].seq(), messages[0].seq() + 1);
    // b 写的，a 也能看到
    ASSERT_TRUE(b.postGroup(ServiceType::QQ, 1001, "10002", "群里说一句"));
    EXPECT_EQ(a.groupHistory(ServiceType::QQ, 1001).size(), 1u);
}

TEST_F(MessageCenterTest, HalfWrittenLineWaitsForItsNewline) {
    MessageCenter center(path_);
    ASSERT_TRUE(center.postPrivate(ServiceType::QQ, "10001", "10002", "第一条"));
    appendRaw("M QQ P 10002 10001 2026-10-18 10:00:00 写到一半");   // 还没有换行：另一个进程正写到一半
    EXPECT_EQ(center.conversation(ServiceType::QQ, "10001", "10002").size(), 1u);
    appendRaw("的消息\r\n");
    const auto messages = center.conversation(ServiceType::QQ, "10001", "10002");
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[1].text(), "写到一半的消息");   // 两次写入拼成完整的一行，没有被拆成两条
}

TEST_F(MessageCenterTest, IncrementalStateMatchesAFreshFullParse) {
    MessageCenter live(path_);
    live.postPrivate(ServiceType::QQ, "10001", "10002", "a");
    live.postGroup(ServiceType::QQ, 1001, "10001", "b");
    EXPECT_EQ(live.groupHistory(ServiceType::QQ, 1001).size(), 1u);   // 先读一次，之后的都是增量
    live.postPrivate(ServiceType::QQ, "10002", "10003", "c");
    live.revoke(ServiceType::QQ, 1, "10001");                            // 撤回第 1 条
    live.clearGroup(ServiceType::QQ, 1001);                              // 群解散：之前的群消息作废
    live.postGroup(ServiceType::QQ, 1001, "10009", "新群的第一条");       // 群号被新群复用
    live.clearAccount(ServiceType::QQ, "10003");                         // 账号注销：与之有关的私信作废
    live.markPrivateRead(ServiceType::QQ, "10002", "10001", 1);

    MessageCenter fresh(path_);   // 新实例：从头完整解析
    for (MessageCenter* center : { &live, &fresh }) {
        const auto chat = center->conversation(ServiceType::QQ, "10001", "10002");
        ASSERT_EQ(chat.size(), 1u);
        EXPECT_TRUE(chat[0].revoked());
        EXPECT_EQ(chat[0].revokedBy(), "10001");
        const auto group = center->groupHistory(ServiceType::QQ, 1001);
        ASSERT_EQ(group.size(), 1u);
        EXPECT_EQ(group[0].text(), "新群的第一条");
        EXPECT_TRUE(center->conversation(ServiceType::QQ, "10002", "10003").empty());
        EXPECT_EQ(center->readMark(ServiceType::QQ, "P", "10001", "10002"), 1);
    }
}

TEST_F(MessageCenterTest, ShrunkFileIsParsedAgainFromTheStart) {
    {
        MessageCenter writer(path_);
        writer.postPrivate(ServiceType::QQ, "10001", "10002", "旧数据 1");
        writer.postPrivate(ServiceType::QQ, "10001", "10002", "旧数据 2");
    }
    MessageCenter center(path_);
    EXPECT_EQ(center.conversation(ServiceType::QQ, "10001", "10002").size(), 2u);
    // 文件被换成了更短的另一份数据（比如恢复了演示数据）：缓存作废，从头解析
    fs::resize_file(fs::u8path(path_), 0);
    appendRaw("M QQ P 10002 10001 2026-10-18 10:00:00 新数据\r\n");
    const auto messages = center.conversation(ServiceType::QQ, "10001", "10002");
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].text(), "新数据");
    EXPECT_EQ(messages[0].seq(), 1);
}

TEST_F(MessageCenterTest, LastFromFindsTheNewestMessageOfASender) {
    MessageCenter center(path_);
    center.postPrivate(ServiceType::QQ, "10001", "10002", "早");
    center.postGroup(ServiceType::QQ, 1001, "10002", "别人的");
    center.postGroup(ServiceType::QQ, 1001, "10001", "晚");
    Message last(0, ServiceType::QQ, false, "", "", "", "");
    ASSERT_TRUE(center.lastFrom(ServiceType::QQ, "10001", last));
    EXPECT_EQ(last.text(), "晚");
    EXPECT_EQ(last.seq(), 3);
    EXPECT_FALSE(center.lastFrom(ServiceType::WECHAT, "10001", last));
}
