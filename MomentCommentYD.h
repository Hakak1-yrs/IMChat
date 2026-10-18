#pragma once
#include <string>

// 朋友圈的一条评论。replyTo 不为空时表示“回复某人”（显示为“李四 回复 张三：……”）。
// 简单的数据类：字段公开
class MomentCommentYD {
public:
    bool isReply() const;

    int seq = 0;
    std::string author;
    std::string replyTo;
    std::string time;
    std::string text;
};
