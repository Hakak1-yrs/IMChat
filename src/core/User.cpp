#include "User.h"
#include "ServiceBase.h"

User::User(int id, const std::string& name) : id_(id), name_(name) {}

int User::id() const { return id_; }

const std::string& User::name() const { return name_; }

void User::attach(ServiceBase* account) { services_[account->type()] = account; }

void User::detach(ServiceType type) { services_.erase(type); }

ServiceBase* User::service(ServiceType type) const {
    auto it = services_.find(type);
    return it == services_.end() ? nullptr : it->second;
}

bool User::hasService(ServiceType type) const { return service(type) != nullptr; }

std::vector<ServiceBase*> User::services() const {
    std::vector<ServiceBase*> result;
    for (const auto& kv : services_) result.push_back(kv.second);
    return result;
}

bool User::isOnline() const {
    for (const auto& kv : services_) {
        if (kv.second->isLoggedIn()) return true;
    }
    return false;
}

void User::logoutAll() {
    for (auto& kv : services_) kv.second->logout();
}
