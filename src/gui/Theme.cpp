#include "Theme.h"

D2D1_COLOR_F Theme::rgb(unsigned hex, float alpha) {
    return D2D1::ColorF(((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f, (hex & 0xFF) / 255.0f, alpha);
}

D2D1_COLOR_F Theme::accent(ServiceType type) {
    switch (type) {
    case ServiceType::QQ: return rgb(0x2B7FFF);
    case ServiceType::WECHAT: return rgb(0x07B862);
    case ServiceType::WEIBO: return rgb(0xFF7A1A);
    case ServiceType::WEISHANG: return rgb(0x8B5CF6);
    }
    return rgb(0x2B7FFF);
}

D2D1_COLOR_F Theme::darker(const D2D1_COLOR_F& c, float amount) {
    return D2D1::ColorF(c.r * (1 - amount), c.g * (1 - amount), c.b * (1 - amount), c.a);
}

D2D1_COLOR_F Theme::avatar(const std::string& key) {
    static const unsigned palette[] = { 0x5B8FF9, 0x36B59D, 0xF0A33A, 0xE8684A, 0x4FB3D9,
                                        0x9270CA, 0xF08BB4, 0x6C8EBF, 0x3FA26B };
    unsigned hash = 2166136261u;   // FNV-1a：同一个账号每次得到同一种颜色
    for (unsigned char c : key) hash = (hash ^ c) * 16777619u;
    return rgb(palette[hash % (sizeof(palette) / sizeof(palette[0]))]);
}

std::wstring Theme::shortName(ServiceType type) {
    switch (type) {
    case ServiceType::QQ: return L"QQ";
    case ServiceType::WECHAT: return L"微信";
    case ServiceType::WEIBO: return L"微博";
    case ServiceType::WEISHANG: return L"微商";
    }
    return L"";
}
