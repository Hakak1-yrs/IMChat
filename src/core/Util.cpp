#include "Util.h"
#include <windows.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

// 从控制台读一行。控制台输入代码页设为 UTF-8 时，ReadFile（std::cin 走的路径）读中文会得到空字节，
// 所以控制台用 ReadConsoleW 读 UTF-16 再转 UTF-8；输入被重定向（管道、文件）时照常用 std::getline
bool Util::getLine(std::string& line) {
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(input, &mode)) {
        if (!std::getline(std::cin, line)) return false;
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);   // 重定向的输入文件可能以 BOM 开头
        return true;
    }
    std::wstring text;
    wchar_t buffer[256];
    while (text.empty() || text.back() != L'\n') {
        DWORD count = 0;
        if (!ReadConsoleW(input, buffer, 256, &count, nullptr) || count == 0) return false;
        text.append(buffer, count);
    }
    if (!text.empty() && text[0] == 0x1A) return false;   // Ctrl+Z 回车：输入结束
    while (!text.empty() && (text.back() == L'\n' || text.back() == L'\r')) text.pop_back();
    line = narrow(text);
    return true;
}

std::vector<std::string> Util::split(const std::string& s, char sep) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, sep)) {
        item = trim(item);
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

std::string Util::joinList(const std::vector<std::string>& items) {
    if (items.empty()) return "-";
    std::string result;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) result += ',';
        result += items[i];
    }
    return result;
}

std::vector<std::string> Util::parseList(const std::string& s) {
    if (s == "-") return {};
    return split(s, ',');
}

std::string Util::trim(const std::string& s) {
    const char* blanks = " \t\r\n";
    size_t begin = s.find_first_not_of(blanks);
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(blanks);
    return s.substr(begin, end - begin + 1);
}

bool Util::contains(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool Util::removeValue(std::vector<std::string>& v, const std::string& x) {
    auto it = std::find(v.begin(), v.end(), x);
    if (it == v.end()) return false;
    v.erase(it);
    return true;
}

std::string Util::readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    if (!getLine(line)) {
        throw std::runtime_error("输入已结束");   // 由 main 捕获，保证平台对象正常析构并保存数据
    }
    return trim(line);
}

std::string Util::readWord(const std::string& prompt) {
    while (true) {
        std::string s = readLine(prompt);
        if (s.empty()) {
            std::cout << "输入不能为空。\n";
            continue;
        }
        return toField(s);
    }
}

std::string Util::toField(std::string s) {
    // 数据文件以空格分隔字段、以逗号分隔列表，因此这两个字符不能出现在单个字段中
    std::replace(s.begin(), s.end(), ' ', '_');
    std::replace(s.begin(), s.end(), '\t', '_');
    std::replace(s.begin(), s.end(), ',', '_');
    return s;
}

std::string Util::fieldText(const std::string& s) {
    std::string field = toField(s);
    field.erase(std::remove_if(field.begin(), field.end(), [](char c) { return c == '\r' || c == '\n'; }), field.end());
    return field.empty() ? "-" : field;
}

std::string Util::lineText(const std::string& s) {
    std::string text = s;
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\r' || c == '\n' || c == '\t'; }, ' ');
    return trim(text);
}

bool Util::containsText(const std::string& text, const std::string& keyword) {
    // UTF-8 是自同步编码：一个字符的字节序列不会出现在另一个字符的中间，直接按字节查找就是按字符查找
    return text.find(keyword) != std::string::npos;
}

std::wstring Util::widen(const std::string& utf8) {
    if (utf8.empty()) return L"";
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &result[0], length);
    return result;
}

std::string Util::narrow(const std::wstring& text) {
    if (text.empty()) return "";
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], length, nullptr, nullptr);
    return result;
}

std::filesystem::path Util::pathOf(const std::string& utf8) { return std::filesystem::path(widen(utf8)); }

size_t Util::charCount(const std::string& utf8) {
    // 只数每个字符的首字节；后续字节的形式都是 10xxxxxx
    return static_cast<size_t>(std::count_if(utf8.begin(), utf8.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::string Util::truncate(const std::string& utf8, size_t maxChars) {
    size_t chars = 0;
    for (size_t i = 0; i < utf8.size(); ++i) {
        if ((static_cast<unsigned char>(utf8[i]) & 0xC0) == 0x80) continue;
        if (chars++ == maxChars) return utf8.substr(0, i) + "…";
    }
    return utf8;
}

int Util::readInt(const std::string& prompt, int minValue, int maxValue) {
    while (true) {
        std::string s = readLine(prompt);
        try {
            size_t pos = 0;
            int value = std::stoi(s, &pos);
            if (pos == s.size() && value >= minValue && value <= maxValue) return value;
        } catch (...) {
        }
        std::cout << "输入无效，请输入 " << minValue << " ~ " << maxValue << " 之间的整数。\n";
    }
}

bool Util::confirm(const std::string& prompt) {
    std::string s = readLine(prompt);
    return s == "y" || s == "Y";
}
