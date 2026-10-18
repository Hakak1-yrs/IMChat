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

## 请求与响应

- 请求可以带 `reqId`（整数），服务端原样放在响应中。客户端可以连续发出多个请求、不必等响应，再根据 `reqId` 把响应和请求对应起来。
- 推送（`KICKED`、`MSG_PUSH`）由服务端主动发出，不带 `reqId`。

## 消息类型

| 类型 | 编号 | 方向 | 正文 |
|---|---|---|---|
| `PING` | 1 | C → S | `{}` |
| `PONG` | 2 | S → C | `{}` |
| `LOGIN_REQ` | 100 | C → S | `{reqId, service: "QQ"\|"WECHAT"\|"WEIBO"\|"WEISHANG", account, password}` |
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
| `DATA_CHANGED` | 512 | S → C | `{topics: ["contacts", "social"]}`：别人的操作影响了我（如通过了我的好友申请），重新获取对应数据 |
| `ERROR_RESP` | 900 | S → C | `{reqId?, code, message}` |

`LOGIN_RESP` 成功时还带有 `location`（所在地）和 `momentsName`（QQ空间 / 朋友圈……）。

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

修改好友关系、备注的操作，服务端会立即把数据文件写回磁盘；消息类记录（好友申请、拉黑等）本来就是即时追加写入的。

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

## 消息可靠性

服务端：

- **去重**：每条消息由客户端生成唯一的 `clientMsgId`。服务端会记住每个账号最近 256 条消息的回执；遇到重复的 `clientMsgId`，直接返回第一次的回执，不会再保存一遍，也不会再推送给对方。
- **序号**：`seq` 是消息在服务端全局递增的序号，可以用来排序，也可以作为同步的位置。
- **离线消息**：对方不在线时，消息照常保存，对方上线后用 `SYNC_REQ` 取到。

客户端（`src/client/ChatClient` 的做法）：

1. 要发的消息先进入**待确认队列**，收到 `SEND_MSG_ACK` 才移除。
2. 断线后自动重连，重连间隔按指数退避（0.5 秒起，最长 30 秒），然后用记住的账号重新登录。
3. 登录成功后先 `SYNC_REQ{afterSeq: 本地最大序号}`；`hasMore` 为 true 时，从这一批最后一条的序号继续要，直到取完。
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
