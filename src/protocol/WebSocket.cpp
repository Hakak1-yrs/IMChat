#include "WebSocket.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text.remove_suffix(1);
    return text;
}

std::uint32_t rotl(std::uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); }

// SHA-1（FIPS 180-4）。这里只用来算握手响应：协议规定用 SHA-1，它在这里不承担安全作用
std::array<std::uint8_t, 20> sha1(const std::string& message) {
    std::uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    std::string data = message;
    const std::uint64_t bits = static_cast<std::uint64_t>(message.size()) * 8;
    data.push_back(static_cast<char>(0x80));
    while (data.size() % 64 != 56) data.push_back('\0');
    for (int i = 7; i >= 0; --i) data.push_back(static_cast<char>((bits >> (8 * i)) & 0xFF));
    for (std::size_t block = 0; block < data.size(); block += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 4; ++j) w[i] = (w[i] << 8) | static_cast<unsigned char>(data[block + 4 * i + j]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f = b ^ c ^ d, k = i < 40 ? 0x6ED9EBA1 : 0xCA62C1D6;   // 第 20～39、60～79 轮
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5A827999;
            } else if (i >= 40 && i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            }
            const std::uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
    }
    std::array<std::uint8_t, 20> digest{};
    for (int i = 0; i < 20; ++i) digest[i] = static_cast<std::uint8_t>(h[i / 4] >> (24 - 8 * (i % 4)));
    return digest;
}

std::string base64(const std::uint8_t* data, std::size_t size) {
    static const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (std::size_t i = 0; i < size; i += 3) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (i + 1 < size ? static_cast<std::uint32_t>(data[i + 1]) << 8 : 0) |
                                (i + 2 < size ? data[i + 2] : 0);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(i + 1 < size ? kAlphabet[(n >> 6) & 63] : '=');
        out.push_back(i + 2 < size ? kAlphabet[n & 63] : '=');
    }
    return out;
}

bool knownOpcode(unsigned opcode) { return opcode <= 2 || (opcode >= 8 && opcode <= 10); }

// 帧头：FIN + 操作码，再是载荷长度（小于 126 直接写；否则 126 + 2 字节或 127 + 8 字节），masked 时长度字节的最高位置 1
std::string header(WebSocketFrame::Opcode opcode, std::size_t size, bool fin, bool masked) {
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80 : 0) | static_cast<unsigned>(opcode)));
    const unsigned maskBit = masked ? 0x80 : 0;
    if (size < 126) {
        out.push_back(static_cast<char>(maskBit | size));
    } else if (size <= 0xFFFF) {
        out.push_back(static_cast<char>(maskBit | 126));
        out.push_back(static_cast<char>((size >> 8) & 0xFF));
        out.push_back(static_cast<char>(size & 0xFF));
    } else {
        out.push_back(static_cast<char>(maskBit | 127));
        for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((static_cast<std::uint64_t>(size) >> (8 * i)) & 0xFF));
    }
    return out;
}

}  // namespace

HttpRequest::ParseResult HttpRequest::parse(const char* data, std::size_t size, HttpRequest& request, std::size_t& consumed) {
    const std::string_view text(data, std::min(size, kMaxHeaderSize));
    const std::size_t end = text.find("\r\n\r\n");
    if (end == std::string_view::npos) return size >= kMaxHeaderSize ? ParseResult::BAD : ParseResult::NEED_MORE;
    request = HttpRequest{};
    bool firstLine = true;
    for (std::size_t start = 0; start < end;) {
        const std::size_t lineEnd = text.find("\r\n", start);
        const std::string_view line = text.substr(start, lineEnd - start);
        start = lineEnd + 2;
        if (firstLine) {   // 请求行："GET /path?query HTTP/1.1"
            firstLine = false;
            const std::size_t a = line.find(' '), b = line.rfind(' ');
            if (a == std::string_view::npos || a == b || line.substr(b + 1, 5) != "HTTP/") return ParseResult::BAD;
            request.method = std::string(line.substr(0, a));
            const std::string_view target = line.substr(a + 1, b - a - 1);
            request.path = std::string(target.substr(0, target.find('?')));
            if (request.path.empty() || request.path[0] != '/') return ParseResult::BAD;
            continue;
        }
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) return ParseResult::BAD;
        std::string& value = request.headers[lower(trim(line.substr(0, colon)))];
        if (!value.empty()) value += ", ";   // 同名的请求头合并成一个
        value += trim(line.substr(colon + 1));
    }
    if (firstLine) return ParseResult::BAD;   // 没有请求行
    consumed = end + 4;
    return ParseResult::OK;
}

std::string HttpRequest::header(const std::string& lowerName) const {
    auto it = headers.find(lowerName);
    return it != headers.end() ? it->second : std::string();
}

bool HttpRequest::isWebSocketUpgrade() const {
    return method == "GET" && lower(header("upgrade")).find("websocket") != std::string::npos &&
           lower(header("connection")).find("upgrade") != std::string::npos && !header("sec-websocket-key").empty() &&
           header("sec-websocket-version") == "13";
}

WebSocketFrame::DecodeResult WebSocketFrame::decode(const char* data, std::size_t size, std::size_t maxPayload,
                                                    WebSocketFrame& frame, std::size_t& consumed) {
    if (size < 2) return DecodeResult::NEED_MORE;
    const unsigned b0 = static_cast<unsigned char>(data[0]), b1 = static_cast<unsigned char>(data[1]);
    const bool fin = (b0 & 0x80) != 0;
    const unsigned opcode = b0 & 0x0F;
    if ((b0 & 0x70) != 0 || !knownOpcode(opcode) || (b1 & 0x80) == 0) return DecodeResult::BAD;   // 扩展位、未知操作码、没加掩码
    std::uint64_t length = b1 & 0x7F;
    std::size_t pos = 2;
    if (length >= 126) {
        const std::size_t bytes = length == 126 ? 2 : 8;
        if (size < pos + bytes) return DecodeResult::NEED_MORE;
        length = 0;
        for (std::size_t i = 0; i < bytes; ++i) length = (length << 8) | static_cast<unsigned char>(data[pos + i]);
        pos += bytes;
    }
    if (opcode >= 8 && (!fin || length > 125)) return DecodeResult::BAD;   // 控制帧不能分片，载荷最多 125 字节
    if (length > maxPayload) return DecodeResult::BAD;   // 先检查长度再等数据，和 Frame::decode 一样
    if (size < pos + 4 + length) return DecodeResult::NEED_MORE;
    const char* mask = data + pos;
    pos += 4;
    frame.fin = fin;
    frame.opcode = static_cast<Opcode>(opcode);
    frame.payload.resize(static_cast<std::size_t>(length));
    for (std::size_t i = 0; i < frame.payload.size(); ++i) frame.payload[i] = static_cast<char>(data[pos + i] ^ mask[i % 4]);
    consumed = pos + static_cast<std::size_t>(length);
    return DecodeResult::OK;
}

std::string WebSocketFrame::encode(Opcode opcode, const char* data, std::size_t size) {
    std::string out = header(opcode, size, true, false);
    out.append(data, size);
    return out;
}

std::string WebSocketFrame::encodeMasked(Opcode opcode, const std::string& payload, std::uint32_t mask, bool fin) {
    std::string out = header(opcode, payload.size(), fin, true);
    const char key[4] = { static_cast<char>(mask >> 24), static_cast<char>(mask >> 16), static_cast<char>(mask >> 8),
                          static_cast<char>(mask) };
    out.append(key, 4);
    for (std::size_t i = 0; i < payload.size(); ++i) out.push_back(static_cast<char>(payload[i] ^ key[i % 4]));
    return out;
}

std::string WebSocketFrame::acceptKey(const std::string& clientKey) {
    const std::array<std::uint8_t, 20> digest = sha1(clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
    return base64(digest.data(), digest.size());
}
