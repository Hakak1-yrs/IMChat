#pragma once
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "ChatLog.h"
#include "DirectLink.h"
#include "Page.h"
#include "ServiceType.h"

class RemoteSession;

// 联网模式下的功能页。单机版里直接操作本地数据的几项功能——个人中心、跨服务好友、实时群聊室、点对点聊天，
// 以及平台概况、开通新服务、注册——联网后改由服务端完成（docs/PROTOCOL.md 的通用请求），页面的样子和单机版一致。
// 服务端的结果晚于页面显示到达：页面持有一个“存活标记”，结果到达时页面已经关了就丢掉

// 个人中心：资料（多态 showInfo 的输出）、修改昵称 / 所在地 / 出生日期 / 密码、绑定 QQ、已开通的服务、开通 / 关闭服务、注销
class NetAccountPage : public Page {
public:
    explicit NetAccountPage(GuiWindow& window);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    std::string key() const override;

private:
    enum Action { ACT_NICKNAME = 1, ACT_LOCATION, ACT_BIRTHDAY, ACT_PASSWORD, ACT_BIND, ACT_SWITCH, ACT_OPEN_SERVICE, ACT_CLOSE_SERVICE, ACT_DEREGISTER };
    void load();
    void update(const char* field, const std::wstring& title, const std::wstring& label, const std::string& current);

    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    nlohmann::json data_;
    std::string error_;
};

// 跨服务好友：依据本人在另一个服务中的好友，推荐添加、列出共同好友
class NetCrossFriendPage : public Page {
public:
    explicit NetCrossFriendPage(GuiWindow& window);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;

private:
    enum Action { ACT_SOURCE = 1, ACT_ADD, ACT_ACCOUNT };
    void load();

    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    std::string source_;
    nlohmann::json data_;
    std::string error_;
};

// 实时群聊室：由服务端主持，在线的群成员进来即时聊天，内容不保存。断线重连后自动重新进入
class NetRoomPage : public Page {
public:
    NetRoomPage(GuiWindow& window, int groupNo);
    ~NetRoomPage() override;
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    void onCompose(const std::string& text) override;
    bool poll() override;
    std::string key() const override;
    int groupNo() const;
    bool active() const;                          // 在群聊室里（离开后可以换一个群）
    void onEvent(const nlohmann::json& event);   // 推送 ROOM_EVENT

private:
    enum Action { ACT_MEMBERS = 1, ACT_LEAVE, ACT_JOIN };
    void join();
    RemoteSession* session();

    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    int groupNo_;
    ServiceType type_;
    std::string name_;
    bool joined_ = false;
    bool rejoin_ = false;   // 断线了：连上后重新进入
    std::vector<std::pair<std::string, std::string>> members_;   // 账号、昵称
    std::vector<std::pair<std::wstring, int>> lines_;
    bool changed_ = false;
};

// 点对点聊天（联网版）：服务端只转发信令，聊天内容直接在两台设备之间传送。
// 电脑这边总是开端口等待（DirectLink），对方是电脑就用 TCP 连过来，是浏览器就用 WebSocket；
// 8 秒内没有直连成功，改由服务端中转（不保存）。收发内容写入本机的聊天记录（data\chat_log.txt），和单机版一样可以查询
class NetP2PPage : public Page {
public:
    explicit NetP2PPage(GuiWindow& window);
    ~NetP2PPage() override;
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    void onCompose(const std::string& text) override;
    bool poll() override;

    bool busy() const;                // 正在邀请、连接或聊天
    void invite(RemoteSession& session, const std::string& peer);
    // 接受 from 的邀请（MainView 弹窗问过用户之后调用）
    void accept(RemoteSession& session, const std::string& from, const std::string& nickname, const nlohmann::json& data);
    void onSignal(RemoteSession& session, const nlohmann::json& signal);   // 推送 P2P_SIGNAL（邀请以外的）

private:
    enum State { IDLE, INVITING, CONNECTING, DIRECT, RELAY, ENDED };
    enum Action { ACT_INVITE = 1, ACT_STOP, ACT_RECENT, ACT_SEARCH, ACT_AGAIN };
    void signal(const nlohmann::json& data);
    nlohmann::json hostInfo();        // 开端口等待，返回 {ips, port, key}；开不了时为空
    void useRelay(const std::string& reason);
    void end(const std::string& reason, bool notify);
    void addLine(const std::string& text, int kind);

    std::shared_ptr<int> alive_ = std::make_shared<int>(0);
    DirectLink link_;
    ChatLog log_;
    State state_ = IDLE;
    std::string id_;                  // 会话编号
    std::string peer_, peerName_;
    ServiceType type_ = ServiceType::QQ;   // 用哪个服务的连接发信令
    unsigned long long deadline_ = 0;      // 邀请没人回应 / 直连没成功的期限
    std::vector<std::pair<std::wstring, int>> lines_;
    std::vector<std::string> history_;
    std::wstring historyTitle_;
    bool changed_ = false;
};

// 联网模式的几个对话框
class NetTools {
public:
    static void showOverview(GuiWindow& window);
    static void openServiceDialog(GuiWindow& window);
    // 注册新用户：还没有登录，临时连一下服务端（server 是登录页上填的“地址:端口”）
    static void registerDialog(GuiWindow& window, const std::string& server);
};
