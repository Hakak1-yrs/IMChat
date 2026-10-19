#include "MomentFeed.h"
#include <algorithm>
#include <cstdlib>
#include "Util.h"

MomentFeed::MomentFeed(const MessageStore& store) : store_(store) {}

std::string MomentFeed::key(ServiceType service, const std::string& id) { return ServiceTypeHelper::code(service) + " " + id; }

void MomentFeed::refresh() {
    const int clears = store_.recordClears();
    if (clears != clears_) {   // 有账号注销、旧记录少了一部分：从头重建
        posts_.clear();
        comments_.clear();
        byAuthor_.clear();
        interactions_.clear();
        lastInteraction_.clear();
        line_ = 0;
        clears_ = clears;
    }
    // 位置前进到最后一条记录（不分类型），下次不必再扫一遍中间的好友记录
    const int end = store_.lastRecordLine();
    if (end <= line_) return;
    int last = end;
    for (const Record& r : store_.recordsAfter("PLKYZ", line_)) {
        apply(r);
        last = (std::max)(last, r.line);
    }
    line_ = last;
}

void MomentFeed::apply(const Record& r) {
    switch (r.kind) {
    case 'P': {   // 作者 可见范围
        Post& p = posts_[r.seq];
        p.seq = r.seq;
        p.service = r.service;
        p.author = r.field(0);
        p.scope = r.field(1);
        p.time = r.time;
        p.text = r.text;
        byAuthor_[key(r.service, p.author)].push_back(r.seq);
        break;
    }
    case 'L': {   // 动态序号 点赞者 1/0：按先后处理点赞和取消
        int& last = lastInteraction_[r.service];
        last = (std::max)(last, r.seq);
        auto it = posts_.find(std::atoi(r.field(0).c_str()));
        if (it == posts_.end()) break;
        Post& p = it->second;
        Util::removeValue(p.likes, r.field(1));
        if (r.field(2) != "1") break;
        p.likes.push_back(r.field(1));
        if (r.field(1) != p.author) interactions_[key(r.service, p.author)].push_back({ 'L', r.seq, p.seq, r.field(1) });
        break;
    }
    case 'K': {   // 动态序号 评论者 回复对象（没有时为 -）
        int& last = lastInteraction_[r.service];
        last = (std::max)(last, r.seq);
        Comment& c = comments_[r.seq];
        c.seq = r.seq;
        c.service = r.service;
        c.postSeq = std::atoi(r.field(0).c_str());
        c.author = r.field(1);
        c.replyTo = r.field(2) == "-" ? "" : r.field(2);
        c.time = r.time;
        c.text = r.text;
        auto it = posts_.find(c.postSeq);
        if (it == posts_.end()) break;
        Post& p = it->second;
        p.comments.push_back(r.seq);
        // 提醒动态作者（有人评论了他的动态）和被回复的人（有人回复了他）；同一个人只记一次
        const Interaction interaction{ 'K', r.seq, p.seq, c.author };
        if (p.author != c.author) interactions_[key(r.service, p.author)].push_back(interaction);
        if (!c.replyTo.empty() && c.replyTo != c.author && c.replyTo != p.author)
            interactions_[key(r.service, c.replyTo)].push_back(interaction);
        break;
    }
    case 'Y': {   // 动态序号
        auto it = posts_.find(std::atoi(r.field(0).c_str()));
        if (it != posts_.end()) it->second.deleted = true;
        break;
    }
    case 'Z': {   // 评论序号
        auto it = comments_.find(std::atoi(r.field(0).c_str()));
        if (it != comments_.end()) it->second.deleted = true;
        break;
    }
    }
}

const MomentFeed::Post* MomentFeed::post(int seq) const {
    auto it = posts_.find(seq);
    return it == posts_.end() ? nullptr : &it->second;
}

const MomentFeed::Comment* MomentFeed::comment(int seq) const {
    auto it = comments_.find(seq);
    return it == comments_.end() ? nullptr : &it->second;
}

std::vector<const MomentFeed::Post*> MomentFeed::postsOf(ServiceType service, const std::string& author) const {
    std::vector<const Post*> result;
    auto it = byAuthor_.find(key(service, author));
    if (it == byAuthor_.end()) return result;
    for (int seq : it->second) {
        if (const Post* p = post(seq)) result.push_back(p);
    }
    return result;
}

const std::vector<MomentFeed::Interaction>& MomentFeed::interactionsOf(ServiceType service, const std::string& id) const {
    static const std::vector<Interaction> none;
    auto it = interactions_.find(key(service, id));
    return it == interactions_.end() ? none : it->second;
}

int MomentFeed::lastInteraction(ServiceType service) const {
    auto it = lastInteraction_.find(service);
    return it == lastInteraction_.end() ? 0 : it->second;
}
