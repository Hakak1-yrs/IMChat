#pragma once
#include <string>

// 简单的日志：带时间和级别，一行一条，多线程同时写也不会交错
class Log {
public:
    enum class Level { DEBUG, INFO, WARN, ERROR_ };

    static void setLevel(Level level);   // 低于该级别的不输出（测试时调高，避免刷屏）
    static void debug(const std::string& text);
    static void info(const std::string& text);
    static void warn(const std::string& text);
    static void error(const std::string& text);

private:
    static void write(Level level, const std::string& text);
};
