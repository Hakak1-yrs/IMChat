#pragma once
#include <dwrite.h>
#include <wrl/client.h>
#include <functional>
#include <string>
#include <vector>
#include "View.h"

// 对话框：浮在界面上方的白色卡片，背景变暗。控制台版中“输入几项内容再确认”的操作在图形版中都用它完成。
// 用法（链式调用）：
//   auto d = std::make_unique<Dialog>(window, L"修改昵称");
//   d->field(L"新昵称", L"小张").primary(L"保存", [](Dialog& dlg) { ...; return true; });
//   window.openDialog(std::move(d));
// 提交函数返回 true 关闭对话框；返回 false 时可以先 setError 显示原因，对话框保持打开
class Dialog : public View {
public:
    using Submit = std::function<bool(Dialog&)>;

    Dialog(GuiWindow& window, const std::wstring& title);

    // ---- 组成部分（按添加顺序从上到下排列）----
    Dialog& message(const std::wstring& text);
    Dialog& field(const std::wstring& label, const std::wstring& initial = L"", const std::wstring& placeholder = L"",
                    bool password = false);
    Dialog& choice(const std::wstring& label, const std::vector<std::wstring>& options, int selected = 0);
    Dialog& pick(const std::wstring& label, const std::vector<std::wstring>& items, bool multiple);   // 从列表中选人
    Dialog& lines(const std::vector<std::wstring>& lines);                                              // 只读的多行内容
    Dialog& primary(const std::wstring& text, Submit onSubmit, bool danger = false);
    Dialog& cancelText(const std::wstring& text);
    Dialog& onCancel(std::function<void()> callback);   // 点了取消（或按 Esc）时调用，比如拒绝邀请时通知对方

    // ---- 提交时读取 ----
    std::string text(size_t field) const;        // 输入框内容（UTF-8，已去掉首尾空白）
    int selected(size_t choice) const;
    std::vector<size_t> picked() const;          // 选中的列表项下标
    void setError(const std::wstring& error);
    bool owns(HWND edit) const;
    void accept();                               // 回车：有主按钮时提交，否则关闭
    void cancel();                               // Esc：关闭

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(Canvas& canvas) override;
    void onWheel(float x, float y, float delta) override;
    bool onEditKey(HWND edit, int key) override;

private:
    enum Action { ACT_CHOICE = 1, ACT_PICK, ACT_OK, ACT_CANCEL, ACT_FIELD };
    struct Field {
        std::wstring label, initial, placeholder;
        bool password;
        HWND edit;
        D2D1_RECT_F box;
    };
    struct Choice {
        std::wstring label;
        std::vector<std::wstring> options;
        int selected;
        D2D1_RECT_F box;
    };
    struct Part {   // 记录各部分的添加顺序
        int kind;     // 0 文字 1 输入框 2 选项 3 列表 4 多行内容
        size_t index;
    };

    void onAction(int action, int index) override;
    void submit();
    void computeLayout();
    float listHeight() const;
    float linesContentHeight();

    std::wstring title_;
    std::vector<std::wstring> messages_;
    std::vector<Field> fields_;
    std::vector<Choice> choices_;
    std::vector<Part> parts_;
    std::wstring pickLabel_;
    std::vector<std::wstring> pickItems_;
    std::vector<bool> pickChecked_;
    bool pickMultiple_ = false;
    float pickScroll_ = 0;
    D2D1_RECT_F pickBox_{};
    std::vector<std::wstring> lines_;
    float linesScroll_ = 0;
    D2D1_RECT_F linesBox_{};
    std::wstring primaryText_;
    Submit onSubmit_;
    bool danger_ = false;
    std::wstring cancelText_ = L"取消";
    std::function<void()> onCancel_;
    std::wstring error_;
    D2D1_RECT_F card_{};
    std::vector<D2D1_RECT_F> messageBoxes_;
    D2D1_RECT_F errorBox_{}, okBox_{}, cancelBox_{};
};
