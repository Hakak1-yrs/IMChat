#pragma once
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include "MessageStore.h"

struct sqlite3;
struct sqlite3_stmt;

// 消息存储的 SQLite 实现（接口和语义见 MessageStore），服务端用 --storage sqlite 选用。
//
// 表结构：
//   messages(seq 主键, service, is_group, target, sender, time, text, revoked_by)
//       索引 (service, is_group, target, seq)：会话、群的历史；(service, sender, seq)：某人最近发的消息
//   records(line 自增主键, kind, seq, service, fields, time, text)   事件记录，line 表示先后顺序
//   read_marks(key 主键, seq)                                        已读位置，只增不减（UPSERT 取较大值）
//   meta(key 主键, value)                                            last_seq：已分配的最大序号
//
// - 序号由 meta 中的 last_seq 分配，与插入放在同一个事务里：消息被清除后序号也不会被重复使用（客户端靠序号同步）；
// - WAL 模式 + synchronous=NORMAL：提交不必每次等磁盘刷新，崩溃时最多丢失最后几个事务，数据库不会损坏；
// - 常用语句预编译后缓存起来重复使用；
// - 第一次打开（数据库还没有数据）时，如果有旧的 messages.txt，就在一个事务里整体导入，序号、已读位置、撤回都保持不变。
//
// 与文本日志不同，SQLite 文件只给一个进程（服务端）使用；单机模式的多个窗口共享数据仍然用 MessageCenter
class SqliteMessageStore : public MessageStore {
public:
    explicit SqliteMessageStore(const std::string& dbPath, const std::string& legacyLogPath = "");
    ~SqliteMessageStore() override;
    SqliteMessageStore(const SqliteMessageStore&) = delete;
    SqliteMessageStore& operator=(const SqliteMessageStore&) = delete;

    bool isOpen() const;
    int imported() const;   // 打开时从旧日志导入的消息条数

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
    bool lastFrom(ServiceType service, const std::string& sender, Message& out) const override;
    std::map<std::string, int> unreadPrivate(ServiceType service, const std::string& me) const override;
    std::map<int, int> unreadGroups(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const override;
    bool appendRecord(char kind, ServiceType service, const std::vector<std::string>& fields, bool timed,
                      const std::string& text = "") override;
    std::vector<Record> records(const std::string& kinds) const override;
    std::vector<Record> recordsAfter(const std::string& kinds, int afterLine) const override;
    int readMark(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader) const override;
    void markRead(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader, int seq) override;
    bool setCommitScheduler(Scheduler schedule) override;   // schedule 只能排队，不能立即执行任务
    void commitPending() override;

private:
    class Query;   // 预编译语句的一次使用：绑定参数、逐行读取，结束时自动 reset
    sqlite3_stmt* statement(const std::string& sql) const;   // 预编译并缓存
    bool exec(const char* sql) const;
    bool postMessage(ServiceType service, bool group, const std::string& target, const std::string& from, const std::string& text);
    int allocateSeq();   // 在事务里调用：分配下一个序号并记入 meta（批量模式下提交时才写 meta）
    bool beginWrite();   // 开始一次写入：批量模式下并入本轮的事务（第一次写入时打开并安排提交），否则单独开事务
    bool endWrite(bool ok, int seqBefore);   // 结束一次写入：非批量模式下提交（失败则回滚）
    void commitBatchLocked();
    int readMarkLocked(const std::string& key) const;
    std::vector<Message> collect(Query& query) const;
    void importLog(const std::string& path);
    void checkpointLoop(const std::string& dbPath);   // 后台线程：每 200 毫秒做一次被动检查点

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
    mutable std::unordered_map<std::string, sqlite3_stmt*> statements_;
    int lastSeq_ = 0;
    int imported_ = 0;
    Scheduler scheduler_;      // 批量提交：安排提交任务（为空表示每次写入单独提交）
    bool batchOpen_ = false;   // 本轮的事务已经打开
    bool seqDirty_ = false;    // 有新分配的序号还没写入 meta
    std::thread checkpointer_;
    std::mutex checkpointMutex_;
    std::condition_variable checkpointWake_;
    bool stopping_ = false;    // 受 checkpointMutex_ 保护
};
