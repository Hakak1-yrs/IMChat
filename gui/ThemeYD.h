#pragma once
#include <d2d1.h>
#include <string>
#include "ServiceTypeYD.h"

// 界面配色：整体用浅色、低饱和的中性色，只有“强调色”随当前服务变化
// （QQ 蓝、微信绿、微博橙、微商紫），切换服务时整个界面的主色调随之改变
class ThemeYD {
public:
    static constexpr unsigned TEXT = 0x1D2129;     // 主要文字
    static constexpr unsigned TEXT2 = 0x4E5969;    // 次要文字
    static constexpr unsigned TEXT3 = 0x86909C;    // 说明文字
    static constexpr unsigned TEXT4 = 0xC2C7D0;    // 最弱的文字（时间等）
    static constexpr unsigned BORDER = 0xE8EAEE;   // 分隔线
    static constexpr unsigned BG = 0xF5F6F8;       // 聊天区背景
    static constexpr unsigned PANEL = 0xFFFFFF;    // 面板
    static constexpr unsigned FIELD = 0xF2F3F5;    // 输入框底色
    static constexpr unsigned HOVER = 0xF5F6F8;    // 鼠标悬停
    static constexpr unsigned RAIL = 0xEEF0F4;     // 最左侧导航栏
    static constexpr unsigned DANGER = 0xF54A45;   // 未读角标、错误提示

    static D2D1_COLOR_F rgb(unsigned hex, float alpha = 1.0f);
    static D2D1_COLOR_F accent(ServiceTypeYD type);
    static D2D1_COLOR_F darker(const D2D1_COLOR_F& c, float amount);   // 按下/悬停时略微加深
    static D2D1_COLOR_F avatar(const std::string& key);                 // 头像底色：按账号固定取一种柔和的颜色
    static std::wstring shortName(ServiceTypeYD type);                  // 导航栏上的服务名称
};
