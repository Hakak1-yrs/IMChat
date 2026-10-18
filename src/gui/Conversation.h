#pragma once
#include <string>

// 一个会话：与某位好友的私信，或某个群的群聊。会话列表中的每一行就是一个会话对象。
// 只是界面层用来传递数据的简单类，字段都由 Session 填写
class Conversation {
public:
    static Conversation privateChat(const std::string& peerId);
    static Conversation groupChat(int groupNo);

    std::string key() const;          // 唯一标识，如 "P:10002"、"G:1001"
    bool sameAs(const Conversation& other) const;

    bool isGroup = false;
    std::string peerId;               // 私信：对方账号
    int groupNo = 0;                  // 群聊：群号
    std::string title;                // 列表中显示的名称（好友备注 / 群名称）
    std::string avatarName;           // 头像上取第一个字的名称（对方昵称 / 群名称），与备注无关，各处头像保持一致
    std::string subtitle;             // 联系人、群列表中的第二行
    std::string preview;              // 最后一条消息的预览
    std::string lastTime;             // 最后一条消息的时间，没有消息时为空
    int unread = 0;
    bool mentioned = false;           // 群里有未读的消息 @了我（列表中显示“[有人@我]”）
};
