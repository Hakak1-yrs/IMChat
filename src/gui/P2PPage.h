#pragma once
#include <memory>
#include <vector>
#include "ChatLink.h"
#include "ChatLog.h"
#include "Page.h"

// QQ 点对点聊天（对应控制台“用户菜单 → 6. QQ点对点聊天（TCP）”）：
// 1 等待好友连接（服务端）  2 连接好友（客户端）  3 查看聊天记录（可按关键字查询）。
// 收发格式、聊天记录文件都与控制台版相同，图形版和控制台版之间可以互相聊天。
// 页面对象由主界面长期保存，切换到别的页面时连接不会断开
class P2PPage : public Page {
public:
    explicit P2PPage(GuiWindow& window);
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    void onCompose(const std::string& text) override;
    bool poll() override;
    bool active() const;   // 正在等待或已连接

private:
    enum Action { ACT_LISTEN = 1, ACT_CONNECT, ACT_STOP, ACT_RESET, ACT_RECENT, ACT_SEARCH, ACT_SWITCH_QQ };
    void addLine(const std::string& text, int kind);
    std::string peerIp() const;

    ChatLink link_;
    ChatLog log_;
    ChatLink::State lastState_ = ChatLink::IDLE;
    std::vector<std::pair<std::wstring, int>> lines_;   // 本次聊天的消息（文字、气泡类型）
    std::vector<std::string> history_;                  // 聊天记录查询结果
    std::string nickname_;                              // 开始聊天时的 QQ 昵称（聊天中切换到其他服务也不变）
    std::wstring historyTitle_;
};
