#pragma once
#include <string>

class Group;
class Platform;
class ServiceBase;

// 实时群聊室（TCP 一对多）：一位群成员开设群聊室（服务端），其他群成员连接进来（客户端），多人同时在线聊天。
//
// 服务端：网络线程用 select 同时照看监听套接字和所有成员的连接——有人连入就核对身份（必须是本群成员），
//         收到消息就转发给其他所有人，并写入群聊记录；主线程只负责读键盘。
// 客户端：与点对点聊天相同，主线程读键盘并发送，接收线程显示别人的消息。
// 发言权限和点对点聊天不同，统一由 Platform::sendGroupMessage 判断（不是群成员、被禁言都不能发）。
//
// 通信协议（每条一行，GBK 编码）：
//   客户端 → 服务端：HELLO 服务 群号 账号（第一行，申请加入）  MSG 内容（发言）  WHO（查看在线成员）
//   服务端 → 客户端：OK 欢迎信息 / NO 拒绝原因（对 HELLO 的应答），之后每行都是一条要显示的文字
class GroupChatRoom {
public:
    GroupChatRoom(Platform& platform, Group& group, ServiceBase& me);
    ~GroupChatRoom();
    GroupChatRoom(const GroupChatRoom&) = delete;
    GroupChatRoom& operator=(const GroupChatRoom&) = delete;

    bool host(unsigned short port);                         // 开设群聊室：socket → bind → listen → select/accept
    bool join(const std::string& ip, unsigned short port);  // 加入群聊室：socket → connect → HELLO

private:
    std::string title() const;   // 如“QQ群 1001（编程交流群）”

    Platform& platform_;
    Group& group_;
    ServiceBase& me_;
    bool wsaReady_;
};
