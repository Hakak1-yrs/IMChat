#include "Sha256.h"
#include <algorithm>
#include <cstring>

namespace {

const std::uint32_t kRound[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be,
    0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa,
    0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85,
    0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f,
    0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

}  // namespace

Sha256::Sha256()
    : state_{ 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 }, buffer_{} {}

void Sha256::transform(const std::uint8_t* block) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) | (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) | block[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const std::uint8_t* data, std::size_t size) {
    length_ += size;
    while (size > 0) {
        const std::size_t take = (std::min)(size, sizeof(buffer_) - buffered_);
        std::memcpy(buffer_ + buffered_, data, take);
        buffered_ += take;
        data += take;
        size -= take;
        if (buffered_ == sizeof(buffer_)) {
            transform(buffer_);
            buffered_ = 0;
        }
    }
}

Sha256::Digest Sha256::finish() {
    // 补位：一个 1 比特，若干 0，最后 8 字节是消息的比特长度（大端）
    const std::uint64_t bits = length_ * 8;
    const std::uint8_t one = 0x80, zero = 0;
    update(&one, 1);
    while (buffered_ != 56) update(&zero, 1);
    std::uint8_t tail[8];
    for (int i = 0; i < 8; ++i) tail[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    update(tail, 8);
    Digest digest{};
    for (int i = 0; i < 8; ++i) {
        for (int k = 0; k < 4; ++k) digest[i * 4 + k] = static_cast<std::uint8_t>(state_[i] >> (24 - 8 * k));
    }
    return digest;
}

Sha256::Digest Sha256::hash(const std::string& data) {
    Sha256 sha;
    sha.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    return sha.finish();
}

Sha256::Digest Sha256::hmac(const std::string& key, const std::string& data) {
    // HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))，K' 是补齐到 64 字节的密钥（太长的先哈希）
    std::uint8_t block[64] = {};
    if (key.size() > sizeof(block)) {
        const Digest hashed = hash(key);
        std::memcpy(block, hashed.data(), hashed.size());
    } else {
        std::memcpy(block, key.data(), key.size());
    }
    std::uint8_t inner[64], outer[64];
    for (int i = 0; i < 64; ++i) {
        inner[i] = block[i] ^ 0x36;
        outer[i] = block[i] ^ 0x5c;
    }
    Sha256 first;
    first.update(inner, sizeof(inner));
    first.update(reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
    const Digest innerDigest = first.finish();
    Sha256 second;
    second.update(outer, sizeof(outer));
    second.update(innerDigest.data(), innerDigest.size());
    return second.finish();
}

std::string Sha256::hex(const Digest& digest) {
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for (std::uint8_t byte : digest) {
        text += digits[byte >> 4];
        text += digits[byte & 0x0F];
    }
    return text;
}
