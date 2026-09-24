# S04 UDP 传输与可靠性子层
> 里程碑：MS04 ｜ 预估：1.5 人日 ｜ 上游移交物：HANDOFF-S03

## 1. 目标
服务端拥有可自证的 `UdpTransport`：真实 UDP 套接字（WinSock2 / POSIX 双实现）之上跑通握手、每通道独立序号、ack 位图、RTO 重传、分片重组、心跳、断线判定与宽限期重连。测试用内存适配器在没有真实网络的情况下可复现丢包/延迟/乱序，后续所有网络集成测试都复用它。

## 2. 入口条件
**入口条件**：HANDOFF-S03
- 事实：包头/扩展头读写与命令包编解码可用。验证：`server/build/ac_tests.exe --filter=codec` → `TESTS 14/14`。
- 事实：会话与重连的身份语义已冻结。验证：`Select-String -Path docs/00-共识/ADR/ADR-006-会话身份与重连令牌.md -Pattern "重连"` → 命中。
- 事实：传输预算与常量来自 [ADR-009](../../00-共识/ADR/ADR-009-UDP传输与协议重构.md)。验证：`Select-String -Path docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md -Pattern "1200"` → 命中单包上限行。
- 事实：构建与断言框架可用。验证：`powershell -NoProfile -File server/build.ps1 -Config Release` → 末行 `[build] ok ac_server.exe`。
- 跨链前置：无。

## 3. 交付物
| 文件 | 状态 | 职责 |
|---|---|---|
| `server/src/net/udp_socket.hpp`、`server/src/net/udp_socket.cpp` | 【新建】 | 非阻塞 UDP 套接字缝：`bind`/`sendTo`/`recvFrom`/`poll`/`close`，WinSock2 与 POSIX 两套实现（`#ifdef _WIN32`） |
| `server/src/net/reliability.hpp`、`server/src/net/reliability.cpp` | 【新建】 | 每通道 seq、可靠消息表、ack 位图、RTO 重传、重传上限与断开判定 |
| `server/src/net/fragment.hpp`、`server/src/net/fragment.cpp` | 【新建】 | 分片发送与重组（组 ID、序号、上限 8、60 tick 超时丢弃） |
| `server/src/net/handshake.hpp`、`server/src/net/handshake.cpp` | 【新建】 | Hello/HelloAck/Resume/Disconnect 的状态机与超时 |
| `server/src/net/keepalive.hpp`、`server/src/net/keepalive.cpp` | 【新建】 | 500ms 心跳、`ackOnly` 包、3s 断线判定、30s 宽限期计时 |
| `server/src/net/memory_transport.hpp` | 【新建】 | 测试用内存适配器：丢包率、单向延迟与抖动、乱序、按毫秒推进的 `pump` |
| `server/tests/transport_test.cpp` | 【新建】 | 环回集成：握手、重传、分片、心跳、断线与宽限期重连 |

## 4. 任务清单
- [ ] 1. 创建 `server/src/net/udp_socket.hpp` 与 `server/src/net/udp_socket.cpp`：非阻塞收发 + `poll` 1ms 超时，WinSock2 与 POSIX 分支各一套。
- [ ] 2. 创建 `server/src/net/reliability.hpp` 与 `server/src/net/reliability.cpp`：按 §5.3 实现 seq/ack 位图/重传表/RTO 序列与断开。
- [ ] 3. 创建 `server/src/net/fragment.hpp` 与 `server/src/net/fragment.cpp`：按 §5.4 实现分片发送、重组缓冲与超时回收。
- [ ] 4. 创建 `server/src/net/handshake.hpp` 与 `server/src/net/handshake.cpp`：按 §5.5 实现四种握手包与 1s/5 次重发预算。
- [ ] 5. 创建 `server/src/net/keepalive.hpp` 与 `server/src/net/keepalive.cpp`：按 §5.6 实现心跳、断线判定与宽限期进入/退出。
- [ ] 6. 创建 `server/src/net/memory_transport.hpp`：按 §5.7 的 API 实现可控丢包/延迟/乱序的内存总线。
- [ ] 7. 创建 `server/tests/transport_test.cpp`：8 个场景（握手、丢包重传、乱序去重、分片、心跳、3s 断线、宽限期重连、令牌校验失败）。
- [ ] 8. 补 §6 的 RTO 序列与分片上限断言，并跑通 `--filter=transport`。
- [ ] 9. 把“传输常量表”抄进 [server/README.md](../../../server/README.md) 的契约段，供客户端对照实现。

## 5. 冻结契约
### 5.1 常量表（逐字对齐 ADR-009）
| 常量 | 值 | 说明 |
|---|---|---|
| `kInitialRtoMs` | 200 | 首次重传等待 |
| `kRtoBackoff` | 1.5 | 退避倍数 |
| `kMaxRtoMs` | 1000 | RTO 上限 |
| `kMaxRetransmits` | 5 | 累计重传达 5 次即判对端失联并进入宽限期，即**第 6 次重传之前**判失联（与 `kRtoTableMs` 的 5 项同长） |
| `kRtoTableMs[5]` | `{200, 300, 450, 675, 1000}` | 第 1–5 次重传的等待（`200 * 1.5^(n-1)` 取整，禁 `pow`）；表长恒等于 `kMaxRetransmits`，第 6 次重传不存在 |
| `kKeepAliveMs` | 500 | 心跳周期 |
| `kDisconnectMs` | 3000 | 未收到任何包即判断线 |
| `kGraceMs` | 30000 | 宽限期，期间保留会话与房间名额 |
| `kMaxPacketBytes` | 1200 | 单包上限（含全部头） |
| `kMaxFragmentPayload` | 1176 | 分片载荷上限 = 1200 − 8 − 12 − 4 |
| `kMaxFragments` | 8 | 分片总数上限；逻辑消息 ≤ 9408 字节 |
| `kFragmentTimeoutTicks` | 60 | 分片组 3s（60 tick）未收齐即丢弃 |
| `kOutboundBacklogBytes` | 65536 | 出站积压上限；达到时丢最旧快照，事件不丢 |
| `kSnapshotBudgetBytes` | 1228 | 稳态快照目标上限 |
| `kClientBandwidthBytesPerSec` | 40960 | 每客户端 40 KB/s（口径 = 全部出站 UDP 载荷，含事件） |
| `kSessions` | 256 | 同时在册会话上限（含宽限期会话） |

### 5.2 会话短 ID 与校验
- 取值 1..65535，`0` 保留给握手前的包；分配器 `next = (next % 65535) + 1`，命中在用 ID 时继续 +1（最多试 65535 次）。
- 每会话独立：`CommandChannel` / `EventChannel` / `SnapshotChannel` 各一条发送 seq 与一条接收 seq（`uint16`，回绕 mod 2^16）。
- 校验：`type != Hello` 的包必须有 `session != 0` 且等于本端点上绑定的会话 ID，否则丢弃并计 `kBadSession`；`Hello` 必须 `session == 0`；`version != 1` 一律回 `Disconnect(reason=1)`。
- 会话生命周期：`Alloc → Connected → GracePeriod(≤30s) → Released`；`Released` 后 ID 可被复用（复用后旧令牌失效）。

### 5.3 序号、ack 位图与重传
```cpp
struct ReliableState { uint32_t sendMsgId = 1; uint32_t ackBase = 0; uint32_t ackBits = 0; };
// 收到 msgId = m 时（m 从 1 起）：
//   k = m - ackBase;
//   if (k >= 1) { ackBits = (k >= 32 ? 0u : (ackBits << k)); ackBits |= (1u << (k - 1)); ackBase = m; }
//   else { d = ackBase - m; if (d >= 1 && d <= 32) ackBits |= (1u << (d - 1)); }
```
- bit i 表示 `msgId == ackBase - 1 - i` 已收到；`k >= 32` 时左移先清零（避免移位宽度未定义）。
- 重复 `msgId`（位图已置位）直接丢弃，不计入错误；`msgId` 与通道 `seq` 相互独立：`seq` 用于丢旧快照与顺序诊断，`msgId` 用于去重与 ack。
- 重传：仅 `flags.reliable = 1` 且载荷 > 0 的消息进重传表；第 n 次重传等待 `kRtoTableMs[n-1]`（n = 1..5）；累计重传达到 `kMaxRetransmits = 5`（即第 6 次重传之前）→ 判失联 → 进入宽限期并回 `Disconnect(reason=3)`。
- 收到 ack（`msgId` 被位图确认）即从重传表移除；`ackOnly` 包参与确认，自身不进重传表。

### 5.4 分片
- 发送：逻辑消息 > `kMaxFragmentPayload` 时切成 ≤ `kMaxFragments` 片，每片 = 通用包头 + 分片头 + 1176B 载荷切片，`type = 9`；快照分片不可靠，事件分片可靠。
- 重组：键 = `(session, channelType, fragId)`；按 `fragIndex` 落位，`fragCount > 8` 或 `fragIndex >= fragCount` 立即判 `kBadValue` 丢弃整组。
- 收齐后整体交给上层解码；组超时 60 tick 未收齐则丢弃并计 `kFragmentTimeout`。

### 5.5 握手字段与时序
| 包 | type | 通道 | 载荷 |
|---|---|---|---|
| `Hello` | 1 | 不可靠，`session = 0` | `clientNonce` u32、`reconnectToken` u32（首次连接填 0） |
| `HelloAck` | 2 | 可靠（`flags.reliable`） | `serverTick` u32、`salt` u32（取自会话层**独立计数器**；不得消费 `ai`/`spawn`/`fx` 任一 RNG 流，否则对拍不可复现） |
| `Resume` | 3 | 可靠 | `reconnectToken` u32 |
| `Disconnect` | 8 | 可靠 | `reason` u8（1 versionMismatch / 2 tokenInvalid / 3 timeout / 4 serverShutdown / 5 malformedPacket / 6 rateLimited / 7 slowConsumer），七个取值的语义见下 |

时序（冻结）：客户端发 `Hello` 后启动 1s 定时器，未收到 `HelloAck` 则重发，累计 5 次后放弃并报错；服务器收到合法 `Hello` 立即分配会话 ID、记录 `clientNonce`、由会话层独立计数器生成 `salt`（不消费任何 RNG 流）并回 `HelloAck`（`session` 填分配值）；客户端收到 `HelloAck` 才进入 `Connected`。
重连令牌规则：`reconnectToken = salt ^ clientNonce`（客户端本地计算，服务器按会话记录复算比对）；宽限期内 `Resume` 校验通过则恢复会话（服务器随后发一次 `baselineTick = 0` 的全量快照），失败或超期回 `Disconnect(reason=2)` 并释放名额。

`Disconnect.reason` 语义（冻结 1–7，与客户端逐字一致）：
- 1 `versionMismatch`：入站包 `version != 1`（§5.2）。
- 2 `tokenInvalid`：`Resume` 的 `reconnectToken`（u32）与会话记录不符，或宽限期已过。
- 3 `timeout`：可靠消息累计重传达到 `kMaxRetransmits = 5`（第 6 次重传之前）判失联（§5.3）。
- 4 `serverShutdown`：对局结束或服务端停机。
- 5 `malformedPacket`：解码失败（`kTruncated`/`kBadLength`/`kBadValue`/`kUnknownEvent`）计数达阈值后丢弃会话（工程约定 §7）。
- 6 `rateLimited`：单位时间入站包数或字节数超过配额。
- 7 `slowConsumer`：出站积压达到 `kOutboundBacklogBytes = 65536` 且已无可丢快照，主动断开会话（§5.1、§5.6）。

### 5.6 心跳、断线判定与宽限期
- 每 500ms 发送一个 `KeepAlive`（`flags = reliable|ackOnly`，载荷 0 字节，不进重传表）；收到任意合法包即刷新 `lastRecvMs`。
- `now - lastRecvMs >= 3000` → 判断线：停发该会话、进入宽限期 30s、世界侧把该玩家置 `idle`（不删实体）。
- 宽限期内允许 `Resume` 重连；30s 到期 → 释放 session ID 与房间名额，实体按房间规则清理。

### 5.7 内存适配器 API（供后续网络集成测试复用）
```cpp
struct EndpointId { uint32_t v; };
class MemoryTransport {
 public:
  void setLossRate(double rate);                       // 0..1，按 fx 流抽样
  void setLatencyMs(uint32_t base, uint32_t jitter);   // 单向延迟与抖动
  void setReorderRate(double rate);                    // 相邻包交换概率
  void send(EndpointId from, EndpointId to, std::span<const std::uint8_t> bytes);
  void pump(uint32_t nowMs, const std::function<void(EndpointId, std::span<const std::uint8_t>)>& onDeliver);
  std::size_t droppedCount() const;                    // 断言丢包确实发生
};
```
生产侧缝：`ac::net::UdpSocket` 暴露 `bool bind(uint16_t port)`、`int sendTo(const Endpoint&, std::span<const std::uint8_t>)`、`int recvFrom(Endpoint&, std::span<std::uint8_t>)`、`bool poll(int timeoutMs)`、`void close()`。

## 6. 验证
| 命令 | 期望输出 | 失败意味着什么 |
|---|---|---|
| `server/build/ac_tests.exe --filter=transport` | `TESTS 8/8` | 握手/重传/分片/心跳/宽限期任一环节不成立 |
| `server/build/ac_tests.exe --filter=reliability` | 打印 `rto=200,300,450,675,1000` 且 `TESTS 5/5` | RTO 序列偏离冻结值，重传节奏与客户端不一致 |
| `server/build/ac_tests.exe --filter=fragment` | `TESTS 4/4`（5000B 消息重组、超 8 片拒绝、乱序收齐、60 tick 超时） | 分片重组有缺陷，大快照会丢包或挂起 |
| `server/build/ac_tests.exe --filter=grace` | `TESTS 4/4`（3s 断线、30s 前 Resume 成功、30s 后释放、令牌错误拒绝） | 宽限期语义错误，重连丢位置或名额泄漏 |
| `server/build/ac_tests.exe --filter=memory` | `TESTS 2/2`（丢包率生效：`dropped > 0`，可靠消息最终全部到达且顺序正确） | 测试适配器不真丢包，网络测试形同虚设 |
| `Get-ChildItem server/src/net -Recurse -Include *.hpp,*.cpp \| Select-String -Pattern "std::pow","exp\(","\b0\.0[0-9]* \* pow"` | 0 命中 | RTO 退避用了超越函数，违反 [ADR-010](../../00-共识/ADR/ADR-010-跨语言确定性与对拍.md) 运算子集 |
| `node tools/check-docs.mjs` | 末行以 `OK：` 开头，退出码 0 | 文档链或相对链接被破坏 |
| `node tools/check-assets.mjs` | 退出码 0 | 引入了素材或白名单外的依赖 |

## 7. DoD（验收标准）
- [ ] 五个模块（socket/reliability/fragment/handshake/keepalive）+ 内存适配器全部落地，`server/src/net/**` 无第三方 include。
- [ ] `--filter=transport`、`reliability`、`fragment`、`grace`、`memory` 五组全绿，合计用例 ≥ 23。
- [ ] 20% 丢包 + 50ms 延迟 + 10% 乱序下，1000 tick 的可靠消息全部到达且按 `msgId` 升序交付。
- [ ] 重传序列打印为 `200,300,450,675,1000`（5 项），第 6 次重传之前进入宽限期。
- [ ] 5000 字节逻辑消息经 5 片重组成功；> 8 片被拒且不崩溃。
- [ ] 心跳间隔实测 500ms（±1 tick），3s 无包判断线，30s 内 `Resume` 成功、超期释放。
- [ ] 本份未修改 `docs/00-共识/**`（共识层冻结，如需变更先走 ADR）；备份仓库 `D:\projects\tmp\angry-chen-bak` 全程只读；`node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。

## 8. 风险与回滚
| 风险 | 触发信号 | 对策 |
|---|---|---|
| WinSock 与 POSIX 行为差异（错误码、`poll` 语义） | CI（Linux）通过而本机（Windows）失败 | 两套实现共享同一接口测试；错误码统一映射到内部枚举后再判断 |
| RTO 表与真实 RTT 严重不匹配 | soak 中重传率 > 5% | 只调 `kRtoTableMs` 的整数值并同步客户端，仍禁用浮点退避 |
| 分片与可靠扩展头顺序歧义 | 客户端重组出的字节与 fixture 不符 | §5.4 已冻结顺序与偏移公式；两侧用同一批 fixture 校验 |
| 宽限期会话占满 256 个槽位 | 新客户端被拒（`kSessions` 打满） | 超期会话优先回收；`kSessions` 不足时按最早进入宽限期的会话驱逐 |
| 心跳包与 ack 混淆导致无限重传 | 出站队列只增长不下降 | `ackOnly` 包不进重传表（§5.3），并有断言守护 |

回滚目标：`HANDOFF-S03`——删除 `server/src/net/{udp_socket,reliability,fragment,handshake,keepalive,memory_transport}*` 与 `server/tests/transport_test.cpp`；编解码与 fixture 不动，`node tools/check-docs.mjs` 仍全绿。

## 9. 移交物
**移交物 ID**：HANDOFF-S04
- 稳定接口：`ac::net::UdpSocket`；`ReliableState` 与 ack 位图算法；`Fragmenter`/`Reassembler`；`HandshakeState` 与四种握手包字段；`KeepAliveTimer`/宽限期状态；`MemoryTransport`（丢包/延迟/乱序可编程）。
- 冻结数据：§5.1 常量表（RTO 表、心跳 500ms、断线 3s、宽限 30s、单包 1200B、分片上限 8、逻辑消息 ≤ 9408B）、§5.2 会话 ID 规则、§5.5 握手字段与重连令牌公式 `salt ^ clientNonce`、Disconnect reason 1–7。
- 已验证能力：丢包/延迟/乱序下可靠交付、分片重组、心跳与断线、宽限期重连与令牌校验（§6 全部实测通过）。
- 消费约定：客户端链按同一常量表与握手时序实现，并用同一套内存适配器参数复现；任何常量变更必须双端同步并重跑 §6。
