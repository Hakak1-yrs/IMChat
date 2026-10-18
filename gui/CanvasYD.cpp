#include "CanvasYD.h"

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace {
const wchar_t* kFontFamily = L"Microsoft YaHei UI";   // 微软雅黑：中文界面的系统字体
// 表情符号用彩色字体（Segoe UI Emoji）画出彩色图案，不加这个选项只会画成单色轮廓
const D2D1_DRAW_TEXT_OPTIONS kColorFont = D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT;
}

bool CanvasYD::init(HWND hwnd, float dpi) {
    hwnd_ = hwnd;
    dpi_ = dpi;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    // Windows 11 的新图标字体更精致；Windows 10 没有时使用旧版图标字体（两者的图标编码相同）
    ComPtrYD<IDWriteFontCollection> fonts;
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(dwrite_->GetSystemFontCollection(fonts.GetAddressOf())) &&
        SUCCEEDED(fonts->FindFamilyName(L"Segoe Fluent Icons", &index, &exists)) && exists)
        iconFamily_ = L"Segoe Fluent Icons";
    return createTarget();
}

bool CanvasYD::createTarget() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = dpi_;
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps =
        D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
    if (FAILED(factory_->CreateHwndRenderTarget(props, hwndProps, target_.ReleaseAndGetAddressOf()))) return false;
    target_->SetDpi(dpi_, dpi_);
    return SUCCEEDED(target_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), brush_.ReleaseAndGetAddressOf()));
}

void CanvasYD::setDpi(float dpi) {
    dpi_ = dpi;
    if (target_) target_->SetDpi(dpi, dpi);
}

void CanvasYD::resize() {
    if (!target_) return;
    RECT rc;
    GetClientRect(hwnd_, &rc);
    target_->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
}

bool CanvasYD::begin() {
    if (!target_ && !createTarget()) return false;
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    return true;
}

void CanvasYD::end() {
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {   // 显卡设备丢失（如远程桌面切换），下一帧重建
        target_.Reset();
        brush_.Reset();
    }
}

float CanvasYD::width() const { return target_ ? target_->GetSize().width : 0.0f; }

float CanvasYD::height() const { return target_ ? target_->GetSize().height : 0.0f; }

void CanvasYD::clear(const D2D1_COLOR_F& c) { target_->Clear(c); }

void CanvasYD::fillRect(const D2D1_RECT_F& r, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    target_->FillRectangle(r, brush_.Get());
}

void CanvasYD::fillRound(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    target_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
}

void CanvasYD::strokeRound(const D2D1_RECT_F& r, float radius, const D2D1_COLOR_F& c, float width) {
    brush_->SetColor(c);
    const float h = width / 2;   // 线条画在边界内侧，避免被裁掉一半
    target_->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + h, r.top + h, r.right - h, r.bottom - h), radius, radius),
                                  brush_.Get(), width);
}

void CanvasYD::fillCircle(float cx, float cy, float radius, const D2D1_COLOR_F& c) {
    brush_->SetColor(c);
    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius), brush_.Get());
}

void CanvasYD::line(float x1, float y1, float x2, float y2, const D2D1_COLOR_F& c, float width) {
    brush_->SetColor(c);
    target_->DrawLine(D2D1::Point2F(x1, y1), D2D1::Point2F(x2, y2), brush_.Get(), width);
}

void CanvasYD::verticalGradient(const D2D1_RECT_F& r, const D2D1_COLOR_F& top, const D2D1_COLOR_F& bottom) {
    D2D1_GRADIENT_STOP stops[] = { { 0.0f, top }, { 1.0f, bottom } };
    ComPtrYD<ID2D1GradientStopCollection> collection;
    ComPtrYD<ID2D1LinearGradientBrush> gradient;
    if (FAILED(target_->CreateGradientStopCollection(stops, 2, collection.GetAddressOf()))) return;
    if (FAILED(target_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top), D2D1::Point2F(r.left, r.bottom)),
            collection.Get(), gradient.GetAddressOf())))
        return;
    target_->FillRectangle(r, gradient.Get());
}

void CanvasYD::shadow(const D2D1_RECT_F& r, float radius, float size, float alpha) {
    const int layers = 8;
    for (int i = layers; i >= 1; --i) {
        const float spread = size * i / layers;
        const float a = alpha * (1.0f - static_cast<float>(i - 1) / layers) / layers * 2.0f;
        fillRound(D2D1::RectF(r.left - spread, r.top - spread + size * 0.35f, r.right + spread, r.bottom + spread + size * 0.35f),
                  radius + spread, D2D1::ColorF(0.10f, 0.13f, 0.20f, a));
    }
}

void CanvasYD::pushClip(const D2D1_RECT_F& r) { target_->PushAxisAlignedClip(r, D2D1_ANTIALIAS_MODE_ALIASED); }

void CanvasYD::popClip() { target_->PopAxisAlignedClip(); }

IDWriteTextFormat* CanvasYD::format(float size, DWRITE_FONT_WEIGHT weight, bool iconFont) {
    auto key = std::make_tuple(static_cast<int>(size * 100), static_cast<int>(weight), iconFont);
    auto it = formats_.find(key);
    if (it != formats_.end()) return it->second.Get();
    ComPtrYD<IDWriteTextFormat> fmt;
    dwrite_->CreateTextFormat(iconFont ? iconFamily_.c_str() : kFontFamily, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, size, L"zh-cn", fmt.GetAddressOf());
    ComPtrYD<IDWriteInlineObject> sign;
    dwrite_->CreateEllipsisTrimmingSign(fmt.Get(), sign.GetAddressOf());
    ellipsis_[fmt.Get()] = sign;
    formats_[key] = fmt;
    return fmt.Get();
}

void CanvasYD::text(const std::wstring& s, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c, AlignYD align,
                    DWRITE_FONT_WEIGHT weight) {
    if (s.empty() || r.right <= r.left) return;
    IDWriteTextFormat* fmt = format(size, weight, false);
    ComPtrYD<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), fmt, r.right - r.left, r.bottom - r.top,
                                         layout.GetAddressOf())))
        return;
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trimming = { DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0 };
    layout->SetTrimming(&trimming, ellipsis_[fmt].Get());
    layout->SetTextAlignment(align == AlignYD::CENTER  ? DWRITE_TEXT_ALIGNMENT_CENTER
                             : align == AlignYD::RIGHT ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                       : DWRITE_TEXT_ALIGNMENT_LEADING);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    brush_->SetColor(c);
    target_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP | kColorFont);
}

void CanvasYD::icon(wchar_t glyph, const D2D1_RECT_F& r, float size, const D2D1_COLOR_F& c) {
    IDWriteTextFormat* fmt = format(size, DWRITE_FONT_WEIGHT_NORMAL, true);
    ComPtrYD<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(&glyph, 1, fmt, r.right - r.left, r.bottom - r.top, layout.GetAddressOf()))) return;
    layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    brush_->SetColor(c);
    target_->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), brush_.Get(), kColorFont);
}

float CanvasYD::textWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight) {
    ComPtrYD<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), format(size, weight, false), 10000.0f,
                                         1000.0f, layout.GetAddressOf())))
        return 0.0f;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

CanvasYD::ComPtrYD<IDWriteTextLayout> CanvasYD::wrapLayout(const std::wstring& s, float size, float maxWidth,
                                                           DWRITE_FONT_WEIGHT weight) {
    ComPtrYD<IDWriteTextLayout> layout;
    dwrite_->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), format(size, weight, false), maxWidth, 100000.0f,
                              layout.GetAddressOf());
    if (layout) layout->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK);   // 很长的英文单词或网址也能断开
    return layout;
}

D2D1_SIZE_F CanvasYD::layoutSize(IDWriteTextLayout* layout) {
    DWRITE_TEXT_METRICS m{};
    if (layout) layout->GetMetrics(&m);
    return D2D1::SizeF(m.width, m.height);
}

void CanvasYD::drawLayout(IDWriteTextLayout* layout, float x, float y, const D2D1_COLOR_F& c) {
    if (!layout) return;
    brush_->SetColor(c);
    target_->DrawTextLayout(D2D1::Point2F(x, y), layout, brush_.Get(), kColorFont);
}
