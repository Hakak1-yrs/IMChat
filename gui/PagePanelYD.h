#pragma once
#include <memory>
#include <vector>
#include "PageModelYD.h"
#include "PageYD.h"
#include "ViewYD.h"

// 功能页面板（主界面右侧）：把 PageYD 给出的内容画成统一的样式——
// 连续的资料行放在同一张白色圆角卡片里，带头像的行显示标签和按钮，聊天页显示气泡和底部输入框
class PagePanelYD : public ViewYD {
public:
    explicit PagePanelYD(GuiWindowYD& window);

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(CanvasYD& canvas) override;
    void onWheel(float x, float y, float delta) override;
    bool onEditKey(HWND edit, int key) override;
    bool onEditChange(HWND edit) override;

    void setPage(std::shared_ptr<PageYD> page);
    std::shared_ptr<PageYD> page() const;
    void clear();

private:
    enum ActionYD { ACT_BUTTON = 1, ACT_FOCUS };
    void onAction(int action, int index) override;
    void placeEdits();
    void send();
    float measure(CanvasYD& c, const PageModelYD::RowYD& row, float width);
    float buttonWidth(CanvasYD& c, const PageModelYD::ButtonYD& b, bool compact);
    void drawButton(CanvasYD& c, const PageModelYD::ButtonYD& b, const D2D1_RECT_F& r, bool compact, const D2D1_RECT_F& clip);
    D2D1_RECT_F bodyArea() const;

    std::shared_ptr<PageYD> page_;
    PageModelYD model_;
    std::vector<PageModelYD::ButtonYD> buttons_;   // 上一次绘制时登记的按钮
    float scroll_ = 0;
    float contentHeight_ = 0;
    size_t lastRows_ = 0;
    bool composerShown_ = false;
    bool hasText_ = false;
    HWND composer_ = nullptr;
};
