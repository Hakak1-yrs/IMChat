#include "DateYD.h"
#include <cstdio>
#include <ctime>

DateYD::DateYD(int year, int month, int day) : year_(year), month_(month), day_(day) {}

DateYD DateYD::today() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    return DateYD(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

bool DateYD::parse(const std::string& text, DateYD& out) {
    int y = 0, m = 0, d = 0;
    char extra = 0;
    if (sscanf_s(text.c_str(), "%d-%d-%d%c", &y, &m, &d, &extra, 1) != 3) return false;
    static const int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (y < 1900 || y > 2100 || m < 1 || m > 12 || d < 1) return false;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    int maxDay = (m == 2 && leap) ? 29 : daysInMonth[m - 1];
    if (d > maxDay) return false;   // 拒绝 2 月 30 日之类不存在的日期
    out = DateYD(y, m, d);
    return true;
}

std::string DateYD::nowText() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

std::string DateYD::nowStamp() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &local);
    return buf;
}

std::string DateYD::toString() const {
    char buf[16];
    sprintf_s(buf, "%04d-%02d-%02d", year_, month_, day_);
    return buf;
}

int DateYD::yearsUntil(const DateYD& later) const {
    int years = later.year_ - year_;
    if (later.month_ < month_ || (later.month_ == month_ && later.day_ < day_)) --years;
    return years < 0 ? 0 : years;
}
