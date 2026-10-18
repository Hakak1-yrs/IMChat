#pragma once
#include <string>

// 日期类：用于出生日期、号码申请时间，以及计算 T 龄
class DateYD {
public:
    DateYD(int year = 2000, int month = 1, int day = 1);

    static DateYD today();
    static bool parse(const std::string& text, DateYD& out);   // 格式：yyyy-mm-dd

    std::string toString() const;
    int yearsUntil(const DateYD& later) const;   // 到 later 为止经过的整年数

    static std::string nowText();     // 当前时间，格式 yyyy-mm-dd hh:mm:ss（用于聊天记录）
    static std::string nowStamp();    // 当前时间，格式 yyyymmdd_hhmmss（用于备份目录名）

private:
    int year_;
    int month_;
    int day_;
};
