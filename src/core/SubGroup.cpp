#include "SubGroup.h"
#include "Util.h"

SubGroup::SubGroup(const std::string& name) : name_(name) {}

const std::string& SubGroup::name() const { return name_; }

const std::vector<std::string>& SubGroup::members() const { return members_; }

const std::string& SubGroup::creator() const {
    static const std::string none;
    return members_.empty() ? none : members_.front();
}

bool SubGroup::addMember(const std::string& id) {
    if (Util::contains(members_, id)) return false;
    members_.push_back(id);
    return true;
}

bool SubGroup::removeMember(const std::string& id) { return Util::removeValue(members_, id); }
