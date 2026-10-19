#pragma once
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "Message.h"
#include "Record.h"

// 消息存储的接口：私信、群消息以及各种社交事件（好友申请、黑名单、朋友圈……）的保存与查询。
// 两种实现，语义完全相同（同一套一致性测试分别验证）：
// - MessageCenter：只追加的文本日志 messages.txt，增量读取。多个程序窗口、多个进程可以共享同一个文件，
//   单机模式（图形界面、控制台）使用；
// - SqliteMessageStore：SQLite 数据库 messages.db（WAL 模式、按查询建索引），服务端可以选用（--storage sqlite）。
//
// 序号：消息和需要被引用的事件记录（好友申请、动态、点赞、评论）共用一个递增的序号，一经分配永远不变，
// 客户端用它排序、作为同步的位置；记录的 line 表示先后顺序。
// 账号注销、群解散后，此前与之有关的消息和记录作废（查不到），之后的不受影响
class MessageStore {
public:
    virtual ~MessageStore() = default;

    // ---- 消息（内容由调用者检查过；返回 false 表示写入失败）----
    virtual bool postPrivate(ServiceType service, const std::string& from, const std::string& to, const std::string& text) = 0;
    virtual bool postGroup(ServiceType service, int groupNo, const std::string& from, const std::string& text) = 0;
    virtual bool revoke(ServiceType service, int seq, const std::string& by) = 0;
    virtual void clearAccount(ServiceType service, const std::string& id) = 0;
    virtual void clearGroup(ServiceType service, int groupNo) = 0;

    // ---- 消息查询（按序号先后排列，撤回的消息带有撤回标记）----
    virtual std::vector<Message> conversation(ServiceType service, const std::string& a, const std::string& b) const = 0;
    virtual std::vector<Message> groupHistory(ServiceType service, int groupNo) const = 0;
    // 与 me 有关的全部消息：me 收发的私信，以及 groupNos 这些群中的群消息
    virtual std::vector<Message> messagesFor(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const = 0;
    // 同上，但只要序号大于 afterSeq 的前 limit 条（limit 为 0 表示不限）：客户端增量同步用，
    // 不必每次把这个人的全部历史读出来再丢掉已有的部分
    virtual std::vector<Message> messagesAfter(ServiceType service, const std::string& me, const std::vector<int>& groupNos,
                                               int afterSeq, std::size_t limit) const = 0;
    virtual bool findMessage(ServiceType service, int seq, Message& out) const = 0;
    // 已经分配出去的最大序号：之后保存的消息、记录，序号一定比它大
    virtual int lastSeq() const = 0;
    // sender 最近发的一条消息（刚保存的那条：拿到它的序号和时间）
    virtual bool lastFrom(ServiceType service, const std::string& sender, Message& out) const = 0;
    // 未读：不计系统提示、撤回的消息和自己发的消息
    virtual std::map<std::string, int> unreadPrivate(ServiceType service, const std::string& me) const = 0;   // 发送者 → 条数
    virtual std::map<int, int> unreadGroups(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const = 0;
    void markPrivateRead(ServiceType service, const std::string& me, const std::string& peer, int seq);
    void markGroupRead(ServiceType service, int groupNo, const std::string& me, int seq);
    int groupReadMark(ServiceType service, int groupNo, const std::string& me) const;

    // ---- 其他事件记录，含义由 Platform 解释 ----
    virtual bool appendRecord(char kind, ServiceType service, const std::vector<std::string>& fields, bool timed,
                              const std::string& text = "") = 0;
    virtual std::vector<Record> records(const std::string& kinds) const = 0;   // 指定类型的有效记录，按先后排列
    // 同上，只要行号大于 afterLine 的：增量应用新记录用（每发一条私信都要先应用新的好友、拉黑记录），
    // 不必每次把全部记录读出来再跳过已经应用过的
    virtual std::vector<Record> recordsAfter(const std::string& kinds, int afterLine) const = 0;
    // 已读位置：范围 P 私信（对象是对方）、G 群（对象是群号）、O 朋友圈通知
    virtual int readMark(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader) const = 0;
    virtual void markRead(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader,
                          int seq) = 0;   // 只会前进，不会后退

    // ---- 批量提交（group commit）----
    // 设置后，存储可以把一段时间内的多次写入合并成一次提交：第一次写入时调用 schedule 安排一个任务，
    // 任务执行时提交。服务端把它安排在事件循环本轮的待办任务里——排在本轮所有回执之前，
    // 所以回执发出时数据一定已经提交（“收到回执 = 已保存”不变）。传空函数表示关闭，并立即提交未提交的写入。
    // 文本日志每次写入本来就直接追加到文件，默认什么也不做
    // 返回 true 表示这个存储确实会批量提交（调用者据此保证：先提交、再发回执）
    using Scheduler = std::function<void(std::function<void()> task)>;
    virtual bool setCommitScheduler(Scheduler schedule);
    virtual void commitPending();   // 立即提交本轮还没提交的写入（发回执之前调用）
};
