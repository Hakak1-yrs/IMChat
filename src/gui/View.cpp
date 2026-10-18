#include "View.h"
#include "GuiWindow.h"

View::View(GuiWindow& window) : window_(window) {}

void View::layout(const D2D1_RECT_F& bounds) { bounds_ = bounds; }

void View::onClick(float x, float y) {
    int hit = hitAt(x, y);
    if (hit >= 0) onAction(hits_[hit].action, hits_[hit].index);
}

void View::onMouseMove(float x, float y) {
    mouseX_ = x;
    mouseY_ = y;
    int hit = hitAt(x, y);
    if (hit != hoverHit_) {   // 悬停的对象变了才重绘，避免鼠标一动就整窗重画
        hoverHit_ = hit;
        window_.invalidate();
    }
}

void View::onWheel(float, float, float) {}

bool View::onEditKey(HWND, int) { return false; }

bool View::onEditChange(HWND) { return false; }

bool View::overClickable() const { return hitAt(mouseX_, mouseY_) >= 0; }

void View::onAction(int, int) {}

void View::beginHits() {
    hits_.clear();
    hoverHit_ = -1;
}

void View::addHit(const D2D1_RECT_F& r, int action, int index) {
    hits_.push_back({ r, action, index });
    if (inside(r, mouseX_, mouseY_)) hoverHit_ = static_cast<int>(hits_.size()) - 1;
}

bool View::hovered(const D2D1_RECT_F& r) const { return inside(r, mouseX_, mouseY_); }

bool View::inside(const D2D1_RECT_F& r, float x, float y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

int View::hitAt(float x, float y) const {
    for (int i = static_cast<int>(hits_.size()) - 1; i >= 0; --i) {   // 后画的在上层，优先命中
        if (inside(hits_[i].rect, x, y)) return i;
    }
    return -1;
}
