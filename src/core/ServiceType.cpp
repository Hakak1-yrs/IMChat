#include "ServiceType.h"

std::string ServiceTypeHelper::code(ServiceType type) {
    switch (type) {
    case ServiceType::QQ:     return "QQ";
    case ServiceType::WECHAT: return "WECHAT";
    case ServiceType::WEIBO:  return "WEIBO";
    case ServiceType::WEISHANG: return "WEISHANG";
    }
    return "UNKNOWN";
}

std::string ServiceTypeHelper::name(ServiceType type) {
    switch (type) {
    case ServiceType::QQ:     return "QQ";
    case ServiceType::WECHAT: return "微信";
    case ServiceType::WEIBO:  return "微博";
    case ServiceType::WEISHANG: return "微商";
    }
    return "未知服务";
}

bool ServiceTypeHelper::parse(const std::string& code, ServiceType& type) {
    for (ServiceType t : all()) {
        if (ServiceTypeHelper::code(t) == code) {
            type = t;
            return true;
        }
    }
    return false;
}

std::vector<ServiceType> ServiceTypeHelper::all() {
    return { ServiceType::QQ, ServiceType::WECHAT, ServiceType::WEIBO, ServiceType::WEISHANG };
}
