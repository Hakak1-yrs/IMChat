#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <tuple>

enum class Align { LEFT, CENTER, RIGHT };

// 画布：封装 Direct2D（图形）和 DirectWrite（文字），界面的所有内容都通过它绘制。
// 坐标一律使用“设备无关像素”（DIP，96 DPI 下 1 DIP = 1 像素），高分屏上自动放大，文字和圆角都保持清晰
class Canvas {
public:
    template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool init(HWND hwnd, float dpi);
    void setDpi(float dpi);
    void resize();                      // 窗口大小变化后调用
    bool begin();                       // 开始一帧
    void end();                         // 结束一帧；显卡设备丢失时下次自动重建
    float width() const;
    float height() const;

    // ---- 图形 ----
    void clear(const D2D1_COLOR_F& c);
    void fillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c);
    void fillRound(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c);
    void strokeRound(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float width = 1.0f);
    void fillCircle(float cx, float cy, float radius, const D2D1_COLOR_F& c);
    void line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float width = 1.0f);
    void verticalGradient(const D2D1_RECT_F& r, const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom);
    void shadow(const D2D1_RECT_F& r, float radius, float size, float alpha);   // 几层逐渐变淡的圆角矩形，形成柔和阴影
    void pushClip(const D2D1_RECT_F& r);
    void popClip();

    // ---- 文字 ----
    // 单行文字：在 r 中垂直居中，超出宽度时以“…”结尾
    void text(const std::wstring& s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c,
              Align align = Align::LEFT, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    void icon(wchar_t glyph, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c);   // 系统图标字体中的图标
    float textWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    // 多行文字：按 maxWidth 自动换行。排版结果由调用者缓存，避免每次重绘都重新排版
    ComPtr<IDWriteTextLayout> wrapLayout(const std::wstring& s, float size, float maxWidth,
                                           DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    static D2D1_SIZE_F layoutSize(IDWriteTextLayout* layout);
    void drawLayout(IDWriteTextLayout* layout, float x, float y, const D2D1_COLOR_F& c);

private:
    IDWriteTextFormat* format(float size, DWRITE_FONT_WEIGHT weight, bool iconFont);
    bool createTarget();

    HWND hwnd_ = nullptr;
    float dpi_ = 96.0f;
    std::wstring iconFamily_ = L"Segoe MDL2 Assets";
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    std::map<std::tuple<int, int, bool>, ComPtr<IDWriteTextFormat>> formats_;
    std::map<IDWriteTextFormat*, ComPtr<IDWriteInlineObject>> ellipsis_;
};
