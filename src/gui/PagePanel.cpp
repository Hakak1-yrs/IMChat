#include "PagePanel.h"
#include <algorithm>
#include "Canvas.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "Theme.h"
#include "Util.h"

namespace {
const float kHeaderHeight = 64.0f;
const float kComposerHeight = 76.0f;
const float kInfoHeight = 50.0f;
const float kPersonHeight = 64.0f;
const float kMaxColumn = 780.0f;   // 内容区最大宽度，宽屏时居中显示

bool isCardRow(PageModel::Kind kind) {
    return kind == PageModel::INFO || kind == PageModel::TEXT || kind == PageModel::PERSON;
}
}  // namespace

PagePanel::PagePanel(GuiWindow& window) : View(window) {}

void PagePanel::show() {
    composer_ = window_.createEdit(false, false, L"输入消息，回车发送", Theme::FIELD);
    ShowWindow(composer_, SW_HIDE);
}

void PagePanel::hide() {
    window_.destroyEdit(composer_);
    page_.reset();
}

void PagePanel::layout(const D2D1_RECT_F& bounds) {
    View::layout(bounds);
    placeEdits();
}

void PagePanel::setPage(std::shared_ptr<Page> page) {
    if (page_ != page) {
        scroll_ = 0;
        lastRows_ = 0;
        if (composer_) SetWindowTextW(composer_, L"");
        hasText_ = false;
    }
    page_ = std::move(page);
    composerShown_ = false;
    model_ = PageModel();
    if (page_) page_->build(model_);
    composerShown_ = model_.composer;
    placeEdits();
    window_.invalidate();
}

std::shared_ptr<Page> PagePanel::page() const { return page_; }

void PagePanel::clear() {
    page_.reset();
    composerShown_ = false;
    placeEdits();
}

D2D1_RECT_F PagePanel::bodyArea() const {
    return D2D1::RectF(bounds_.left, bounds_.top + kHeaderHeight, bounds_.right,
                       bounds_.bottom - (composerShown_ ? kComposerHeight : 0));
}

void PagePanel::placeEdits() {
    if (!composer_) return;
    const bool visible = page_ && composerShown_ && !window_.hasDialog();
    ShowWindow(composer_, visible ? SW_SHOW : SW_HIDE);
    if (visible) {
        const float mid = bounds_.bottom - kComposerHeight / 2;
        window_.placeEdit(composer_, D2D1::RectF(bounds_.left + 44, mid - 11, bounds_.right - 28 - 92 - 28, mid + 11));
    }
}

// ======================== 输入 ========================

void PagePanel::send() {
    if (!page_ || !composer_) return;
    const std::string text = Util::lineText(GuiText::utf8(GuiText::editText(composer_)));
    if (text.empty()) return;
    SetWindowTextW(composer_, L"");
    hasText_ = false;
    std::shared_ptr<Page> keep = page_;   // 处理过程中页面可能被替换
    keep->onCompose(text);
    window_.invalidate();
}

bool PagePanel::onEditKey(HWND edit, int key) {
    if (edit != composer_) return false;
    if (key == VK_RETURN) send();
    return true;
}

bool PagePanel::onEditChange(HWND edit) {
    if (edit != composer_) return false;
    hasText_ = GetWindowTextLengthW(composer_) > 0;
    window_.invalidate();
    return true;
}

void PagePanel::onAction(int action, int index) {
    if (action == ACT_FOCUS) {
        SetFocus(composer_);
        return;
    }
    if (action == ACT_BUTTON && index >= 0 && index < static_cast<int>(buttons_.size())) {
        if (buttons_[index].action == -1) {   // 输入框旁的“发送”按钮
            send();
            return;
        }
        const PageModel::Button button = buttons_[index];   // 复制一份：处理过程中页面可能被替换
        std::shared_ptr<Page> keep = page_;
        if (keep) keep->onAction(button.action, button.arg);
        window_.invalidate();
    }
}

void PagePanel::onWheel(float x, float y, float delta) {
    const D2D1_RECT_F area = bodyArea();
    if (!inside(area, x, y)) return;
    const float limit = (std::max)(0.0f, contentHeight_ - (area.bottom - area.top));
    scroll_ = (std::min)((std::max)(scroll_ - delta * 60, 0.0f), limit);
    window_.invalidate();
}

// ======================== 排版 ========================

float PagePanel::buttonWidth(Canvas& c, const PageModel::Button& b, bool compact) {
    return c.textWidth(b.text, compact ? 12.5f : 13.5f) + (compact ? 24.0f : 32.0f);
}

float PagePanel::measure(Canvas& c, const PageModel::Row& row, float width) {
    switch (row.kind) {
    case PageModel::SECTION: return 40;
    case PageModel::INFO: return kInfoHeight;
    case PageModel::PERSON: return kPersonHeight;
    case PageModel::TEXT:
        return Canvas::layoutSize(c.wrapLayout(row.title, 13.5f, width - 36).Get()).height + 28;
    case PageModel::NOTE:
        return Canvas::layoutSize(c.wrapLayout(row.title, 12.5f, width).Get()).height + 12;
    case PageModel::ACTIONS: {
        float x = 0, lines = 1;
        for (const PageModel::Button& b : row.buttons) {
            const float w = buttonWidth(c, b, false);
            if (x > 0 && x + w > width) {
                ++lines;
                x = 0;
            }
            x += w + 10;
        }
        return 14 + lines * 46;
    }
    case PageModel::CHAT: {
        if (row.chatKind == PageModel::CHAT_SYSTEM)
            return Canvas::layoutSize(c.wrapLayout(row.title, 12, width).Get()).height + 14;
        return Canvas::layoutSize(c.wrapLayout(row.title, 14, width * 0.7f - 28).Get()).height + 20 + 12;
    }
    }
    return 0;
}

// ======================== 绘制 ========================

void PagePanel::drawButton(Canvas& c, const PageModel::Button& b, const D2D1_RECT_F& r, bool compact,
                             const D2D1_RECT_F& clip) {
    const D2D1_COLOR_F accent = Theme::accent(window_.session().me().type);
    const bool hover = hovered(r);
    D2D1_COLOR_F fill, text;
    if (b.style == PageModel::PRIMARY) {
        fill = hover ? Theme::darker(accent, 0.08f) : accent;
        text = Theme::rgb(0xFFFFFF);
    } else if (b.style == PageModel::DANGER) {
        fill = Theme::rgb(Theme::DANGER, hover ? 0.16f : 0.09f);
        text = Theme::rgb(Theme::DANGER);
    } else {
        fill = Theme::rgb(hover ? 0xE5E6EB : Theme::FIELD);
        text = Theme::rgb(Theme::TEXT);
    }
    c.fillRound(r, compact ? 8.0f : 10.0f, fill);
    c.text(b.text, r, compact ? 12.5f : 13.5f, text, Align::CENTER,
           b.style == PageModel::PRIMARY ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
    const D2D1_RECT_F hit = D2D1::RectF(r.left, (std::max)(r.top, clip.top), r.right, (std::min)(r.bottom, clip.bottom));
    if (hit.bottom > hit.top) {
        buttons_.push_back(b);
        addHit(hit, ACT_BUTTON, static_cast<int>(buttons_.size()) - 1);
    }
}

void PagePanel::paint(Canvas& c) {
    beginHits();
    buttons_.clear();
    c.fillRect(bounds_, Theme::rgb(Theme::BG));
    if (!page_) return;
    model_ = PageModel();
    page_->build(model_);   // 每次重绘都取最新内容，数据变化后页面自然更新
    if (model_.composer != composerShown_) {
        composerShown_ = model_.composer;
        placeEdits();
    }
    const D2D1_COLOR_F accent = Theme::accent(window_.session().me().type);
    const D2D1_RECT_F area = bodyArea();
    const float areaWidth = area.right - area.left;
    const float width = (std::min)(kMaxColumn, areaWidth - 64);
    const float x0 = area.left + (areaWidth - width) / 2, x1 = x0 + width;

    // 先量出每一行的高度
    std::vector<float> heights;
    float total = 20;
    for (const PageModel::Row& row : model_.rows) {
        heights.push_back(measure(c, row, width));
        total += heights.back();
    }
    contentHeight_ = total + 24;
    const float viewHeight = area.bottom - area.top;
    const float limit = (std::max)(0.0f, contentHeight_ - viewHeight);
    if (model_.stickBottom && model_.rows.size() != lastRows_) scroll_ = limit;   // 聊天页有新消息：滚到最底部
    lastRows_ = model_.rows.size();
    scroll_ = (std::min)(scroll_, limit);

    c.pushClip(area);
    float y = area.top + 20 - scroll_;
    // 连续的资料行画在同一张白色卡片上
    for (size_t i = 0; i < model_.rows.size();) {
        if (!isCardRow(model_.rows[i].kind)) {
            y += heights[i++];
            continue;
        }
        float runTop = y;
        while (i < model_.rows.size() && isCardRow(model_.rows[i].kind)) y += heights[i++];
        c.fillRound(D2D1::RectF(x0, runTop, x1, y), 14, Theme::rgb(Theme::PANEL));
    }

    y = area.top + 20 - scroll_;
    for (size_t i = 0; i < model_.rows.size(); ++i) {
        const PageModel::Row& row = model_.rows[i];
        const float h = heights[i];
        const bool visible = y + h > area.top && y < area.bottom;
        if (visible) {
            const bool nextInCard = i + 1 < model_.rows.size() && isCardRow(model_.rows[i + 1].kind);
            switch (row.kind) {
            case PageModel::SECTION:
                c.text(row.title, D2D1::RectF(x0 + 4, y + 14, x1, y + 34), 13, Theme::rgb(Theme::TEXT3), Align::LEFT,
                       DWRITE_FONT_WEIGHT_SEMI_BOLD);
                break;
            case PageModel::INFO:
            case PageModel::PERSON: {
                // 右侧按钮（从右往左排）
                float right = x1 - 16;
                const float cy = y + h / 2;
                for (auto it = row.buttons.rbegin(); it != row.buttons.rend(); ++it) {
                    const float w = buttonWidth(c, *it, true);
                    drawButton(c, *it, D2D1::RectF(right - w, cy - 15, right, cy + 15), true, area);
                    right -= w + 8;
                }
                if (row.kind == PageModel::INFO) {
                    c.text(row.title, D2D1::RectF(x0 + 20, y, x0 + 128, y + h), 13.5f, Theme::rgb(Theme::TEXT3));
                    c.text(row.value, D2D1::RectF(x0 + 128, y, right - 8, y + h), 13.5f, Theme::rgb(Theme::TEXT));
                } else {
                    const float ax = x0 + 20, size = 40;
                    const D2D1_RECT_F avatar = D2D1::RectF(ax, cy - size / 2, ax + size, cy + size / 2);
                    if (row.square) c.fillRound(avatar, 12, Theme::avatar(row.avatarKey));
                    else c.fillCircle(ax + size / 2, cy, size / 2, Theme::avatar(row.avatarKey));
                    std::wstring initial = row.avatarName.empty() ? L"?" : GuiText::initial(GuiText::utf8(row.avatarName));
                    c.text(initial, avatar, 16, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                    const float tx = ax + size + 14;
                    float titleRight = (std::min)(right - 8, tx + c.textWidth(row.title, 14) + 2);
                    c.text(row.title, D2D1::RectF(tx, y + 11, titleRight, y + 33), 14, Theme::rgb(Theme::TEXT));
                    float tagX = titleRight + 8;
                    for (const PageModel::Tag& tag : row.tags) {   // 标签：群主、管理员、禁言……
                        const float w = c.textWidth(tag.text, 11) + 14;
                        if (tagX + w > right - 8) break;
                        const D2D1_RECT_F t = D2D1::RectF(tagX, y + 13, tagX + w, y + 31);
                        c.fillRound(t, 6, Theme::rgb(tag.color, 0.12f));
                        c.text(tag.text, t, 11, Theme::rgb(tag.color), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                        tagX += w + 6;
                    }
                    c.text(row.value, D2D1::RectF(tx, y + 34, right - 8, y + 54), 12.5f, Theme::rgb(Theme::TEXT3));
                }
                if (nextInCard) c.line(x0 + 20, y + h - 0.5f, x1 - 20, y + h - 0.5f, Theme::rgb(Theme::BORDER));
                break;
            }
            case PageModel::TEXT: {
                auto layout = c.wrapLayout(row.title, 13.5f, width - 36);
                c.drawLayout(layout.Get(), x0 + 18, y + 14, Theme::rgb(Theme::TEXT2));
                if (nextInCard) c.line(x0 + 20, y + h - 0.5f, x1 - 20, y + h - 0.5f, Theme::rgb(Theme::BORDER));
                break;
            }
            case PageModel::NOTE: {
                auto layout = c.wrapLayout(row.title, 12.5f, width);
                c.drawLayout(layout.Get(), x0 + 4, y + 6, Theme::rgb(Theme::TEXT3));
                break;
            }
            case PageModel::ACTIONS: {
                float x = x0, top = y + 14;
                for (const PageModel::Button& b : row.buttons) {
                    const float w = buttonWidth(c, b, false);
                    if (x > x0 && x + w > x1) {
                        x = x0;
                        top += 46;
                    }
                    drawButton(c, b, D2D1::RectF(x, top, x + w, top + 36), false, area);
                    x += w + 10;
                }
                break;
            }
            case PageModel::CHAT: {
                if (row.chatKind == PageModel::CHAT_SYSTEM) {
                    auto layout = c.wrapLayout(row.title, 12, width);
                    const float w = Canvas::layoutSize(layout.Get()).width;
                    c.drawLayout(layout.Get(), (x0 + x1 - w) / 2, y + 4, Theme::rgb(Theme::TEXT3));
                    break;
                }
                auto layout = c.wrapLayout(row.title, 14, width * 0.7f - 28);
                const D2D1_SIZE_F size = Canvas::layoutSize(layout.Get());
                const float bw = size.width + 28, bh = size.height + 20;
                const bool mine = row.chatKind == PageModel::CHAT_MINE;
                const D2D1_RECT_F bubble = mine ? D2D1::RectF(x1 - bw, y, x1, y + bh) : D2D1::RectF(x0, y, x0 + bw, y + bh);
                if (!mine) c.shadow(bubble, 12, 3, 0.05f);
                c.fillRound(bubble, 12, mine ? accent : Theme::rgb(0xFFFFFF));
                c.drawLayout(layout.Get(), bubble.left + 14, bubble.top + 10, mine ? Theme::rgb(0xFFFFFF) : Theme::rgb(Theme::TEXT));
                break;
            }
            }
        }
        y += h;
    }
    if (contentHeight_ > viewHeight) {   // 细滚动条
        const float thumb = (std::max)(40.0f, viewHeight * viewHeight / contentHeight_);
        const float ty = area.top + (viewHeight - thumb) * (limit > 0 ? scroll_ / limit : 0);
        c.fillRound(D2D1::RectF(area.right - 8, ty + 4, area.right - 4, ty + thumb - 4), 2, Theme::rgb(0x000000, 0.12f));
    }
    c.popClip();

    // 标题栏
    const D2D1_RECT_F h = D2D1::RectF(bounds_.left, bounds_.top, bounds_.right, bounds_.top + kHeaderHeight);
    c.fillRect(h, Theme::rgb(Theme::PANEL));
    c.line(h.left, h.bottom - 0.5f, h.right, h.bottom - 0.5f, Theme::rgb(Theme::BORDER));
    float right = h.right - 24;
    for (auto it = model_.headerButtons.rbegin(); it != model_.headerButtons.rend(); ++it) {
        const float w = buttonWidth(c, *it, true);
        drawButton(c, *it, D2D1::RectF(right - w, h.top + 17, right, h.top + 47), true, h);
        right -= w + 8;
    }
    c.text(model_.title, D2D1::RectF(h.left + 28, h.top + 11, right - 12, h.top + 36), 17, Theme::rgb(Theme::TEXT),
           Align::LEFT, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(model_.subtitle, D2D1::RectF(h.left + 28, h.top + 36, right - 12, h.top + 54), 12, Theme::rgb(Theme::TEXT3));

    // 底部输入框（聊天页）
    if (composerShown_) {
        const D2D1_RECT_F bar = D2D1::RectF(bounds_.left, bounds_.bottom - kComposerHeight, bounds_.right, bounds_.bottom);
        c.fillRect(bar, Theme::rgb(Theme::PANEL));
        c.line(bar.left, bar.top + 0.5f, bar.right, bar.top + 0.5f, Theme::rgb(Theme::BORDER));
        const D2D1_RECT_F field = D2D1::RectF(bar.left + 28, bar.top + 16, bar.right - 28 - 92 - 12, bar.bottom - 16);
        c.fillRound(field, 10, Theme::rgb(Theme::FIELD));
        addHit(field, ACT_FOCUS);
        const D2D1_RECT_F sendBox = D2D1::RectF(bar.right - 28 - 92, bar.top + 16, bar.right - 28, bar.bottom - 16);
        PageModel::Button sendButton = PageModel::button(L"发送", -1, "", PageModel::PRIMARY);
        drawButton(c, sendButton, sendBox, false, bar);
        if (!hasText_) c.fillRound(sendBox, 10, Theme::rgb(0xFFFFFF, 0.55f));   // 没有内容时按钮显得淡一些
    }
}
