#pragma once
#include <memory>
#include <vector>
#include "PageModel.h"
#include "Page.h"
#include "View.h"

// 功能页面板（主界面右侧）：把 Page 给出的内容画成统一的样式——
// 连续的资料行放在同一张白色圆角卡片里，带头像的行显示标签和按钮，聊天页显示气泡和底部输入框
class PagePanel : public View {
public:
    explicit PagePanel(GuiWindow& window);

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(Canvas& canvas) override;
    void onWheel(float x, float y, float delta) override;
    bool onEditKey(HWND edit, int key) override;
    bool onEditChange(HWND edit) override;

    void setPage(std::shared_ptr<Page> page);
    std::shared_ptr<Page> page() const;
    void clear();

private:
    enum Action { ACT_BUTTON = 1, ACT_FOCUS };
    void onAction(int action, int index) override;
    void placeEdits();
    void send();
    float measure(Canvas& c, const PageModel::Row& row, float width);
    float buttonWidth(Canvas& c, const PageModel::Button& b, bool compact);
    void drawButton(Canvas& c, const PageModel::Button& b, const D2D1_RECT_F& r, bool compact, const D2D1_RECT_F& clip);
    D2D1_RECT_F bodyArea() const;

    std::shared_ptr<Page> page_;
    PageModel model_;
    std::vector<PageModel::Button> buttons_;   // 上一次绘制时登记的按钮
    float scroll_ = 0;
    float contentHeight_ = 0;
    size_t lastRows_ = 0;
    bool composerShown_ = false;
    bool hasText_ = false;
    HWND composer_ = nullptr;
};
