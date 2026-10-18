#pragma once
#include <string>
#include <vector>

// 平台上的微X服务种类。新增服务时在此添加枚举值（微商即按此方式扩展而来）
enum class ServiceTypeYD { QQ, WECHAT, WEIBO, WEISHANG };

// 服务类型辅助类：文件中使用英文代码存储，界面上显示中文名称
class ServiceTypeHelperYD {
public:
    static std::string code(ServiceTypeYD type);     // QQ / WECHAT / WEIBO / WEISHANG
    static std::string name(ServiceTypeYD type);     // QQ / 微信 / 微博 / 微商
    static bool parse(const std::string& code, ServiceTypeYD& type);
    static std::vector<ServiceTypeYD> all();
};
