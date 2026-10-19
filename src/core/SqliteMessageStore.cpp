#include "SqliteMessageStore.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include "Date.h"
#include "MessageCenter.h"
#include "RecordSchema.h"
#include "Util.h"

namespace {

const int kRestartFrames = 4000;       // WAL 超过这么多页（约 16 MiB）就让它从头复用
const char kFieldSeparator = '\x1f';   // 记录的各个字段拼成一列保存（字段里不会出现这个控制字符）

const char* kSchema = R"(
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS messages(
    seq        INTEGER PRIMARY KEY,
    service    TEXT    NOT NULL,
    is_group   INTEGER NOT NULL,
    target     TEXT    NOT NULL,
    sender     TEXT    NOT NULL,
    time       TEXT    NOT NULL,
    text       TEXT    NOT NULL,
    revoked_by TEXT);
CREATE INDEX IF NOT EXISTS messages_by_target ON messages(service, is_group, target, seq);
CREATE INDEX IF NOT EXISTS messages_by_sender ON messages(service, sender, seq);
CREATE TABLE IF NOT EXISTS records(
    line    INTEGER PRIMARY KEY AUTOINCREMENT,
    kind    TEXT    NOT NULL,
    seq     INTEGER NOT NULL,
    service TEXT    NOT NULL,
    fields  TEXT    NOT NULL,
    time    TEXT    NOT NULL,
    text    TEXT    NOT NULL);
CREATE TABLE IF NOT EXISTS read_marks(key TEXT PRIMARY KEY, seq INTEGER NOT NULL);
)";

const char* kMessageColumns = "SELECT seq, service, is_group, target, sender, time, text, revoked_by FROM messages ";

std::string joinFields(const std::vector<std::string>& fields) {
    std::string joined;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) joined += kFieldSeparator;
        joined += fields[i];
    }
    return joined;
}

std::vector<std::string> splitFields(const std::string& joined) {
    std::vector<std::string> fields;
    if (joined.empty()) return fields;
    size_t begin = 0;
    while (true) {
        const size_t end = joined.find(kFieldSeparator, begin);
        fields.push_back(joined.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) return fields;
        begin = end + 1;
    }
}

std::string markKey(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader) {
    return ServiceTypeHelper::code(service) + " " + scope + " " + target + " " + reader;
}

// "target IN ('1001','1002')"：群号都是整数，直接拼进语句是安全的
std::string groupList(const std::vector<int>& groupNos) {
    std::string list;
    for (int no : groupNos) list += (list.empty() ? "'" : ",'") + std::to_string(no) + "'";
    return list;
}

}  // namespace

// ======================== 预编译语句的一次使用 ========================

class SqliteMessageStore::Query {
public:
    explicit Query(sqlite3_stmt* statement) : statement_(statement) {}
    ~Query() {
        if (!statement_) return;
        sqlite3_reset(statement_);   // 放回缓存，下次接着用
        sqlite3_clear_bindings(statement_);
    }
    Query(const Query&) = delete;
    Query& operator=(const Query&) = delete;

    Query& bind(int index, const std::string& value) {
        if (statement_) sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Query& bind(int index, long long value) {
        if (statement_) sqlite3_bind_int64(statement_, index, value);
        return *this;
    }
    bool next() { return statement_ && sqlite3_step(statement_) == SQLITE_ROW; }   // 取下一行
    bool run() { return statement_ && sqlite3_step(statement_) == SQLITE_DONE; }   // 执行不返回数据的语句
    std::string text(int column) const {
        const unsigned char* value = sqlite3_column_text(statement_, column);
        return value ? std::string(reinterpret_cast<const char*>(value), static_cast<size_t>(sqlite3_column_bytes(statement_, column)))
                     : std::string();
    }
    long long integer(int column) const { return sqlite3_column_int64(statement_, column); }
    bool isNull(int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

private:
    sqlite3_stmt* statement_;
};

// ======================== 打开、建表、导入 ========================

SqliteMessageStore::SqliteMessageStore(const std::string& dbPath, const std::string& legacyLogPath) {
    std::error_code ec;
    const std::filesystem::path parent = Util::pathOf(dbPath).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    // 文件名按 UTF-8 解释（各平台相同），数据目录含中文也没问题
    if (sqlite3_open_v2(dbPath.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        sqlite3_close(db_);
        db_ = nullptr;
        return;
    }
    sqlite3_busy_timeout(db_, 5000);   // 与后台检查点短暂冲突时等待，而不是立即失败
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA cache_size=-32768");        // 页缓存 32 MiB（默认约 2 MiB）
    exec("PRAGMA mmap_size=268435456");      // 读取走内存映射，省去一次复制
    exec("PRAGMA temp_store=MEMORY");
    exec("PRAGMA journal_size_limit=33554432");   // WAL 从头复用时，文件大小截回 32 MiB 以内
    // 检查点（把 WAL 里的修改写回主库）不在写入线程里做：默认的自动检查点会在某次提交时同步执行，
    // 那一次写入就要多等几十毫秒（压测中表现为 p99 延迟的尖峰）。改由后台线程用自己的连接定期做被动检查点，
    // WAL 模式下它与写入可以同时进行
    exec("PRAGMA wal_autocheckpoint=0");
    checkpointer_ = std::thread([this, dbPath] { checkpointLoop(dbPath); });
    // 但写入一直不停时，后台检查点永远追不上最新的几页，WAL 不会从头复用、会无限增长。
    // 所以 WAL 超过阈值时，由写入方在提交后自己补做一次被动检查点：大部分页后台已经写回，这一次只剩很少的几页，
    // 做完 WAL 就全部写回了，下一次写入从 WAL 开头重新写（SQLite 内置的自动检查点也是用这个钩子实现的）
    sqlite3_wal_hook(db_, [](void*, sqlite3* db, const char* name, int frames) {
        if (frames >= kRestartFrames) sqlite3_wal_checkpoint_v2(db, name, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
        return SQLITE_OK;
    }, nullptr);
    exec(kSchema);
    std::lock_guard<std::mutex> lock(mutex_);
    bool fresh = true;
    {
        Query query(statement("SELECT value FROM meta WHERE key = 'last_seq'"));
        if (query.next()) {
            lastSeq_ = static_cast<int>(query.integer(0));
            fresh = false;
        }
    }
    if (fresh) importLog(legacyLogPath);
}

void SqliteMessageStore::checkpointLoop(const std::string& dbPath) {
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    // 连接是懒打开的：先执行一条语句让它真正打开数据库、进入 WAL 模式，否则检查点什么也不做
    sqlite3_exec(db, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    std::unique_lock<std::mutex> lock(checkpointMutex_);
    while (!stopping_) {
        checkpointWake_.wait_for(lock, std::chrono::milliseconds(200));
        if (stopping_) break;
        lock.unlock();
        // 被动检查点：不等待读写，能写回多少写回多少（大部分页的复制在这里完成）
        sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_PASSIVE, nullptr, nullptr);
        lock.lock();
    }
    sqlite3_close(db);
}

SqliteMessageStore::~SqliteMessageStore() {
    if (checkpointer_.joinable()) {
        {
            std::lock_guard<std::mutex> lock(checkpointMutex_);
            stopping_ = true;
        }
        checkpointWake_.notify_one();
        checkpointer_.join();
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        commitBatchLocked();   // 还没提交的批量写入
    }
    for (auto& entry : statements_) sqlite3_finalize(entry.second);
    if (db_) sqlite3_close(db_);
}

bool SqliteMessageStore::isOpen() const { return db_ != nullptr; }

int SqliteMessageStore::imported() const { return imported_; }

void SqliteMessageStore::importLog(const std::string& path) {
    // 新数据库：有旧的文本日志就整体导入（一个事务，要么全部导入，要么什么都没写）
    std::error_code ec;
    const bool hasLog = !path.empty() && std::filesystem::is_regular_file(Util::pathOf(path), ec);
    exec("BEGIN");
    bool ok = true;
    if (hasLog) {
        const MessageCenter::Snapshot log = MessageCenter(path).snapshot();
        for (const Message& m : log.messages) {
            Query insert(statement("INSERT INTO messages(seq, service, is_group, target, sender, time, text, revoked_by) "
                                   "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, NULLIF(?8, ''))"));
            ok = ok && insert.bind(1, m.seq()).bind(2, ServiceTypeHelper::code(m.service())).bind(3, m.isGroup() ? 1 : 0)
                           .bind(4, m.target()).bind(5, m.sender()).bind(6, m.time()).bind(7, m.text()).bind(8, m.revokedBy()).run();
        }
        for (const Record& r : log.records) {   // 保留原来的行号：之后新增的记录排在它们后面
            Query insert(statement("INSERT INTO records(line, kind, seq, service, fields, time, text) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)"));
            ok = ok && insert.bind(1, r.line).bind(2, std::string(1, r.kind)).bind(3, r.seq).bind(4, ServiceTypeHelper::code(r.service))
                           .bind(5, joinFields(r.fields)).bind(6, r.time).bind(7, r.text).run();
        }
        for (const auto& [key, seq] : log.readMarks) {
            Query insert(statement("INSERT INTO read_marks(key, seq) VALUES(?1, ?2)"));
            ok = ok && insert.bind(1, key).bind(2, seq).run();
        }
        lastSeq_ = log.lastSeq;
        imported_ = static_cast<int>(log.messages.size());
    }
    Query meta(statement("INSERT INTO meta(key, value) VALUES('last_seq', ?1)"));
    ok = ok && meta.bind(1, lastSeq_).run();
    if (!(ok && exec("COMMIT"))) {
        exec("ROLLBACK");
        lastSeq_ = 0;
        imported_ = 0;
    }
}

sqlite3_stmt* SqliteMessageStore::statement(const std::string& sql) const {
    if (!db_) return nullptr;
    auto it = statements_.find(sql);
    if (it != statements_.end()) return it->second;
    sqlite3_stmt* prepared = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &prepared, nullptr) != SQLITE_OK) return nullptr;
    statements_[sql] = prepared;
    return prepared;
}

bool SqliteMessageStore::exec(const char* sql) const { return db_ && sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }

int SqliteMessageStore::allocateSeq() {
    if (scheduler_) {   // 批量模式：序号先在内存里分配，提交时一并写入 meta
        seqDirty_ = true;
        return ++lastSeq_;
    }
    Query update(statement("UPDATE meta SET value = ?1 WHERE key = 'last_seq'"));
    if (!update.bind(1, lastSeq_ + 1).run()) return 0;
    return ++lastSeq_;
}

// ======================== 事务与批量提交 ========================

bool SqliteMessageStore::beginWrite() {
    if (!scheduler_) return exec("BEGIN");
    if (batchOpen_) return true;   // 本轮已经有一个打开的事务：这次写入并进去
    if (!exec("BEGIN")) return false;
    batchOpen_ = true;
    // 安排在本轮的待办任务里提交（只是排队，不会立即执行，所以这里持有锁没有关系）
    scheduler_([this] {
        std::lock_guard<std::mutex> lock(mutex_);
        commitBatchLocked();
    });
    return true;
}

bool SqliteMessageStore::endWrite(bool ok, int seqBefore) {
    if (scheduler_) return ok;   // 等安排好的任务统一提交；单次失败不影响同一事务里的其他写入
    if (ok && exec("COMMIT")) return true;
    exec("ROLLBACK");
    lastSeq_ = seqBefore;
    return false;
}

void SqliteMessageStore::commitBatchLocked() {
    if (!batchOpen_) return;
    if (seqDirty_) {
        Query update(statement("UPDATE meta SET value = ?1 WHERE key = 'last_seq'"));
        update.bind(1, lastSeq_).run();
        seqDirty_ = false;
    }
    exec("COMMIT");
    batchOpen_ = false;
}

bool SqliteMessageStore::setCommitScheduler(Scheduler schedule) {
    std::lock_guard<std::mutex> lock(mutex_);
    commitBatchLocked();   // 切换前把未提交的写入提交掉
    scheduler_ = std::move(schedule);
    return static_cast<bool>(scheduler_);
}

void SqliteMessageStore::commitPending() {
    std::lock_guard<std::mutex> lock(mutex_);
    commitBatchLocked();
}

// ======================== 写入 ========================

bool SqliteMessageStore::postMessage(ServiceType service, bool group, const std::string& target, const std::string& from,
                                       const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string body = Util::lineText(text);
    const int before = lastSeq_;
    if (!beginWrite()) return false;
    const int seq = allocateSeq();   // 与文本日志相同：内容为空的消息也占一个序号，只是不保存
    bool ok = seq > 0;
    if (ok && !body.empty()) {
        Query insert(statement("INSERT INTO messages(seq, service, is_group, target, sender, time, text) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)"));
        ok = insert.bind(1, seq).bind(2, ServiceTypeHelper::code(service)).bind(3, group ? 1 : 0).bind(4, target).bind(5, from)
                 .bind(6, Date::nowText()).bind(7, body).run();
    }
    return endWrite(ok, before);
}

bool SqliteMessageStore::postPrivate(ServiceType service, const std::string& from, const std::string& to, const std::string& text) {
    return postMessage(service, false, to, from, text);
}

bool SqliteMessageStore::postGroup(ServiceType service, int groupNo, const std::string& from, const std::string& text) {
    return postMessage(service, true, std::to_string(groupNo), from, text);
}

bool SqliteMessageStore::revoke(ServiceType service, int seq, const std::string& by) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!beginWrite()) return false;
    bool ok = false;
    {
        Query update(statement("UPDATE messages SET revoked_by = ?3 WHERE seq = ?1 AND service = ?2"));
        ok = update.bind(1, seq).bind(2, ServiceTypeHelper::code(service)).bind(3, by).run();
    }
    return endWrite(ok, lastSeq_);
}

void SqliteMessageStore::clearAccount(ServiceType service, const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string code = ServiceTypeHelper::code(service);
    if (!beginWrite()) return;
    {
        Query remove(statement("DELETE FROM messages WHERE service = ?1 AND is_group = 0 AND (sender = ?2 OR target = ?2)"));
        remove.bind(1, code).bind(2, id).run();
    }
    // 涉及这个账号的事件记录：哪些字段是账号由记录格式决定
    std::vector<long long> lines;
    {
        Query select(statement("SELECT line, kind, fields FROM records WHERE service = ?1"));
        select.bind(1, code);
        while (select.next()) {
            const std::string kind = select.text(1);
            const RecordSchema* schema = RecordSchema::of(kind.empty() ? '\0' : kind[0]);
            const std::vector<std::string> fields = splitFields(select.text(2));
            if (!schema) continue;
            for (int index : schema->accounts) {
                if (index < static_cast<int>(fields.size()) && fields[index] == id) {
                    lines.push_back(select.integer(0));
                    break;
                }
            }
        }
    }
    for (long long line : lines) {
        Query remove(statement("DELETE FROM records WHERE line = ?1"));
        remove.bind(1, line).run();
    }
    endWrite(true, lastSeq_);
}

void SqliteMessageStore::clearGroup(ServiceType service, int groupNo) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!beginWrite()) return;
    bool ok = false;
    {
        Query remove(statement("DELETE FROM messages WHERE service = ?1 AND is_group = 1 AND target = ?2"));
        ok = remove.bind(1, ServiceTypeHelper::code(service)).bind(2, std::to_string(groupNo)).run();
    }
    endWrite(ok, lastSeq_);
}

bool SqliteMessageStore::appendRecord(char kind, ServiceType service, const std::vector<std::string>& fields, bool timed,
                                        const std::string& text) {
    const RecordSchema* schema = RecordSchema::of(kind);
    if (!schema) return true;   // 文本日志会写下这一行但解析时忽略；这里直接忽略
    std::vector<std::string> cleaned;
    for (const std::string& f : fields) cleaned.push_back(Util::fieldText(f));   // 与文本日志写入时的处理相同
    std::lock_guard<std::mutex> lock(mutex_);
    const int before = lastSeq_;
    if (!beginWrite()) return false;
    const int seq = schema->entity ? allocateSeq() : 0;
    bool ok = !schema->entity || seq > 0;
    if (ok) {
        Query insert(statement("INSERT INTO records(kind, seq, service, fields, time, text) VALUES(?1, ?2, ?3, ?4, ?5, ?6)"));
        ok = insert.bind(1, std::string(1, kind)).bind(2, seq).bind(3, ServiceTypeHelper::code(service)).bind(4, joinFields(cleaned))
                 .bind(5, timed ? Date::nowText() : std::string()).bind(6, schema->text ? Util::lineText(text) : std::string()).run();
    }
    return endWrite(ok, before);
}

void SqliteMessageStore::markRead(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader,
                                    int seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!beginWrite()) return;
    bool ok = false;
    {
        // UPSERT：没有就插入，有就取较大值——已读位置只会前进
        Query upsert(statement("INSERT INTO read_marks(key, seq) VALUES(?1, ?2) ON CONFLICT(key) DO UPDATE SET seq = MAX(seq, excluded.seq)"));
        ok = upsert.bind(1, markKey(service, scope, target, reader)).bind(2, seq).run();
    }
    endWrite(ok, lastSeq_);
}

// ======================== 查询 ========================

std::vector<Message> SqliteMessageStore::collect(Query& query) const {
    std::vector<Message> result;
    while (query.next()) {
        ServiceType service = ServiceType::QQ;
        ServiceTypeHelper::parse(query.text(1), service);
        Message m(static_cast<int>(query.integer(0)), service, query.integer(2) != 0, query.text(3), query.text(4), query.text(5),
                  query.text(6));
        if (!query.isNull(7)) m.markRevoked(query.text(7));
        result.push_back(m);
    }
    return result;
}

std::vector<Message> SqliteMessageStore::conversation(ServiceType service, const std::string& a, const std::string& b) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Query query(statement(std::string(kMessageColumns) +
                          "WHERE service = ?1 AND is_group = 0 AND ((sender = ?2 AND target = ?3) OR (sender = ?3 AND target = ?2)) "
                          "ORDER BY seq"));
    query.bind(1, ServiceTypeHelper::code(service)).bind(2, a).bind(3, b);
    return collect(query);
}

std::vector<Message> SqliteMessageStore::groupHistory(ServiceType service, int groupNo) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Query query(statement(std::string(kMessageColumns) + "WHERE service = ?1 AND is_group = 1 AND target = ?2 ORDER BY seq"));
    query.bind(1, ServiceTypeHelper::code(service)).bind(2, std::to_string(groupNo));
    return collect(query);
}

std::vector<Message> SqliteMessageStore::messagesFor(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const {
    return messagesAfter(service, me, groupNos, 0, 0);
}

std::vector<Message> SqliteMessageStore::messagesAfter(ServiceType service, const std::string& me, const std::vector<int>& groupNos,
                                                       int afterSeq, std::size_t limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // 每个分支都带上 service 和 seq 条件，SQLite 才能对每个分支分别走索引（OR 优化），
    // 只读 afterSeq 之后的部分，而不是先把这个人的全部消息读出来
    std::string sql = std::string(kMessageColumns) +
        "WHERE (service = ?1 AND sender = ?2 AND is_group = 0 AND seq > ?3)"
        " OR (service = ?1 AND is_group = 0 AND target = ?2 AND seq > ?3)";
    if (!groupNos.empty()) sql += " OR (service = ?1 AND is_group = 1 AND target IN (" + groupList(groupNos) + ") AND seq > ?3)";
    sql += " ORDER BY seq LIMIT ?4";
    // 群号组合因人而异，这条语句不进缓存，用完即释放
    sqlite3_stmt* raw = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &raw, nullptr) != SQLITE_OK) return {};
    std::unique_ptr<sqlite3_stmt, int (*)(sqlite3_stmt*)> owner(raw, sqlite3_finalize);
    Query query(raw);
    query.bind(1, ServiceTypeHelper::code(service)).bind(2, me).bind(3, afterSeq)
         .bind(4, limit == 0 ? -1LL : static_cast<long long>(limit));   // LIMIT -1 表示不限
    return collect(query);
}

bool SqliteMessageStore::findMessage(ServiceType service, int seq, Message& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Query query(statement(std::string(kMessageColumns) + "WHERE seq = ?1 AND service = ?2"));
    query.bind(1, seq).bind(2, ServiceTypeHelper::code(service));
    std::vector<Message> found = collect(query);
    if (found.empty()) return false;
    out = found.front();
    return true;
}

bool SqliteMessageStore::lastFrom(ServiceType service, const std::string& sender, Message& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Query query(statement(std::string(kMessageColumns) + "WHERE service = ?1 AND sender = ?2 ORDER BY seq DESC LIMIT 1"));
    query.bind(1, ServiceTypeHelper::code(service)).bind(2, sender);
    std::vector<Message> found = collect(query);
    if (found.empty()) return false;
    out = found.front();
    return true;
}

int SqliteMessageStore::readMarkLocked(const std::string& key) const {
    Query query(statement("SELECT seq FROM read_marks WHERE key = ?1"));
    query.bind(1, key);
    return query.next() ? static_cast<int>(query.integer(0)) : 0;
}

int SqliteMessageStore::readMark(ServiceType service, const std::string& scope, const std::string& target, const std::string& reader) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return readMarkLocked(markKey(service, scope, target, reader));
}

std::map<std::string, int> SqliteMessageStore::unreadPrivate(ServiceType service, const std::string& me) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, int>> rows;   // 先读完，再查已读位置（不能在一条语句还没读完时复用另一条）
    {
        Query query(statement("SELECT sender, seq FROM messages WHERE service = ?1 AND is_group = 0 AND target = ?2 AND sender <> ?2 "
                              "AND sender <> '#' AND revoked_by IS NULL ORDER BY seq"));
        query.bind(1, ServiceTypeHelper::code(service)).bind(2, me);
        while (query.next()) rows.push_back({ query.text(0), static_cast<int>(query.integer(1)) });
    }
    std::map<std::string, int> marks, result;
    for (const auto& [sender, seq] : rows) {
        auto mark = marks.find(sender);
        if (mark == marks.end()) mark = marks.emplace(sender, readMarkLocked(markKey(service, "P", sender, me))).first;
        if (seq > mark->second) ++result[sender];
    }
    return result;
}

std::map<int, int> SqliteMessageStore::unreadGroups(ServiceType service, const std::string& me, const std::vector<int>& groupNos) const {
    if (groupNos.empty()) return {};
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string sql = "SELECT target, seq FROM messages WHERE service = ?1 AND is_group = 1 AND sender <> ?2 AND sender <> '#' "
                            "AND revoked_by IS NULL AND target IN (" + groupList(groupNos) + ") ORDER BY seq";
    sqlite3_stmt* raw = nullptr;
    if (!db_ || sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &raw, nullptr) != SQLITE_OK) return {};
    std::unique_ptr<sqlite3_stmt, int (*)(sqlite3_stmt*)> owner(raw, sqlite3_finalize);
    std::vector<std::pair<int, int>> rows;
    {
        Query query(raw);
        query.bind(1, ServiceTypeHelper::code(service)).bind(2, me);
        while (query.next()) rows.push_back({ std::atoi(query.text(0).c_str()), static_cast<int>(query.integer(1)) });
    }
    std::map<int, int> marks, result;
    for (const auto& [groupNo, seq] : rows) {
        auto mark = marks.find(groupNo);
        if (mark == marks.end()) mark = marks.emplace(groupNo, readMarkLocked(markKey(service, "G", std::to_string(groupNo), me))).first;
        if (seq > mark->second) ++result[groupNo];
    }
    return result;
}

std::vector<Record> SqliteMessageStore::records(const std::string& kinds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    Query query(statement("SELECT line, kind, seq, service, fields, time, text FROM records WHERE instr(?1, kind) > 0 ORDER BY line"));
    query.bind(1, kinds);
    std::vector<Record> result;
    while (query.next()) {
        Record r;
        r.line = static_cast<int>(query.integer(0));
        const std::string kind = query.text(1);
        r.kind = kind.empty() ? '\0' : kind[0];
        r.seq = static_cast<int>(query.integer(2));
        ServiceTypeHelper::parse(query.text(3), r.service);
        r.fields = splitFields(query.text(4));
        r.time = query.text(5);
        r.text = query.text(6);
        result.push_back(r);
    }
    return result;
}
