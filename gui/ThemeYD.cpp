#include "ThemeYD.h"

D2D1_COLOR_F ThemeYD::rgb(unsigned hex, float alpha) {
    return D2D1::ColorF(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha);
}

D2D1_COLOR_F ThemeYD::accent(ServiceTypeYD type) {
    switch (type) {
    case ServiceTypeYD::QQ: return rgb(0x2B7FFF);
    case ServiceTypeYD::WECHAT: return rgb(0x07B862);
    case ServiceTypeYD::WEIBO: return rgb(0xFF7A1A);
    case ServiceTypeYD::WEISHANG: return rgb(0x8B5CF6);
    }
    return rgb(0x2B7FFF);
}

D2D1_COLOR_F ThemeYD::darker(const D2D1_COLOR_F& c, float amount) {
    return D2D1::ColorF(c.r * (1 - amount), c.g * (1 - amount), c.b * (1 - amount), c.a);
}

D2D1_COLOR_F ThemeYD::avatar(const std::string& key) {
    static const unsigned palette[] = { 0x5B8FF9, 0x36B59D, 0xF0A33A, 0xE8684A, 0x4FB3D9,
                                        0x9270CA, 0xF08BB4, 0x6C8EBF, 0x3FA26B };
    unsigned hash = 2166136261u;   // FNV-1a：同一个账号每次得到同一种颜色
    for (unsigned char c : key) hash = (hash ^ c) * 16777619u;
    return rgb(palette[hash % (sizeof(palette) / sizeof(palette[0]))]);
}

std::wstring ThemeYD::shortName(ServiceTypeYD type) {
    switch (type) {
    case ServiceTypeYD::QQ: return L"QQ";
    case ServiceTypeYD::WECHAT: return L"微信";
    case ServiceTypeYD::WEIBO: return L"微博";
    case ServiceTypeYD::WEISHANG: return L"微商";
    }
    return L"";
}
