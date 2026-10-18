#include "DialogYD.h"
#include <algorithm>
#include "CanvasYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "ThemeYD.h"
#include "UtilYD.h"

namespace {
const float kRowHeight = 40.0f;   // 选择列表每一行的高度
const int kVisibleRows = 5;       // 选择列表最多同时显示几行，多了可以滚动
const wchar_t kIconCheck = 0xE73E;
}  // namespace

DialogYD::DialogYD(GuiWindowYD& window, const std::wstring& title) : ViewYD(window), title_(title) {}

// ======================== 组成部分 ========================

DialogYD& DialogYD::message(const std::wstring& text) {
    parts_.push_back({ 0, messages_.size() });
    messages_.push_back(text);
    return *this;
}

DialogYD& DialogYD::field(const std::wstring& label, const std::wstring& initial, const std::wstring& placeholder,
                          bool password) {
    parts_.push_back({ 1, fields_.size() });
    fields_.push_back({ label, initial, placeholder, password, nullptr, {} });
    return *this;
}

DialogYD& DialogYD::choice(const std::wstring& label, const std::vector<std::wstring>& options, int selected) {
    parts_.push_back({ 2, choices_.size() });
    choices_.push_back({ label, options, selected, {} });
    return *this;
}

DialogYD& DialogYD::pick(const std::wstring& label, const std::vector<std::wstring>& items, bool multiple) {
    parts_.push_back({ 3, 0 });
    pickLabel_ = label;
    pickItems_ = items;
    pickChecked_.assign(items.size(), false);
    pickMultiple_ = multiple;
    return *this;
}

DialogYD& DialogYD::lines(const std::vector<std::wstring>& lines) {
    parts_.push_back({ 4, 0 });
    lines_ = lines;
    return *this;
}

DialogYD& DialogYD::primary(const std::wstring& text, SubmitYD onSubmit, bool danger) {
    primaryText_ = text;
    onSubmit_ = std::move(onSubmit);
    danger_ = danger;
    return *this;
}

DialogYD& DialogYD::cancelText(const std::wstring& text) {
    cancelText_ = text;
    return *this;
}

// ======================== 读取结果 ========================

std::string DialogYD::text(size_t field) const {
    return field < fields_.size() && fields_[field].edit ? UtilYD::trim(GuiTextYD::utf8(GuiTextYD::editText(fields_[field].edit)))
                                                         : "";
}

int DialogYD::selected(size_t choice) const { return choice < choices_.size() ? choices_[choice].selected : -1; }

std::vector<size_t> DialogYD::picked() const {
    std::vector<size_t> result;
    for (size_t i = 0; i < pickChecked_.size(); ++i) {
        if (pickChecked_[i]) result.push_back(i);
    }
    return result;
}

void DialogYD::setError(const std::wstring& error) {
    error_ = error;
    computeLayout();
    window_.invalidate();
}

bool DialogYD::owns(HWND edit) const {
    for (const FieldYD& f : fields_) {
        if (f.edit == edit) return true;
    }
    return false;
}

// ======================== 显示与排版 ========================

void DialogYD::show() {
    for (FieldYD& f : fields_) {
        f.edit = window_.createEdit(false, f.password, f.placeholder, ThemeYD::FIELD);
        SetWindowTextW(f.edit, f.initial.c_str());
    }
    if (!fields_.empty()) GuiWindowYD::focus(fields_[0].edit);
}

void DialogYD::hide() {
    for (FieldYD& f : fields_) window_.destroyEdit(f.edit);
}

void DialogYD::layout(const D2D1_RECT_F& bounds) {
    ViewYD::layout(bounds);
    computeLayout();
}

float DialogYD::listHeight() const {
    const int rows = (std::min)(static_cast<int>(pickItems_.size()), kVisibleRows);
    return (std::max)(rows, 1) * kRowHeight + 8;
}

float DialogYD::linesContentHeight() {
    float h = 0;
    const float width = (card_.right - card_.left) - 56 - 24;
    for (const std::wstring& line : lines_) {
        auto layout = window_.canvas().wrapLayout(line, 13.5f, width);
        h += CanvasYD::layoutSize(layout.Get()).height + 10;
    }
    return h;
}

// 从上到下依次排好各部分；先按原点 0 排一次求出总高度，再把卡片放在窗口正中
void DialogYD::computeLayout() {
    const bool twoColumns = fields_.size() >= 5;   // 输入项较多（注册、开通服务）时分两列排，对话框不会超出窗口
    const float width = twoColumns ? 600.0f : (lines_.empty() ? 440.0f : 560.0f);
    auto place = [&](float left, float top) {
        card_ = D2D1::RectF(left, top, left + width, top);
        const float x0 = left + 28, x1 = left + width - 28, mid = (x0 + x1) / 2;
        float y = top + 26 + 28 + 16;
        int column = 0;          // 两列排版时，下一个输入框放在第几列
        float rowTop = y;
        messageBoxes_.assign(messages_.size(), D2D1_RECT_F{});
        for (const PartYD& part : parts_) {
            if (twoColumns && part.kind == 1) {
                if (column == 0) {
                    y += 24;
                    rowTop = y;
                    fields_[part.index].box = D2D1::RectF(x0, rowTop, mid - 8, rowTop + 44);
                    y += 44 + 14;
                    column = 1;
                } else {
                    fields_[part.index].box = D2D1::RectF(mid + 8, rowTop, x1, rowTop + 44);
                    column = 0;
                }
                continue;
            }
            column = 0;
            switch (part.kind) {
            case 0: {
                auto layout = window_.canvas().wrapLayout(messages_[part.index], 14, x1 - x0);
                const float h = CanvasYD::layoutSize(layout.Get()).height;
                messageBoxes_[part.index] = D2D1::RectF(x0, y, x1, y + h);
                y += h + 16;
                break;
            }
            case 1:
                y += 24;
                fields_[part.index].box = D2D1::RectF(x0, y, x1, y + 44);
                y += 44 + 14;
                break;
            case 2:
                y += 24;
                choices_[part.index].box = D2D1::RectF(x0, y, x1, y + 40);
                y += 40 + 14;
                break;
            case 3:
                y += 24;
                pickBox_ = D2D1::RectF(x0, y, x1, y + listHeight());
                y += listHeight() + 14;
                break;
            case 4: {
                const float h = (std::min)(linesContentHeight(), 340.0f);
                linesBox_ = D2D1::RectF(x0, y, x1, y + h + 24);
                y += h + 24 + 14;
                break;
            }
            }
        }
        if (!error_.empty()) {
            errorBox_ = D2D1::RectF(x0, y, x1, y + 20);
            y += 28;
        }
        y += 4;
        okBox_ = primaryText_.empty() ? D2D1_RECT_F{} : D2D1::RectF(x1 - 108, y, x1, y + 40);
        const float cancelRight = primaryText_.empty() ? x1 : x1 - 108 - 10;
        cancelBox_ = D2D1::RectF(cancelRight - 92, y, cancelRight, y + 40);
        y += 40 + 24;
        card_.bottom = y;
        return y - top;
    };
    const float height = place(0, 0);
    const float left = (bounds_.left + bounds_.right - width) / 2;
    const float top = (std::max)(bounds_.top + 16, (bounds_.top + bounds_.bottom - height) / 2);
    place(left, top);
    for (FieldYD& f : fields_) {
        if (!f.edit) continue;
        const float mid = (f.box.top + f.box.bottom) / 2;
        window_.placeEdit(f.edit, D2D1::RectF(f.box.left + 14, mid - 11, f.box.right - 14, mid + 11));
    }
}

void DialogYD::paint(CanvasYD& c) {
    beginHits();
    const SessionYD& session = window_.session();
    const D2D1_COLOR_F accent =
        session.loggedIn() ? ThemeYD::accent(session.current()->type()) : ThemeYD::accent(ServiceTypeYD::QQ);
    c.fillRect(bounds_, ThemeYD::rgb(0x0F1115, 0.32f));
    c.shadow(card_, 18, 24, 0.16f);
    c.fillRound(card_, 18, ThemeYD::rgb(ThemeYD::PANEL));
    const float x0 = card_.left + 28, x1 = card_.right - 28;
    c.text(title_, D2D1::RectF(x0, card_.top + 26, x1, card_.top + 54), 18, ThemeYD::rgb(ThemeYD::TEXT), AlignYD::LEFT,
           DWRITE_FONT_WEIGHT_SEMI_BOLD);

    for (const PartYD& part : parts_) {
        switch (part.kind) {
        case 0: {
            const D2D1_RECT_F& r = messageBoxes_[part.index];
            auto layout = c.wrapLayout(messages_[part.index], 14, r.right - r.left);
            c.drawLayout(layout.Get(), r.left, r.top, ThemeYD::rgb(ThemeYD::TEXT2));
            break;
        }
        case 1: {
            const FieldYD& f = fields_[part.index];
            c.text(f.label, D2D1::RectF(f.box.left + 2, f.box.top - 22, f.box.right, f.box.top - 4), 13, ThemeYD::rgb(ThemeYD::TEXT2));
            c.fillRound(f.box, 10, ThemeYD::rgb(ThemeYD::FIELD));
            if (GetFocus() == f.edit) c.strokeRound(f.box, 10, accent, 1.5f);
            addHit(f.box, ACT_FIELD, static_cast<int>(part.index));
            break;
        }
        case 2: {
            const ChoiceYD& ch = choices_[part.index];
            c.text(ch.label, D2D1::RectF(ch.box.left + 2, ch.box.top - 22, ch.box.right, ch.box.top - 4), 13,
                   ThemeYD::rgb(ThemeYD::TEXT2));
            c.fillRound(ch.box, 10, ThemeYD::rgb(ThemeYD::FIELD));
            const float w = (ch.box.right - ch.box.left) / ch.options.size();
            for (size_t i = 0; i < ch.options.size(); ++i) {
                const D2D1_RECT_F seg = D2D1::RectF(ch.box.left + w * i, ch.box.top, ch.box.left + w * (i + 1), ch.box.bottom);
                const bool on = static_cast<int>(i) == ch.selected;
                if (on) c.fillRound(D2D1::RectF(seg.left + 3, seg.top + 3, seg.right - 3, seg.bottom - 3), 8, ThemeYD::rgb(ThemeYD::PANEL));
                c.text(ch.options[i], seg, 13.5f, on ? accent : ThemeYD::rgb(ThemeYD::TEXT2), AlignYD::CENTER,
                       on ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
                addHit(seg, ACT_CHOICE, static_cast<int>(part.index * 100 + i));
            }
            break;
        }
        case 3: {
            c.text(pickLabel_, D2D1::RectF(pickBox_.left + 2, pickBox_.top - 22, pickBox_.right, pickBox_.top - 4), 13,
                   ThemeYD::rgb(ThemeYD::TEXT2));
            c.strokeRound(pickBox_, 10, ThemeYD::rgb(ThemeYD::BORDER));
            if (pickItems_.empty()) {
                c.text(L"（没有可选的对象）", pickBox_, 13, ThemeYD::rgb(ThemeYD::TEXT4), AlignYD::CENTER);
                break;
            }
            const D2D1_RECT_F inner = D2D1::RectF(pickBox_.left + 1, pickBox_.top + 4, pickBox_.right - 1, pickBox_.bottom - 4);
            c.pushClip(inner);
            for (size_t i = 0; i < pickItems_.size(); ++i) {
                const float top = inner.top + i * kRowHeight - pickScroll_;
                if (top + kRowHeight < inner.top || top > inner.bottom) continue;
                const D2D1_RECT_F row = D2D1::RectF(inner.left + 4, top, inner.right - 4, top + kRowHeight);
                if (hovered(row)) c.fillRound(row, 8, ThemeYD::rgb(ThemeYD::HOVER));
                const float cy = top + kRowHeight / 2;
                const D2D1_RECT_F box = D2D1::RectF(row.left + 10, cy - 9, row.left + 28, cy + 9);
                if (pickChecked_[i]) {
                    c.fillRound(box, pickMultiple_ ? 5.0f : 9.0f, accent);
                    c.icon(kIconCheck, box, 11, ThemeYD::rgb(0xFFFFFF));
                } else {
                    c.strokeRound(box, pickMultiple_ ? 5.0f : 9.0f, ThemeYD::rgb(0xC9CDD4), 1.5f);
                }
                c.text(pickItems_[i], D2D1::RectF(row.left + 40, top, row.right - 8, top + kRowHeight), 13.5f,
                       ThemeYD::rgb(ThemeYD::TEXT));
                addHit(D2D1::RectF(row.left, (std::max)(row.top, inner.top), row.right, (std::min)(row.bottom, inner.bottom)),
                       ACT_PICK, static_cast<int>(i));
            }
            c.popClip();
            break;
        }
        case 4: {
            c.fillRound(linesBox_, 12, ThemeYD::rgb(ThemeYD::BG));
            const D2D1_RECT_F inner = D2D1::RectF(linesBox_.left + 12, linesBox_.top + 12, linesBox_.right - 12, linesBox_.bottom - 12);
            c.pushClip(inner);
            float y = inner.top - linesScroll_;
            for (const std::wstring& line : lines_) {
                auto layout = c.wrapLayout(line, 13.5f, inner.right - inner.left);
                const float h = CanvasYD::layoutSize(layout.Get()).height;
                if (y + h > inner.top && y < inner.bottom) c.drawLayout(layout.Get(), inner.left, y, ThemeYD::rgb(ThemeYD::TEXT));
                y += h + 10;
            }
            c.popClip();
            break;
        }
        }
    }
    if (!error_.empty()) c.text(error_, errorBox_, 13, ThemeYD::rgb(ThemeYD::DANGER));

    c.fillRound(cancelBox_, 10, ThemeYD::rgb(hovered(cancelBox_) ? 0xE5E6EB : ThemeYD::FIELD));
    c.text(cancelText_, cancelBox_, 14, ThemeYD::rgb(ThemeYD::TEXT2), AlignYD::CENTER);
    addHit(cancelBox_, ACT_CANCEL);
    if (!primaryText_.empty()) {
        const D2D1_COLOR_F color = danger_ ? ThemeYD::rgb(ThemeYD::DANGER) : accent;
        c.fillRound(okBox_, 10, hovered(okBox_) ? ThemeYD::darker(color, 0.08f) : color);
        c.text(primaryText_, okBox_, 14, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        addHit(okBox_, ACT_OK);
    }
}

// ======================== 操作 ========================

void DialogYD::onAction(int action, int index) {
    switch (action) {
    case ACT_CHOICE:
        choices_[index / 100].selected = index % 100;
        break;
    case ACT_PICK:
        if (!pickMultiple_) pickChecked_.assign(pickChecked_.size(), false);
        pickChecked_[index] = pickMultiple_ ? !pickChecked_[index] : true;
        break;
    case ACT_FIELD:
        GuiWindowYD::focus(fields_[index].edit);
        break;
    case ACT_OK:
        submit();
        return;
    case ACT_CANCEL:
        window_.closeDialog(this);
        return;
    }
    window_.invalidate();
}

void DialogYD::submit() {
    if (!onSubmit_ || onSubmit_(*this)) window_.closeDialog(this);
}

void DialogYD::accept() { submit(); }

void DialogYD::cancel() { window_.closeDialog(this); }

void DialogYD::onWheel(float x, float y, float delta) {
    if (inside(pickBox_, x, y)) {
        const float limit = (std::max)(0.0f, pickItems_.size() * kRowHeight - (pickBox_.bottom - pickBox_.top - 8));
        pickScroll_ = (std::min)((std::max)(pickScroll_ - delta * 40, 0.0f), limit);
    } else if (inside(linesBox_, x, y)) {
        const float limit = (std::max)(0.0f, linesContentHeight() - (linesBox_.bottom - linesBox_.top - 24));
        linesScroll_ = (std::min)((std::max)(linesScroll_ - delta * 40, 0.0f), limit);
    }
    window_.invalidate();
}

bool DialogYD::onEditKey(HWND edit, int key) {
    size_t index = 0;
    while (index < fields_.size() && fields_[index].edit != edit) ++index;
    if (index == fields_.size()) return false;
    if (key == VK_ESCAPE) {
        window_.closeDialog(this);
    } else if (key == VK_TAB || (key == VK_RETURN && index + 1 < fields_.size())) {
        GuiWindowYD::focus(fields_[(index + 1) % fields_.size()].edit);   // 回车、Tab 跳到下一项
    } else if (key == VK_RETURN) {
        submit();   // 最后一项按回车：提交
    }
    window_.invalidate();
    return true;
}
