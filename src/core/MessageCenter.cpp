#include "MessageCenter.h"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <set>
#include <sstream>
#include "Date.h"
#include "RecordSchema.h"
#include "Util.h"

namespace {

const char* kHeader =
    "# 消息与社交事件记录（程序只在末尾追加，请不要手工修改；各类记录的格式见 MessageCenter.h）\r\n";

const RecordSchema* schemaOf(char kind) { return RecordSchema::of(kind); }

std::string privateKey(ServiceType service, const std::string& peer, const std::string& reader) {
    return ServiceTypeHelper::code(service) + " P " + peer + " " + reader;
}

std::string groupKey(ServiceType service, int groupNo, const std::string& reader) {
    return ServiceTypeHelper::code(service) + " G " + std::to_string(groupNo) + " " + reader;
}

// 计入未读的消息：不是系统提示、没有撤回、不是自己发的
bool countsAsUnread(const Message& m, const std::string& me) { return !m.isSystem() && !m.revoked() && m.sender() != me; }

}  // namespace

MessageCenter::MessageCenter(const std::string& filePath) : filePath_(filePath) {}

MessageCenter::~MessageCenter() {
#ifdef _WIN32
    if (file_) CloseHandle(file_);
#else
    if (file_ >= 0) ::close(file_);
#endif
}

// ======================== 写入 ========================

bool MessageCenter::postPrivate(ServiceType service, const std::string& from, const std::string& to,
                                  const std::string& text) {
    return append("M " + ServiceTypeHelper::code(service) + " P " + to + " " + from + " " + Date::nowText() + " " +
                  Util::lineText(text));
}

bool MessageCenter::postGroup(ServiceType service, int groupNo, const std::string& from, const std::string& text) {
    return append("M " + ServiceTypeHelper::code(service) + " G " + std::to_string(groupNo) + " " + from + " " +
                  Date::nowText() + " " + Util::lineText(text));
}

bool MessageCenter::revoke(ServiceType service, int seq, const std::string& by) {
    return append("W " + ServiceTypeHelper::code(service) + " " + std::to_string(seq) + " " + by + " " + Date::nowText());
}

void MessageCenter::clearAccount(ServiceType service, const std::string& id) {
    append("C " + ServiceTypeHelper::code(service) + " A " + id);
}

void MessageCenter::clearGroup(ServiceType service, int groupNo) {
    append("C " + ServiceTypeHelper::code(service) + " G " + std::to_string(groupNo));
}

bool MessageCenter::appendRecord(char kind, ServiceType service, const std::vector<std::string>& fields, bool timed,
                                   const std::string& text) {
    std::string line = std::string(1, kind) + " " + ServiceTypeHelper::code(service);
    for (const std::string& f : fields) line += " " + Util::fieldText(f);   // 每个字段不能含空格
    if (timed) line += " " + Date::nowText();
    if (!text.empty()) line += " " + Util::lineText(text);
    return append(line);
}

// ======================== 消息查询 ========================

std::vector<Message> MessageCenter::conversation(ServiceType service, const std::string& a,
                                                     const std::string& b) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Message> result;
    for (const auto& [seq, m] : refresh().messages) {
        if (m.isGroup() || m.service() != service) continue;
        if ((m.sender() == a && m.target() == b) || (m.sender() == b && m.target() == a)) result.push_back(m);
    }
    return result;
}

std::vector<Message> MessageCenter::groupHistory(ServiceType service, int groupNo) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Message> result;
    const std::string no = std::to_string(groupNo);
    for (const auto& [seq, m] : refresh().messages) {
        if (m.isGroup() && m.service() == service && m.target() == no) result.push_back(m);
    }
    return result;
}

std::vector<Message> MessageCenter::messagesFor(ServiceType service, const std::string& me,
                                                    const std::vector<int>& groupNos) const {
    std::set<std::string> wanted;
    for (int no : groupNos) wanted.insert(std::to_string(no));
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Message> result;
    for (const auto& [seq, m] : refresh().messages) {
        if (m.service() != service) continue;
        bool mine = m.isGroup() ? wanted.count(m.target()) > 0 : (m.sender() == me || m.target() == me);
        if (mine) result.push_back(m);
    }
    return result;
}

bool MessageCenter::findMessage(ServiceType service, int seq, Message& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const State& state = refresh();
    auto it = state.messages.find(seq);
    if (it == state.messages.end() || it->second.service() != service) return false;
    out = it->second;
    return true;
}

bool MessageCenter::lastFrom(ServiceType service, const std::string& sender, Message& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const State& state = refresh();
    for (auto it = state.messages.rbegin(); it != state.messages.rend(); ++it) {
        if (it->second.service() == service && it->second.sender() == sender) {
            out = it->second;
            return true;
        }
    }
    return false;
}

std::map<std::string, int> MessageCenter::unreadPrivate(ServiceType service, const std::string& me) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const State& state = refresh();
    std::map<std::string, int> result;
    for (const auto& [seq, m] : state.messages) {
        if (m.isGroup() || m.service() != service || m.target() != me || !countsAsUnread(m, me)) continue;
        if (seq > markOf(state, privateKey(service, m.sender(), me))) ++result[m.sender()];
    }
    return result;
}

std::map<int, int> MessageCenter::unreadGroups(ServiceType service, const std::string& me,
                                                 const std::vector<int>& groupNos) const {
    std::set<std::string> wanted;
    for (int no : groupNos) wanted.insert(std::to_string(no));
    std::lock_guard<std::mutex> lock(mutex_);
    const State& state = refresh();
    std::map<int, int> result;
    for (const auto& [seq, m] : state.messages) {
        if (!m.isGroup() || m.service() != service || !wanted.count(m.target()) || !countsAsUnread(m, me)) continue;
        const int no = std::stoi(m.target());
        if (seq > markOf(state, groupKey(service, no, me))) ++result[no];
    }
    return result;
}

MessageCenter::Snapshot MessageCenter::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const State& state = refresh();
    Snapshot result;
    for (const auto& entry : state.messages) result.messages.push_back(entry.second);
    result.records = state.records;
    result.readMarks = state.readMarks;
    result.lastSeq = state.count;
    return result;
}

// ======================== 事件记录 ========================

std::vector<Record> MessageCenter::records(const std::string& kinds) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Record> result;
    for (const Record& r : refresh().records) {
        if (kinds.find(r.kind) != std::string::npos) result.push_back(r);
    }
    return result;
}

int MessageCenter::readMark(ServiceType service, const std::string& scope, const std::string& target,
                              const std::string& reader) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return markOf(refresh(), ServiceTypeHelper::code(service) + " " + scope + " " + target + " " + reader);
}

void MessageCenter::markRead(ServiceType service, const std::string& scope, const std::string& target,
                               const std::string& reader, int seq) {
    const std::string key = ServiceTypeHelper::code(service) + " " + scope + " " + target + " " + reader;
    if (seq > readMark(service, scope, target, reader)) append("R " + key + " " + std::to_string(seq));
}

int MessageCenter::markOf(const State& state, const std::string& key) {
    auto it = state.readMarks.find(key);
    return it == state.readMarks.end() ? 0 : it->second;
}

// ======================== 读取与解析（增量） ========================

bool MessageCenter::openFile() const {
#ifdef _WIN32
    if (file_) return true;
    // 读 + 追加写：FILE_APPEND_DATA（不含 FILE_WRITE_DATA）保证写入永远接在文件末尾，
    // 多个窗口、多个进程各自打开着也互不覆盖；共享读写，别的程序照样可以打开它
    auto open = [&] {
        return CreateFileW(Util::widen(filePath_).c_str(), GENERIC_READ | FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    };
    HANDLE file = open();
    if (file == INVALID_HANDLE_VALUE) {   // data 目录还不存在时先创建
        std::error_code ec;
        std::filesystem::create_directories(Util::pathOf(filePath_).parent_path(), ec);
        file = open();
        if (file == INVALID_HANDLE_VALUE) return false;
    }
    if (GetLastError() != ERROR_ALREADY_EXISTS) {   // 新建的文件先写说明
        DWORD written = 0;
        WriteFile(file, kHeader, static_cast<DWORD>(std::strlen(kHeader)), &written, nullptr);
    }
    file_ = file;
    return true;
#else
    if (file_ >= 0) return true;
    // 读 + 追加写：O_APPEND 保证每次 write 都原子地接在文件末尾，多个进程各自打开着也互不覆盖
    auto open = [&] { return ::open(filePath_.c_str(), O_RDWR | O_APPEND | O_CREAT, 0644); };
    int fd = open();
    if (fd < 0) {   // data 目录还不存在时先创建
        std::error_code ec;
        std::filesystem::create_directories(Util::pathOf(filePath_).parent_path(), ec);
        fd = open();
        if (fd < 0) return false;
    }
    struct stat info {};
    if (::fstat(fd, &info) == 0 && info.st_size == 0) {   // 新建的（空）文件先写说明
        const ssize_t written = ::write(fd, kHeader, std::strlen(kHeader));
        (void)written;
    }
    file_ = fd;
    return true;
#endif
}

bool MessageCenter::fileSize(unsigned long long& size) const {
#ifdef _WIN32
    LARGE_INTEGER value{};
    if (!GetFileSizeEx(file_, &value)) return false;
    size = static_cast<unsigned long long>(value.QuadPart);
#else
    // 用 lseek 到末尾取大小，而不是 fstat：后者在有的平台上（如 Cygwin）要毫秒级，每次查询都调用就成了瓶颈。
    // 文件以 O_APPEND 打开、读取用 pread，文件指针的位置对读写都没有影响
    const off_t end = ::lseek(file_, 0, SEEK_END);
    if (end < 0) return false;
    size = static_cast<unsigned long long>(end);
#endif
    return true;
}

std::size_t MessageCenter::readAt(unsigned long long position, char* buffer, std::size_t size) const {
    // 指定位置读取（Windows 用 OVERLAPPED，POSIX 用 pread），不移动文件指针，不影响追加写
    size = (std::min)(size, static_cast<std::size_t>(1) << 24);
#ifdef _WIN32
    OVERLAPPED at{};
    at.Offset = static_cast<DWORD>(position & 0xFFFFFFFFu);
    at.OffsetHigh = static_cast<DWORD>(position >> 32);
    DWORD read = 0;
    if (!ReadFile(file_, buffer, static_cast<DWORD>(size), &read, &at)) return 0;
    return read;
#else
    const ssize_t read = ::pread(file_, buffer, size, static_cast<off_t>(position));
    return read > 0 ? static_cast<std::size_t>(read) : 0;
#endif
}

const MessageCenter::State& MessageCenter::refresh() const {
    if (!openFile()) return state_;
    unsigned long long total = 0;
    if (!fileSize(total)) return state_;
    if (total < state_.offset) state_ = State{};   // 文件变短了（被替换成了别的数据）：从头重新解析
    if (total == state_.offset) return state_;

    // 只读新追加的部分
    std::string chunk(static_cast<std::size_t>(total - state_.offset), '\0');
    std::size_t got = 0;
    while (got < chunk.size()) {
        const std::size_t read = readAt(state_.offset + got, &chunk[got], chunk.size() - got);
        if (read == 0) break;
        got += read;
    }
    // 只处理完整的行：别的进程可能正写到一半，没有换行符的尾巴留到下次
    const std::size_t end = chunk.rfind('\n', got == 0 ? 0 : got - 1);
    if (got == 0 || end == std::string::npos) return state_;
    std::size_t begin = 0;
    while (begin <= end) {
        const std::size_t newline = chunk.find('\n', begin);
        ++state_.lineNo;
        parseLine(state_, chunk.substr(begin, newline - begin));
        begin = newline + 1;
    }
    state_.offset += end + 1;
    return state_;
}

// 账号关闭或群解散（C 记录）：此前与之有关的消息和事件记录全部作废。之后的记录不受影响
void MessageCenter::clear(State& state, const std::string& code, const std::string& scope, const std::string& target) const {
    for (auto it = state.messages.begin(); it != state.messages.end();) {
        const Message& m = it->second;
        const bool related = ServiceTypeHelper::code(m.service()) == code &&
                             (scope == "G" ? m.isGroup() && m.target() == target
                                           : !m.isGroup() && (m.sender() == target || m.target() == target));
        it = related ? state.messages.erase(it) : std::next(it);
    }
    if (scope != "A") return;
    state.records.erase(std::remove_if(state.records.begin(), state.records.end(),
                                       [&](const Record& r) {
                                           if (ServiceTypeHelper::code(r.service) != code) return false;
                                           for (int index : schemaOf(r.kind)->accounts)
                                               if (r.field(index) == target) return true;
                                           return false;
                                       }),
                        state.records.end());
}

void MessageCenter::parseLine(State& state, const std::string& raw) const {
    const std::string line = Util::trim(raw);
    if (line.size() < 3 || line[1] != ' ') return;
    const char kind = line[0];
    std::istringstream iss(line.substr(2));
    std::string code;
    ServiceType service;
    if (kind == 'M') {
        // 只要是 M 记录就占一个序号，即使这一行内容不完整：保证同一条消息每次读到的序号都相同
        const int seq = ++state.count;
        std::string scope, target, sender, date, time, text;
        if (!(iss >> code >> scope >> target >> sender >> date >> time) || !ServiceTypeHelper::parse(code, service) ||
            (scope != "P" && scope != "G"))
            return;
        std::getline(iss, text);
        text = Util::trim(text);
        if (text.empty()) return;
        Message message(seq, service, scope == "G", target, sender, date + " " + time, text);
        auto revoked = state.revokedBy.find(code + " " + std::to_string(seq));
        if (revoked != state.revokedBy.end()) message.markRevoked(revoked->second);
        state.messages.emplace(seq, message);
    } else if (kind == 'R') {
        std::string scope, target, reader;
        int seq = 0;
        if (iss >> code >> scope >> target >> reader >> seq) {
            int& mark = state.readMarks[code + " " + scope + " " + target + " " + reader];
            mark = (std::max)(mark, seq);   // 加括号：避免被 windows.h 中的 max 宏替换
        }
    } else if (kind == 'C') {
        std::string scope, target;
        if (iss >> code >> scope >> target) clear(state, code, scope, target);
    } else if (kind == 'W') {
        std::string seq, by;
        if (!(iss >> code >> seq >> by)) return;
        state.revokedBy[code + " " + seq] = by;
        auto it = state.messages.find(std::atoi(seq.c_str()));
        if (it != state.messages.end() && ServiceTypeHelper::code(it->second.service()) == code) it->second.markRevoked(by);
    } else if (const RecordSchema* schema = schemaOf(kind)) {
        Record r;
        r.kind = kind;
        r.line = state.lineNo;
        if (schema->entity) r.seq = ++state.count;   // 与 M 一样：出现即占序号
        if (!(iss >> code) || !ServiceTypeHelper::parse(code, r.service)) return;
        for (int i = 0; i < schema->fields; ++i) {
            std::string f;
            if (!(iss >> f)) return;
            r.fields.push_back(f);
        }
        if (schema->timed) {
            std::string date, time;
            if (!(iss >> date >> time)) return;
            r.time = date + " " + time;
        }
        if (schema->text) {
            std::getline(iss, r.text);
            r.text = Util::trim(r.text);
        }
        state.records.push_back(r);
    }
}

bool MessageCenter::append(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!openFile()) return false;
    // 一次写完整的一行：以追加方式打开，由系统保证整行接在文件末尾，不会和别的进程交错
    const std::string data = line + "\r\n";
#ifdef _WIN32
    DWORD written = 0;
    return WriteFile(file_, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
#else
    return ::write(file_, data.data(), data.size()) == static_cast<ssize_t>(data.size());
#endif
}
