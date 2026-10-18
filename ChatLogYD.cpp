#include "ChatLogYD.h"
#include <windows.h>
#include <deque>
#include <fstream>
#include <iostream>
#include "DateYD.h"
#include "UtilYD.h"

ChatLogYD::ChatLogYD(const std::string& filePath) : filePath_(filePath) {}

void ChatLogYD::append(const std::string& direction, const std::string& peer, const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string line = DateYD::nowText() + "  " + direction + "  " + peer + "：" + text + "\r\n";
    // 本机演示时两个程序窗口会同时写同一个记录文件。以 FILE_APPEND_DATA 方式打开，由系统保证每次写入
    // 都完整地接在文件末尾，两个窗口的记录不会互相覆盖（普通的追加方式是“先定位到末尾再写”，可能丢行）
    HANDLE file = CreateFileA(filePath_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    CloseHandle(file);
}

std::vector<std::string> ChatLogYD::query(const std::string& keyword, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ifstream in(filePath_);
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        line = UtilYD::trim(line);
        if (line.empty() || (!keyword.empty() && !UtilYD::containsText(line, keyword))) continue;
        lines.push_back(line);
        if (keyword.empty() && static_cast<int>(lines.size()) > count) lines.pop_front();   // 只保留最近 count 条
    }
    return std::vector<std::string>(lines.begin(), lines.end());
}

int ChatLogYD::show(const std::string& keyword, int count) const {
    std::vector<std::string> lines = query(keyword, count);
    for (const std::string& l : lines) std::cout << "  " << l << '\n';
    return static_cast<int>(lines.size());
}
