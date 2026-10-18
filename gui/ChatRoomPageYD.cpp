#include "ChatRoomPageYD.h"
#include <windows.h>
#include "ConversationYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "UtilYD.h"

namespace {

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

ChatRoomPageYD::ChatRoomPageYD(GuiWindowYD& window, int groupNo) : PageYD(window), groupNo_(groupNo) {}

ChatRoomPageYD::~ChatRoomPageYD() {
    if (server_) server_->stop();   // 通知所有成员后关闭
}

std::string ChatRoomPageYD::key() const { return "G:" + std::to_string(groupNo_); }

bool ChatRoomPageYD::active() const { return mode_ != IDLE; }

int ChatRoomPageYD::groupNo() const { return groupNo_; }

void ChatRoomPageYD::addLine(const std::string& text) {
    const bool system = text.compare(0, std::string("[系统]").size(), "[系统]") == 0;
    lines_.push_back({ GuiTextYD::wide(system ? UtilYD::trim(text.substr(std::string("[系统]").size())) : text),
                       system ? PageModelYD::CHAT_SYSTEM : PageModelYD::CHAT_OTHER });
}

void ChatRoomPageYD::end(const std::string& reason) {
    const bool wasActive = mode_ == HOSTING || mode_ == JOINED;
    if (server_) server_->stop();
    server_.reset();
    link_.reset();
    mode_ = IDLE;
    if (!reason.empty()) lines_.push_back({ GuiTextYD::wide(reason), PageModelYD::CHAT_SYSTEM });
    if (wasActive) {   // 与控制台版相同：群聊室里的消息已经实时看过了，记为已读
        std::vector<MessageYD> history = platform().messages().groupHistory(me().type(), groupNo_);
        if (!history.empty()) platform().messages().markGroupRead(me().type(), groupNo_, me().id(), history.back().seq());
        refresh();
    }
}

void ChatRoomPageYD::build(PageModelYD& model) {
    GroupYD* g = platform().groups().find(me().type(), groupNo_);
    const std::wstring title = L"群 " + std::to_wstring(groupNo_) + (g ? L"（" + GuiTextYD::wide(g->name()) + L"）" : L"");
    model.title = L"实时群聊室 · " + title;
    switch (mode_) {
    case IDLE: model.subtitle = L"未开始"; break;
    case HOSTING: model.subtitle = L"你是主持人 · 成员连接本机 IP 和端口即可加入"; break;
    case JOINING: model.subtitle = L"正在连接……"; break;
    case JOINED: model.subtitle = L"已进入群聊室"; break;
    }
    if (mode_ == IDLE) {
        if (!g || !g->isMember(me().id())) {
            model.note(L"只有群成员才能使用群聊室。");
            return;
        }
        if (lines_.empty()) {
            model.text(L"实时群聊室是多人同时在线的 TCP 聊天：一位成员开设群聊室（服务端，用 select 同时照看多个连接），"
                       L"其他成员加入（客户端）。任何人发言，其他所有人都能立即看到；有人进出时全体提示；聊天内容自动存入群聊记录。\n"
                       L"本机演示：再打开几个程序窗口（图形版或控制台版都可以），登录本群的其他成员，一个开设，其他加入，IP 填 127.0.0.1。");
        }
        for (const auto& line : lines_) model.chat(line.first, static_cast<PageModelYD::ChatKindYD>(line.second));
        model.actions({ PageModelYD::button(L"开设群聊室", ACT_HOST, "", PageModelYD::PRIMARY), PageModelYD::button(L"加入群聊室", ACT_JOIN),
                        PageModelYD::button(L"返回群聊", ACT_CHAT) });
        return;
    }
    if (mode_ == JOINING) {
        model.note(L"正在连接群聊室并核对身份……");
        model.actions({ PageModelYD::button(L"取消", ACT_END, "", PageModelYD::DANGER) });
        return;
    }
    if (mode_ == HOSTING) {
        std::wstring online;
        std::vector<std::string> names = server_->online();
        for (size_t i = 0; i < names.size(); ++i) online += (i ? L"、" : L"") + GuiTextYD::wide(names[i]) + (i == 0 ? L"（主持人）" : L"");
        model.note(L"在线 " + std::to_wstring(names.size()) + L" 人：" + online);
    }
    model.headerButtons.push_back(PageModelYD::button(L"在线成员", ACT_WHO));
    model.headerButtons.push_back(PageModelYD::button(mode_ == HOSTING ? L"关闭群聊室" : L"离开", ACT_END, "", PageModelYD::DANGER));
    for (const auto& line : lines_) model.chat(line.first, static_cast<PageModelYD::ChatKindYD>(line.second));
    model.composer = true;
    model.stickBottom = true;
}

bool ChatRoomPageYD::poll() {
    const size_t before = lines_.size();
    if (mode_ == HOSTING) {
        server_->poll(0);   // 不等待：只处理已经到达的连接和消息
    } else if (mode_ == JOINING || mode_ == JOINED) {
        std::vector<std::string> received = link_->poll();
        if (mode_ == JOINING) {
            if (link_->state() == ChatLinkYD::CONNECTED && !helloSent_) {   // 连上后先申请进入
                link_->send("HELLO " + ServiceTypeHelperYD::code(me().type()) + " " + std::to_string(groupNo_) + " " + me().id());
                helloSent_ = true;
                helloAt_ = GetTickCount64();
            }
            if (!received.empty()) {
                const std::string reply = received.front();
                received.erase(received.begin());
                if (reply.compare(0, 3, "OK ") == 0) {
                    mode_ = JOINED;
                    lines_.push_back({ GuiTextYD::wide(reply.substr(3)), PageModelYD::CHAT_SYSTEM });
                } else {
                    end(reply.compare(0, 3, "NO ") == 0 ? "无法进入：" + reply.substr(3) : "没有收到群聊室的应答，请确认对方开设的是本群的群聊室。");
                }
            } else if (helloSent_ && GetTickCount64() - helloAt_ > 5000) {   // 连到的不是群聊室（例如点对点聊天的服务端）
                end("没有收到群聊室的应答，请确认对方开设的是本群的群聊室。");
            }
        }
        if (mode_ == JOINED) {
            for (const std::string& line : received) addLine(line);
        }
        if (link_ && link_->state() == ChatLinkYD::CLOSED) end(mode_ == JOINED ? "与群聊室的连接已断开。" : link_->error());
    }
    const bool changed = lines_.size() != before || lines_.size() != shownLines_;
    shownLines_ = lines_.size();
    return changed;
}

void ChatRoomPageYD::onCompose(const std::string& text) {
    if (mode_ == HOSTING) {
        GroupYD* g = platform().groups().find(me().type(), groupNo_);
        if (g && !g->canSpeak(me().id())) {   // 业务层还会再检查一次
            lines_.push_back({ L"你已被禁言，不能在本群发言。", PageModelYD::CHAT_SYSTEM });
            return;
        }
        server_->say(text);
    } else if (mode_ == JOINED) {
        if (!link_->send("MSG " + text)) return;
    } else {
        return;
    }
    lines_.push_back({ GuiTextYD::wide(text), PageModelYD::CHAT_MINE });
}

void ChatRoomPageYD::onAction(int action, const std::string&) {
    switch (action) {
    case ACT_CHAT:
        main().openChat(ConversationYD::groupChat(groupNo_));
        break;
    case ACT_WHO:
        if (mode_ == HOSTING) server_->say("/who");
        else if (mode_ == JOINED) link_->send("WHO");
        break;
    case ACT_END:
        end(mode_ == HOSTING ? "群聊室已关闭。" : "已离开群聊室。");
        break;
    case ACT_HOST: {
        auto dialog = std::make_unique<DialogYD>(window_, L"开设实时群聊室");
        dialog->field(L"端口号（1024-65535）", L"9999").primary(L"开设", [this](DialogYD& d) {
            unsigned short port = 0;
            if (!readPort(d, 0, port)) return false;
            GroupYD* g = platform().groups().find(me().type(), groupNo_);
            if (!g) return true;
            auto server = std::make_unique<ChatRoomServerYD>(platform(), *g, me(), [this](const std::string& text) { addLine(text); });
            std::string error;
            if (!server->start(port, error)) {
                d.setError(GuiTextYD::wide(error));
                return false;
            }
            server_ = std::move(server);
            mode_ = HOSTING;
            lines_.clear();
            lines_.push_back({ L"群聊室已开设，端口 " + std::to_wstring(port) + L"。其他成员“加入群聊室”时填本机 IP（本机演示 127.0.0.1）和这个端口。",
                               PageModelYD::CHAT_SYSTEM });
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    case ACT_JOIN: {
        auto dialog = std::make_unique<DialogYD>(window_, L"加入实时群聊室");
        dialog->field(L"群聊室所在电脑的 IP", L"127.0.0.1").field(L"端口号", L"9999").primary(L"加入", [this](DialogYD& d) {
            unsigned short port = 0;
            if (!readPort(d, 1, port)) return false;
            auto link = std::make_unique<ChatLinkYD>();
            std::string error;
            if (!link->connect(d.text(0), port, error)) {
                d.setError(GuiTextYD::wide(error));
                return false;
            }
            link_ = std::move(link);
            helloSent_ = false;
            mode_ = JOINING;
            lines_.clear();
            return true;
        });
        window_.openDialog(std::move(dialog));
        break;
    }
    }
}
