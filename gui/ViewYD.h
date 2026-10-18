#pragma once
#include <windows.h>
#include <d2d1.h>
#include <vector>

class CanvasYD;
class GuiWindowYD;

// 界面视图抽象基类：登录页、主界面、聊天面板都从它派生。
// 窗口只通过这些虚函数把绘制、鼠标、键盘事件交给“当前视图”，不需要知道具体是哪一页（多态）。
//
// 可点击区域的处理方式：每次绘制时，视图把画出来的按钮、列表项等区域连同“动作编号”登记下来（addHit），
// 鼠标点击时找到对应区域，调用 onAction 处理。这样绘制和点击判断用的是同一个矩形，不会对不上
class ViewYD {
public:
    explicit ViewYD(GuiWindowYD& window);
    virtual ~ViewYD() = default;
    ViewYD(const ViewYD&) = delete;
    ViewYD& operator=(const ViewYD&) = delete;

    virtual void show() {}                                  // 切换到本视图：创建输入框等子控件
    virtual void hide() {}                                  // 离开本视图：销毁子控件
    virtual void layout(const D2D1_RECT_F& bounds);         // 视图占据的区域发生变化
    virtual void paint(CanvasYD& canvas) = 0;
    virtual void onTimer() {}
    virtual void onClick(float x, float y);
    virtual void onRightClick(float, float) {}             // 右键（聊天面板用来弹出消息菜单）
    virtual void onMouseMove(float x, float y);
    virtual void onWheel(float x, float y, float delta);
    virtual bool onEditKey(HWND edit, int key);             // 输入框中按下回车、Tab、Esc；返回 true 表示已处理
    virtual bool onEditChange(HWND edit);                   // 输入框内容变化
    virtual bool overClickable() const;                     // 鼠标是否停在可点击的区域（显示手形光标）

protected:
    virtual void onAction(int action, int index);
    void beginHits();                                       // 每次绘制开始时清空登记的区域
    void addHit(const D2D1_RECT_F& r, int action, int index = 0);
    bool hovered(const D2D1_RECT_F& r) const;
    static bool inside(const D2D1_RECT_F& r, float x, float y);

    GuiWindowYD& window_;
    D2D1_RECT_F bounds_{};
    float mouseX_ = -1, mouseY_ = -1;

private:
    struct HitYD {
        D2D1_RECT_F rect;
        int action;
        int index;
    };
    int hitAt(float x, float y) const;

    std::vector<HitYD> hits_;
    int hoverHit_ = -1;
};
