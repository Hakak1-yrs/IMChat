#include "SubGroupYD.h"
#include "UtilYD.h"

SubGroupYD::SubGroupYD(const std::string& name) : name_(name) {}

const std::string& SubGroupYD::name() const { return name_; }

const std::vector<std::string>& SubGroupYD::members() const { return members_; }

const std::string& SubGroupYD::creator() const {
    static const std::string none;
    return members_.empty() ? none : members_.front();
}

bool SubGroupYD::addMember(const std::string& id) {
    if (UtilYD::contains(members_, id)) return false;
    members_.push_back(id);
    return true;
}

bool SubGroupYD::removeMember(const std::string& id) { return UtilYD::removeValue(members_, id); }
