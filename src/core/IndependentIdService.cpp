#include "IndependentIdService.h"
#include <iostream>

bool IndependentIdService::usesQQNumber() const { return false; }

std::string IndependentIdService::linkedQQ() const { return boundQQ_; }

void IndependentIdService::showInfo() const {
    ServiceBase::showInfo();
    std::cout << "  绑定QQ：" << (isBound() ? boundQQ_ : "未绑定（绑定后才能与 QQ/微博 互认好友）") << "\n";
}

std::string IndependentIdService::extraData() const { return isBound() ? boundQQ_ : "-"; }

void IndependentIdService::loadExtraData(const std::string& data) {
    boundQQ_ = (data == "-") ? "" : data;
}

void IndependentIdService::bindQQ(const std::string& qqId) { boundQQ_ = qqId; }

void IndependentIdService::unbindQQ() { boundQQ_.clear(); }

bool IndependentIdService::isBound() const { return !boundQQ_.empty(); }

const std::string& IndependentIdService::boundQQ() const { return boundQQ_; }
