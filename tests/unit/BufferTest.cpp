#include <gtest/gtest.h>
#include <algorithm>
#include <string>
#include "Buffer.h"

TEST(BufferTest, AppendAndRetrieve) {
    Buffer buffer;
    buffer.append("hello world");
    EXPECT_EQ(buffer.readableBytes(), 11u);
    buffer.retrieve(6);
    EXPECT_EQ(std::string(buffer.peek(), buffer.readableBytes()), "world");
    EXPECT_EQ(buffer.retrieveAllAsString(), "world");
    EXPECT_EQ(buffer.readableBytes(), 0u);
}

TEST(BufferTest, RetrievingMoreThanAvailableEmptiesBuffer) {
    Buffer buffer;
    buffer.append("abc");
    buffer.retrieve(100);
    EXPECT_EQ(buffer.readableBytes(), 0u);
}

TEST(BufferTest, CompactionKeepsUnreadDataIntact) {
    // 反复“追加一些、取走一些”，触发多次搬移，内容必须始终与期望一致
    Buffer buffer;
    std::string expected;
    for (int i = 0; i < 1000; ++i) {
        const std::string chunk = std::to_string(i) + ",";
        buffer.append(chunk);
        expected += chunk;
        const std::size_t take = static_cast<std::size_t>(i % 7);
        buffer.retrieve(take);
        expected.erase(0, std::min(take, expected.size()));
        ASSERT_EQ(std::string(buffer.peek(), buffer.readableBytes()), expected) << i;
    }
}
