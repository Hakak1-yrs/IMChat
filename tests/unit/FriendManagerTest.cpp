#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "FriendManager.h"

namespace {

std::vector<std::string> ids(const FriendManager& friends) {
    std::vector<std::string> result;
    for (const Friend& f : friends.all()) result.push_back(f.id());
    return result;
}

}  // namespace

TEST(FriendManagerTest, KeepsInsertionOrderAndFindsAfterRemovals) {
    FriendManager friends;
    for (const char* id : { "10001", "10002", "10003", "10004" }) EXPECT_TRUE(friends.add(id, std::string("备注") + id));
    EXPECT_FALSE(friends.add("10002", "重复"));
    EXPECT_TRUE(friends.remove("10002"));   // 删掉中间的：后面的前移，索引跟着更新
    EXPECT_FALSE(friends.remove("10002"));
    EXPECT_EQ(ids(friends), (std::vector<std::string>{ "10001", "10003", "10004" }));
    ASSERT_NE(friends.find("10004"), nullptr);
    EXPECT_EQ(friends.find("10004")->remark(), "备注10004");
    EXPECT_EQ(friends.find("10002"), nullptr);
    EXPECT_TRUE(friends.modifyRemark("10003", "新备注"));
    EXPECT_EQ(friends.find("10003")->remark(), "新备注");
    EXPECT_FALSE(friends.modifyRemark("10002", "不存在"));
    EXPECT_TRUE(friends.remove("10001"));   // 删掉第一个
    EXPECT_TRUE(friends.add("10002", "又加回来"));   // 加在最后
    EXPECT_EQ(ids(friends), (std::vector<std::string>{ "10003", "10004", "10002" }));
    EXPECT_EQ(friends.find("10002")->remark(), "又加回来");
    EXPECT_TRUE(friends.contains("10004"));
    EXPECT_EQ(friends.size(), 3u);

    FriendManager copy = friends;   // 账号对象会被复制：索引也要跟着正确复制
    EXPECT_TRUE(copy.remove("10003"));
    EXPECT_EQ(copy.find("10002")->remark(), "又加回来");
    EXPECT_TRUE(friends.contains("10003"));
}
