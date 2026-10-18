#pragma once
#include <string>
#include <vector>

class Platform;
class ServiceBase;

// 群操作命令（命令模式）：把“谁对哪个群做什么”封装成一个对象。
// 单机模式下图形界面直接交给 GroupService 执行；联网模式下客户端把它序列化后发给服务端，
// 服务端调用同一个 GroupService::execute——群管理的规则只有一份，两种模式的行为不会不一致
struct GroupCommand {
    enum Kind {
        JOIN, APPROVE, INVITE, QUIT, KICK, SET_ADMIN, SET_MUTED, TRANSFER, RENAME, SET_NOTICE, SWITCH_MODE,
        NEW_SUB_GROUP, ADD_TO_SUB_GROUP, DISSOLVE_SUB_GROUP, DISSOLVE
    };
    Kind kind = JOIN;
    int groupNo = 0;
    std::string target;                 // 成员账号（审批、邀请、踢人、管理员、禁言、转让）或讨论组名称（拉人、解散讨论组）
    std::string text;                   // 新群名、公告内容（为空表示清除）、新讨论组的名称、目标管理模式（"QQ" / "WECHAT"）
    bool on = true;                     // 同意/拒绝、设为/取消管理员、禁言/解除禁言
    std::vector<std::string> members;   // 新建讨论组、讨论组拉人时选中的成员

    static std::string kindName(Kind kind);   // "join"、"approve"……（联网时作为命令名）
    static bool parseKind(const std::string& name, Kind& kind);
};

struct GroupMemberInfo {
    std::string id;
    std::string nickname;
    bool owner = false;
    bool admin = false;   // 只在当前模式有管理员制度时为 true
    bool muted = false;   // 只在当前模式有禁言制度时为 true
    // 查看者能对他做的操作（由群当前的管理策略算出）
    bool canSetAdmin = false;
    bool canMute = false;
    bool canTransfer = false;
    bool canKick = false;
};

struct SubGroupInfo {
    std::string name;
    std::string creator;
    std::vector<std::string> members;
    bool canAdd = false;        // 查看者可以往里拉人
    bool canDissolve = false;   // 查看者可以解散它
};

// 某个查看者看到的群详情：群资料、成员、入群申请、讨论组，以及他在这个群里的身份和权限。
// 权限全部由服务端（单机模式下是本机）按群当前的管理策略算好，界面只管显示，不重复实现规则
struct GroupInfo {
    bool exists = false;
    int groupNo = 0;
    std::string name;
    std::string mode;       // "QQ" / "WECHAT"
    std::string modeName;   // “QQ群模式” / “微信群模式”
    std::string owner;      // 为空表示无主群（原群主注销且无人接任）
    std::string notice;
    bool preset = false;    // 1001～1006 平台预置群（不能解散）
    std::vector<std::string> features;   // 当前管理策略 showFeatures() 的输出（多态）
    std::vector<GroupMemberInfo> members;
    std::vector<GroupMemberInfo> pending;   // 待审批的入群申请
    std::vector<SubGroupInfo> subGroups;
    int frozenAdmins = 0;   // 当前模式没有管理员制度，但保留着的管理员记录数
    int frozenMuted = 0;    // 当前模式没有禁言制度，但保留着的禁言记录数

    bool member = false;
    bool isOwner = false;
    bool isAdmin = false;
    bool applied = false;          // 我已提交入群申请
    bool canEditInfo = false;      // 改群名、发公告
    bool allowApplyJoin = false;
    bool allowSubGroup = false;
    bool allowAdmin = false;
    bool allowMute = false;
    bool canApprove = false;
    bool canDissolve = false;
};

struct GroupBrief {   // “全部群”列表中的一项
    int groupNo = 0;
    std::string name;
    std::string modeName;
    std::string owner;
    std::string ownerName;
    int memberCount = 0;
    bool joined = false;
    bool applied = false;
    bool allowApplyJoin = false;
};

// 群的应用服务：查询群详情、执行群命令。图形界面的单机模式和服务端共用
class GroupService {
public:
    static GroupInfo describe(Platform& platform, const ServiceBase& viewer, int groupNo);
    static std::vector<GroupBrief> list(Platform& platform, const ServiceBase& viewer);   // 本服务的全部群

    // 执行命令：成功返回 true；msg 是给用户看的结果（切换管理模式时有多行：结果、数据比对、新模式的特色）
    static bool execute(Platform& platform, const ServiceBase& me, const GroupCommand& command, std::string& msg);
    // 新建群：成功返回群号，失败返回 0
    static int create(Platform& platform, const ServiceBase& me, const std::string& name, std::string& msg);
};
