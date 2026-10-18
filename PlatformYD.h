#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "DataFileYD.h"
#include "FriendRequestYD.h"
#include "GroupListenerYD.h"
#include "GroupManagerYD.h"
#include "MessageCenterYD.h"
#include "MomentYD.h"
#include "ServiceBaseYD.h"
#include "UserYD.h"

class IndependentIdServiceYD;

// 平台总容器：统一管理所有用户、服务账号和群。
// 构造时从数据文件加载（并检查、修正不一致的数据），析构时自动写回文件，实现“断电保存”。
// 数据文件的读写交给 DataFileYD：内容没变不写、被其他窗口改过不覆盖、先写临时文件并刷盘再替换。
// 平台同时是群事件的监听者（GroupListenerYD）：群里发生的事写成群聊记录中的系统提示
class PlatformYD : public GroupListenerYD {
public:
    explicit PlatformYD(const std::string& dataDir);
    ~PlatformYD();
    PlatformYD(const PlatformYD&) = delete;
    PlatformYD& operator=(const PlatformYD&) = delete;

    bool save() const;
    // 程序即将被强制结束（点击窗口 × 或按 Ctrl+C）时调用：最后保存一次，之后不再写文件，
    // 避免进程被结束时另一个线程正写到一半导致文件损坏
    bool saveOnExit() const;
    const std::string& dataDir() const;

    // ---- 用户与账号 ----
    ServiceBaseYD* findAccount(ServiceTypeYD type, const std::string& id) const;
    UserYD* findUser(int userId);
    UserYD* ownerOf(const ServiceBaseYD* account);
    const std::map<int, UserYD>& users() const;
    std::string displayName(ServiceTypeYD type, const std::string& id) const;

    // 检查某服务能否作为新用户开通的第一个服务（如微博必须先有 QQ）
    bool canRegisterWith(ServiceTypeYD type, std::string& msg) const;
    // 注册新用户并开通第一个服务；开通失败时不会留下空用户
    ServiceBaseYD* registerUser(const std::string& name, ServiceTypeYD type, const std::string& nickname,
                                const DateYD& birthday, const std::string& location,
                                const std::string& password, std::string& msg);
    // 为已有用户开通新服务，账号按服务规则自动分配
    ServiceBaseYD* openService(UserYD& user, ServiceTypeYD type, const std::string& nickname,
                               const DateYD& birthday, const std::string& location,
                               const std::string& password, std::string& msg);
    // 关闭某项服务：删除账号，并清除它在好友列表、群、讨论组、入群申请中的全部数据
    bool closeService(UserYD& user, ServiceTypeYD type, std::string& msg);
    // 注销整个用户：关闭其全部服务后删除用户。调用后 user 引用失效
    void deleteUser(int userId, std::string& msg);

    // ---- 跨服务身份对应（题目：微博与 QQ 共享 ID，微信独立 ID 但可与 QQ 号码绑定对应）----
    // 两个账号属于同一个用户、并且对应同一个 QQ 号（见 ServiceBaseYD::linkedQQ）时，才认定为同一人
    static bool isLinked(const ServiceBaseYD& a, const ServiceBaseYD& b);
    // 独立号码的服务（微信、微商）只能绑定本人已开通的 QQ 号
    bool bindQQ(UserYD& user, IndependentIdServiceYD& account, std::string& msg);
    bool unbindQQ(IndependentIdServiceYD& account, std::string& msg);

    // ---- 好友 ----
    // 直接建立双向好友关系（不经过验证）。界面上添加好友一律走 requestFriend
    bool addFriend(ServiceBaseYD& me, const std::string& friendId, const std::string& remark, std::string& msg);
    // 删除好友是单向的（与微信相同）：只从我的列表中删除，对方列表里仍有我；之后对方给我发消息会被拒绝
    bool removeFriend(ServiceBaseYD& me, const std::string& friendId, std::string& msg);
    std::vector<FriendYD> searchFriends(const ServiceBaseYD& me, const std::string& keyword) const;
    // 共同好友：在 a、b 两个服务中都是我好友、并且两边账号能确认是同一个人的用户。
    // 我自己的 a、b 两个账号对应不上（独立号码服务未绑定 QQ）时返回 false，msg 说明原因；
    // 对方未绑定、无法确认是同一人而没有计入的，写入 skipped
    bool commonFriends(const UserYD& user, ServiceTypeYD a, ServiceTypeYD b, std::vector<UserYD*>& result,
                       std::vector<std::string>& skipped, std::string& msg);
    // 推荐好友：source 服务中的好友，在 target 服务中有能确认是同一人的账号、但还不是我好友的
    bool recommendFriends(const UserYD& user, ServiceTypeYD target, ServiceTypeYD source,
                          std::vector<ServiceBaseYD*>& result, std::vector<std::string>& skipped, std::string& msg);
    // 我和某位好友在同一服务中的共同好友（返回账号）
    std::vector<std::string> mutualFriends(const ServiceBaseYD& me, const ServiceBaseYD& other) const;

    GroupManagerYD& groups();

    // ---- 群的新建与解散 ----
    // 新建的群号从 1007 开始顺延，创建者为群主，管理模式取本服务的默认模式
    GroupYD* createGroup(const ServiceBaseYD& me, const std::string& name, std::string& msg);
    // 只有群主可以解散；1001～1006 是题目约定的预置群，不能解散。解散后群聊记录一并作废
    bool dissolveGroup(const ServiceBaseYD& me, int groupNo, std::string& msg);

    // ---- 消息：好友私信、群消息（保存在 messages.txt，只追加，两个窗口可以同时收发）----
    MessageCenterYD& messages();
    // 只能给同一服务中的好友发私信
    bool sendPrivate(const ServiceBaseYD& me, const std::string& friendId, const std::string& text, std::string& msg);
    // 只有群成员可以发群消息；是否被禁言由群的当前管理策略决定
    bool sendGroupMessage(const ServiceBaseYD& me, const GroupYD& group, const std::string& text, std::string& msg);
    // 某账号的未读消息：私信条数、已加入的群中的群消息条数
    void unreadCount(const ServiceBaseYD& me, int& privateCount, int& groupCount) const;

    // ---- 好友申请与验证、黑名单（记录写在消息文件中，其他窗口也能立即看到）----
    // 发送好友申请。对方关闭了“加我为好友时需要验证”，或对方的好友列表里仍有我（我曾单向删除对方）时，直接成为好友
    bool requestFriend(ServiceBaseYD& me, const std::string& targetId, const std::string& greeting,
                       const std::string& remark, std::string& msg);
    std::vector<FriendRequestYD> friendRequests(const ServiceBaseYD& me);   // 收到的和发出的申请，最新的在前
    int pendingRequestCount(const ServiceBaseYD& me);                       // 收到的、还没处理的申请
    bool respondRequest(ServiceBaseYD& me, int seq, bool accept, const std::string& remark, std::string& msg);
    bool setBlocked(const ServiceBaseYD& me, const std::string& id, bool on, std::string& msg);
    bool isBlocked(ServiceTypeYD type, const std::string& blocker, const std::string& blocked) const;
    std::vector<std::string> blockedList(const ServiceBaseYD& me) const;
    void setVerification(const ServiceBaseYD& me, bool on);
    bool requiresVerification(const ServiceBaseYD& account) const;
    // 把消息文件中的好友申请结果、单向删除、拉黑、验证设置应用到内存（包括其他窗口刚写入的）
    void syncSocial();

    // ---- 撤回与 @提醒 ----
    // 自己的消息 2 分钟内可以撤回；群主、管理员按群当前的管理策略可以撤回其他成员的群消息
    bool canRevoke(const ServiceBaseYD& me, const MessageYD& message) const;
    bool revokeMessage(const ServiceBaseYD& me, int seq, std::string& msg);
    std::string revokeText(const ServiceBaseYD& viewer, const MessageYD& message) const;   // “小张”撤回了一条消息
    // 一条群消息 @ 了哪些成员：“@昵称”提醒某人；群主（QQ 群模式下还有管理员）可以“@所有人”
    std::vector<std::string> mentionsOf(const GroupYD& group, const MessageYD& message) const;
    int unreadMentions(const ServiceBaseYD& me, int groupNo) const;   // 未读消息中 @我 的条数

    // ---- 朋友圈（QQ 空间 / 微博动态……）----
    // 可见范围：all 公开 / self 仅自己 / only:a,b 部分可见 / except:a,b 不给谁看
    bool postMoment(const ServiceBaseYD& me, const std::string& text, const std::string& scope, std::string& msg);
    // viewer 能看到的动态（author 不为空时只看某个人的），最新的在前。
    // 只能看到互为好友的人的动态；点赞和评论只显示共同好友的（作者本人能看到全部）
    std::vector<MomentYD> moments(const ServiceBaseYD& viewer, const std::string& author = "");
    bool toggleLike(const ServiceBaseYD& me, int seq, std::string& msg);
    bool commentMoment(const ServiceBaseYD& me, int seq, const std::string& replyTo, const std::string& text, std::string& msg);
    bool deleteMoment(const ServiceBaseYD& me, int seq, std::string& msg);
    bool deleteComment(const ServiceBaseYD& me, int commentSeq, std::string& msg);
    // 别人给我的动态点赞、评论，或回复我的评论（unreadOnly 为 true 时只取未读的），最新的在前
    std::vector<std::string> momentNotices(const ServiceBaseYD& me, bool unreadOnly);
    void markMomentsRead(const ServiceBaseYD& me);

    // 群事件 → 群聊记录中的系统提示（GroupListenerYD 的接口）
    void onGroupEvent(const GroupYD& group, GroupEventYD event, const std::string& actor, const std::string& target) override;

private:
    std::string nickOf(ServiceTypeYD type, const std::string& id) const;
    bool friendsBothWays(ServiceTypeYD type, const std::string& a, const std::string& b) const;
    bool canSeeMoment(ServiceTypeYD type, const std::string& viewer, const std::string& author, const std::string& scope) const;
    void applyFriendship(ServiceTypeYD type, const std::string& requester, const std::string& target,
                         const std::string& requesterRemark, const std::string& targetRemark);
    static std::unique_ptr<ServiceBaseYD> createService(ServiceTypeYD type, const std::string& id, int ownerId,
                                                        const std::string& nickname, const DateYD& birthday,
                                                        const DateYD& registerDate, const std::string& location,
                                                        const std::string& password);
    std::string generateId(const UserYD& user, ServiceTypeYD type, std::string& msg) const;
    bool checkOwnLink(const ServiceBaseYD* first, const ServiceBaseYD* second, std::string& msg) const;
    bool sharedNumberOk(const UserYD& owner, const ServiceBaseYD& account) const;
    bool writeAll() const;

    // 文本 → 内存对象，返回格式错误或与已有数据冲突而被跳过的行数
    int loadUsers(const std::string& text);
    int loadAccounts(const std::string& text);
    int loadFriends(const std::string& text);
    // 全部加载后检查跨文件的一致性并自动修正，返回修正的处数
    int repairData();
    // 内存对象 → 文本
    std::string usersText() const;
    std::string accountsText() const;
    std::string friendsText() const;

    std::string dataDir_;
    std::string sessionStamp_;                                // 本次运行的时间戳，用于备份/冲突目录名
    std::vector<std::unique_ptr<ServiceBaseYD>> accounts_;   // 拥有所有账号对象
    std::map<int, UserYD> users_;
    GroupManagerYD groups_;
    MessageCenterYD messages_;
    std::set<std::string> blocked_;    // "服务 拉黑者 对方"
    std::set<std::string> noVerify_;   // "服务 账号"：加好友不需要验证（默认需要）
    int syncedLine_ = 0;               // 消息文件中已经应用到内存的记录位置
    mutable DataFileYD usersFile_;
    mutable DataFileYD accountsFile_;
    mutable DataFileYD friendsFile_;
    mutable DataFileYD groupsFile_;
    mutable DataFileYD subGroupsFile_;
    mutable std::mutex saveMutex_;   // 防止两个线程同时写文件
    mutable bool finalSaved_ = false;
};
