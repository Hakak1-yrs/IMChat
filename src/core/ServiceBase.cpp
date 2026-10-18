#include "ServiceBase.h"
#include <iostream>
#include "Password.h"

ServiceBase::ServiceBase(const std::string& id, int ownerId, const std::string& nickname,
                             const Date& birthday, const Date& registerDate,
                             const std::string& location, const std::string& password)
    : id_(id), ownerId_(ownerId), nickname_(nickname), birthday_(birthday),
      registerDate_(registerDate), location_(location), loggedIn_(false) {
    // 传入的既可能是注册时输入的明文，也可能是文件中保存的加密记录；
    // 旧版本数据文件中的明文密码在这里自动升级为加密记录，下次存盘时写回文件
    password_ = Password::isRecord(password) ? password : Password::makeRecord(password);
}

void ServiceBase::showInfo() const {
    std::cout << "  服务类型：" << serviceName() << "\n"
              << "  " << idLabel() << "：" << id_ << "\n"
              << "  昵称：" << nickname_ << "\n"
              << "  出生日期：" << birthday_.toString() << "\n"
              << "  号码申请时间：" << registerDate_.toString() << "（T龄 " << tAge() << " 年）\n"
              << "  所在地：" << location_ << "\n"
              << "  好友数：" << friends_.size() << "\n";
}

std::string ServiceBase::extraData() const { return "-"; }

void ServiceBase::loadExtraData(const std::string&) {}

std::string ServiceBase::linkedQQ() const { return usesQQNumber() ? id_ : std::string(); }

std::string ServiceBase::serviceName() const { return ServiceTypeHelper::name(type()); }

std::string ServiceBase::displayName() const { return nickname_ + "(" + id_ + ")"; }

const std::string& ServiceBase::id() const { return id_; }

int ServiceBase::ownerId() const { return ownerId_; }

const std::string& ServiceBase::nickname() const { return nickname_; }

void ServiceBase::setNickname(const std::string& nickname) { nickname_ = nickname; }

const Date& ServiceBase::birthday() const { return birthday_; }

void ServiceBase::setBirthday(const Date& birthday) { birthday_ = birthday; }

const Date& ServiceBase::registerDate() const { return registerDate_; }

int ServiceBase::tAge() const { return registerDate_.yearsUntil(Date::today()); }

const std::string& ServiceBase::location() const { return location_; }

void ServiceBase::setLocation(const std::string& location) { location_ = location; }

const std::string& ServiceBase::password() const { return password_; }

bool ServiceBase::checkPassword(const std::string& password) const { return Password::verify(password, password_); }

void ServiceBase::setPassword(const std::string& plain) { password_ = Password::makeRecord(plain); }

bool ServiceBase::isLoggedIn() const { return loggedIn_; }

void ServiceBase::login() { loggedIn_ = true; }

void ServiceBase::logout() { loggedIn_ = false; }

FriendManager& ServiceBase::friends() { return friends_; }

const FriendManager& ServiceBase::friends() const { return friends_; }
