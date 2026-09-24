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
| 1 | `type` | u8 | 1 Hello / 2 HelloAck / 3 Resume / 4 Command / 5 Snapshot / 6 Event / 7 KeepAlive / 8 Disconnect / 9 Fragment / 10 MatchState（reliable，单播） |
| 2 | `flags` | u16 | bit0=reliable，bit1=moreFragments，bit2=ackOnly |
| 4 | `session` | u16 | 会话短 ID（服务器分配，握手前的包为 0） |
| 6 | `seq` | u16 | **每通道独立**递增序号（回绕模 2^16） |

### 可靠包扩展头（`flags.reliable = 1` 时紧随通用包头，12 字节）

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 8 | `msgId` | u32 | 可靠消息唯一 ID，接收侧据此去重 |
| 12 | `ackBase` | u32 | 已收到的最大 `msgId` |
| 16 | `ackBits` | u32 | `ackBase` 之前的 32 位位图（1 = 已收到） |

### 分片头（`flags.moreFragments = 1` 时紧随通用包头，4 字节）

> 勘误（C02 施工时发现，2026-09-24）：原表述的第二个条件「或 `fragCount > 1`」不可判定——读通用包头时还看不到 `fragCount`，条件自相矛盾；与 S03 §5.1 的 `payloadOffset` 公式、`wire.hpp` 与客户端 `PacketReader.Read` 的实现（只看 `flags` 位 1）均不符，故删去。

| 偏移 | 字段 | 类型 | 说明 |
|---|---|---|---|
| 8 | `fragId` | u16 | 同一逻辑消息的分片组 ID |
| 10 | `fragIndex` | u8 | 分片序号（0 起） |
| 11 | `fragCount` | u8 | 分片总数（上限 8） |

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

- **单播且可靠**（type 10）：每个客户端各收到一份内容相同的帧；节拍与触发见 S10 §5.8。
- **`hostId` 不上线**（v1 同）：客户端把 `pid` 最小者视为主机。
- 帧长上界：`5 + 4 × (16 + 12) = 117` 字节，远小于 1200 字节单包上限，不分片。
- 名称为空或超过 12 字节、`count > 4`、`weapon > 2` 一律按 `DecodeFailure` 拒收该帧（不得截断后使用）。

### 握手时序（冻结）

```
C→S  Hello{version, clientNonce u32, reconnectToken? }      (type=1, session=0, unreliable)
S→C  HelloAck{session u16, serverTick u32, salt u32}        (type=2, reliable)
C→S  Resume{reconnectToken}                                 (type=3, reliable)   // 仅重连
S→C  Disconnect{reason u8}                                  (type=8, reliable)
```

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
