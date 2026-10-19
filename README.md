# IMChat

[![CI](https://github.com/Hakak1-yrs/IMChat/actions/workflows/ci.yml/badge.svg)](https://github.com/Hakak1-yrs/IMChat/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

用 C++17 从零实现的即时通信系统：自己写的 Reactor 网络库（epoll / poll）、多线程服务端、SQLite 存储、断线不丢消息的客户端库，Win32 + Direct2D 自绘的图形客户端，以及手机浏览器直接打开就能用的网页客户端。

## 亮点

- **网络库从零实现**：one loop per thread 的 Reactor，Linux 上用 epoll、Windows 上用 WSAPoll，同一套 `Poller` 接口；跨线程投递按 I/O 线程分批，同一轮发给同一连接的数据合并成一次系统调用。同一个端口同时服务原生客户端和浏览器（自己实现的 HTTP 升级握手与 WebSocket 分帧），手机和电脑可以互发消息。
- **性能有数据**：私信 **5～6.6 万条/秒**（p99 约 5 ms），100 人群消息 **2.7 万条/秒**，5,000 条并发连接下推送一条不少。每次优化都是先测量、找到热点、再修改、再测量，过程（包括测错的那次）写在 [压测报告](docs/BENCHMARK.md) 里。
- **数据越多越慢的问题逐个解决**：积累 2 万条社交记录后，私信吞吐从 178 条/秒恢复到 6.4 万条/秒；重连同步从 107 ms 降到 0.06 ms；好友修改从 129 次/秒提高到 1.5 万次/秒，仍然先写入磁盘再回复。
- **消息不丢不重**：客户端消息编号去重、断线自动重连并重发、按序号增量同步；服务端宕机期间发出的消息，恢复后恰好送达一次。
- **测试与检查**：100 多个单元和集成测试、356 项业务测试、服务端模糊测试；CI 在 Windows 和 Linux 上运行，AddressSanitizer、UBSan、ThreadSanitizer 都必须通过（TSan 在 CI 上抓到过一个真实的线程竞争，已修复）。

## 架构

```
 图形客户端 / 命令行客户端 / 压测工具                     手机或电脑的浏览器（网页客户端，服务端自带）
        │  ChatClient：自动重连、心跳、待确认队列、按序号增量同步       │  同样的重连、重发、增量同步逻辑
        │  TCP，协议 [4 字节长度][2 字节类型][JSON]                      │  WebSocket，每条消息里装一帧同样的协议帧
        ▼                                                                ▼  （同一个端口；I/O 线程拆掉 WebSocket 这层后，和原生连接走同一套代码）
           主线程                 I/O 线程 × N（每个线程一个事件循环）            业务线程
 连接 ──▶ accept ──轮转分配──▶ Poller ─▶ TcpConnection ─▶ 切帧 + 解析 JSON ──批量──▶ ChatServer ─▶ Platform ─▶ MessageStore
                            (epoll/poll)  输入/输出缓冲区    (半包/粘包)            会话、推送、去重  业务规则    文本日志 / SQLite
                                    ◀──────────── 发件箱：本轮要发的数据按 I/O 线程分批交回 ────────────┘
```

## 快速开始

Linux（服务端、命令行客户端、压测工具和全部测试；Windows 见[构建与运行](#构建与运行)）：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
ctest --test-dir build                                   # 运行全部测试
cp -r data_demo data                                     # 演示数据（账号密码都是 123456）
build/bin/IMChatServer --port 9000 --data data           # 启动服务端
build/bin/IMChatCli --port 9000 QQ 10001 123456          # 另开两个终端，各登录一个账号互发消息
build/bin/IMChatCli --port 9000 QQ 10002 123456          # 命令：/to 10001 你好
```

### 用手机聊天

服务端自带网页客户端，手机不用安装任何东西：

1. 电脑上启动服务端（Windows：`build\bin\Release\IMChatServer.exe --port 9000 --data data`）。第一次运行时 Windows 防火墙会询问，选“允许”（专用网络）。
2. 在电脑上用 `ipconfig` 查到局域网 IPv4 地址，比如 `192.168.1.5`。
3. 手机连**同一个 Wi-Fi**，浏览器打开 `http://192.168.1.5:9000`，用另一个账号登录（如 QQ 10002 / 123456）。
4. 电脑上用图形客户端（联网模式）或命令行客户端登录 10001，两边就能互发私信、群消息，有未读数和已读回执；手机锁屏、切换网络后回到页面会自动重连并补收消息。

<img src="docs/images/mobile.png" width="260" alt="手机浏览器里的网页客户端，和电脑上的客户端互发消息">

![群聊与右键菜单](docs/images/chat.png)
![朋友圈](docs/images/moments.png)

> 项目最初是面向对象程序设计的课程设计（单机版，多个窗口共享本地数据文件），之后扩展为客户端/服务端架构，并持续做性能和可靠性方面的改进。进度见 [路线图](docs/ROADMAP.md)。

## 组成

| 程序 | 说明 |
|---|---|
| `IMChatServer` | 服务端：私信和群消息的确认与在线推送、离线同步、已读回执、单点登录、心跳超时、消息去重、限流、登录锁定 |
| `ChatClient`（库） | 网络客户端库：自动重连（指数退避）、心跳检测、待确认队列与断线重发、增量同步 |
| `IMChatCli` | 命令行客户端，基于 `ChatClient` |
| `IMChatClient` | 图形客户端（Windows）：Win32 + Direct2D 自绘，不依赖界面库。联网模式通过服务端工作（一处登录，本人的多个服务同时在线）；单机模式直接读写本地数据 |
| `IMChatConsole` | 控制台客户端（Windows，单机模式） |
| 网页客户端 | `web/index.html`，编译进服务端，浏览器访问服务端地址即可使用（手机、平板、任何电脑）：消息列表、私信与群聊、未读数、已读回执、撤回、断线自动重连与重发。不依赖任何外部资源，局域网没有外网也能用 |
| `IMChatBench` | 压测工具：吞吐、回执延迟（p50/p90/p99）、推送是否一条不少，以及各项存储和数据规模的微基准 |

## 功能

| 模块 | 内容 |
|---|---|
| 账号 | 一个用户可开通 QQ、微信、微博、微商四种服务；QQ 与微博共享号码，微信/微商可绑定 QQ；一处登录、其他服务同步登录 |
| 好友 | 好友申请与验证、备注、单向删除、黑名单、跨服务的共同好友与好友推荐 |
| 群 | QQ 群模式（群主 + 管理员，可申请入群）与微信群模式（仅群主有特权，只能邀请入群）可在线切换；禁言、群公告、临时讨论组、群系统提示 |
| 消息 | 私信与群消息、未读计数、聊天记录查询、@提醒（含 @所有人）、2 分钟内撤回、群主/管理员撤回成员消息 |
| 朋友圈 | 发表（公开 / 私密 / 部分可见 / 不给谁看）、点赞、评论与回复；只显示共同好友的互动 |
| 实时通信 | TCP 点对点聊天、多人实时群聊室（`select` 多路复用的服务端） |
| 数据 | 账号、好友、群：纯文本存储，写入采用"临时文件 + 刷盘 + 原子替换"，多个客户端同时运行时检测冲突并另存，不会互相覆盖。消息：只追加的文本日志，或 SQLite（服务端 `--storage sqlite`，首次启动时自动导入已有日志） |

## 服务端

整体结构见上面的[架构图](#架构)。

- **Reactor**：`EventLoop` 在一个线程里循环执行"等待 I/O 事件 → 分发给连接 → 执行定时器 → 执行其他线程投递的任务"。I/O 多路复用抽象成 `Poller` 接口，有两种实现：Linux 上默认用 `EpollPoller`（水平触发），其他平台用 `PollPoller`（Windows 上是 `WSAPoll`）。事件循环和连接的代码只认接口，换实现不用改。
- **多线程（one loop per thread）**：
  - 主循环只 `accept`，新连接轮转分给 N 个 I/O 线程（默认 2 个）；
  - I/O 线程负责收发、切帧、解析 JSON，把一批请求一次交给业务线程；
  - `Platform` 和会话表只在业务线程里访问，不需要加锁；
  - 业务线程要发的数据先进发件箱，本轮结束时按 I/O 线程分批交回，避免每条推送都跨线程唤醒一次。
- **跨线程唤醒**：其他线程用 `runInLoop` 投递任务，再往一对互连的套接字里写一个字节，唤醒阻塞在 `poll` 上的事件循环。Linux 用 `socketpair` 建这对套接字；Windows 没有 `socketpair`，就在回环地址上自己连自己。
- **非阻塞发送与合并写**：调用 `send` 时先放进输出缓冲区，本轮事件处理完后统一写出，同一轮里发给同一连接的多条消息合并成一次系统调用（群发时效果明显）。写不完的部分留在缓冲区，等可写事件再继续写；只有缓冲区里有数据时才关心可写事件，避免 `poll` 空转。如果对方长时间不读、积压超过 8 MiB，就断开连接，防止慢速客户端拖垮服务端内存。
- **协议**：`[4 字节长度][2 字节类型][JSON]`，按长度切分以处理半包和粘包；长度超过上限时直接断开。详见 [PROTOCOL.md](docs/PROTOCOL.md)。
- **会话**：同一账号只能在一处登录，新登录会把旧连接挤下线；超时没有收到心跳的连接会被关闭；服务端按客户端生成的 `clientMsgId` 去重，重发的消息只保存一次、只推送一次。
- **存储**：消息存储抽象成 `MessageStore` 接口，有文本日志和 SQLite 两种实现。SQLite 使用 WAL 和预编译语句缓存；同一轮事件循环的写入合并成一个事务，先提交、再发回执；检查点在后台线程完成。好友、群的数据文件也一样：一轮里的修改只保存一次，保存完才回复。两种实现的对比见[压测报告](docs/BENCHMARK.md#4-存储文本日志-vs-sqlite)。
- **保护与运维**：登录失败太多次时按账号和 IP 暂时锁定（防暴力猜密码，不暴露账号是否存在）；连接数上限（默认 1 万，满了新连接立即关闭），`accept` 持续出错（如文件描述符用完）时暂停 100 毫秒，避免事件循环空转；每条连接用令牌桶限制发消息的速率（防刷屏，允许重连后的补发突发）；`server.stats` 查询运行统计，服务端每分钟打印一行运行概况。

### 图形客户端的联网模式

登录页右上角切换到"联网"，填写服务器地址即可。也可以用命令行参数直接登录：

```powershell
build\bin\Release\IMChatClient.exe --server 127.0.0.1:9000 --login QQ 10001 123456
```

| 断线时：顶部提示正在重连，刚发的消息显示"发送中"，重连后自动补发 | 群聊：成员和角色、@提醒、撤回都通过服务端同步 |
|---|---|
| ![断线](docs/images/offline.png) | ![群聊](docs/images/online.png) |

界面通过 `Session` 接口取数据，有两种实现（桥接模式）：`LocalSession` 直接调用本地业务类，`RemoteSession` 基于 `ChatClient` 和本地缓存。

`RemoteSession` 的线程处理：
- `ChatClient` 的回调在网络线程里执行，不直接修改缓存，而是把"要做的修改"放进收件箱。
- 界面线程定时取出收件箱里的修改并执行，所以缓存只在界面线程里读写，不需要加锁。

### 两台电脑之间聊天

```powershell
# 电脑 A：启动服务端（--data 指定数据目录，--verbose 输出调试日志）
build\bin\Release\IMChatServer.exe --port 9000

# 电脑 A、B：各开一个命令行客户端（把 IP 换成电脑 A 的局域网地址）
build\bin\Release\IMChatCli.exe --host 192.168.1.10 --port 9000 QQ 10001 123456
build\bin\Release\IMChatCli.exe --host 192.168.1.10 --port 9000 QQ 10002 123456
```

命令行客户端的命令：`/to 账号 内容` 发私信，`/g 群号 内容` 发群消息，`/read` 把当前会话标为已读，`/status` 查看连接状态，`/stats` 查看服务端运行统计。

```
  [22:41] 10001：你好，我是张三，这条走的是 IMChatServer 👋   #7
* 当前会话：10001
  ✓ 已送达 #8
* 正在连接                ← 服务端宕机
* 已登录                  ← 服务端恢复，自动重连并重新登录
  [22:41] 10001：服务端挂了的时候发的这条，会在它恢复后补发   #9
```

## 设计要点

- **策略模式**：群管理规则抽象为 `GroupPolicy`，`QQGroupPolicy` / `WeChatGroupPolicy` 实现各自的权限判断（能否踢人、禁言、撤回……），群在运行时切换策略而不改动数据。
- **命令模式**：群操作封装成 `GroupCommand`，由 `GroupService` 执行。单机模式在本机执行，联网模式序列化后发给服务端执行同一个函数，两种模式共用一份群管理规则。
- **观察者模式**：`Group` 发生变化（邀请、退群、禁言、改名……）时通知 `GroupListener`，由 `Platform` 生成群系统提示；群对象不依赖消息模块。
- **多态**：四种服务派生自 `ServiceBase`，各自实现资料展示、朋友圈名称（QQ空间 / 朋友圈 / 微博动态 / 商品动态）等差异。
- **追加式事件日志**：消息、好友申请、点赞、撤回等都作为记录追加到 `messages.txt`（`FILE_APPEND_DATA` 保证每条记录完整写入）。解析结果缓存在内存里，每次只读取新追加的部分（增量读取），多个窗口、多个进程共享同一个文件，状态保持一致。
- **自绘界面**：图形客户端直接用 Direct2D / DirectWrite 绘制，包括消息气泡、对话框、右键菜单、彩色表情；绘制时登记可点击区域，点击判断和绘制使用同一个矩形。
- **全 UTF-8**：程序内部统一使用 UTF-8，只在调用 Windows 宽字符 API（文件路径、控制台、界面）时转换。

## 构建与运行

**Windows**：需要 Windows 10/11 和 Visual Studio 2022 或更新版本（带"使用 C++ 的桌面开发"）。也可以用 MinGW 的 GCC 编译。

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release        # 运行测试

scripts\reset_demo_data.bat              # 第一次运行前：把演示数据复制到 data\
build\bin\Release\IMChatClient.exe       # 图形客户端
build\bin\Release\IMChatConsole.exe      # 控制台客户端
```

**Linux**：服务端、命令行客户端、压测工具和全部测试可以在 Linux 上编译运行（图形客户端和控制台客户端只支持 Windows）。需要 GCC 或 Clang，以及 CMake 3.20 以上。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
cp -r data_demo data                                  # data_demo 是演示数据母版，复制一份再用
build/bin/IMChatServer --port 9000 --data data        # Linux 上使用 epoll；IMCHAT_POLLER=poll 改用 poll
```

可以用 `-DIMCHAT_SANITIZE=address,undefined` 或 `-DIMCHAT_SANITIZE=thread` 打开消毒器。

移植时，平台相关的部分都集中在少数几处：
- 文件（`DataFile`、`MessageCenter`、`ChatLog`）：Windows API 与 POSIX 的 `open` / `pread` / `fsync` / `rename`；
- 编码：旧版 GBK 数据文件的转换，Windows 用 `MultiByteToWideChar`，其他平台用 iconv；
- 密码学：Windows 用 CNG，其他平台用经过标准测试向量验证的 `Sha256`；
- 网络库：`SocketOps` 和 `Poller`。

Windows（MSVC、MinGW GCC）、Linux（GCC，CI 上运行）和 MSYS2 的 POSIX 环境下，全部测试都已通过。

在 Windows 上也可以直接用 VS 2022 "打开文件夹"。演示账号的密码都是 `123456`，例如 QQ `10001`、微信 `wx2001`。同时打开多个客户端并登录不同账号，就能互相聊天：

```powershell
build\bin\Release\IMChatClient.exe --login QQ 10002 123456
```

## 目录结构

```
src/core/      业务核心：账号、好友、群与群策略、消息日志、朋友圈、数据文件
src/protocol/  协议：帧编解码、JSON 消息（不依赖平台）
src/reactor/   网络库：Poller、EventLoop、TcpServer、TcpClient、TcpConnection（Windows / POSIX）
src/server/    聊天服务端
src/client/    网络客户端库 ChatClient（重连、心跳、可靠发送）
src/cli/       命令行客户端
src/bench/     压测工具
src/net/       旧的阻塞式网络代码（控制台版的点对点聊天、实时群聊室）
src/console/   控制台客户端
src/gui/       图形客户端（Win32 + Direct2D）
tests/         测试：业务核心测试 + GoogleTest 单元测试与集成测试
third_party/   nlohmann/json
data_demo/     演示数据
docs/          协议、路线图、截图
```

## 测试

```powershell
ctest --test-dir build -C Release
```

每次推送都由 GitHub Actions（[.github/workflows/ci.yml](.github/workflows/ci.yml)）检查：
- **Windows**：编译（警告视为错误）、运行全部测试、乱序重复运行单元测试，并跑一遍私信和群消息的压测，确认推送一条不少；
- **Linux**：GCC 编译和全部测试（epoll），网络相关测试再用 poll 跑一遍，服务端模糊测试多换 20 个随机种子，并做压测冒烟测试；另有 AddressSanitizer + UndefinedBehaviorSanitizer、ThreadSanitizer 两个任务。
- Windows 上还有一个 AddressSanitizer 任务。所有任务都必须通过。

- `tests/TestMain.cpp`（356 项检查）：直接调用业务接口，每个用例从同一份基线数据开始，在独立的临时目录中运行。覆盖账号绑定规则、两种群模式的权限、模式切换不损坏数据、保存与重新加载、加载时修复不一致的数据、两个窗口同时修改时的冲突处理、编码转换、含中文的数据路径、好友申请、撤回、朋友圈可见范围等。
- `tests/unit/`（GoogleTest，120 个用例；服务端和客户端的集成测试还会在 3 个 I/O 线程下、以及用 SQLite 存储消息时各再跑一遍）：
  - 协议：半包、粘包、非法长度、JSON 往返。
  - 网络库：缓冲区、定时器、跨线程任务、4 MiB 数据的分段发送、50 个并发连接（单线程和 4 个 I/O 线程各跑一遍，并检查连接确实分到了各个线程）、客户端在服务端恢复后自动重连。
  - 服务端集成测试：用真实套接字验证以下内容：
    - 登录，私信与群消息的推送，离线同步（含分批），已读回执；
    - 好友在线状态推送，联系人与群资料接口（含按管理策略计算的权限），撤回及其推送，@提醒；
    - 好友申请与验证、关闭验证后直接添加、备注、单向删除、黑名单，服务端重启后好友关系仍在；
    - 群详情中按策略算出的权限，审批入群后系统提示和数据变化的推送，越权和参数类型错误的请求被拒绝，切换模式后成员数据不变且重启后仍在，建群与解散；
    - 朋友圈的可见范围（私密、不给谁看）、点赞评论与互动消息、不能删除别人的动态；
    - 登录令牌：篡改、过期、换密钥后失效；凭令牌登录本人的其他服务，不能登录别人的账号，服务端重启后仍然有效；
    - 重发去重，重复登录挤下线，心跳超时，非法帧与非法 JSON，令牌桶限流与运行统计，连续输错密码后锁定与自动解锁；
    - 好友、群的修改在回复前已经写入文件，同一轮的多次修改只保存一次。
  - 模糊测试：登录前后各发几千个随机请求（随机的消息类型和操作名，字段类型、超长文本、非法 UTF-8、极端数值都是随机的），服务端可以拒绝，但不能崩溃、卡住或断开连接；随机种子固定可重现，`IMCHAT_FUZZ_SEED` 换种子（在 AddressSanitizer 下也跑过）。
  - 内存索引：朋友圈、好友申请的增量构建与账号注销后的重建（两种存储各跑一遍，另测多窗口共享日志），好友列表的哈希索引。
  - 消息日志的增量读取：另一个实例追加的内容可见，写到一半的行等换行后才解析，文件变短时从头解析，与完整解析的结果一致。
  - 消息存储的一致性测试：同一组用例分别跑在文本日志和 SQLite 上（序号共享且不复用、查询与排序、撤回与未读、注销与解散、重新打开后数据仍在），以及旧日志导入 SQLite。
  - 浏览器接入：WebSocket 握手（RFC 6455 的例子）、各种长度编码、违反协议的帧（没加掩码、分片的控制帧、扩展位、超长）被拒绝；用模拟浏览器的套接字取网页、握手、和原生客户端互发消息，握手请求后紧跟的数据、分片消息中间插入心跳、正常关闭和出错关闭、被挤下线的通知。
  - SHA-256 / HMAC 的标准测试向量，Poller（Linux 上是 epoll）的基本行为。
  - 客户端端到端测试：服务端停机期间发出的消息，恢复后恰好送达一次；被挤下线的客户端不会自动重连；分批同步的途中断线，重连后从同步到的位置接着要（用按脚本应答的假服务端制造这个时序）。

## 许可证

[MIT](LICENSE)。第三方库：[nlohmann/json](third_party/nlohmann/LICENSE.MIT)（MIT），构建时下载的 SQLite（公有领域）和 GoogleTest（BSD-3-Clause，仅用于测试）。
