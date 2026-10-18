#pragma once
#include <cstddef>
#include <string>
#include <vector>

// 连接的收发缓冲区：从尾部追加，从头部取走。
// 取走数据只移动读位置，不搬动内存；读位置过了一半时才把剩余数据挪到开头，避免每次取数据都整体移动
class Buffer {
public:
    std::size_t readableBytes() const;
    const char* peek() const;
    void retrieve(std::size_t size);
    void retrieveAll();
    std::string retrieveAllAsString();
    void append(const char* data, std::size_t size);
    void append(const std::string& data);

private:
    std::vector<char> data_;
    std::size_t readPos_ = 0;
};
