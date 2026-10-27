# 公网生产服压测证据（43.143.120.65）

> 本文是本轮对**已在跑的公网生产实例**的压测原始记录：环境、命令、逐秒采样序列、四个跑次的结果与判定，
> 以及 soak 跑次暴露出的一个**功能性缺陷**（比赛无法开局）与代码级定位。
> 门槛数值沿用 `docs/plans-v2/server/S14-压测基准与性能守门.md` §5 的冻结表（G1–G8），**未放宽任何限值**。

## 0. 结论摘要

| 项 | 结果 |
|---|---|
| 性能（4 人满负载，2 分钟） | **全部达标**：单核 1.82%、每客户端 22.5 KB/s、快照均值 1091.5 B、零丢弃/零跳过 |
| 性能（5 分钟 soak） | 单核 1.63%、**RSS 271 秒恒定 0 增长**、零异常 —— 但比赛没开局 |
| **功能缺陷（严重）** | soak 跑次中 4 名玩家全员就绪、25,749 条命令帧全部合法，**5 分钟内比赛从未开局**；单 bot 则立刻开局 |
| 根因（代码级定位） | `ensureMatchRunning` 把「数组第一个会话」当房主交给 `tryStartMatch`，而后者要求 `session.pid == hostId`（**最小 pid**）；两者在实体 id 被回收后不再相等 → 永久 `kNotHost` |
| 影响面 | 任何**已经打过比赛的长跑服务器**：老玩家离开后新一组玩家进来，永远卡在大厅 |

## 1. 环境与部署指纹

| 项 | 值 |
|---|---|
| 主机 | 43.143.120.65（Ubuntu，4 vCPU，3.7 GB 内存；同机还有 nginx / docker / 1panel，实测 1.7 GB 已用） |
| 服务 | `angry-chen-server --serve --udp-port=8788 --http-port=8787`，systemd 单元 `angry-chen-server.service`，PID 1480108 |
| 启动时间 / 已运行 | 2026-10-11 00:16:23 ／ 采样时约 1 天 17 小时 |
| 版本指纹 | `ac_server 0.1.0 protocol=2 tick=50ms`；`/health` → `"protocolVersion":2` |
| 二进制 SHA256 | `11e9d6fe7738e0502686688360472d575287ff0e8331bc144156ecdffe64fbf7` |
| 仓库 HEAD | `9aea70b`（2026-10-10，S16）→ 部署物（10-11 00:16）**晚于 HEAD**，判定为同版本代码 |
| 监听面 | UDP 8788 绑 `0.0.0.0`（对外可达）；**HTTP 8787 只绑 `127.0.0.1`**，外网直连 8787 返回 502，正常入口是 nginx `:80` 反代 |
| 负载端 | 本机 Windows `server/build/ac_bot.exe`（protocol=2）经**真实公网**打 UDP 8788 |
| 采样端 | SSH 到主机，1 Hz 读 `/proc/<pid>/stat` 的 `utime+stime`（`HZ=100`）与 `/proc/<pid>/status` 的 `VmRSS` |
| 计时粒度 | `HZ=100` → 单次采样分辨率 1%（10 ms/tick），故 CPU 读数呈 0.99 / 1.99 / 2.99 的量化台阶 |

ICMP 被云安全组拦截（`ping` 100% 丢包），TCP/UDP 正常。

## 2. 命令

```powershell
# 负载（本机 → 公网）
.\server\build\ac_bot.exe --players 4 --minutes 2 --host 43.143.120.65 --port 8788
.\server\build\ac_bot.exe --players 4 --minutes 5 --host 43.143.120.65 --port 8788
.\server\build\ac_bot.exe --players 1 --minutes 0.4 --host 43.143.120.65 --port 8788
```

```bash
# 采样（主机上，1 Hz 落盘）
PID=$(systemctl show -p MainPID --value angry-chen-server)
awk '{print $14, $15}' /proc/$PID/stat; awk '/^VmRSS/{print $2}' /proc/$PID/status
curl -s http://127.0.0.1:8787/metrics ; curl -s http://127.0.0.1:8787/health
```

## 3. 跑次 A：4 人 + 服务端波次羊群，120 秒（≈ 计划 §5 的 `gate-4p2min` 口径）

负载侧（4 个 bot 各自一条套接字）：

```
bot session=4 packetsIn=5331 packetsOut=2577 bytesIn=2681550 bytesOut=87604 snapshots=2390 matchStates=140 commands=2576 hasSession=1
bot session=7 packetsIn=5331 packetsOut=2577 bytesIn=2681467 bytesOut=87604 snapshots=2391 matchStates=139 commands=2576 hasSession=1
bot session=6 packetsIn=5332 packetsOut=2577 bytesIn=2680475 bytesOut=87604 snapshots=2390 matchStates=140 commands=2576 hasSession=1
bot session=5 packetsIn=5334 packetsOut=2577 bytesIn=2681615 bytesOut=87604 snapshots=2391 matchStates=140 commands=2576 hasSession=1
```

服务端逐秒采样（`/proc`，单核百分比）：

| 阶段 | 样本 | 均值 | P95 | 最大 |
|---|---|---|---|---|
| 空载基线 | 24 | **1.28%** | 1.99% | 1.99% |
| 4 人满载 | 121 | **1.82%** | 1.99% | 2.99% |

- 满载 121 秒共消耗 **2.21 秒 CPU**；RSS 由 4520 kB 一次性涨到 **5624 kB**（+1.08 MB，开局分配），此后恒定。
- 服务端计数器差分（`/metrics` 前后）：出站 11,040,664 B、入站 350,416 B、快照帧 9,838、事件 1,726 条。
- 快照实体记录均值 **75.06**（4 玩家 + 约 71 只羊/投射物）—— 略高于文档的「60 羊」设计点。

| 门槛 | 限值 | 实测 | 判定 |
|---|---|---|---|
| G1 单核占用（4 人 + 羊群） | < 30% | **1.82%** | pass（**16× 余量**） |
| G2 每客户端出站带宽 | ≤ 40 KB/s | **22.5 KB/s**（bot 侧 21.8） | pass（1.78× 余量） |
| G3 快照帧字节 P95 | ≤ 1228 B | 均值 **1091.5 B**、峰值 1304 B | pass（均值口径；P95 未由指标暴露，见 §6 存疑） |
| G4 单快照帧字节上限 | ≤ 2048 B | **1304 B** | pass |
| G5 硬纠正 | ≤ 5 次/分/人 | **0** | pass |
| G6 tick 调度误差 | 见 §5 替代判据 | `sim_drift` −17→**−1 ms**，`tick_interval_error_p95` 13 ms | pass（替代判据） |
| G7 RSS 增长斜率 | < 1 MB/分 | **0**（开局后恒定） | pass |
| G8 事件丢弃/异常/tick 跳过 | 0/0/0 | **0/0/0**，journal 无 error | pass |

其他全零异常计数：`malformed` / `oversized` / `rate_limited` / `pose_rejected` / `pose_suspect` / `speed_violations` / `rewind_clamped` / `slow_client_drops` / `room_budget_exceeded` / `snapshot_rate_downshifts` / `send_queue_bytes`。
唯一非零：`ac_dropped_frames_total` +13（占入站 10,308 帧的 **0.13%**），语义是通用「入站帧被拒」（重复/迟到/会话前帧等，见 `server/src/server/runtime.cpp:453` 记录的「Ready 位第一帧被当重复」），非异常。

`ac_tick_work_ms_p95` 由空载 0.105 ms 升到满载 **0.219 ms**（P99 0.283 ms）—— 20 Hz 预算 50 ms，占用 **0.44%**。

**比赛确实在跑**：`matches.ndjson` 本轮新增 2 场（63.35 s 与 46.5 s，均 4 人，`waveReached=1`），战绩常驻数 203 → 205。

## 4. 跑次 B：4 人 300 秒 soak —— 性能达标，但比赛没开局

负载侧：

```
bot session=10 packetsIn=13293 bytesIn=330959 snapshots=5999 matchStates=304 commands=6438 hasSession=1
bot session=11 packetsIn=13296 bytesIn=331019 snapshots=5999 matchStates=304 commands=6438 hasSession=1
bot session=8  packetsIn=13294 bytesIn=331040 snapshots=5999 matchStates=305 commands=6438 hasSession=1
bot session=9  packetsIn=13294 bytesIn=331040 snapshots=5999 matchStates=305 commands=6438 hasSession=1
```

**异常信号**：每个 bot 只收到 **331 KB**（跑次 A 是 2.68 MB），5999 个快照 ⇒ 平均每个快照仅 **55 B**。

服务端指标证实世界是空的：

| 指标 | 跑次 A | 跑次 B（soak） |
|---|---|---|
| `ac_snapshot_records_avg` | 75.06 | **4.000**（只有 4 个玩家实体） |
| `ac_snapshot_bytes_avg` | 1091.5 B | **25.0 B** |
| `ac_events_sent_total` 增量 | +1,726 | **+20** |
| `ac_records_retained` | +2 | **0（没有任何一场比赛跑完）** |

采样（1 Hz，271 个样本）：

| 阶段 | 样本 | 均值 | P95 | 最大 |
|---|---|---|---|---|
| 4 人 soak | 269 | **1.63%** | 1.99% | 2.99% |

- 271 秒共 **4.38 秒 CPU**。
- **RSS：271/271 个样本全部等于 5624 kB，增长为 0** —— 无泄漏。
- 零异常：`events_dropped` / `tick_skips` / `malformed` / `oversized` / `rate_limited` / `pose_*` / `hard_correct` / `downshifts` / `slow_client_drops` 全部 0；`dropped_frames` 在 soak 期间**一条都没涨**；journal 无任何条目。
- 入站命令帧 25,749 条**全部合法**（4 × 6,438，与 `ac_frames_in_total` 增量一致）。

也就是说：**性能毫无问题，但功能上这 5 分钟是空转** —— 4 个玩家连上了、就绪了，比赛永远没开始。

## 5. 判别实验：单 bot 立刻开局

在同一台服务器、同一份部署上，从空大厅只放 **1 个 bot**（24 秒）：

```
mid-run: {"connections":1,"players":1,"graceActive":0}  ticks=10798
ac_snapshot_records_avg 58.849     (世界满了：羊群已生成)
ac_snapshot_bytes_avg   851.620
ac_events_sent_total    2368 → 2386   (+18，波次事件)
ac_records_retained     206 → 208     (24 秒内跑完 2 场比赛)
```

单会话时 `sessions[0].pid` **必然**等于最小 pid，于是开局链路完全正常。结合跑次 B（4 人 5 分钟一次都没开局），故障被锁定在**多玩家时的房主选择**上。

## 6. 代码级定位

开局**只有一条路径**（全仓库 grep：`tryStartMatch` 仅一处调用）：

```
server/src/server/runtime.cpp:582   pollOnce → ensureMatchRunning(nowMs)
server/src/server/runtime.cpp:682   Runtime::ensureMatchRunning
server/src/server/runtime.cpp:691-701
    for (i = 0 .. kMaxPlayersPerRoom) { if (!isConnected) continue; if (host == nullptr) host = session; }
    if (host != nullptr) tryStartMatch(*room_, *host);      // ← 传的是「数组里第一个已连接会话」
server/src/room/match_controller.cpp:78
    if (session.pid == 0u || session.pid != room.match.hostId) return StartOutcome::kNotHost;
server/src/room/room.cpp:24-31
    refreshHostId(): room.match.hostId = 「所有会话里 pid 的最小值」
```

三处事实叠加成缺陷：

1. **pid = 实体 id，来自 LIFO 回收栈**：`server/src/sim/entity_table.hpp:186-191`（`if (freeCount > 0) id = freeIds[--freeCount]; else id = ++highWater;`）。羊/投射物被回收后，新玩家拿到的 id 可能很大。
2. **`sessions[]` 是加入顺序**：`roomJoin` 追加到末尾（`room.cpp:274-275`），`removeMember` 左移数组（`room.cpp:353-357`）。
3. **交给 `tryStartMatch` 的却是 `sessions[0]`**，而它要求这个会话必须是 `hostId`（最小 pid）。

**全新世界**里 `spawnEntity` 依序发出 1、2、3、4，加入顺序恰好等于 pid 顺序 ⇒ `sessions[0].pid == 1 == hostId`，判据**碰巧成立**。所以：
- 所有单测、`ac_gate` 四场景都在**新起的进程**上跑 ⇒ 全部通过；
- 一旦服务器已经打过比赛（id 被回收），加入顺序与 pid 顺序解耦 ⇒ `ensureMatchRunning` 每 tick 都拿到非房主 ⇒ **`kNotHost` 永久返回，比赛永不开始**。

**排除法确认相位是 kLobby**：
- 不是 `kPlaying` —— 跑次 A 证明 `kPlaying` 会生成羊群（records_avg 75），而 soak 是 4.000 且 5 分钟零羊零事件；
- 不是 `kLoading` —— 只有 1500 ms（`match_controller.cpp:83`）；
- 不是 `kIntermission` —— 该相位接受 ready，全员就绪会把 `intermissionMs` 清零并进 `kPlaying`（`match_controller.cpp:143`），而 4 个 bot 每条命令都带 ready；
- ⇒ 相位是 `kLobby`；4 人全员 ready（`areAllPlayersReady` 成立）⇒ `tryStartMatch` 唯一失败的前置条件就是 **`session.pid != hostId`**。

**测试为什么漏掉**：夹具 `Harness` 用 `room::createSession(i + 1u, nowMs)` 建会话（`server/tests/match_flow_test.cpp:77-86`），并在 `:366` 断言 `room->match.hostId == 1u` —— 夹具**恒为**「sessions[0] 即最小 pid」，正是缺陷所依赖的那个不变量。全套 468 个用例结构上不可能覆盖「加入顺序 ≠ pid 顺序」。仓库也只测了 `tryStartMatch` 的 `kNotHost` 方向（`:392`），没测 `ensureMatchRunning` 的房主选择。

**可观测性缺口**：这条失败是**静默**的 —— 没有任何指标/日志记录「开局被拒」。`room.match.counters.deniedTransitions` 只统计相位跃迁被拒，不含 `StartOutcome`。本轮只能靠「快照字节骤降」这种间接信号发现。

### 建议修法（未实施，待裁决）

1. **最小改动**：`ensureMatchRunning` 里按 `room.match.hostId` 查会话并交给 `tryStartMatch`（`sessionByPid(*room_, hostId)`），而不是取 `sessions[0]`；
2. 或：服务端驱动路径放宽 `tryStartMatch` 的房主前置；
3. **必须同时补测试**：构造「加入顺序 ≠ pid 顺序」的房间，断言 `ensureMatchRunning` 仍能开局（否则回归会再次隐身）；
4. 建议把 `StartOutcome != kOk` 计入指标（如 `ac_match_start_denied_total{reason=not-host}`），让这类静默卡死可见。

## 7. 存疑与未验证

- **G3 的口径**：本轮开了约 75 条实体记录（高于文档的 60 羊设计点），快照**均值** 1091.5 B、**峰值** 1304 B；`/metrics` 不暴露快照 P95，故无法与 1228 B 的 P95 门限逐位对比 —— **峰值已越过 P95 门限但仍低于 2048 B 的硬上限**。需在 60 羊设计点上用 `ac_gate` 复测才能给 P95 定性。
- **部署物与源码同版本的判定**基于「部署时间晚于 HEAD 提交」+ 协议号一致，**未做二进制与源码的逐位比对**。
- **单机回环噪声**：负载端在本地 Windows 经公网发出，主机上同时跑着 nginx/docker/1panel；CPU 读数分辨率仅 1%（`HZ=100`），亚 1% 的差异不可辨。
- **未测**：>4 人的准入拒绝（`kMaxPlayersPerRoom` 满员路径）、丢包/抖动场景（`ac_bot --loss/--latency`）、客户端侧观感。

## 8. 本轮对生产实例的改动

- 只做了**连接与读取**，未改任何配置、未重启服务、未动数据文件。
- 战绩文件因压测新增 5 场记录（`recordsRetained` 203 → 208），属正常游戏产物。
- 收尾状态：`connections=0 players=0 graceActive=0`，世界仍在跑一场无人的比赛（与我接手时的状态一致）。
