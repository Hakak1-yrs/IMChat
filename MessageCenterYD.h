#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "MessageYD.h"
#include "RecordYD.h"

// 消息中心：私信、群消息以及各种社交事件的存储与查询，数据保存在 data\messages.txt。
//
// 1. 文件只追加、不改写：两个程序窗口可以同时写，互不覆盖（与 ChatLogYD 相同，用 FILE_APPEND_DATA 方式写）；
// 2. 每次查询都重新读文件，另一个窗口刚写入的内容这边马上就能看到；
// 3. 记录格式（每行一条，第一个字母是类型，第二项是服务）：
//      M 服务 P/G 接收者或群号 发送者 日期 时间 内容      一条消息（P 私信，G 群消息；发送者为 # 表示群系统提示）
//      W 服务 消息序号 撤回者 日期 时间                     撤回一条消息
//      R 服务 范围 对象 读者 序号                            读者已读到第几条（范围 P 私信、G 群、O 朋友圈通知）
//      C 服务 A/G 账号或群号                                 账号关闭或群解散：此前与之有关的记录全部作废
//      F 服务 对方 申请人 备注 日期 时间 验证消息            好友申请
//      V 服务 申请序号 A/X 处理人 备注                       同意（A）或拒绝（X）好友申请
//      D 服务 删除者 被删除者                                单向删除好友
//      B 服务 拉黑者 对方 1/0                                加入或移出黑名单
//      S 服务 账号 verify 1/0                                加我为好友时是否需要验证
//      P 服务 作者 可见范围 日期 时间 内容                   发表一条朋友圈动态
//      L 服务 动态序号 点赞者 1/0                            点赞或取消点赞
//      K 服务 动态序号 评论者 回复对象 日期 时间 内容        评论（回复对象为 - 表示直接评论动态）
//      Y 服务 动态序号                                       删除动态
//      Z 服务 评论序号                                       删除评论
//    M、F、P、L、K 记录各占一个序号（按出现顺序从 1 编起），文件只追加，序号永远不会变
class MessageCenterYD {
public:
    explicit MessageCenterYD(const std::string& filePath);

    // ---- 消息（内容由调用者检查过；返回 false 表示写文件失败）----
    bool postPrivate(ServiceTypeYD service, const std::string& from, const std::string& to, const std::string& text);
    bool postGroup(ServiceTypeYD service, int groupNo, const std::string& from, const std::string& text);
    bool revoke(ServiceTypeYD service, int seq, const std::string& by);
    void clearAccount(ServiceTypeYD service, const std::string& id);
    void clearGroup(ServiceTypeYD service, int groupNo);

    // ---- 消息查询（按时间先后排列，撤回的消息带有撤回标记）----
    std::vector<MessageYD> conversation(ServiceTypeYD service, const std::string& a, const std::string& b) const;
    std::vector<MessageYD> groupHistory(ServiceTypeYD service, int groupNo) const;
    // 与 me 有关的全部消息：me 收发的私信，以及 groupNos 这些群中的群消息（只读一次文件，图形界面的会话列表使用）
    std::vector<MessageYD> messagesFor(ServiceTypeYD service, const std::string& me, const std::vector<int>& groupNos) const;
    bool findMessage(ServiceTypeYD service, int seq, MessageYD& out) const;
    // 未读：不计系统提示、撤回的消息和自己发的消息
    std::map<std::string, int> unreadPrivate(ServiceTypeYD service, const std::string& me) const;   // 发送者 → 条数
    std::map<int, int> unreadGroups(ServiceTypeYD service, const std::string& me, const std::vector<int>& groupNos) const;
    void markPrivateRead(ServiceTypeYD service, const std::string& me, const std::string& peer, int seq);
    void markGroupRead(ServiceTypeYD service, int groupNo, const std::string& me, int seq);
    int groupReadMark(ServiceTypeYD service, int groupNo, const std::string& me) const;

    // ---- 其他事件记录（好友申请、黑名单、朋友圈……），含义由 PlatformYD 解释 ----
    bool appendRecord(char kind, ServiceTypeYD service, const std::vector<std::string>& fields, bool timed,
                      const std::string& text = "");
    std::vector<RecordYD> records(const std::string& kinds) const;   // 指定类型的有效记录，按先后排列
    int readMark(ServiceTypeYD service, const std::string& scope, const std::string& target, const std::string& reader) const;
    void markRead(ServiceTypeYD service, const std::string& scope, const std::string& target, const std::string& reader, int seq);

private:
    struct StateYD {
        std::vector<MessageYD> messages;        // 仍然有效的消息
        std::vector<RecordYD> records;          // 仍然有效的事件记录
        std::map<std::string, int> readMarks;   // "服务 范围 对象 读者" → 已读到的序号
    };
    StateYD load() const;
    static int markOf(const StateYD& state, const std::string& key);
    bool append(const std::string& line);

    std::string filePath_;
    mutable std::mutex mutex_;   // 群聊室的网络线程和主线程可能同时写消息
};
