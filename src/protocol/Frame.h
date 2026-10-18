#pragma once
#include <cstddef>
#include <string>
#include "MessageType.h"

// 帧格式：[4 字节长度][2 字节消息类型][正文]，整数都是网络字节序（大端）。
// “长度”是类型加正文的字节数，不含长度字段本身。
// TCP 是字节流：一次收到的数据可能是半帧，也可能是好几帧，解码器按长度切分（处理半包和粘包）。
// 长度超过上限直接判定为错误，避免对方报一个巨大的长度让我们无限制地缓存数据
struct Frame {
    MessageType type = MessageType::PING;
    std::string body;

    static constexpr std::size_t kLengthSize = 4;
    static constexpr std::size_t kTypeSize = 2;
    static constexpr std::size_t kHeaderSize = kLengthSize + kTypeSize;
    static constexpr std::size_t kMaxBodySize = 1 << 20;   // 1 MiB

    enum class DecodeResult { NEED_MORE, OK, BAD_LENGTH };

    std::string encode() const;
    static std::string encode(MessageType type, const std::string& body);
    // 从 data 开头尝试解出一帧；成功时 consumed 是这一帧占用的字节数。
    // 数据不够一帧时返回 NEED_MORE（等下次收到更多数据再试），长度字段不合法时返回 BAD_LENGTH（应断开连接）
    static DecodeResult decode(const char* data, std::size_t size, Frame& frame, std::size_t& consumed);
};
