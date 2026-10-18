#pragma once
#include <vector>

// 事件记录（好友申请、黑名单、朋友圈……）的格式，两种消息存储（文本日志、SQLite）共用：
// 字段个数、是否带时间、是否带行尾文字、是否占序号，以及哪些字段是账号（账号注销后，涉及它的记录随之作废）
struct RecordSchema {
    char kind;
    int fields;
    bool timed;
    bool text;
    bool entity;                 // 占一个序号（可以被其他记录引用，如点赞引用动态）
    std::vector<int> accounts;   // 哪些字段是账号

    static const RecordSchema* of(char kind);   // 不认识的类型返回空
};
