#pragma once
#include <string>
#include <vector>
#include "MomentCommentYD.h"
#include "ServiceTypeYD.h"

// 一条朋友圈动态。likes、comments 只包含“当前查看者有权看到的”点赞和评论——
// 与微信相同：只能看到共同好友的点赞和评论（动态作者能看到全部）。
// 简单的数据类：字段公开，由 PlatformYD::moments() 填写
class MomentYD {
public:
    // 可见范围的写法：all 所有好友 / self 仅自己 / only:a,b 部分可见 / except:a,b 不给谁看
    static std::string scopeText(const std::string& scope);   // 界面上显示的说明
    bool likedBy(const std::string& id) const;

    int seq = 0;
    ServiceTypeYD service = ServiceTypeYD::QQ;
    std::string author;
    std::string scope;
    std::string time;
    std::string text;
    std::vector<std::string> likes;
    std::vector<MomentCommentYD> comments;
};
