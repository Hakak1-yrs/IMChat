#include "RemoteAccounts.h"

RemoteAccounts::RemoteAccounts(std::string host, std::uint16_t port) : host_(std::move(host)), port_(port) {}

void RemoteAccounts::start(ServiceType type, const std::string& account, const std::string& password) {
    current_ = type;
    auto session = std::make_unique<RemoteSession>(host_, port_, this);
    session->start(type, account, password);
    sessions_[type] = std::move(session);
}

RemoteSession& RemoteAccounts::current() { return *sessions_.at(current_); }

RemoteSession* RemoteAccounts::find(ServiceType type) {
    auto it = sessions_.find(type);
    return it == sessions_.end() ? nullptr : it->second.get();
}

std::vector<ServiceType> RemoteAccounts::services() const {
    std::vector<ServiceType> result;
    for (const auto& [type, session] : sessions_) {   // std::map 按枚举值排序：QQ、微信、微博、微商
        if (type == current_ || session->loggedIn()) result.push_back(type);
    }
    return result;
}

bool RemoteAccounts::switchTo(ServiceType type) {
    RemoteSession* target = find(type);
    if (!target || !target->loggedIn()) return false;
    current_ = type;
    return true;
}

void RemoteAccounts::onLogin(const nlohmann::json& response) {
    const std::string token = response.value("token", "");
    if (token.empty()) return;
    for (const nlohmann::json& item : response.value("services", nlohmann::json::array())) {
        ServiceType type = ServiceType::QQ;
        if (!ServiceTypeHelper::parse(item.value("service", ""), type) || sessions_.count(type)) continue;   // 重连后的登录响应也会列出它们
        auto other = std::make_unique<RemoteSession>(host_, port_, this);
        other->startWithToken(type, item.value("account", ""), token);
        sessions_[type] = std::move(other);
    }
}

bool RemoteAccounts::pollOthers(const RemoteSession& except) {
    bool changed = false;
    for (auto& entry : sessions_) {
        if (entry.second.get() != &except && entry.second->pollChanges()) changed = true;
    }
    return changed;
}
