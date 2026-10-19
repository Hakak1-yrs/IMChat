# 通信协议

客户端与 `IMChatServer` 之间使用 TCP 长连接，传输自定义的二进制帧。

## 帧格式

```
+----------------+----------------+---------------------------+
| 长度 (4 字节)  | 类型 (2 字节)  | 正文（UTF-8 JSON 对象）    |
+----------------+----------------+---------------------------+
```

- 整数都是网络字节序（大端）。
- **长度** = 类型字段 + 正文的字节数，不含长度字段本身，范围是 `2 ~ 2 + 1 MiB`。
- 正文为空时视为 `{}`。

### 为什么这样设计

- **按长度切分，解决半包和粘包**：TCP 只保证字节有序到达，不保留消息边界。一次 `recv` 可能只收到半帧，也可能收到好几帧。接收方先读 4 字节长度，数据不够就留在缓冲区里，等下次收到更多数据再解析；够了就切出一帧，继续解析下一帧。
- **先检查长度再等数据**：对方报出一个超过上限的长度（比如 4 GB）时，立即判定为协议错误并断开连接，不会为它分配内存、也不会一直等待。
- **类型放在帧头里**：服务端不解析 JSON 就能知道这是什么消息，方便以后按类型做限流、统计，或者把正文换成 Protobuf 时帧格式保持不变。
- **正文用 JSON**：可读、方便调试（用 PowerShell 或 Python 几行代码就能写一个测试客户端），对聊天消息这样的数据量来说性能足够。

## 浏览器接入（WebSocket）

浏览器里的网页不能直接用 TCP 套接字，所以服务端在**同一个端口**上同时接受浏览器：

- 连接的第一个字节决定它是哪种客户端。原生客户端的第一个字节是帧长度的最高字节，长度不超过 1 MiB 多一点，这个字节总是 0；浏览器发来的是 HTTP 请求，以 `GET` 开头。两者不会混淆。
- `GET /`：回复网页客户端（`web/index.html`，构建时嵌进服务端程序），然后关闭连接。
- `GET /ws` 并带 `Upgrade: websocket`：按 RFC 6455 握手（`Sec-WebSocket-Accept` = Base64(SHA-1(Key + 固定 GUID))），之后这条连接收发 WebSocket 帧。
- 其他路径回复 404，格式不对的请求回复 400；请求头超过 8 KiB 视为格式不对。

握手之后，**每条 WebSocket 二进制消息里装一帧上面的协议帧**，消息类型和正文与原生客户端完全相同：

```
WebSocket 帧头（2～14 字节，客户端发的带掩码） | 长度 (4) | 类型 (2) | JSON 正文
```

I/O 线程把 WebSocket 这一层拆掉，得到和原生连接一样的字节流，再交给同一套切帧、解析、分发代码，业务处理不知道对方是哪种客户端。发送时反过来，给每帧套上 WebSocket 帧头。所以浏览器和原生客户端可以互发消息、互相挤下线，所有规则都一样。

WebSocket 层的规则（`src/protocol/WebSocket.*`、`src/server/WebTransport.*`）：

- 客户端的帧必须加掩码；保留位不为 0、未定义的操作码、分片的或超过 125 字节的控制帧，都按协议错误处理：回复关闭帧（1002）后断开。
- 支持分片消息，分片之间可以插入控制帧；一条消息（合起来）最长 1 MiB + 6 字节，超过回复 1009。
- 文本消息不支持（协议帧是二进制的），回复 1003。
- 收到 ping 立即回 pong；收到关闭帧时回一个关闭帧（带回对方的状态码）后断开。
- 浏览器自己的心跳仍然用协议里的 `PING`，和原生客户端一样受 `--idle-timeout` 约束。

## 请求与响应

- 请求可以带 `reqId`（整数），服务端原样放在响应中。客户端可以连续发出多个请求、不必等响应，再根据 `reqId` 把响应和请求对应起来。
- 推送（`KICKED`、`MSG_PUSH`）由服务端主动发出，不带 `reqId`。

## 消息类型

| 类型 | 编号 | 方向 | 正文 |
|---|---|---|---|
| `PING` | 1 | C → S | `{}` |
| `PONG` | 2 | S → C | `{}` |
| `LOGIN_REQ` | 100 | C → S | `{reqId, service: "QQ"\|"WECHAT"\|"WEIBO"\|"WEISHANG", account, password}`，或用 `token` 代替 `password` |
| `LOGIN_RESP` | 101 | S → C | 成功：`{reqId, ok: true, nickname, unreadPrivate, unreadGroup}`；失败：`{reqId, ok: false, error}` |
| `KICKED` | 102 | S → C | `{reason}`。同一账号在别处登录，发完这条通知后服务端关闭连接 |
| `SEND_MSG_REQ` | 200 | C → S | 私信：`{reqId, clientMsgId, to, text}`；群消息：`{reqId, clientMsgId, groupNo, text}` |
| `SEND_MSG_ACK` | 201 | S → C | 成功：`{reqId, clientMsgId, ok: true, seq, time}`；失败：`{reqId, clientMsgId, ok: false, error}` |
| `MSG_PUSH` | 202 | S → C | 一条消息（格式见下）。私信推给对方，群消息推给除发送者外所有在线的群成员 |
| `SYNC_REQ` | 300 | C → S | `{reqId, afterSeq, limit?}`：取序号大于 `afterSeq` 的、与我有关的消息（我收发的私信 + 我所在的群的消息） |
| `SYNC_RESP` | 301 | S → C | `{reqId, messages: [消息…], hasMore}`：按序号递增；`limit` 默认 200，最多 500 |
| `READ_REQ` | 400 | C → S | `{reqId, peer, seq}` 或 `{reqId, groupNo, seq}`：这个会话已读到 `seq` |
| `READ_RESP` | 401 | S → C | `{reqId, ok}` |
| `READ_RECEIPT` | 402 | S → C | `{reader, seq}`：私信的对方已读到 `seq`（群消息不发回执） |
| `CALL_REQ` | 500 | C → S | `{reqId, op, args}`：通用请求，`op` 见下表 |
| `CALL_RESP` | 501 | S → C | 成功：`{reqId, ok: true, data}`；失败：`{reqId, ok: false, error}` |
| `PRESENCE` | 510 | S → C | `{account, online}`：好友上线 / 下线 |
| `MSG_REVOKED` | 511 | S → C | `{seq, revokedBy}`：会话里的一条消息被撤回 |
| `DATA_CHANGED` | 512 | S → C | `{topics: ["contacts", "social", "groups", "moments"]}`：别人的操作影响了我（如通过了我的好友申请、把我拉进群），重新获取对应数据 |
| `ERROR_RESP` | 900 | S → C | `{reqId?, code, message}` |

`LOGIN_RESP` 成功时还带有 `location`（所在地）、`momentsName`（QQ空间 / 朋友圈……），以及：
- `token`：登录令牌，可以在 `LOGIN_REQ` 里代替 `password`（`{service, account, token}`）；
- `services`：本人开通的其他服务 `[{service, account, nickname}]`。

### 登录令牌

题目要求"一处登录，其他服务视为自动登录"。客户端用密码登录一个服务后，凭响应里的令牌再登录 `services` 中的其他账号，每个服务一条连接，同时在线。断线重连时客户端也用令牌重新登录，明文密码登录成功后就从内存里清除。

- 格式：`用户编号.过期时间.签名`，签名 = HMAC-SHA256(服务端密钥, `用户编号.过期时间`)，有效期 7 天。
- 服务端不保存令牌，验证时重新计算签名比对。没有密钥就伪造不出签名，改动用户编号或过期时间都会让签名对不上。
- 令牌只能登录同一个用户名下的账号。
- 密钥保存在数据目录的 `server.key` 中，第一次启动时随机生成，所以服务端重启后令牌仍然有效。删除这个文件，所有令牌立即失效。

### 通用请求（`CALL_REQ`）

查询、修改类的操作都走通用请求：服务端用"操作名 → 处理函数"的表分发，新增一种操作不必新增消息类型。

| `op` | `args` | `data` |
|---|---|---|
| `contacts.list` | — | `{friends: [{id, nickname, remark, online, location, birthday, tAge, mutual: [{id, nickname}]}]}` |
| `groups.list` | — | `{groups: [{groupNo, name, mode, modeName, notice, owner, admins, muted, members: [{id, nickname}], canSpeak, canMentionAll, revocable}]}` |
| `reads.get` | — | `{private: {好友: 我读到的序号}, peerRead: {好友: 对方读到的序号}, groups: {群号: 我读到的序号}}` |
| `message.revoke` | `{seq}` | `{seq, revokedBy}`；同时向会话里其他在线的人推送 `MSG_REVOKED` |
| `social.get` | — | `{requests: [{seq, from, to, fromNickname, toNickname, greeting, time, status: "pending"\|"accepted"\|"rejected"}], verify, blocked: [{id, nickname}]}` |
| `profile.get` | `{id}` | `{exists, id, nickname, location, birthday, tAge, idLabel, info: [按账号类型输出的资料], isFriend, remark, peerHasMe, blocked, online, mutual}` |
| `friends.request` | `{target, greeting, remark}` | `{message, becameFriends}`；对方没开验证时直接成为好友；向对方推送 `DATA_CHANGED` |
| `friends.respond` | `{seq, accept, remark}` | `{message}`；向申请人推送 `DATA_CHANGED` |
| `friends.remove` | `{id}` | `{message}`（单向删除） |
| `friends.remark` | `{id, remark}` | `{message}` |
| `friends.block` | `{id, on}` | `{message}` |
| `settings.verify` | `{on}` | `{verify, message}` |

| `groups.all` | — | `{groups: [{groupNo, name, modeName, owner, ownerName, memberCount, joined, applied, allowApplyJoin}]}`：本服务的全部群 |
| `group.get` | `{groupNo}` | 群详情（见下）；群不存在时 `{exists: false, groupNo}` |
| `group.create` | `{name}` | `{groupNo, message}` |
| `group.exec` | `{kind, groupNo, target, text, on, members}` | `{message}`：群管理命令，`kind` 见下表 |

| `server.stats` | — | `{uptimeSeconds, ioThreads, connections, rejectedConnections, online, logins, messages, pushes, calls, rateLimited, loginLockouts, dataSaves, dedupAccounts}`：服务端运行统计，命令行客户端的 `/stats`（`dataSaves` 是保存好友、群数据文件的次数，同一轮里的多次修改只保存一次；`dedupAccounts` 是保留着消息去重表的账号数，`rejectedConnections` 是因为连接数已满而拒绝的连接数） |
| `moments.list` | `{author?}` | `{moments: [{seq, author, scope, time, text, likes, comments: [{seq, author, replyTo, time, text}]}], names: {账号: 昵称}}`：我能看到的动态，最新的在前 |
| `moments.notices` | — | `{all: [文字…], unread: [文字…]}`：别人给我的点赞、评论、回复 |
| `moments.markRead` | — | `{}` |
| `moments.post` | `{text, scope}` | `{message}`；`scope`：`all` / `self` / `only:a,b` / `except:a,b` |
| `moments.like` | `{seq}` | `{message}`（点赞或取消） |
| `moments.comment` | `{seq, replyTo?, text}` | `{message}` |
| `moments.delete` | `{seq}` | `{message}` |
| `moments.deleteComment` | `{seq}` | `{message}` |

朋友圈的规则都由服务端按查看者处理：只能看到互为好友的人的动态；可见范围不允许的不下发；点赞和评论只下发共同好友的（作者本人能看到全部）；别人动态的 `scope` 为空，不泄露"不给谁看"的名单。发表、点赞、评论、删除后，动态作者和作者的好友收到 `DATA_CHANGED {topics: ["moments"]}`。

修改好友关系、备注、群数据的操作，服务端会立即把数据文件写回磁盘；消息类记录（好友申请、拉黑等）本来就是即时追加写入的。

#### 群管理命令（`group.exec`）

群操作封装成命令对象 `GroupCommand`（命令模式）。单机模式下图形界面直接交给 `GroupService::execute` 执行；联网模式下客户端把同一个对象序列化成下表的参数发给服务端，服务端调用同一个函数。群管理规则只有一份，两种模式的行为一致。

| `kind` | 含义 | 用到的参数 |
|---|---|---|
| `join` | 申请加入（无主群直接认领） | — |
| `approve` | 审批入群申请 | `target` 申请人，`on` 同意/拒绝 |
| `invite` | 推荐好友入群（只能推荐自己的好友） | `target` |
| `quit` | 退群 | — |
| `kick` | 踢出成员 | `target` |
| `admin` | 设置/取消管理员 | `target`，`on` |
| `mute` | 禁言/解除禁言 | `target`，`on` |
| `transfer` | 转让群主 | `target` |
| `rename` | 修改群名 | `text` |
| `notice` | 发布公告（同时发一条群消息）；`text` 为空表示清除 | `text` |
| `mode` | 切换管理模式；`message` 有多行：结果、切换前后的数据比对、新模式的特色 | `text`：`"QQ"` / `"WECHAT"` |
| `subNew` | 新建讨论组 | `text` 名称，`members` |
| `subAdd` | 讨论组拉人 | `target` 讨论组名称，`members` |
| `subDissolve` | 解散讨论组 | `target` 讨论组名称 |
| `dissolve` | 解散群 | — |

权限全部由服务端按群当前的管理策略检查，客户端伪造请求也无法越权。命令执行成功后：
1. 操作产生的群消息（如"A 邀请 B 加入了群聊"、群公告）以 `MSG_PUSH` 推给全体在线成员，包括操作者本人；
2. 操作前后的成员、入群申请人、被操作的人（被踢、被拒绝的人也在内）收到 `DATA_CHANGED {topics: ["groups"]}`，重新获取群列表和打开着的群详情。

#### 群详情（`group.get` 的 `data`）

群资料 `{groupNo, name, mode, modeName, owner, notice, preset, features}`，成员 `members` 和入群申请 `pending`（每项 `{id, nickname, owner, admin, muted, canSetAdmin, canMute, canTransfer, canKick}`），讨论组 `subGroups`（`{name, creator, members, canAdd, canDissolve}`），以及查看者的身份和权限 `{member, isOwner, isAdmin, applied, canEditInfo, allowApplyJoin, allowSubGroup, allowAdmin, allowMute, canApprove, canDissolve}`。当前模式下不生效、但保留着的管理员和禁言记录数放在 `frozenAdmins`、`frozenMuted`。

`groups.list` 里的 `canSpeak`（我能否发言）、`canMentionAll`（我能否 @所有人）、`revocable`（我能撤回哪些成员的消息），都由服务端按群当前的管理策略算好。客户端直接使用，不必重复实现权限规则，也就不会出现两边规则不一致的问题。

### 消息格式（`MSG_PUSH` 和 `SYNC_RESP.messages` 中的每一项）

```json
{"seq": 12, "service": "QQ", "from": "10001", "to": "10002", "text": "你好", "time": "2026-10-17 22:41:05"}
{"seq": 13, "service": "QQ", "from": "10001", "groupNo": 1001, "text": "群里说一句", "time": "2026-10-17 22:41:09"}
```

- 私信带 `to`，群消息带 `groupNo`。
- 群消息 @ 了哪些成员放在 `mentions`（账号数组）：`@昵称`，以及有权限的人发的 `@所有人`。
- 群系统提示（如"A 邀请 B 加入了群聊"）带 `"system": true`。
- 撤回的消息带 `"revoked": true` 和 `revokedBy`，`text` 为空，不再下发原文。

### 错误码（`ERROR_RESP.code`）

| 码 | 含义 |
|---|---|
| 1 | `BAD_REQUEST`：正文不是合法的 JSON 对象，或缺少必要字段 |
| 2 | `UNKNOWN_TYPE`：不支持的消息类型 |
| 3 | `NOT_LOGGED_IN`：需要先登录 |
| 4 | `ALREADY_LOGGED_IN`：这条连接已经登录 |

业务规则导致的失败（比如对方不是好友、被拉黑、消息过长）不算协议错误，通过 `SEND_MSG_ACK` 里的 `ok: false` 和 `error` 返回。

## 连接管理

- **心跳**：客户端应定期发送 `PING`。服务端在 `--idle-timeout` 秒内（默认 90 秒）没有收到任何数据，就关闭这条连接。
- **单点登录**：同一账号只能有一个在线连接。新连接登录成功后，服务端给旧连接发 `KICKED`，再关闭旧连接。
- **慢速客户端**：如果某条连接待发送的数据超过 8 MiB（对方长时间不读），服务端直接断开它，防止内存被占满。
- **防暴力猜密码**：
  - 同一账号 5 分钟内登录失败 5 次（同一 IP 是 20 次），暂停登录 60 秒。账号不存在也照样计数，提示相同，不暴露哪些账号存在。
  - 暂停期间直接拒绝，不再校验密码（猜对了也看不出来），响应为 `{ok: false, error, retryAfter: 秒数}`。
  - 凭令牌登录失败同样计数。登录成功后，这个账号的失败次数清零。
- **速率限制**：每条连接发消息的速率由令牌桶限制（默认每秒 20 条，允许突发 50 条，`--rate-limit` 调整，0 表示不限）。超出时这条消息不保存、不推送，回执为 `{ok: false, error, retryable: true}`，客户端可以稍后用同一个 `clientMsgId` 重发。突发额度足够断线重连后补发积压的消息。

## 消息可靠性

服务端：

- **去重**：每条消息由客户端生成唯一的 `clientMsgId`。服务端会记住每个账号最近 256 条消息的回执（序号和时间）；遇到重复的 `clientMsgId`，直接返回第一次的回执，不会再保存一遍，也不会再推送给对方。账号下线 30 分钟后释放它的去重表（客户端重连后会立即重发没收到回执的消息，用不了这么久），否则服务端运行越久内存越多。
- **序号**：`seq` 是消息在服务端全局递增的序号，可以用来排序，也可以作为同步的位置。
- **离线消息**：对方不在线时，消息照常保存，对方上线后用 `SYNC_REQ` 取到。

客户端（`src/client/ChatClient` 的做法）：

1. 要发的消息先进入**待确认队列**，收到 `SEND_MSG_ACK` 才移除。
2. 断线后自动重连，重连间隔按指数退避（0.5 秒起，最长 30 秒），然后用登录令牌重新登录。
3. 登录成功后先 `SYNC_REQ{afterSeq: 同步位置}`；`hasMore` 为 true 时，从这一批最后一条的序号继续要，直到取完。
   “同步位置”是“这个序号以前的消息都已经收到”的位置，**不一定是收到的最大序号**：分批同步的途中可能推送来更新的消息，它和当前这一批之间还缺着。所以同步途中，同步位置只前进到每一批的末尾；同步完成后才前进到收到的最大序号，之后随推送前进。否则同步途中断线，下次从推送来的那条开始要，中间的消息就永远收不到了（`ChatClientSyncTest` 用按脚本应答的假服务端复现这个时序）。
4. 同步完成后，把待确认队列里的消息**按原顺序、用原来的 `clientMsgId`** 重发。
5. 推送和同步可能重叠（比如登录后同步期间又来了新消息），客户端按 `seq` 去重，每条消息只交给界面一次。
6. 心跳：每 15 秒发一次 `PING`；如果 45 秒内没有收到服务端的任何数据，就认为连接已经失效（比如网络被切断），主动断开并重连。
7. 收到 `KICKED` 后不再自动重连，避免两个客户端互相挤下线。

这样，服务端宕机、网络中断期间发出的消息，都会在恢复后送达，并且只送达一次。

## 示例

登录（`reqId` 为 1）：

```
00 00 00 42  00 64  {"reqId":1,"service":"QQ","account":"10001","password":"123456"}
```

长度 0x42 = 66 = 2 字节类型 + 64 字节正文；类型 0x64 = 100（`LOGIN_REQ`）。

响应（“小张”在 UTF-8 中占 6 字节）：

```
00 00 00 4D  00 65  {"nickname":"小张","ok":true,"reqId":1,"unreadGroup":2,"unreadPrivate":2}
```
