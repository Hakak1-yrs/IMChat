// 自动测试：直接调用业务类的接口，逐条检查题目要求和设计约定，不需要手工在菜单里输入。
// 每个测试从同一份固定的基线数据开始，在 test_tmp 目录下运行，不会改动 data 目录中的演示数据。
// 运行方式：在解决方案中把 IMSystemTestsYD 设为启动项目后按 Ctrl+F5，或直接运行生成的 IMSystemTestsYD.exe。
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../DataFileYD.h"
#include "../DateYD.h"
#include "../GroupYD.h"
#include "../IndependentIdServiceYD.h"
#include "../MessageCenterYD.h"
#include "../PasswordYD.h"
#include "../PlatformYD.h"
#include "../UtilYD.h"

namespace fs = std::filesystem;

namespace {

int g_checks = 0;
int g_failures = 0;
std::string g_case;

void check(bool ok, const std::string& what, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cout << "    [失败] " << g_case << "：" << what << "（第 " << line << " 行）\n";
    }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

// 运行 action 期间把 std::cout 的输出收集起来返回：平台类的提示不会淹没测试结果，也可以检查提示内容
template <typename Action>
std::string capture(Action action) {
    std::ostringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
    action();
    std::cout.rdbuf(old);
    return buffer.str();
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

const std::string kRoot = "test_tmp\\";

// ---- 基线数据：与发布的演示数据相同（密码写成明文 123456，加载时会自动转为加密记录）----
const char* kUsers = R"(# 用户编号 姓名
1 张三
2 李四
3 王五
4 赵六
5 孙七
6 周八
)";

const char* kAccounts = R"(# 服务 账号 用户编号 昵称 出生日期 申请日期 所在地 密码记录 扩展字段
QQ 10001 1 小张 2004-03-15 2014-07-01 北京 123456 -
QQ 10002 2 李小四 2003-11-02 2015-09-10 上海 123456 -
QQ 10003 3 王老五 2004-06-20 2016-02-18 广州 123456 -
QQ 10004 4 赵六六 2005-01-08 2017-05-05 深圳 123456 -
QQ 10005 5 孙小七 2004-09-30 2018-08-08 杭州 123456 -
WECHAT wx2001 1 张三丰 2004-03-15 2016-03-01 北京 123456 10001
WECHAT wx2002 2 四哥 2003-11-02 2017-04-12 上海 123456 10002
WECHAT wx2003 3 五五开 2004-06-20 2018-06-06 广州 123456 10003
WECHAT wx2005 5 七七 2004-09-30 2019-01-20 杭州 123456 -
WECHAT wx2006 6 周周 2002-12-12 2016-10-10 成都 123456 -
WEIBO 10001 1 张三的微博 2004-03-15 2019-05-01 北京 123456 -
WEIBO 10003 3 老五爱摄影 2004-06-20 2020-07-15 广州 123456 -
WEIBO 10004 4 六六说科技 2005-01-08 2021-02-02 深圳 123456 -
WEISHANG ws3001 6 周八小店 2002-12-12 2021-06-18 成都 123456 -
WEISHANG ws3002 5 孙七好物 2004-09-30 2022-03-15 深圳 123456 -
)";

const char* kFriends = R"(# 服务 我的账号 好友账号 备注
QQ 10001 10002 李四
QQ 10001 10003 王五
QQ 10001 10004 赵六
QQ 10001 10005 孙七
QQ 10002 10001 张三
QQ 10002 10003 老五
QQ 10003 10001 张三
QQ 10003 10002 四哥
QQ 10004 10001 张三
QQ 10005 10001 张三
WECHAT wx2001 wx2002 李四
WECHAT wx2001 wx2006 周八
WECHAT wx2002 wx2001 张三
WECHAT wx2002 wx2003 王五
WECHAT wx2003 wx2002 李四
WECHAT wx2006 wx2001 张三
WEIBO 10001 10004 六六
WEIBO 10004 10001 张三
WEISHANG ws3001 ws3002 七七
WEISHANG ws3002 ws3001 周老板
)";

const char* kGroups = R"(# 服务 群号 群名 群主 管理模式 管理员列表 成员列表 待审批申请
QQ 1001 编程交流群 10001 QQ 10002 10001,10002,10003,10004 10005
QQ 1002 C++学习群 10002 QQ - 10002,10001,10005 -
QQ 1003 篮球爱好者 10003 QQ - 10003,10004 -
QQ 1004 考研互助群 10004 QQ - 10004,10001 -
QQ 1005 游戏开黑群 10005 QQ - 10005 -
QQ 1006 班级通知群 10001 QQ 10003 10001,10002,10003,10004,10005 -
WECHAT 1001 相亲相爱一家人 wx2001 WECHAT - wx2001,wx2006 -
WECHAT 1002 302宿舍 wx2002 WECHAT - wx2002,wx2001,wx2003 -
WECHAT 1003 课设项目组 wx2001 WECHAT - wx2001,wx2002,wx2003,wx2005 -
WECHAT 1004 跑步打卡 wx2003 WECHAT - wx2003 -
WECHAT 1005 校园二手 wx2006 WECHAT - wx2006,wx2005 -
WECHAT 1006 高中同学 wx2005 WECHAT - wx2005,wx2001 -
WEIBO 1001 粉丝交流群 10001 QQ - 10001,10004 -
WEIBO 1002 读书分享 10003 QQ - 10003 -
WEIBO 1003 摄影交流 10004 QQ - 10004,10001 -
WEIBO 1004 美食探店 10001 QQ - 10001 -
WEIBO 1005 旅行攻略 10003 QQ - 10003,10004 -
WEIBO 1006 科技资讯 10004 QQ - 10004 -
WEISHANG 1001 微商交流群 ws3001 WECHAT - ws3001 -
WEISHANG 1002 微商学习群 ws3001 WECHAT - ws3001 -
WEISHANG 1003 微商兴趣群 ws3001 WECHAT - ws3001 -
WEISHANG 1004 微商活动群 ws3001 WECHAT - ws3001 -
WEISHANG 1005 微商通知群 ws3001 WECHAT - ws3001 -
WEISHANG 1006 微商闲聊群 ws3001 WECHAT - ws3001 -
)";

const char* kSubGroups = R"(# 服务 群号 讨论组名 成员列表
QQ 1001 课设讨论组 10001,10002,10003
)";

void writeFile(const std::string& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// 新建一个只属于本测试的数据目录，并写入基线数据
std::string freshDir(const std::string& name) {
    std::string dir = kRoot + name + "\\";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    writeFile(dir + "users.txt", kUsers);
    writeFile(dir + "accounts.txt", kAccounts);
    writeFile(dir + "friends.txt", kFriends);
    writeFile(dir + "groups.txt", kGroups);
    writeFile(dir + "subgroups.txt", kSubGroups);
    return dir;
}

std::unique_ptr<PlatformYD> openPlatform(const std::string& dir, std::string* output = nullptr) {
    std::unique_ptr<PlatformYD> platform;
    std::string text = capture([&] { platform = std::make_unique<PlatformYD>(dir); });
    if (output) *output = text;
    return platform;
}

void closePlatform(std::unique_ptr<PlatformYD>& platform) {
    capture([&] { platform.reset(); });
}

UserYD& user(PlatformYD& p, int id) { return *p.findUser(id); }

ServiceBaseYD& account(PlatformYD& p, ServiceTypeYD type, const std::string& id) { return *p.findAccount(type, id); }

IndependentIdServiceYD& independent(PlatformYD& p, ServiceTypeYD type, const std::string& id) {
    return dynamic_cast<IndependentIdServiceYD&>(account(p, type, id));
}

GroupYD& group(PlatformYD& p, ServiceTypeYD type, int no) { return *p.groups().find(type, no); }

std::vector<std::string> accountIds(const std::vector<ServiceBaseYD*>& list) {
    std::vector<std::string> ids;
    for (ServiceBaseYD* a : list) ids.push_back(a->id());
    return ids;
}

std::vector<std::string> userNames(const std::vector<UserYD*>& list) {
    std::vector<std::string> names;
    for (UserYD* u : list) names.push_back(u->name());
    return names;
}

// ======================== 测试用例 ========================

void testLoadBaseline() {
    std::string out;
    auto p = openPlatform(freshDir("load"), &out);
    CHECK(p->users().size() == 6);
    CHECK(p->groups().count() == 24);
    CHECK(!contains(out, "[警告]"));
    // 题目：每个微X服务都有 1001～1006 号群，同一群号在不同服务中是不同的群
    for (ServiceTypeYD t : ServiceTypeHelperYD::all()) {
        for (int no = 1001; no <= 1006; ++no) CHECK(p->groups().find(t, no) != nullptr);
    }
    CHECK(p->groups().find(ServiceTypeYD::QQ, 1001) != p->groups().find(ServiceTypeYD::WECHAT, 1001));
    // 明文密码在加载时转换为“盐$摘要”，登录校验仍然正确
    ServiceBaseYD& qq = account(*p, ServiceTypeYD::QQ, "10001");
    CHECK(PasswordYD::isRecord(qq.password()));
    CHECK(qq.checkPassword("123456"));
    CHECK(!qq.checkPassword("1234567"));
    closePlatform(p);
}

void testSharedNumber() {
    auto p = openPlatform(freshDir("shared"));
    ServiceBaseYD& qq = account(*p, ServiceTypeYD::QQ, "10001");
    ServiceBaseYD& weibo = account(*p, ServiceTypeYD::WEIBO, "10001");
    CHECK(&qq != &weibo);                                   // 共享号码，但是两个独立的账号
    CHECK(qq.linkedQQ() == "10001" && weibo.linkedQQ() == "10001");
    CHECK(PlatformYD::isLinked(qq, weibo));
    std::string msg;
    CHECK(!p->canRegisterWith(ServiceTypeYD::WEIBO, msg));   // 微博与 QQ 共享号码，必须先有 QQ
    ServiceBaseYD* newQQ = p->registerUser("测试", ServiceTypeYD::QQ, "小测", DateYD(2005, 1, 1), "西安", "abcdef", msg);
    CHECK(newQQ != nullptr && newQQ->id() == "10006");
    UserYD* tester = p->ownerOf(newQQ);
    ServiceBaseYD* newWeibo = p->openService(*tester, ServiceTypeYD::WEIBO, "小测微博", DateYD(2005, 1, 1), "西安", "abcdef", msg);
    CHECK(newWeibo != nullptr && newWeibo->id() == "10006");
    CHECK(p->openService(*tester, ServiceTypeYD::QQ, "x", DateYD(2005, 1, 1), "x", "abcdef", msg) == nullptr);   // 重复开通
    ServiceBaseYD* wx = p->openService(*tester, ServiceTypeYD::WECHAT, "小测微信", DateYD(2005, 1, 1), "西安", "abcdef", msg);
    CHECK(wx != nullptr && wx->id() == "wx2007");
    closePlatform(p);
}

void testBinding() {
    auto p = openPlatform(freshDir("binding"));
    std::string msg;
    IndependentIdServiceYD& wx2005 = independent(*p, ServiceTypeYD::WECHAT, "wx2005");
    IndependentIdServiceYD& ws3002 = independent(*p, ServiceTypeYD::WEISHANG, "ws3002");
    IndependentIdServiceYD& ws3001 = independent(*p, ServiceTypeYD::WEISHANG, "ws3001");
    CHECK(!p->bindQQ(user(*p, 1), wx2005, msg));     // 不能替别人的账号绑定
    CHECK(!p->bindQQ(user(*p, 6), ws3001, msg));     // 周八没有开通 QQ，无法绑定
    CHECK(!ws3001.isBound());
    CHECK(p->bindQQ(user(*p, 5), wx2005, msg) && wx2005.boundQQ() == "10005");
    CHECK(p->bindQQ(user(*p, 5), ws3002, msg) && ws3002.boundQQ() == "10005");   // 微商同样可以绑定
    CHECK(!p->bindQQ(user(*p, 5), wx2005, msg));     // 已经绑定过
    CHECK(PlatformYD::isLinked(wx2005, ws3002));     // 两个独立号码服务绑定同一个 QQ，也互相对应
    CHECK(p->unbindQQ(ws3002, msg) && !ws3002.isBound());
    CHECK(!PlatformYD::isLinked(wx2005, ws3002));
    // 关闭 QQ 时，本人所有绑定该 QQ 号的服务自动解绑
    CHECK(p->bindQQ(user(*p, 5), ws3002, msg));
    CHECK(p->closeService(user(*p, 5), ServiceTypeYD::QQ, msg));
    CHECK(!wx2005.isBound() && !ws3002.isBound());
    closePlatform(p);
}

void testCommonAndRecommend() {
    auto p = openPlatform(freshDir("friends"));
    std::string msg;
    std::vector<UserYD*> common;
    std::vector<ServiceBaseYD*> recommended;
    std::vector<std::string> skipped;
    // 张三的 QQ/微信共同好友：只有李四
    CHECK(p->commonFriends(user(*p, 1), ServiceTypeYD::QQ, ServiceTypeYD::WECHAT, common, skipped, msg));
    CHECK(userNames(common) == std::vector<std::string>{ "李四" });
    // 依据 QQ 给微信推荐：王五（微信已绑定）；孙七的微信未绑定 QQ，不推荐但说明原因；赵六没有微信
    CHECK(p->recommendFriends(user(*p, 1), ServiceTypeYD::WECHAT, ServiceTypeYD::QQ, recommended, skipped, msg));
    CHECK(accountIds(recommended) == std::vector<std::string>{ "wx2003" });
    CHECK(skipped.size() == 1 && contains(skipped[0], "wx2005"));
    // 反方向、换一个人：王五在微信上依据 QQ 好友推荐 → 张三的 wx2001（李四的 wx2002 已是好友）
    CHECK(p->recommendFriends(user(*p, 3), ServiceTypeYD::WECHAT, ServiceTypeYD::QQ, recommended, skipped, msg));
    CHECK(accountIds(recommended) == std::vector<std::string>{ "wx2001" });
    // 添加推荐的好友后：推荐为空，共同好友变为李四、王五
    CHECK(p->addFriend(account(*p, ServiceTypeYD::WECHAT, "wx2001"), "wx2003", "", msg));
    CHECK(p->recommendFriends(user(*p, 1), ServiceTypeYD::WECHAT, ServiceTypeYD::QQ, recommended, skipped, msg));
    CHECK(recommended.empty());
    CHECK(p->commonFriends(user(*p, 1), ServiceTypeYD::QQ, ServiceTypeYD::WECHAT, common, skipped, msg));
    CHECK(userNames(common) == (std::vector<std::string>{ "李四", "王五" }));
    // 孙七绑定微信后，就能被推荐了
    CHECK(p->bindQQ(user(*p, 5), independent(*p, ServiceTypeYD::WECHAT, "wx2005"), msg));
    CHECK(p->recommendFriends(user(*p, 1), ServiceTypeYD::WECHAT, ServiceTypeYD::QQ, recommended, skipped, msg));
    CHECK(accountIds(recommended) == std::vector<std::string>{ "wx2005" } && skipped.empty());
    // 微博 → QQ：赵六在微博上加了王五，QQ 上就推荐王五的 QQ（共享号码，天然对应）
    CHECK(p->addFriend(account(*p, ServiceTypeYD::WEIBO, "10004"), "10003", "", msg));
    CHECK(p->recommendFriends(user(*p, 4), ServiceTypeYD::QQ, ServiceTypeYD::WEIBO, recommended, skipped, msg));
    CHECK(accountIds(recommended) == std::vector<std::string>{ "10003" });
    closePlatform(p);
}

void testOwnBindingRequired() {
    auto p = openPlatform(freshDir("ownlink"));
    std::string msg;
    std::vector<UserYD*> common;
    std::vector<ServiceBaseYD*> recommended;
    std::vector<std::string> skipped;
    CHECK(p->unbindQQ(independent(*p, ServiceTypeYD::WECHAT, "wx2001"), msg));
    // 本人的微信没有绑定 QQ：明确拒绝并说明原因，而不是返回一个看似正常的空列表
    CHECK(!p->recommendFriends(user(*p, 1), ServiceTypeYD::WECHAT, ServiceTypeYD::QQ, recommended, skipped, msg));
    CHECK(contains(msg, "未绑定QQ"));
    CHECK(!p->commonFriends(user(*p, 1), ServiceTypeYD::QQ, ServiceTypeYD::WECHAT, common, skipped, msg));
    // QQ 与微博共享号码，不需要绑定
    CHECK(p->commonFriends(user(*p, 1), ServiceTypeYD::QQ, ServiceTypeYD::WEIBO, common, skipped, msg));
    CHECK(userNames(common) == std::vector<std::string>{ "赵六" });
    closePlatform(p);
}

void testFriendBasics() {
    auto p = openPlatform(freshDir("friendbasic"));
    std::string msg;
    ServiceBaseYD& me = account(*p, ServiceTypeYD::QQ, "10003");
    CHECK(!p->addFriend(me, "10003", "", msg));           // 不能加自己
    CHECK(!p->addFriend(me, "10001", "", msg));           // 已经是好友
    CHECK(!p->addFriend(me, "wx2001", "", msg));          // 不能跨服务直接加
    CHECK(p->addFriend(me, "10005", "", msg));
    CHECK(account(*p, ServiceTypeYD::QQ, "10005").friends().contains("10003"));   // 双向
    CHECK(me.friends().modifyRemark("10005", "七哥"));
    CHECK(account(*p, ServiceTypeYD::QQ, "10005").friends().find("10003")->remark() == "王老五");   // 只改自己这一端
    CHECK(p->searchFriends(me, "七").size() == 1);       // 昵称“孙小七”、备注“七哥”都属于同一位好友
    CHECK(p->searchFriends(me, "张").size() == 1);       // 按备注“张三”找到 10001
    CHECK(p->searchFriends(me, "不存在").empty());
    closePlatform(p);
}

void testQQGroupRules() {
    auto p = openPlatform(freshDir("qqgroup"));
    std::string msg;
    GroupYD& g = group(*p, ServiceTypeYD::QQ, 1003);   // 群主 10003，成员 10004
    CHECK(g.applyJoin("10001", msg) && g.pending().size() == 1 && g.members().size() == 2);
    CHECK(!g.applyJoin("10001", msg));                  // 重复申请
    CHECK(!g.approve("10004", "10001", true, msg));     // 普通成员不能审批
    CHECK(g.setAdmin("10003", "10004", true, msg));
    CHECK(g.approve("10004", "10001", true, msg) && g.isMember("10001") && g.pending().empty());
    CHECK(!g.kick("10004", "10003", msg));              // 管理员不能踢群主
    CHECK(g.kick("10004", "10001", msg) && !g.isMember("10001"));   // 管理员可以踢普通成员
    CHECK(g.applyJoin("10002", msg) && g.approve("10003", "10002", true, msg));
    CHECK(g.setAdmin("10003", "10002", true, msg));
    CHECK(!g.kick("10004", "10002", msg));              // 管理员不能踢管理员
    CHECK(g.kick("10003", "10002", msg) && !g.isAdmin("10002"));    // 群主可以踢管理员
    CHECK(!g.quit("10003", msg));                       // 群主不能直接退群
    CHECK(g.transferOwner("10003", "10004", msg) && g.isOwner("10004") && !g.isAdmin("10004"));
    CHECK(g.quit("10003", msg) && !g.isMember("10003"));
    closePlatform(p);
}

void testWeChatGroupRules() {
    auto p = openPlatform(freshDir("wxgroup"));
    std::string msg;
    GroupYD& g = group(*p, ServiceTypeYD::WECHAT, 1002);   // 群主 wx2002，成员 wx2001、wx2003
    CHECK(!g.applyJoin("wx2006", msg) && contains(msg, "只能由群成员推荐"));
    CHECK(g.invite("wx2001", "wx2006", msg) && g.isMember("wx2006"));   // 普通成员推荐好友入群
    CHECK(!g.invite("wx2005", "wx2005", msg));                          // 非成员不能推荐
    CHECK(!g.kick("wx2001", "wx2003", msg));                            // 只有群主能踢人
    CHECK(g.kick("wx2002", "wx2003", msg));
    CHECK(!g.setAdmin("wx2002", "wx2001", true, msg));                  // 没有管理员制度
    CHECK(!g.createSubGroup("wx2002", "小组", { "wx2001" }, msg));      // 不允许子群
    closePlatform(p);
}

void testModeSwitchKeepsData() {
    auto p = openPlatform(freshDir("switch"));
    std::string msg;
    GroupYD& g = group(*p, ServiceTypeYD::QQ, 1001);   // 群主 10001，管理员 10002，讨论组 1 个，申请 1 条
    const std::string before = g.dataSnapshot();
    CHECK(!g.switchMode("10002", GroupModeYD::WECHAT, msg));   // 只有群主能切换
    CHECK(g.switchMode("10001", GroupModeYD::WECHAT, msg));
    CHECK(g.dataSnapshot() == before);                          // 只换规则，数据完全不变
    CHECK(g.isAdmin("10002"));                                  // 管理员记录保留……
    CHECK(!g.kick("10002", "10004", msg));                      // ……但在微信模式下没有权限
    CHECK(!g.approve("10002", "10005", true, msg));
    CHECK(!g.addToSubGroup("10001", "课设讨论组", "10004", msg));   // 讨论组冻结
    CHECK(!g.switchMode("10001", GroupModeYD::WECHAT, msg));    // 重复切换同一模式被拒，不重置数据
    CHECK(g.switchMode("10001", GroupModeYD::QQ, msg));
    CHECK(g.dataSnapshot() == before);                          // 往返后与起点完全一致
    CHECK(g.kick("10002", "10004", msg));                       // 管理员权限恢复
    closePlatform(p);
}

void testSubGroups() {
    auto p = openPlatform(freshDir("subgroup"));
    std::string msg;
    GroupYD& g = group(*p, ServiceTypeYD::QQ, 1001);
    CHECK(g.createSubGroup("10004", "篮球小组", { "10003", "10005" }, msg));   // 10005 不是本群成员
    CHECK(contains(msg, "10005") && contains(msg, "已忽略"));
    CHECK(g.subGroups().size() == 2 && g.subGroups()[1].members().size() == 2);
    CHECK(!g.createSubGroup("10004", "篮球小组", {}, msg));                     // 重名
    // 微信模式下群主踢人：讨论组虽冻结，成员也同步移除（讨论组成员必须是群成员）
    CHECK(g.switchMode("10001", GroupModeYD::WECHAT, msg));
    CHECK(g.kick("10001", "10003", msg));
    for (const SubGroupYD& s : g.subGroups()) CHECK(!UtilYD::contains(s.members(), "10003"));
    // 成员全部离开的讨论组自动删除
    CHECK(g.kick("10001", "10004", msg));
    CHECK(g.subGroups().size() == 1 && g.subGroups()[0].name() == "课设讨论组");
    CHECK(g.switchMode("10001", GroupModeYD::QQ, msg));
    CHECK(g.dissolveSubGroup("10001", "课设讨论组", msg) && g.subGroups().empty());
    closePlatform(p);
}

void testCloseServiceCascade() {
    auto p = openPlatform(freshDir("cascade"));
    std::string msg;
    CHECK(p->closeService(user(*p, 2), ServiceTypeYD::QQ, msg));   // 李四关闭 QQ
    CHECK(p->findAccount(ServiceTypeYD::QQ, "10002") == nullptr);
    CHECK(!account(*p, ServiceTypeYD::QQ, "10001").friends().contains("10002"));
    GroupYD& g1001 = group(*p, ServiceTypeYD::QQ, 1001);
    CHECK(!g1001.isMember("10002") && g1001.admins().empty());
    CHECK(!UtilYD::contains(g1001.subGroups()[0].members(), "10002"));
    CHECK(group(*p, ServiceTypeYD::QQ, 1002).ownerId() == "10001");   // 群主由最早入群的成员接任
    CHECK(!independent(*p, ServiceTypeYD::WECHAT, "wx2002").isBound());
    // 注销张三：QQ:1006 由管理员 10003 接任；微博 1004 群只有他一人 → 变成无主群，第一个申请者认领
    p->deleteUser(1, msg);
    CHECK(p->findUser(1) == nullptr && p->findAccount(ServiceTypeYD::WECHAT, "wx2001") == nullptr);
    GroupYD& weibo1004 = group(*p, ServiceTypeYD::WEIBO, 1004);
    CHECK(!weibo1004.hasOwner() && weibo1004.members().empty());
    CHECK(weibo1004.applyJoin("10004", msg) && weibo1004.isOwner("10004"));
    CHECK(group(*p, ServiceTypeYD::QQ, 1006).ownerId() == "10003");   // 1006 的管理员 10003 接任
    closePlatform(p);
}

void testPersistenceRoundTrip() {
    std::string dir = freshDir("roundtrip");
    std::string msg;
    {
        auto p = openPlatform(dir);
        CHECK(p->addFriend(account(*p, ServiceTypeYD::WECHAT, "wx2001"), "wx2003", "老五 同学", msg));
        CHECK(group(*p, ServiceTypeYD::QQ, 1001).switchMode("10001", GroupModeYD::WECHAT, msg));
        CHECK(p->bindQQ(user(*p, 5), independent(*p, ServiceTypeYD::WEISHANG, "ws3002"), msg));
        account(*p, ServiceTypeYD::QQ, "10004").setNickname("新昵称");
        closePlatform(p);   // 析构时写回文件
    }
    std::string out;
    auto p = openPlatform(dir, &out);
    CHECK(!contains(out, "[警告]"));
    CHECK(account(*p, ServiceTypeYD::WECHAT, "wx2001").friends().contains("wx2003"));
    CHECK(account(*p, ServiceTypeYD::WECHAT, "wx2001").friends().find("wx2003")->remark() == "老五 同学" ||
          account(*p, ServiceTypeYD::WECHAT, "wx2001").friends().find("wx2003")->remark() == "老五_同学");
    GroupYD& g = group(*p, ServiceTypeYD::QQ, 1001);
    CHECK(g.policy().mode() == GroupModeYD::WECHAT && g.isAdmin("10002") && g.subGroups().size() == 1);
    CHECK(independent(*p, ServiceTypeYD::WEISHANG, "ws3002").boundQQ() == "10005");
    CHECK(account(*p, ServiceTypeYD::QQ, "10004").nickname() == "新昵称");
    CHECK(!fs::exists(dir + "users.txt.tmp") && !fs::exists(dir + "groups.txt.tmp"));   // 不留下临时文件
    closePlatform(p);
}

void testRepairOnLoad() {
    std::string dir = freshDir("repair");
    std::string accounts = kAccounts;
    accounts += "WEIBO 10002 5 七的微博 2004-09-30 2020-01-01 杭州 123456 -\n";   // 号码 10002 属于李四
    accounts += "WEIBO 10009 2 李四微博 2003-11-02 2020-01-01 上海 123456 -\n";   // 与李四的 QQ 号不同
    accounts.replace(accounts.find("123456 10003"), 12, "123456 10001");           // 王五的微信绑定到张三的 QQ
    writeFile(dir + "accounts.txt", accounts);
    writeFile(dir + "friends.txt", std::string(kFriends) + "QQ 10004 10004 自己\nQQ 10003 10005 单向\n");
    std::string groups = kGroups;
    groups.replace(groups.find("10003 QQ - 10003,10004 -"), 24, "99999 QQ - 99999,10003,10004,88888 -");
    writeFile(dir + "groups.txt", groups);
    writeFile(dir + "subgroups.txt", std::string(kSubGroups) + "QQ 1001 课设讨论组 10004\nQQ 1002 空组 99999\n");
    std::string out;
    auto p = openPlatform(dir, &out);
    CHECK(contains(out, "有 5 行") && contains(out, "4 处"));
    CHECK(p->findAccount(ServiceTypeYD::WEIBO, "10002") == nullptr && p->findAccount(ServiceTypeYD::WEIBO, "10009") == nullptr);
    CHECK(!independent(*p, ServiceTypeYD::WECHAT, "wx2003").isBound());
    CHECK(!account(*p, ServiceTypeYD::QQ, "10004").friends().contains("10004"));
    // 与微信相同，好友关系允许单向（一方删除了另一方），加载时保留原样，不再补成双向
    CHECK(account(*p, ServiceTypeYD::QQ, "10003").friends().contains("10005"));
    CHECK(!account(*p, ServiceTypeYD::QQ, "10005").friends().contains("10003"));
    GroupYD& g = group(*p, ServiceTypeYD::QQ, 1003);
    CHECK(g.ownerId() == "10003" && g.members().size() == 2);                   // 不存在的群主被替换
    CHECK(group(*p, ServiceTypeYD::QQ, 1001).subGroups().size() == 1);
    CHECK(group(*p, ServiceTypeYD::QQ, 1002).subGroups().empty());
    bool backedUp = false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_directory() && contains(entry.path().filename().string(), "backup_"))
            backedUp = fs::exists(entry.path() / "accounts.txt");
    }
    CHECK(backedUp);
    closePlatform(p);
}

void testConflictKeepsDataConsistent() {
    std::string dir = freshDir("conflict");
    std::string msg;
    auto a = openPlatform(dir);
    auto b = openPlatform(dir);
    CHECK(a->addFriend(account(*a, ServiceTypeYD::QQ, "10004"), "10005", "", msg));
    CHECK(a->save());
    // 窗口 b 同时关闭了一项服务（会改动 accounts、friends、groups 三个文件）
    CHECK(b->closeService(user(*b, 2), ServiceTypeYD::QQ, msg));
    std::string out = capture([&] { CHECK(b->save()); });
    CHECK(contains(out, "conflict_"));
    // data 目录仍是窗口 a 的完整版本，没有混入 b 的一部分文件
    CHECK(contains(readFile(dir + "accounts.txt"), "QQ 10002 "));
    CHECK(contains(readFile(dir + "friends.txt"), "QQ 10004 10005"));
    bool fullCopy = false;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_directory() && contains(entry.path().filename().string(), "conflict_")) {
            fullCopy = fs::exists(entry.path() / "users.txt") && fs::exists(entry.path() / "subgroups.txt") &&
                       !contains(readFile((entry.path() / "accounts.txt").string()), "QQ 10002 ");
        }
    }
    CHECK(fullCopy);
    closePlatform(a);
    closePlatform(b);
}

void testUnchangedNotWritten() {
    std::string dir = freshDir("unchanged");
    {
        auto p = openPlatform(dir);
        closePlatform(p);   // 第一次保存：明文密码转为加密记录等，会写文件
    }
    auto stampBefore = fs::last_write_time(dir + "groups.txt");
    auto p = openPlatform(dir);
    closePlatform(p);   // 什么都没改：不写任何文件
    CHECK(fs::last_write_time(dir + "groups.txt") == stampBefore);
}

void testUtf8FileAccepted() {
    std::string dir = freshDir("utf8");
    // users.txt 被另存为带 BOM 的 UTF-8：“张三” 的 UTF-8 编码是 E5 BC A0 E4 B8 89
    writeFile(dir + "users.txt", "\xEF\xBB\xBF# id name\n1 \xE5\xBC\xA0\xE4\xB8\x89\n2 L4\n3 W5\n4 Z6\n5 S7\n6 Z8\n");
    std::string out;
    auto p = openPlatform(dir, &out);
    CHECK(contains(out, "UTF-8"));
    CHECK(user(*p, 1).name() == "张三");   // 源文件按 GBK 编译，这里比较的是 GBK 字节
    closePlatform(p);
    std::string saved = readFile(dir + "users.txt");
    CHECK(contains(saved, "1 张三") && saved.compare(0, 3, "\xEF\xBB\xBF") != 0);   // 写回 GBK，去掉 BOM
}

void testMissingDataDir() {
    std::string dir = kRoot + "missing\\sub\\";
    std::error_code ec;
    fs::remove_all(kRoot + "missing", ec);
    std::string msg;
    auto p = openPlatform(dir);
    CHECK(p->registerUser("新人", ServiceTypeYD::QQ, "新人", DateYD(2006, 6, 6), "长沙", "abcdef", msg) != nullptr);
    CHECK(p->save());                                       // data 目录不存在时自动创建
    CHECK(fs::exists(dir + "accounts.txt"));
    closePlatform(p);
}

void testDateAndPassword() {
    DateYD d;
    CHECK(DateYD::parse("2024-02-29", d));
    CHECK(!DateYD::parse("2023-02-29", d));
    CHECK(!DateYD::parse("1900-02-29", d));
    CHECK(DateYD::parse("2000-02-29", d));
    CHECK(!DateYD::parse("2026-13-01", d));
    CHECK(!DateYD::parse("2026-01-01abc", d));
    CHECK(!DateYD::parse("", d));
    CHECK(DateYD(2004, 3, 15).yearsUntil(DateYD(2026, 3, 14)) == 21);
    CHECK(DateYD(2004, 3, 15).yearsUntil(DateYD(2026, 3, 15)) == 22);
    std::string r1 = PasswordYD::makeRecord("123456");
    std::string r2 = PasswordYD::makeRecord("123456");
    CHECK(r1 != r2);                                        // 每次随机盐不同
    CHECK(PasswordYD::verify("123456", r1) && PasswordYD::verify("123456", r2));
    CHECK(!PasswordYD::verify("123457", r1));
}

// ======================== 功能扩展：群聊天、私信、群公告、禁言、新建/解散群 ========================

const ServiceTypeYD kQQ = ServiceTypeYD::QQ;
const ServiceTypeYD kWeChat = ServiceTypeYD::WECHAT;

void testGroupMessages() {
    std::string dir = freshDir("groupmsg");
    auto p = openPlatform(dir);
    std::string msg;
    GroupYD& g = group(*p, kQQ, 1001);   // 群主 10001，管理员 10002，成员 10003、10004；10005 只是申请人
    ServiceBaseYD& zhang = account(*p, kQQ, "10001");
    ServiceBaseYD& li = account(*p, kQQ, "10002");
    ServiceBaseYD& wang = account(*p, kQQ, "10003");
    CHECK(p->sendGroupMessage(zhang, g, "大家好 今晚开会", msg));
    CHECK(p->sendGroupMessage(li, g, "收到", msg));
    CHECK(!p->sendGroupMessage(account(*p, kQQ, "10005"), g, "我也想说", msg));   // 不是群成员
    CHECK(!p->sendGroupMessage(wang, g, "   ", msg));                             // 空消息
    CHECK(!p->sendGroupMessage(account(*p, kWeChat, "wx2001"), g, "串门", msg));   // 其他服务的账号
    std::vector<MessageYD> history = p->messages().groupHistory(kQQ, 1001);
    CHECK(history.size() == 2 && history[0].text() == "大家好 今晚开会" && history[1].sender() == "10002");
    CHECK(p->messages().groupHistory(kWeChat, 1001).empty());   // 群号相同、服务不同的群互不相干
    // 未读：王五 2 条；李四只有 1 条（自己发的不算）；没有消息的群不出现
    std::map<int, int> unread = p->messages().unreadGroups(kQQ, "10003", { 1001, 1006 });
    CHECK(unread.size() == 1 && unread[1001] == 2);
    CHECK(p->messages().unreadGroups(kQQ, "10002", { 1001 })[1001] == 1);
    p->messages().markGroupRead(kQQ, 1001, "10003", history.back().seq());
    CHECK(p->messages().unreadGroups(kQQ, "10003", { 1001 }).empty());
    // 另一个程序窗口（另一个 MessageCenterYD 对象写同一个文件）发来的消息，这边马上就能看到
    MessageCenterYD otherWindow(dir + "messages.txt");
    CHECK(otherWindow.postGroup(kQQ, 1001, "10004", "我在另一个窗口"));
    CHECK(p->messages().groupHistory(kQQ, 1001).size() == 3);
    int privateCount = 0, groupCount = 0;
    p->unreadCount(wang, privateCount, groupCount);
    CHECK(privateCount == 0 && groupCount == 1);
    // 图形界面的会话列表：一次取出与我有关的私信和指定群的群消息，别人之间的私信、其他群的消息不包含在内
    CHECK(p->sendPrivate(zhang, "10003", "私聊王五", msg) && p->sendPrivate(li, "10001", "私聊张三", msg));
    std::vector<MessageYD> mine = p->messages().messagesFor(kQQ, "10003", { 1001 });
    CHECK(mine.size() == 4 && mine.back().text() == "私聊王五");
    CHECK(p->messages().messagesFor(kQQ, "10003", {}).size() == 1);
    closePlatform(p);
}

void testPrivateMessages() {
    std::string dir = freshDir("private");
    auto p = openPlatform(dir);
    std::string msg;
    ServiceBaseYD& zhang = account(*p, kQQ, "10001");
    CHECK(p->sendPrivate(zhang, "10002", "在吗？晚上 一起自习", msg));
    CHECK(p->sendPrivate(zhang, "10002", "第二条", msg));
    CHECK(p->sendPrivate(account(*p, kQQ, "10002"), "10001", "好的", msg));
    CHECK(!p->sendPrivate(account(*p, kQQ, "10004"), "10005", "你好", msg));   // 不是好友
    CHECK(!p->sendPrivate(zhang, "10002", "", msg));
    CHECK(!p->sendPrivate(zhang, "10002", std::string(600, 'a'), msg));        // 太长
    std::vector<MessageYD> chat = p->messages().conversation(kQQ, "10002", "10001");
    CHECK(chat.size() == 3 && chat[0].text() == "在吗？晚上 一起自习" && chat[2].sender() == "10002");
    CHECK(p->messages().conversation(kWeChat, "10001", "10002").empty());
    std::map<std::string, int> unread = p->messages().unreadPrivate(kQQ, "10002");
    CHECK(unread.size() == 1 && unread["10001"] == 2);
    p->messages().markPrivateRead(kQQ, "10002", "10001", chat.back().seq());
    CHECK(p->messages().unreadPrivate(kQQ, "10002").empty());
    CHECK(p->messages().unreadPrivate(kQQ, "10001")["10002"] == 1);   // 张三那边还有 1 条未读
    closePlatform(p);
    p = openPlatform(dir);   // 重新启动后，消息和已读状态都还在
    CHECK(p->messages().conversation(kQQ, "10001", "10002").size() == 3);
    CHECK(p->messages().unreadPrivate(kQQ, "10002").empty());
    // 关闭 QQ 后与之相关的私信作废：号码以后被重新分配时，新主人看不到
    CHECK(p->closeService(user(*p, 2), kQQ, msg));
    CHECK(p->messages().conversation(kQQ, "10001", "10002").empty());
    CHECK(p->messages().unreadPrivate(kQQ, "10001").empty());
    closePlatform(p);
}

void testMuteAndNotice() {
    auto p = openPlatform(freshDir("mute"));
    std::string msg;
    GroupYD& g = group(*p, kQQ, 1001);   // 群主 10001，管理员 10002，普通成员 10003、10004
    CHECK(!g.setMuted("10003", "10004", true, msg));                        // 普通成员不能禁言
    CHECK(!g.setMuted("10002", "10001", true, msg));                        // 管理员不能禁言群主
    CHECK(g.setMuted("10002", "10004", true, msg) && g.isMuted("10004"));   // 管理员可以禁言普通成员
    CHECK(!g.setMuted("10002", "10004", true, msg));                        // 重复禁言
    CHECK(!g.setMuted("10001", "10005", true, msg));                        // 不是群成员
    CHECK(!p->sendGroupMessage(account(*p, kQQ, "10004"), g, "说话", msg) && contains(msg, "禁言"));
    CHECK(!g.setNotice("10003", "普通成员发公告", msg));
    CHECK(g.setNotice("10002", "周五 交 课设报告", msg) && g.notice() == "周五 交 课设报告");   // QQ 群管理员可以发公告
    CHECK(g.rename("10002", "编程 交流群", msg) && g.name() == "编程_交流群");
    // 切换为微信群模式：禁言名单、公告冻结保留，但禁言不再生效；群资料只有群主能改
    const std::string before = g.dataSnapshot();
    CHECK(g.switchMode("10001", GroupModeYD::WECHAT, msg));
    CHECK(g.dataSnapshot() == before && g.isMuted("10004") && g.canSpeak("10004"));
    CHECK(p->sendGroupMessage(account(*p, kQQ, "10004"), g, "终于能说话了", msg));
    CHECK(!g.setMuted("10001", "10003", true, msg));   // 微信群没有禁言制度
    CHECK(!g.setNotice("10002", "管理员改公告", msg));
    CHECK(g.setNotice("10001", "群主改公告", msg));
    CHECK(g.switchMode("10001", GroupModeYD::QQ, msg));
    CHECK(!g.canSpeak("10004"));                        // 切回 QQ 群模式，禁言重新生效
    // 群主转让给被禁言的人：自动解除禁言；被禁言者被踢出：禁言记录一并清除
    CHECK(g.transferOwner("10001", "10004", msg) && !g.isMuted("10004") && g.canSpeak("10004"));
    CHECK(g.setMuted("10004", "10003", true, msg) && g.kick("10004", "10003", msg) && g.muted().empty());
    closePlatform(p);
}

void testGroupInfoPersistence() {
    std::string dir = freshDir("groupinfo");
    std::string msg;
    {
        auto p = openPlatform(dir);
        GroupYD& g = group(*p, kQQ, 1001);
        CHECK(g.setNotice("10001", "周五 下午 3 点开会", msg));
        CHECK(g.setMuted("10001", "10004", true, msg));
        closePlatform(p);
    }
    std::string out;
    auto p = openPlatform(dir, &out);
    CHECK(!contains(out, "[警告]"));
    CHECK(group(*p, kQQ, 1001).notice() == "周五 下午 3 点开会");   // 公告在行尾，空格原样保留
    CHECK(group(*p, kQQ, 1001).muted() == std::vector<std::string>{ "10004" });
    CHECK(group(*p, kQQ, 1002).notice().empty() && group(*p, kQQ, 1002).muted().empty());
    closePlatform(p);
    // 手工改坏：禁言名单里有群主自己和不存在的账号 → 加载时去掉并提示
    std::string groups = readFile(dir + "groups.txt");
    groups.replace(groups.find("10005 10004 周五"), 16, "10005 10004,10001,77777 周五");
    writeFile(dir + "groups.txt", groups);
    p = openPlatform(dir, &out);
    CHECK(contains(out, "2 处"));
    CHECK(group(*p, kQQ, 1001).muted() == std::vector<std::string>{ "10004" });
    closePlatform(p);
}

void testCreateAndDissolveGroup() {
    std::string dir = freshDir("newgroup");
    std::string msg;
    {
        auto p = openPlatform(dir);
        ServiceBaseYD& wang = account(*p, kQQ, "10003");
        GroupYD* g = p->createGroup(wang, "羽毛球 约球", msg);
        CHECK(g && g->groupNo() == 1007 && g->isOwner("10003") && g->name() == "羽毛球_约球");
        CHECK(g && g->policy().mode() == GroupModeYD::QQ);
        GroupYD* wx = p->createGroup(account(*p, kWeChat, "wx2001"), "家长群", msg);
        CHECK(wx && wx->groupNo() == 1007 && wx->policy().mode() == GroupModeYD::WECHAT);   // 各服务分别编号，按服务的默认模式
        CHECK(p->createGroup(wang, "  ", msg) == nullptr);
        CHECK(g->invite("10003", "10001", msg));
        CHECK(p->sendGroupMessage(wang, *g, "周六打球", msg));
        CHECK(!p->dissolveGroup(account(*p, kQQ, "10001"), 1007, msg));                            // 不是群主
        CHECK(!p->dissolveGroup(account(*p, kQQ, "10001"), 1001, msg) && contains(msg, "预置群"));   // 预置群不能解散
        closePlatform(p);
    }
    auto p = openPlatform(dir);   // 新建的群保存后重新加载仍然存在
    GroupYD* g = p->groups().find(kQQ, 1007);
    CHECK(g && g->members().size() == 2 && g->name() == "羽毛球_约球");
    CHECK(p->dissolveGroup(account(*p, kQQ, "10003"), 1007, msg));
    CHECK(p->groups().find(kQQ, 1007) == nullptr && p->groups().find(kWeChat, 1007) != nullptr);
    // 群号 1007 再分配给新群时，看不到旧群的聊天记录
    GroupYD* again = p->createGroup(account(*p, kQQ, "10004"), "新群", msg);
    CHECK(again && again->groupNo() == 1007);
    std::vector<MessageYD> fresh = p->messages().groupHistory(kQQ, 1007);   // 只有新群自己的“创建了群聊”提示
    CHECK(fresh.size() == 1 && fresh[0].isSystem() && contains(fresh[0].text(), "创建了群聊"));
    closePlatform(p);
}

// ======================== 微信功能：好友验证、单向删除、黑名单、撤回、群系统提示、@、朋友圈 ========================

void testFriendRequests() {
    std::string dir = freshDir("request");
    auto p = openPlatform(dir);
    std::string msg;
    ServiceBaseYD& wang = account(*p, kQQ, "10003");
    ServiceBaseYD& zhao = account(*p, kQQ, "10004");   // 王五和赵六还不是好友
    CHECK(p->requestFriend(wang, "10004", "我是王五", "六哥", msg) && contains(msg, "等待"));
    CHECK(!wang.friends().contains("10004") && !zhao.friends().contains("10003"));   // 对方同意前还不是好友
    CHECK(p->pendingRequestCount(zhao) == 1);
    std::vector<FriendRequestYD> requests = p->friendRequests(zhao);
    CHECK(requests.size() == 1 && requests[0].from == "10003" && requests[0].greeting == "我是王五" &&
          requests[0].status == FriendRequestYD::PENDING);
    CHECK(p->friendRequests(wang).size() == 1);   // 自己发出的申请也能看到
    CHECK(p->respondRequest(zhao, requests[0].seq, true, "老五", msg));
    CHECK(wang.friends().find("10004")->remark() == "六哥" && zhao.friends().find("10003")->remark() == "老五");
    CHECK(p->pendingRequestCount(zhao) == 0 && !p->respondRequest(zhao, requests[0].seq, true, "", msg));   // 不能重复处理
    CHECK(!p->requestFriend(wang, "10004", "", "", msg) && contains(msg, "已经是你的好友"));
    // 拒绝
    CHECK(p->requestFriend(account(*p, kQQ, "10005"), "10003", "", "", msg));
    for (const FriendRequestYD& r : p->friendRequests(wang)) {
        if (r.from == "10005") CHECK(p->respondRequest(wang, r.seq, false, "", msg));
    }
    CHECK(!wang.friends().contains("10005") && p->pendingRequestCount(wang) == 0);
    // 对方关闭了好友验证：直接成为好友
    p->setVerification(account(*p, kQQ, "10002"), false);
    CHECK(!p->requiresVerification(account(*p, kQQ, "10002")));
    CHECK(p->requestFriend(zhao, "10002", "", "", msg) && contains(msg, "直接"));
    CHECK(zhao.friends().contains("10002") && account(*p, kQQ, "10002").friends().contains("10004"));
    closePlatform(p);
    p = openPlatform(dir);   // 重新启动：好友关系和验证设置都还在
    CHECK(account(*p, kQQ, "10003").friends().contains("10004"));
    CHECK(!p->requiresVerification(account(*p, kQQ, "10002")));
    closePlatform(p);
}

void testOneWayDeleteAndBlacklist() {
    std::string dir = freshDir("blacklist");
    auto p = openPlatform(dir);
    auto other = openPlatform(dir);   // 同时打开的另一个程序窗口
    std::string msg;
    ServiceBaseYD& zhang = account(*p, kQQ, "10001");
    ServiceBaseYD& li = account(*p, kQQ, "10002");
    CHECK(p->removeFriend(zhang, "10002", msg));
    CHECK(!zhang.friends().contains("10002") && li.friends().contains("10001"));   // 单向删除
    CHECK(!p->sendPrivate(li, "10001", "在吗", msg) && contains(msg, "朋友验证"));
    CHECK(!p->sendPrivate(zhang, "10002", "在吗", msg));
    other->syncSocial();   // 另一个窗口同步后，同样看到删除结果
    CHECK(!account(*other, kQQ, "10001").friends().contains("10002"));
    // 重新添加：李四仍保留着张三，不需要验证，直接成为好友
    CHECK(p->requestFriend(zhang, "10002", "", "", msg) && contains(msg, "仍有你") && zhang.friends().contains("10002"));
    CHECK(p->sendPrivate(li, "10001", "又是好友了", msg));
    // 黑名单：拉黑不删除好友，但拒收对方的消息
    CHECK(p->setBlocked(zhang, "10002", true, msg) && p->isBlocked(kQQ, "10001", "10002"));
    CHECK(!p->sendPrivate(li, "10001", "在吗", msg) && contains(msg, "拒收"));
    CHECK(p->sendPrivate(zhang, "10002", "拉黑的一方仍然可以发", msg) && zhang.friends().contains("10002"));
    CHECK(p->blockedList(zhang) == std::vector<std::string>{ "10002" });
    other->syncSocial();
    CHECK(other->isBlocked(kQQ, "10001", "10002"));
    CHECK(!other->sendPrivate(account(*other, kQQ, "10002"), "10001", "另一个窗口", msg) && contains(msg, "拒收"));
    CHECK(p->setBlocked(zhang, "10002", false, msg) && p->sendPrivate(li, "10001", "解除拉黑了", msg));
    // 被拉黑的人发出的好友申请，对方看不到
    ServiceBaseYD& wang = account(*p, kQQ, "10003");
    CHECK(p->setBlocked(wang, "10004", true, msg));
    CHECK(p->requestFriend(account(*p, kQQ, "10004"), "10003", "", "", msg) && p->pendingRequestCount(wang) == 0);
    closePlatform(other);
    closePlatform(p);
}

void testRevoke() {
    std::string dir = freshDir("revoke");
    auto p = openPlatform(dir);
    std::string msg;
    ServiceBaseYD& zhang = account(*p, kQQ, "10001");
    ServiceBaseYD& li = account(*p, kQQ, "10002");
    CHECK(p->sendPrivate(zhang, "10002", "说错了", msg));
    int seq = p->messages().conversation(kQQ, "10001", "10002").back().seq();
    CHECK(!p->revokeMessage(li, seq, msg));   // 对方不能撤回
    CHECK(p->revokeMessage(zhang, seq, msg));
    MessageYD last = p->messages().conversation(kQQ, "10001", "10002").back();
    CHECK(last.revoked() && p->revokeText(zhang, last) == "你撤回了一条消息" && contains(p->revokeText(li, last), "撤回了一条消息"));
    CHECK(p->messages().unreadPrivate(kQQ, "10002").empty());   // 撤回的消息不计未读
    CHECK(!p->revokeMessage(zhang, seq, msg));                   // 不能重复撤回
    // 超过 2 分钟的消息不能撤回
    {
        std::ofstream out(dir + "messages.txt", std::ios::binary | std::ios::app);
        out << "M QQ P 10002 10001 2020-01-01 10:00:00 很久以前的消息\r\n";
    }
    seq = p->messages().conversation(kQQ, "10001", "10002").back().seq();
    CHECK(!p->revokeMessage(zhang, seq, msg) && contains(msg, "2 分钟"));
    // 群：QQ 群模式管理员可以撤回普通成员的消息；普通成员不能撤回别人的
    GroupYD& g = group(*p, kQQ, 1001);   // 群主 10001，管理员 10002，成员 10003、10004
    CHECK(p->sendGroupMessage(account(*p, kQQ, "10003"), g, "成员的消息", msg));
    seq = p->messages().groupHistory(kQQ, 1001).back().seq();
    CHECK(!p->revokeMessage(account(*p, kQQ, "10004"), seq, msg));
    CHECK(p->revokeMessage(li, seq, msg));
    CHECK(contains(p->revokeText(zhang, p->messages().groupHistory(kQQ, 1001).back()), "王老五"));
    // 微信群模式：只有群主可以撤回别人的消息（权限由群的管理策略决定）
    CHECK(g.switchMode("10001", GroupModeYD::WECHAT, msg));
    CHECK(p->sendGroupMessage(account(*p, kQQ, "10003"), g, "又一条", msg));
    seq = p->messages().groupHistory(kQQ, 1001).back().seq();
    CHECK(!p->revokeMessage(li, seq, msg) && p->revokeMessage(zhang, seq, msg));
    closePlatform(p);
}

void testGroupEventsAndMentions() {
    auto p = openPlatform(freshDir("events"));
    std::string msg;
    GroupYD& g = group(*p, kQQ, 1003);   // 群主 10003，成员 10004
    CHECK(g.invite("10003", "10001", msg) && g.rename("10003", "篮球群", msg) && g.kick("10003", "10004", msg));
    std::vector<MessageYD> history = p->messages().groupHistory(kQQ, 1003);
    CHECK(history.size() == 3 && history[0].isSystem() && history[1].isSystem() && history[2].isSystem());
    CHECK(contains(history[0].text(), "邀请") && contains(history[1].text(), "篮球群") && contains(history[2].text(), "移出"));
    CHECK(p->messages().unreadGroups(kQQ, "10001", { 1003 }).empty());   // 系统提示不计未读
    // @提醒
    GroupYD& g1 = group(*p, kQQ, 1001);   // 群主 10001，管理员 10002，成员 10003、10004
    ServiceBaseYD& li = account(*p, kQQ, "10002");
    ServiceBaseYD& zhao = account(*p, kQQ, "10004");
    CHECK(p->sendGroupMessage(account(*p, kQQ, "10003"), g1, "@李小四 帮忙看一下", msg));
    CHECK(p->unreadMentions(li, 1001) == 1 && p->unreadMentions(zhao, 1001) == 0);
    CHECK(p->sendGroupMessage(zhao, g1, "@所有人 普通成员发的", msg));
    CHECK(p->unreadMentions(account(*p, kQQ, "10003"), 1001) == 0);   // 普通成员 @所有人 无效
    CHECK(p->sendGroupMessage(account(*p, kQQ, "10001"), g1, "@所有人 明天开会", msg));
    CHECK(p->unreadMentions(zhao, 1001) == 1 && p->unreadMentions(li, 1001) == 2);
    p->messages().markGroupRead(kQQ, 1001, "10002", p->messages().groupHistory(kQQ, 1001).back().seq());
    CHECK(p->unreadMentions(li, 1001) == 0);
    closePlatform(p);
}

void testMoments() {
    auto p = openPlatform(freshDir("moments"));
    std::string msg;
    // QQ 好友：张三—李四、王五、赵六、孙七；李四—王五
    ServiceBaseYD& zhang = account(*p, kQQ, "10001");
    ServiceBaseYD& li = account(*p, kQQ, "10002");
    ServiceBaseYD& wang = account(*p, kQQ, "10003");
    ServiceBaseYD& zhao = account(*p, kQQ, "10004");
    CHECK(zhang.momentsName() == "QQ空间" && account(*p, kWeChat, "wx2001").momentsName() == "朋友圈");   // 多态
    auto find = [](const std::vector<MomentYD>& list, const std::string& text) {
        for (const MomentYD& m : list) {
            if (m.text == text) return m;
        }
        return MomentYD();
    };
    CHECK(p->postMoment(zhang, "周末去爬山", "all", msg));
    CHECK(p->postMoment(zhang, "只给李四看", "only:10002", msg));
    CHECK(p->postMoment(zhang, "不给赵六看", "except:10004", msg));
    CHECK(p->postMoment(zhang, "私密日记", "self", msg));
    CHECK(!p->postMoment(zhang, "部分可见却没选人", "only:", msg));
    CHECK(p->moments(zhang).size() == 4 && p->moments(li).size() == 3 && p->moments(wang).size() == 2 &&
          p->moments(zhao).size() == 1);
    CHECK(p->moments(zhang).front().text == "私密日记");   // 最新的在前
    CHECK(p->postMoment(zhao, "赵六的动态", "all", msg));
    CHECK(p->moments(wang, "10004").empty() && p->moments(zhang, "10004").size() == 1);   // 不是好友看不到
    // 点赞、评论：只能看到共同好友的
    const int seq = find(p->moments(zhang), "周末去爬山").seq;
    CHECK(p->toggleLike(wang, seq, msg) && p->toggleLike(zhao, seq, msg));
    CHECK(find(p->moments(zhang), "周末去爬山").likes.size() == 2);
    CHECK(find(p->moments(li), "周末去爬山").likes == std::vector<std::string>{ "10003" });   // 李四和赵六不是好友
    CHECK(p->commentMoment(zhao, seq, "", "赵六评论", msg) && p->commentMoment(li, seq, "", "李四评论", msg));
    CHECK(find(p->moments(li), "周末去爬山").comments.size() == 1 && find(p->moments(zhang), "周末去爬山").comments.size() == 2);
    CHECK(p->commentMoment(zhang, seq, "10002", "回复李四", msg));
    CHECK(find(p->moments(li), "周末去爬山").comments.back().isReply());
    CHECK(p->toggleLike(wang, seq, msg) && find(p->moments(zhang), "周末去爬山").likes.size() == 1);   // 取消赞
    CHECK(!p->toggleLike(li, find(p->moments(zhang), "私密日记").seq, msg));                         // 看不到的动态不能赞
    // 通知：张三收到 2 个赞、2 条评论；李四收到 1 条回复
    CHECK(p->momentNotices(zhang, true).size() == 4 && p->momentNotices(li, true).size() == 1);
    p->markMomentsRead(zhang);
    CHECK(p->momentNotices(zhang, true).empty() && p->momentNotices(zhang, false).size() == 4);
    // 删除评论、删除动态
    const int comment = find(p->moments(zhang), "周末去爬山").comments.front().seq;
    CHECK(!p->deleteComment(li, comment, msg) && p->deleteComment(zhang, comment, msg));   // 动态作者可以删别人的评论
    CHECK(!p->deleteMoment(li, seq, msg) && p->deleteMoment(zhang, seq, msg));
    CHECK(find(p->moments(li), "周末去爬山").seq == 0);
    // 李四单向删除张三后，双方都看不到对方的动态
    CHECK(p->removeFriend(li, "10001", msg) && p->moments(li, "10001").empty());
    closePlatform(p);
}

void run(const std::string& name, void (*test)()) {
    g_case = name;
    int failuresBefore = g_failures;
    int checksBefore = g_checks;
    test();
    std::cout << (g_failures == failuresBefore ? "  [通过] " : "  [失败] ") << name << "（" << g_checks - checksBefore
              << " 项检查）\n";
}

}  // namespace

int main() {
    SetConsoleOutputCP(936);
    std::error_code ec;
    fs::remove_all(kRoot, ec);
    std::cout << "==== IMSystemYD 自动测试 ====\n";
    run("加载基线数据、预置群、密码加密", testLoadBaseline);
    run("QQ 与微博共享号码、账号分配", testSharedNumber);
    run("微信/微商绑定 QQ 的规则", testBinding);
    run("跨服务共同好友与推荐好友", testCommonAndRecommend);
    run("本人未绑定时明确拒绝", testOwnBindingRequired);
    run("好友增删改查", testFriendBasics);
    run("QQ 群：申请、审批、管理员、踢人、转让", testQQGroupRules);
    run("微信群：只能推荐、仅群主有特权、无子群", testWeChatGroupRules);
    run("动态切换管理模式不伤害数据", testModeSwitchKeepsData);
    run("临时讨论组", testSubGroups);
    run("关闭服务与注销用户的级联处理", testCloseServiceCascade);
    run("保存后重新加载", testPersistenceRoundTrip);
    run("加载时检查并修正不一致的数据", testRepairOnLoad);
    run("两个窗口同时修改：整组另存、不混合", testConflictKeepsDataConsistent);
    run("内容没变时不写文件", testUnchangedNotWritten);
    run("UTF-8 编码的数据文件也能读", testUtf8FileAccepted);
    run("data 目录不存在时自动创建", testMissingDataDir);
    run("日期合法性与密码记录", testDateAndPassword);
    run("群消息：发送权限、记录、未读、跨窗口可见", testGroupMessages);
    run("好友私信：只能发给好友、未读、关闭服务后作废", testPrivateMessages);
    run("禁言与群公告：权限随管理模式变化", testMuteAndNotice);
    run("群公告、禁言名单的保存与加载修正", testGroupInfoPersistence);
    run("新建群与解散群", testCreateAndDissolveGroup);
    run("好友申请与验证", testFriendRequests);
    run("单向删除、黑名单、多窗口同步", testOneWayDeleteAndBlacklist);
    run("消息撤回：2 分钟内、群管理者按管理策略", testRevoke);
    run("群系统提示与 @提醒", testGroupEventsAndMentions);
    run("朋友圈：可见范围、点赞评论只看共同好友、通知", testMoments);
    std::cout << "==== 共 " << g_checks << " 项检查，失败 " << g_failures << " 项 ====\n";
    if (g_failures == 0) fs::remove_all(kRoot, ec);   // 全部通过时清理临时数据；失败时保留以便查看
    return g_failures == 0 ? 0 : 1;
}
