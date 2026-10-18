#include "Date.h"
#include <cstdio>
#include <ctime>

namespace {

std::tm localNow() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    return local;
}

}  // namespace

Date::Date(int year, int month, int day) : year_(year), month_(month), day_(day) {}

Date Date::today() {
    const std::tm local = localNow();
    return Date(local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
}

bool Date::parse(const std::string& text, Date& out) {
    int y = 0, m = 0, d = 0, used = 0;
    // %n 记下读到了第几个字符：后面还有多余内容（如 2000-01-01x）就不算合法日期
    if (std::sscanf(text.c_str(), "%d-%d-%d%n", &y, &m, &d, &used) != 3 || used != static_cast<int>(text.size())) return false;
    static const int daysInMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (y < 1900 || y > 2100 || m < 1 || m > 12 || d < 1) return false;
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    int maxDay = (m == 2 && leap) ? 29 : daysInMonth[m - 1];
    if (d > maxDay) return false;   // 拒绝 2 月 30 日之类不存在的日期
    out = Date(y, m, d);
    return true;
}

std::string Date::nowText() {
    const std::tm local = localNow();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &local);
    return buf;
}

std::string Date::nowStamp() {
    const std::tm local = localNow();
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &local);
    return buf;
}

std::string Date::toString() const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year_, month_, day_);
    return buf;
}

int Date::yearsUntil(const Date& later) const {
    int years = later.year_ - year_;
    if (later.month_ < month_ || (later.month_ == month_ && later.day_ < day_)) --years;
    return years < 0 ? 0 : years;
}
