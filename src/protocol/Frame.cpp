#include "Frame.h"
#include <cstdint>

namespace {

// 按大端序读写整数：逐字节移位，和机器本身的字节序无关
void putUint(std::string& out, std::uint32_t value, std::size_t bytes) {
    for (std::size_t i = bytes; i > 0; --i) out.push_back(static_cast<char>((value >> (8 * (i - 1))) & 0xFF));
}

std::uint32_t getUint(const char* data, std::size_t bytes) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) value = (value << 8) | static_cast<unsigned char>(data[i]);
    return value;
}

}  // namespace

std::string Frame::encode() const { return encode(type, body); }

std::string Frame::encode(MessageType type, const std::string& body) {
    std::string out;
    out.reserve(kHeaderSize + body.size());
    putUint(out, static_cast<std::uint32_t>(kTypeSize + body.size()), kLengthSize);
    putUint(out, static_cast<std::uint16_t>(type), kTypeSize);
    out += body;
    return out;
}

Frame::DecodeResult Frame::decode(const char* data, std::size_t size, Frame& frame, std::size_t& consumed) {
    if (size < kLengthSize) return DecodeResult::NEED_MORE;
    const std::uint32_t length = getUint(data, kLengthSize);
    // 先检查长度再等数据：不合法的长度不必等它“收全”
    if (length < kTypeSize || length > kTypeSize + kMaxBodySize) return DecodeResult::BAD_LENGTH;
    if (size < kLengthSize + length) return DecodeResult::NEED_MORE;
    frame.type = static_cast<MessageType>(getUint(data + kLengthSize, kTypeSize));
    frame.body.assign(data + kHeaderSize, length - kTypeSize);
    consumed = kLengthSize + length;
    return DecodeResult::OK;
}
