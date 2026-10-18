#pragma once
#include "ServiceBase.h"

// 使用独立号码的服务账号的公共基类（微信、微商都属于这一类）。
// 题目：“微博与QQ共享ID，微信采用独立ID，但是可以与QQ号码绑定对应。其他微X产品也分为这两种情况。”
// 共享号码的服务（QQ、微博）天然对应自己的 QQ 号；独立号码的服务只有绑定了本人的 QQ 号，
// 平台才认定它与本人的 QQ、微博属于同一个人。绑定规则写在这一层，新增的独立号码服务直接继承即可。
class IndependentIdService : public ServiceBase {
public:
    using ServiceBase::ServiceBase;

    bool usesQQNumber() const override;                   // 独立号码，一律返回 false
    std::string linkedQQ() const override;                // 绑定的 QQ 号，未绑定为空
    void showInfo() const override;                       // 在基类信息后追加绑定情况
    std::string extraData() const override;               // 存盘内容为绑定的 QQ 号，未绑定写 -
    void loadExtraData(const std::string& data) override;

    // 只记录绑定结果；“只能绑定本人的 QQ 号”由 Platform::bindQQ 检查
    void bindQQ(const std::string& qqId);
    void unbindQQ();
    bool isBound() const;
    const std::string& boundQQ() const;

private:
    std::string boundQQ_;   // 绑定的 QQ 号，为空表示未绑定
};
