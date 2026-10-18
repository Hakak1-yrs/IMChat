#include "MainView.h"
#include <algorithm>
#include "AccountPage.h"
#include "Canvas.h"
#include "ChatRoomPage.h"
#include "CrossFriendPage.h"
#include "FriendPage.h"
#include "GroupPage.h"
#include "GroupSquarePage.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "LoginView.h"
#include "MomentsPage.h"
#include "NewFriendsPage.h"
#include "P2PPage.h"
#include "PlatformTools.h"
#include "Theme.h"
#include "Util.h"

namespace {

const float kRailWidth = 76.0f;
const float kListWidth = 300.0f;
const float kItemHeight = 68.0f;
const float kListHeaderHeight = 180.0f;   // 个人信息 + 搜索框 + 页签
const float kStripHeight = 46.0f;
const wchar_t kIconSearch = 0xE721;
const wchar_t kIconPower = 0xE7E8;
const wchar_t kIconAdd = 0xE710;
const wchar_t kIconConnect = 0xE703;
const wchar_t kIconHome = 0xE80F;
const wchar_t kIconSave = 0xE74E;
const wchar_t kIconChevron = 0xE76C;
const wchar_t kIconCamera = 0xE722;      // 朋友圈
const wchar_t kIconAddFriend = 0xE8FA;   // 新的朋友

}  // namespace

MainView::MainView(GuiWindow& window) : View(window), chat_(window), pages_(window) {}

void MainView::show() {
    search_ = window_.createEdit(false, false, L"搜索", Theme::FIELD);
    chat_.show();
    pages_.show();
    tab_ = TAB_CHATS;
    filter_.clear();
    listScroll_ = 0;
    leaving_ = false;
    status_.clear();
    window_.session().changed();   // 记下数据的当前状态，之后只在有变化时刷新
    reload();
    updateTitle();
}

void MainView::hide() {
    endNetworkSessions();
    window_.destroyEdit(search_);
    chat_.hide();
    pages_.hide();
    toast_.clear();
}

void MainView::layout(const D2D1_RECT_F& bounds) {
    View::layout(bounds);
    chat_.layout(chatArea());
    pages_.layout(chatArea());
    if (search_) {
        const D2D1_RECT_F l = listArea();
        const float mid = l.top + 106;
        window_.placeEdit(search_, D2D1::RectF(l.left + 50, mid - 11, l.right - 28, mid + 11));
    }
}

D2D1_RECT_F MainView::railArea() const {
    return D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + kRailWidth, bounds_.bottom);
}

D2D1_RECT_F MainView::listArea() const {
    return D2D1::RectF(bounds_.left + kRailWidth, bounds_.top, bounds_.left + kRailWidth + kListWidth, bounds_.bottom);
}

D2D1_RECT_F MainView::listBody() const {
    const D2D1_RECT_F l = listArea();
    return D2D1::RectF(l.left, l.top + kListHeaderHeight, l.right, l.bottom);
}

D2D1_RECT_F MainView::chatArea() const {
    return D2D1::RectF(bounds_.left + kRailWidth + kListWidth, bounds_.top, bounds_.right, bounds_.bottom);
}

float MainView::stripHeight() const {
    if (tab_ == TAB_CHATS) return 0.0f;
    if (tab_ == TAB_CONTACTS) return kStripHeight + kItemHeight;   // 按钮条 + “新的朋友”
    return window_.session().isRemote() ? 0.0f : kStripHeight;     // 建群、全部群暂时只在单机模式下有
}

bool MainView::showingPage() const { return pages_.page() != nullptr; }

View& MainView::right() { return showingPage() ? static_cast<View&>(pages_) : static_cast<View&>(chat_); }

const View& MainView::right() const {
    return showingPage() ? static_cast<const View&>(pages_) : static_cast<const View&>(chat_);
}

std::string MainView::selectedKey() const { return showingPage() ? pages_.page()->key() : chat_.openKey(); }

void MainView::updateTitle() {
    Session& session = window_.session();
    if (!session.loggedIn()) return;
    const AccountView me = session.me();
    window_.setTitle(L"IMChat · " + GuiText::wide(ServiceTypeHelper::name(me.type) + " " + me.nickname + "(" + me.id + ")") +
                     (session.isRemote() ? L" · 联网模式" : L""));
}

// ======================== 页面切换 ========================

void MainView::openChat(const Conversation& conversation) {
    window_.post([this, conversation] {
        pages_.clear();
        chat_.open(conversation);
        reload();
    });
}

void MainView::showPage(std::shared_ptr<Page> page) {
    window_.post([this, page] {
        chat_.close();
        pages_.setPage(page);
        window_.invalidate();
    });
}

void MainView::showAccount() { showPage(std::make_shared<AccountPage>(window_)); }

void MainView::showFriend(const std::string& peerId) { showPage(std::make_shared<FriendPage>(window_, peerId)); }

void MainView::showGroup(int groupNo) { showPage(std::make_shared<GroupPage>(window_, groupNo)); }

void MainView::showSquare() { showPage(std::make_shared<GroupSquarePage>(window_)); }

void MainView::showCrossFriends() { showPage(std::make_shared<CrossFriendPage>(window_)); }

void MainView::showP2P() {
    if (!p2p_) p2p_ = std::make_shared<P2PPage>(window_);
    showPage(p2p_);
}

void MainView::showRoom(int groupNo) {
    if (room_ && room_->groupNo() != groupNo && room_->active()) {   // 同一时间只进行一个群聊室
        toast(L"群 " + std::to_wstring(room_->groupNo()) + L" 的群聊室正在进行，请先结束");
        showPage(room_);
        return;
    }
    if (!room_ || room_->groupNo() != groupNo) room_ = std::make_shared<ChatRoomPage>(window_, groupNo);
    showPage(room_);
}

void MainView::showNewFriends() { showPage(std::make_shared<NewFriendsPage>(window_)); }

void MainView::showMoments() {
    showPage(std::make_shared<MomentsPage>(window_));   // 打开时把互动消息记为已读
    momentNotices_ = 0;
}

bool MainView::roomActive(int groupNo) const { return room_ && room_->groupNo() == groupNo && room_->active(); }

void MainView::endNetworkSessions() {
    if (pages_.page() && (pages_.page() == p2p_ || pages_.page() == room_)) pages_.clear();
    p2p_.reset();
    room_.reset();
}

void MainView::switchService(ServiceType type) {
    window_.post([this, type] {
        Session& session = window_.session();
        if (!session.loggedIn() || session.me().type == type || !session.switchService(type)) return;
        chat_.close();
        pages_.clear();
        listScroll_ = 0;
        reload();
        updateTitle();
        toast(L"已切换到" + Theme::shortName(type));
    });
}

void MainView::afterServiceChanged() {
    window_.post([this] {
        chat_.close();
        pages_.clear();
        listScroll_ = 0;
        reload();
        updateTitle();
    });
}

// ======================== 数据 ========================

void MainView::reload() {
    Session& session = window_.session();
    if (!session.loggedIn()) return;
    chats_ = session.recentChats();
    contacts_ = session.contacts();
    groups_ = session.myGroups();
    railUnread_.clear();
    for (ServiceType type : session.services()) railUnread_.push_back(session.unreadOf(type));
    pendingRequests_ = session.pendingFriendRequests();
    momentNotices_ = session.momentNotices();
    window_.invalidate();
}

const std::vector<Conversation>& MainView::currentList() const {
    return tab_ == TAB_CHATS ? chats_ : tab_ == TAB_CONTACTS ? contacts_ : groups_;
}

std::vector<size_t> MainView::visibleItems() const {
    const std::vector<Conversation>& list = currentList();
    std::vector<size_t> result;
    for (size_t i = 0; i < list.size(); ++i) {
        const Conversation& c = list[i];
        if (filter_.empty() || Util::containsText(c.title, filter_) || Util::containsText(c.subtitle, filter_) ||
            Util::containsText(c.peerId, filter_) || (c.isGroup && std::to_string(c.groupNo).find(filter_) != std::string::npos))
            result.push_back(i);
    }
    return result;
}

void MainView::toast(const std::wstring& text) {
    toast_ = text;
    toastUntil_ = GetTickCount64() + 2600;
    window_.invalidate();
}

// ======================== 事件 ========================

void MainView::onTimer() {
    Session& session = window_.session();
    if (!session.loggedIn()) {
        // 联网模式下被挤下线：回到登录页并说明原因
        if (session.isRemote()) backToLogin(GuiText::wide(session.takeNotice()));
        return;
    }
    // 每 0.1 秒：点对点聊天、群聊室的网络收发
    bool changed = false;
    if (p2p_ && p2p_->poll()) changed = true;
    if (room_ && room_->poll()) changed = true;
    if (changed) window_.invalidate();
    // 联网模式每 0.1 秒处理一次服务端推来的数据；单机模式每 0.4 秒检查一次消息文件（其他窗口、控制台版发来的消息）
    if (!session.isRemote() && ++ticks_ % 4 != 0) return;
    const bool foreground = GetForegroundWindow() == window_.hwnd();
    const bool dataChanged = session.changed();
    if (!session.loggedIn()) {   // 刚处理的推送里有“被挤下线”：带着原因回到登录页
        backToLogin(GuiText::wide(session.takeNotice()));
        return;
    }
    if (dataChanged || (foreground && !foreground_)) {
        chat_.refresh();   // 正在看的会话：显示新消息并记为已读
        reload();
    }
    foreground_ = foreground;
    const std::string notice = session.takeNotice();   // 比如联网模式下撤回失败的原因
    if (!notice.empty()) toast(GuiText::wide(notice));
    const std::wstring status = GuiText::wide(session.status());
    if (status != status_) {   // 连接状态变化（断线、重连成功）
        status_ = status;
        window_.invalidate();
    }
    if (!toast_.empty() && GetTickCount64() >= toastUntil_) {
        toast_.clear();
        window_.invalidate();
    }
}

void MainView::backToLogin(const std::wstring& notice) {
    if (leaving_) return;
    leaving_ = true;
    window_.post([this, notice] {
        window_.session().logout();
        window_.showLogin();   // 先切换视图（主界面隐藏时还会用到会话），再结束联网会话
        window_.endRemote();
        if (!notice.empty()) window_.loginView().setNotice(notice);
    });
}

void MainView::onClick(float x, float y) {
    if (inside(chatArea(), x, y)) {
        right().onClick(x, y);
        return;
    }
    chat_.closeMenu();   // 点到左侧时收起聊天面板里弹出的消息菜单
    View::onClick(x, y);
}

void MainView::onRightClick(float x, float y) {
    if (inside(chatArea(), x, y)) right().onRightClick(x, y);
}

void MainView::onMouseMove(float x, float y) {
    View::onMouseMove(x, y);
    right().onMouseMove(x, y);
}

bool MainView::overClickable() const { return View::overClickable() || right().overClickable(); }

void MainView::onWheel(float x, float y, float delta) {
    if (!inside(listBody(), x, y)) {
        right().onWheel(x, y, delta);
        return;
    }
    const D2D1_RECT_F body = listBody();
    const float content = stripHeight() + visibleItems().size() * kItemHeight + 12;
    const float limit = (std::max)(0.0f, content - (body.bottom - body.top));
    listScroll_ = (std::min)((std::max)(listScroll_ - delta * 60, 0.0f), limit);
    window_.invalidate();
}

bool MainView::onEditKey(HWND edit, int key) {
    if (chat_.onEditKey(edit, key) || pages_.onEditKey(edit, key)) return true;
    if (edit != search_) return false;
    if (key == VK_ESCAPE) SetWindowTextW(search_, L"");
    if (key == VK_RETURN) {   // 回车打开第一个搜索结果
        std::vector<size_t> items = visibleItems();
        if (!items.empty()) onAction(ACT_ITEM, static_cast<int>(items.front()));
    }
    return true;
}

bool MainView::onEditChange(HWND edit) {
    if (chat_.onEditChange(edit) || pages_.onEditChange(edit)) return true;
    if (edit != search_) return false;
    filter_ = Util::trim(GuiText::utf8(GuiText::editText(search_)));
    listScroll_ = 0;
    window_.invalidate();
    return true;
}

void MainView::onAction(int action, int index) {
    switch (action) {
    case ACT_SERVICE:
        switchService(static_cast<ServiceType>(index));
        break;
    case ACT_LOGOUT:
        backToLogin(L"");
        return;
    case ACT_TAB:
        tab_ = static_cast<Tab>(index);
        listScroll_ = 0;
        break;
    case ACT_ITEM: {
        const Conversation& item = currentList()[index];
        if (tab_ == TAB_CONTACTS) showFriend(item.peerId);   // 联系人：先看资料，资料页可以发消息、改备注
        else openChat(item);
        break;
    }
    case ACT_FOCUS_SEARCH:
        SetFocus(search_);
        break;
    case ACT_PROFILE:
        showAccount();
        break;
    case ACT_NEW_FRIENDS:
        showNewFriends();
        break;
    case ACT_TOOL:
        if (index == TOOL_OPEN_SERVICE) PlatformTools::openServiceDialog(window_);
        if (index == TOOL_P2P) showP2P();
        if (index == TOOL_OVERVIEW) PlatformTools::showOverview(window_);
        if (index == TOOL_SAVE) PlatformTools::saveNow(window_);
        if (index == TOOL_MOMENTS) showMoments();
        break;
    case ACT_STRIP:
        if (index == STRIP_ADD_FRIEND) FriendPage::addFriendDialog(window_);
        if (index == STRIP_CROSS) showCrossFriends();
        if (index == STRIP_NEW_GROUP) GroupSquarePage::createGroupDialog(window_);
        if (index == STRIP_SQUARE) showSquare();
        break;
    }
    window_.invalidate();
}

// ======================== 绘制 ========================

void MainView::paint(Canvas& c) {
    beginHits();
    tooltip_.clear();
    if (!window_.session().loggedIn()) return;
    paintRail(c);
    paintList(c);
    right().paint(c);
    paintStatus(c);
    paintToast(c);
    if (!tooltip_.empty()) {   // 导航栏按钮的悬停提示，画在最上层
        const float w = c.textWidth(tooltip_, 12.5f) + 24;
        const float cy = (tooltipAnchor_.top + tooltipAnchor_.bottom) / 2;
        const D2D1_RECT_F tip = D2D1::RectF(tooltipAnchor_.right + 10, cy - 15, tooltipAnchor_.right + 10 + w, cy + 15);
        c.shadow(tip, 8, 6, 0.10f);
        c.fillRound(tip, 8, Theme::rgb(0x1D2129, 0.92f));
        c.text(tooltip_, tip, 12.5f, Theme::rgb(0xFFFFFF), Align::CENTER);
    }
}

void MainView::railButton(Canvas& c, const D2D1_RECT_F& r, wchar_t icon, const std::wstring& tip, int action, int index,
                            bool dot) {
    const bool hover = hovered(r);
    if (hover) {
        c.fillRound(r, 12, Theme::rgb(0xE1E4EA));
        tooltip_ = tip;
        tooltipAnchor_ = r;
    }
    c.icon(icon, r, 17, Theme::rgb(hover && action == ACT_LOGOUT ? Theme::DANGER : (hover ? Theme::TEXT : Theme::TEXT3)));
    if (dot) c.fillCircle(r.right - 9, r.top + 9, 4, Theme::rgb(0x23C343));
    addHit(r, action, index);
}

void MainView::paintRail(Canvas& c) {
    const D2D1_RECT_F r = railArea();
    Session& session = window_.session();
    const AccountView me = session.me();
    const bool local = !session.isRemote();
    const D2D1_COLOR_F accent = Theme::accent(me.type);
    const float cx = (r.left + r.right) / 2;
    c.fillRect(r, Theme::rgb(Theme::RAIL));

    const D2D1_RECT_F logo = D2D1::RectF(cx - 20, r.top + 22, cx + 20, r.top + 62);
    c.fillRound(logo, 12, accent);
    c.text(L"IM", logo, 15, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    float y = r.top + 88;
    const std::vector<ServiceType> services = session.services();
    for (size_t i = 0; i < services.size(); ++i) {
        const ServiceType type = services[i];
        const bool active = type == me.type;
        const D2D1_RECT_F button = D2D1::RectF(cx - 25, y, cx + 25, y + 48);
        if (active) c.fillRound(button, 15, Theme::accent(type));
        else if (hovered(button)) c.fillRound(button, 15, Theme::rgb(0xE1E4EA));
        c.text(Theme::shortName(type), button, 13, active ? Theme::rgb(0xFFFFFF) : Theme::rgb(Theme::TEXT2),
               Align::CENTER, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        if (!active && i < railUnread_.size() && railUnread_[i] > 0) {   // 其他服务有未读消息：名称右上角的小红点
            const float labelHalf = c.textWidth(Theme::shortName(type), 13) / 2;
            c.fillCircle(cx + labelHalf + 4, (button.top + button.bottom) / 2 - 8, 4, Theme::rgb(Theme::DANGER));
        }
        if (hovered(button)) {
            tooltip_ = active ? Theme::shortName(type) + L"（当前）" : L"切换到" + Theme::shortName(type);
            tooltipAnchor_ = button;
        }
        addHit(button, ACT_SERVICE, static_cast<int>(type));
        y += 58;
    }
    if (local && services.size() < ServiceTypeHelper::all().size())   // 还有没开通的服务
        railButton(c, D2D1::RectF(cx - 22, y + 2, cx + 22, y + 46), kIconAdd, L"开通新服务", ACT_TOOL, TOOL_OPEN_SERVICE, false);

    // 底部工具：朋友圈、点对点聊天、平台概况、立即保存（这几项直接操作本地数据，只在单机模式下有），退出登录
    if (local) {
        const D2D1_RECT_F moments = D2D1::RectF(cx - 22, r.bottom - 274, cx + 22, r.bottom - 230);
        railButton(c, moments, kIconCamera, GuiText::wide(me.momentsName), ACT_TOOL, TOOL_MOMENTS, false);
        if (momentNotices_ > 0) c.fillCircle(moments.right - 9, moments.top + 9, 4.5f, Theme::rgb(Theme::DANGER));   // 有新互动
        const bool p2pActive = p2p_ && p2p_->active();
        railButton(c, D2D1::RectF(cx - 22, r.bottom - 222, cx + 22, r.bottom - 178), kIconConnect, L"QQ点对点聊天（TCP）", ACT_TOOL,
                   TOOL_P2P, p2pActive);
        railButton(c, D2D1::RectF(cx - 22, r.bottom - 170, cx + 22, r.bottom - 126), kIconHome, L"平台概况", ACT_TOOL, TOOL_OVERVIEW, false);
        railButton(c, D2D1::RectF(cx - 22, r.bottom - 118, cx + 22, r.bottom - 74), kIconSave, L"立即保存数据", ACT_TOOL, TOOL_SAVE, false);
    }
    railButton(c, D2D1::RectF(cx - 22, r.bottom - 66, cx + 22, r.bottom - 22), kIconPower, L"退出登录", ACT_LOGOUT, 0, false);
}

void MainView::paintList(Canvas& c) {
    const D2D1_RECT_F r = listArea();
    Session& session = window_.session();
    const AccountView me = session.me();
    const bool local = !session.isRemote();
    const D2D1_COLOR_F accent = Theme::accent(me.type);
    c.fillRect(r, Theme::rgb(Theme::PANEL));
    c.line(r.right - 0.5f, r.top, r.right - 0.5f, r.bottom, Theme::rgb(Theme::BORDER));

    // 个人信息（单机模式下点击进入个人中心）
    const D2D1_RECT_F profile = D2D1::RectF(r.left + 8, r.top + 14, r.right - 8, r.top + 74);
    if (local) {
        if (hovered(profile) || selectedKey() == "account") c.fillRound(profile, 12, Theme::rgb(Theme::HOVER));
        addHit(profile, ACT_PROFILE);
    }
    const float ax = r.left + 42, ay = r.top + 44;
    c.fillCircle(ax, ay, 22, Theme::avatar(me.id));
    c.text(GuiText::initial(me.nickname), D2D1::RectF(ax - 22, ay - 22, ax + 22, ay + 22), 17, Theme::rgb(0xFFFFFF),
           Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    const bool connected = status_.empty();   // 联网模式断线时显示灰色
    c.fillCircle(ax + 16, ay + 16, 6, Theme::rgb(Theme::PANEL));   // 在线状态
    c.fillCircle(ax + 16, ay + 16, 4, Theme::rgb(connected ? 0x23C343 : 0xC2C7D0));
    c.text(GuiText::wide(me.nickname), D2D1::RectF(r.left + 76, r.top + 24, r.right - 40, r.top + 46), 15.5f,
           Theme::rgb(Theme::TEXT), Align::LEFT, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    std::wstring second = Theme::shortName(me.type) + L" " + GuiText::wide(me.id + " · " + me.location);
    if (RemoteSession* remote = window_.remoteSession()) second = Theme::shortName(me.type) + L" " + GuiText::wide(me.id) +
                                                                  (connected ? L" · 已连接 " : L" · 连接中 ") + GuiText::wide(remote->server());
    c.text(second, D2D1::RectF(r.left + 76, r.top + 46, r.right - (local ? 40 : 16), r.top + 64), 12, Theme::rgb(Theme::TEXT3));
    if (local) c.icon(kIconChevron, D2D1::RectF(r.right - 40, r.top + 30, r.right - 16, r.top + 58), 11, Theme::rgb(Theme::TEXT4));

    // 搜索框（输入框控件放在里面）
    const D2D1_RECT_F search = D2D1::RectF(r.left + 16, r.top + 88, r.right - 16, r.top + 124);
    c.fillRound(search, 10, Theme::rgb(Theme::FIELD));
    if (GetFocus() == search_) c.strokeRound(search, 10, accent, 1.2f);
    c.icon(kIconSearch, D2D1::RectF(search.left + 6, search.top, search.left + 34, search.bottom), 13, Theme::rgb(Theme::TEXT3));
    addHit(search, ACT_FOCUS_SEARCH);

    // 页签
    const wchar_t* tabs[] = { L"消息", L"联系人", L"群聊" };
    const float tabWidth = (r.right - r.left - 32) / 3;
    int totalUnread = 0;
    for (const Conversation& item : chats_) totalUnread += item.unread;
    for (int i = 0; i < 3; ++i) {
        const D2D1_RECT_F t = D2D1::RectF(r.left + 16 + tabWidth * i, r.top + 136, r.left + 16 + tabWidth * (i + 1), r.top + 174);
        const bool active = tab_ == i;
        const float w = c.textWidth(tabs[i], 14, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        const float mid = (t.left + t.right) / 2;
        c.text(tabs[i], t, 14, Theme::rgb(active ? Theme::TEXT : (hovered(t) ? Theme::TEXT2 : Theme::TEXT3)),
               Align::CENTER, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        if (active) c.fillRound(D2D1::RectF(mid - 12, t.bottom - 4, mid + 12, t.bottom - 1), 1.5f, accent);
        if (i == TAB_CHATS && totalUnread > 0) {
            const std::wstring n = totalUnread > 99 ? L"99+" : std::to_wstring(totalUnread);
            const float bw = (std::max)(16.0f, c.textWidth(n, 10, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 9);
            const D2D1_RECT_F badge = D2D1::RectF(mid + w / 2 + 3, t.top + 4, mid + w / 2 + 3 + bw, t.top + 20);
            c.fillRound(badge, 8, Theme::rgb(Theme::DANGER));
            c.text(n, badge, 10, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        }
        addHit(t, ACT_TAB, i);
    }

    // 列表
    const D2D1_RECT_F body = listBody();
    c.pushClip(body);
    float y = body.top + 4 - listScroll_;
    if (tab_ == TAB_CONTACTS || (tab_ == TAB_GROUPS && local)) {   // 操作按钮条：添加好友 / 跨服务推荐；新建群 / 全部群
        const std::wstring labels[2] = { tab_ == TAB_CONTACTS ? L"＋ 添加好友" : L"＋ 新建群",
                                         tab_ == TAB_CONTACTS ? L"跨服务推荐" : L"全部群" };
        const int ids[2] = { tab_ == TAB_CONTACTS ? STRIP_ADD_FRIEND : STRIP_NEW_GROUP, tab_ == TAB_CONTACTS ? STRIP_CROSS : STRIP_SQUARE };
        const int count = local ? 2 : 1;   // 跨服务推荐要用到本人的多个服务账号，联网模式暂不提供
        const float width = local ? (body.right - body.left - 32 - 8) / 2 : body.right - body.left - 32;
        for (int i = 0; i < count; ++i) {
            const D2D1_RECT_F b = D2D1::RectF(body.left + 16 + (width + 8) * i, y + 2, body.left + 16 + (width + 8) * i + width, y + 34);
            D2D1_COLOR_F tint = accent;
            tint.a = hovered(b) ? 0.16f : 0.09f;
            c.fillRound(b, 9, tint);
            c.text(labels[i], b, 13, accent, Align::CENTER);
            if (b.bottom > body.top) addHit(D2D1::RectF(b.left, (std::max)(b.top, body.top), b.right, b.bottom), ACT_STRIP, ids[i]);
        }
        y += kStripHeight;
        if (tab_ == TAB_CONTACTS) {   // 与微信通讯录相同：最上面是“新的朋友”
            const D2D1_RECT_F item = D2D1::RectF(body.left + 8, y, body.right - 8, y + kItemHeight);
            if (selectedKey() == "newfriends") {
                D2D1_COLOR_F tint = accent;
                tint.a = 0.10f;
                c.fillRound(item, 12, tint);
            } else if (hovered(item)) {
                c.fillRound(item, 12, Theme::rgb(Theme::HOVER));
            }
            const float cy = (item.top + item.bottom) / 2;
            const D2D1_RECT_F icon = D2D1::RectF(item.left + 12, cy - 22, item.left + 56, cy + 22);
            c.fillRound(icon, 14, Theme::rgb(0xFF9A2E));
            c.icon(kIconAddFriend, icon, 18, Theme::rgb(0xFFFFFF));
            c.text(L"新的朋友", D2D1::RectF(item.left + 68, item.top + 13, item.right - 50, item.top + 35), 14.5f, Theme::rgb(Theme::TEXT));
            c.text(pendingRequests_ ? L"有新的好友申请" : L"好友申请、好友验证、黑名单",
                   D2D1::RectF(item.left + 68, item.top + 37, item.right - 50, item.top + 57), 12.5f, Theme::rgb(Theme::TEXT3));
            if (pendingRequests_ > 0) {
                const std::wstring n = std::to_wstring(pendingRequests_);
                const float bw = (std::max)(18.0f, c.textWidth(n, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 10);
                const D2D1_RECT_F badge = D2D1::RectF(item.right - 14 - bw, cy - 9, item.right - 14, cy + 9);
                c.fillRound(badge, 9, Theme::rgb(Theme::DANGER));
                c.text(n, badge, 11, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            }
            if (item.bottom > body.top)
                addHit(D2D1::RectF(item.left, (std::max)(item.top, body.top), item.right, (std::min)(item.bottom, body.bottom)), ACT_NEW_FRIENDS);
            y += kItemHeight;
        }
    }
    const std::vector<Conversation>& list = currentList();
    std::vector<size_t> items = visibleItems();
    if (items.empty()) {
        const wchar_t* empty = !filter_.empty()         ? L"没有找到匹配的结果"
                               : tab_ == TAB_CHATS    ? L"暂无消息"
                               : tab_ == TAB_CONTACTS ? L"还没有好友"
                                                      : L"还没有加入群聊";
        c.text(empty, D2D1::RectF(body.left, y + 36, body.right, y + 66), 13, Theme::rgb(Theme::TEXT4), Align::CENTER);
    }
    for (size_t index : items) {
        const D2D1_RECT_F item = D2D1::RectF(body.left + 8, y, body.right - 8, y + kItemHeight);
        if (item.bottom > body.top && item.top < body.bottom) {
            paintItem(c, list[index], item);
            addHit(D2D1::RectF(item.left, (std::max)(item.top, body.top), item.right, (std::min)(item.bottom, body.bottom)),
                   ACT_ITEM, static_cast<int>(index));
        }
        y += kItemHeight;
    }
    c.popClip();
}

void MainView::paintItem(Canvas& c, const Conversation& item, const D2D1_RECT_F& r) {
    const D2D1_COLOR_F accent = Theme::accent(window_.session().me().type);
    if (selectedKey() == item.key()) {
        D2D1_COLOR_F tint = accent;
        tint.a = 0.10f;
        c.fillRound(r, 12, tint);
    } else if (hovered(r)) {
        c.fillRound(r, 12, Theme::rgb(Theme::HOVER));
    }
    // 头像：好友用圆形，群用圆角方形，一眼就能区分
    const float cy = (r.top + r.bottom) / 2;
    const D2D1_RECT_F avatar = D2D1::RectF(r.left + 12, cy - 22, r.left + 56, cy + 22);
    if (item.isGroup) c.fillRound(avatar, 14, Theme::avatar("G" + std::to_string(item.groupNo)));
    else c.fillCircle(r.left + 34, cy, 22, Theme::avatar(item.peerId));
    c.text(GuiText::initial(item.avatarName), avatar, 17, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    const bool chats = tab_ == TAB_CHATS;
    const float timeWidth = chats && !item.lastTime.empty() ? 52.0f : 0.0f;
    c.text(GuiText::wide(item.title), D2D1::RectF(r.left + 68, r.top + 13, r.right - 14 - timeWidth, r.top + 35), 14.5f,
           Theme::rgb(Theme::TEXT));
    if (timeWidth > 0)
        c.text(GuiText::listTime(item.lastTime), D2D1::RectF(r.right - 14 - timeWidth, r.top + 13, r.right - 14, r.top + 35), 11.5f,
               Theme::rgb(Theme::TEXT4), Align::RIGHT);

    float badgeWidth = 0;
    if (item.unread > 0) {
        const std::wstring n = item.unread > 99 ? L"99+" : std::to_wstring(item.unread);
        badgeWidth = (std::max)(18.0f, c.textWidth(n, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 10);
        const D2D1_RECT_F badge = D2D1::RectF(r.right - 14 - badgeWidth, r.top + 38, r.right - 14, r.top + 56);
        c.fillRound(badge, 9, Theme::rgb(Theme::DANGER));
        c.text(n, badge, 11, Theme::rgb(0xFFFFFF), Align::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    }
    std::wstring second = GuiText::wide(chats ? item.preview : item.subtitle);
    D2D1_COLOR_F secondColor = Theme::rgb(Theme::TEXT3);
    if (chats && item.preview.empty()) {
        second = L"暂无消息";
        secondColor = Theme::rgb(Theme::TEXT4);
    }
    float x = r.left + 68;
    if (item.mentioned) {   // 与微信相同：红色的“[有人@我]”
        const std::wstring mark = L"[有人@我]";
        c.text(mark, D2D1::RectF(x, r.top + 37, r.right - 18 - badgeWidth, r.top + 57), 12.5f, Theme::rgb(Theme::DANGER));
        x += c.textWidth(mark, 12.5f) + 2;
    }
    c.text(second, D2D1::RectF(x, r.top + 37, r.right - 18 - badgeWidth, r.top + 57), 12.5f, secondColor);
}

void MainView::paintStatus(Canvas& c) {
    if (status_.empty()) return;   // 联网模式断线：聊天区顶部一条橙色提示，重连成功后自动消失
    const D2D1_RECT_F area = chatArea();
    const D2D1_RECT_F bar = D2D1::RectF(area.left, area.top + 64, area.right, area.top + 64 + 32);
    c.fillRect(bar, Theme::rgb(0xFFF3E8));
    c.line(bar.left, bar.bottom - 0.5f, bar.right, bar.bottom - 0.5f, Theme::rgb(0xFFD8B5));
    c.text(status_ + L"  发出的消息会在重连后自动补发", D2D1::RectF(bar.left + 24, bar.top, bar.right - 24, bar.bottom), 12.5f,
           Theme::rgb(0xD25F00));
}

void MainView::paintToast(Canvas& c) {
    if (toast_.empty()) return;
    const D2D1_RECT_F area = chatArea();
    const float w = (std::min)(c.textWidth(toast_, 13) + 40, area.right - area.left - 40);
    const float cx = (area.left + area.right) / 2;
    const D2D1_RECT_F pill = D2D1::RectF(cx - w / 2, area.top + 80, cx + w / 2, area.top + 116);
    c.shadow(pill, 18, 8, 0.10f);
    c.fillRound(pill, 18, Theme::rgb(0x1D2129, 0.90f));
    c.text(toast_, D2D1::RectF(pill.left + 16, pill.top, pill.right - 16, pill.bottom), 13, Theme::rgb(0xFFFFFF), Align::CENTER);
}
