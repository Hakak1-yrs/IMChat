#include "ServiceTypeYD.h"

std::string ServiceTypeHelperYD::code(ServiceTypeYD type) {
    switch (type) {
    case ServiceTypeYD::QQ:     return "QQ";
    case ServiceTypeYD::WECHAT: return "WECHAT";
    case ServiceTypeYD::WEIBO:  return "WEIBO";
    case ServiceTypeYD::WEISHANG: return "WEISHANG";
    }
    return "UNKNOWN";
}

std::string ServiceTypeHelperYD::name(ServiceTypeYD type) {
    switch (type) {
    case ServiceTypeYD::QQ:     return "QQ";
    case ServiceTypeYD::WECHAT: return "微信";
    case ServiceTypeYD::WEIBO:  return "微博";
    case ServiceTypeYD::WEISHANG: return "微商";
    }
    return "未知服务";
}

bool ServiceTypeHelperYD::parse(const std::string& code, ServiceTypeYD& type) {
    for (ServiceTypeYD t : all()) {
        if (ServiceTypeHelperYD::code(t) == code) {
            type = t;
            return true;
        }
    }
    return false;
}

std::vector<ServiceTypeYD> ServiceTypeHelperYD::all() {
    return { ServiceTypeYD::QQ, ServiceTypeYD::WECHAT, ServiceTypeYD::WEIBO, ServiceTypeYD::WEISHANG };
}
