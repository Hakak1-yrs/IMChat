#pragma once
#include <string>
#include "ServiceTypeYD.h"

// 一条好友申请（对应微信“新的朋友”中的一行）。由 PlatformYD 根据消息文件中的 F、V 记录整理得到。
// 简单的数据类：字段公开
class FriendRequestYD {
public:
    enum StatusYD { PENDING, ACCEPTED, REJECTED };

    std::string statusText() const;   // 等待验证 / 已添加 / 已拒绝

    int seq = 0;
    ServiceTypeYD service = ServiceTypeYD::QQ;
    std::string from;        // 申请人
    std::string to;          // 对方
    std::string remark;      // 申请人给对方设置的备注
    std::string greeting;    // 验证消息
    std::string time;
    StatusYD status = PENDING;
};
