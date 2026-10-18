#include "ChatPanelYD.h"
#include <algorithm>
#include "CanvasYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "ThemeYD.h"
#include "UtilYD.h"

namespace {

const float kHeaderHeight = 64.0f;
const float kInputHeight = 172.0f;
const float kBlockedHeight = 64.0f;   // 不能发言时，底部只显示一行原因
const float kDrawerWidth = 300.0f;
const std::string kNoticePrefix = "【群公告】";   // 发布群公告时自动发出的群消息以此开头，显示成公告卡片

// 图标字体中的图标编码
const wchar_t kIconSearch = 0xE721;
const wchar_t kIconInfo = 0xE946;
const wchar_t kIconChat = 0xE8BD;
const wchar_t kIconBlocked = 0xE72E;   // 锁
const wchar_t kIconSettings = 0xE713;  // 齿轮
const wchar_t kIconRoom = 0xE703;      // 连接（实时群聊室）

void copyToClipboard(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        memcpy(GlobalLock(memory), text.c_str(), bytes);
        GlobalUnlock(memory);
        if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);   // 成功后内存归剪贴板所有
    }
    CloseClipboard();
}

}  // namespace

ChatPanelYD::ChatPanelYD(GuiWindowYD& window) : ViewYD(window) {}

void ChatPanelYD::show() {
    input_ = window_.createEdit(true, false, L"", ThemeYD::FIELD);   // 底色与输入框区域一致
    search_ = window_.createEdit(false, false, L"搜索聊天记录", ThemeYD::FIELD);
    placeEdits();
}

void ChatPanelYD::hide() {
    window_.destroyEdit(input_);
    window_.destroyEdit(search_);
    open_ = false;
    drafts_.clear();
}

void ChatPanelYD::layout(const D2D1_RECT_F& bounds) {
    ViewYD::layout(bounds);
    placeEdits();
}

bool ChatPanelYD::isOpen() const { return open_; }

std::string ChatPanelYD::openKey() const { return open_ ? conversation_.key() : ""; }

// ======================== 打开、刷新、发送 ========================

void ChatPanelYD::open(const ConversationYD& conversation) {
    if (open_ && input_) drafts_[conversation_.key()] = GuiTextYD::editText(input_);   // 保存上一个会话的草稿
    conversation_ = conversation;
    open_ = true;
    messages_.clear();
    lastSeq_ = -1;
    revokedCount_ = 0;
    menuSeq_ = -1;
    rows_.clear();
    scroll_ = 0;
    stickBottom_ = true;
    searching_ = false;
    keyword_.clear();
    drawerScroll_ = 0;
    if (search_) SetWindowTextW(search_, L"");
    const std::wstring draft = drafts_[conversation_.key()];
    if (input_) SetWindowTextW(input_, draft.c_str());
    hasText_ = !draft.empty();
    refresh();
    if (canSend_ && input_) {
        SetFocus(input_);
        SendMessageW(input_, EM_SETSEL, draft.size(), draft.size());
    }
}

void ChatPanelYD::close() {
    open_ = false;
    drawer_ = false;
    searching_ = false;
    placeEdits();
    window_.invalidate();
}

void ChatPanelYD::refresh() {
    if (!open_) return;
    SessionYD& session = window_.session();
    std::vector<MessageYD> list = session.messages(conversation_);
    const int last = list.empty() ? 0 : list.back().seq();
    size_t revoked = 0;   // 撤回不会增加消息条数，单独统计，撤回后也能及时刷新
    for (const MessageYD& m : list) revoked += m.revoked() ? 1 : 0;
    if (last != lastSeq_ || list.size() != messages_.size() || revoked != revokedCount_) {
        messages_ = std::move(list);
        lastSeq_ = last;
        revokedCount_ = revoked;
        rowsDirty_ = true;
        const MessageYD* m = menuSeq_ >= 0 ? findMessage(menuSeq_) : nullptr;
        if (!m || m->revoked()) menuSeq_ = -1;   // 菜单对应的消息已被撤回（可能是在别的窗口撤回的）
    }
    canSend_ = session.canSend(conversation_, reason_);
    // 窗口在前台时才算“看过了”，在后台收到的消息仍然计为未读
    if (GetForegroundWindow() == window_.hwnd() && !IsIconic(window_.hwnd())) session.markRead(conversation_, lastSeq_);
    placeEdits();
    window_.invalidate();
}

void ChatPanelYD::send() {
    const std::string text = UtilYD::lineText(GuiTextYD::utf8(GuiTextYD::editText(input_)));
    if (text.empty()) return;
    std::string msg;
    if (!window_.session().send(conversation_, text, msg)) {
        window_.mainView().toast(GuiTextYD::wide(msg));
        return;
    }
    SetWindowTextW(input_, L"");
    hasText_ = false;
    stickBottom_ = true;
    refresh();
    window_.mainView().reload();   // 会话列表的最后一条消息、排序随之更新
    SetFocus(input_);
}

void ChatPanelYD::mention() {
    GroupYD* g = window_.session().groupOf(conversation_);
    if (!g || !input_) return;
    PlatformYD& platform = window_.session().platform();
    const std::string me = window_.session().current()->id();
    std::vector<std::string> names;
    std::vector<std::wstring> items;
    if (g->policy().canEditInfo(*g, me)) {   // 群主（QQ 群模式下还有管理员）可以 @所有人
        names.push_back("所有人");
        items.push_back(L"所有人");
    }
    for (const std::string& id : g->members()) {
        if (id == me) continue;
        ServiceBaseYD* acc = platform.findAccount(g->service(), id);
        names.push_back(acc ? acc->nickname() : id);
        items.push_back(GuiTextYD::wide(platform.displayName(g->service(), id)));
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"选择要提醒的人");
    dialog->pick(L"群成员（可多选）", items, true).primary(L"插入", [this, names](DialogYD& d) {
        std::wstring text;
        for (size_t i : d.picked()) text += L"@" + GuiTextYD::wide(names[i]) + L" ";
        if (text.empty()) {
            d.setError(L"请至少选择一个人。");
            return false;
        }
        // 等对话框关闭、输入框重新显示之后再插入文字并放回焦点
        HWND input = input_;
        window_.post([this, input, text] {
            window_.post([input, text] {
                SendMessageW(input, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
                SetFocus(input);
            });
        });
        return true;
    });
    window_.openDialog(std::move(dialog));
}

// ======================== 输入与鼠标 ========================

void ChatPanelYD::onAction(int action, int index) {
    switch (action) {
    case ACT_SEND: send(); break;
    case ACT_FOCUS_INPUT: SetFocus(input_); break;
    case ACT_SETTINGS:   // 群聊 → 群设置；私信 → 好友资料
        if (conversation_.isGroup) window_.mainView().showGroup(conversation_.groupNo);
        else window_.mainView().showFriend(conversation_.peerId);
        return;
    case ACT_ROOM:
        window_.mainView().showRoom(conversation_.groupNo);
        return;
    case ACT_MENTION:
        mention();
        return;
    case ACT_REVOKE: {
        std::string msg;
        if (!window_.session().platform().revokeMessage(*window_.session().current(), index, msg))
            window_.mainView().toast(GuiTextYD::wide(msg));
        refresh();
        window_.mainView().reload();
        return;
    }
    case ACT_COPY:
        if (const MessageYD* m = findMessage(index)) {
            copyToClipboard(window_.hwnd(), GuiTextYD::wide(m->text()));
            window_.mainView().toast(L"已复制");
        }
        return;
    case ACT_AT_SENDER:   // 把“@昵称 ”插入输入框
        if (const MessageYD* m = findMessage(index)) {
            ServiceBaseYD* acc = window_.session().platform().findAccount(m->service(), m->sender());
            const std::wstring text = L"@" + GuiTextYD::wide(acc ? acc->nickname() : m->sender()) + L" ";
            SendMessageW(input_, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(text.c_str()));
            SetFocus(input_);
        }
        return;
    case ACT_SEARCH:
        searching_ = !searching_;
        if (!searching_) {
            SetWindowTextW(search_, L"");
            keyword_.clear();
            rowsDirty_ = true;
        }
        placeEdits();
        if (searching_) SetFocus(search_);
        break;
    case ACT_INFO:
        drawer_ = !drawer_;
        drawerScroll_ = 0;
        rowsDirty_ = true;   // 消息区变窄，气泡要重新排版
        placeEdits();
        break;
    }
    window_.invalidate();
}

bool ChatPanelYD::onEditKey(HWND edit, int key) {
    if (edit == input_ && key == VK_RETURN) {
        send();
        return true;
    }
    if (edit == search_ && key == VK_ESCAPE) {
        onAction(ACT_SEARCH, 0);
        return true;
    }
    return edit == input_ || edit == search_;
}

bool ChatPanelYD::onEditChange(HWND edit) {
    if (edit == input_) {
        hasText_ = GetWindowTextLengthW(input_) > 0;
        window_.invalidate();
        return true;
    }
    if (edit == search_) {
        keyword_ = UtilYD::trim(GuiTextYD::utf8(GuiTextYD::editText(search_)));
        rowsDirty_ = true;
        stickBottom_ = true;
        window_.invalidate();
        return true;
    }
    return false;
}

void ChatPanelYD::onWheel(float x, float y, float delta) {
    if (!open_) return;
    menuSeq_ = -1;
    if (drawer_ && inside(drawerArea(), x, y)) {
        const float limit = (std::max)(0.0f, drawerHeight_ - (drawerArea().bottom - drawerArea().top));
        drawerScroll_ = (std::min)((std::max)(drawerScroll_ - delta * 60, 0.0f), limit);
    } else if (inside(messageArea(), x, y)) {
        scroll_ = (std::min)((std::max)(scroll_ - delta * 60, 0.0f), maxScroll());
        stickBottom_ = scroll_ >= maxScroll() - 2;
    }
    window_.invalidate();
}

// ======================== 区域划分 ========================

float ChatPanelYD::contentRight() const { return drawer_ ? bounds_.right - kDrawerWidth : bounds_.right; }

int ChatPanelYD::iconCount() const { return conversation_.isGroup ? 4 : 3; }

float ChatPanelYD::iconsLeft() const { return bounds_.right - 24 - iconCount() * 36.0f - (iconCount() - 1) * 8.0f; }

D2D1_RECT_F ChatPanelYD::headerArea() const {
    return D2D1::RectF(bounds_.left, bounds_.top, bounds_.right, bounds_.top + kHeaderHeight);
}

D2D1_RECT_F ChatPanelYD::inputArea() const {
    const float height = canSend_ ? kInputHeight : kBlockedHeight;
    return D2D1::RectF(bounds_.left, bounds_.bottom - height, contentRight(), bounds_.bottom);
}

D2D1_RECT_F ChatPanelYD::messageArea() const {
    return D2D1::RectF(bounds_.left, bounds_.top + kHeaderHeight, contentRight(), inputArea().top);
}

D2D1_RECT_F ChatPanelYD::drawerArea() const {
    return D2D1::RectF(bounds_.right - kDrawerWidth, bounds_.top + kHeaderHeight, bounds_.right, bounds_.bottom);
}

D2D1_RECT_F ChatPanelYD::inputField() const {
    const D2D1_RECT_F r = inputArea();
    const float top = conversation_.isGroup ? r.top + 50 : r.top + 18;   // 群聊上方留出“@ 提醒”工具栏
    return D2D1::RectF(r.left + 24, top, r.right - 24, r.bottom - 60);
}

int ChatPanelYD::rowAt(float x, float y) const {
    const D2D1_RECT_F area = messageArea();
    if (!inside(area, x, y)) return -1;
    const float contentY = y - area.top + scroll_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (contentY >= rows_[i].top - 8 && contentY < rows_[i].top + rows_[i].height + 8) return static_cast<int>(i);
    }
    return -1;
}

void ChatPanelYD::onClick(float x, float y) {
    const bool menu = menuSeq_ >= 0;
    ViewYD::onClick(x, y);   // 菜单项最后登记，盖在其他区域上面，优先命中
    if (menu) closeMenu();   // 点了菜单项或者别处，菜单都收起
}

void ChatPanelYD::onRightClick(float x, float y) {
    menuSeq_ = -1;
    const int row = open_ ? rowAt(x, y) : -1;
    if (row >= 0 && rows_[row].kind != ROW_TIME && rows_[row].kind != ROW_SYSTEM) {   // 系统提示、撤回提示没有菜单
        menuSeq_ = messages_[rows_[row].message].seq();
        menuX_ = x;
        menuY_ = y;
    }
    window_.invalidate();
}

void ChatPanelYD::closeMenu() {
    if (menuSeq_ < 0) return;
    menuSeq_ = -1;
    window_.invalidate();
}

const MessageYD* ChatPanelYD::findMessage(int seq) const {
    for (const MessageYD& m : messages_) {
        if (m.seq() == seq) return &m;
    }
    return nullptr;
}

std::vector<ChatPanelYD::MenuItemYD> ChatPanelYD::menuItems(const MessageYD& m) const {
    SessionYD& session = window_.session();
    std::vector<MenuItemYD> items{ { L"复制", ACT_COPY, false } };
    if (conversation_.isGroup && canSend_ && m.sender() != session.current()->id()) items.push_back({ L"@TA", ACT_AT_SENDER, false });
    if (session.platform().canRevoke(*session.current(), m)) items.push_back({ L"撤回", ACT_REVOKE, true });
    return items;
}

float ChatPanelYD::maxScroll() const {
    const D2D1_RECT_F area = messageArea();
    return (std::max)(0.0f, contentHeight_ - (area.bottom - area.top));
}

void ChatPanelYD::placeEdits() {
    if (!input_ || !search_) return;
    const bool dialog = window_.hasDialog();   // 对话框打开期间不显示输入框（系统控件会盖住对话框）
    const bool showInput = open_ && canSend_ && !dialog;
    ShowWindow(input_, showInput ? SW_SHOW : SW_HIDE);
    if (showInput) {
        const D2D1_RECT_F f = inputField();
        window_.placeEdit(input_, D2D1::RectF(f.left + 14, f.top + 10, f.right - 14, f.bottom - 8));
    }
    const bool showSearch = open_ && searching_ && !dialog;
    ShowWindow(search_, showSearch ? SW_SHOW : SW_HIDE);
    if (showSearch) {
        const D2D1_RECT_F h = headerArea();
        const float right = iconsLeft() - 12;   // 查找框在图标按钮的左边
        const float mid = (h.top + h.bottom) / 2;
        window_.placeEdit(search_, D2D1::RectF(right - 240 + 36, mid - 11, right - 12, mid + 11));
    }
}

// ======================== 消息排版 ========================

std::wstring ChatPanelYD::senderName(const std::string& id) const {
    ServiceBaseYD* acc = window_.session().platform().findAccount(window_.session().current()->type(), id);
    return GuiTextYD::wide(acc ? acc->nickname() : id);
}

bool ChatPanelYD::matches(const MessageYD& m) const {
    if (keyword_.empty()) return true;
    return UtilYD::containsText(m.text(), keyword_) || UtilYD::containsText(GuiTextYD::utf8(senderName(m.sender())), keyword_);
}

void ChatPanelYD::rebuildRows(CanvasYD& c) {
    const D2D1_RECT_F area = messageArea();
    const float width = area.right - area.left;
    const float maxBubble = (std::min)(540.0f, width * 0.70f);
    const std::string& me = window_.session().current()->id();
    rows_.clear();
    matchCount_ = 0;
    float y = 24;
    long long previous = -1;
    for (size_t i = 0; i < messages_.size(); ++i) {
        const MessageYD& m = messages_[i];
        if (!matches(m)) continue;
        ++matchCount_;
        const long long minutes = GuiTextYD::minutes(m.time());
        if (previous < 0 || minutes - previous >= 5) {   // 与上一条相隔 5 分钟以上，显示时间
            rows_.push_back({ ROW_TIME, i, y, 28, 0, 0, 0, nullptr, GuiTextYD::chatTime(m.time()) });
            y += 28 + 8;
        }
        previous = minutes;
        RowYD row{ ROW_OTHER, i, y, 0, 0, 0, 0, nullptr, L"" };
        if (m.isSystem() || m.revoked()) {   // 系统提示、撤回提示：居中的灰色小字
            row.kind = ROW_SYSTEM;
            const std::string text = m.revoked() ? window_.session().platform().revokeText(*window_.session().current(), m) : m.text();
            row.layout = c.wrapLayout(GuiTextYD::wide(text), 12, width * 0.7f);
            const D2D1_SIZE_F size = CanvasYD::layoutSize(row.layout.Get());
            row.bubbleWidth = size.width + 20;
            row.bubbleHeight = size.height + 10;
            row.height = row.bubbleHeight;
        } else if (m.isGroup() && m.text().compare(0, kNoticePrefix.size(), kNoticePrefix) == 0) {
            row.kind = ROW_NOTICE;
            row.bubbleWidth = (std::min)(width * 0.72f, 520.0f);
            row.layout = c.wrapLayout(GuiTextYD::wide(m.text().substr(kNoticePrefix.size())), 14, row.bubbleWidth - 36);
            row.height = 44 + CanvasYD::layoutSize(row.layout.Get()).height + 16;
        } else {
            row.kind = m.sender() == me ? ROW_MINE : ROW_OTHER;
            row.layout = c.wrapLayout(GuiTextYD::wide(m.text()), 14.5f, maxBubble - 28);
            const D2D1_SIZE_F size = CanvasYD::layoutSize(row.layout.Get());
            row.bubbleWidth = (std::max)(size.width + 28, 44.0f);
            row.bubbleHeight = size.height + 20;
            row.nameHeight = (conversation_.isGroup && row.kind == ROW_OTHER) ? 20.0f : 0.0f;
            row.height = (std::max)(40.0f, row.nameHeight + row.bubbleHeight);
        }
        rows_.push_back(row);
        y += row.height + 16;
    }
    contentHeight_ = y + 8;
    rowsWidth_ = width;
    rowsDirty_ = false;
}

// ======================== 绘制 ========================

void ChatPanelYD::paint(CanvasYD& c) {
    beginHits();
    c.fillRect(bounds_, ThemeYD::rgb(ThemeYD::BG));
    if (!open_) {
        paintEmpty(c);
        return;
    }
    const D2D1_RECT_F area = messageArea();
    if (rowsDirty_ || rowsWidth_ != area.right - area.left) rebuildRows(c);
    if (stickBottom_) scroll_ = maxScroll();
    scroll_ = (std::min)(scroll_, maxScroll());
    paintMessages(c);
    paintInput(c);
    if (drawer_) paintDrawer(c);
    paintHeader(c);
    paintMenu(c);   // 右键菜单画在最上层
}

void ChatPanelYD::paintEmpty(CanvasYD& c) {
    const float cx = (bounds_.left + bounds_.right) / 2, cy = (bounds_.top + bounds_.bottom) / 2 - 30;
    c.fillCircle(cx, cy - 20, 52, ThemeYD::rgb(0xFFFFFF));
    c.icon(kIconChat, D2D1::RectF(cx - 40, cy - 60, cx + 40, cy + 20), 40,
           ThemeYD::accent(window_.session().current()->type()));
    c.text(L"选择一个会话，开始聊天", D2D1::RectF(bounds_.left, cy + 50, bounds_.right, cy + 76), 16,
           ThemeYD::rgb(ThemeYD::TEXT2), AlignYD::CENTER);
    c.text(L"消息实时同步 —— 再打开一个窗口登录好友的账号试试", D2D1::RectF(bounds_.left, cy + 80, bounds_.right, cy + 100), 12.5f,
           ThemeYD::rgb(ThemeYD::TEXT3), AlignYD::CENTER);
}

void ChatPanelYD::paintHeader(CanvasYD& c) {
    const D2D1_RECT_F h = headerArea();
    SessionYD& session = window_.session();
    c.fillRect(h, ThemeYD::rgb(ThemeYD::PANEL));
    c.line(h.left, h.bottom - 0.5f, h.right, h.bottom - 0.5f, ThemeYD::rgb(ThemeYD::BORDER));
    const D2D1_COLOR_F accent = ThemeYD::accent(session.current()->type());

    // 右侧图标按钮（从左到右）：实时群聊室（仅群聊）、查找聊天记录、群设置/好友资料、资料抽屉
    const float iconTop = h.top + 14;
    struct IconYD {
        wchar_t glyph;
        int action;
        bool active;
    };
    std::vector<IconYD> icons;
    if (conversation_.isGroup) icons.push_back({ kIconRoom, ACT_ROOM, false });
    icons.push_back({ kIconSearch, ACT_SEARCH, searching_ });
    icons.push_back({ kIconSettings, ACT_SETTINGS, false });
    icons.push_back({ kIconInfo, ACT_INFO, drawer_ });
    float x = iconsLeft();
    for (const IconYD& icon : icons) {
        const D2D1_RECT_F button = D2D1::RectF(x, iconTop, x + 36, iconTop + 36);
        D2D1_COLOR_F tint = accent;
        tint.a = 0.10f;
        if (icon.active) c.fillRound(button, 10, tint);
        else if (hovered(button)) c.fillRound(button, 10, ThemeYD::rgb(ThemeYD::FIELD));
        c.icon(icon.glyph, button, 16, icon.active ? accent : ThemeYD::rgb(ThemeYD::TEXT2));
        addHit(button, icon.action);
        x += 36 + 8;
    }

    float titleRight = iconsLeft() - 16;
    if (searching_) {   // 查找框
        const D2D1_RECT_F field = D2D1::RectF(iconsLeft() - 12 - 240, iconTop, iconsLeft() - 12, iconTop + 36);
        c.fillRound(field, 10, ThemeYD::rgb(ThemeYD::FIELD));
        c.icon(kIconSearch, D2D1::RectF(field.left + 4, field.top, field.left + 34, field.bottom), 13, ThemeYD::rgb(ThemeYD::TEXT3));
        titleRight = field.left - 16;
    }

    std::wstring subtitle;
    if (conversation_.isGroup) {
        GroupYD* g = session.groupOf(conversation_);
        subtitle = L"群号 " + std::to_wstring(conversation_.groupNo);
        if (g) subtitle += L" · " + std::to_wstring(g->members().size()) + L" 人 · " + GuiTextYD::wide(g->policy().modeName());
    } else {
        subtitle = ThemeYD::shortName(session.current()->type()) + L" " + GuiTextYD::wide(conversation_.peerId);
    }
    if (!keyword_.empty()) subtitle = L"找到 " + std::to_wstring(matchCount_) + L" 条相关记录";
    c.text(GuiTextYD::wide(session.titleOf(conversation_)), D2D1::RectF(h.left + 28, h.top + 11, titleRight, h.top + 36), 17,
           ThemeYD::rgb(ThemeYD::TEXT), AlignYD::LEFT, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    c.text(subtitle, D2D1::RectF(h.left + 28, h.top + 36, titleRight, h.top + 54), 12,
           keyword_.empty() ? ThemeYD::rgb(ThemeYD::TEXT3) : accent);
}

void ChatPanelYD::paintAvatar(CanvasYD& c, float cx, float cy, float radius, const std::string& id) {
    ServiceBaseYD* acc = window_.session().platform().findAccount(window_.session().current()->type(), id);
    c.fillCircle(cx, cy, radius, ThemeYD::avatar(id));
    c.text(GuiTextYD::initial(acc ? acc->nickname() : id), D2D1::RectF(cx - radius, cy - radius, cx + radius, cy + radius),
           radius * 0.78f, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
}

void ChatPanelYD::paintMessages(CanvasYD& c) {
    const D2D1_RECT_F area = messageArea();
    const D2D1_COLOR_F accent = ThemeYD::accent(window_.session().current()->type());
    c.pushClip(area);
    if (rows_.empty()) {
        c.text(keyword_.empty() ? L"还没有消息，打个招呼吧" : L"没有找到相关的聊天记录",
               D2D1::RectF(area.left, area.top, area.right, area.bottom), 13, ThemeYD::rgb(ThemeYD::TEXT3), AlignYD::CENTER);
    }
    const float base = area.top - scroll_;
    SessionYD& session = window_.session();
    PlatformYD& platform = session.platform();
    const GroupYD* group = session.groupOf(conversation_);
    for (const RowYD& row : rows_) {
        const float top = base + row.top;
        if (top + row.height < area.top || top > area.bottom) continue;   // 只画看得见的部分
        const MessageYD& m = messages_[row.message];
        switch (row.kind) {
        case ROW_TIME:
            c.text(row.label, D2D1::RectF(area.left, top, area.right, top + row.height), 12, ThemeYD::rgb(ThemeYD::TEXT3),
                   AlignYD::CENTER);
            break;
        case ROW_SYSTEM: {   // 与微信相同：灰色小字，居中
            const float cx = (area.left + area.right) / 2;
            const D2D1_RECT_F pill = D2D1::RectF(cx - row.bubbleWidth / 2, top, cx + row.bubbleWidth / 2, top + row.bubbleHeight);
            c.fillRound(pill, 6, ThemeYD::rgb(0x000000, 0.04f));
            c.drawLayout(row.layout.Get(), pill.left + 10, pill.top + 5, ThemeYD::rgb(ThemeYD::TEXT3));
            break;
        }
        case ROW_NOTICE: {
            const float cx = (area.left + area.right) / 2;
            const D2D1_RECT_F card = D2D1::RectF(cx - row.bubbleWidth / 2, top, cx + row.bubbleWidth / 2, top + row.height);
            c.fillRound(card, 12, ThemeYD::rgb(0xFFFFFF));
            D2D1_COLOR_F tint = accent;
            tint.a = 0.07f;
            c.fillRound(card, 12, tint);
            c.icon(kIconInfo, D2D1::RectF(card.left + 14, top + 12, card.left + 32, top + 32), 14, accent);
            c.text(L"群公告 · " + senderName(m.sender()), D2D1::RectF(card.left + 38, top + 12, card.right - 16, top + 32), 12.5f,
                   accent, AlignYD::LEFT, DWRITE_FONT_WEIGHT_SEMI_BOLD);
            c.drawLayout(row.layout.Get(), card.left + 18, top + 42, ThemeYD::rgb(ThemeYD::TEXT));
            break;
        }
        case ROW_OTHER: {
            paintAvatar(c, area.left + 28 + 20, top + 20, 20, m.sender());
            const float x = area.left + 80;
            if (row.nameHeight > 0) {
                const std::wstring name = senderName(m.sender());
                c.text(name, D2D1::RectF(x + 2, top, x + 300, top + 16), 12, ThemeYD::rgb(ThemeYD::TEXT3));
                if (group && UtilYD::contains(platform.mentionsOf(*group, m), session.current()->id())) {   // 这条消息 @了我
                    const float nx = x + 2 + c.textWidth(name, 12) + 6;
                    c.text(L"@了你", D2D1::RectF(nx, top, nx + 60, top + 16), 12, accent, AlignYD::LEFT, DWRITE_FONT_WEIGHT_SEMI_BOLD);
                }
            }
            const D2D1_RECT_F bubble = D2D1::RectF(x, top + row.nameHeight, x + row.bubbleWidth, top + row.nameHeight + row.bubbleHeight);
            c.shadow(bubble, 12, 3, 0.05f);
            c.fillRound(bubble, 12, ThemeYD::rgb(0xFFFFFF));
            c.drawLayout(row.layout.Get(), bubble.left + 14, bubble.top + 10, ThemeYD::rgb(ThemeYD::TEXT));
            break;
        }
        case ROW_MINE: {
            paintAvatar(c, area.right - 28 - 20, top + 20, 20, m.sender());
            const float right = area.right - 80;
            const D2D1_RECT_F bubble = D2D1::RectF(right - row.bubbleWidth, top, right, top + row.bubbleHeight);
            c.fillRound(bubble, 12, accent);
            c.drawLayout(row.layout.Get(), bubble.left + 14, bubble.top + 10, ThemeYD::rgb(0xFFFFFF));
            break;
        }
        }
    }
    // 细滚动条
    const float viewHeight = area.bottom - area.top;
    if (contentHeight_ > viewHeight) {
        const float thumb = (std::max)(40.0f, viewHeight * viewHeight / contentHeight_);
        const float y = area.top + (viewHeight - thumb) * (maxScroll() > 0 ? scroll_ / maxScroll() : 0);
        c.fillRound(D2D1::RectF(area.right - 8, y + 4, area.right - 4, y + thumb - 4), 2, ThemeYD::rgb(0x000000, 0.12f));
    }
    c.popClip();
}

void ChatPanelYD::paintInput(CanvasYD& c) {
    const D2D1_RECT_F r = inputArea();
    // 与消息区明确分开：消息区底部一道淡淡的阴影 + 一条分隔线，输入区为白色
    c.verticalGradient(D2D1::RectF(r.left, r.top - 10, r.right, r.top), ThemeYD::rgb(0x000000, 0.0f), ThemeYD::rgb(0x000000, 0.045f));
    c.fillRect(r, ThemeYD::rgb(ThemeYD::PANEL));
    c.line(r.left, r.top + 0.5f, r.right, r.top + 0.5f, ThemeYD::rgb(0xDCDFE4));
    if (!canSend_) {
        const std::wstring text = GuiTextYD::wide(reason_);
        const float w = c.textWidth(text, 13) + 26;
        const float cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        c.icon(kIconBlocked, D2D1::RectF(cx - w / 2, cy - 10, cx - w / 2 + 18, cy + 10), 13, ThemeYD::rgb(ThemeYD::TEXT3));
        c.text(text, D2D1::RectF(cx - w / 2 + 26, cy - 12, cx + w / 2 + 4, cy + 12), 13, ThemeYD::rgb(ThemeYD::TEXT3));
        return;
    }
    const D2D1_COLOR_F accent = ThemeYD::accent(window_.session().current()->type());
    if (conversation_.isGroup) {   // 工具栏：@ 提醒
        const D2D1_RECT_F at = D2D1::RectF(r.left + 24, r.top + 12, r.left + 24 + 84, r.top + 40);
        if (hovered(at)) c.fillRound(at, 8, ThemeYD::rgb(ThemeYD::FIELD));
        c.text(L"@ 提醒", at, 13, ThemeYD::rgb(hovered(at) ? ThemeYD::TEXT : ThemeYD::TEXT2), AlignYD::CENTER);
        addHit(at, ACT_MENTION);
    }
    // 输入框：独立的圆角灰色区域，获得焦点时描上强调色边框
    const D2D1_RECT_F field = inputField();
    c.fillRound(field, 12, ThemeYD::rgb(ThemeYD::FIELD));
    if (GetFocus() == input_) c.strokeRound(field, 12, accent, 1.5f);
    else c.strokeRound(field, 12, ThemeYD::rgb(0xE3E5E8));
    addHit(field, ACT_FOCUS_INPUT);
    const D2D1_RECT_F button = D2D1::RectF(r.right - 24 - 92, r.bottom - 48, r.right - 24, r.bottom - 14);
    D2D1_COLOR_F fill = hasText_ ? (hovered(button) ? ThemeYD::darker(accent, 0.08f) : accent) : accent;
    if (!hasText_) fill.a = 0.35f;
    c.fillRound(button, 10, fill);
    c.text(L"发送", button, 14, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    addHit(button, ACT_SEND);
    c.text(L"Enter 发送", D2D1::RectF(button.left - 160, button.top, button.left - 14, button.bottom), 12,
           ThemeYD::rgb(ThemeYD::TEXT4), AlignYD::RIGHT);
    c.text(L"右键消息可以复制、撤回", D2D1::RectF(field.left + 4, button.top, button.left - 170, button.bottom), 12,
           ThemeYD::rgb(ThemeYD::TEXT4));
}

void ChatPanelYD::paintMenu(CanvasYD& c) {
    const MessageYD* m = menuSeq_ >= 0 ? findMessage(menuSeq_) : nullptr;
    if (!m) return;
    const std::vector<MenuItemYD> items = menuItems(*m);
    const float w = 132, itemHeight = 36, pad = 6;
    const float h = items.size() * itemHeight + pad * 2;
    const D2D1_RECT_F area = messageArea();
    const float left = (std::min)(menuX_, area.right - w - 8);   // 靠近边缘时往里收，不超出消息区
    const float top = (std::min)(menuY_, area.bottom - h - 8);
    const D2D1_RECT_F card = D2D1::RectF(left, top, left + w, top + h);
    c.shadow(card, 10, 8, 0.12f);
    c.fillRound(card, 10, ThemeYD::rgb(0xFFFFFF));
    c.strokeRound(card, 10, ThemeYD::rgb(ThemeYD::BORDER));
    float y = top + pad;
    for (const MenuItemYD& item : items) {
        const D2D1_RECT_F r = D2D1::RectF(left + pad, y, left + w - pad, y + itemHeight);
        if (hovered(r)) c.fillRound(r, 6, ThemeYD::rgb(ThemeYD::FIELD));
        c.text(item.label, D2D1::RectF(r.left + 14, r.top, r.right - 8, r.bottom), 13.5f,
               ThemeYD::rgb(item.danger ? ThemeYD::DANGER : ThemeYD::TEXT));
        addHit(r, item.action, menuSeq_);
        y += itemHeight;
    }
}

float ChatPanelYD::paintTag(CanvasYD& c, float right, float cy, const std::wstring& text, const D2D1_COLOR_F& color) {
    const float w = c.textWidth(text, 11) + 14;
    D2D1_COLOR_F tint = color;
    tint.a = 0.12f;
    const D2D1_RECT_F tag = D2D1::RectF(right - w, cy - 10, right, cy + 10);
    c.fillRound(tag, 6, tint);
    c.text(text, tag, 11, color, AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    return tag.left - 6;
}

void ChatPanelYD::paintDrawer(CanvasYD& c) {
    const D2D1_RECT_F r = drawerArea();
    SessionYD& session = window_.session();
    PlatformYD& platform = session.platform();
    ServiceBaseYD* me = session.current();
    const ServiceTypeYD type = me->type();
    const D2D1_COLOR_F accent = ThemeYD::accent(type);
    c.fillRect(r, ThemeYD::rgb(ThemeYD::PANEL));
    c.line(r.left + 0.5f, r.top, r.left + 0.5f, r.bottom, ThemeYD::rgb(ThemeYD::BORDER));
    c.pushClip(r);
    const float left = r.left + 28, right = r.right - 28, cx = (r.left + r.right) / 2;
    float y = r.top + 28 - drawerScroll_;
    auto section = [&](const std::wstring& title) {
        y += 18;
        c.text(title, D2D1::RectF(left, y, right, y + 20), 12.5f, ThemeYD::rgb(ThemeYD::TEXT3), AlignYD::LEFT,
               DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += 28;
    };
    auto field = [&](const std::wstring& label, const std::wstring& value) {
        c.text(label, D2D1::RectF(left, y, left + 80, y + 22), 13, ThemeYD::rgb(ThemeYD::TEXT3));
        c.text(value, D2D1::RectF(left + 80, y, right, y + 22), 13, ThemeYD::rgb(ThemeYD::TEXT), AlignYD::RIGHT);
        y += 32;
    };

    if (!conversation_.isGroup) {
        ServiceBaseYD* other = platform.findAccount(type, conversation_.peerId);
        paintAvatar(c, cx, y + 36, 36, conversation_.peerId);
        y += 84;
        c.text(GuiTextYD::wide(session.titleOf(conversation_)), D2D1::RectF(left, y, right, y + 26), 17,
               ThemeYD::rgb(ThemeYD::TEXT), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += 28;
        c.text(ThemeYD::shortName(type) + L" " + GuiTextYD::wide(conversation_.peerId), D2D1::RectF(left, y, right, y + 18), 12.5f,
               ThemeYD::rgb(ThemeYD::TEXT3), AlignYD::CENTER);
        y += 22;
        if (other) {
            section(L"资料");
            field(L"昵称", GuiTextYD::wide(other->nickname()));
            field(L"所在地", GuiTextYD::wide(other->location()));
            field(L"生日", GuiTextYD::wide(other->birthday().toString()));
            field(L"T龄", std::to_wstring(other->tAge()) + L" 年");
            std::vector<std::string> mutual = platform.mutualFriends(*me, *other);
            section(L"共同好友 · " + std::to_wstring(mutual.size()));
            if (mutual.empty()) {
                c.text(L"暂无", D2D1::RectF(left, y, right, y + 20), 13, ThemeYD::rgb(ThemeYD::TEXT4));
                y += 28;
            }
            for (const std::string& id : mutual) {
                paintAvatar(c, left + 14, y + 14, 14, id);
                c.text(senderName(id), D2D1::RectF(left + 38, y, right, y + 28), 13, ThemeYD::rgb(ThemeYD::TEXT));
                y += 38;
            }
        }
    } else if (GroupYD* g = session.groupOf(conversation_)) {
        const GroupPolicyYD& policy = g->policy();
        const D2D1_RECT_F logo = D2D1::RectF(cx - 36, y, cx + 36, y + 72);
        c.fillRound(logo, 20, ThemeYD::avatar("G" + std::to_string(g->groupNo())));
        c.text(GuiTextYD::initial(g->name()), logo, 28, ThemeYD::rgb(0xFFFFFF), AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += 84;
        c.text(GuiTextYD::wide(g->name()), D2D1::RectF(left, y, right, y + 26), 17, ThemeYD::rgb(ThemeYD::TEXT),
               AlignYD::CENTER, DWRITE_FONT_WEIGHT_SEMI_BOLD);
        y += 28;
        c.text(L"群号 " + std::to_wstring(g->groupNo()) + L" · " + GuiTextYD::wide(policy.modeName()),
               D2D1::RectF(left, y, right, y + 18), 12.5f, ThemeYD::rgb(ThemeYD::TEXT3), AlignYD::CENTER);
        y += 22;
        section(L"群公告");
        if (g->notice().empty()) {
            c.text(L"暂无公告", D2D1::RectF(left, y, right, y + 20), 13, ThemeYD::rgb(ThemeYD::TEXT4));
            y += 28;
        } else {
            auto layout = c.wrapLayout(GuiTextYD::wide(g->notice()), 13, right - left - 28);
            const float h = CanvasYD::layoutSize(layout.Get()).height;
            c.fillRound(D2D1::RectF(left, y, right, y + h + 24), 10, ThemeYD::rgb(ThemeYD::FIELD));
            c.drawLayout(layout.Get(), left + 14, y + 12, ThemeYD::rgb(ThemeYD::TEXT2));
            y += h + 34;
        }
        section(L"群成员 · " + std::to_wstring(g->members().size()));
        for (const std::string& id : g->members()) {
            paintAvatar(c, left + 15, y + 15, 15, id);
            float tagRight = right;
            if (g->isOwner(id)) tagRight = paintTag(c, tagRight, y + 15, L"群主", accent);
            else if (policy.allowAdmin() && g->isAdmin(id)) tagRight = paintTag(c, tagRight, y + 15, L"管理员", ThemeYD::rgb(0xFF7A1A));
            if (policy.allowMute() && g->isMuted(id)) tagRight = paintTag(c, tagRight, y + 15, L"禁言", ThemeYD::rgb(ThemeYD::TEXT3));
            std::wstring name = senderName(id);
            if (id == me->id()) name += L"（我）";
            c.text(name, D2D1::RectF(left + 40, y, tagRight - 4, y + 30), 13, ThemeYD::rgb(ThemeYD::TEXT));
            y += 40;
        }
    }
    drawerHeight_ = y + drawerScroll_ - r.top + 20;
    c.popClip();
}
