#include "Platform.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iterator>
#include <sstream>
#include "IndependentIdService.h"
#include "MessageCenter.h"
#include "QQService.h"
#include "SqliteMessageStore.h"
#include "Util.h"
#include "WeChatService.h"
#include "WeiShangService.h"
#include "WeiboService.h"

namespace {

// 说明两个账号为什么对应不上（用于提示信息）
std::string linkProblem(const ServiceBase& a, const ServiceBase& b) {
    for (const ServiceBase* acc : { &a, &b }) {
        if (acc->linkedQQ().empty()) return acc->serviceName() + " " + acc->displayName() + " 未绑定QQ";
    }
    return a.serviceName() + "与" + b.serviceName() + "对应的QQ号不一致";
}

// 消息内容检查：不能为空，长度有限制（按字符数计算）
bool messageTextOk(const std::string& text, std::string& msg) {
    if (text.empty()) {
        msg = "消息内容不能为空。";
        return false;
    }
    if (Util::charCount(text) > 250) {
        msg = "消息太长（最多 250 个字），请分几次发送。";
        return false;
    }
    return true;
}

int toInt(const std::string& s) {
    try {
        return std::stoi(s);
    } catch (...) {
        return 0;
    }
}

// “yyyy-mm-dd hh:mm:ss”距现在多少秒（用于“2 分钟内可以撤回”）
double secondsSince(const std::string& time) {
    std::tm t{};
    if (std::sscanf(time.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6)
        return 1e9;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    return std::difftime(std::time(nullptr), std::mktime(&t));
}

std::unique_ptr<MessageStore> createStore(const std::string& dataDir, Platform::Storage storage) {
    if (storage == Platform::Storage::SQLITE) {
        // 第一次使用时，已有的 messages.txt 会整体导入数据库（文本文件本身保留不动）
        auto store = std::make_unique<SqliteMessageStore>(dataDir + "messages.db", dataDir + "messages.txt");
        if (store->isOpen()) {
            if (store->imported() > 0)
                std::cout << "[系统] 已把 messages.txt 中的 " << store->imported() << " 条消息导入 messages.db。\n";
            return store;
        }
        std::cout << "[系统] 无法打开 messages.db，改用 messages.txt。\n";
    }
    return std::make_unique<MessageCenter>(dataDir + "messages.txt");
}

}  // namespace

Platform::Platform(const std::string& dataDir, Storage storage)
    : dataDir_(dataDir), sessionStamp_(Date::nowStamp()), messages_(createStore(dataDir, storage)), feed_(*messages_), requestBook_(*messages_),
      usersFile_(dataDir, "users.txt"), accountsFile_(dataDir, "accounts.txt"), friendsFile_(dataDir, "friends.txt"),
      groupsFile_(dataDir, "groups.txt"), subGroupsFile_(dataDir, "subgroups.txt") {
    // 文件缺失时以空数据启动，退出时会自动创建数据文件
    std::string users, accounts, friends, groups, subGroups;
    if (!usersFile_.read(users)) std::cout << "[系统] 未找到 users.txt\n";
    if (!accountsFile_.read(accounts)) std::cout << "[系统] 未找到 accounts.txt\n";
    if (!friendsFile_.read(friends)) std::cout << "[系统] 未找到 friends.txt\n";
    if (!groupsFile_.read(groups)) std::cout << "[系统] 未找到 groups.txt\n";
    subGroupsFile_.read(subGroups);
    for (const DataFile* f : { &usersFile_, &accountsFile_, &friendsFile_, &groupsFile_, &subGroupsFile_ }) {
        if (f->convertedFromGbk())
            std::cout << "[系统] " << f->fileName() << " 是旧版本的 GBK 编码，已自动转换读取，保存时写成 UTF-8。\n";
    }

    // 必须按依赖顺序加载：用户 → 账号 → 好友 → 群，最后统一检查跨文件的一致性
    int badLines = loadUsers(users);
    badLines += loadAccounts(accounts);
    badLines += loadFriends(friends);
    int repaired = 0;
    badLines += groups_.load(groups, subGroups, repaired);
    repaired += repairData();
    groups_.setListener(this);   // 之后群里发生的事写成系统提示（加载数据本身不产生提示）
    syncSocial();                // 应用消息文件中的好友申请结果、单向删除、拉黑、验证设置
    requestBook_.refresh();      // 好友申请、朋友圈的内存索引：启动时建好，第一次查询不必等
    feed_.refresh();

    // 题目约定每个微X服务都有 1001～1006 号群：有账号但还没有群的服务（如新增的微商）补建预置群
    for (ServiceType type : ServiceTypeHelper::all()) {
        for (const auto& acc : accounts_) {
            if (acc->type() == type) {
                groups_.createPresetGroups(type, acc->id(), acc->defaultGroupMode());
                break;
            }
        }
    }

    // 有被跳过或修正的数据时，下次保存后原文件内容就会改变，因此先把原文件完整备份一份
    if (badLines > 0 || repaired > 0) {
        std::string backupDir = dataDir_ + "backup_" + sessionStamp_;
        for (const DataFile* f : { &usersFile_, &accountsFile_, &friendsFile_, &groupsFile_, &subGroupsFile_ })
            f->copyTo(backupDir);
        std::cout << "[警告] 数据文件中";
        if (badLines > 0) std::cout << "有 " << badLines << " 行格式不正确或与其他数据冲突，已跳过";
        if (badLines > 0 && repaired > 0) std::cout << "；另有 ";
        else if (repaired > 0) std::cout << "有 ";
        if (repaired > 0) std::cout << repaired << " 处数据前后不一致，已自动修正";
        std::cout << "。\n       原始文件已备份到 " << backupDir << "，可在修正后放回 data 目录。\n";
    }
    std::cout << "[系统] 数据加载完成：用户 " << users_.size() << " 人，服务账号 " << accounts_.size()
              << " 个，群 " << groups_.count() << " 个。\n";
}

Platform::~Platform() {
    std::cout << (save() ? "[系统] 数据已写回文件。\n" : "[系统] 数据写回失败，请检查 data 目录！\n");
}

bool Platform::save() const {
    std::lock_guard<std::mutex> lock(saveMutex_);
    return finalSaved_ ? true : writeAll();
}

bool Platform::saveOnExit() const {
    std::lock_guard<std::mutex> lock(saveMutex_);
    if (finalSaved_) return true;
    finalSaved_ = true;
    return writeAll();
}

// 5 个文件作为一个整体保存：
// 1. 没有变化的文件不写；
// 2. 只要有一个要写的文件已被其他窗口改过，就不写 data 目录，而是把本窗口的全部数据另存到冲突目录——
//    data 目录始终是某一个窗口的完整数据，不会出现“一半是本窗口、一半是对方”的混合状态；
// 3. 先把所有要写的文件写成临时文件并刷到磁盘，全部成功后再逐个替换正式文件。
bool Platform::writeAll() const {
    DataFile* files[] = { &usersFile_, &accountsFile_, &friendsFile_, &groupsFile_, &subGroupsFile_ };
    const std::string texts[] = { usersText(), accountsText(), friendsText(), groups_.groupText(), groups_.subGroupText() };
    const int count = 5;
    bool changed[count] = {};
    bool anyChange = false, conflict = false;
    for (int i = 0; i < count; ++i) {
        changed[i] = files[i]->hasChanges(texts[i]);
        // 被其他窗口改过、但内容与本窗口要写的完全一样（例如两边都同步了同一条好友申请结果）：不算冲突，也不必再写
        if (changed[i] && files[i]->modifiedOutside() && files[i]->adoptIfSame(texts[i])) changed[i] = false;
        anyChange = anyChange || changed[i];
        if (changed[i] && files[i]->modifiedOutside()) conflict = true;
    }
    if (!anyChange) return true;

    if (conflict) {
        const std::string conflictDir = dataDir_ + "conflict_" + sessionStamp_;
        bool ok = true;
        for (int i = 0; i < count; ++i) ok = files[i]->saveCopy(conflictDir, texts[i]) && ok;
        std::cout << "[注意] 数据文件已被另一个程序窗口修改。为避免互相覆盖，本窗口的数据没有写入 data 目录，\n"
                  << "       而是完整另存到了 " << conflictDir << "（需要时可整体复制回 data 目录）。\n";
        return ok;
    }

    for (int i = 0; i < count; ++i) {
        if (changed[i] && !files[i]->prepare(texts[i])) {
            std::cout << "[错误] 写入 " << files[i]->fileName() << " 的临时文件失败，本次没有修改任何数据文件。\n";
            for (int j = 0; j < count; ++j) files[j]->discard();
            return false;
        }
    }
    bool ok = true;
    for (int i = 0; i < count; ++i) {
        if (changed[i] && !files[i]->commit()) {
            std::cout << "[错误] 替换 " << files[i]->fileName() << " 失败。\n";
            ok = false;
        }
    }
    return ok;
}

const std::string& Platform::dataDir() const { return dataDir_; }

// ======================== 用户与账号 ========================

std::string Platform::accountKey(ServiceType type, const std::string& id) { return ServiceTypeHelper::code(type) + " " + id; }

// 哈希索引：原来是逐个比较全部账号，几乎每个请求都要查好几次（群消息算 @ 时每个成员查一次），账号一多就成了瓶颈
ServiceBase* Platform::findAccount(ServiceType type, const std::string& id) const {
    auto it = accountIndex_.find(accountKey(type, id));
    return it == accountIndex_.end() ? nullptr : it->second;
}

User* Platform::findUser(int userId) {
    auto it = users_.find(userId);
    return it == users_.end() ? nullptr : &it->second;
}

User* Platform::ownerOf(const ServiceBase* account) {
    return account ? findUser(account->ownerId()) : nullptr;
}

const std::map<int, User>& Platform::users() const { return users_; }

std::string Platform::displayName(ServiceType type, const std::string& id) const {
    if (id.empty()) return "（无）";
    ServiceBase* acc = findAccount(type, id);
    return acc ? acc->displayName() : id + "(已注销)";
}

bool Platform::canRegisterWith(ServiceType type, std::string& msg) const {
    User candidate;
    return !generateId(candidate, type, msg).empty();
}

ServiceBase* Platform::registerUser(const std::string& name, ServiceType type, const std::string& nickname,
                                        const Date& birthday, const std::string& location,
                                        const std::string& password, std::string& msg) {
    int newId = users_.empty() ? 1 : users_.rbegin()->first + 1;
    User candidate(newId, name);
    if (generateId(candidate, type, msg).empty()) return nullptr;   // 先检查能否开通，避免留下空用户
    User& user = users_.emplace(newId, candidate).first->second;
    return openService(user, type, nickname, birthday, location, password, msg);
}

ServiceBase* Platform::openService(User& user, ServiceType type, const std::string& nickname,
                                       const Date& birthday, const std::string& location,
                                       const std::string& password, std::string& msg) {
    if (user.hasService(type)) {
        msg = "你已经开通过" + ServiceTypeHelper::name(type) + "。";
        return nullptr;
    }
    std::string id = generateId(user, type, msg);
    if (id.empty()) return nullptr;
    auto account = createService(type, id, user.id(), nickname, birthday, Date::today(), location, password);
    ServiceBase* raw = account.get();
    user.attach(raw);
    accountIndex_[accountKey(type, id)] = raw;
    accounts_.push_back(std::move(account));
    msg = "开通成功！你的" + raw->idLabel() + "为：" + id;
    // 该服务还没有任何群（如新增的微商服务），按题目约定建立 1001～1006 号群
    if (groups_.groupsOfService(type).empty()) {
        groups_.createPresetGroups(type, id, raw->defaultGroupMode());
        msg += "。你是第一位" + ServiceTypeHelper::name(type) + "用户，已为你创建 1001～1006 号群";
    }
    return raw;
}

bool Platform::closeService(User& user, ServiceType type, std::string& msg) {
    ServiceBase* account = user.service(type);
    if (!account) {
        msg = "你没有开通" + ServiceTypeHelper::name(type) + "。";
        return false;
    }
    const std::string id = account->id();
    // 1. 从同一服务所有人的好友列表中删除该账号（好友关系是双向的）
    for (const auto& acc : accounts_) {
        if (acc->type() == type) acc->friends().remove(id);
    }
    // 2. 从群、管理员、讨论组和入群申请中删除；若是群主则自动移交
    groups_.removeAccount(type, id);
    // 3. 关闭 QQ 时，解除本人微信、微商等独立号码服务与该 QQ 号的绑定
    if (type == ServiceType::QQ) {
        for (ServiceBase* s : user.services()) {
            auto* independent = dynamic_cast<IndependentIdService*>(s);
            if (independent && independent->boundQQ() == id) independent->unbindQQ();
        }
    }
    // 4. 与该账号有关的私信、好友申请、黑名单、朋友圈作废：号码以后被重新分配时，新主人不会看到这些内容
    messages_->clearAccount(type, id);
    const std::string code = ServiceTypeHelper::code(type);
    for (auto it = blocked_.begin(); it != blocked_.end();) {
        std::istringstream iss(*it);
        std::string c, blocker, blocked;
        iss >> c >> blocker >> blocked;
        it = (c == code && (blocker == id || blocked == id)) ? blocked_.erase(it) : std::next(it);
    }
    noVerify_.erase(code + " " + id);
    // 5. 删除账号对象本身
    user.detach(type);
    accountIndex_.erase(accountKey(type, id));
    accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(),
                                   [account](const std::unique_ptr<ServiceBase>& a) { return a.get() == account; }),
                    accounts_.end());
    msg = "已关闭" + ServiceTypeHelper::name(type) + "（" + id + "），相关好友关系、群成员身份和私信已一并清除。";
    return true;
}

void Platform::deleteUser(int userId, std::string& msg) {
    User* user = findUser(userId);
    if (!user) {
        msg = "用户不存在。";
        return;
    }
    std::string name = user->name();
    for (ServiceBase* s : user->services()) {
        std::string ignored;
        closeService(*user, s->type(), ignored);
    }
    users_.erase(userId);
    msg = "用户 " + name + " 已注销，全部服务账号及相关数据已删除。";
}

// 工厂方法：根据服务类型创建对应的派生类对象
std::unique_ptr<ServiceBase> Platform::createService(ServiceType type, const std::string& id, int ownerId,
                                                         const std::string& nickname, const Date& birthday,
                                                         const Date& registerDate, const std::string& location,
                                                         const std::string& password) {
    switch (type) {
    case ServiceType::QQ:
        return std::make_unique<QQService>(id, ownerId, nickname, birthday, registerDate, location, password);
    case ServiceType::WECHAT:
        return std::make_unique<WeChatService>(id, ownerId, nickname, birthday, registerDate, location, password);
    case ServiceType::WEIBO:
        return std::make_unique<WeiboService>(id, ownerId, nickname, birthday, registerDate, location, password);
    case ServiceType::WEISHANG:
        return std::make_unique<WeiShangService>(id, ownerId, nickname, birthday, registerDate, location, password);
    }
    return nullptr;
}

// 账号分配规则：QQ 号顺序递增；微博与 QQ 共享号码；微信、微商使用各自前缀的独立号码
std::string Platform::generateId(const User& user, ServiceType type, std::string& msg) const {
    if (type == ServiceType::QQ || type == ServiceType::WEIBO) {
        // 共享号码：本人已有 QQ 或微博中的任意一个，另一个就沿用同一号码
        ServiceType partner = (type == ServiceType::QQ) ? ServiceType::WEIBO : ServiceType::QQ;
        if (ServiceBase* shared = user.service(partner)) return shared->id();
        if (type == ServiceType::WEIBO) {
            msg = "微博与QQ共享号码，请先开通QQ服务。";
            return "";
        }
        long long maxId = 10000;   // QQ 与微博共用一个号码段
        for (const auto& acc : accounts_) {
            if (acc->usesQQNumber()) maxId = std::max(maxId, std::atoll(acc->id().c_str()));
        }
        return std::to_string(maxId + 1);
    }
    const std::string prefix = (type == ServiceType::WECHAT) ? "wx" : "ws";
    int maxNo = (type == ServiceType::WECHAT) ? 2000 : 3000;
    for (const auto& acc : accounts_) {
        if (acc->type() == type && acc->id().size() > 2) maxNo = std::max(maxNo, std::atoi(acc->id().c_str() + 2));
    }
    return prefix + std::to_string(maxNo + 1);
}

// ======================== 跨服务身份对应 ========================

bool Platform::isLinked(const ServiceBase& a, const ServiceBase& b) {
    return a.ownerId() == b.ownerId() && !a.linkedQQ().empty() && a.linkedQQ() == b.linkedQQ();
}

bool Platform::bindQQ(User& user, IndependentIdService& account, std::string& msg) {
    if (user.service(account.type()) != &account) {
        msg = "只能为自己的账号绑定QQ。";
        return false;
    }
    if (account.isBound()) {
        msg = "已经绑定了QQ " + account.boundQQ() + "，如需更换请先解绑。";
        return false;
    }
    ServiceBase* qq = user.service(ServiceType::QQ);
    if (!qq) {
        msg = "你还没有开通QQ，无法绑定（只能绑定本人的QQ号）。";
        return false;
    }
    account.bindQQ(qq->id());
    msg = account.serviceName() + "已与QQ号 " + qq->id() + " 绑定，现在可以和QQ、微博互认好友了。";
    return true;
}

bool Platform::unbindQQ(IndependentIdService& account, std::string& msg) {
    if (!account.isBound()) {
        msg = account.serviceName() + "还没有绑定QQ。";
        return false;
    }
    msg = account.serviceName() + "已与QQ号 " + account.boundQQ() + " 解除绑定。";
    account.unbindQQ();
    return true;
}

bool Platform::checkOwnLink(const ServiceBase* first, const ServiceBase* second, std::string& msg) const {
    if (!first || !second) {
        msg = "需要先开通这两项服务。";
        return false;
    }
    if (isLinked(*first, *second)) return true;
    msg = "你的" + linkProblem(*first, *second) + "，平台无法确认你的" + first->serviceName() + "和" +
          second->serviceName() + "属于同一人。请先在“个人信息 → 绑定/解绑QQ”中绑定。";
    return false;
}

// ======================== 好友 ========================

bool Platform::addFriend(ServiceBase& me, const std::string& friendId, const std::string& remark,
                           std::string& msg) {
    if (friendId == me.id()) {
        msg = "不能添加自己为好友。";
        return false;
    }
    ServiceBase* other = findAccount(me.type(), friendId);
    if (!other) {
        msg = me.serviceName() + "中不存在账号 " + friendId + "。";
        return false;
    }
    if (me.friends().contains(friendId)) {
        msg = other->displayName() + " 已经是你的好友。";
        return false;
    }
    me.friends().add(friendId, remark.empty() ? other->nickname() : remark);
    other->friends().add(me.id(), me.nickname());
    msg = "已添加好友 " + other->displayName() + "。";
    return true;
}

bool Platform::removeFriend(ServiceBase& me, const std::string& friendId, std::string& msg) {
    syncSocial();
    if (!me.friends().remove(friendId)) {
        msg = friendId + " 不是你的好友。";
        return false;
    }
    messages_->appendRecord('D', me.type(), { me.id(), friendId }, false);   // 其他窗口据此同步
    msg = "已删除好友 " + displayName(me.type(), friendId) + "。对方的好友列表中仍有你，对方再给你发消息时会被拒绝。";
    return true;
}

std::vector<Friend> Platform::searchFriends(const ServiceBase& me, const std::string& keyword) const {
    std::vector<Friend> result;
    for (const Friend& f : me.friends().all()) {
        ServiceBase* acc = findAccount(me.type(), f.id());
        bool hit = Util::containsText(f.id(), keyword) || Util::containsText(f.remark(), keyword) ||
                   (acc && Util::containsText(acc->nickname(), keyword));
        if (hit) result.push_back(f);
    }
    return result;
}

bool Platform::commonFriends(const User& user, ServiceType a, ServiceType b, std::vector<User*>& result,
                               std::vector<std::string>& skipped, std::string& msg) {
    result.clear();
    skipped.clear();
    ServiceBase* mineA = user.service(a);
    ServiceBase* mineB = user.service(b);
    if (!checkOwnLink(mineA, mineB, msg)) return false;
    // 以“人”为纽带：a 服务中的好友 → 此人 → 此人在 b 服务中的账号，且两边账号必须对应同一个 QQ 号
    for (const Friend& f : mineA->friends().all()) {
        ServiceBase* accA = findAccount(a, f.id());
        User* person = accA ? findUser(accA->ownerId()) : nullptr;
        ServiceBase* accB = person ? person->service(b) : nullptr;
        if (!accB || !mineB->friends().contains(accB->id())) continue;   // 在 b 服务中不是我的好友
        if (isLinked(*accA, *accB)) result.push_back(person);
        else skipped.push_back(person->name() + "：" + linkProblem(*accA, *accB) + "，无法确认两个账号是同一人");
    }
    return true;
}

bool Platform::recommendFriends(const User& user, ServiceType target, ServiceType source,
                                  std::vector<ServiceBase*>& result, std::vector<std::string>& skipped,
                                  std::string& msg) {
    result.clear();
    skipped.clear();
    ServiceBase* mine = user.service(target);
    ServiceBase* mineSource = user.service(source);
    if (!checkOwnLink(mineSource, mine, msg)) return false;
    for (const Friend& f : mineSource->friends().all()) {
        ServiceBase* from = findAccount(source, f.id());
        User* person = from ? findUser(from->ownerId()) : nullptr;
        ServiceBase* acc = person ? person->service(target) : nullptr;
        if (!acc || mine->friends().contains(acc->id())) continue;   // 对方没开通目标服务，或已经是好友
        if (isLinked(*from, *acc)) result.push_back(acc);
        else skipped.push_back(from->displayName() + "：" + linkProblem(*from, *acc) + "，无法确认是同一人");
    }
    return true;
}

std::vector<std::string> Platform::mutualFriends(const ServiceBase& me, const ServiceBase& other) const {
    std::vector<std::string> result;
    for (const Friend& f : me.friends().all()) {
        if (f.id() != other.id() && other.friends().contains(f.id())) result.push_back(f.id());
    }
    return result;
}

GroupManager& Platform::groups() { return groups_; }

// ======================== 群的新建与解散 ========================

Group* Platform::createGroup(const ServiceBase& me, const std::string& name, std::string& msg) {
    const std::string groupName = Util::toField(Util::lineText(name));
    if (groupName.empty()) {
        msg = "群名称不能为空。";
        return nullptr;
    }
    Group* group = groups_.createGroup(me.type(), groupName, me.id(), me.defaultGroupMode());
    messages_->postGroup(me.type(), group->groupNo(), Message::systemSender(), nickOf(me.type(), me.id()) + " 创建了群聊");
    msg = "群 " + std::to_string(group->groupNo()) + "（" + groupName + "）创建成功，你是群主，管理模式为" +
          group->policy().modeName() + "。";
    return group;
}

bool Platform::dissolveGroup(const ServiceBase& me, int groupNo, std::string& msg) {
    Group* group = groups_.find(me.type(), groupNo);
    if (!group) {
        msg = me.serviceName() + "中不存在群 " + std::to_string(groupNo) + "。";
        return false;
    }
    if (!group->isOwner(me.id())) {
        msg = "只有群主可以解散群。";
        return false;
    }
    if (GroupManager::isPreset(groupNo)) {
        msg = "1001～1006 是平台预置群，不能解散（可以转让群主）。";
        return false;
    }
    const std::string name = group->name();
    groups_.remove(me.type(), groupNo);
    messages_->clearGroup(me.type(), groupNo);   // 群号以后可能分配给新群，旧的群聊记录随之作废
    msg = "群 " + std::to_string(groupNo) + "（" + name + "）已解散，群聊记录一并清除。";
    return true;
}

// ======================== 消息 ========================

MessageStore& Platform::messages() { return *messages_; }

bool Platform::sendPrivate(const ServiceBase& me, const std::string& friendId, const std::string& text,
                             std::string& msg) {
    syncSocial();   // 对方可能刚在另一个窗口删除了我或把我拉黑
    ServiceBase* other = findAccount(me.type(), friendId);
    if (!me.friends().contains(friendId) || !other) {
        msg = friendId + " 不是你的" + me.serviceName() + "好友，只能给好友发私信。";
        return false;
    }
    const std::string line = Util::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    if (isBlocked(me.type(), friendId, me.id())) {   // 与微信相同的提示
        msg = "消息已发出，但被对方拒收了。";
        return false;
    }
    if (!other->friends().contains(me.id())) {
        msg = "对方开启了朋友验证，你还不是他（她）的朋友。请先发送好友申请，对方验证通过后，才能聊天。";
        return false;
    }
    if (!messages_->postPrivate(me.type(), me.id(), friendId, line)) {
        msg = "消息保存失败，请检查 data 目录。";
        return false;
    }
    msg = "已发送给 " + displayName(me.type(), friendId) + "。";
    return true;
}

bool Platform::sendGroupMessage(const ServiceBase& me, const Group& group, const std::string& text,
                                  std::string& msg) {
    if (group.service() != me.type() || !group.isMember(me.id())) {
        msg = "你不是该群成员，不能发群消息。";
        return false;
    }
    if (!group.canSpeak(me.id())) {
        msg = "你已被禁言，不能在本群发言。";
        return false;
    }
    const std::string line = Util::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    if (!messages_->postGroup(me.type(), group.groupNo(), me.id(), line)) {
        msg = "消息保存失败，请检查 data 目录。";
        return false;
    }
    msg = "群消息已发送。";
    return true;
}

void Platform::unreadCount(const ServiceBase& me, int& privateCount, int& groupCount) const {
    privateCount = groupCount = 0;
    for (const auto& kv : messages_->unreadPrivate(me.type(), me.id())) privateCount += kv.second;
    std::vector<int> groupNos;
    for (Group* g : groups_.groupsOf(me.type(), me.id())) groupNos.push_back(g->groupNo());
    for (const auto& kv : messages_->unreadGroups(me.type(), me.id(), groupNos)) groupCount += kv.second;
}

// ======================== 文本与对象的转换 ========================

// users.txt 每行：用户编号 姓名
int Platform::loadUsers(const std::string& text) {
    int bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = Util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        int id = 0;
        std::string name;
        if (iss >> id >> name && id > 0 && !users_.count(id)) users_.emplace(id, User(id, name));
        else ++bad;
    }
    return bad;
}

// 共享号码规则：QQ、微博的号码必须是数字；同一个人的 QQ 与微博号码相同；一个号码不能属于两个人
bool Platform::sharedNumberOk(const User& owner, const ServiceBase& account) const {
    if (!account.usesQQNumber()) return true;
    const std::string& id = account.id();
    if (id.empty() || id.find_first_not_of("0123456789") != std::string::npos) return false;
    for (const auto& acc : accounts_) {
        if (!acc->usesQQNumber()) continue;
        if (acc->ownerId() == owner.id() && acc->id() != id) return false;
        if (acc->ownerId() != owner.id() && acc->id() == id) return false;
    }
    return true;
}

// accounts.txt 每行：服务 账号 用户编号 昵称 出生日期 申请日期 所在地 密码记录 扩展字段
int Platform::loadAccounts(const std::string& text) {
    int bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = Util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string typeCode, id, nickname, birthText, registerText, location, password, extra;
        int ownerId = 0;
        ServiceType type;
        Date birthday, registerDate;
        User* owner = nullptr;
        if (!(iss >> typeCode >> id >> ownerId >> nickname >> birthText >> registerText >> location >> password >> extra) ||
            !(owner = findUser(ownerId)) || !ServiceTypeHelper::parse(typeCode, type) ||
            !Date::parse(birthText, birthday) || !Date::parse(registerText, registerDate) ||
            owner->hasService(type) || findAccount(type, id)) {
            ++bad;
            continue;
        }
        auto account = createService(type, id, ownerId, nickname, birthday, registerDate, location, password);
        if (!sharedNumberOk(*owner, *account)) {   // 例如微博号与本人 QQ 号不同，或号码已属于别人
            ++bad;
            continue;
        }
        account->loadExtraData(extra);
        owner->attach(account.get());
        accountIndex_[accountKey(account->type(), account->id())] = account.get();
        accounts_.push_back(std::move(account));
    }
    return bad;
}

// friends.txt 每行：服务 我的账号 好友账号 备注
int Platform::loadFriends(const std::string& text) {
    int bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = Util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string typeCode, mine, other, remark;
        ServiceType type;
        ServiceBase* me = nullptr;
        if (!(iss >> typeCode >> mine >> other >> remark) || !ServiceTypeHelper::parse(typeCode, type) ||
            mine == other || !(me = findAccount(type, mine)) || !findAccount(type, other)) {
            ++bad;
            continue;
        }
        me->friends().add(other, remark);
    }
    return bad;
}

int Platform::repairData() {
    int repaired = 0;
    for (const auto& acc : accounts_) {
        // 1. 绑定只能指向本人已开通的 QQ 号，否则解除
        auto* independent = dynamic_cast<IndependentIdService*>(acc.get());
        if (independent && independent->isBound()) {
            User* owner = findUser(acc->ownerId());
            ServiceBase* qq = owner ? owner->service(ServiceType::QQ) : nullptr;
            if (!qq || qq->id() != independent->boundQQ()) {
                independent->unbindQQ();
                ++repaired;
            }
        }
        // （好友关系允许单向：与微信相同，删除好友只删除自己这一方，所以不再把单向好友补成双向）
    }
    // 3. 群里不存在的账号（成员、管理员、申请人）一律移除；被移除的是群主时按接任规则移交
    for (ServiceType type : ServiceTypeHelper::all()) {
        for (Group* g : groups_.groupsOfService(type)) {
            std::vector<std::string> ids = g->members();
            ids.insert(ids.end(), g->pending().begin(), g->pending().end());
            for (const std::string& id : ids) {
                if (!findAccount(type, id)) {
                    g->removeAccount(id);
                    ++repaired;
                }
            }
        }
    }
    return repaired;
}

std::string Platform::usersText() const {
    std::ostringstream out;
    out << "# 用户编号 姓名\n";
    for (const auto& kv : users_) out << kv.first << ' ' << Util::fieldText(kv.second.name()) << '\n';
    return out.str();
}

std::string Platform::accountsText() const {
    std::ostringstream out;
    out << "# 服务 账号 用户编号 昵称 出生日期 申请日期 所在地 密码记录（盐$SHA-256摘要） 扩展字段（微信、微商为绑定的QQ号）\n";
    // 自由输入的文字字段再统一过滤一次：即使业务接口收到含空格的内容，也不会写坏数据文件
    for (const auto& acc : accounts_) {
        out << ServiceTypeHelper::code(acc->type()) << ' ' << acc->id() << ' ' << acc->ownerId() << ' '
            << Util::fieldText(acc->nickname()) << ' ' << acc->birthday().toString() << ' '
            << acc->registerDate().toString() << ' ' << Util::fieldText(acc->location()) << ' ' << acc->password()
            << ' ' << acc->extraData() << '\n';
    }
    return out.str();
}

std::string Platform::friendsText() const {
    std::ostringstream out;
    out << "# 服务 我的账号 好友账号 备注\n";
    for (const auto& acc : accounts_) {
        for (const Friend& f : acc->friends().all()) {
            out << ServiceTypeHelper::code(acc->type()) << ' ' << acc->id() << ' ' << f.id() << ' '
                << Util::fieldText(f.remark()) << '\n';
        }
    }
    return out.str();
}

// ======================== 好友申请与验证、黑名单 ========================

std::string Platform::nickOf(ServiceType type, const std::string& id) const {
    ServiceBase* acc = findAccount(type, id);
    return acc ? acc->nickname() : id;
}

bool Platform::friendsBothWays(ServiceType type, const std::string& a, const std::string& b) const {
    if (a == b) return true;
    ServiceBase* x = findAccount(type, a);
    ServiceBase* y = findAccount(type, b);
    return x && y && x->friends().contains(b) && y->friends().contains(a);
}

void Platform::applyFriendship(ServiceType type, const std::string& requester, const std::string& target,
                                 const std::string& requesterRemark, const std::string& targetRemark) {
    ServiceBase* a = findAccount(type, requester);
    ServiceBase* b = findAccount(type, target);
    if (!a || !b || a == b) return;
    if (!a->friends().contains(target)) a->friends().add(target, requesterRemark.empty() || requesterRemark == "-" ? b->nickname() : requesterRemark);
    if (!b->friends().contains(requester)) b->friends().add(requester, targetRemark.empty() || targetRemark == "-" ? a->nickname() : targetRemark);
}

// 按先后顺序重放消息文件中的社交记录。所有操作都可以重复执行而结果不变（加好友前先看是否已是好友……），
// 所以本窗口自己写入的记录被再次应用也没有关系；多个窗口最终都得到与记录一致的结果
void Platform::syncSocial() {
    // 只读上次应用之后的新记录：每发一条私信都要调用，原来每次把全部记录读出来再跳过，
    // 1 万条记录时一条私信要多花几毫秒。
    // 位置前进到最后一条记录（不分类型），否则中间大量的朋友圈记录每次都要重新扫一遍
    const int end = messages_->lastRecordLine();
    if (end <= syncedLine_) return;
    const std::vector<Record> records = messages_->recordsAfter("FVDBS", syncedLine_);
    int last = end;
    for (const Record& r : records) {
        last = (std::max)(last, r.line);   // 读完位置之后、读记录之前别的窗口又写了几条：也已经应用了
        const std::string code = ServiceTypeHelper::code(r.service);
        switch (r.kind) {
        case 'V': {   // 申请序号 A/X 处理人 备注
            if (r.field(1) != "A") break;
            requestBook_.refresh();   // 要找到当初那条申请（F）
            if (const FriendRequest* request = requestBook_.find(toInt(r.field(0))))
                applyFriendship(r.service, request->from, request->to, request->remark, r.field(3));
            break;
        }
        case 'D':   // 删除者 被删除者
            if (ServiceBase* acc = findAccount(r.service, r.field(0))) acc->friends().remove(r.field(1));
            break;
        case 'B':   // 拉黑者 对方 1/0
            if (r.field(2) == "1") blocked_.insert(code + " " + r.field(0) + " " + r.field(1));
            else blocked_.erase(code + " " + r.field(0) + " " + r.field(1));
            break;
        case 'S':   // 账号 verify 1/0
            if (r.field(2) == "0") noVerify_.insert(code + " " + r.field(0));
            else noVerify_.erase(code + " " + r.field(0));
            break;
        }
    }
    syncedLine_ = last;
}

bool Platform::requestFriend(ServiceBase& me, const std::string& targetId, const std::string& greeting,
                               const std::string& remark, std::string& msg) {
    syncSocial();
    if (targetId == me.id()) {
        msg = "不能添加自己为好友。";
        return false;
    }
    ServiceBase* other = findAccount(me.type(), targetId);
    if (!other) {
        msg = me.serviceName() + "中不存在账号 " + targetId + "。";
        return false;
    }
    if (me.friends().contains(targetId)) {
        msg = other->displayName() + " 已经是你的好友。";
        return false;
    }
    std::string hello = Util::lineText(greeting);
    if (hello.empty()) hello = "我是" + me.nickname();
    const std::string myRemark = Util::toField(Util::lineText(remark));
    const bool stillFriend = other->friends().contains(me.id());   // 我曾单向删除对方，对方仍保留着我
    const bool direct = (stillFriend || !requiresVerification(*other)) && !isBlocked(me.type(), targetId, me.id());
    if (!messages_->appendRecord('F', me.type(), { targetId, me.id(), myRemark.empty() ? "-" : myRemark }, true, hello)) {
        msg = "好友申请发送失败，请检查 data 目录。";
        return false;
    }
    const int requestSeq = messages_->lastSeq();   // 申请占一个序号：就是刚分配的这个
    if (!direct) {   // 被对方拉黑时，申请照常“发出”，但对方看不到（与微信相同）
        msg = "好友申请已发送，等待 " + other->displayName() + " 验证。";
        return true;
    }
    // 不需要验证：自动通过（写一条处理人为 - 的“同意”记录，其他窗口据此同步）
    messages_->appendRecord('V', me.type(), { std::to_string(requestSeq), "A", "-", "-" }, false);
    applyFriendship(me.type(), me.id(), targetId, myRemark, "");
    msg = stillFriend ? other->displayName() + " 的好友列表中仍有你，已直接添加为好友。"
                      : other->displayName() + " 没有开启好友验证，已直接添加为好友。";
    return true;
}

std::vector<FriendRequest> Platform::friendRequests(const ServiceBase& me) {
    syncSocial();
    requestBook_.refresh();
    std::map<std::string, FriendRequest> incoming, outgoing;   // 同一个人发了几次，只保留最新的一条
    for (const FriendRequest* req : requestBook_.of(me.type(), me.id())) {   // 按序号递增：后面的覆盖前面的
        if (req->to == me.id() && !isBlocked(me.type(), me.id(), req->from)) incoming[req->from] = *req;
        if (req->from == me.id()) outgoing[req->to] = *req;
    }
    std::vector<FriendRequest> result;
    for (auto& kv : incoming) result.push_back(kv.second);
    for (auto& kv : outgoing) result.push_back(kv.second);
    std::sort(result.begin(), result.end(), [](const FriendRequest& a, const FriendRequest& b) { return a.seq > b.seq; });
    return result;
}

int Platform::pendingRequestCount(const ServiceBase& me) {
    int count = 0;
    for (const FriendRequest& r : friendRequests(me)) {
        if (r.to == me.id() && r.status == FriendRequest::PENDING) ++count;
    }
    return count;
}

bool Platform::respondRequest(ServiceBase& me, int seq, bool accept, const std::string& remark, std::string& msg) {
    for (const FriendRequest& r : friendRequests(me)) {
        if (r.seq != seq || r.to != me.id()) continue;
        if (r.status != FriendRequest::PENDING) {
            msg = "该申请已经处理过了。";
            return false;
        }
        if (!findAccount(me.type(), r.from)) {
            msg = "对方账号已关闭。";
            return false;
        }
        const std::string myRemark = Util::toField(Util::lineText(remark));
        messages_->appendRecord('V', me.type(), { std::to_string(seq), accept ? "A" : "X", me.id(), myRemark.empty() ? "-" : myRemark },
                               false);
        if (accept) {
            applyFriendship(me.type(), r.from, me.id(), r.remark, myRemark);
            msg = "已通过 " + displayName(me.type(), r.from) + " 的好友申请，你们已经是好友了。";
        } else {
            msg = "已拒绝 " + displayName(me.type(), r.from) + " 的好友申请。";
        }
        return true;
    }
    msg = "没有找到这条好友申请。";
    return false;
}

bool Platform::setBlocked(const ServiceBase& me, const std::string& id, bool on, std::string& msg) {
    syncSocial();
    if (id == me.id() || !findAccount(me.type(), id)) {
        msg = me.serviceName() + "中不存在账号 " + id + "。";
        return false;
    }
    if (on == isBlocked(me.type(), me.id(), id)) {
        msg = on ? "对方已经在黑名单中。" : "对方不在黑名单中。";
        return false;
    }
    messages_->appendRecord('B', me.type(), { me.id(), id, on ? "1" : "0" }, false);
    const std::string key = ServiceTypeHelper::code(me.type()) + " " + me.id() + " " + id;
    if (on) blocked_.insert(key);
    else blocked_.erase(key);
    msg = on ? "已将 " + displayName(me.type(), id) + " 加入黑名单。你将不再收到对方的消息和好友申请，对方也看不到你的" + me.momentsName() + "。"
             : "已将 " + displayName(me.type(), id) + " 移出黑名单。";
    return true;
}

bool Platform::isBlocked(ServiceType type, const std::string& blocker, const std::string& blocked) const {
    return blocked_.count(ServiceTypeHelper::code(type) + " " + blocker + " " + blocked) > 0;
}

std::vector<std::string> Platform::blockedList(const ServiceBase& me) const {
    std::vector<std::string> result;
    const std::string prefix = ServiceTypeHelper::code(me.type()) + " " + me.id() + " ";
    for (const std::string& key : blocked_) {
        if (key.compare(0, prefix.size(), prefix) == 0) result.push_back(key.substr(prefix.size()));
    }
    return result;
}

void Platform::setVerification(const ServiceBase& me, bool on) {
    messages_->appendRecord('S', me.type(), { me.id(), "verify", on ? "1" : "0" }, false);
    if (on) noVerify_.erase(ServiceTypeHelper::code(me.type()) + " " + me.id());
    else noVerify_.insert(ServiceTypeHelper::code(me.type()) + " " + me.id());
}

bool Platform::requiresVerification(const ServiceBase& account) const {
    return !noVerify_.count(ServiceTypeHelper::code(account.type()) + " " + account.id());
}

// ======================== 撤回与 @提醒 ========================

bool Platform::canRevoke(const ServiceBase& me, const Message& message) const {
    if (message.revoked() || message.isSystem() || message.service() != me.type()) return false;
    if (message.sender() == me.id()) return secondsSince(message.time()) <= 120;   // 自己的消息：2 分钟内
    if (!message.isGroup()) return false;
    const Group* g = groups_.find(me.type(), toInt(message.target()));
    // 群主、管理员撤回其他成员的消息：权限由群当前的管理策略决定（策略模式）
    return g && g->isMember(me.id()) && g->policy().canRevoke(*g, me.id(), message.sender());
}

bool Platform::revokeMessage(const ServiceBase& me, int seq, std::string& msg) {
    Message message(0, me.type(), false, "", "", "", "");
    if (!messages_->findMessage(me.type(), seq, message)) {
        msg = "消息不存在。";
        return false;
    }
    if (!canRevoke(me, message)) {
        msg = message.revoked()                  ? "这条消息已经撤回了。"
              : message.sender() == me.id()     ? "发送时间超过 2 分钟的消息，不能被撤回。"
                                                : "你没有撤回这条消息的权限。";
        return false;
    }
    if (!messages_->revoke(me.type(), seq, me.id())) {
        msg = "撤回失败，请检查 data 目录。";
        return false;
    }
    msg = "已撤回。";
    return true;
}

std::string Platform::revokeText(const ServiceBase& viewer, const Message& message) const {
    const std::string& by = message.revokedBy();
    auto who = [&](const std::string& id) { return id == viewer.id() ? std::string("你") : "“" + nickOf(viewer.type(), id) + "”"; };
    if (by == message.sender()) return who(by) + "撤回了一条消息";
    return who(by) + "撤回了" + who(message.sender()) + "的一条消息";   // 群主或管理员撤回成员的消息
}

std::vector<std::string> Platform::mentionsOf(const Group& group, const Message& message) const {
    std::vector<std::string> result;
    if (!message.isGroup() || message.isSystem() || message.revoked()) return result;
    const std::string& sender = message.sender();
    // @所有人：只有能修改群资料的人（群主；QQ 群模式下还有管理员）才有效
    if (message.text().find('@') == std::string::npos) return result;   // 绝大多数消息没有 @：不必逐个成员比对
    const bool everyone = Util::containsText(message.text(), "@所有人") && group.policy().canEditInfo(group, sender);
    for (const std::string& id : group.members()) {
        if (id == sender) continue;
        const std::string nick = nickOf(group.service(), id);
        if (everyone || (!nick.empty() && Util::containsText(message.text(), "@" + nick))) result.push_back(id);
    }
    return result;
}

int Platform::unreadMentions(const ServiceBase& me, int groupNo) const {
    const Group* g = groups_.find(me.type(), groupNo);
    if (!g) return 0;
    const int mark = messages_->groupReadMark(me.type(), groupNo, me.id());
    int count = 0;
    for (const Message& m : messages_->groupHistory(me.type(), groupNo)) {
        if (m.seq() <= mark || m.sender() == me.id()) continue;
        if (Util::contains(mentionsOf(*g, m), me.id())) ++count;
    }
    return count;
}

// ======================== 朋友圈 ========================

bool Platform::canSeeMoment(ServiceType type, const std::string& viewer, const std::string& author,
                              const std::string& scope) const {
    if (viewer == author) return true;
    // 互为好友、且没有被作者拉黑，才能看到；再按作者设置的可见范围判断
    if (!friendsBothWays(type, viewer, author) || isBlocked(type, author, viewer)) return false;
    if (scope == "self") return false;
    if (scope.compare(0, 5, "only:") == 0) return Util::contains(Util::parseList(scope.substr(5)), viewer);
    if (scope.compare(0, 7, "except:") == 0) return !Util::contains(Util::parseList(scope.substr(7)), viewer);
    return true;
}

bool Platform::postMoment(const ServiceBase& me, const std::string& text, const std::string& scope, std::string& msg) {
    const std::string line = Util::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    std::string range = scope.empty() ? "all" : scope;
    if ((range.compare(0, 5, "only:") == 0 && Util::parseList(range.substr(5)).empty()) ||
        (range.compare(0, 7, "except:") == 0 && Util::parseList(range.substr(7)).empty())) {
        msg = "请至少选择一位好友。";
        return false;
    }
    if (!messages_->appendRecord('P', me.type(), { me.id(), range }, true, line)) {
        msg = "发表失败，请检查 data 目录。";
        return false;
    }
    msg = "已发表到" + me.momentsName() + "（" + Moment::scopeText(range) + "）。";
    return true;
}

std::vector<Moment> Platform::moments(const ServiceBase& viewer, const std::string& author) {
    syncSocial();
    feed_.refresh();
    const ServiceType type = viewer.type();
    // 能看到的只有自己和好友的动态（canSeeMoment 要求互为好友），所以只看这些人的，不必遍历所有人的动态
    std::vector<std::string> authors;
    if (!author.empty()) {
        authors.push_back(author);
    } else {
        authors.push_back(viewer.id());
        for (const Friend& f : viewer.friends().all()) authors.push_back(f.id());
    }
    // 只显示查看者能看到的人的点赞和评论：作者本人看全部，其他人只看共同好友的
    auto visible = [&](const std::string& postAuthor, const std::string& person) {
        return viewer.id() == postAuthor || person == viewer.id() || friendsBothWays(type, viewer.id(), person);
    };
    std::vector<Moment> result;
    for (const std::string& id : authors) {
        for (const MomentFeed::Post* p : feed_.postsOf(type, id)) {
            if (p->deleted || !canSeeMoment(type, viewer.id(), p->author, p->scope)) continue;
            Moment m;
            m.seq = p->seq;
            m.service = type;
            m.author = p->author;
            m.scope = p->scope;
            m.time = p->time;
            m.text = p->text;
            for (const std::string& person : p->likes) {
                if (visible(p->author, person)) m.likes.push_back(person);
            }
            for (int commentSeq : p->comments) {
                const MomentFeed::Comment* c = feed_.comment(commentSeq);
                if (c && !c->deleted && visible(p->author, c->author)) m.comments.push_back(*c);
            }
            result.push_back(std::move(m));
        }
    }
    std::sort(result.begin(), result.end(), [](const Moment& a, const Moment& b) { return a.seq > b.seq; });   // 最新的在前
    return result;
}

const MomentFeed::Post* Platform::visibleMoment(const ServiceBase& me, int seq) {
    syncSocial();
    feed_.refresh();
    const MomentFeed::Post* p = feed_.post(seq);
    if (!p || p->deleted || p->service != me.type() || !canSeeMoment(me.type(), me.id(), p->author, p->scope)) return nullptr;
    return p;
}

bool Platform::toggleLike(const ServiceBase& me, int seq, std::string& msg) {
    const MomentFeed::Post* p = visibleMoment(me, seq);
    if (!p) {
        msg = "这条动态不存在，或你没有权限查看。";
        return false;
    }
    const bool liked = Util::contains(p->likes, me.id());
    messages_->appendRecord('L', me.type(), { std::to_string(seq), me.id(), liked ? "0" : "1" }, false);
    msg = liked ? "已取消赞。" : "已赞。";
    return true;
}

bool Platform::commentMoment(const ServiceBase& me, int seq, const std::string& replyTo, const std::string& text,
                               std::string& msg) {
    const std::string line = Util::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    if (!visibleMoment(me, seq)) {
        msg = "这条动态不存在，或你没有权限查看。";
        return false;
    }
    messages_->appendRecord('K', me.type(), { std::to_string(seq), me.id(), replyTo.empty() ? "-" : replyTo }, true, line);
    msg = replyTo.empty() || replyTo == "-" ? "评论成功。" : "已回复 " + nickOf(me.type(), replyTo) + "。";
    return true;
}

bool Platform::deleteMoment(const ServiceBase& me, int seq, std::string& msg) {
    feed_.refresh();
    const MomentFeed::Post* p = feed_.post(seq);
    if (!p || p->service != me.type()) {
        msg = "这条动态不存在。";
        return false;
    }
    if (p->author != me.id()) {
        msg = "只能删除自己的动态。";
        return false;
    }
    messages_->appendRecord('Y', me.type(), { std::to_string(seq) }, false);
    msg = "动态已删除。";
    return true;
}

bool Platform::deleteComment(const ServiceBase& me, int commentSeq, std::string& msg) {
    feed_.refresh();
    const MomentFeed::Comment* c = feed_.comment(commentSeq);
    if (!c || c->service != me.type()) {
        msg = "这条评论不存在。";
        return false;
    }
    const MomentFeed::Post* p = feed_.post(c->postSeq);
    if (c->author != me.id() && (!p || p->author != me.id())) {   // 评论者本人或动态作者可以删除
        msg = "只能删除自己的评论，或自己动态下的评论。";
        return false;
    }
    messages_->appendRecord('Z', me.type(), { std::to_string(commentSeq) }, false);
    msg = "评论已删除。";
    return true;
}

std::vector<std::string> Platform::momentNotices(const ServiceBase& me, bool unreadOnly) {
    feed_.refresh();
    const int mark = unreadOnly ? messages_->readMark(me.type(), "O", "-", me.id()) : 0;
    const std::vector<MomentFeed::Interaction>& interactions = feed_.interactionsOf(me.type(), me.id());
    std::vector<std::string> result;
    for (auto it = interactions.rbegin(); it != interactions.rend(); ++it) {   // 最新的在前
        const MomentFeed::Interaction& x = *it;
        if (x.seq <= mark || x.actor == me.id()) continue;
        const MomentFeed::Post* post = feed_.post(x.postSeq);
        if (!post || post->deleted || post->service != me.type()) continue;
        const bool myPost = post->author == me.id();
        const std::string who = nickOf(me.type(), x.actor);
        const std::string what = "“" + Util::truncate(post->text, 12) + "”";
        if (x.kind == 'L') {
            if (myPost) result.push_back(who + " 赞了你的动态 " + what);
            continue;
        }
        const MomentFeed::Comment* c = feed_.comment(x.seq);
        if (!c || c->deleted) continue;
        if (c->replyTo == me.id()) result.push_back(who + " 回复了你：" + c->text);
        else if (myPost) result.push_back(who + " 评论了你的动态 " + what + "：" + c->text);
    }
    return result;
}

void Platform::markMomentsRead(const ServiceBase& me) {
    feed_.refresh();
    messages_->markRead(me.type(), "O", "-", me.id(), feed_.lastInteraction(me.type()));
}

// ======================== 群事件 → 系统提示 ========================

void Platform::onGroupEvent(const Group& group, GroupEvent event, const std::string& actor, const std::string& target) {
    const ServiceType type = group.service();
    const std::string a = nickOf(type, actor), b = nickOf(type, target);
    std::string text;
    switch (event) {
    case GroupEvent::INVITED: text = a + " 邀请 " + b + " 加入了群聊"; break;
    case GroupEvent::APPROVED: text = b + " 经 " + a + " 同意加入了群聊"; break;
    case GroupEvent::CLAIMED: text = a + " 加入了群聊并成为新群主（原群主已注销）"; break;
    case GroupEvent::QUIT: text = a + " 退出了群聊"; break;
    case GroupEvent::KICKED: text = a + " 将 " + b + " 移出了群聊"; break;
    case GroupEvent::RENAMED: text = a + " 修改群名为“" + target + "”"; break;
    case GroupEvent::TRANSFERRED: text = a + " 已将群主转让给 " + b; break;
    case GroupEvent::MODE_SWITCHED: text = "群主 " + a + " 将本群切换为" + target; break;
    case GroupEvent::MUTED: text = a + " 将 " + b + " 禁言"; break;
    case GroupEvent::UNMUTED: text = a + " 解除了 " + b + " 的禁言"; break;
    case GroupEvent::ADMIN_SET: text = a + " 将 " + b + " 设为管理员"; break;
    case GroupEvent::ADMIN_UNSET: text = a + " 取消了 " + b + " 的管理员身份"; break;
    }
    messages_->postGroup(type, group.groupNo(), Message::systemSender(), text);
}
