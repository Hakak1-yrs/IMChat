#include "P2PPageYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "UtilYD.h"

namespace {

// 端口号输入检查：1024～65535
bool readPort(DialogYD& d, size_t field, unsigned short& port) {
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

P2PPageYD::P2PPageYD(GuiWindowYD& window) : PageYD(window), log_(window.platform().dataDir() + "chat_log.txt") {}

bool P2PPageYD::active() const {
    return link_.state() == ChatLinkYD::LISTENING || link_.state() == ChatLinkYD::CONNECTING ||
           link_.state() == ChatLinkYD::CONNECTED;
}

std::string P2PPageYD::peerIp() const { return link_.peer().substr(0, link_.peer().find(':')); }

void P2PPageYD::addLine(const std::string& text, int kind) { lines_.push_back({ GuiTextYD::wide(text), kind }); }

void P2PPageYD::build(PageModelYD& model) {
    model.title = L"QQ点对点聊天（TCP）";
    if (me().type() != ServiceTypeYD::QQ && !active()) {
        model.subtitle = L"点对点聊天是 QQ 的功能";
        model.note(L"点对点聊天是QQ的功能，请先切换到QQ服务。");
        if (user().hasService(ServiceTypeYD::QQ)) model.actions({ PageModelYD::button(L"切换到QQ", ACT_SWITCH_QQ, "", PageModelYD::PRIMARY) });
        return;
    }
    switch (link_.state()) {
    case ChatLinkYD::IDLE:
    case ChatLinkYD::CLOSED:
        model.subtitle = link_.state() == ChatLinkYD::CLOSED ? L"连接已结束" : L"未连接";
        if (lines_.empty()) {
            model.text(L"一方选择“等待好友连接”作为服务端（socket → bind → listen → accept），另一方选择“连接好友”作为客户端"
                       L"（socket → connect）。连接建立后双方可以同时收发消息，收发内容自动写入聊天记录。\n"
                       L"本机演示：再打开一个程序窗口（图形版或控制台版都可以），IP 填 127.0.0.1，端口一致。");
        }
        for (const auto& line : lines_) model.chat(line.first, static_cast<PageModelYD::ChatKindYD>(line.second));
        model.actions({ PageModelYD::button(L"等待好友连接（服务端）", ACT_LISTEN, "", PageModelYD::PRIMARY),
                        PageModelYD::button(L"连接好友（客户端）", ACT_CONNECT) });
        if (!lines_.empty()) model.actions({ PageModelYD::button(L"清空本次聊天", ACT_RESET) });
        break;
    case ChatLinkYD::LISTENING:
        model.subtitle = L"正在端口 " + std::to_wstring(link_.port()) + L" 等待好友连接……";
        model.note(L"正在端口 " + std::to_wstring(link_.port()) + L" 等待好友连接。可以切换到其他页面，连接建立后回到这里聊天。");
        model.actions({ PageModelYD::button(L"取消等待", ACT_STOP, "", PageModelYD::DANGER) });
        break;
    case ChatLinkYD::CONNECTING:
        model.subtitle = L"正在连接 " + GuiTextYD::wide(link_.peer()) + L" ……";
        model.note(L"正在连接……");
        model.actions({ PageModelYD::button(L"取消", ACT_STOP, "", PageModelYD::DANGER) });
        break;
    case ChatLinkYD::CONNECTED:
        model.subtitle = L"已连接 " + GuiTextYD::wide(link_.peer());
        model.headerButtons.push_back(PageModelYD::button(L"结束聊天", ACT_STOP, "", PageModelYD::DANGER));
        for (const auto& line : lines_) model.chat(line.first, static_cast<PageModelYD::ChatKindYD>(line.second));
        model.composer = true;
        model.stickBottom = true;
        return;   // 聊天中不显示历史记录区域
    }

    // 聊天记录（data\chat_log.txt）
    model.section(L"聊天记录");
    model.actions({ PageModelYD::button(L"最近 20 条", ACT_RECENT), PageModelYD::button(L"按关键字查找", ACT_SEARCH) });
    if (!historyTitle_.empty()) {
        std::wstring text;
        for (const std::string& line : history_) text += (text.empty() ? L"" : L"\n") + GuiTextYD::wide(line);
        model.note(historyTitle_);
        if (!history_.empty()) model.text(text);
    }
}

bool P2PPageYD::poll() {
    std::vector<std::string> received = link_.poll();
    bool changed = !received.empty();
    for (const std::string& line : received) {
        addLine(line, PageModelYD::CHAT_OTHER);   // 对方发来的内容已带有“[昵称] ”前缀
        log_.append("收", peerIp(), line);
    }
    if (link_.state() != lastState_) {
        if (link_.state() == ChatLinkYD::CONNECTED) addLine("连接成功：" + link_.peer() + "，现在可以聊天了。", PageModelYD::CHAT_SYSTEM);
        if (link_.state() == ChatLinkYD::CLOSED) addLine(link_.error(), PageModelYD::CHAT_SYSTEM);
        if (link_.state() == ChatLinkYD::CLOSED && lastState_ == ChatLinkYD::CONNECTED) toast(L"点对点聊天：对方已断开连接");
        lastState_ = link_.state();
        changed = true;
    }
    return changed;
}

void P2PPageYD::onCompose(const std::string& text) {
    if (link_.state() != ChatLinkYD::CONNECTED) return;
    if (!link_.send("[" + nickname_ + "] " + text)) {   // 与控制台版相同的格式
        addLine(link_.error(), PageModelYD::CHAT_SYSTEM);
        return;
    }
    addLine(text, PageModelYD::CHAT_MINE);
    log_.append("发", peerIp(), text);
}

void P2PPageYD::onAction(int action, const std::string&) {
    switch (action) {
    case ACT_SWITCH_QQ:
        main().switchService(ServiceTypeYD::QQ);
        break;
    case ACT_LISTEN: {
        auto dialog = std::make_unique<DialogYD>(window_, L"等待好友连接（服务端）");
        dialog->field(L"端口号（1024-65535）", L"8888").primary(L"开始等待", [this](DialogYD& d) {
            unsigned short port = 0;
            if (!readPort(d, 0, port)) return false;
            std::string error;
            if (!link_.listen(port, error)) {
                d.setError(GuiTextYD::wide(error));
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
        auto dialog = std::make_unique<DialogYD>(window_, L"连接好友（客户端）");
        dialog->field(L"对方 IP 地址", L"127.0.0.1").field(L"端口号", L"8888").primary(L"连接", [this](DialogYD& d) {
            unsigned short port = 0;
            if (!readPort(d, 1, port)) return false;
            std::string error;
            if (!link_.connect(d.text(0), port, error)) {
                d.setError(GuiTextYD::wide(error));
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
        if (link_.state() == ChatLinkYD::CONNECTED) addLine("聊天结束。", PageModelYD::CHAT_SYSTEM);
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
        auto dialog = std::make_unique<DialogYD>(window_, L"查找聊天记录");
        dialog->field(L"关键字", L"", L"对方 IP、消息内容……").primary(L"查找", [this](DialogYD& d) {
            const std::string keyword = d.text(0);
            history_ = log_.query(keyword, 20);
            historyTitle_ = history_.empty() ? L"没有找到包含“" + GuiTextYD::wide(keyword) + L"”的聊天记录。"
                                             : L"包含“" + GuiTextYD::wide(keyword) + L"”的记录共 " + std::to_wstring(history_.size()) + L" 条：";
            window_.invalidate();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}
