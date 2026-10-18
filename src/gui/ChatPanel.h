#pragma once
#include <dwrite.h>
#include <wrl/client.h>
#include <map>
#include <string>
#include <vector>
#include "Conversation.h"
#include "Message.h"
#include "View.h"

// 聊天面板（主界面右侧）：标题栏、消息气泡、输入框，以及可展开的资料抽屉（好友资料 / 群成员与群公告）。
// 自己的消息在右侧、用当前服务的强调色；别人的消息在左侧、白色气泡；相隔 5 分钟以上的消息之间显示时间。
// 标题栏的放大镜按钮打开“聊天记录查询”，输入关键字即时筛选。
// 与微信电脑版相同，右键一条消息弹出菜单：复制、@TA（群聊）、撤回（自己 2 分钟内的消息，或群主、管理员撤回成员的消息）
class ChatPanel : public View {
public:
    explicit ChatPanel(GuiWindow& window);

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(Canvas& canvas) override;
    void onClick(float x, float y) override;
    void onRightClick(float x, float y) override;   // 右键消息：弹出消息菜单
    void onWheel(float x, float y, float delta) override;
    bool onEditKey(HWND edit, int key) override;
    bool onEditChange(HWND edit) override;

    void open(const Conversation& conversation);
    void close();
    void refresh();                 // 重新读取消息（消息文件有变化时由主界面调用）
    void closeMenu();               // 收起消息菜单
    bool isOpen() const;
    std::string openKey() const;

private:
    enum Action { ACT_SEND = 1, ACT_SEARCH, ACT_INFO, ACT_FOCUS_INPUT, ACT_SETTINGS, ACT_ROOM, ACT_REVOKE, ACT_MENTION,
                    ACT_COPY, ACT_AT_SENDER };
    struct MenuItem {
        std::wstring label;
        int action;
        bool danger;
    };
    enum RowKind { ROW_TIME, ROW_MINE, ROW_OTHER, ROW_NOTICE, ROW_SYSTEM };   // ROW_SYSTEM：系统提示、撤回提示
    struct Row {
        RowKind kind;
        size_t message;                                       // 在 messages_ 中的下标
        float top, height, bubbleWidth, bubbleHeight, nameHeight;
        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
        std::wstring label;                                   // 时间分隔的文字
    };

    void onAction(int action, int index) override;
    void send();
    void mention();                  // “@ 提醒”：选择群成员，把“@昵称 ”插入输入框
    const Message* findMessage(int seq) const;
    std::vector<MenuItem> menuItems(const Message& m) const;   // 这条消息的右键菜单里有哪些项
    D2D1_RECT_F inputField() const;  // 输入框所在的圆角区域
    int rowAt(float x, float y) const;
    void placeEdits();
    void rebuildRows(Canvas& c);
    bool matches(const Message& m) const;
    std::wstring senderName(const std::string& id) const;
    float contentRight() const;
    int iconCount() const;          // 标题栏右侧的图标按钮个数（群聊多一个“实时群聊室”）
    float iconsLeft() const;        // 最左边一个图标按钮的左边界
    D2D1_RECT_F headerArea() const;
    D2D1_RECT_F inputArea() const;
    D2D1_RECT_F messageArea() const;
    D2D1_RECT_F drawerArea() const;
    float maxScroll() const;

    void paintEmpty(Canvas& c);
    void paintHeader(Canvas& c);
    void paintMessages(Canvas& c);
    void paintAvatar(Canvas& c, float cx, float cy, float radius, const std::string& id);
    void paintInput(Canvas& c);
    void paintDrawer(Canvas& c);
    void paintMenu(Canvas& c);
    float paintTag(Canvas& c, float right, float cy, const std::wstring& text, const D2D1_COLOR_F& color);

    Conversation conversation_;
    bool open_ = false;
    std::vector<Message> messages_;
    int lastSeq_ = -1;
    std::vector<Row> rows_;
    bool rowsDirty_ = true;
    float rowsWidth_ = 0;
    float contentHeight_ = 0;
    float scroll_ = 0;
    bool stickBottom_ = true;      // 停在最底部时，新消息到来自动滚动显示
    int matchCount_ = 0;
    int menuSeq_ = -1;             // 弹出右键菜单的那条消息（-1 表示没有弹出）
    float menuX_ = 0, menuY_ = 0;  // 右键的位置
    size_t revokedCount_ = 0;

    bool canSend_ = true;
    std::string reason_;           // 不能发言的原因
    bool hasText_ = false;
    bool searching_ = false;
    std::string keyword_;
    bool drawer_ = false;
    float drawerScroll_ = 0;
    float drawerHeight_ = 0;
    std::map<std::string, std::wstring> drafts_;   // 每个会话没发出去的草稿

    HWND input_ = nullptr;
    HWND search_ = nullptr;
};
