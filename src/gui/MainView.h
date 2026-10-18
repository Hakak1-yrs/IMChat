#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ChatPanel.h"
#include "Conversation.h"
#include "PagePanel.h"
#include "ServiceType.h"
#include "View.h"

class P2PPage;
class ChatRoomPage;

// 主界面：最左侧是导航栏（服务切换、开通新服务、点对点聊天、平台概况、立即保存、退出登录），
// 中间是列表面板（个人信息、搜索、“消息 / 联系人 / 群聊”三个页签），
// 右侧显示聊天面板 ChatPanel 或功能页 PagePanel（个人中心、好友资料、群设置、全部群……）。
// 控制台版的每一个菜单项在这里都有对应的入口。定时器负责检查新消息和处理 TCP 网络收发
class MainView : public View {
public:
    explicit MainView(GuiWindow& window);

    void show() override;
    void hide() override;
    void layout(const D2D1_RECT_F& bounds) override;
    void paint(Canvas& canvas) override;
    void onTimer() override;
    void onClick(float x, float y) override;
    void onRightClick(float x, float y) override;
    void onMouseMove(float x, float y) override;
    void onWheel(float x, float y, float delta) override;
    bool onEditKey(HWND edit, int key) override;
    bool onEditChange(HWND edit) override;
    bool overClickable() const override;

    void reload();                              // 重新整理会话列表和未读数
    void toast(const std::wstring& text);       // 顶部短暂显示一条提示
    void updateTitle();

    // ---- 页面切换（推迟到消息循环中执行，调用者的对象在本次处理结束前不会被销毁）----
    void openChat(const Conversation& conversation);
    void showPage(std::shared_ptr<Page> page);
    void showAccount();
    void showFriend(const std::string& peerId);
    void showGroup(int groupNo);
    void showSquare();
    void showCrossFriends();
    void showP2P();
    void showRoom(int groupNo);
    void showNewFriends();
    void showMoments();
    void switchService(ServiceType type);
    void afterServiceChanged();                 // 当前服务被关闭后：清空右侧并刷新
    void endNetworkSessions();                  // 结束点对点聊天和群聊室（关闭服务、注销前调用）
    bool roomActive(int groupNo) const;

private:
    enum Action { ACT_SERVICE = 1, ACT_LOGOUT, ACT_TAB, ACT_ITEM, ACT_FOCUS_SEARCH, ACT_PROFILE, ACT_TOOL, ACT_STRIP, ACT_NEW_FRIENDS };
    enum Tool { TOOL_OPEN_SERVICE, TOOL_P2P, TOOL_OVERVIEW, TOOL_SAVE, TOOL_MOMENTS };
    enum Strip { STRIP_ADD_FRIEND, STRIP_CROSS, STRIP_NEW_GROUP, STRIP_SQUARE };
    enum Tab { TAB_CHATS, TAB_CONTACTS, TAB_GROUPS };
    void onAction(int action, int index) override;
    const std::vector<Conversation>& currentList() const;
    std::vector<size_t> visibleItems() const;   // 经过搜索筛选后的列表项下标
    D2D1_RECT_F railArea() const;
    D2D1_RECT_F listArea() const;
    D2D1_RECT_F listBody() const;               // 列表项所在的滚动区域
    D2D1_RECT_F chatArea() const;
    float stripHeight() const;                  // 联系人、群聊页签顶部的操作按钮条（联系人页还有“新的朋友”一行）
    bool showingPage() const;
    View& right();
    const View& right() const;
    std::string selectedKey() const;

    void paintRail(Canvas& c);
    void railButton(Canvas& c, const D2D1_RECT_F& r, wchar_t icon, const std::wstring& tip, int action, int index, bool dot);
    void paintList(Canvas& c);
    void paintItem(Canvas& c, const Conversation& item, const D2D1_RECT_F& r);
    void paintStatus(Canvas& c);
    void paintToast(Canvas& c);
    void backToLogin(const std::wstring& notice);   // 退出登录或被挤下线：回到登录页（联网模式同时断开服务器）

    ChatPanel chat_;
    PagePanel pages_;
    std::shared_ptr<P2PPage> p2p_;            // 网络页面长期保存：切换到别的页面时连接不会断开
    std::shared_ptr<ChatRoomPage> room_;
    HWND search_ = nullptr;
    Tab tab_ = TAB_CHATS;
    std::vector<Conversation> chats_, contacts_, groups_;
    std::vector<int> railUnread_;               // 导航栏上各服务的未读数
    int pendingRequests_ = 0;                   // 新的好友申请
    int momentNotices_ = 0;                     // 朋友圈新互动
    std::string filter_;
    float listScroll_ = 0;
    std::wstring toast_;
    ULONGLONG toastUntil_ = 0;
    std::wstring status_;                       // 连接状态（联网模式断线时），正常时为空
    bool leaving_ = false;                      // 已经安排回到登录页（避免定时器重复安排）
    bool foreground_ = false;                   // 窗口回到前台时，把正在看的会话记为已读
    int ticks_ = 0;
    std::wstring tooltip_;                      // 导航栏按钮的悬停提示
    D2D1_RECT_F tooltipAnchor_{};
};
