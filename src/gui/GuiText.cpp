#include "GuiText.h"
#include <cstdio>
#include <cwctype>
#include <iostream>
#include <sstream>
#include "Date.h"
#include "Util.h"

namespace {

bool parseTime(const std::string& time, int& y, int& mo, int& d, int& h, int& mi) {
    h = mi = 0;
    return sscanf_s(time.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) >= 3;
}

// 公历日期 → 从 1970-01-01 起的天数（只用于计算间隔）
long long daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const long long yoe = y - era * 400;
    const long long doy = (153LL * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

}  // namespace

std::wstring GuiText::wide(const std::string& utf8) { return Util::widen(utf8); }

std::string GuiText::utf8(const std::wstring& text) { return Util::narrow(text); }

std::wstring GuiText::editText(HWND edit) {
    int length = GetWindowTextLengthW(edit);
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(edit, &text[0], length + 1);
    text.resize(length);
    return text;
}

std::wstring GuiText::initial(const std::string& name) {
    std::wstring w = wide(name);
    for (wchar_t c : w) {
        if (iswspace(c) || c == L'(' || c == L'（' || c == L'_') continue;
        return std::wstring(1, static_cast<wchar_t>(towupper(c)));
    }
    return L"?";
}

std::wstring GuiText::listTime(const std::string& time) {
    int y, mo, d, h, mi;
    if (!parseTime(time, y, mo, d, h, mi)) return L"";
    wchar_t buf[32];
    if (time.compare(0, 10, Date::today().toString()) == 0) swprintf_s(buf, L"%02d:%02d", h, mi);
    else swprintf_s(buf, L"%02d-%02d", mo, d);
    return buf;
}

std::wstring GuiText::chatTime(const std::string& time) {
    int y, mo, d, h, mi;
    if (!parseTime(time, y, mo, d, h, mi)) return wide(time);
    const std::string today = Date::today().toString();
    wchar_t buf[48];
    if (time.compare(0, 10, today) == 0) swprintf_s(buf, L"%02d:%02d", h, mi);
    else if (time.compare(0, 4, today, 0, 4) == 0) swprintf_s(buf, L"%d月%d日 %02d:%02d", mo, d, h, mi);
    else swprintf_s(buf, L"%d年%d月%d日 %02d:%02d", y, mo, d, h, mi);
    return buf;
}

std::vector<std::string> GuiText::capture(const std::function<void()>& action) {
    std::ostringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
    action();
    std::cout.rdbuf(old);
    std::vector<std::string> lines;
    std::istringstream in(buffer.str());
    std::string line;
    while (std::getline(in, line)) {
        line = Util::trim(line);
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

long long GuiText::minutes(const std::string& time) {
    int y, mo, d, h, mi;
    if (!parseTime(time, y, mo, d, h, mi)) return 0;
    return daysFromCivil(y, mo, d) * 1440 + h * 60 + mi;
}
