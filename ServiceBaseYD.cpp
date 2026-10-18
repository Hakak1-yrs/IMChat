#include "ServiceBaseYD.h"
#include <iostream>
#include "PasswordYD.h"

ServiceBaseYD::ServiceBaseYD(const std::string& id, int ownerId, const std::string& nickname,
                             const DateYD& birthday, const DateYD& registerDate,
                             const std::string& location, const std::string& password)
    : id_(id), ownerId_(ownerId), nickname_(nickname), birthday_(birthday),
      registerDate_(registerDate), location_(location), loggedIn_(false) {
    // 传入的既可能是注册时输入的明文，也可能是文件中保存的加密记录；
    // 旧版本数据文件中的明文密码在这里自动升级为加密记录，下次存盘时写回文件
    password_ = PasswordYD::isRecord(password) ? password : PasswordYD::makeRecord(password);
}

void ServiceBaseYD::showInfo() const {
    std::cout << "  服务类型：" << serviceName() << "\n"
              << "  " << idLabel() << "：" << id_ << "\n"
              << "  昵称：" << nickname_ << "\n"
              << "  出生日期：" << birthday_.toString() << "\n"
              << "  号码申请时间：" << registerDate_.toString() << "（T龄 " << tAge() << " 年）\n"
              << "  所在地：" << location_ << "\n"
              << "  好友数：" << friends_.size() << "\n";
}

std::string ServiceBaseYD::extraData() const { return "-"; }

void ServiceBaseYD::loadExtraData(const std::string&) {}

std::string ServiceBaseYD::linkedQQ() const { return usesQQNumber() ? id_ : std::string(); }

std::string ServiceBaseYD::serviceName() const { return ServiceTypeHelperYD::name(type()); }

std::string ServiceBaseYD::displayName() const { return nickname_ + "(" + id_ + ")"; }

const std::string& ServiceBaseYD::id() const { return id_; }

int ServiceBaseYD::ownerId() const { return ownerId_; }

const std::string& ServiceBaseYD::nickname() const { return nickname_; }

void ServiceBaseYD::setNickname(const std::string& nickname) { nickname_ = nickname; }

const DateYD& ServiceBaseYD::birthday() const { return birthday_; }

void ServiceBaseYD::setBirthday(const DateYD& birthday) { birthday_ = birthday; }

const DateYD& ServiceBaseYD::registerDate() const { return registerDate_; }

int ServiceBaseYD::tAge() const { return registerDate_.yearsUntil(DateYD::today()); }

const std::string& ServiceBaseYD::location() const { return location_; }

void ServiceBaseYD::setLocation(const std::string& location) { location_ = location; }

const std::string& ServiceBaseYD::password() const { return password_; }

bool ServiceBaseYD::checkPassword(const std::string& password) const { return PasswordYD::verify(password, password_); }

void ServiceBaseYD::setPassword(const std::string& plain) { password_ = PasswordYD::makeRecord(plain); }

bool ServiceBaseYD::isLoggedIn() const { return loggedIn_; }

void ServiceBaseYD::login() { loggedIn_ = true; }

void ServiceBaseYD::logout() { loggedIn_ = false; }

FriendManagerYD& ServiceBaseYD::friends() { return friends_; }

const FriendManagerYD& ServiceBaseYD::friends() const { return friends_; }
