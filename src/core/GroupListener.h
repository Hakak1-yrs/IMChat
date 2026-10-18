#pragma once
#include <string>

class Group;

// 群里发生的、需要告诉全体成员的事情（对应微信群聊天记录中的灰色系统提示）
enum class GroupEvent {
    INVITED,          // actor 邀请 target 加入了群聊
    APPROVED,         // actor 同意 target 入群
    CLAIMED,          // actor 认领了无主群
    QUIT,             // actor 退出了群聊
    KICKED,           // actor 将 target 移出群聊
    RENAMED,          // actor 修改群名为 target
    TRANSFERRED,      // actor 将群主转让给 target
    MODE_SWITCHED,    // actor 将管理模式切换为 target
    MUTED,            // actor 将 target 禁言
    UNMUTED,          // actor 解除了 target 的禁言
    ADMIN_SET,        // actor 将 target 设为管理员
    ADMIN_UNSET       // actor 取消了 target 的管理员身份
};

// 群事件监听者接口（观察者模式）：Group 的操作成功后通知监听者，由 Platform 把它写成群里的系统提示。
// 群类只负责“发生了什么”，不关心提示怎么显示、存在哪里
class GroupListener {
public:
    virtual ~GroupListener();
    virtual void onGroupEvent(const Group& group, GroupEvent event, const std::string& actor,
                              const std::string& target) = 0;
};
