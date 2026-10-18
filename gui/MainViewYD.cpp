#include "MainViewYD.h"
#include <algorithm>
#include "AccountPageYD.h"
#include "CanvasYD.h"
#include "ChatRoomPageYD.h"
#include "CrossFriendPageYD.h"
#include "FriendPageYD.h"
#include "GroupPageYD.h"
#include "GroupSquarePageYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MomentsPageYD.h"
#include "NewFriendsPageYD.h"
#include "P2PPageYD.h"
#include "PlatformToolsYD.h"
#include "ThemeYD.h"
#include "UtilYD.h"

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

MainViewYD::MainViewYD(GuiWindowYD& window) : ViewYD(window), chat_(window), pages_(window) {}

void MainViewYD::show() {
    search_ = window_.createEdit(false, false, L"搜索", ThemeYD::FIELD);
    chat_.show();
    pages_.show();
    tab_ = TAB_CHATS;
    filter_.clear();
    listScroll_ = 0;
    window_.session().messagesChanged();   // 记下消息文件的当前状态，之后只在有变化时刷新
    reload();
    updateTitle();
}

void MainViewYD::hide() {
    endNetworkSessions();
    window_.destroyEdit(search_);
    chat_.hide();
    pages_.hide();
    toast_.clear();
}

void MainViewYD::layout(const D2D1_RECT_F& bounds) {
    ViewYD::layout(bounds);
    chat_.layout(chatArea());
    pages_.layout(chatArea());
    if (search_) {
        const D2D1_RECT_F l = listArea();
        const float mid = l.top + 106;
        window_.placeEdit(search_, D2D1::RectF(l.left + 50, mid - 11, l.right - 28, mid + 11));
    }
}

D2D1_RECT_F MainViewYD::railArea() const {
    return D2D1::RectF(bounds_.left, bounds_.top, bounds_.left + kRailWidth, bounds_.bottom);
}

D2D1_RECT_F MainViewYD::listArea() const {
    return D2D1::RectF(bounds_.left + kRailWidth, bounds_.top, bounds_.left + kRailWidth + kListWidth, bounds_.bottom);
}

D2D1_RECT_F MainViewYD::listBody() const {
    const D2D1_RECT_F l = listArea();
    return D2D1::RectF(l.left, l.top + kListHeaderHeight, l.right, l.bottom);
}

D2D1_RECT_F MainViewYD::chatArea() const {
    return D2D1::RectF(bounds_.left + kRailWidth + kListWidth, bounds_.top, bounds_.right, bounds_.bottom);
}

float MainViewYD::stripHeight() const {
    return tab_ == TAB_CHATS ? 0.0f : tab_ == TAB_CONTACTS ? kStripHeight + kItemHeight : kStripHeight;
}

bool MainViewYD::showingPage() const { return pages_.page() != nullptr; }

ViewYD& MainViewYD::right() { return showingPage() ? static_cast<ViewYD&>(pages_) : static_cast<ViewYD&>(chat_); }

const ViewYD& MainViewYD::right() const {
    return showingPage() ? static_cast<const ViewYD&>(pages_) : static_cast<const ViewYD&>(chat_);
}

std::string MainViewYD::selectedKey() const { return showingPage() ? pages_.page()->key() : chat_.openKey(); }

void MainViewYD::updateTitle() {
    ServiceBaseYD* me = window_.session().current();
    if (me) window_.setTitle(L"腾*立体社交 · " + GuiTextYD::wide(me->serviceName() + " " + me->displayName()));
}

// ======================== 页面切换 ========================

void MainViewYD::openChat(const ConversationYD& conversation) {
    window_.post([this, conversation] {
        pages_.clear();
        chat_.open(conversation);
        reload();
    });
}

void MainViewYD::showPage(std::shared_ptr<PageYD> page) {
    window_.post([this, page] {
        chat_.close();
        pages_.setPage(page);
        window_.invalidate();
    });
}

void MainViewYD::showAccount() { showPage(std::make_shared<AccountPageYD>(window_)); }

void MainViewYD::showFriend(const std::string& peerId) { showPage(std::make_shared<FriendPageYD>(window_, peerId)); }

void MainViewYD::showGroup(int groupNo) { showPage(std::make_shared<GroupPageYD>(window_, groupNo)); }

void MainViewYD::showSquare() { showPage(std::make_shared<GroupSquarePageYD>(window_)); }

void MainViewYD::showCrossFriends() { showPage(std::make_shared<CrossFriendPageYD>(window_)); }

void MainViewYD::showP2P() {
    if (!p2p_) p2p_ = std::make_shared<P2PPageYD>(window_);
    showPage(p2p_);
}

void MainViewYD::showRoom(int groupNo) {
    if (room_ && room_->groupNo() != groupNo && room_->active()) {   // 同一时间只进行一个群聊室
        toast(L"群 " + std::to_wstring(room_->groupNo()) + L" 的群聊室正在进行，请先结束");
        showPage(room_);
        return;
    }
    if (!room_ || room_->groupNo() != groupNo) room_ = std::make_shared<ChatRoomPageYD>(window_, groupNo);
    showPage(room_);
}

void MainViewYD::showNewFriends() { showPage(std::make_shared<NewFriendsPageYD>(window_)); }

void MainViewYD::showMoments() {
    showPage(std::make_shared<MomentsPageYD>(window_));   // 打开时把互动消息记为已读
    momentNotices_ = 0;
}

bool MainViewYD::roomActive(int groupNo) const { return room_ && room_->groupNo() == groupNo && room_->active(); }

void MainViewYD::endNetworkSessions() {
    if (pages_.page() && (pages_.page() == p2p_ || pages_.page() == room_)) pages_.clear();
    p2p_.reset();
    room_.reset();
}

void MainViewYD::switchService(ServiceTypeYD type) {
    window_.post([this, type] {
        SessionYD& session = window_.session();
        if (!session.loggedIn() || session.current()->type() == type || !session.switchService(type)) return;
        chat_.close();
        pages_.clear();
        listScroll_ = 0;
        reload();
        updateTitle();
        toast(L"已切换到" + ThemeYD::shortName(type));
    });
}

void MainViewYD::afterServiceChanged() {
    window_.post([this] {
        chat_.close();
        pages_.clear();
        listScroll_ = 0;
        reload();
        updateTitle();
    });
}

// ======================== 数据 ========================

void MainViewYD::reload() {
    SessionYD& session = window_.session();
    if (!session.loggedIn()) return;
    chats_ = session.recentChats();
    contacts_ = session.contacts();
    groups_ = session.myGroups();
    railUnread_.clear();
    for (ServiceBaseYD* s : session.user()->services()) railUnread_.push_back(session.unreadOf(*s));
    pendingRequests_ = session.platform().pendingRequestCount(*session.current());
    momentNotices_ = static_cast<int>(session.platform().momentNotices(*session.current(), true).size());
    window_.invalidate();
}

const std::vector<ConversationYD>& MainViewYD::currentList() const {
    return tab_ == TAB_CHATS ? chats_ : tab_ == TAB_CONTACTS ? contacts_ : groups_;
}

std::vector<size_t> MainViewYD::visibleItems() const {
    const std::vector<ConversationYD>& list = currentList();
    std::vector<size_t> result;
    for (size_t i = 0; i < list.size(); ++i) {
        const ConversationYD& c = list[i];
        if (filter_.empty() || UtilYD::containsText(c.title, filter_) || UtilYD::containsText(c.subtitle, filter_) ||
            UtilYD::containsText(c.peerId, filter_) || (c.isGroup && std::to_string(c.groupNo).find(filter_) != std::string::npos))
            result.push_back(i);
    }
    return result;
}

void MainViewYD::toast(const std::wstring& text) {
    toast_ = text;
    toastUntil_ = GetTickCount64() + 2600;
    window_.invalidate();
}

// ======================== 事件 ========================

void MainViewYD::onTimer() {
    SessionYD& session = window_.session();
    if (!session.loggedIn()) return;
    // 每 0.1 秒：点对点聊天、群聊室的网络收发
    bool changed = false;
    if (p2p_ && p2p_->poll()) changed = true;
    if (room_ && room_->poll()) changed = true;
    if (changed) window_.invalidate();
    if (++ticks_ % 4 != 0) return;
    // 每 0.4 秒：检查消息文件（其他窗口、控制台版发来的消息），以及提示条是否到时
    const bool foreground = GetForegroundWindow() == window_.hwnd();
    if (session.messagesChanged() || (foreground && !foreground_)) {
        session.platform().syncSocial();   // 其他窗口刚写入的好友申请结果、删除、拉黑
        chat_.refresh();                   // 正在看的会话：显示新消息并记为已读
        reload();
    }
    foreground_ = foreground;
    if (!toast_.empty() && GetTickCount64() >= toastUntil_) {
        toast_.clear();
        window_.invalidate();
    }
}

void MainViewYD::onClick(float x, float y) {
    if (inside(chatArea(), x, y)) {
        right().onClick(x, y);
        return;
    }
    chat_.closeMenu();   // 点到左侧时收起聊天面板里弹出的消息菜单
    ViewYD::onClick(x, y);
}

void MainViewYD::onRightClick(float x, float y) {
    if (inside(chatArea(), x, y)) right().onRightClick(x, y);
}

void MainViewYD::onMouseMove(float x, float y) {
    ViewYD::onMouseMove(x, y);
    right().onMouseMove(x, y);
}

bool MainViewYD::overClickable() const { return ViewYD::overClickable() || right().overClickable(); }

void MainViewYD::onWheel(float x, float y, float delta) {
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

bool MainViewYD::onEditKey(HWND edit, int key) {
    if (chat_.onEditKey(edit, key) || pages_.onEditKey(edit, key)) return true;
    if (edit != search_) return false;
    if (key == VK_ESCAPE) SetWindowTextW(search_, L"");
    if (key == VK_RETURN) {   // 回车打开第一个搜索结果
        std::vector<size_t> items = visibleItems();
        if (!items.empty()) onAction(ACT_ITEM, static_cast<int>(items.front()));
    }
    return true;
}

bool MainViewYD::onEditChange(HWND edit) {
    if (chat_.onEditChange(edit) || pages_.onEditChange(edit)) return true;
    if (edit != search_) return false;
    filter_ = UtilYD::trim(GuiTextYD::gbk(GuiTextYD::editText(search_)));
    listScroll_ = 0;
    window_.invalidate();
    return true;
}

void MainViewYD::onAction(int action, int index) {
    switch (action) {
    case ACT_SERVICE:
        switchService(static_cast<ServiceTypeYD>(index));
        break;
    case ACT_LOGOUT:
        window_.post([this] {
            window_.session().logout();
            window_.showLogin();
        });
        return;
    case ACT_TAB:
        tab_ = static_cast<TabYD>(index);
        listScroll_ = 0;
        break;
    case ACT_ITEM: {
        const ConversationYD& item = currentList()[index];
        if (tab_ == TAB_CONTACTS) showFriend(item.peerId);   // 联系人：先看资料，资料页可以发消息
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
        if (index == TOOL_OPEN_SERVICE) PlatformToolsYD::openServiceDialog(window_);
        if (index == TOOL_P2P) showP2P();
        if (index == TOOL_OVERVIEW) PlatformToolsYD::showOverview(window_);
        if (index == TOOL_SAVE) PlatformToolsYD::saveNow(window_);
        if (index == TOOL_MOMENTS) showMoments();
        break;
    case ACT_STRIP:
        if (index == STRIP_ADD_FRIEND) FriendPageYD::addFriendDialog(window_);
        if (index == STRIP_CROSS) showCrossFriends();
        if (index == STRIP_NEW_GROUP) GroupSquarePageYD::createGroupDialog(window_);
        if (index == STRIP_SQUARE) showSquare();
        break;
    }
    window_.invalidate();
}

// ======================== 绘制 ========================

void MainViewYD::paint(CanvasYD& c) {
    beginHits();
    tooltip_.clear();
    if (!window_.session().loggedIn()) return;
    paintRail(c);
    paintList(c);
    right().paint(c);
    paintToast(c);
    if (!tooltip_.empty()) {   // 导航栏按钮的悬停提示，画在最上层
        const float w = c.textWidth(tooltip_, 12.5f) + 24;
        const float cy = (tooltipAnchor_.top + tooltipAnchor_.bottom) / 2;
        const D2D1_RECT_F tip = D2D1::RectF(tooltipAnchor_.right + 10, cy - 15, tooltipAnchor_.right + 10 + w, cy + 15);
        c.shadow(tip, 8, 6, 0.10f);
        c.fillRound(tip, 8, ThemeYD::rgb(0x1D2129, 0.92f));
        c.text(tooltip_, tip, 12.5f, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER);
    }
}

void MainViewYD::railButton(CanvasYD& c, const D2D1_RECT_F& r, wchar_t icon, const std::wstring& tip, int action, int index,
                            bool dot) {
    const bool hover = hovered(r);
    if (hover) {
        c.fillRound(r, 12, ThemeYD::rgb(0xE1E4EA));
        tooltip_ = tip;
        tooltipAnchor_ = r;
    }
    c.icon(icon, r, 17, ThemeYD::rgb(hover && action == ACT_LOGOUT ? ThemeYD::DANGER : (hover ? ThemeYD::TEXT : ThemeYD::TEXT3)));
    if (dot) c.fillCircle(r.right - 9, r.top + 9, 4, ThemeYD::rgb(0x23C343));
    addHit(r, action, index);
}

void MainViewYD::paintRail(CanvasYD& c) {
    const D2D1_RECT_F r = railArea();
    SessionYD& session = window_.session();
    const D2D1_COLOR_F accent = ThemeYD::accent(session.current()->type());
    const float cx = (r.left + r.right) / 2;
    c.fillRect(r, ThemeYD::rgb(ThemeYD::RAIL));

    const D2D1_RECT_F logo = D2D1::RectF(cx - 20, r.top + 22, cx + 20, r.top + 62);
    c.fillRound(logo, 12, accent);
    c.text(L"腾", logo, 18, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    float y = r.top + 88;
    std::vector<ServiceBaseYD*> services = session.user()->services();
    for (size_t i = 0; i < services.size(); ++i) {
        ServiceBaseYD* s = services[i];
        const bool active = s == session.current();
        const D2D1_RECT_F button = D2D1::RectF(cx - 25, y, cx + 25, y + 48);
        if (active) c.fillRound(button, 15, ThemeYD::accent(s->type()));
        else if (hovered(button)) c.fillRound(button, 15, ThemeYD::rgb(0xE1E4EA));
        c.text(ThemeYD::shortName(s->type()), button, 13, active ? ThemeYD::rgb(0xFFFFFF) : ThemeYD::rgb(ThemeYD::TEXT2),
               AlignYD::CENTER, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        if (!active && i < railUnread_.size() && railUnread_[i] > 0) {   // 其他服务有未读消息：名称右上角的小红点
            const float labelHalf = c.textWidth(ThemeYD::shortName(s->type()), 13) / 2;
            c.fillCircle(cx + labelHalf + 4, (button.top + button.bottom) / 2 - 8, 4, ThemeYD::rgb(ThemeYD::DANGER));
        }
        if (hovered(button)) {
            tooltip_ = L"切换到" + ThemeYD::shortName(s->type()) + L"  " + GuiTextYD::wide(s->id());
            tooltipAnchor_ = button;
        }
        addHit(button, ACT_SERVICE, static_cast<int>(s->type()));
        y += 58;
    }
    if (services.size() < ServiceTypeHelperYD::all().size())   // 还有没开通的服务
        railButton(c, D2D1::RectF(cx - 22, y + 2, cx + 22, y + 46), kIconAdd, L"开通新服务", ACT_TOOL, TOOL_OPEN_SERVICE, false);

    // 底部工具：朋友圈、点对点聊天、平台概况、立即保存、退出登录
    const D2D1_RECT_F moments = D2D1::RectF(cx - 22, r.bottom - 274, cx + 22, r.bottom - 230);
    railButton(c, moments, kIconCamera, GuiTextYD::wide(session.current()->momentsName()), ACT_TOOL, TOOL_MOMENTS, false);
    if (momentNotices_ > 0) c.fillCircle(moments.right - 9, moments.top + 9, 4.5f, ThemeYD::rgb(ThemeYD::DANGER));   // 有新互动
    const bool p2pActive = p2p_ && p2p_->active();
    railButton(c, D2D1::RectF(cx - 22, r.bottom - 222, cx + 22, r.bottom - 178), kIconConnect, L"QQ点对点聊天（TCP）", ACT_TOOL,
               TOOL_P2P, p2pActive);
    railButton(c, D2D1::RectF(cx - 22, r.bottom - 170, cx + 22, r.bottom - 126), kIconHome, L"平台概况", ACT_TOOL, TOOL_OVERVIEW, false);
    railButton(c, D2D1::RectF(cx - 22, r.bottom - 118, cx + 22, r.bottom - 74), kIconSave, L"立即保存数据", ACT_TOOL, TOOL_SAVE, false);
    railButton(c, D2D1::RectF(cx - 22, r.bottom - 66, cx + 22, r.bottom - 22), kIconPower, L"退出登录", ACT_LOGOUT, 0, false);
}

void MainViewYD::paintList(CanvasYD& c) {
    const D2D1_RECT_F r = listArea();
    ServiceBaseYD* me = window_.session().current();
    const D2D1_COLOR_F accent = ThemeYD::accent(me->type());
    c.fillRect(r, ThemeYD::rgb(ThemeYD::PANEL));
    c.line(r.right - 0.5f, r.top, r.right - 0.5f, r.bottom, ThemeYD::rgb(ThemeYD::BORDER));

    // 个人信息（点击进入个人中心）
    const D2D1_RECT_F profile = D2D1::RectF(r.left + 8, r.top + 14, r.right - 8, r.top + 74);
    if (hovered(profile) || selectedKey() == "account") c.fillRound(profile, 12, ThemeYD::rgb(ThemeYD::HOVER));
    addHit(profile, ACT_PROFILE);
    const float ax = r.left + 42, ay = r.top + 44;
    c.fillCircle(ax, ay, 22, ThemeYD::avatar(me->id()));
    c.text(GuiTextYD::initial(me->nickname()), D2D1::RectF(ax - 22, ay - 22, ax + 22, ay + 22), 17, ThemeYD::rgb(0xFFFFFF),
           AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.fillCircle(ax + 16, ay + 16, 6, ThemeYD::rgb(ThemeYD::PANEL));   // 在线状态
    c.fillCircle(ax + 16, ay + 16, 4, ThemeYD::rgb(0x23C343));
    c.text(GuiTextYD::wide(me->nickname()), D2D1::RectF(r.left + 76, r.top + 24, r.right - 40, r.top + 46), 15.5f,
           ThemeYD::rgb(ThemeYD::TEXT), AlignYD::LEFT, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(ThemeYD::shortName(me->type()) + L" " + GuiTextYD::wide(me->id() + " · " + me->location()),
           D2D1::RectF(r.left + 76, r.top + 46, r.right - 40, r.top + 64), 12, ThemeYD::rgb(ThemeYD::TEXT3));
    c.icon(kIconChevron, D2D1::RectF(r.right - 40, r.top + 30, r.right - 16, r.top + 58), 11, ThemeYD::rgb(ThemeYD::TEXT4));

    // 搜索框（输入框控件放在里面）
    const D2D1_RECT_F search = D2D1::RectF(r.left + 16, r.top + 88, r.right - 16, r.top + 124);
    c.fillRound(search, 10, ThemeYD::rgb(ThemeYD::FIELD));
    if (GetFocus() == search_) c.strokeRound(search, 10, accent, 1.2f);
    c.icon(kIconSearch, D2D1::RectF(search.left + 6, search.top, search.left + 34, search.bottom), 13, ThemeYD::rgb(ThemeYD::TEXT3));
    addHit(search, ACT_FOCUS_SEARCH);

    // 页签
    const wchar_t* tabs[] = { L"消息", L"联系人", L"群聊" };
    const float tabWidth = (r.right - r.left - 32) / 3;
    int totalUnread = 0;
    for (const ConversationYD& item : chats_) totalUnread += item.unread;
    for (int i = 0; i < 3; ++i) {
        const D2D1_RECT_F t = D2D1::RectF(r.left + 16 + tabWidth * i, r.top + 136, r.left + 16 + tabWidth * (i + 1), r.top + 174);
        const bool active = tab_ == i;
        const float w = c.textWidth(tabs[i], 14, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        const float mid = (t.left + t.right) / 2;
        c.text(tabs[i], t, 14, ThemeYD::rgb(active ? ThemeYD::TEXT : (hovered(t) ? ThemeYD::TEXT2 : ThemeYD::TEXT3)),
               AlignYD::CENTER, active ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL);
        if (active) c.fillRound(D2D1::RectF(mid - 12, t.bottom - 4, mid + 12, t.bottom - 1), 1.5f, accent);
        if (i == TAB_CHATS && totalUnread > 0) {
            const std::wstring n = totalUnread > 99 ? L"99+" : std::to_wstring(totalUnread);
            const float bw = (std::max)(16.0f, c.textWidth(n, 10, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 9);
            const D2D1_RECT_F badge = D2D1::RectF(mid + w / 2 + 3, t.top + 4, mid + w / 2 + 3 + bw, t.top + 20);
            c.fillRound(badge, 8, ThemeYD::rgb(ThemeYD::DANGER));
            c.text(n, badge, 10, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        }
        addHit(t, ACT_TAB, i);
    }

    // 列表
    const D2D1_RECT_F body = listBody();
    c.pushClip(body);
    float y = body.top + 4 - listScroll_;
    if (tab_ != TAB_CHATS) {   // 操作按钮条：添加好友 / 跨服务推荐；新建群 / 全部群
        const std::wstring labels[2] = { tab_ == TAB_CONTACTS ? L"＋ 添加好友" : L"＋ 新建群",
                                         tab_ == TAB_CONTACTS ? L"跨服务推荐" : L"全部群" };
        const int ids[2] = { tab_ == TAB_CONTACTS ? STRIP_ADD_FRIEND : STRIP_NEW_GROUP, tab_ == TAB_CONTACTS ? STRIP_CROSS : STRIP_SQUARE };
        const float half = (body.right - body.left - 32 - 8) / 2;
        for (int i = 0; i < 2; ++i) {
            const D2D1_RECT_F b = D2D1::RectF(body.left + 16 + (half + 8) * i, y + 2, body.left + 16 + (half + 8) * i + half, y + 34);
            D2D1_COLOR_F tint = accent;
            tint.a = hovered(b) ? 0.16f : 0.09f;
            c.fillRound(b, 9, tint);
            c.text(labels[i], b, 13, accent, AlignYD::CENTER);
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
                c.fillRound(item, 12, ThemeYD::rgb(ThemeYD::HOVER));
            }
            const float cy = (item.top + item.bottom) / 2;
            const D2D1_RECT_F icon = D2D1::RectF(item.left + 12, cy - 22, item.left + 56, cy + 22);
            c.fillRound(icon, 14, ThemeYD::rgb(0xFF9A2E));
            c.icon(kIconAddFriend, icon, 18, ThemeYD::rgb(0xFFFFFF));
            c.text(L"新的朋友", D2D1::RectF(item.left + 68, item.top + 13, item.right - 50, item.top + 35), 14.5f, ThemeYD::rgb(ThemeYD::TEXT));
            c.text(pendingRequests_ ? L"有新的好友申请" : L"好友申请、好友验证、黑名单",
                   D2D1::RectF(item.left + 68, item.top + 37, item.right - 50, item.top + 57), 12.5f, ThemeYD::rgb(ThemeYD::TEXT3));
            if (pendingRequests_ > 0) {
                const std::wstring n = std::to_wstring(pendingRequests_);
                const float bw = (std::max)(18.0f, c.textWidth(n, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 10);
                const D2D1_RECT_F badge = D2D1::RectF(item.right - 14 - bw, cy - 9, item.right - 14, cy + 9);
                c.fillRound(badge, 9, ThemeYD::rgb(ThemeYD::DANGER));
                c.text(n, badge, 11, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            }
            if (item.bottom > body.top)
                addHit(D2D1::RectF(item.left, (std::max)(item.top, body.top), item.right, (std::min)(item.bottom, body.bottom)), ACT_NEW_FRIENDS);
            y += kItemHeight;
        }
    }
    const std::vector<ConversationYD>& list = currentList();
    std::vector<size_t> items = visibleItems();
    if (items.empty()) {
        const wchar_t* empty = !filter_.empty()         ? L"没有找到匹配的结果"
                               : tab_ == TAB_CHATS    ? L"暂无消息"
                               : tab_ == TAB_CONTACTS ? L"还没有好友"
                                                      : L"还没有加入群聊";
        c.text(empty, D2D1::RectF(body.left, y + 36, body.right, y + 66), 13, ThemeYD::rgb(ThemeYD::TEXT4), AlignYD::CENTER);
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

void MainViewYD::paintItem(CanvasYD& c, const ConversationYD& item, const D2D1_RECT_F& r) {
    const D2D1_COLOR_F accent = ThemeYD::accent(window_.session().current()->type());
    if (selectedKey() == item.key()) {
        D2D1_COLOR_F tint = accent;
        tint.a = 0.10f;
        c.fillRound(r, 12, tint);
    } else if (hovered(r)) {
        c.fillRound(r, 12, ThemeYD::rgb(ThemeYD::HOVER));
    }
    // 头像：好友用圆形，群用圆角方形，一眼就能区分
    const float cy = (r.top + r.bottom) / 2;
    const D2D1_RECT_F avatar = D2D1::RectF(r.left + 12, cy - 22, r.left + 56, cy + 22);
    if (item.isGroup) c.fillRound(avatar, 14, ThemeYD::avatar("G" + std::to_string(item.groupNo)));
    else c.fillCircle(r.left + 34, cy, 22, ThemeYD::avatar(item.peerId));
    c.text(GuiTextYD::initial(item.avatarName), avatar, 17, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    const bool chats = tab_ == TAB_CHATS;
    const float timeWidth = chats && !item.lastTime.empty() ? 52.0f : 0.0f;
    c.text(GuiTextYD::wide(item.title), D2D1::RectF(r.left + 68, r.top + 13, r.right - 14 - timeWidth, r.top + 35), 14.5f,
           ThemeYD::rgb(ThemeYD::TEXT));
    if (timeWidth > 0)
        c.text(GuiTextYD::listTime(item.lastTime), D2D1::RectF(r.right - 14 - timeWidth, r.top + 13, r.right - 14, r.top + 35), 11.5f,
               ThemeYD::rgb(ThemeYD::TEXT4), AlignYD::RIGHT);

    float badgeWidth = 0;
    if (item.unread > 0) {
        const std::wstring n = item.unread > 99 ? L"99+" : std::to_wstring(item.unread);
        badgeWidth = (std::max)(18.0f, c.textWidth(n, 11, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 10);
        const D2D1_RECT_F badge = D2D1::RectF(r.right - 14 - badgeWidth, r.top + 38, r.right - 14, r.top + 56);
        c.fillRound(badge, 9, ThemeYD::rgb(ThemeYD::DANGER));
        c.text(n, badge, 11, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    }
    std::wstring second = GuiTextYD::wide(chats ? item.preview : item.subtitle);
    D2D1_COLOR_F secondColor = ThemeYD::rgb(ThemeYD::TEXT3);
    if (chats && item.preview.empty()) {
        second = L"暂无消息";
        secondColor = ThemeYD::rgb(ThemeYD::TEXT4);
    }
    float x = r.left + 68;
    if (item.mentioned) {   // 与微信相同：红色的“[有人@我]”
        const std::wstring mark = L"[有人@我]";
        c.text(mark, D2D1::RectF(x, r.top + 37, r.right - 18 - badgeWidth, r.top + 57), 12.5f, ThemeYD::rgb(ThemeYD::DANGER));
        x += c.textWidth(mark, 12.5f) + 2;
    }
    c.text(second, D2D1::RectF(x, r.top + 37, r.right - 18 - badgeWidth, r.top + 57), 12.5f, secondColor);
}

void MainViewYD::paintToast(CanvasYD& c) {
    if (toast_.empty()) return;
    const D2D1_RECT_F area = chatArea();
    const float w = (std::min)(c.textWidth(toast_, 13) + 40, area.right - area.left - 40);
    const float cx = (area.left + area.right) / 2;
    const D2D1_RECT_F pill = D2D1::RectF(cx - w / 2, area.top + 80, cx + w / 2, area.top + 116);
    c.shadow(pill, 18, 8, 0.10f);
    c.fillRound(pill, 18, ThemeYD::rgb(0x1D2129, 0.90f));
    c.text(toast_, D2D1::RectF(pill.left + 16, pill.top, pill.right - 16, pill.bottom), 13, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER);
}
