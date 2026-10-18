#include "GroupManagerYD.h"
#include <algorithm>
#include <sstream>
#include "UtilYD.h"

GroupYD* GroupManagerYD::find(ServiceTypeYD service, int groupNo) const {
    for (const auto& g : groups_) {
        if (g->service() == service && g->groupNo() == groupNo) return g.get();
    }
    return nullptr;
}

std::vector<GroupYD*> GroupManagerYD::groupsOfService(ServiceTypeYD service) const {
    std::vector<GroupYD*> result;
    for (const auto& g : groups_) {
        if (g->service() == service) result.push_back(g.get());
    }
    return result;
}

std::vector<GroupYD*> GroupManagerYD::groupsOf(ServiceTypeYD service, const std::string& accountId) const {
    std::vector<GroupYD*> result;
    for (const auto& g : groups_) {
        if (g->service() == service && g->isMember(accountId)) result.push_back(g.get());
    }
    return result;
}

size_t GroupManagerYD::count() const { return groups_.size(); }

void GroupManagerYD::createPresetGroups(ServiceTypeYD service, const std::string& ownerId, GroupModeYD mode) {
    if (!groupsOfService(service).empty()) return;
    const char* topics[] = { "交流群", "学习群", "兴趣群", "活动群", "通知群", "闲聊群" };
    for (int i = 0; i < 6; ++i) {
        std::string name = ServiceTypeHelperYD::name(service) + topics[i];
        groups_.push_back(std::make_unique<GroupYD>(service, 1001 + i, name, ownerId, mode));
        groups_.back()->setListener(listener_);
    }
}

void GroupManagerYD::setListener(GroupListenerYD* listener) {
    listener_ = listener;
    for (const auto& g : groups_) g->setListener(listener);
}

bool GroupManagerYD::isPreset(int groupNo) { return groupNo >= 1001 && groupNo <= 1006; }

GroupYD* GroupManagerYD::createGroup(ServiceTypeYD service, const std::string& name, const std::string& ownerId,
                                     GroupModeYD mode) {
    int maxNo = 1006;
    for (GroupYD* g : groupsOfService(service)) maxNo = std::max(maxNo, g->groupNo());
    groups_.push_back(std::make_unique<GroupYD>(service, maxNo + 1, name, ownerId, mode));
    groups_.back()->setListener(listener_);
    return groups_.back().get();
}

bool GroupManagerYD::remove(ServiceTypeYD service, int groupNo) {
    auto it = std::find_if(groups_.begin(), groups_.end(), [&](const std::unique_ptr<GroupYD>& g) {
        return g->service() == service && g->groupNo() == groupNo;
    });
    if (it == groups_.end()) return false;
    groups_.erase(it);
    return true;
}

void GroupManagerYD::removeAccount(ServiceTypeYD service, const std::string& accountId) {
    for (const auto& g : groups_) {
        if (g->service() == service) g->removeAccount(accountId);
    }
}

// groups.txt 每行：服务 群号 群名 群主 管理模式 管理员列表 成员列表 待审批列表 禁言列表 群公告
//   （禁言列表和群公告是后来增加的两列，旧文件没有这两列也能正常读取；群公告是行尾的整段文字，可以含空格）
// subgroups.txt 每行：服务 群号 讨论组名 成员列表
int GroupManagerYD::load(const std::string& groupText, const std::string& subGroupText, int& repaired) {
    int badLines = 0;
    std::istringstream in(groupText);
    std::string line;
    while (std::getline(in, line)) {
        line = UtilYD::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string serviceCode, name, owner, modeCode, admins, members, pending;
        int groupNo = 0;
        ServiceTypeYD service;
        GroupModeYD mode;
        if (!(iss >> serviceCode >> groupNo >> name >> owner >> modeCode >> admins >> members >> pending) ||
            !ServiceTypeHelperYD::parse(serviceCode, service) || !GroupPolicyYD::parseMode(modeCode, mode) ||
            find(service, groupNo)) {
            ++badLines;
            continue;
        }
        std::string muted, notice;
        if (!(iss >> muted)) muted = "-";
        std::getline(iss, notice);
        notice = UtilYD::trim(notice);
        auto group = std::make_unique<GroupYD>(service, groupNo, name, owner, mode);
        repaired += group->loadData(UtilYD::parseList(members), UtilYD::parseList(admins), UtilYD::parseList(pending),
                                    UtilYD::parseList(muted));
        group->loadNotice(notice == "-" ? "" : notice);
        groups_.push_back(std::move(group));
    }

    std::istringstream subIn(subGroupText);
    while (std::getline(subIn, line)) {
        line = UtilYD::trim(line);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string serviceCode, name, members;
        int groupNo = 0;
        ServiceTypeYD service;
        GroupYD* group = nullptr;
        if (!(iss >> serviceCode >> groupNo >> name >> members) || !ServiceTypeHelperYD::parse(serviceCode, service) ||
            !(group = find(service, groupNo))) {
            ++badLines;
            continue;
        }
        SubGroupYD sub(name);
        for (const std::string& m : UtilYD::parseList(members)) {
            if (group->isMember(m)) sub.addMember(m);   // 讨论组成员必须是群成员
            else ++repaired;
        }
        if (!group->loadSubGroup(sub)) ++badLines;      // 空讨论组或重名讨论组
    }
    return badLines;
}

std::string GroupManagerYD::groupText() const {
    std::ostringstream out;
    out << "# 服务 群号 群名 群主 管理模式 管理员列表 成员列表 待审批申请 禁言列表 群公告"
           "（列表用逗号分隔，空列表、无群主、无公告写作 -）\n";
    for (const auto& g : groups_) {
        const std::string notice = UtilYD::lineText(g->notice());
        out << ServiceTypeHelperYD::code(g->service()) << ' ' << g->groupNo() << ' ' << UtilYD::fieldText(g->name()) << ' '
            << (g->hasOwner() ? g->ownerId() : "-") << ' ' << GroupPolicyYD::modeCode(g->policy().mode()) << ' '
            << UtilYD::joinList(g->admins()) << ' ' << UtilYD::joinList(g->members()) << ' '
            << UtilYD::joinList(g->pending()) << ' ' << UtilYD::joinList(g->muted()) << ' '
            << (notice.empty() ? "-" : notice) << '\n';
    }
    return out.str();
}

std::string GroupManagerYD::subGroupText() const {
    std::ostringstream out;
    out << "# 服务 群号 讨论组名 成员列表\n";
    for (const auto& g : groups_) {
        for (const SubGroupYD& s : g->subGroups()) {
            out << ServiceTypeHelperYD::code(g->service()) << ' ' << g->groupNo() << ' ' << UtilYD::fieldText(s.name())
                << ' ' << UtilYD::joinList(s.members()) << '\n';
        }
    }
    return out.str();
}
