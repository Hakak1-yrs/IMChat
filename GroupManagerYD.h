#pragma once
#include <memory>
#include <string>
#include <vector>
#include "GroupYD.h"

// 群容器类：管理平台上所有服务的群，负责群数据与文本之间的相互转换
class GroupManagerYD {
public:
    GroupYD* find(ServiceTypeYD service, int groupNo) const;
    std::vector<GroupYD*> groupsOfService(ServiceTypeYD service) const;
    std::vector<GroupYD*> groupsOf(ServiceTypeYD service, const std::string& accountId) const;  // 某账号加入的群
    size_t count() const;

    // 某服务还没有任何群时（例如新增的微商服务第一次有人开通），
    // 按题目约定创建 1001～1006 号群，由该账号担任群主
    void createPresetGroups(ServiceTypeYD service, const std::string& ownerId, GroupModeYD mode);
    static bool isPreset(int groupNo);   // 1001～1006 是题目约定的预置群

    // 用户新建群：群号取本服务现有最大群号 + 1（至少 1007），创建者为群主
    GroupYD* createGroup(ServiceTypeYD service, const std::string& name, const std::string& ownerId, GroupModeYD mode);
    // 删除一个群（权限检查由调用者完成）
    bool remove(ServiceTypeYD service, int groupNo);

    // 账号被关闭时，从该服务的所有群中移除其数据
    void removeAccount(ServiceTypeYD service, const std::string& accountId);

    // 从文本解析群和讨论组；返回格式错误或重复而被跳过的行数，自动修正的数据处数累加到 repaired
    int load(const std::string& groupText, const std::string& subGroupText, int& repaired);
    std::string groupText() const;
    std::string subGroupText() const;

    // 设置群事件的监听者：已有的群和以后新建的群都会通知它
    void setListener(GroupListenerYD* listener);

private:
    std::vector<std::unique_ptr<GroupYD>> groups_;
    GroupListenerYD* listener_ = nullptr;
};
