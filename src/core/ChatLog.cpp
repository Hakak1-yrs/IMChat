#include "ChatLog.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <deque>
#include <fstream>
#include <iostream>
#include "Date.h"
#include "Util.h"

ChatLog::ChatLog(const std::string& filePath) : filePath_(filePath) {}

void ChatLog::append(const std::string& direction, const std::string& peer, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string line = Date::nowText() + "  " + direction + "  " + peer + "：" + text + "\r\n";
    // 本机演示时两个程序窗口会同时写同一个记录文件。以追加方式打开（Windows 的 FILE_APPEND_DATA、POSIX 的 O_APPEND），
    // 由系统保证每次写入都完整地接在文件末尾，两个窗口的记录不会互相覆盖（“先定位到末尾再写”可能丢行）
#ifdef _WIN32
    HANDLE file = CreateFileW(Util::widen(filePath_).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
#else
    const int fd = ::open(filePath_.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) return;
    const ssize_t written = ::write(fd, line.data(), line.size());
    (void)written;
    ::close(fd);
#endif
}

std::vector<std::string> ChatLog::query(const std::string& keyword, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(Util::pathOf(filePath_));
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        line = Util::trim(line);
        if (line.empty() || (!keyword.empty() && !Util::containsText(line, keyword))) continue;
        lines.push_back(line);
        if (keyword.empty() && static_cast<int>(lines.size()) > count) lines.pop_front();   // 只保留最近 count 条
    }
    return std::vector<std::string>(lines.begin(), lines.end());
}

int ChatLog::show(const std::string& keyword, int count) const {
    std::vector<std::string> lines = query(keyword, count);
    for (const std::string& l : lines) std::cout << "  " << l << '\n';
    return static_cast<int>(lines.size());
}
