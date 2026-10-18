#include "UserYD.h"
#include "ServiceBaseYD.h"

UserYD::UserYD(int id, const std::string& name) : id_(id), name_(name) {}

int UserYD::id() const { return id_; }

const std::string& UserYD::name() const { return name_; }

void UserYD::attach(ServiceBaseYD* account) { services_[account->type()] = account; }

void UserYD::detach(ServiceTypeYD type) { services_.erase(type); }

ServiceBaseYD* UserYD::service(ServiceTypeYD type) const {
    auto it = services_.find(type);
    return it == services_.end() ? nullptr : it->second;
}

bool UserYD::hasService(ServiceTypeYD type) const { return service(type) != nullptr; }

std::vector<ServiceBaseYD*> UserYD::services() const {
    std::vector<ServiceBaseYD*> result;
    for (const auto& kv : services_) result.push_back(kv.second);
    return result;
}

bool UserYD::isOnline() const {
    for (const auto& kv : services_) {
        if (kv.second->isLoggedIn()) return true;
    }
    return false;
}

void UserYD::logoutAll() {
    for (auto& kv : services_) kv.second->logout();
}
