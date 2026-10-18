#include "MomentsPage.h"
#include "Dialog.h"
#include "GuiText.h"
#include "GuiWindow.h"
#include "MainView.h"
#include "Session.h"
#include "Util.h"

MomentsPage::MomentsPage(GuiWindow& window, const std::string& author) : Page(window), author_(author) {
    Session& s = window.session();
    s.loadMoments(author_);   // 联网模式：向服务端取；单机模式什么也不做
    newNotices_ = s.momentNoticeList(true);
    s.markMomentsRead();
}

std::string MomentsPage::key() const { return author_.empty() ? "moments" : "P:" + author_; }

std::wstring MomentsPage::name(const std::string& id) { return GuiText::wide(session().nicknameOf(id) + "(" + id + ")"); }

void MomentsPage::build(PageModel& model) {
    Session& s = session();
    const AccountView m = s.me();
    model.title = GuiText::wide(m.momentsName);
    model.subtitle = L"只能看到互为好友的人的动态 · 点赞和评论只显示共同好友的";
    model.headerButtons.push_back(PageModel::button(L"互动消息", ACT_NOTICES));
    model.headerButtons.push_back(PageModel::button(author_ == m.id ? L"全部动态" : L"我的动态", author_ == m.id ? ACT_ALL : ACT_MINE));
    model.headerButtons.push_back(PageModel::button(L"发表动态", ACT_POST, "", PageModel::PRIMARY));

    if (!newNotices_.empty()) {
        model.section(L"新互动 · " + std::to_wstring(newNotices_.size()));
        std::wstring text;
        for (const std::string& n : newNotices_) text += (text.empty() ? L"" : L"\n") + GuiText::wide(n);
        model.text(text);
    }
    if (!author_.empty()) {
        model.note(L"只看 " + name(author_) + L" 的动态");
        if (author_ != m.id) model.actions({ PageModel::button(L"查看全部动态", ACT_ALL) });
    }

    const std::optional<std::vector<Moment>> list = s.moments(author_);
    if (!list) {
        model.note(L"正在获取动态……");
        return;
    }
    if (list->empty()) model.note(L"还没有动态。发表第一条吧！");
    for (const Moment& post : *list) {
        model.note(L"");   // 每条动态单独一张卡片
        std::wstring sub = GuiText::chatTime(post.time);
        if (post.author == m.id) sub += L" · " + GuiText::wide(Moment::scopeText(post.scope));
        PageModel::Row& head = model.person(post.author, GuiText::wide(s.nicknameOf(post.author)), name(post.author), sub);
        const std::string seq = std::to_string(post.seq);
        head.buttons.push_back(PageModel::button(post.likedBy(m.id) ? L"♥ 取消赞" : L"♡ 赞", ACT_LIKE, seq,
                                                 post.likedBy(m.id) ? PageModel::PRIMARY : PageModel::NORMAL));
        head.buttons.push_back(PageModel::button(L"评论", ACT_COMMENT, seq));
        if (post.author == m.id) head.buttons.push_back(PageModel::button(L"删除", ACT_DELETE_POST, seq, PageModel::DANGER));
        model.text(GuiText::wide(post.text));
        if (!post.likes.empty()) {
            std::wstring names;
            for (const std::string& id : post.likes) names += (names.empty() ? L"" : L"、") + name(id);
            model.info(L"♥ " + std::to_wstring(post.likes.size()), names);
        }
        for (const MomentComment& c : post.comments) {
            std::wstring value = GuiText::wide(c.text);
            if (c.isReply()) value = L"回复 " + GuiText::wide(s.nicknameOf(c.replyTo)) + L"：" + value;
            PageModel::Row& row = model.info(GuiText::wide(s.nicknameOf(c.author)), value);
            if (c.author != m.id) row.buttons.push_back(PageModel::button(L"回复", ACT_REPLY, seq + "|" + c.author));
            if (c.author == m.id || post.author == m.id)   // 自己的评论，或自己动态下的评论
                row.buttons.push_back(PageModel::button(L"删除", ACT_DELETE_COMMENT, std::to_string(c.seq)));
        }
    }
}

void MomentsPage::onAction(int action, const std::string& arg) {
    switch (action) {
    case ACT_POST: postDialog(); return;
    case ACT_MINE: main().showPage(std::make_shared<MomentsPage>(window_, session().me().id)); return;
    case ACT_ALL: main().showPage(std::make_shared<MomentsPage>(window_)); return;
    case ACT_LIKE: {
        GuiWindow& window = window_;   // 点赞成功不提示，只刷新；失败才提示原因
        session().toggleLike(std::stoi(arg), [&window](bool ok, const std::string& message) {
            if (!ok) window.mainView().toast(GuiText::wide(message));
            window.mainView().reload();
        });
        return;
    }
    case ACT_COMMENT: commentDialog(std::stoi(arg), ""); return;
    case ACT_REPLY: {
        const size_t bar = arg.find('|');
        commentDialog(std::stoi(arg.substr(0, bar)), arg.substr(bar + 1));
        return;
    }
    case ACT_DELETE_POST: {
        auto dialog = std::make_unique<Dialog>(window_, L"删除动态");
        dialog->message(L"确认删除这条动态？点赞和评论会一起删除。").primary(L"删除", [this, arg](Dialog&) {
            session().deleteMoment(std::stoi(arg), report());
            return true;
        }, true);
        window_.openDialog(std::move(dialog));
        return;
    }
    case ACT_DELETE_COMMENT: session().deleteComment(std::stoi(arg), report()); return;
    case ACT_NOTICES: {
        std::vector<std::wstring> lines;
        for (const std::string& n : session().momentNoticeList(false)) lines.push_back(GuiText::wide(n));
        if (lines.empty()) lines.push_back(L"暂无互动消息");
        auto dialog = std::make_unique<Dialog>(window_, L"互动消息");
        dialog->lines(lines).cancelText(L"关闭");
        window_.openDialog(std::move(dialog));
        return;
    }
    }
}

void MomentsPage::postDialog() {
    std::vector<std::string> ids;
    std::vector<std::wstring> names;
    for (const Conversation& f : session().contacts()) {
        ids.push_back(f.peerId);
        names.push_back(name(f.peerId));
    }
    auto dialog = std::make_unique<Dialog>(window_, L"发表动态");
    dialog->field(L"这一刻的想法……", L"", L"最多 250 个字")
        .choice(L"谁可以看", { L"公开", L"私密", L"部分可见", L"不给谁看" }, 0)
        .pick(L"选择好友（“部分可见”“不给谁看”时使用）", names, true)
        .primary(L"发表", [this, ids](Dialog& d) {
            if (Util::trim(d.text(0)).empty()) {
                d.setError(L"动态内容不能为空。");
                return false;
            }
            static const char* scopes[] = { "all", "self", "only:", "except:" };
            std::string scope = scopes[d.selected(0)];
            if (d.selected(0) >= 2) {
                std::vector<std::string> chosen;
                for (size_t i : d.picked()) chosen.push_back(ids[i]);
                if (chosen.empty()) {
                    d.setError(L"请选择好友。");
                    return false;
                }
                scope += Util::joinList(chosen);
            }
            session().postMoment(d.text(0), scope, report());
            return true;
        });
    window_.openDialog(std::move(dialog));
}

void MomentsPage::commentDialog(int seq, const std::string& replyTo) {
    auto dialog = std::make_unique<Dialog>(window_, replyTo.empty() ? L"评论" : L"回复 " + GuiText::wide(session().nicknameOf(replyTo)));
    dialog->field(L"内容").primary(L"发送", [this, seq, replyTo](Dialog& d) {
        if (Util::trim(d.text(0)).empty()) {
            d.setError(L"评论内容不能为空。");
            return false;
        }
        session().commentMoment(seq, replyTo, d.text(0), report());
        return true;
    });
    window_.openDialog(std::move(dialog));
}
