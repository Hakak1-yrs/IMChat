#pragma once
#include <vector>
#include "Page.h"

// 朋友圈（QQ 空间 / 微博动态……，名称由服务的虚函数 momentsName() 决定）：
// 发表动态（公开、私密、部分可见、不给谁看），点赞、评论、回复评论，删除自己的动态和评论，查看互动消息。
// 与微信相同：只能看到互为好友的人的动态，点赞和评论只显示共同好友的。
// 页面只通过 Session 取数据、做操作，单机和联网两种模式共用
class MomentsPage : public Page {
public:
    MomentsPage(GuiWindow& window, const std::string& author = "");   // author 不为空时只看某人的动态
    void build(PageModel& model) override;
    void onAction(int action, const std::string& arg) override;
    std::string key() const override;

private:
    enum Action { ACT_POST = 1, ACT_LIKE, ACT_COMMENT, ACT_REPLY, ACT_DELETE_POST, ACT_DELETE_COMMENT, ACT_NOTICES, ACT_MINE, ACT_ALL };
    std::wstring name(const std::string& id);   // “昵称(账号)”
    void postDialog();
    void commentDialog(int seq, const std::string& replyTo);

    std::string author_;
    std::vector<std::string> newNotices_;   // 打开页面时的未读互动（打开即记为已读）
};
