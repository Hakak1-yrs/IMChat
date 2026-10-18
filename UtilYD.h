#pragma once
#include <string>
#include <vector>

// 工具类：字符串拆分/拼接、控制台输入等通用静态函数
class UtilYD {
public:
    static std::vector<std::string> split(const std::string& s, char sep);
    static std::string joinList(const std::vector<std::string>& items);   // 空列表写作 "-"
    static std::vector<std::string> parseList(const std::string& s);      // "-" 解析为空列表
    static std::string trim(const std::string& s);
    static bool contains(const std::vector<std::string>& v, const std::string& x);
    static bool removeValue(std::vector<std::string>& v, const std::string& x);
    static std::string toField(std::string s);   // 空格和逗号替换为下划线，保证可以作为数据文件中的一个字段
    static std::string fieldText(const std::string& s);   // 写文件用：toField，空字符串写作 "-"，保证不会错列
    static std::string lineText(const std::string& s);    // 一行文字（消息、公告）：换行和制表符换成空格，去掉首尾空白
    // 按字符查找关键字：GBK 中文占两个字节，逐字符比较，避免从某个汉字的后半个字节开始误匹配
    static bool containsText(const std::string& text, const std::string& keyword);

    // ---- 控制台输入（统一使用整行读取，避免 cin >> 与 getline 混用的问题）----
    static std::string readLine(const std::string& prompt);
    static std::string readWord(const std::string& prompt);   // 不允许为空，空格和逗号替换为下划线
    static int readInt(const std::string& prompt, int minValue, int maxValue);
    static bool confirm(const std::string& prompt);
};
