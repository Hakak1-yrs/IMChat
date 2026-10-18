#include "UtilYD.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>

std::vector<std::string> UtilYD::split(const std::string& s, char sep) {
    std::vector<std::string> result;
    std::istringstream iss(s);
    std::string item;
    while (std::getline(iss, item, sep)) {
        item = trim(item);
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

std::string UtilYD::joinList(const std::vector<std::string>& items) {
    if (items.empty()) return "-";
    std::string result;
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) result += ',';
        result += items[i];
    }
    return result;
}

std::vector<std::string> UtilYD::parseList(const std::string& s) {
    if (s == "-") return {};
    return split(s, ',');
}

std::string UtilYD::trim(const std::string& s) {
    const char* blanks = " \t\r\n";
    size_t begin = s.find_first_not_of(blanks);
    if (begin == std::string::npos) return "";
    size_t end = s.find_last_not_of(blanks);
    return s.substr(begin, end - begin + 1);
}

bool UtilYD::contains(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool UtilYD::removeValue(std::vector<std::string>& v, const std::string& x) {
    auto it = std::find(v.begin(), v.end(), x);
    if (it == v.end()) return false;
    v.erase(it);
    return true;
}

std::string UtilYD::readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string line;
    if (!std::getline(std::cin, line)) {
        throw std::runtime_error("输入已结束");   // 由 main 捕获，保证平台对象正常析构并保存数据
    }
    return trim(line);
}

std::string UtilYD::readWord(const std::string& prompt) {
    while (true) {
        std::string s = readLine(prompt);
        if (s.empty()) {
            std::cout << "输入不能为空。\n";
            continue;
        }
        return toField(s);
    }
}

std::string UtilYD::toField(std::string s) {
    // 数据文件以空格分隔字段、以逗号分隔列表，因此这两个字符不能出现在单个字段中
    std::replace(s.begin(), s.end(), ' ', '_');
    std::replace(s.begin(), s.end(), '\t', '_');
    std::replace(s.begin(), s.end(), ',', '_');
    return s;
}

std::string UtilYD::fieldText(const std::string& s) {
    std::string field = toField(s);
    field.erase(std::remove_if(field.begin(), field.end(), [](char c) { return c == '\r' || c == '\n'; }), field.end());
    return field.empty() ? "-" : field;
}

std::string UtilYD::lineText(const std::string& s) {
    std::string text = s;
    std::replace_if(text.begin(), text.end(), [](char c) { return c == '\r' || c == '\n' || c == '\t'; }, ' ');
    return trim(text);
}

bool UtilYD::containsText(const std::string& text, const std::string& keyword) {
    if (keyword.empty()) return true;
    for (size_t i = 0; i + keyword.size() <= text.size();) {
        if (text.compare(i, keyword.size(), keyword) == 0) return true;
        // GBK 双字节字符的首字节 >= 0x81：跳过整个字符，保证下一次比较从字符边界开始
        i += (static_cast<unsigned char>(text[i]) >= 0x81 && i + 1 < text.size()) ? 2 : 1;
    }
    return false;
}

int UtilYD::readInt(const std::string& prompt, int minValue, int maxValue) {
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

bool UtilYD::confirm(const std::string& prompt) {
    std::string s = readLine(prompt);
    return s == "y" || s == "Y";
}
