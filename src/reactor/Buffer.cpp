#include "Buffer.h"
#include <algorithm>

std::size_t Buffer::readableBytes() const { return data_.size() - readPos_; }

const char* Buffer::peek() const { return data_.data() + readPos_; }

void Buffer::retrieve(std::size_t size) {
    readPos_ += std::min(size, readableBytes());
    if (readPos_ == data_.size()) retrieveAll();
}

void Buffer::retrieveAll() {
    data_.clear();
    readPos_ = 0;
}

std::string Buffer::retrieveAllAsString() {
    std::string text(peek(), readableBytes());
    retrieveAll();
    return text;
}

void Buffer::append(const char* data, std::size_t size) {
    if (readPos_ > 0 && readPos_ >= data_.size() / 2) {   // 前面已取走的部分超过一半：先把剩余数据挪到开头
        data_.erase(data_.begin(), data_.begin() + static_cast<std::ptrdiff_t>(readPos_));
        readPos_ = 0;
    }
    data_.insert(data_.end(), data, data + size);
}

void Buffer::append(const std::string& data) { append(data.data(), data.size()); }
