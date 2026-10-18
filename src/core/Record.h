#pragma once
#include <string>
#include <vector>
#include "ServiceType.h"

// 消息文件中的一条“事件记录”：好友申请与处理、单向删除、黑名单、验证设置、朋友圈动态与点赞评论……
// MessageCenter 只负责按格式保存和读取，具体含义由 Platform 解释。
// 简单的数据类：字段公开，便于按记录类型取用
class Record {
public:
    const std::string& field(size_t i) const;   // 越界时返回空字符串

    char kind = 0;                  // 记录类型，见 MessageCenter.h 中的说明
    int seq = 0;                    // 需要被其他记录引用的记录（好友申请、动态、评论、点赞）才有序号
    int line = 0;                   // 在文件中的行号，表示先后顺序
    ServiceType service = ServiceType::QQ;
    std::vector<std::string> fields;
    std::string time;               // yyyy-mm-dd hh:mm:ss，没有时间的记录为空
    std::string text;               // 行尾的整段文字（验证消息、动态内容、评论内容）
};
