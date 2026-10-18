#pragma once
#include <memory>
#include <vector>
#include "ChatLinkYD.h"
#include "ChatRoomServerYD.h"
#include "PageYD.h"

// 实时群聊室（对应控制台“群聊天 → 3. 开设实时群聊室 / 4. 加入实时群聊室”）：
// 开设时使用与控制台版相同的 ChatRoomServerYD（在界面定时器中轮询），加入时使用 ChatLinkYD 按同样的协议通信，
// 所以图形版和控制台版的成员可以在同一个群聊室里聊天。页面对象由主界面长期保存，切换页面不会中断群聊
class ChatRoomPageYD : public PageYD {
public:
    ChatRoomPageYD(GuiWindowYD& window, int groupNo);
    ~ChatRoomPageYD() override;
    void build(PageModelYD& model) override;
    void onAction(int action, const std::string& arg) override;
    void onCompose(const std::string& text) override;
    bool poll() override;
    std::string key() const override;
    bool active() const;   // 正在开设或已加入
    int groupNo() const;

private:
    enum ModeYD { IDLE, HOSTING, JOINING, JOINED };
    enum ActionYD { ACT_HOST = 1, ACT_JOIN, ACT_WHO, ACT_END, ACT_CHAT };
    void addLine(const std::string& text);              // 群聊室发来的一行：系统消息居中，其他人的消息在左侧
    void end(const std::string& reason);

    int groupNo_;
    ModeYD mode_ = IDLE;
    std::unique_ptr<ChatRoomServerYD> server_;
    std::unique_ptr<ChatLinkYD> link_;
    bool helloSent_ = false;
    unsigned long long helloAt_ = 0;
    std::vector<std::pair<std::wstring, int>> lines_;
    size_t shownLines_ = 0;
};
