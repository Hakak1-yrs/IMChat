#include "PlatformYD.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iterator>
#include <sstream>
#include "IndependentIdServiceYD.h"
#include "QQServiceYD.h"
#include "UtilYD.h"
#include "WeChatServiceYD.h"
#include "WeiShangServiceYD.h"
#include "WeiboServiceYD.h"

namespace {

// 说明两个账号为什么对应不上（用于提示信息）
std::string linkProblem(const ServiceBaseYD& a, const ServiceBaseYD& b) {
    for (const ServiceBaseYD* acc : { &a, &b }) {
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
    if (UtilYD::charCount(text) > 250) {
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
    if (sscanf_s(time.c_str(), "%d-%d-%d %d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6)
        return 1e9;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    t.tm_isdst = -1;
    return std::difftime(std::time(nullptr), std::mktime(&t));
}

}  // namespace

PlatformYD::PlatformYD(const std::string& dataDir)
    : dataDir_(dataDir), sessionStamp_(DateYD::nowStamp()), messages_(dataDir + "messages.txt"),
      usersFile_(dataDir, "users.txt"), accountsFile_(dataDir, "accounts.txt"), friendsFile_(dataDir, "friends.txt"),
      groupsFile_(dataDir, "groups.txt"), subGroupsFile_(dataDir, "subgroups.txt") {
    // 文件缺失时以空数据启动，退出时会自动创建数据文件
    std::string users, accounts, friends, groups, subGroups;
    if (!usersFile_.read(users)) std::cout << "[系统] 未找到 users.txt\n";
    if (!accountsFile_.read(accounts)) std::cout << "[系统] 未找到 accounts.txt\n";
    if (!friendsFile_.read(friends)) std::cout << "[系统] 未找到 friends.txt\n";
    if (!groupsFile_.read(groups)) std::cout << "[系统] 未找到 groups.txt\n";
    subGroupsFile_.read(subGroups);
    for (const DataFileYD* f : { &usersFile_, &accountsFile_, &friendsFile_, &groupsFile_, &subGroupsFile_ }) {
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

    // 题目约定每个微X服务都有 1001～1006 号群：有账号但还没有群的服务（如新增的微商）补建预置群
    for (ServiceTypeYD type : ServiceTypeHelperYD::all()) {
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
        for (const DataFileYD* f : { &usersFile_, &accountsFile_, &friendsFile_, &groupsFile_, &subGroupsFile_ })
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

PlatformYD::~PlatformYD() {
    std::cout << (save() ? "[系统] 数据已写回文件。\n" : "[系统] 数据写回失败，请检查 data 目录！\n");
}

bool PlatformYD::save() const {
    std::lock_guard<std::mutex> lock(saveMutex_);
    return finalSaved_ ? true : writeAll();
}

bool PlatformYD::saveOnExit() const {
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
bool PlatformYD::writeAll() const {
    DataFileYD* files[] = { &usersFile_, &accountsFile_, &friendsFile_, &groupsFile_, &subGroupsFile_ };
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

const std::string& PlatformYD::dataDir() const { return dataDir_; }

// ======================== 用户与账号 ========================

ServiceBaseYD* PlatformYD::findAccount(ServiceTypeYD type, const std::string& id) const {
    for (const auto& acc : accounts_) {
        if (acc->type() == type && acc->id() == id) return acc.get();
    }
    return nullptr;
}

UserYD* PlatformYD::findUser(int userId) {
    auto it = users_.find(userId);
    return it == users_.end() ? nullptr : &it->second;
}

UserYD* PlatformYD::ownerOf(const ServiceBaseYD* account) {
    return account ? findUser(account->ownerId()) : nullptr;
}

const std::map<int, UserYD>& PlatformYD::users() const { return users_; }

std::string PlatformYD::displayName(ServiceTypeYD type, const std::string& id) const {
    if (id.empty()) return "（无）";
    ServiceBaseYD* acc = findAccount(type, id);
    return acc ? acc->displayName() : id + "(已注销)";
}

bool PlatformYD::canRegisterWith(ServiceTypeYD type, std::string& msg) const {
    UserYD candidate;
    return !generateId(candidate, type, msg).empty();
}

ServiceBaseYD* PlatformYD::registerUser(const std::string& name, ServiceTypeYD type, const std::string& nickname,
                                        const DateYD& birthday, const std::string& location,
                                        const std::string& password, std::string& msg) {
    int newId = users_.empty() ? 1 : users_.rbegin()->first + 1;
    UserYD candidate(newId, name);
    if (generateId(candidate, type, msg).empty()) return nullptr;   // 先检查能否开通，避免留下空用户
    UserYD& user = users_.emplace(newId, candidate).first->second;
    return openService(user, type, nickname, birthday, location, password, msg);
}

ServiceBaseYD* PlatformYD::openService(UserYD& user, ServiceTypeYD type, const std::string& nickname,
                                       const DateYD& birthday, const std::string& location,
                                       const std::string& password, std::string& msg) {
    if (user.hasService(type)) {
        msg = "你已经开通过" + ServiceTypeHelperYD::name(type) + "。";
        return nullptr;
    }
    std::string id = generateId(user, type, msg);
    if (id.empty()) return nullptr;
    auto account = createService(type, id, user.id(), nickname, birthday, DateYD::today(), location, password);
    ServiceBaseYD* raw = account.get();
    user.attach(raw);
    accounts_.push_back(std::move(account));
    msg = "开通成功！你的" + raw->idLabel() + "为：" + id;
    // 该服务还没有任何群（如新增的微商服务），按题目约定建立 1001～1006 号群
    if (groups_.groupsOfService(type).empty()) {
        groups_.createPresetGroups(type, id, raw->defaultGroupMode());
        msg += "。你是第一位" + ServiceTypeHelperYD::name(type) + "用户，已为你创建 1001～1006 号群";
    }
    return raw;
}

bool PlatformYD::closeService(UserYD& user, ServiceTypeYD type, std::string& msg) {
    ServiceBaseYD* account = user.service(type);
    if (!account) {
        msg = "你没有开通" + ServiceTypeHelperYD::name(type) + "。";
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
    if (type == ServiceTypeYD::QQ) {
        for (ServiceBaseYD* s : user.services()) {
            auto* independent = dynamic_cast<IndependentIdServiceYD*>(s);
            if (independent && independent->boundQQ() == id) independent->unbindQQ();
        }
    }
    // 4. 与该账号有关的私信、好友申请、黑名单、朋友圈作废：号码以后被重新分配时，新主人不会看到这些内容
    messages_.clearAccount(type, id);
    const std::string code = ServiceTypeHelperYD::code(type);
    for (auto it = blocked_.begin(); it != blocked_.end();) {
        std::istringstream iss(*it);
        std::string c, blocker, blocked;
        iss >> c >> blocker >> blocked;
        it = (c == code && (blocker == id || blocked == id)) ? blocked_.erase(it) : std::next(it);
    }
    noVerify_.erase(code + " " + id);
    // 5. 删除账号对象本身
    user.detach(type);
    accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(),
                                   [account](const std::unique_ptr<ServiceBaseYD>& a) { return a.get() == account; }),
                    accounts_.end());
    msg = "已关闭" + ServiceTypeHelperYD::name(type) + "（" + id + "），相关好友关系、群成员身份和私信已一并清除。";
    return true;
}

void PlatformYD::deleteUser(int userId, std::string& msg) {
    UserYD* user = findUser(userId);
    if (!user) {
        msg = "用户不存在。";
        return;
    }
    std::string name = user->name();
    for (ServiceBaseYD* s : user->services()) {
        std::string ignored;
        closeService(*user, s->type(), ignored);
    }
    users_.erase(userId);
    msg = "用户 " + name + " 已注销，全部服务账号及相关数据已删除。";
}

// 工厂方法：根据服务类型创建对应的派生类对象
std::unique_ptr<ServiceBaseYD> PlatformYD::createService(ServiceTypeYD type, const std::string& id, int ownerId,
                                                         const std::string& nickname, const DateYD& birthday,
                                                         const DateYD& registerDate, const std::string& location,
                                                         const std::string& password) {
    switch (type) {
    case ServiceTypeYD::QQ:
        return std::make_unique<QQServiceYD>(id, ownerId, nickname, birthday, registerDate, location, password);
    case ServiceTypeYD::WECHAT:
        return std::make_unique<WeChatServiceYD>(id, ownerId, nickname, birthday, registerDate, location, password);
    case ServiceTypeYD::WEIBO:
        return std::make_unique<WeiboServiceYD>(id, ownerId, nickname, birthday, registerDate, location, password);
    case ServiceTypeYD::WEISHANG:
        return std::make_unique<WeiShangServiceYD>(id, ownerId, nickname, birthday, registerDate, location, password);
    }
    return nullptr;
}

// 账号分配规则：QQ 号顺序递增；微博与 QQ 共享号码；微信、微商使用各自前缀的独立号码
std::string PlatformYD::generateId(const UserYD& user, ServiceTypeYD type, std::string& msg) const {
    if (type == ServiceTypeYD::QQ || type == ServiceTypeYD::WEIBO) {
        // 共享号码：本人已有 QQ 或微博中的任意一个，另一个就沿用同一号码
        ServiceTypeYD partner = (type == ServiceTypeYD::QQ) ? ServiceTypeYD::WEIBO : ServiceTypeYD::QQ;
        if (ServiceBaseYD* shared = user.service(partner)) return shared->id();
        if (type == ServiceTypeYD::WEIBO) {
            msg = "微博与QQ共享号码，请先开通QQ服务。";
            return "";
        }
        long long maxId = 10000;   // QQ 与微博共用一个号码段
        for (const auto& acc : accounts_) {
            if (acc->usesQQNumber()) maxId = std::max(maxId, std::atoll(acc->id().c_str()));
        }
        return std::to_string(maxId + 1);
    }
    const std::string prefix = (type == ServiceTypeYD::WECHAT) ? "wx" : "ws";
    int maxNo = (type == ServiceTypeYD::WECHAT) ? 2000 : 3000;
    for (const auto& acc : accounts_) {
        if (acc->type() == type && acc->id().size() > 2) maxNo = std::max(maxNo, std::atoi(acc->id().c_str() + 2));
    }
    return prefix + std::to_string(maxNo + 1);
}

// ======================== 跨服务身份对应 ========================

bool PlatformYD::isLinked(const ServiceBaseYD& a, const ServiceBaseYD& b) {
    return a.ownerId() == b.ownerId() && !a.linkedQQ().empty() && a.linkedQQ() == b.linkedQQ();
}

bool PlatformYD::bindQQ(UserYD& user, IndependentIdServiceYD& account, std::string& msg) {
    if (user.service(account.type()) != &account) {
        msg = "只能为自己的账号绑定QQ。";
        return false;
    }
    if (account.isBound()) {
        msg = "已经绑定了QQ " + account.boundQQ() + "，如需更换请先解绑。";
        return false;
    }
    ServiceBaseYD* qq = user.service(ServiceTypeYD::QQ);
    if (!qq) {
        msg = "你还没有开通QQ，无法绑定（只能绑定本人的QQ号）。";
        return false;
    }
    account.bindQQ(qq->id());
    msg = account.serviceName() + "已与QQ号 " + qq->id() + " 绑定，现在可以和QQ、微博互认好友了。";
    return true;
}

bool PlatformYD::unbindQQ(IndependentIdServiceYD& account, std::string& msg) {
    if (!account.isBound()) {
        msg = account.serviceName() + "还没有绑定QQ。";
        return false;
    }
    msg = account.serviceName() + "已与QQ号 " + account.boundQQ() + " 解除绑定。";
    account.unbindQQ();
    return true;
}

bool PlatformYD::checkOwnLink(const ServiceBaseYD* first, const ServiceBaseYD* second, std::string& msg) const {
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

bool PlatformYD::addFriend(ServiceBaseYD& me, const std::string& friendId, const std::string& remark,
                           std::string& msg) {
    if (friendId == me.id()) {
        msg = "不能添加自己为好友。";
        return false;
    }
    ServiceBaseYD* other = findAccount(me.type(), friendId);
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

bool PlatformYD::removeFriend(ServiceBaseYD& me, const std::string& friendId, std::string& msg) {
    syncSocial();
    if (!me.friends().remove(friendId)) {
        msg = friendId + " 不是你的好友。";
        return false;
    }
    messages_.appendRecord('D', me.type(), { me.id(), friendId }, false);   // 其他窗口据此同步
    msg = "已删除好友 " + displayName(me.type(), friendId) + "。对方的好友列表中仍有你，对方再给你发消息时会被拒绝。";
    return true;
}

std::vector<FriendYD> PlatformYD::searchFriends(const ServiceBaseYD& me, const std::string& keyword) const {
    std::vector<FriendYD> result;
    for (const FriendYD& f : me.friends().all()) {
        ServiceBaseYD* acc = findAccount(me.type(), f.id());
        bool hit = UtilYD::containsText(f.id(), keyword) || UtilYD::containsText(f.remark(), keyword) ||
                   (acc && UtilYD::containsText(acc->nickname(), keyword));
        if (hit) result.push_back(f);
    }
    return result;
}

bool PlatformYD::commonFriends(const UserYD& user, ServiceTypeYD a, ServiceTypeYD b, std::vector<UserYD*>& result,
                               std::vector<std::string>& skipped, std::string& msg) {
    result.clear();
    skipped.clear();
    ServiceBaseYD* mineA = user.service(a);
    ServiceBaseYD* mineB = user.service(b);
    if (!checkOwnLink(mineA, mineB, msg)) return false;
    // 以“人”为纽带：a 服务中的好友 → 此人 → 此人在 b 服务中的账号，且两边账号必须对应同一个 QQ 号
    for (const FriendYD& f : mineA->friends().all()) {
        ServiceBaseYD* accA = findAccount(a, f.id());
        UserYD* person = accA ? findUser(accA->ownerId()) : nullptr;
        ServiceBaseYD* accB = person ? person->service(b) : nullptr;
        if (!accB || !mineB->friends().contains(accB->id())) continue;   // 在 b 服务中不是我的好友
        if (isLinked(*accA, *accB)) result.push_back(person);
        else skipped.push_back(person->name() + "：" + linkProblem(*accA, *accB) + "，无法确认两个账号是同一人");
    }
    return true;
}

bool PlatformYD::recommendFriends(const UserYD& user, ServiceTypeYD target, ServiceTypeYD source,
                                  std::vector<ServiceBaseYD*>& result, std::vector<std::string>& skipped,
                                  std::string& msg) {
    result.clear();
    skipped.clear();
    ServiceBaseYD* mine = user.service(target);
    ServiceBaseYD* mineSource = user.service(source);
    if (!checkOwnLink(mineSource, mine, msg)) return false;
    for (const FriendYD& f : mineSource->friends().all()) {
        ServiceBaseYD* from = findAccount(source, f.id());
        UserYD* person = from ? findUser(from->ownerId()) : nullptr;
        ServiceBaseYD* acc = person ? person->service(target) : nullptr;
        if (!acc || mine->friends().contains(acc->id())) continue;   // 对方没开通目标服务，或已经是好友
        if (isLinked(*from, *acc)) result.push_back(acc);
        else skipped.push_back(from->displayName() + "：" + linkProblem(*from, *acc) + "，无法确认是同一人");
    }
    return true;
}

std::vector<std::string> PlatformYD::mutualFriends(const ServiceBaseYD& me, const ServiceBaseYD& other) const {
    std::vector<std::string> result;
    for (const FriendYD& f : me.friends().all()) {
        if (f.id() != other.id() && other.friends().contains(f.id())) result.push_back(f.id());
    }
    return result;
}

GroupManagerYD& PlatformYD::groups() { return groups_; }

// ======================== 群的新建与解散 ========================

GroupYD* PlatformYD::createGroup(const ServiceBaseYD& me, const std::string& name, std::string& msg) {
    const std::string groupName = UtilYD::toField(UtilYD::lineText(name));
    if (groupName.empty()) {
        msg = "群名称不能为空。";
        return nullptr;
    }
    GroupYD* group = groups_.createGroup(me.type(), groupName, me.id(), me.defaultGroupMode());
    messages_.postGroup(me.type(), group->groupNo(), MessageYD::systemSender(), nickOf(me.type(), me.id()) + " 创建了群聊");
    msg = "群 " + std::to_string(group->groupNo()) + "（" + groupName + "）创建成功，你是群主，管理模式为" +
          group->policy().modeName() + "。";
    return group;
}

bool PlatformYD::dissolveGroup(const ServiceBaseYD& me, int groupNo, std::string& msg) {
    GroupYD* group = groups_.find(me.type(), groupNo);
    if (!group) {
        msg = me.serviceName() + "中不存在群 " + std::to_string(groupNo) + "。";
        return false;
    }
    if (!group->isOwner(me.id())) {
        msg = "只有群主可以解散群。";
        return false;
    }
    if (GroupManagerYD::isPreset(groupNo)) {
        msg = "1001～1006 是平台预置群，不能解散（可以转让群主）。";
        return false;
    }
    const std::string name = group->name();
    groups_.remove(me.type(), groupNo);
    messages_.clearGroup(me.type(), groupNo);   // 群号以后可能分配给新群，旧的群聊记录随之作废
    msg = "群 " + std::to_string(groupNo) + "（" + name + "）已解散，群聊记录一并清除。";
    return true;
}

// ======================== 消息 ========================

MessageCenterYD& PlatformYD::messages() { return messages_; }

bool PlatformYD::sendPrivate(const ServiceBaseYD& me, const std::string& friendId, const std::string& text,
                             std::string& msg) {
    syncSocial();   // 对方可能刚在另一个窗口删除了我或把我拉黑
    ServiceBaseYD* other = findAccount(me.type(), friendId);
    if (!me.friends().contains(friendId) || !other) {
        msg = friendId + " 不是你的" + me.serviceName() + "好友，只能给好友发私信。";
        return false;
    }
    const std::string line = UtilYD::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    if (isBlocked(me.type(), friendId, me.id())) {   // 与微信相同的提示
        msg = "消息已发出，但被对方拒收了。";
        return false;
    }
    if (!other->friends().contains(me.id())) {
        msg = "对方开启了朋友验证，你还不是他（她）的朋友。请先发送好友申请，对方验证通过后，才能聊天。";
        return false;
    }
    if (!messages_.postPrivate(me.type(), me.id(), friendId, line)) {
        msg = "消息保存失败，请检查 data 目录。";
        return false;
    }
    msg = "已发送给 " + displayName(me.type(), friendId) + "。";
    return true;
}

bool PlatformYD::sendGroupMessage(const ServiceBaseYD& me, const GroupYD& group, const std::string& text,
                                  std::string& msg) {
    if (group.service() != me.type() || !group.isMember(me.id())) {
        msg = "你不是该群成员，不能发群消息。";
        return false;
    }
    if (!group.canSpeak(me.id())) {
        msg = "你已被禁言，不能在本群发言。";
        return false;
    }
    const std::string line = UtilYD::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    if (!messages_.postGroup(me.type(), group.groupNo(), me.id(), line)) {
        msg = "消息保存失败，请检查 data 目录。";
        return false;
    }
    msg = "群消息已发送。";
    return true;
}

void PlatformYD::unreadCount(const ServiceBaseYD& me, int& privateCount, int& groupCount) const {
    privateCount = groupCount = 0;
    for (const auto& kv : messages_.unreadPrivate(me.type(), me.id())) privateCount += kv.second;
    std::vector<int> groupNos;
    for (GroupYD* g : groups_.groupsOf(me.type(), me.id())) groupNos.push_back(g->groupNo());
    for (const auto& kv : messages_.unreadGroups(me.type(), me.id(), groupNos)) groupCount += kv.second;
}

// ======================== 文本与对象的转换 ========================

// users.txt 每行：用户编号 姓名
int PlatformYD::loadUsers(const std::string& text) {
    int bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = UtilYD::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        int id = 0;
        std::string name;
        if (iss >> id >> name && id > 0 && !users_.count(id)) users_.emplace(id, UserYD(id, name));
        else ++bad;
    }
    return bad;
}

// 共享号码规则：QQ、微博的号码必须是数字；同一个人的 QQ 与微博号码相同；一个号码不能属于两个人
bool PlatformYD::sharedNumberOk(const UserYD& owner, const ServiceBaseYD& account) const {
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
int PlatformYD::loadAccounts(const std::string& text) {
    int bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = UtilYD::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string typeCode, id, nickname, birthText, registerText, location, password, extra;
        int ownerId = 0;
        ServiceTypeYD type;
        DateYD birthday, registerDate;
        UserYD* owner = nullptr;
        if (!(iss >> typeCode >> id >> ownerId >> nickname >> birthText >> registerText >> location >> password >> extra) ||
            !(owner = findUser(ownerId)) || !ServiceTypeHelperYD::parse(typeCode, type) ||
            !DateYD::parse(birthText, birthday) || !DateYD::parse(registerText, registerDate) ||
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
        accounts_.push_back(std::move(account));
    }
    return bad;
}

// friends.txt 每行：服务 我的账号 好友账号 备注
int PlatformYD::loadFriends(const std::string& text) {
    int bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = UtilYD::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string typeCode, mine, other, remark;
        ServiceTypeYD type;
        ServiceBaseYD* me = nullptr;
        if (!(iss >> typeCode >> mine >> other >> remark) || !ServiceTypeHelperYD::parse(typeCode, type) ||
            mine == other || !(me = findAccount(type, mine)) || !findAccount(type, other)) {
            ++bad;
            continue;
        }
        me->friends().add(other, remark);
    }
    return bad;
}

int PlatformYD::repairData() {
    int repaired = 0;
    for (const auto& acc : accounts_) {
        // 1. 绑定只能指向本人已开通的 QQ 号，否则解除
        auto* independent = dynamic_cast<IndependentIdServiceYD*>(acc.get());
        if (independent && independent->isBound()) {
            UserYD* owner = findUser(acc->ownerId());
            ServiceBaseYD* qq = owner ? owner->service(ServiceTypeYD::QQ) : nullptr;
            if (!qq || qq->id() != independent->boundQQ()) {
                independent->unbindQQ();
                ++repaired;
            }
        }
        // （好友关系允许单向：与微信相同，删除好友只删除自己这一方，所以不再把单向好友补成双向）
    }
    // 3. 群里不存在的账号（成员、管理员、申请人）一律移除；被移除的是群主时按接任规则移交
    for (ServiceTypeYD type : ServiceTypeHelperYD::all()) {
        for (GroupYD* g : groups_.groupsOfService(type)) {
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

std::string PlatformYD::usersText() const {
    std::ostringstream out;
    out << "# 用户编号 姓名\n";
    for (const auto& kv : users_) out << kv.first << ' ' << UtilYD::fieldText(kv.second.name()) << '\n';
    return out.str();
}

std::string PlatformYD::accountsText() const {
    std::ostringstream out;
    out << "# 服务 账号 用户编号 昵称 出生日期 申请日期 所在地 密码记录（盐$SHA-256摘要） 扩展字段（微信、微商为绑定的QQ号）\n";
    // 自由输入的文字字段再统一过滤一次：即使业务接口收到含空格的内容，也不会写坏数据文件
    for (const auto& acc : accounts_) {
        out << ServiceTypeHelperYD::code(acc->type()) << ' ' << acc->id() << ' ' << acc->ownerId() << ' '
            << UtilYD::fieldText(acc->nickname()) << ' ' << acc->birthday().toString() << ' '
            << acc->registerDate().toString() << ' ' << UtilYD::fieldText(acc->location()) << ' ' << acc->password()
            << ' ' << acc->extraData() << '\n';
    }
    return out.str();
}

std::string PlatformYD::friendsText() const {
    std::ostringstream out;
    out << "# 服务 我的账号 好友账号 备注\n";
    for (const auto& acc : accounts_) {
        for (const FriendYD& f : acc->friends().all()) {
            out << ServiceTypeHelperYD::code(acc->type()) << ' ' << acc->id() << ' ' << f.id() << ' '
                << UtilYD::fieldText(f.remark()) << '\n';
        }
    }
    return out.str();
}

// ======================== 好友申请与验证、黑名单 ========================

std::string PlatformYD::nickOf(ServiceTypeYD type, const std::string& id) const {
    ServiceBaseYD* acc = findAccount(type, id);
    return acc ? acc->nickname() : id;
}

bool PlatformYD::friendsBothWays(ServiceTypeYD type, const std::string& a, const std::string& b) const {
    if (a == b) return true;
    ServiceBaseYD* x = findAccount(type, a);
    ServiceBaseYD* y = findAccount(type, b);
    return x && y && x->friends().contains(b) && y->friends().contains(a);
}

void PlatformYD::applyFriendship(ServiceTypeYD type, const std::string& requester, const std::string& target,
                                 const std::string& requesterRemark, const std::string& targetRemark) {
    ServiceBaseYD* a = findAccount(type, requester);
    ServiceBaseYD* b = findAccount(type, target);
    if (!a || !b || a == b) return;
    if (!a->friends().contains(target)) a->friends().add(target, requesterRemark.empty() || requesterRemark == "-" ? b->nickname() : requesterRemark);
    if (!b->friends().contains(requester)) b->friends().add(requester, targetRemark.empty() || targetRemark == "-" ? a->nickname() : targetRemark);
}

// 按先后顺序重放消息文件中的社交记录。所有操作都可以重复执行而结果不变（加好友前先看是否已是好友……），
// 所以本窗口自己写入的记录被再次应用也没有关系；多个窗口最终都得到与记录一致的结果
void PlatformYD::syncSocial() {
    std::vector<RecordYD> records = messages_.records("FVDBS");
    std::map<int, const RecordYD*> requests;
    for (const RecordYD& r : records) {
        if (r.kind == 'F') requests[r.seq] = &r;
    }
    int last = syncedLine_;
    for (const RecordYD& r : records) {
        if (r.line <= syncedLine_) continue;
        last = (std::max)(last, r.line);
        const std::string code = ServiceTypeHelperYD::code(r.service);
        switch (r.kind) {
        case 'V': {   // 申请序号 A/X 处理人 备注
            auto it = requests.find(toInt(r.field(0)));
            if (r.field(1) == "A" && it != requests.end())
                applyFriendship(r.service, it->second->field(1), it->second->field(0), it->second->field(2), r.field(3));
            break;
        }
        case 'D':   // 删除者 被删除者
            if (ServiceBaseYD* acc = findAccount(r.service, r.field(0))) acc->friends().remove(r.field(1));
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

bool PlatformYD::requestFriend(ServiceBaseYD& me, const std::string& targetId, const std::string& greeting,
                               const std::string& remark, std::string& msg) {
    syncSocial();
    if (targetId == me.id()) {
        msg = "不能添加自己为好友。";
        return false;
    }
    ServiceBaseYD* other = findAccount(me.type(), targetId);
    if (!other) {
        msg = me.serviceName() + "中不存在账号 " + targetId + "。";
        return false;
    }
    if (me.friends().contains(targetId)) {
        msg = other->displayName() + " 已经是你的好友。";
        return false;
    }
    std::string hello = UtilYD::lineText(greeting);
    if (hello.empty()) hello = "我是" + me.nickname();
    const std::string myRemark = UtilYD::toField(UtilYD::lineText(remark));
    const bool stillFriend = other->friends().contains(me.id());   // 我曾单向删除对方，对方仍保留着我
    const bool direct = (stillFriend || !requiresVerification(*other)) && !isBlocked(me.type(), targetId, me.id());
    messages_.appendRecord('F', me.type(), { targetId, me.id(), myRemark.empty() ? "-" : myRemark }, true, hello);
    if (!direct) {   // 被对方拉黑时，申请照常“发出”，但对方看不到（与微信相同）
        msg = "好友申请已发送，等待 " + other->displayName() + " 验证。";
        return true;
    }
    // 不需要验证：自动通过（写一条处理人为 - 的“同意”记录，其他窗口据此同步）
    std::vector<RecordYD> sent = messages_.records("F");
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
        if (it->service == me.type() && it->field(0) == targetId && it->field(1) == me.id()) {
            messages_.appendRecord('V', me.type(), { std::to_string(it->seq), "A", "-", "-" }, false);
            break;
        }
    }
    applyFriendship(me.type(), me.id(), targetId, myRemark, "");
    msg = stillFriend ? other->displayName() + " 的好友列表中仍有你，已直接添加为好友。"
                      : other->displayName() + " 没有开启好友验证，已直接添加为好友。";
    return true;
}

std::vector<FriendRequestYD> PlatformYD::friendRequests(const ServiceBaseYD& me) {
    syncSocial();
    std::vector<RecordYD> records = messages_.records("FV");
    std::map<int, FriendRequestYD::StatusYD> status;
    for (const RecordYD& r : records) {
        if (r.kind == 'V') status[toInt(r.field(0))] = r.field(1) == "A" ? FriendRequestYD::ACCEPTED : FriendRequestYD::REJECTED;
    }
    std::map<std::string, FriendRequestYD> incoming, outgoing;   // 同一个人发了几次，只保留最新的一条
    for (const RecordYD& r : records) {
        if (r.kind != 'F' || r.service != me.type()) continue;
        FriendRequestYD req;
        req.seq = r.seq;
        req.service = r.service;
        req.to = r.field(0);
        req.from = r.field(1);
        req.remark = r.field(2) == "-" ? "" : r.field(2);
        req.greeting = r.text;
        req.time = r.time;
        if (status.count(r.seq)) req.status = status[r.seq];
        if (req.to == me.id() && !isBlocked(me.type(), me.id(), req.from)) incoming[req.from] = req;
        if (req.from == me.id()) outgoing[req.to] = req;
    }
    std::vector<FriendRequestYD> result;
    for (auto& kv : incoming) result.push_back(kv.second);
    for (auto& kv : outgoing) result.push_back(kv.second);
    std::sort(result.begin(), result.end(), [](const FriendRequestYD& a, const FriendRequestYD& b) { return a.seq > b.seq; });
    return result;
}

int PlatformYD::pendingRequestCount(const ServiceBaseYD& me) {
    int count = 0;
    for (const FriendRequestYD& r : friendRequests(me)) {
        if (r.to == me.id() && r.status == FriendRequestYD::PENDING) ++count;
    }
    return count;
}

bool PlatformYD::respondRequest(ServiceBaseYD& me, int seq, bool accept, const std::string& remark, std::string& msg) {
    for (const FriendRequestYD& r : friendRequests(me)) {
        if (r.seq != seq || r.to != me.id()) continue;
        if (r.status != FriendRequestYD::PENDING) {
            msg = "该申请已经处理过了。";
            return false;
        }
        if (!findAccount(me.type(), r.from)) {
            msg = "对方账号已关闭。";
            return false;
        }
        const std::string myRemark = UtilYD::toField(UtilYD::lineText(remark));
        messages_.appendRecord('V', me.type(), { std::to_string(seq), accept ? "A" : "X", me.id(), myRemark.empty() ? "-" : myRemark },
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

bool PlatformYD::setBlocked(const ServiceBaseYD& me, const std::string& id, bool on, std::string& msg) {
    syncSocial();
    if (id == me.id() || !findAccount(me.type(), id)) {
        msg = me.serviceName() + "中不存在账号 " + id + "。";
        return false;
    }
    if (on == isBlocked(me.type(), me.id(), id)) {
        msg = on ? "对方已经在黑名单中。" : "对方不在黑名单中。";
        return false;
    }
    messages_.appendRecord('B', me.type(), { me.id(), id, on ? "1" : "0" }, false);
    const std::string key = ServiceTypeHelperYD::code(me.type()) + " " + me.id() + " " + id;
    if (on) blocked_.insert(key);
    else blocked_.erase(key);
    msg = on ? "已将 " + displayName(me.type(), id) + " 加入黑名单。你将不再收到对方的消息和好友申请，对方也看不到你的" + me.momentsName() + "。"
             : "已将 " + displayName(me.type(), id) + " 移出黑名单。";
    return true;
}

bool PlatformYD::isBlocked(ServiceTypeYD type, const std::string& blocker, const std::string& blocked) const {
    return blocked_.count(ServiceTypeHelperYD::code(type) + " " + blocker + " " + blocked) > 0;
}

std::vector<std::string> PlatformYD::blockedList(const ServiceBaseYD& me) const {
    std::vector<std::string> result;
    const std::string prefix = ServiceTypeHelperYD::code(me.type()) + " " + me.id() + " ";
    for (const std::string& key : blocked_) {
        if (key.compare(0, prefix.size(), prefix) == 0) result.push_back(key.substr(prefix.size()));
    }
    return result;
}

void PlatformYD::setVerification(const ServiceBaseYD& me, bool on) {
    messages_.appendRecord('S', me.type(), { me.id(), "verify", on ? "1" : "0" }, false);
    if (on) noVerify_.erase(ServiceTypeHelperYD::code(me.type()) + " " + me.id());
    else noVerify_.insert(ServiceTypeHelperYD::code(me.type()) + " " + me.id());
}

bool PlatformYD::requiresVerification(const ServiceBaseYD& account) const {
    return !noVerify_.count(ServiceTypeHelperYD::code(account.type()) + " " + account.id());
}

// ======================== 撤回与 @提醒 ========================

bool PlatformYD::canRevoke(const ServiceBaseYD& me, const MessageYD& message) const {
    if (message.revoked() || message.isSystem() || message.service() != me.type()) return false;
    if (message.sender() == me.id()) return secondsSince(message.time()) <= 120;   // 自己的消息：2 分钟内
    if (!message.isGroup()) return false;
    const GroupYD* g = groups_.find(me.type(), toInt(message.target()));
    // 群主、管理员撤回其他成员的消息：权限由群当前的管理策略决定（策略模式）
    return g && g->isMember(me.id()) && g->policy().canRevoke(*g, me.id(), message.sender());
}

bool PlatformYD::revokeMessage(const ServiceBaseYD& me, int seq, std::string& msg) {
    MessageYD message(0, me.type(), false, "", "", "", "");
    if (!messages_.findMessage(me.type(), seq, message)) {
        msg = "消息不存在。";
        return false;
    }
    if (!canRevoke(me, message)) {
        msg = message.revoked()                  ? "这条消息已经撤回了。"
              : message.sender() == me.id()     ? "发送时间超过 2 分钟的消息，不能被撤回。"
                                                : "你没有撤回这条消息的权限。";
        return false;
    }
    if (!messages_.revoke(me.type(), seq, me.id())) {
        msg = "撤回失败，请检查 data 目录。";
        return false;
    }
    msg = "已撤回。";
    return true;
}

std::string PlatformYD::revokeText(const ServiceBaseYD& viewer, const MessageYD& message) const {
    const std::string& by = message.revokedBy();
    auto who = [&](const std::string& id) { return id == viewer.id() ? std::string("你") : "“" + nickOf(viewer.type(), id) + "”"; };
    if (by == message.sender()) return who(by) + "撤回了一条消息";
    return who(by) + "撤回了" + who(message.sender()) + "的一条消息";   // 群主或管理员撤回成员的消息
}

std::vector<std::string> PlatformYD::mentionsOf(const GroupYD& group, const MessageYD& message) const {
    std::vector<std::string> result;
    if (!message.isGroup() || message.isSystem() || message.revoked()) return result;
    const std::string& sender = message.sender();
    // @所有人：只有能修改群资料的人（群主；QQ 群模式下还有管理员）才有效
    const bool everyone = UtilYD::containsText(message.text(), "@所有人") && group.policy().canEditInfo(group, sender);
    for (const std::string& id : group.members()) {
        if (id == sender) continue;
        const std::string nick = nickOf(group.service(), id);
        if (everyone || (!nick.empty() && UtilYD::containsText(message.text(), "@" + nick))) result.push_back(id);
    }
    return result;
}

int PlatformYD::unreadMentions(const ServiceBaseYD& me, int groupNo) const {
    const GroupYD* g = groups_.find(me.type(), groupNo);
    if (!g) return 0;
    const int mark = messages_.groupReadMark(me.type(), groupNo, me.id());
    int count = 0;
    for (const MessageYD& m : messages_.groupHistory(me.type(), groupNo)) {
        if (m.seq() <= mark || m.sender() == me.id()) continue;
        if (UtilYD::contains(mentionsOf(*g, m), me.id())) ++count;
    }
    return count;
}

// ======================== 朋友圈 ========================

bool PlatformYD::canSeeMoment(ServiceTypeYD type, const std::string& viewer, const std::string& author,
                              const std::string& scope) const {
    if (viewer == author) return true;
    // 互为好友、且没有被作者拉黑，才能看到；再按作者设置的可见范围判断
    if (!friendsBothWays(type, viewer, author) || isBlocked(type, author, viewer)) return false;
    if (scope == "self") return false;
    if (scope.compare(0, 5, "only:") == 0) return UtilYD::contains(UtilYD::parseList(scope.substr(5)), viewer);
    if (scope.compare(0, 7, "except:") == 0) return !UtilYD::contains(UtilYD::parseList(scope.substr(7)), viewer);
    return true;
}

bool PlatformYD::postMoment(const ServiceBaseYD& me, const std::string& text, const std::string& scope, std::string& msg) {
    const std::string line = UtilYD::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    std::string range = scope.empty() ? "all" : scope;
    if ((range.compare(0, 5, "only:") == 0 && UtilYD::parseList(range.substr(5)).empty()) ||
        (range.compare(0, 7, "except:") == 0 && UtilYD::parseList(range.substr(7)).empty())) {
        msg = "请至少选择一位好友。";
        return false;
    }
    if (!messages_.appendRecord('P', me.type(), { me.id(), range }, true, line)) {
        msg = "发表失败，请检查 data 目录。";
        return false;
    }
    msg = "已发表到" + me.momentsName() + "（" + MomentYD::scopeText(range) + "）。";
    return true;
}

std::vector<MomentYD> PlatformYD::moments(const ServiceBaseYD& viewer, const std::string& author) {
    syncSocial();
    const ServiceTypeYD type = viewer.type();
    const std::vector<RecordYD> records = messages_.records("PLKYZ");
    std::set<int> deletedPosts, deletedComments;
    for (const RecordYD& r : records) {
        if (r.kind == 'Y') deletedPosts.insert(toInt(r.field(0)));
        if (r.kind == 'Z') deletedComments.insert(toInt(r.field(0)));
    }
    std::map<int, MomentYD> posts;
    for (const RecordYD& r : records) {
        if (r.kind != 'P' || r.service != type || deletedPosts.count(r.seq)) continue;
        if (!author.empty() && r.field(0) != author) continue;
        if (!canSeeMoment(type, viewer.id(), r.field(0), r.field(1))) continue;
        MomentYD m;
        m.seq = r.seq;
        m.service = type;
        m.author = r.field(0);
        m.scope = r.field(1);
        m.time = r.time;
        m.text = r.text;
        posts[r.seq] = m;
    }
    // 只显示查看者能看到的人的点赞和评论：作者本人看全部，其他人只看共同好友的
    auto visible = [&](const MomentYD& m, const std::string& person) {
        return viewer.id() == m.author || person == viewer.id() || friendsBothWays(type, viewer.id(), person);
    };
    for (const RecordYD& r : records) {
        if (r.kind != 'L' && r.kind != 'K') continue;   // 这两种记录的第一项是动态序号
        auto it = posts.find(toInt(r.field(0)));
        if (it == posts.end()) continue;
        MomentYD& m = it->second;
        if (r.kind == 'L') {   // 按先后顺序处理点赞/取消点赞
            UtilYD::removeValue(m.likes, r.field(1));
            if (r.field(2) == "1") m.likes.push_back(r.field(1));
        } else if (r.kind == 'K' && !deletedComments.count(r.seq) && visible(m, r.field(1))) {
            MomentCommentYD c;
            c.seq = r.seq;
            c.author = r.field(1);
            c.replyTo = r.field(2) == "-" ? "" : r.field(2);
            c.time = r.time;
            c.text = r.text;
            m.comments.push_back(c);
        }
    }
    std::vector<MomentYD> result;
    for (auto it = posts.rbegin(); it != posts.rend(); ++it) {
        MomentYD m = it->second;
        std::vector<std::string> likes;
        for (const std::string& id : m.likes) {
            if (visible(m, id)) likes.push_back(id);
        }
        m.likes = likes;
        result.push_back(m);
    }
    return result;
}

bool PlatformYD::toggleLike(const ServiceBaseYD& me, int seq, std::string& msg) {
    for (const MomentYD& m : moments(me)) {
        if (m.seq != seq) continue;
        const bool liked = m.likedBy(me.id());
        messages_.appendRecord('L', me.type(), { std::to_string(seq), me.id(), liked ? "0" : "1" }, false);
        msg = liked ? "已取消赞。" : "已赞。";
        return true;
    }
    msg = "这条动态不存在，或你没有权限查看。";
    return false;
}

bool PlatformYD::commentMoment(const ServiceBaseYD& me, int seq, const std::string& replyTo, const std::string& text,
                               std::string& msg) {
    const std::string line = UtilYD::lineText(text);
    if (!messageTextOk(line, msg)) return false;
    for (const MomentYD& m : moments(me)) {
        if (m.seq != seq) continue;
        messages_.appendRecord('K', me.type(), { std::to_string(seq), me.id(), replyTo.empty() ? "-" : replyTo }, true, line);
        msg = replyTo.empty() || replyTo == "-" ? "评论成功。" : "已回复 " + nickOf(me.type(), replyTo) + "。";
        return true;
    }
    msg = "这条动态不存在，或你没有权限查看。";
    return false;
}

bool PlatformYD::deleteMoment(const ServiceBaseYD& me, int seq, std::string& msg) {
    for (const RecordYD& r : messages_.records("P")) {
        if (r.seq != seq || r.service != me.type()) continue;
        if (r.field(0) != me.id()) {
            msg = "只能删除自己的动态。";
            return false;
        }
        messages_.appendRecord('Y', me.type(), { std::to_string(seq) }, false);
        msg = "动态已删除。";
        return true;
    }
    msg = "这条动态不存在。";
    return false;
}

bool PlatformYD::deleteComment(const ServiceBaseYD& me, int commentSeq, std::string& msg) {
    const std::vector<RecordYD> records = messages_.records("PK");
    for (const RecordYD& c : records) {
        if (c.kind != 'K' || c.seq != commentSeq || c.service != me.type()) continue;
        std::string postAuthor;
        for (const RecordYD& p : records) {
            if (p.kind == 'P' && p.seq == toInt(c.field(0))) postAuthor = p.field(0);
        }
        if (c.field(1) != me.id() && postAuthor != me.id()) {   // 评论者本人或动态作者可以删除
            msg = "只能删除自己的评论，或自己动态下的评论。";
            return false;
        }
        messages_.appendRecord('Z', me.type(), { std::to_string(commentSeq) }, false);
        msg = "评论已删除。";
        return true;
    }
    msg = "这条评论不存在。";
    return false;
}

std::vector<std::string> PlatformYD::momentNotices(const ServiceBaseYD& me, bool unreadOnly) {
    const std::vector<RecordYD> records = messages_.records("PLKYZ");
    std::map<int, const RecordYD*> posts;
    std::set<int> deletedPosts, deletedComments;
    for (const RecordYD& r : records) {
        if (r.kind == 'P' && r.service == me.type()) posts[r.seq] = &r;
        if (r.kind == 'Y') deletedPosts.insert(toInt(r.field(0)));
        if (r.kind == 'Z') deletedComments.insert(toInt(r.field(0)));
    }
    const int mark = unreadOnly ? messages_.readMark(me.type(), "O", "-", me.id()) : 0;
    std::vector<std::string> result;
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const RecordYD& r = *it;
        if ((r.kind != 'L' && r.kind != 'K') || r.seq <= mark || r.field(1) == me.id()) continue;
        auto post = posts.find(toInt(r.field(0)));
        if (post == posts.end() || deletedPosts.count(post->first)) continue;
        const bool myPost = post->second->field(0) == me.id();
        const std::string who = nickOf(me.type(), r.field(1));
        const std::string what = "“" + UtilYD::truncate(post->second->text, 12) + "”";
        if (r.kind == 'L' && myPost && r.field(2) == "1") result.push_back(who + " 赞了你的动态 " + what);
        if (r.kind == 'K' && !deletedComments.count(r.seq)) {
            if (r.field(2) == me.id()) result.push_back(who + " 回复了你：" + r.text);
            else if (myPost) result.push_back(who + " 评论了你的动态 " + what + "：" + r.text);
        }
    }
    return result;
}

void PlatformYD::markMomentsRead(const ServiceBaseYD& me) {
    int last = 0;
    for (const RecordYD& r : messages_.records("LK")) {
        if (r.service == me.type()) last = (std::max)(last, r.seq);
    }
    messages_.markRead(me.type(), "O", "-", me.id(), last);
}

// ======================== 群事件 → 系统提示 ========================

void PlatformYD::onGroupEvent(const GroupYD& group, GroupEventYD event, const std::string& actor, const std::string& target) {
    const ServiceTypeYD type = group.service();
    const std::string a = nickOf(type, actor), b = nickOf(type, target);
    std::string text;
    switch (event) {
    case GroupEventYD::INVITED: text = a + " 邀请 " + b + " 加入了群聊"; break;
    case GroupEventYD::APPROVED: text = b + " 经 " + a + " 同意加入了群聊"; break;
    case GroupEventYD::CLAIMED: text = a + " 加入了群聊并成为新群主（原群主已注销）"; break;
    case GroupEventYD::QUIT: text = a + " 退出了群聊"; break;
    case GroupEventYD::KICKED: text = a + " 将 " + b + " 移出了群聊"; break;
    case GroupEventYD::RENAMED: text = a + " 修改群名为“" + target + "”"; break;
    case GroupEventYD::TRANSFERRED: text = a + " 已将群主转让给 " + b; break;
    case GroupEventYD::MODE_SWITCHED: text = "群主 " + a + " 将本群切换为" + target; break;
    case GroupEventYD::MUTED: text = a + " 将 " + b + " 禁言"; break;
    case GroupEventYD::UNMUTED: text = a + " 解除了 " + b + " 的禁言"; break;
    case GroupEventYD::ADMIN_SET: text = a + " 将 " + b + " 设为管理员"; break;
    case GroupEventYD::ADMIN_UNSET: text = a + " 取消了 " + b + " 的管理员身份"; break;
    }
    messages_.postGroup(type, group.groupNo(), MessageYD::systemSender(), text);
}
