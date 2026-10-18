#include <gtest/gtest.h>
#include <string>
#include "Packet.h"

namespace {

std::string header(std::uint32_t length, std::uint16_t type) {
    std::string out;
    for (int i = 3; i >= 0; --i) out.push_back(static_cast<char>((length >> (8 * i)) & 0xFF));
    out.push_back(static_cast<char>(type >> 8));
    out.push_back(static_cast<char>(type & 0xFF));
    return out;
}

}  // namespace

TEST(FrameTest, EncodesLengthAndTypeInBigEndian) {
    const std::string bytes = Frame::encode(MessageType::LOGIN_REQ, "ab");
    EXPECT_EQ(bytes, header(4, 100) + "ab");   // 长度 = 2 字节类型 + 2 字节正文
}

TEST(FrameTest, RoundTrip) {
    const std::string bytes = Frame::encode(MessageType::SEND_MSG_REQ, "{\"text\":\"你好\"}");
    Frame frame;
    std::size_t consumed = 0;
    ASSERT_EQ(Frame::decode(bytes.data(), bytes.size(), frame, consumed), Frame::DecodeResult::OK);
    EXPECT_EQ(consumed, bytes.size());
    EXPECT_EQ(frame.type, MessageType::SEND_MSG_REQ);
    EXPECT_EQ(frame.body, "{\"text\":\"你好\"}");
}

TEST(FrameTest, HalfPacketNeedsMoreUntilLastByte) {
    // 半包：数据逐字节到达，只有最后一个字节到了才能解出完整的一帧
    const std::string bytes = Frame::encode(MessageType::PING, "payload");
    Frame frame;
    std::size_t consumed = 0;
    for (std::size_t size = 0; size < bytes.size(); ++size)
        EXPECT_EQ(Frame::decode(bytes.data(), size, frame, consumed), Frame::DecodeResult::NEED_MORE) << size;
    EXPECT_EQ(Frame::decode(bytes.data(), bytes.size(), frame, consumed), Frame::DecodeResult::OK);
}

TEST(FrameTest, StickyPacketsDecodeOneByOne) {
    // 粘包：三帧连在一起到达，依次解出，最后剩半帧
    const std::string third = Frame::encode(MessageType::MSG_PUSH, "3");
    const std::string bytes = Frame::encode(MessageType::PING, "") + Frame::encode(MessageType::PONG, "22") +
                              third.substr(0, third.size() - 1);
    std::size_t offset = 0;
    Frame frame;
    std::size_t consumed = 0;
    ASSERT_EQ(Frame::decode(bytes.data(), bytes.size(), frame, consumed), Frame::DecodeResult::OK);
    EXPECT_EQ(frame.type, MessageType::PING);
    EXPECT_TRUE(frame.body.empty());
    offset += consumed;
    ASSERT_EQ(Frame::decode(bytes.data() + offset, bytes.size() - offset, frame, consumed), Frame::DecodeResult::OK);
    EXPECT_EQ(frame.type, MessageType::PONG);
    EXPECT_EQ(frame.body, "22");
    offset += consumed;
    EXPECT_EQ(Frame::decode(bytes.data() + offset, bytes.size() - offset, frame, consumed), Frame::DecodeResult::NEED_MORE);
}

TEST(FrameTest, RejectsInvalidLengthBeforeBodyArrives) {
    Frame frame;
    std::size_t consumed = 0;
    const std::string tooShort = header(1, 1);   // 长度连类型字段都放不下
    EXPECT_EQ(Frame::decode(tooShort.data(), tooShort.size(), frame, consumed), Frame::DecodeResult::BAD_LENGTH);
    // 声称有 4 GB：不必等数据“收全”，看到长度就判定为错误
    const std::string huge = header(0xFFFFFFFFu, 1);
    EXPECT_EQ(Frame::decode(huge.data(), huge.size(), frame, consumed), Frame::DecodeResult::BAD_LENGTH);
    const std::string overLimit = header(Frame::kTypeSize + Frame::kMaxBodySize + 1, 1);
    EXPECT_EQ(Frame::decode(overLimit.data(), overLimit.size(), frame, consumed), Frame::DecodeResult::BAD_LENGTH);
}

TEST(FrameTest, AcceptsBodyAtExactLimit) {
    const std::string bytes = Frame::encode(MessageType::PING, std::string(Frame::kMaxBodySize, 'x'));
    Frame frame;
    std::size_t consumed = 0;
    EXPECT_EQ(Frame::decode(bytes.data(), bytes.size(), frame, consumed), Frame::DecodeResult::OK);
    EXPECT_EQ(frame.body.size(), Frame::kMaxBodySize);
}

TEST(PacketTest, JsonRoundTripKeepsUtf8) {
    Packet packet;
    packet.type = MessageType::SEND_MSG_REQ;
    packet.body = { { "to", "10002" }, { "text", "表情 😀 和中文" }, { "reqId", 7 } };
    const std::string bytes = packet.encode();
    Frame frame;
    std::size_t consumed = 0;
    ASSERT_EQ(Frame::decode(bytes.data(), bytes.size(), frame, consumed), Frame::DecodeResult::OK);
    std::optional<Packet> decoded = Packet::fromFrame(frame);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->type, MessageType::SEND_MSG_REQ);
    EXPECT_EQ(decoded->body["text"], "表情 😀 和中文");
    EXPECT_EQ(decoded->body["reqId"], 7);
}

TEST(PacketTest, RejectsBodiesThatAreNotJsonObjects) {
    Frame frame;
    frame.type = MessageType::LOGIN_REQ;
    frame.body = "not json";
    EXPECT_FALSE(Packet::fromFrame(frame));
    frame.body = "[1,2,3]";   // 合法的 JSON，但不是对象
    EXPECT_FALSE(Packet::fromFrame(frame));
    frame.body = "";          // 空正文视为 {}
    std::optional<Packet> empty = Packet::fromFrame(frame);
    ASSERT_TRUE(empty);
    EXPECT_TRUE(empty->body.is_object() && empty->body.empty());
}
