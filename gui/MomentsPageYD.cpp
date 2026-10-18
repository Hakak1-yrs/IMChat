#include "MomentsPageYD.h"
#include "DialogYD.h"
#include "GuiTextYD.h"
#include "GuiWindowYD.h"
#include "MainViewYD.h"
#include "UtilYD.h"

MomentsPageYD::MomentsPageYD(GuiWindowYD& window, const std::string& author) : PageYD(window), author_(author) {
    newNotices_ = platform().momentNotices(me(), true);
    platform().markMomentsRead(me());
}

std::string MomentsPageYD::key() const { return author_.empty() ? "moments" : "P:" + author_; }

void MomentsPageYD::build(PageModelYD& model) {
    ServiceBaseYD& m = me();
    const ServiceTypeYD type = m.type();
    model.title = GuiTextYD::wide(m.momentsName());
    model.subtitle = L"只能看到互为好友的人的动态 · 点赞和评论只显示共同好友的";
    model.headerButtons.push_back(PageModelYD::button(L"互动消息", ACT_NOTICES));
    model.headerButtons.push_back(PageModelYD::button(author_ == m.id() ? L"全部动态" : L"我的动态", author_ == m.id() ? ACT_ALL : ACT_MINE));
    model.headerButtons.push_back(PageModelYD::button(L"发表动态", ACT_POST, "", PageModelYD::PRIMARY));

    if (!newNotices_.empty()) {
        model.section(L"新互动 · " + std::to_wstring(newNotices_.size()));
        std::wstring text;
        for (const std::string& n : newNotices_) text += (text.empty() ? L"" : L"\n") + GuiTextYD::wide(n);
        model.text(text);
    }
    if (!author_.empty()) {
        model.note(L"只看 " + GuiTextYD::wide(platform().displayName(type, author_)) + L" 的动态");
        if (author_ != m.id()) model.actions({ PageModelYD::button(L"查看全部动态", ACT_ALL) });
    }

    std::vector<MomentYD> list = platform().moments(m, author_);
    if (list.empty()) model.note(L"还没有动态。发表第一条吧！");
    for (const MomentYD& post : list) {
        model.note(L"");   // 每条动态单独一张卡片
        ServiceBaseYD* author = platform().findAccount(type, post.author);
        std::wstring sub = GuiTextYD::chatTime(post.time);
        if (post.author == m.id()) sub += L" · " + GuiTextYD::wide(MomentYD::scopeText(post.scope));
        PageModelYD::RowYD& head = model.person(post.author, GuiTextYD::wide(author ? author->nickname() : post.author),
                                                GuiTextYD::wide(platform().displayName(type, post.author)), sub);
        const std::string seq = std::to_string(post.seq);
        head.buttons.push_back(PageModelYD::button(post.likedBy(m.id()) ? L"♥ 取消赞" : L"♡ 赞", ACT_LIKE, seq,
                                                   post.likedBy(m.id()) ? PageModelYD::PRIMARY : PageModelYD::NORMAL));
        head.buttons.push_back(PageModelYD::button(L"评论", ACT_COMMENT, seq));
        if (post.author == m.id()) head.buttons.push_back(PageModelYD::button(L"删除", ACT_DELETE_POST, seq, PageModelYD::DANGER));
        model.text(GuiTextYD::wide(post.text));
        if (!post.likes.empty()) {
            std::wstring names;
            for (const std::string& id : post.likes) names += (names.empty() ? L"" : L"、") + GuiTextYD::wide(platform().displayName(type, id));
            model.info(L"♥ " + std::to_wstring(post.likes.size()), names);
        }
        for (const MomentCommentYD& c : post.comments) {
            ServiceBaseYD* who = platform().findAccount(type, c.author);
            std::wstring value = GuiTextYD::wide(c.text);
            if (c.isReply()) {
                ServiceBaseYD* to = platform().findAccount(type, c.replyTo);
                value = L"回复 " + GuiTextYD::wide(to ? to->nickname() : c.replyTo) + L"：" + value;
            }
            PageModelYD::RowYD& row = model.info(GuiTextYD::wide(who ? who->nickname() : c.author), value);
            if (c.author != m.id()) row.buttons.push_back(PageModelYD::button(L"回复", ACT_REPLY, seq + "|" + c.author));
            if (c.author == m.id() || post.author == m.id())   // 自己的评论，或自己动态下的评论
                row.buttons.push_back(PageModelYD::button(L"删除", ACT_DELETE_COMMENT, std::to_string(c.seq)));
        }
    }
}

void MomentsPageYD::onAction(int action, const std::string& arg) {
    std::string msg;
    switch (action) {
    case ACT_POST: postDialog(); return;
    case ACT_MINE: main().showPage(std::make_shared<MomentsPageYD>(window_, me().id())); return;
    case ACT_ALL: main().showPage(std::make_shared<MomentsPageYD>(window_)); return;
    case ACT_LIKE:
        if (!platform().toggleLike(me(), std::stoi(arg), msg)) toast(GuiTextYD::wide(msg));
        return;
    case ACT_COMMENT: commentDialog(std::stoi(arg), ""); return;
    case ACT_REPLY: {
        const size_t bar = arg.find('|');
        commentDialog(std::stoi(arg.substr(0, bar)), arg.substr(bar + 1));
        return;
    }
    case ACT_DELETE_POST: {
        auto dialog = std::make_unique<DialogYD>(window_, L"删除动态");
        dialog->message(L"确认删除这条动态？点赞和评论会一起删除。").primary(L"删除", [this, arg](DialogYD&) {
            std::string result;
            platform().deleteMoment(me(), std::stoi(arg), result);
            toast(GuiTextYD::wide(result));
            return true;
        }, true);
        window_.openDialog(std::move(dialog));
        return;
    }
    case ACT_DELETE_COMMENT:
        platform().deleteComment(me(), std::stoi(arg), msg);
        toast(GuiTextYD::wide(msg));
        return;
    case ACT_NOTICES: {
        std::vector<std::wstring> lines;
        for (const std::string& n : platform().momentNotices(me(), false)) lines.push_back(GuiTextYD::wide(n));
        if (lines.empty()) lines.push_back(L"暂无互动消息");
        auto dialog = std::make_unique<DialogYD>(window_, L"互动消息");
        dialog->lines(lines).cancelText(L"关闭");
        window_.openDialog(std::move(dialog));
        return;
    }
    }
}

void MomentsPageYD::postDialog() {
    std::vector<std::string> ids;
    std::vector<std::wstring> names;
    for (const FriendYD& f : me().friends().all()) {
        ids.push_back(f.id());
        names.push_back(GuiTextYD::wide(platform().displayName(me().type(), f.id())));
    }
    auto dialog = std::make_unique<DialogYD>(window_, L"发表动态");
    dialog->field(L"这一刻的想法……", L"", L"最多 250 个汉字")
        .choice(L"谁可以看", { L"公开", L"私密", L"部分可见", L"不给谁看" }, 0)
        .pick(L"选择好友（“部分可见”“不给谁看”时使用）", names, true)
        .primary(L"发表", [this, ids](DialogYD& d) {
            static const char* scopes[] = { "all", "self", "only:", "except:" };
            std::string scope = scopes[d.selected(0)];
            if (d.selected(0) >= 2) {
                std::vector<std::string> chosen;
                for (size_t i : d.picked()) chosen.push_back(ids[i]);
                scope += UtilYD::joinList(chosen);
            }
            std::string result;
            if (!platform().postMoment(me(), d.text(0), scope, result)) {
                d.setError(GuiTextYD::wide(result));
                return false;
            }
            toast(GuiTextYD::wide(result));
            return true;
        });
    window_.openDialog(std::move(dialog));
}

void MomentsPageYD::commentDialog(int seq, const std::string& replyTo) {
    ServiceBaseYD* to = replyTo.empty() ? nullptr : platform().findAccount(me().type(), replyTo);
    auto dialog = std::make_unique<DialogYD>(window_, to ? L"回复 " + GuiTextYD::wide(to->nickname()) : L"评论");
    dialog->field(L"内容").primary(L"发送", [this, seq, replyTo](DialogYD& d) {
        std::string result;
        if (!platform().commentMoment(me(), seq, replyTo, d.text(0), result)) {
            d.setError(GuiTextYD::wide(result));
            return false;
        }
        toast(GuiTextYD::wide(result));
        return true;
    });
    window_.openDialog(std::move(dialog));
}
