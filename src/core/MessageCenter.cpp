#include "MessageCenter.h"
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include "Date.h"
#include "Util.h"

namespace {

const char* kHeader =
    "# 消息与社交事件记录（程序只在末尾追加，请不要手工修改；各类记录的格式见 MessageCenter.h）\r\n";

// 事件记录的格式：字段个数、是否带时间、是否带行尾文字、是否占序号、哪些字段是账号（账号作废后记录随之作废）
struct Schema {
    char kind;
    int fields;
    bool timed;
    bool text;
    bool entity;
    std::vector<int> accounts;
};

const Schema* schemaOf(char kind) {
    static const Schema schemas[] = {
        { 'F', 3, true, true, true, { 0, 1 } },     // 对方 申请人 备注
        { 'V', 4, false, false, false, { 2 } },     // 申请序号 A/X 处理人 备注
        { 'D', 2, false, false, false, { 0, 1 } },  // 删除者 被删除者
        { 'B', 3, false, false, false, { 0, 1 } },  // 拉黑者 对方 1/0
        { 'S', 3, false, false, false, { 0 } },     // 账号 verify 1/0
        { 'P', 2, true, true, true, { 0 } },        // 作者 可见范围
        { 'L', 3, false, false, true, { 1 } },      // 动态序号 点赞者 1/0
        { 'K', 3, true, true, true, { 1 } },        // 动态序号 评论者 回复对象
        { 'Y', 1, false, false, false, {} },        // 动态序号
        { 'Z', 1, false, false, false, {} },        // 评论序号
    };
    for (const Schema& s : schemas) {
        if (s.kind == kind) return &s;
    }
    return nullptr;
}

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
    std::vector<Message> result;
    for (const Message& m : load().messages) {
        if (m.isGroup() || m.service() != service) continue;
        if ((m.sender() == a && m.target() == b) || (m.sender() == b && m.target() == a)) result.push_back(m);
    }
    return result;
}

std::vector<Message> MessageCenter::groupHistory(ServiceType service, int groupNo) const {
    std::vector<Message> result;
    const std::string no = std::to_string(groupNo);
    for (const Message& m : load().messages) {
        if (m.isGroup() && m.service() == service && m.target() == no) result.push_back(m);
    }
    return result;
}

std::vector<Message> MessageCenter::messagesFor(ServiceType service, const std::string& me,
                                                    const std::vector<int>& groupNos) const {
    std::set<std::string> wanted;
    for (int no : groupNos) wanted.insert(std::to_string(no));
    std::vector<Message> result;
    for (const Message& m : load().messages) {
        if (m.service() != service) continue;
        bool mine = m.isGroup() ? wanted.count(m.target()) > 0 : (m.sender() == me || m.target() == me);
        if (mine) result.push_back(m);
    }
    return result;
}

bool MessageCenter::findMessage(ServiceType service, int seq, Message& out) const {
    for (const Message& m : load().messages) {
        if (m.service() == service && m.seq() == seq) {
            out = m;
            return true;
        }
    }
    return false;
}

std::map<std::string, int> MessageCenter::unreadPrivate(ServiceType service, const std::string& me) const {
    State state = load();
    std::map<std::string, int> result;
    for (const Message& m : state.messages) {
        if (m.isGroup() || m.service() != service || m.target() != me || !countsAsUnread(m, me)) continue;
        if (m.seq() > markOf(state, privateKey(service, m.sender(), me))) ++result[m.sender()];
    }
    return result;
}

std::map<int, int> MessageCenter::unreadGroups(ServiceType service, const std::string& me,
                                                 const std::vector<int>& groupNos) const {
    State state = load();
    std::set<std::string> wanted;
    for (int no : groupNos) wanted.insert(std::to_string(no));
    std::map<int, int> result;
    for (const Message& m : state.messages) {
        if (!m.isGroup() || m.service() != service || !wanted.count(m.target()) || !countsAsUnread(m, me)) continue;
        const int no = std::stoi(m.target());
        if (m.seq() > markOf(state, groupKey(service, no, me))) ++result[no];
    }
    return result;
}

void MessageCenter::markPrivateRead(ServiceType service, const std::string& me, const std::string& peer, int seq) {
    const std::string key = privateKey(service, peer, me);
    if (seq > markOf(load(), key)) append("R " + key + " " + std::to_string(seq));
}

void MessageCenter::markGroupRead(ServiceType service, int groupNo, const std::string& me, int seq) {
    const std::string key = groupKey(service, groupNo, me);
    if (seq > markOf(load(), key)) append("R " + key + " " + std::to_string(seq));
}

int MessageCenter::groupReadMark(ServiceType service, int groupNo, const std::string& me) const {
    return markOf(load(), groupKey(service, groupNo, me));
}

// ======================== 事件记录 ========================

std::vector<Record> MessageCenter::records(const std::string& kinds) const {
    std::vector<Record> result;
    for (const Record& r : load().records) {
        if (kinds.find(r.kind) != std::string::npos) result.push_back(r);
    }
    return result;
}

int MessageCenter::readMark(ServiceType service, const std::string& scope, const std::string& target,
                              const std::string& reader) const {
    return markOf(load(), ServiceTypeHelper::code(service) + " " + scope + " " + target + " " + reader);
}

void MessageCenter::markRead(ServiceType service, const std::string& scope, const std::string& target,
                               const std::string& reader, int seq) {
    const std::string key = ServiceTypeHelper::code(service) + " " + scope + " " + target + " " + reader;
    if (seq > markOf(load(), key)) append("R " + key + " " + std::to_string(seq));
}

int MessageCenter::markOf(const State& state, const std::string& key) {
    auto it = state.readMarks.find(key);
    return it == state.readMarks.end() ? 0 : it->second;
}

// ======================== 读取与解析 ========================

MessageCenter::State MessageCenter::load() const {
    std::lock_guard<std::mutex> lock(mutex_);
    State state;
    std::ifstream in(Util::pathOf(filePath_), std::ios::binary);
    std::vector<std::pair<Message, int>> messages;          // 消息及其行号
    std::vector<Record> records;
    std::map<std::string, int> clearedAt;                     // "服务 A/G 账号或群号" → 作废记录所在的行号
    std::map<std::string, std::string> revokedBy;             // "服务 序号" → 撤回者
    int count = 0, lineNo = 0;
    std::string line;
    while (std::getline(in, line)) {
        ++lineNo;
        line = Util::trim(line);
        if (line.size() < 3 || line[1] != ' ') continue;
        const char kind = line[0];
        std::istringstream iss(line.substr(2));
        std::string code;
        ServiceType service;
        if (kind == 'M') {
            // 只要是 M 记录就占一个序号，即使这一行内容不完整：保证同一条消息每次读到的序号都相同
            const int seq = ++count;
            std::string scope, target, sender, date, time, text;
            if (!(iss >> code >> scope >> target >> sender >> date >> time) || !ServiceTypeHelper::parse(code, service) ||
                (scope != "P" && scope != "G"))
                continue;
            std::getline(iss, text);
            text = Util::trim(text);
            if (!text.empty()) messages.push_back({ Message(seq, service, scope == "G", target, sender, date + " " + time, text), lineNo });
        } else if (kind == 'R') {
            std::string scope, target, reader;
            int seq = 0;
            if (iss >> code >> scope >> target >> reader >> seq) {
                int& mark = state.readMarks[code + " " + scope + " " + target + " " + reader];
                mark = (std::max)(mark, seq);   // 加括号：避免被 windows.h 中的 max 宏替换
            }
        } else if (kind == 'C') {
            std::string scope, target;
            if (iss >> code >> scope >> target) clearedAt[code + " " + scope + " " + target] = lineNo;
        } else if (kind == 'W') {
            std::string seq, by;
            if (iss >> code >> seq >> by) revokedBy[code + " " + seq] = by;
        } else if (const Schema* schema = schemaOf(kind)) {
            Record r;
            r.kind = kind;
            r.line = lineNo;
            if (schema->entity) r.seq = ++count;   // 与 M 一样：出现即占序号
            if (!(iss >> code) || !ServiceTypeHelper::parse(code, r.service)) continue;
            bool ok = true;
            for (int i = 0; i < schema->fields && ok; ++i) {
                std::string f;
                ok = static_cast<bool>(iss >> f);
                r.fields.push_back(f);
            }
            if (ok && schema->timed) {
                std::string date, time;
                ok = static_cast<bool>(iss >> date >> time);
                r.time = date + " " + time;
            }
            if (!ok) continue;
            if (schema->text) {
                std::getline(iss, r.text);
                r.text = Util::trim(r.text);
            }
            records.push_back(r);
        }
    }
    auto cleared = [&](const std::string& key) {
        auto it = clearedAt.find(key);
        return it == clearedAt.end() ? 0 : it->second;
    };
    for (auto& item : messages) {
        Message& m = item.first;
        const std::string code = ServiceTypeHelper::code(m.service());
        bool valid = m.isGroup() ? item.second > cleared(code + " G " + m.target())
                                 : item.second > cleared(code + " A " + m.sender()) && item.second > cleared(code + " A " + m.target());
        if (!valid) continue;
        auto revoked = revokedBy.find(code + " " + std::to_string(m.seq()));
        if (revoked != revokedBy.end()) m.markRevoked(revoked->second);
        state.messages.push_back(m);
    }
    for (const Record& r : records) {
        const std::string code = ServiceTypeHelper::code(r.service);
        bool valid = true;
        for (int index : schemaOf(r.kind)->accounts) valid = valid && r.line > cleared(code + " A " + r.field(index));
        if (valid) state.records.push_back(r);
    }
    return state;
}

bool MessageCenter::append(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    // 与 ChatLog 相同：以 FILE_APPEND_DATA 方式打开，由系统保证每次写入都完整地接在文件末尾
    auto open = [&] {
        return CreateFileW(Util::widen(filePath_).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    };
    HANDLE file = open();
    if (file == INVALID_HANDLE_VALUE) {   // data 目录还不存在时先创建
        std::error_code ec;
        std::filesystem::create_directories(Util::pathOf(filePath_).parent_path(), ec);
        file = open();
        if (file == INVALID_HANDLE_VALUE) return false;
    }
    std::string data = line + "\r\n";
    if (GetLastError() != ERROR_ALREADY_EXISTS) data = kHeader + data;   // 新建的文件先写说明
    DWORD written = 0;
    bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
    CloseHandle(file);
    return ok;
}
