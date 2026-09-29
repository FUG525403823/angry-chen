# ADR-009 UDP 传输与协议重构

- 状态：已接受（v2 起点）
- 取代：[ADR-002](ADR-002-网络模型与协议.md) 的**传输层与协议**条款（"服务器权威 / 客户端预测 / 量化表 / 差分基线 / 回滚补偿"这些**语义**继续有效并被本 ADR 继承）
- 生效范围：`server/` 的网络与协议模块、`client/` 的网络与预测模块、`docs/plans-v2/` 的 S03/S04/S12 与 C02/C03/C04/C06

## 背景

浏览器（v1 客户端）不能发裸 UDP，ADR-002 因此锁 TCP/WebSocket，并用"小快照 + 不重传 + 旧快照丢弃"缓解队头阻塞。v2 客户端是原生 Unity 进程（ADR-008），这条约束消失；而射击玩法的体验瓶颈正是丢包下的抖动与延迟上限，故传输改为 UDP。

## 决策

1. **权威模型不变**：服务器产生既成事实（快照/事件），客户端只上报命令（意图）。客户端永不产生伤害判定。
2. **传输 = 自研 UDP 可靠性层**，双端实现同一份冻结规范（本 ADR §冻结契约）。不引入 Unity Transport、KCP 库或任何第三方网络库。
3. **三通道模型**（一"连接"= 一条 UDP 五元组 + 会话短 ID）：

| 通道 | 方向 | 语义 | 丢失处理 |
|---|---|---|---|
| `SnapshotChannel` | S→C | **不可靠**、按 tick 单调、旧包直接丢 | 不重传；下一个 tick 的快照覆盖 |
| `EventChannel` | S→C | **可靠有序 + 幂等**（`eventId` 单调） | 重传直到 ack；重复应用无副作用 |
| `CommandChannel` | C→S | **可靠有序**（只发最新，积压 >2 丢中间） | 重传直到 ack；服务器丢弃过期命令 |

4. **节奏**：服务器 tick 20Hz（50ms，`dtMs = 50` 恒定）；快照每 tick 一次、可自适应 **10–30Hz**，以 `snapshotRateX10`（单位 1/10 Hz）表达，冻结档位 `{200,150,100}` = 20/15/10 Hz；命令上行 30Hz。
5. **差分快照**：服务器为每客户端记录"已编码基线 tick"（`baselineTick` = 上一次为该客户端编码出的快照所属 tick），只下发相对基线的改变实体与移除实体列表（继承 ADR-002 语义，编码按本 ADR 的头格式）。快照通道**不可靠，不等客户端 ack**：客户端丢包后的恢复靠 `baselineTick = 0`（强制全量）+ 每 40 tick 一次的全量节拍。
6. **命中判定回滚补偿**：服务器保留 1 秒（20 tick）姿态历史环，回退时长 = `min(rttMs / 2, 200)`，按回退后的姿态求交。回退时长越界（> 200ms）时**不回滚**：返回 `ok = false` 并按**当下**姿态判定；诊断字段 `clamped` 只用于观测，不改变判定语义。
7. **握手与重连**：`Hello`/`HelloAck` 建连接并发会话短 ID；断线后 30 秒宽限期内用重连令牌 `Resume`（继承 [ADR-006](ADR-006-会话身份与重连令牌.md)），宽限期后名额释放。
8. **量化表沿用 ADR-002 的冻结表，不重新设计**（见下）。

## 冻结契约

### 通用包头（所有包，8 字节，小端）

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | `version` | u8 | 协议版本，v2 = `1`；不匹配即拒绝并回 `Disconnect(reason=1 versionMismatch)` |
| 1 | `type` | u8 | 1 Hello / 2 HelloAck / 3 Resume / 4 Command / 5 Snapshot / 6 Event / 7 KeepAlive / 8 Disconnect / 9 Fragment / 10 MatchState（reliable，单播）/ 11 Join（reliable，C→S） |
| 2 | `flags` | u16 | bit0=reliable，bit1=moreFragments，bit2=ackOnly |
| 4 | `session` | u16 | 会话短 ID（服务器分配，握手前的包为 0） |
| 6 | `seq` | u16 | **每通道独立**递增序号（回绕模 2^16） |

### 可靠包扩展头（`flags.reliable = 1` 时紧随通用包头，12 字节）

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 8 | `msgId` | u32 | 可靠消息唯一 ID，接收侧据此去重 |
| 12 | `ackBase` | u32 | 已收到的最大 `msgId` |
| 16 | `ackBits` | u32 | `ackBase` 之前的 32 位位图（1 = 已收到） |

### 分片头（`type = 9`（分片包）时紧随通用包头，4 字节）

> **原问题（C02 施工时发现、S03/S04 期登记，2026-09-24）**：原表述是「`flags.moreFragments = 1` **或** `fragCount > 1` 时紧随通用包头」。第二个条件不可判定——读通用包头时还看不到 `fragCount`；而若把 `moreFragments` 读作「后面还有分片」，末分片就不置该位，也就无处携带 `fragId`/`fragIndex`，S04 的本地重组无法完成。
>
> **已裁决（B1，按实现修正 ADR，不反过来改代码）**：
> 1. **在场条件只有一个：该包是分片包（`type = 9`）**，末分片的 4 字节分片头同样在场；原「或 `fragCount > 1`」已删（读包头时不可判定）。
> 2. 实现口径：`server/src/net/wire.hpp` 的 `payloadOffset = 8 + 12×reliable + 4×moreFragments`、`decodePacket` 的 `hasFragmentHeader` 只看该位；`server/src/net/codec.cpp` 对 `type 9` 要求置 `flags.moreFragments`（不置即 `DecodeFailure::kBadValue`）；`server/src/net/fragment.cpp` 的 `splitMessage` 对**每一片（含末片）**都置该位。
> 3. 于是线上**任一分片都带分片头**：`moreFragments` 在 v2 分片里的语义是「本包是分片」，**不是**「后面还有分片」；末片靠 `fragIndex + 1 == fragCount` 识别，接收侧不得用 `moreFragments` 判断末片，也不得用它决定是否存在 4 字节分片头。
> 4. 依据：`server/README.md` §4.2-5（本条原始登记）、§5.1/§5.2 行为要点、§4.1 的 `fragment.hex`（`type 9` + `moreFragments`，slice 0/2）。ADR 与实现不一致时按工程约定 §8 先改 ADR。

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 8 | `fragId` | u16 | 同一逻辑消息的分片组 ID |
| 10 | `fragIndex` | u8 | 分片序号（0 起） |
| 11 | `fragCount` | u8 | 分片总数（上限 8） |

> **已裁决（C4 批次，按实现裁定）**：**分片重组键 = `(session, fragId)`**。分片包的包头 `type` 恒为 `9`（`kFragment`），原通道身份不上线，§5.4 写的 `channelType` 无法从线上还原；`fragId` 在**同一会话内全局唯一**（快照与事件共用一个命名空间），因此**不给分片头加通道字段**（那要改本 ADR + 两端编解码）。
> 实现口径：`server/src/net/fragment.hpp:27-39` 的 `FragmentKey` **保留 `type` 字段但线上恒为 `kFragment`**（`server/src/net/fragment.cpp:25` 的 `sliceHeader.type = kFragment`），等效按 `(session, fragId)` 分组。依据：`server/README.md` §5.3-3、§5.2 行为要点；客户端链 C03 必须与之一致。

### 报文上限与预算

| 项 | 值 |
|---|---|
| 单包最大载荷 | 1200 字节（含头），超过必须分片 |
| 单快照上限 | 2048 字节；稳态 ≤ 1228 字节 |
| 单帧实体记录 | ≤ 128 条（`count` 为 u8，硬上限 255）；超出部分按 `EntityId` 升序留到后续 tick 继续差分 |
| 单帧事件 | ≤ 64 条；超出部分走 `EventChannel` 可靠通道补发 |
| 每客户端带宽 | ≤ 40 KB/s（口径 = 全部出站 UDP 载荷，含事件与可靠重传） |
| 心跳 | 每 500ms 一次 `KeepAlive`（`flags.ackOnly` 时无载荷） |
| 断线判定 | 3s 未收到任何包 → 进入宽限期（30s） |
| 重传 | 仅可靠包；RTO 序列 `{200, 300, 450, 675, 1000}` ms（200ms 起、1.5 倍退避、上限 1s），最多 5 次后判失联并重连 |

### 量化（**承继 ADR-002，不改**）

| 量 | 编码 | 精度 |
|---|---|---|
| 位置 x/y/z | `int16`，厘米 | 1cm，范围 ±327.67m |
| 偏航/俯仰 | `uint16` 映射 `0..2π` | ≈0.0055° |
| 血量 | `uint8` = `round(hp/maxHp*255)` | 1/255 相对值 |
| 状态位域 | `uint8` | kind/anim/flags |
| 实体标识 | `uint16` | 单世界唯一 |
| tick | `uint32` | — |
| 服务器时间 | `uint32` 毫秒（相对对局开始） | 1ms |
| `eventId` | `uint32` | 单调递增，幂等键 |

### 载荷布局（v2 冻结，双方逐位实现）

> 与 [S03 §5.2–§5.4](../../plans-v2/server/S03-二进制协议与编解码.md) 必须逐字一致；改任一处必须在同一提交内改另一处（工程约定 §8）。本表只冻结线上字节，不约束内存结构。

**命令载荷（type 4，14 字节，紧随通用包头）**

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | `moveX` | i8 | 归一化移动轴，−127…127 |
| 1 | `moveY` | i8 | 同上 |
| 2 | `yaw` | u16 | 角度量化（0..2π 映射） |
| 4 | `pitch` | u16 | 俯仰角度量化（与 `yaw` 同刻度） |
| 6 | `buttons` | u8 | 位域见下 |
| 7 | `switchTo` | u8 | 武器槽：0 手枪 / 1 步枪 / 2 霰弹枪 |
| 8 | `seq` | u16 | 命令序号，单调 |
| 10 | `clientTick` | u32 | 客户端预估 tick（v1 的 `Command.tick`） |

**`buttons` 位域（8 位，写端 `& 0xff`）**：`fire=1`、`sprint=2`、`jump=4`、`reload=8`、`interact=16`、`rage=32`、`switchWeapon=64`、`ready=128`。位序承继 v1（原 7 位 `& 0x7f`），v2 新增 `ready`。

**快照载荷（type 5，不可靠）段序**：通用包头 → `tick` u32 → `serverTimeMs` u32 → `lastAckedSeq` u16 → `baselineTick` u32 → 实体块（`count` u8 + 15B×n）→ 移除列表（`removedCount` u8 + u16×m）→ 事件块（`eventCount` u8 + Σ 条目）。实体记录 15 字节：`id` u16、`kindFlags` u8（bit0-1 = kind：0 player/1 sheep/2 projectile/3 pickup；bit2-7 = flags：downed=1、rageMode=2、reloading=4、charging=8、fading=16、idle=32）、`xCm`/`yCm`/`zCm` i16 厘米、`yawUnits` u16、`pitchUnits` u16、`hpRatioUnits` u8、`state` u8。事件块是 v2 新增（v1 快照无事件块），置于载荷末尾。

**事件条目** = `eventId` u32 + `type` u8 + 类型载荷。类型与条目总长：

| type | 名称 | 条目总长（字节） |
|---|---|---|
| 1 | `playerHit` | 18（v2 新增权威命中点 `hitX`/`hitY`/`hitZ` i16 厘米；v1 为 12） |
| 2 | `sheepKilled` | 10 |
| 3 | `waveStart` | 8 |
| 4 | `waveClear` | 10 |
| 5 | `playerDowned` | 7 |
| 6 | `reviveProgress` | 11 |
| 7 | `reviveDone` | 9 |
| 8 | `rageActivated` | 9 |
| 9 | `matchEnded` | 11 |
| 10 | `phaseChange`（v2 新增） | 9 |

编号 1–9 沿用 v1 的 `EVENT_TYPE`，10 为 v2 新增。

逐字段的载荷布局（各类型的字段次序与字节）以 S03 §5.4 为准。

**MatchState 载荷（type 10，reliable，单播，v1 `OPCODE.matchState` 的 v2 对应物）**：v2 的通用包头取代 v1 的 opcode 字节，其余逐字段与 v1 `encodeMatchState` 一致。

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 0 | `phase` | u8 | `MATCH_PHASE`（0 lobby / 1 loading / 2 playing / 3 intermission / 4 ended） |
| 1 | `wave` | u8 | 当前波次 0–10 |
| 2 | `intermissionMs` | u16 | 波间剩余毫秒（非波间为 0） |
| 4 | `count` | u8 | 玩家记录数，≤ 4（`maxPlayersPerRoom`） |
| 5 | 记录块 | 变长 | `count` 条记录，见下 |

**记录（v1 同序，每条 = 3 + nameLen + 13 字节）**：`pid` u16@0、`nameLen` u8@2、`name` UTF-8@3（**1–12 字节**，即 `minNameBytes`/`maxNameBytes`）、`ready` u8（0/1）、`weapon` u8（0/1/2）、`hpRatio` u8（量化比）、`kills` u16、`mag` u8、`reserve` u16、`reloadLeft10Ms` u8（10ms 单位）、`rage` u8、`rageLeft100Ms` u8（100ms 单位）、`downed` u8（0/1）、`reviveRatio255` u8（0–255）。

> **`kills` 字段来源（已裁决，本批落地）**：线上 `kills` u16 = 该 `pid` 的**真实击杀数** `PlayerStats::kills`（`noteKill` 累加：`server/src/room/stats.cpp:25-26`，调用点 `server/src/room/match_controller.cpp:335-341`）。房间侧组装 MatchState 时按 `pid` 读战绩记录（`buildMatchState` → `playerRecordFor(room, pid)->stats.kills`，`server/src/room/room.cpp:352-356`），**不再**读 v1 遗留、永不累加的 `Session::kills`（只在加入时置 0、重连时照抄）。载荷布局与 13 字节位宽不变。依据：用户裁定 + `server/tests/match_flow_test.cpp`（`matchstate_kills_follow_real_kill_stats`）。

- **单播且可靠**（type 10）：每个客户端各收到一份内容相同的帧；节拍与触发见 S10 §5.8。
- **`hostId` 不上线**（v1 同）：客户端把 `pid` 最小者视为主机。
- 帧长上界：`5 + 4 × (16 + 12) = 117` 字节，远小于 1200 字节单包上限，不分片。
- 名称为空或超过 12 字节、`count > 4`、`weapon > 2` 一律按 `DecodeFailure` 拒收该帧（不得截断后使用）。

### 握手时序（冻结）

```
C→S  Hello{version, clientNonce u32, reconnectToken? }      (type=1, session=0, unreliable)
S→C  HelloAck{session u16, serverTick u32, salt u32}        (type=2, reliable)
C→S  Resume{reconnectToken}                                 (type=3, reliable)   // 仅重连
C→S  Join{nameLen u8, name[nameLen]}                        (type=11, reliable)  // 昵称上报，可重发
S→C  Disconnect{reason u8}                                  (type=8, reliable)
```

> **已裁决（联调轮，2026-09-28 补）：`type = 11 Join` 是「昵称上报」通道。**
>
> **原问题**：客户端侧把身份做成了「按昵称在大厅玩家表里认领 pid」（`client/Assets/Scripts/Net/LocalIdentity.cs`），但**线上没有任何报文能把本地昵称送到服务端**——服务端 `room.cpp` 的 `roomJoin` 在 `session.nameBytes < kNameMinBytes` 时兜底填 `"player"`，而 `setSessionName` 在全仓只有这一处调用点。于是真连时所有玩家在 MatchState 里都叫 `player`，按名认领永远认不出自己（只有恰好输入 `player` 才点亮），相机/HUD 的本地绑定整体落不下来。
>
> **为什么昵称不放进 Hello 载荷**：客户端在 `GameBootstrap` 里**开机即 `transport.Connect(...)`**（首帧之前），而昵称是大厅相位才由玩家键入的（`Lobby` → `ScreenFlow` → `GameLoop.LocalName`）。Hello 是一次性、早于键入的动作，把昵称挂在 Hello 上在**时序上不可行**（除非把连接推迟到「加入」动作之后，那会连带改变「≤5s 进入大厅」的既有行为）。因此新增一条**可重发的独立消息**，而不是扩 Hello。
>
> **载荷（冻结）**：`nameLen u8` + `name[nameLen]`，合计 `2..13` 字节；`name` 是 UTF-8，`1..12` **字节**（与 MatchState 的 `kNameMinBytes/kNameMaxBytes` 同源），由既有 `setSessionName`（`server/src/room/session.cpp`）净化后写入会话。`nameLen` 越界、字节数不足、非 UTF-8 一律 `DecodeFailure`（不得截断使用）。
>
> **时序与幂等**：`Join` 可在 `HelloAck` 之后的任意时刻发送；**重发同一昵称是幂等的**（写同一个会话字段）。客户端在「昵称变化」与「进入 Connected」两个时机各发一次（后者覆盖「先连上、后打字」的顺序）。服务端**不回复**：`pid` 仍由 MatchState（type 10）逐玩家行带出的 `pid` + `name` 认领——这是**沿用既有口径**，不新增回显报文。
>
> **重连不许改身份**：`Join` 不参与身份判定。`Resume` 路径照旧「只按令牌匹配、昵称不参与」（§5.5），且服务端沿用旧会话的昵称与令牌；与此同名的纪律见 `room.cpp` 的 `roomReconnect`。
>
> **不做的事（如实登记，非待办）**：服务端**不强制昵称唯一**，同名多行的歧义由客户端 `LocalIdentity.AmbiguousCount` 如实计数（取最小 `pid`，确定性但不保证正确）。若要消除歧义，需要额外的唯一性裁决或 pid 回显，属后续计划。
>
> **实现口径**：`server/src/net/wire.hpp`（`kJoin = 11`、`kMaxPacketType = 11`）、`server/src/net/codec.hpp/.cpp`（`encodeJoin`/`decodeJoin`）、`server/src/security/validate.cpp`（`isClientToServerType` 增分支）、`server/src/server/runtime.cpp`（`case kJoin` → `setSessionName`，开局后同步战绩记录）、客户端 `Assets/Scripts/Net/JoinCodec.cs` 与 `UdpTransport.SendJoin`。用例：`server/tests/codec_test.cpp`、`server/tests/match_flow_test.cpp`、客户端 `Assets/Tests/ContractSuite.cs`/`IdentitySuite.cs`。

`Disconnect.reason` 冻结枚举：1 `versionMismatch` / 2 `tokenInvalid` / 3 `timeout` / 4 `serverShutdown` / 5 `malformedPacket` / 6 `rateLimited` / 7 `slowConsumer`。重连令牌 `reconnectToken` 是 u32，线上以 **8 位小写十六进制 ASCII** 编码。

## 被否决方案的理由

- **Unity Transport / Netcode for GameObjects**：把可靠性语义与序列化交给包，C++ 侧无法实现同一套私有协议，双端规范无法"同名"。
- **第三方 KCP 库**：违反 ADR-008 的"服务端无第三方运行时库"，且其拥塞控制针对大流量文件传输，对"最新即最优"的快照通道是负担。
- **继续用 TCP/WebSocket**：v1 的缓解手法（小快照、不重传、丢旧包）本质上是在 TCP 上模拟不可靠通道，徒增延迟下界。

## 后果

- 正面：丢包不再阻塞后续快照；可显式控制重传预算；快照通道天然"最新即最优"。
- 负面：需要自研握手、序号、ack 位图、分片与重传（S04/C03 的主要工作量）；NAT 穿透未做（v2 只面向直连/端口映射的服务器，不做 P2P）。
- 安全：会话短 ID + 重连令牌 + 逐包 `session` 校验；命令与事件都要过字段范围校验（S11）。

## 复查触发条件

- 需要 P2P 或 NAT 穿透 → 新 ADR（届时才需要信令）。
- 实测公网丢包 > 10% 且重传开销顶满带宽预算 → 调整 RTO/冗余策略并修订本 ADR 的预算表。
