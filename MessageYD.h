#pragma once
#include <string>
#include "ServiceTypeYD.h"

// 一条消息：好友私信或群消息。由 MessageCenterYD 从消息文件中解析得到
class MessageYD {
public:
    MessageYD(int seq, ServiceTypeYD service, bool isGroup, const std::string& target, const std::string& sender,
              const std::string& time, const std::string& text);

    int seq() const;                     // 在消息文件中的序号（从 1 开始递增），用来记录“已读到哪一条”
    ServiceTypeYD service() const;
    bool isGroup() const;
    const std::string& target() const;   // 私信：接收者账号；群消息：群号
    const std::string& sender() const;
    const std::string& time() const;     // yyyy-mm-dd hh:mm:ss
    const std::string& text() const;
    bool isSystem() const;               // 群里的系统提示（如“A 邀请 B 加入了群聊”），发送者记为 "#"
    bool revoked() const;
    const std::string& revokedBy() const;   // 撤回者账号（本人或群主、管理员）
    void markRevoked(const std::string& by);

    static const char* systemSender();   // "#"

private:
    int seq_;
    ServiceTypeYD service_;
    bool isGroup_;
    std::string target_;
    std::string sender_;
    std::string time_;
    std::string text_;
    std::string revokedBy_;
};
