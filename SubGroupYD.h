#pragma once
#include <string>
#include <vector>

// 临时讨论组（子群）：只在 QQ 群模式下可用
class SubGroupYD {
public:
    explicit SubGroupYD(const std::string& name);

    const std::string& name() const;
    const std::vector<std::string>& members() const;
    const std::string& creator() const;   // 创建者：第一个成员
    bool addMember(const std::string& id);
    bool removeMember(const std::string& id);

private:
    std::string name_;
    std::vector<std::string> members_;
};
