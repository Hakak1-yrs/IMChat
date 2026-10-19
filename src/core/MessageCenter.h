#pragma once
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "MessageStore.h"

// 消息存储的文本日志实现（接口见 MessageStore）：私信、群消息以及各种社交事件，数据保存在 data\messages.txt。
//
// 1. 文件只追加、不改写：两个程序窗口可以同时写，互不覆盖（与 ChatLog 相同，用 FILE_APPEND_DATA 方式写）；
// 2. 解析结果缓存在内存里。每次查询前看一眼文件大小，只读取、解析上次之后新追加的部分（增量读取）：
//    另一个窗口刚写入的内容这边马上就能看到，又不必每次都把整个文件重新解析一遍。
//    （压测发现原来每发一条消息都要完整解析两遍文件，消息一多就越来越慢，见 docs/BENCHMARK.md）
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
class MessageCenter : public MessageStore {
public:
    explicit MessageCenter(const std::string& filePath);
    ~MessageCenter() override;
    MessageCenter(const MessageCenter&) = delete;
    MessageCenter& operator=(const MessageCenter&) = delete;

    bool postPrivate(ServiceType service, const std::string& from, const std::string& to, const std::string& text) override;
    bool postGroup(ServiceType service, int groupNo, const std::string& from, const std::string& text) override;
    bool revoke(ServiceType service, int seq, const std::string& by) override;
    void clearAccount(ServiceType service, const std::string& id) override;
    void clearGroup(ServiceType service, int groupNo) override;
    std::vector<Message> conversation(ServiceType service, const std::string& a, const std::string& b) const override;
    std::vector<Message> groupHistory(ServiceType service, int groupNo) const override;
    std::vector<Message> messagesFor(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const override;
    std::vector<Message> messagesAfter(ServiceType service, const std::string& me, const std::vector<int>& groupNos,
                                       int afterSeq, std::size_t limit) const override;
    int lastSeq() const override;
    bool findMessage(ServiceType service, int seq, Message& out) const override;
    bool lastFrom(ServiceType service, const std::string& sender, Message& out) const override;   // 从最新的往前找
    std::map<std::string, int> unreadPrivate(ServiceType service, const std::string& me) const override;
    std::map<int, int> unreadGroups(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const override;
    bool appendRecord(char kind, ServiceType service, const std::vector<std::string>& fields, bool timed,
                      const std::string& text = "") override;
    std::vector<Record> records(const std::string& kinds) const override;
    std::vector<Record> recordsAfter(const std::string& kinds, int afterLine) const override;
    int recordClears() const override;
    int lastRecordLine() const override;   // 已解析的行数（记录的行号就是它在文件里的行号）
    int readMark(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader) const override;
    void markRead(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader, int seq) override;

    // 整个日志解析后的结果（迁移到 SQLite 时使用）：仍然有效的消息、记录（带原来的行号）、已读位置、已分配的最大序号
    struct Snapshot {
        std::vector<Message> messages;
        std::vector<Record> records;
        std::map<std::string, int> readMarks;
        int lastSeq = 0;
    };
    Snapshot snapshot() const;

private:
    // 已经解析过的部分：仍然有效的消息和事件记录、已读位置，以及继续解析需要的状态
    struct State {
        std::map<int, Message> messages;          // 序号 → 消息（按序号有序）
        std::vector<Record> records;
        std::map<std::string, int> readMarks;     // "服务 范围 对象 读者" → 已读到的序号
        std::map<std::string, std::string> revokedBy;   // "服务 序号" → 撤回者
        int count = 0;                            // 已分配的最大序号
        int recordClears = 0;                     // 账号注销清除记录的次数
        int lineNo = 0;                           // 已解析的行数
        unsigned long long offset = 0;            // 已解析到文件的哪个字节（之后都是新追加的）
    };
    const State& refresh() const;   // 调用者持有 mutex_：读取新追加的内容，返回最新状态
    void parseLine(State& state, const std::string& line) const;
    void clear(State& state, const std::string& code, const std::string& scope, const std::string& target) const;
    bool openFile() const;
    bool fileSize(unsigned long long& size) const;
    std::size_t readAt(unsigned long long position, char* buffer, std::size_t size) const;   // 返回读到的字节数
    static int markOf(const State& state, const std::string& key);
    bool append(const std::string& line);

    std::string filePath_;
    mutable std::mutex mutex_;   // 群聊室的网络线程和主线程可能同时读写
    mutable State state_;
    // 一直打开着的文件（读 + 追加写），省去每次打开、关闭文件的开销
#ifdef _WIN32
    mutable void* file_ = nullptr;
#else
    mutable int file_ = -1;
#endif
};
