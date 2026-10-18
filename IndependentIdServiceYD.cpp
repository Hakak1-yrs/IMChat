#include "IndependentIdServiceYD.h"
#include <iostream>

bool IndependentIdServiceYD::usesQQNumber() const { return false; }

std::string IndependentIdServiceYD::linkedQQ() const { return boundQQ_; }

void IndependentIdServiceYD::showInfo() const {
    ServiceBaseYD::showInfo();
    std::cout << "  绑定QQ：" << (isBound() ? boundQQ_ : "未绑定（绑定后才能与 QQ/微博 互认好友）") << "\n";
}

std::string IndependentIdServiceYD::extraData() const { return isBound() ? boundQQ_ : "-"; }

void IndependentIdServiceYD::loadExtraData(const std::string& data) {
    boundQQ_ = (data == "-") ? "" : data;
}

void IndependentIdServiceYD::bindQQ(const std::string& qqId) { boundQQ_ = qqId; }

void IndependentIdServiceYD::unbindQQ() { boundQQ_.clear(); }

bool IndependentIdServiceYD::isBound() const { return !boundQQ_.empty(); }

const std::string& IndependentIdServiceYD::boundQQ() const { return boundQQ_; }
