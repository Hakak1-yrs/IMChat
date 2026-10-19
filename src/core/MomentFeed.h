#pragma once
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include "MessageStore.h"
#include "MomentComment.h"
#include "ServiceType.h"

// 朋友圈的内存索引：由消息存储里的动态（P）、点赞（L）、评论（K）、删除动态（Y）、删除评论（Z）记录按先后构建。
// 和好友记录一样增量更新：每次只应用上次之后的新记录（单机模式下其他窗口写的记录也能及时看到）。
// 看朋友圈只需要遍历查看者自己和好友的动态，点赞、评论、删除按序号直接找到，不必再把全部记录读一遍。
// 账号注销会让与它有关的记录作废（从存储里删掉），这时整体重建一次。
// 只保存记录本身的内容；谁能看到哪些动态、哪些点赞和评论，由 Platform 在查询时按当时的好友关系判断
class MomentFeed {
public:
    struct Post {
        int seq = 0;
        ServiceType service = ServiceType::QQ;
        std::string author;
        std::string scope;
        std::string time;
        std::string text;
        bool deleted = false;
        std::vector<std::string> likes;   // 按点赞的先后
        std::vector<int> comments;        // 评论序号，按先后
    };
    struct Comment : MomentComment {
        ServiceType service = ServiceType::QQ;
        int postSeq = 0;
        bool deleted = false;
    };
    // 和某人有关的一次互动：别人给他的动态点赞、评论，或者回复了他。用来生成"朋友圈提醒"
    struct Interaction {
        char kind = 'L';   // L 点赞 / K 评论
        int seq = 0;       // 这条点赞或评论记录的序号
        int postSeq = 0;
        std::string actor;
    };

    explicit MomentFeed(const MessageStore& store);

    void refresh();   // 应用新记录；存储里有记录被清除过，就整体重建
    const Post* post(int seq) const;         // 包括已删除的动态（deleted 为 true）
    const Comment* comment(int seq) const;   // 包括已删除的评论
    std::vector<const Post*> postsOf(ServiceType service, const std::string& author) const;   // 按序号递增
    const std::vector<Interaction>& interactionsOf(ServiceType service, const std::string& id) const;   // 按先后
    int lastInteraction(ServiceType service) const;   // 最近一次点赞或评论的序号

private:
    void apply(const Record& r);
    static std::string key(ServiceType service, const std::string& id);

    const MessageStore& store_;
    int line_ = 0;      // 已经应用到的记录位置
    int clears_ = 0;    // 构建时存储的 recordClears()
    std::unordered_map<int, Post> posts_;
    std::unordered_map<int, Comment> comments_;
    std::unordered_map<std::string, std::vector<int>> byAuthor_;              // "服务 作者" → 动态序号
    std::unordered_map<std::string, std::vector<Interaction>> interactions_;  // "服务 账号" → 和他有关的互动
    std::map<ServiceType, int> lastInteraction_;
};
