#include "GroupManager.h"
#include <algorithm>
#include <sstream>
#include "Util.h"

Group* GroupManager::find(ServiceType service, int groupNo) const {
    for (const auto& g : groups_) {
        if (g->service() == service && g->groupNo() == groupNo) return g.get();
    }
    return nullptr;
}

std::vector<Group*> GroupManager::groupsOfService(ServiceType service) const {
    std::vector<Group*> result;
    for (const auto& g : groups_) {
        if (g->service() == service) result.push_back(g.get());
    }
    return result;
}

std::vector<Group*> GroupManager::groupsOf(ServiceType service, const std::string& accountId) const {
    std::vector<Group*> result;
    for (const auto& g : groups_) {
        if (g->service() == service && g->isMember(accountId)) result.push_back(g.get());
    }
    return result;
}

size_t GroupManager::count() const { return groups_.size(); }

void GroupManager::createPresetGroups(ServiceType service, const std::string& ownerId, GroupMode mode) {
    if (!groupsOfService(service).empty()) return;
    const char* topics[] = { "交流群", "学习群", "兴趣群", "活动群", "通知群", "闲聊群" };
    for (int i = 0; i < 6; ++i) {
        std::string name = ServiceTypeHelper::name(service) + topics[i];
        groups_.push_back(std::make_unique<Group>(service, 1001 + i, name, ownerId, mode));
        groups_.back()->setListener(listener_);
    }
}

void GroupManager::setListener(GroupListener* listener) {
    listener_ = listener;
    for (const auto& g : groups_) g->setListener(listener);
}

bool GroupManager::isPreset(int groupNo) { return groupNo >= 1001 && groupNo <= 1006; }

Group* GroupManager::createGroup(ServiceType service, const std::string& name, const std::string& ownerId,
                                     GroupMode mode) {
    int maxNo = 1006;
    for (Group* g : groupsOfService(service)) maxNo = std::max(maxNo, g->groupNo());
    groups_.push_back(std::make_unique<Group>(service, maxNo + 1, name, ownerId, mode));
    groups_.back()->setListener(listener_);
    return groups_.back().get();
}

bool GroupManager::remove(ServiceType service, int groupNo) {
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const std::unique_ptr<Group>& g) {
        return g->service() == service && g->groupNo() == groupNo;
    });
    if (it == groups_.end()) return false;
    groups_.erase(it);
    return true;
}

void GroupManager::removeAccount(ServiceType service, const std::string& accountId) {
    for (const auto& g : groups_) {
        if (g->service() == service) g->removeAccount(accountId);
    }
}

// groups.txt 每行：服务 群号 群名 群主 管理模式 管理员列表 成员列表 待审批列表 禁言列表 群公告
//   （禁言列表和群公告是后来增加的两列，旧文件没有这两列也能正常读取；群公告是行尾的整段文字，可以含空格）
// subgroups.txt 每行：服务 群号 讨论组名 成员列表
int GroupManager::load(const std::string& groupText, const std::string& subGroupText, int& repaired) {
    int badLines = 0;
    std::istringstream in(groupText);
    std::string line;
    while (std::getline(in, line)) {
        line = Util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string serviceCode, name, owner, modeCode, admins, members, pending;
        int groupNo = 0;
        ServiceType service;
        GroupMode mode;
        if (!(iss >> serviceCode >> groupNo >> name >> owner >> modeCode >> admins >> members >> pending) ||
            !ServiceTypeHelper::parse(serviceCode, service) || !GroupPolicy::parseMode(modeCode, mode) ||
            find(service, groupNo)) {
            ++badLines;
            continue;
        }
        std::string muted, notice;
        if (!(iss >> muted)) muted = "-";
        std::getline(iss, notice);
        notice = Util::trim(notice);
        auto group = std::make_unique<Group>(service, groupNo, name, owner, mode);
        repaired += group->loadData(Util::parseList(members), Util::parseList(admins), Util::parseList(pending),
                                    Util::parseList(muted));
        group->loadNotice(notice == "-" ? "" : notice);
        groups_.push_back(std::move(group));
    }

    std::istringstream subIn(subGroupText);
    while (std::getline(subIn, line)) {
        line = Util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string serviceCode, name, members;
        int groupNo = 0;
        ServiceType service;
        Group* group = nullptr;
        if (!(iss >> serviceCode >> groupNo >> name >> members) || !ServiceTypeHelper::parse(serviceCode, service) ||
            !(group = find(service, groupNo))) {
            ++badLines;
            continue;
        }
        SubGroup sub(name);
        for (const std::string& m : Util::parseList(members)) {
            if (group->isMember(m)) sub.addMember(m);   // 讨论组成员必须是群成员
            else ++repaired;
        }
        if (!group->loadSubGroup(sub)) ++badLines;      // 空讨论组或重名讨论组
    }
    return badLines;
}

std::string GroupManager::groupText() const {
    std::ostringstream out;
    out << "# 服务 群号 群名 群主 管理模式 管理员列表 成员列表 待审批申请 禁言列表 群公告"
           "（列表用逗号分隔，空列表、无群主、无公告写作 -）\n";
    for (const auto& g : groups_) {
        const std::string notice = Util::lineText(g->notice());
        out << ServiceTypeHelper::code(g->service()) << ' ' << g->groupNo() << ' ' << Util::fieldText(g->name()) << ' '
            << (g->hasOwner() ? g->ownerId() : "-") << ' ' << GroupPolicy::modeCode(g->policy().mode()) << ' '
            << Util::joinList(g->admins()) << ' ' << Util::joinList(g->members()) << ' '
            << Util::joinList(g->pending()) << ' ' << Util::joinList(g->muted()) << ' '
            << (notice.empty() ? "-" : notice) << '\n';
    }
    return out.str();
}

std::string GroupManager::subGroupText() const {
    std::ostringstream out;
    out << "# 服务 群号 讨论组名 成员列表\n";
    for (const auto& g : groups_) {
        for (const SubGroup& s : g->subGroups()) {
            out << ServiceTypeHelper::code(g->service()) << ' ' << g->groupNo() << ' ' << Util::fieldText(s.name())
                << ' ' << Util::joinList(s.members()) << '\n';
        }
    }
    return out.str();
}
