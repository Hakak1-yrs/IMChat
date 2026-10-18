#include "P2PPage.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "Util.h"

namespace {

// 端口号输入检查：1024～65535
bool readPort(Dialog& d, size_t field, unsigned short& port) {
    try {
        size_t pos = 0;
        const std::string text = d.text(field);
        const int value = std::stoi(text, &pos);
        if (pos == text.size() && value >= 1024 && value <= 65535) {
            port = static_cast<unsigned short>(value);
            return true;
        }
    } catch (...) {
    }
    d.setError(L"端口号应为 1024～65535 之间的整数。");
    return false;
}

}  // namespace

P2PPage::P2PPage(GuiWindow& window) : Page(window), log_(window.platform().dataDir() + "chat_log.txt") {}

bool P2PPage::active() const {
    return link_.state() == ChatLink::LISTENING || link_.state() == ChatLink::CONNECTING ||
           link_.state() == ChatLink::CONNECTED;
}

std::string P2PPage::peerIp() const { return link_.peer().substr(0, link_.peer().find(':')); }

void P2PPage::addLine(const std::string& text, int kind) { lines_.push_back({ GuiText::wide(text), kind }); }

void P2PPage::build(PageModel& model) {
    model.title = L"QQ点对点聊天（TCP）";
    if (me().type() != ServiceType::QQ && !active()) {
        model.subtitle = L"点对点聊天是 QQ 的功能";
        model.note(L"点对点聊天是QQ的功能，请先切换到QQ服务。");
        if (user().hasService(ServiceType::QQ)) model.actions({ PageModel::button(L"切换到QQ", ACT_SWITCH_QQ, "", PageModel::PRIMARY) });
        return;
    }
    switch (link_.state()) {
    case ChatLink::IDLE:
    case ChatLink::CLOSED:
        model.subtitle = link_.state() == ChatLink::CLOSED ? L"连接已结束" : L"未连接";
        if (lines_.empty()) {
            model.text(L"一方选择“等待好友连接”作为服务端（socket → bind → listen → accept），另一方选择“连接好友”作为客户端"
                       L"（socket → connect）。连接建立后双方可以同时收发消息，收发内容自动写入聊天记录。\n"
                       L"本机演示：再打开一个程序窗口（图形版或控制台版都可以），IP 填 127.0.0.1，端口一致。");
        }
        for (const auto& line : lines_) model.chat(line.first, static_cast<PageModel::ChatKind>(line.second));
        model.actions({ PageModel::button(L"等待好友连接（服务端）", ACT_LISTEN, "", PageModel::PRIMARY),
                        PageModel::button(L"连接好友（客户端）", ACT_CONNECT) });
        if (!lines_.empty()) model.actions({ PageModel::button(L"清空本次聊天", ACT_RESET) });
        break;
    case ChatLink::LISTENING:
        model.subtitle = L"正在端口 " + std::to_wstring(link_.port()) + L" 等待好友连接……";
        model.note(L"正在端口 " + std::to_wstring(link_.port()) + L" 等待好友连接。可以切换到其他页面，连接建立后回到这里聊天。");
        model.actions({ PageModel::button(L"取消等待", ACT_STOP, "", PageModel::DANGER) });
        break;
    case ChatLink::CONNECTING:
        model.subtitle = L"正在连接 " + GuiText::wide(link_.peer()) + L" ……";
        model.note(L"正在连接……");
        model.actions({ PageModel::button(L"取消", ACT_STOP, "", PageModel::DANGER) });
        break;
    case ChatLink::CONNECTED:
        model.subtitle = L"已连接 " + GuiText::wide(link_.peer());
        model.headerButtons.push_back(PageModel::button(L"结束聊天", ACT_STOP, "", PageModel::DANGER));
        for (const auto& line : lines_) model.chat(line.first, static_cast<PageModel::ChatKind>(line.second));
        model.composer = true;
        model.stickBottom = true;
        return;   // 聊天中不显示历史记录区域
    }

    // 聊天记录（data\chat_log.txt）
    model.section(L"聊天记录");
    model.actions({ PageModel::button(L"最近 20 条", ACT_RECENT), PageModel::button(L"按关键字查找", ACT_SEARCH) });
    if (!historyTitle_.empty()) {
        std::wstring text;
        for (const std::string& line : history_) text += (text.empty() ? L"" : L"\n") + GuiText::wide(line);
        model.note(historyTitle_);
        if (!history_.empty()) model.text(text);
    }
}

bool P2PPage::poll() {
    std::vector<std::string> received = link_.poll();
    bool changed = !received.empty();
    for (const std::string& line : received) {
        addLine(line, PageModel::CHAT_OTHER);   // 对方发来的内容已带有“[昵称] ”前缀
        log_.append("收", peerIp(), line);
    }
    if (link_.state() != lastState_) {
        if (link_.state() == ChatLink::CONNECTED) addLine("连接成功：" + link_.peer() + "，现在可以聊天了。", PageModel::CHAT_SYSTEM);
        if (link_.state() == ChatLink::CLOSED) addLine(link_.error(), PageModel::CHAT_SYSTEM);
        if (link_.state() == ChatLink::CLOSED && lastState_ == ChatLink::CONNECTED) toast(L"点对点聊天：对方已断开连接");
        lastState_ = link_.state();
        changed = true;
    }
    return changed;
}

void P2PPage::onCompose(const std::string& text) {
    if (link_.state() != ChatLink::CONNECTED) return;
    if (!link_.send("[" + nickname_ + "] " + text)) {   // 与控制台版相同的格式
        addLine(link_.error(), PageModel::CHAT_SYSTEM);
        return;
    }
    addLine(text, PageModel::CHAT_MINE);
    log_.append("发", peerIp(), text);
}

void P2PPage::onAction(int action, const std::string&) {
    switch (action) {
    case ACT_SWITCH_QQ:
        main().switchService(ServiceType::QQ);
        break;
    case ACT_LISTEN: {
        auto dialog = std::make_unique<Dialog>(window_, L"等待好友连接（服务端）");
        dialog->field(L"端口号（1024-65535）", L"8888").primary(L"开始等待", [this](Dialog& d) {
            unsigned short port = 0;
            if (!readPort(d, 0, port)) return false;
            std::string error;
            if (!link_.listen(port, error)) {
                d.setError(GuiText::wide(error));
                return false;
            }
            lines_.clear();
            nickname_ = me().nickname();
            lastState_ = link_.state();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_CONNECT: {
        auto dialog = std::make_unique<Dialog>(window_, L"连接好友（客户端）");
        dialog->field(L"对方 IP 地址", L"127.0.0.1").field(L"端口号", L"8888").primary(L"连接", [this](Dialog& d) {
            unsigned short port = 0;
            if (!readPort(d, 1, port)) return false;
            std::string error;
            if (!link_.connect(d.text(0), port, error)) {
                d.setError(GuiText::wide(error));
                return false;
            }
            lines_.clear();
            nickname_ = me().nickname();
            lastState_ = link_.state();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_STOP:
        if (link_.state() == ChatLink::CONNECTED) addLine("聊天结束。", PageModel::CHAT_SYSTEM);
        link_.close();
        lastState_ = link_.state();
        break;
    case ACT_RESET:
        lines_.clear();
        break;
    case ACT_RECENT:
        history_ = log_.query("", 20);
        historyTitle_ = history_.empty() ? L"没有聊天记录。" : L"最近 " + std::to_wstring(history_.size()) + L" 条：";
        break;
    case ACT_SEARCH: {
        auto dialog = std::make_unique<Dialog>(window_, L"查找聊天记录");
        dialog->field(L"关键字", L"", L"对方 IP、消息内容……").primary(L"查找", [this](Dialog& d) {
            const std::string keyword = d.text(0);
            history_ = log_.query(keyword, 20);
            historyTitle_ = history_.empty() ? L"没有找到包含“" + GuiText::wide(keyword) + L"”的聊天记录。"
                                             : L"包含“" + GuiText::wide(keyword) + L"”的记录共 " + std::to_wstring(history_.size()) + L" 条：";
            window_.invalidate();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}
