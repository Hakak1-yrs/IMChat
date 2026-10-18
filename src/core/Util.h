#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

// 工具类：字符串拆分/拼接、编码转换、控制台输入等通用静态函数。
// 程序内部的字符串一律是 UTF-8；只在调用 Windows 宽字符 API（文件路径、控制台、界面）时转换成 UTF-16
class Util {
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
    static bool containsText(const std::string& text, const std::string& keyword);
    // 执行 action，把它输出到 cout 的内容按行收集（去掉首尾空白和空行）。
    // 业务类的 show 系列虚函数为控制台而写，图形界面和服务端用它把输出转成数据
    static std::vector<std::string> captureLines(const std::function<void()>& action);

    // ---- 编码 ----
    static std::wstring widen(const std::string& utf8);
    static std::string narrow(const std::wstring& text);              // UTF-16 → UTF-8
    static std::filesystem::path pathOf(const std::string& utf8);     // 路径可能含中文，必须经宽字符构造
    static size_t charCount(const std::string& utf8);                 // 字符数（不是字节数）
    static bool isValidUtf8(const std::string& text);
    static std::string truncate(const std::string& utf8, size_t maxChars);   // 超长时截断并加“…”，不会截断半个字符

    // ---- 控制台输入（统一使用整行读取，避免 cin >> 与 getline 混用的问题）----
    static bool getLine(std::string& line);                   // 读一整行原样返回；输入结束时返回 false
    static std::string readLine(const std::string& prompt);
    static std::string readWord(const std::string& prompt);   // 不允许为空，空格和逗号替换为下划线
    static int readInt(const std::string& prompt, int minValue, int maxValue);
    static bool confirm(const std::string& prompt);
};
