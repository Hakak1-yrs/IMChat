#include "Util.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

// 解码 text[i] 开始的一个 UTF-8 字符：返回它占的字节数并写出码点；不是合法的 UTF-8 时返回 0
int decodeUtf8(const std::string& text, size_t i, unsigned long& code) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    const int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
    if (extra < 0 || i + extra >= text.size()) return 0;
    code = extra == 0 ? c : (c & (0x3Fu >> extra));
    for (int k = 1; k <= extra; ++k) {
        const unsigned char next = static_cast<unsigned char>(text[i + k]);
        if ((next & 0xC0) != 0x80) return 0;
        code = (code << 6) | (next & 0x3F);
    }
    return extra + 1;
}

}  // namespace

// 从控制台读一行。Windows 控制台输入代码页设为 UTF-8 时，ReadFile（std::cin 走的路径）读中文会得到空字节，
// 所以控制台用 ReadConsoleW 读 UTF-16 再转 UTF-8；输入被重定向（管道、文件）时，以及其他平台上，照常用 std::getline
bool Util::getLine(std::string& line) {
#ifdef _WIN32
    HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    if (!GetConsoleMode(input, &mode)) {
#endif
        if (!std::getline(std::cin, line)) return false;
        if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);   // 重定向的输入文件可能以 BOM 开头
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return true;
#ifdef _WIN32
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
#endif
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

std::vector<std::string> Util::captureLines(const std::function<void()>& action) {
    std::ostringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
    action();
    std::cout.rdbuf(old);
    std::vector<std::string> lines;
    std::istringstream in(buffer.str());
    for (std::string line; std::getline(in, line);) {
        line = trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
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

#ifdef _WIN32
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
#else
// 其他平台上 wchar_t 通常是 32 位（UTF-32，如 Linux），也可能是 16 位（UTF-16，如 Cygwin）：
// 按 UTF-8 的规则逐个字符转换，16 位时超出基本平面的字符用代理对表示；非法字节按 U+FFFD 处理
std::wstring Util::widen(const std::string& utf8) {
    std::wstring result;
    for (size_t i = 0; i < utf8.size();) {
        unsigned long code = 0;
        const int length = decodeUtf8(utf8, i, code);
        if (length == 0) code = 0xFFFD;
        if (sizeof(wchar_t) == 2 && code >= 0x10000) {
            code -= 0x10000;
            result += static_cast<wchar_t>(0xD800 + (code >> 10));
            result += static_cast<wchar_t>(0xDC00 + (code & 0x3FF));
        } else {
            result += static_cast<wchar_t>(code);
        }
        i += length > 0 ? static_cast<size_t>(length) : 1;
    }
    return result;
}

std::string Util::narrow(const std::wstring& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned long c = static_cast<unsigned long>(text[i]) & (sizeof(wchar_t) == 2 ? 0xFFFFul : 0xFFFFFFFFul);
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < text.size()) {   // 代理对（16 位 wchar_t）
            const unsigned long low = static_cast<unsigned long>(text[i + 1]) & 0xFFFF;
            if (low >= 0xDC00 && low < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }
        if (c < 0x80) {
            result += static_cast<char>(c);
        } else if (c < 0x800) {
            result += static_cast<char>(0xC0 | (c >> 6));
            result += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            result += static_cast<char>(0xE0 | (c >> 12));
            result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (c >> 18));
            result += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return result;
}

std::filesystem::path Util::pathOf(const std::string& utf8) { return std::filesystem::path(utf8); }   // 路径本来就是 UTF-8
#endif

bool Util::isValidUtf8(const std::string& text) {
    for (size_t i = 0; i < text.size();) {
        unsigned long code = 0;
        const int length = decodeUtf8(text, i, code);
        if (length == 0) return false;
        i += static_cast<size_t>(length);
    }
    return true;
}

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
