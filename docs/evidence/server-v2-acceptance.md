# 服务端 v2 验收报告（S15 §7）

范围：`docs/plans-v2/server/S15-发布运维与验收.md`。固定点：S14 提交 `cdf1353`（= 本步改动的父提交）。
发布版号 **0.1.0**，协议版本 **1**。

## 1. 交付物落盘

| 文件 | 状态 | 说明 |
|---|---|---|
| `server/CMakeLists.txt` | 改 | `AC_SERVER_VERSION` 注入（默认 0.1.0）、`install(PROGRAMS $<TARGET_FILE:ac_server> DESTINATION bin RENAME angry-chen-server)`（实测 `cmake --install --prefix build/acprefix` 产出 `bin/angry-chen-server.exe`；构建产物仍叫 `ac_server.exe`，`build.ps1` 的产物断言与计划 §6-1 的命令都不受影响）、Release 仍为 `-O2 -DNDEBUG` + `-ffp-contract=off -fno-fast-math -Werror` |
| `server/src/core/version.hpp` | 改 | `kVersion` 改为读构建注入宏（发布口径的唯一来源；`#ifndef` 回落只服务 g++ 直编兜底） |
| `server/src/persist/match_store.cpp` | 改 | `AC_DATA_DIR` 未设时：POSIX → `/var/lib/angry-chen`（§5 冻结），Windows → `data`（平台差异见 README §18.8-1） |
| `deploy/angry-chen-server.service` | 新建 | §5 的 Unit/Service 字段逐条对齐（`Type=simple`、`User=angrychen`、`Restart=on-failure/RestartSec=3`、`KillSignal=SIGTERM`、`TimeoutStopSec=10`、六项加固、stdout/stderr 落盘） |
| `deploy/Caddyfile` | 新建 | 只转发 `/health`、`/metrics`、`/api/*`，其余 404 + `Cache-Control: no-store` |
| `deploy/nginx.conf` | 新建 | 同上；注明 UDP 8788 不经反代、由防火墙放行 |
| `server/README.md` §18 | 改 | 目录布局、端口与端点、环境变量、日志轮转、五条排障命令、退役判定清单、部署步骤 |
| 本文件 | 新建 | 验收报告 |

## 2. §6 验证 1–7 的实测输出

| # | 命令 | 期望 | 实测 |
|---|---|---|---|
| 1 | `cmake --build server/build`（Release）+ `ac_server --version` | `ac_server 0.1.0 protocol=1 tick=50ms` | ✅ 逐字一致，退出码 0 |
| 2 | `ac_server --serve --http-port 8799 --udp-port 8798 --data-dir build/acvar`（**非默认端口**才证明参数生效；空格写法与 `--data-dir` 由本步补齐，见 §1） | 200 / 200 | ✅ `health=200`、`metrics=200`（都在 8799），`build/acvar` 已创建 |
| 3 | `GET /nope` | 404 | ✅ `nope=404` |
| 4 | `Select-String -Path deploy/Caddyfile,deploy/nginx.conf -Pattern 'file_server|try_files|root '` | 合计 0 | ✅ `0`（注释里也刻意不出现这些词；计划原文的 `\|` 是 .NET 正则的字面竖线，恒 0 命中，属计划文本缺陷，见 README §18.8-11） |
| 5 | `Select-String -Path deploy/angry-chen-server.service,deploy/nginx.conf,server/README.md -Pattern 'AC_UDP_PORT|8788'` | 三处都出现且端口一致 | ✅ service 2 处、nginx.conf 3 处、README 12 处，全部 8788/UDP + 8787/HTTP |
| 6 | `node tools/check-docs.mjs` / `node tools/check-assets.mjs` | 退出码 0 | ✅ `OK：v2 30 份计划（S/C 链） + 10 份前置文档…`；`OK：仓库零外部素材…` |
| 7 | `ac_tests --filter=store`（§2 入口条件 3）与全量 `ac_tests`（S15 后回归） | 17/17；468/468 | ✅ `TESTS 17/17`、`TESTS 468/468`（`store_data_dir_env_fallback` 已按平台断言，Windows/Linux 都成立） |

构建走仓库既有入口 `powershell -NoProfile -File server/build.ps1 -Config Release`（CMake + Ninja + mingw g++ 15.2.0，与 §6-1 的 `-DCMAKE_BUILD_TYPE=Release` 等价）；`cmake --install` 的安装规则已写在 CMakeLists 里，Linux 上的执行步骤见 README §18.7。

## 3. 性能门槛（S14 §5 的 8 条，抄自 `build/server-perf-*.json`）

以 5 分钟浸泡跑次（`soak-4p5min`，300s，1s 采样）为准；2 分钟门禁与 200ms RTT 场景的成绩见
`docs/evidence/server-perf-4p2min.md`。

| # | 门槛 | 限值 | 实测 | 判定 |
|---|---|---|---|---|
| G1 | 4 人 + 60 羊单核占用 | < 30 % | 6.998 % | pass |
| G2 | 每客户端出站带宽 | ≤ 40 KB/s | 20.191 KB/s | pass |
| G3 | 快照帧字节 P95 | ≤ 1228 B | 955 B | pass |
| G4 | 单快照帧字节上限 | ≤ 2048 B | 1015 B | pass |
| G5 | 硬纠正 | ≤ 5 次/分钟/人 | 0 | pass |
| G6 | tick 调度误差 P95 | 严格口径 8 ms；本机定时器粒度 12–15 ms > 8 ms ⇒ 按 §5 走替代判据（报告里该行 `limit` = 2 ms） | 首尾 1/3 P95 差 0.000 ms、\|drift\| 0.0 ms；同一次跑次的严格口径原值 `metrics.scheduleErrorP95Ms` = 78 ms，按 §15.4 A1 的条件化判据放行（原始 JSON：`build/server-perf-soak2.json`） | pass |
| G7 | RSS 增长斜率 | < 1 MB/分钟 | 0.024 MB/分钟 | pass |
| G8 | 事件丢弃 / 未捕获异常 / tick 跳过 | 0 / 0 / 0 | 0 / 0 / 0 | pass |

## 4. 四个 HTTP 端点

| 端点 | 实测 |
|---|---|
| `GET /health` | 200，含 `protocolVersion`（与 `ac_server_version` 口径一致） |
| `GET /metrics` | 200，`text/plain; version=0.0.4`，46 行名字表 |
| `GET /api/leaderboard?limit=` | 200（限流触发时 429；30 次/分钟滑动窗口） |
| `GET /api/matches/recent?limit=` | 200（同上） |
| 其它路径 | 404 |

## 5. 退役判定清单逐条状态

| # | 条目 | 状态 | 依据 |
|---|---|---|---|
| ① | v2 服务器 + v2 客户端 ≥30 分钟联合对局（含断线重连、波间跳过） | **未验证** | 需要 v2 客户端批次，本步只能提供 S14 的 5 分钟机器人浸泡 |
| ② | 性能门槛 8 条全绿 | **满足** | 见 §3（另有 2 分钟与延迟场景两份成绩） |
| ③ | 对拍向量逐位复现 | **未验证**（本步未动对拍链路） | `packages/shared` 冻结不删；对拍用例在 `--filter=step`/`--filter=codec` 组内 |
| ④ | `check-docs`/`check-assets` 全绿 | **满足** | 见 §2 第 6 行；`packages/server` 测试未在本步改动 |
| ⑤ | 生产连续 7 天无未捕获异常、无战绩写失败 | **未验证** | 需要真实部署与 7 天观察窗；`ac_uncaught_*_total` 与战绩写失败计数已可观测 |
| ⑥ | 回滚演练：旧 Node 服务器 5 分钟内恢复服务 | **未验证** | 需要一次真实演练（旧单元在 `D:\projects\tmp\angry-chen-bak\deploy\angry-chen.service`（`packages/` 不在本仓库）） |

## 8. 全量复检（干净构建）

入口：删掉 `server/build` 与仓库根的 `data/`，从零跑 `powershell -NoProfile -File server/build.ps1 -Config Release`（CMake Configure + Ninja 真编译，不走 g++ 直编兜底）。日志：`build/verify-all.log`、`build/verify-gates2.log`。

| # | 检查 | 结果 |
|---|---|---|
| 1 | 干净构建 | ✅ `build exit=0`，`ac_server/ac_tests/ac_bot/ac_bench/ac_gate` 五个产物齐全 |
| 2 | `ac_server --version` | ✅ 逐字 `ac_server 0.1.0 protocol=1 tick=50ms`，退出码 0 |
| 3 | `--selftest-log` | ✅ `serverStarted` 行含 `"version":"0.1.0","protocol":1,"tickMs":50` |
| 4 | 全量单元测试 | ✅ `TESTS 468/468` |
| 5 | `ctest` | ✅ 1/1 passed（4.83 s） |
| 6 | 41 组 `--filter` 计数 | ✅ 逐组与冻结值一致（size=5 math=8 trig=4 rng=6 quantize=11 codec=15 hex=10 fuzz=3 wire=3 match=53 transport=9 reliability=5 fragment=4 grace=6 memory=3 world=6 entity=9 pose=5 grid=4 alloc=6 step=27 combat=45 fixture=6 ai=36 waves=16 security=51 malicious=36 motion_authority=13 rewind=12 room=8 matchstate=9 log_double=1 replication=19 schedule=15 log=25 store=17 http=17 report=16 listener=7 threshold=11 runtime=6） |
| 7 | fixture 门（10 万行 / 24.9 MB NDJSON） | ✅ `retained=10000 corrupt=0`、`evictedRetained=9901 cap=10000 batch=100`、`TESTS 17/17` |
| 8 | 部署静态检查 | ✅ §6-4 禁用字符串 = 0；§6-5 端口模式 = 18（service 2 / nginx 3 / README 13）；`cmake --install` → `bin/angry-chen-server` |
| 9 | 微基准 | ✅ 26303 ticks/s（6000 ticks、1024 实体、60 羊、4 人），step p50=12 µs p95=14 µs |
| 10 | `check-docs` / `check-assets` | ✅ 退出码 0 |
| 11 | `gate-4p2min` | ✅ `verdict=pass exit=0`（120.0 s）：G1 8.369% · G2 20.633 KB/s · G3 1000 B · G4 1015 B · G5 0 · G6 0.000 ms · G8 0 · dropped/skips 0 |
| 12 | `gate-4p2min --break=G3=0`（反向自检） | ✅ `verdict=fail exit=1`，**只有 G3 红**（1000 B > 0 B） |
| 13 | `soak-4p5min` | ✅ `verdict=pass exit=0`（300.0 s）：G1 8.116% · G2 19.963 KB/s · G3 970 B · G7 0.025 MB/分钟 · G8 0 |
| 14 | `latency-200` | ✅ `verdict=pass exit=0`（120.0 s）：G1 9.117% · G2 20.507 KB/s · G3 985 B · G6 0.000 ms（schedP95 174 ms 走替代判据）· drift 0.0 ms · dropped 0 |

四份报告 JSON 各含 8 条阈值：`build/cg-1.json`(pass) / `cg-2.json`(fail，预期) / `cg-3.json`(pass) / `cg-4.json`(pass)。

### 8.1 复检自身踩到的两个坑（验证脚本问题，不是仓库缺陷）

1. 后台作业里用 PowerShell **自动变量 `$args`** 装门禁参数：赋值不生效，`@args` 展开为空 ⇒ 门禁被无参调用，打印 usage 后以 **exit 2** 退出（四个场景全中，且看起来像门禁坏了）。换变量名 + 显式展开后正常。
2. 后台作业里相对路径（`server/build/ac_gate.exe`、`--out=build/...`）不可靠；统一改绝对路径后四个场景全部跑通。

## 9. 未验证项与风险

- ①⑤⑥ 三项必须由**真实部署**（Linux + systemd + 反代）与 v2 客户端联调才能关闭；本步只交付可执行的部署单元与验收口径。
- `systemd-analyze verify` 无法在本机（Windows）执行；`deploy/angry-chen-server.service` 已逐字段对照 §5（见 §1 表）。
- 反代两份片段只做**静态**对照（`Select-String` 计数 0）；真实 Caddy/Nginx 语法校验需在 Linux 上 `caddy validate` / `nginx -t`。
- `AC_DATA_DIR` 的默认值在 Windows 上仍是 `data`（平台差异，见 README §18.8-1）；部署单元显式注入 `/var/lib/angry-chen`。

## 10. 2026-09-28 Linux 复检与 G1 口径改造证据（Ubuntu 22.04 云主机）

环境：Ubuntu 22.04 / kernel 5.15 / 4 vCPU / g++ 11.4.0 / cmake 4.4.3 / ninja 1.13。源码由 scp 同步，
并按 README §19.4 的做法用 `grep -cF` 自证改动在位（`SOURCE_OK` 通过后才采信输出）。

### 10.1 构建与用例

- `log.cpp` 缓冲 40 → 64 后构建通过：GCC 11.4 与 GCC 13 报同一个 `-Werror=format-truncation`；
- `ctest` 1/1、全量 **468/468**、`--filter=store` 17/17、`--version` 与冻结串逐字一致。

### 10.2 G1 口径改造前后（四场景同源可比）

| 平台 | 改造前 G1（量 gate 进程） | 改造后 G1（只量独立服务器进程） |
|---|---|---|
| Windows 本机 | 8.37% | **1.283%** ⇒ pass |
| Linux 云主机 | 44–53% | **1.835%** ⇒ pass |
| GitHub runner | 44–48% | CI 全绿（含 G1） |
| 服务器零客户端基线（独立采样 `/proc/PID/stat`） | — | 1.20% |

⇒ 结论：原 G1 量的是「服务器 + 门禁进程内压测工装」，44–53% 是工装造成的假高；真实服务器在
4 人 + 60 羊下只占约 1–2% 单核，30% 限值余量充足。

### 10.3 Linux 四场景

| 场景 | 结论 | 关键量 |
|---|---|---|
| `gate-4p2min` | **pass**（改造后） | G1 1.835%、G2 22.08KB/s、G3 1135B、G4 1135B、G5 0、G6 0.000/2.000、G8 0 |
| `break=G3=0` | fail，**仅 G3**（反向自检正确） | G3 1135B vs limit 0 |
| `soak-4p5min` | **pass**（实测，含 solo CPU 相） | G1 1.835%、G7 RSS **0.014 MB/min**、G8 0 |
| `latency-200` | **pass**（实测） | G1 1.887%、G6 **0.000/2.000**（严格 schedP95=145ms 仅作参考值）、G8 0 |

### 10.4 结论

`ci / quality`、`ci / server`、`ci / server-perf` **全绿**：编译期真红（GCC 格式截断）、数据目录的
"root 假象"、G6 判据不可跨环境复现、G1 量错对象 —— 四类问题全部收口，A1 结案。

## 11. 2026-09-28 联调期服务端修复与复验（本机 Windows）

背景：客户端侧的六步联调（C15 §5）是本批唯一"服务端必须在场"的验收。第一次真连就暴露出服务端侧三个
问题，逐个定位到代码并修掉（契约变更走 ADR：ADR-012 回执、ADR-013 大厅/自动准备）：

| # | 现象（联调现场） | 根因 | 处置 |
|---|---|---|---|
| 1 | 第 2 步"新进大厅 ready=false"实测 `actual=True` | `RuntimeConfig::isAutoReady` 默认 `true` 且**没有开关能关**：`ensureMatchRunning` 对每个会话无条件 `roomSetReady(true)` 后 `tryStartMatch` ⇒ 大厅在真连时不可达 | ADR-013：默认改 `false`，新增 `--auto-ready`（装载/门禁显式开），`ac_bot` 常置 Ready 位扮演"已准备的玩家" |
| 2 | 第 5 人（超员）在 HelloAck 之后**没有任何回执**，客户端只能干等 | Hello 路径把准入结果丢掉（`kRoomFull` 静默） | ADR-012：新增 `Disconnect` reason 8 `kRoomUnavailable`，Hello 后补一帧；用例 `runtime_room_full_answers_disconnect_reason_eight` 连"重发也要再补一帧"一起钉住 |
| 3 | 客户端举了 Ready 位，服务端**毫无反应**（`ready` 一直 0） | ① `config::kButtonReady = 0x80` 全仓库只被定义、从没被读；② `handleCommand` 里 §5.5 的窗口去重**方向反了**：`if (!noteCommandSequence(...)) → 丢`，而该函数命中窗口才返回 `true` ⇒ 每条命令的**首次发送**都被当重复丢掉，只有重传才被应用 | ADR-013 裁决 5/7：`handleCommand` 在大厅相位按 Ready 位调 `roomSetReady`；去重改回 `if (noteCommandSequence(...)) → 丢` |

复验（同一台 Windows 机器、同一条生产路径）：

- `ac_tests` 全量 **`TESTS 511/511` exit 0**（本批新增 3 条：大厅就绪路径、满员回执、`--auto-ready` 选项解析）。
- `ac_gate --scenario gate-4p2min`：**`verdict=pass exit=0`**，`cpuMean=1.21%`、`bw=20.87KB/s`、
  `snapP95=1033B`（限 1228）、`snapMax=1112B`（限 2048）、`schedP95=14.00ms`、`drift=16.0ms`、`dropped=0`、
  `skips=0`；原始 JSON `build/gate-4p2min-postfix.json`。**注意口径**：去重方向修好后机器人真的会移动
  （此前只有重传那一份被应用 ⇒ 实际是"站桩人群"），所以本节数字与 §10.3 不可逐字比较（见 README §15.5）。
- HTTP 面空房口径同步修正：`/health` 的 `players` 与 `/metrics` 的 `ac_players` 此前用的是
  `activePlayerCount()`（"空房夹到 1"是**波次预算**的口径），空房时两端点都报 1；改为房间真实会话数
  `connectedSessionCount()`，用例断言空房为 `0`（`runtime_serves_health_and_metrics_over_http`）。

> 这三个问题都是**服务端侧遗留**，不是客户端用例的问题：客户端那条断言（`ready=false`）在只有客户端、
> 没有服务端的隔离环境里永远测不出来 —— 这正是"两个 agent 各自隔离、没有联调"漏掉的那一类缺陷。
> 分工回写：ADR-012/ADR-013 是契约，`server/src/**` 与 `server/tests/**` 是本批改动，
> 客户端侧只动了 `LocalIdentity` 的注释与 `InputSampler` 的 Ready 状态位（见 `client-v2-acceptance.md`）。

## 12. 2026-XX-XX 联调复跑期服务端修复（第二批：ack/序号/心跳）

§11 那批修完，联调从"进不去大厅"推进到"能开对局但六步仍红"，又暴露四个**只有真连才看得见**的问题。
本批全部落在 §5.2/§5.3/§5.6 的**已冻结契约内部**（实现错，不是契约错），故不新增 ADR。

| # | 现象（联调现场） | 根因 | 处置 |
|---|---|---|---|
| 1 | `/health` 说 `players=4`，客户端只解出 `rows=1`、`matchStates` 几乎不涨 ⇒ 对局根本没在客户端跑起来 | `encodeMatchState` 用**整帧长度**去比 `kMatchStateMaxBytes`（那是**载荷**预算 `5 + 4×(16+12) = 117`），满员那一条广播整条被容量检查吞掉（平白多算 20 字节包头） | 容量检查改成两段：`payloadBytes > kMatchStateMaxBytes` 与 `8+12+payloadBytes > capacity` 分开判；`kMatchStateMaxBytes=117`、`kMatchStateFrameMaxBytes=137`；新用例 `match_full_room_four_twelve_byte_names`（4 行 × 12 字节昵称，`payload==117`、整帧 `137`、第 5 行被拒） |
| 2 | 机器人进房后客户端**整包判 `BadValue`** | 机器人从不发 `kJoin`，行表里昵称是 0 长度 —— 而 0 长度昵称非法 | 行表构造对无 `kJoin` 的会话落 `player` 兜底值；新用例 `runtime_row_without_join_uses_player_fallback`（4 个真实 socket、每会话时间戳错开、`isAutoReady=false`，断言 `lastFrame=40/55/70/85B`、`rows=1..4`、`name=player`） |
| 3 | 第 3 步"丢包 ≤5%"读到 **500‰**（封顶）且 `retx` 持续增长 | 服务端**一条 `seq` 计数器**同时喂 Snapshot(5) 与 MatchState(10)；客户端按**类型**估期望包数（`NetStats.cs:91-105` 的 `_seqSeen[(int)type]`）⇒ 插入的另一种包被算成丢包 | 拆成每类型一条流：`snapshotSeq` / `matchStateSeq` / `keepAliveSeq`（`msgId` 仍按通道共享）；用例断言 Snapshot 与 MatchState 两条流各自**严格连续**（`last+1`） |
| 4 | `rttMs=344~485`、每条命令重传 2~4 次、`dup` 一度到 31 | 服务端**从不主动回 ack**：§5.6 的 500ms `KeepAlive` 只有计时器、包没发出去，客户端只能等 1Hz 的 MatchState 带 ack（RTO 表 200/300/450/675/1000ms 全走完） | 新增 `Runtime::sendKeepAlive` + `flushHeartbeats`（每个 poll 处理一次）：① 到点发 §5.6 的 `reliable\|ackOnly`、载荷 0 的心跳；② 本 poll 收到过可靠包就**合并成一帧**立刻回执。`msgId` 逐帧推进（写死常数会被客户端当重复包整帧丢弃，连 ack 一起丢）；`header.seq` 走 `keepAliveSeq`。用例 `runtime_match_state_carries_command_acks` 断言心跳形状（`isAckOnly && isReliable && payloadBytes==0 && hasReliableExt`）、首帧及时性（≤ 第 2 个 poll）、`isAckedBy(peer, msgId)` 与 1.5s 内 ≥2 帧 |

复验（同一台 Windows 机器、同一条生产路径）：

- `ac_tests` 全量 **`TESTS 514/514` exit 0**（本批新增 4 条：满员行表编码、无 `kJoin` 兜底、命令 ack 与两条流的连续性、心跳形状/及时性/确认）。另：`tiny_test.hpp` 的 `kMaxCases` 从 512 提到 768 —— 用例数超过上限时**新增用例会被静默丢弃**（本轮就撞上了"用例数已达上限 512"）。
- `ac_gate --scenario gate-4p2min`：**`verdict=pass exit=0`**，`cpuMean=1.52%`、`bw=21.56KB/s`（限 40）、
  `snapP95=1033B`（限 1228）、`snapMax=1148B`（限 2048）、`schedP95=14.00ms`、`drift=17.0ms`、`dropped=0`、`skips=0`。
  相比 §11 的 `bw=20.87KB/s` 多出的约 0.7KB/s 就是本批新增的心跳/回执（20 帧/s × 20 字节 ≈ 0.4KB/s，另加每客户端 2 帧/s 的周期心跳），远在 40KB/s 门槛内。
- 六步联调：**`JOINT-ACCEPTANCE PASS`（退出码 0）**，客户端 `SELFTEST OK cases=187`。第 3 步 `rttMs=31.0`、`lossPermille=0`、`retx=0`、`worstSnapshotHz=20.0`；第 5 步战绩里本地会话 `aliveMs=21650`（本批之前同一条是 `aliveMs: 0`）。逐字原始行见 `docs/evidence/client-v2-acceptance.md` 第 2 节。

> 教训（已写进 README §15.5）：**"服务端有没有把 ack 送出去"这件事，任何单侧用例都测不出来**——
> 服务端的可靠层只有重传表、没有对端视角；客户端只能看见"我的命令迟迟没被确认"。
> 两侧各自绿，合起来 `aliveMs=0`：又是隔离开发漏掉的那一类缺陷。

## 13. 2026-XX-XX 收官裁决（三项挂账的跨侧口径）

联调收尾时挂了三项"要人拍板"的口径，用户裁决：**按建议来**。逐条落点：

| # | 挂账 | 裁决 | 落点 |
|---|---|---|---|
| A4 | 被积压封顶**拒收**的可靠消息，要不要回滚重传表？ | **fail-closed：受理失败就一个字节都不许上线**（`Send` 先 `Enqueue` 后 `Track`） | 语义属 S04，实现在客户端 `Net/UdpTransport.cs:241-250`；服务端侧无对应路径（服务端发送不做积压封顶）。新用例 `net.send_reject_no_retransmit`：链路硬失败期连发 200 条（部分受理、部分拒收），放开链路推过 6s（多个 RTO）后，逐字节扫 `MemoryLink` 留档的**每个出站数据报**，被拒载荷的标记一个都不许出现 |
| A10 | 包头 `seq` 到底是"每通道"还是"每类型"？ | **`seq` 每 `type` 一条；`msgId` 每可靠通道一条**（与 §12 第 3、4 条的实现一致，客户端统计不改） | 写进 [S03 §5.1](../plans-v2/server/S03-二进制协议与编解码.md) 的 `seq` 行 + "序号口径"两条（并写明服务器→客户端只有 `MatchState`/`KeepAlive` 带 `ackBase/ackBits`，快照不带回执）；客户端 [C02 §5.1](../plans-v2/client/C02-客户端数学量化与协议解码.md) 复述同一句 |
| A3 | 剔除距离 / 阴影距离 / 准星散布三处数值"对不上" | **以实现侧的档位表为唯一来源**，客户端散布值与 `server/src/config/weapons.hpp` 对齐 | 核对结果：三项**逐值已同源**（`Batching.SheepCullDistanceM=60`/`ArenaCullDistanceM=80`、`Batching.ShadowDistanceM={20,35,50}`、`BaseSpreadDeg={0.8,0.6,4.0}` + 生长 0.1/上限 0.25 与 `weapons.hpp:24-35` 相同）——是文档滞后，不是代码分歧。核对记录见 `docs/evidence/client-v2-remaining-work.md` A3 段 |

三项都不动 wire 格式，故不新增 ADR（A10 属 §5.1 的**措辞澄清**，与 ADR-009 的包头定义不冲突）。

## 14. 2026-09-29 生产部署与云端验收（43.143.120.65）

**一句话结论**：v2 服务器已按 §18 的部署契约装到 `43.143.120.65` 并以 systemd 常驻运行，`HTTP` 面（nginx `:80`
→ `127.0.0.1:8787`）**公网可达**、游戏面在其**本机**回环上收发正常、S14 两条门槛在部署机上重跑全绿；
**`UDP 8788` 目前被云侧安全组挡在公网之外**（主机侧无拦截、抓包证明确实没到主机），这一条需要部署方在
云控制台加一条入站规则 —— 详见 §14.6，它是本次唯一"需要人出手"的残余项。

环境：腾讯云 `ins-jcc98nn1` / `ap-shanghai` / Ubuntu 22.04（kernel `5.15.0-181-generic`）/ 4 vCPU / 3.7 GiB /
内网 `10.0.0.17`；`g++ 11.4.0`、`cmake 4.4.3`、`ninja 1.13.2`、`nginx`（发行版包）。

### 14.1 先证明"跑的就是这份源码"（README §19.4 的纪律）

远端源码由本机工作树打包同步（只带 `server/`、`deploy/`、`docs/evidence/fixtures`，HEAD = `0d2bc6f`），
逐文件 SHA256 与本机**完全一致**：

| 文件 | SHA256（两侧同一值） |
|---|---|
| `server/src/main.cpp` | `e9b8080962fc809e0fc08f2294d6fc11ed478710ca30ac20d116770cfd658710` |
| `server/src/server/runtime.cpp` | `693e3c2dbe2363143fa05ebd9dfa1d97623104049acb5fe639d845d9cbc3b3bf` |
| `server/src/core/log.cpp` | `7e0b37fe254c808e0007670b0361004bed13c3bdd7a70185a4ebaa8b26b4ac05` |
| `server/src/net/reliability.cpp` | `fa84858069e4acb15f4e777fd49b859b5c81c081474f88806d46b3198c4f4c51` |
| `server/CMakeLists.txt` | `6ae9efde9667f7712160ce294155fa5341323aaad03e7fdcc3f1218e9e87fcbd` |
| `deploy/angry-chen-server.service` | `0028699eef04e78c9a98f6196d3dda87add1e88044bffef28f3e0b7ece0b521a` |
| `deploy/nginx.conf` | `2476e51e07b459e11cc13b8fca2cdc65e6f4b79aa14c22983ab00083216a42ec` |

本轮所依赖的改动也在远端逐个 `grep -cF` 自证在位（任一为 0 即中止）：`--auto-ready` 8、`kRoomUnavailable` 2、
`flushHeartbeats` 3、`logFileOpenFailed` 2、`solo cpu phase` 2、`sendKeepAlive` 3、`ac_server_version` 3。
**装上去的二进制与构建产物同哈希**：`sha256(/opt/angry-chen/bin/angry-chen-server)` = `sha256(server/build/ac_server)`
= `d6f5f2f3ed37a458bf0209a167e384b5353e320e4237435cde8f9070cde53830`。

### 14.2 构建与用例（Linux / GCC 11.4）

```
cmake -S server -B server/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DAC_WERROR=ON -DAC_SERVER_VERSION=0.1.0
cmake --build server/build -j 4
```

配置 + 编译（96 个目标）**零告警零错误**（`-Werror` 生效，§19.1 的 GCC 格式截断真红未复现），随后：

| 检查 | 实测 |
|---|---|
| `ac_server --version` | `ac_server 0.1.0 protocol=1 tick=50ms`（退出码 0，与 §5 冻结串逐字一致） |
| `ctest --test-dir server/build` | `1/1 Passed`（8.12 s） |
| 全量 `ac_tests` | **`TESTS 514/514`**（与 §12 的 Windows 结果同数，零回归） |

### 14.3 安装与启动

1. 备份上一版二进制（Sep 28 的 `680072 B`）→ `/opt/angry-chen/bin/backup/angry-chen-server.20260929-222540`；
2. `cmake --install server/build --prefix /opt/angry-chen` → `/opt/angry-chen/bin/angry-chen-server`（`686032 B`）；
3. `/etc/angry-chen/server.env`（`root:angrychen` `0640`）含 `AC_UDP_PORT/AC_HTTP_PORT/AC_DATA_DIR/AC_LOG_LEVEL`
   与 **`AC_LOG_FILE=/var/log/angry-chen/server.log`**（§18.7 前置条件 ①）；
4. `chown -R angrychen:angrychen /var/lib/angry-chen /var/log/angry-chen`（服务用户 `angrychen` uid 998；§18.7 前置条件 ②）；
5. 单元与反代都**按仓库文件重装**（`deploy/angry-chen-server.service` → `/etc/systemd/system/`，
   `deploy/nginx.conf` → `/etc/nginx/conf.d/angry-chen.conf`，`rm -f /etc/nginx/sites-enabled/default`），`nginx -t` 通过
   （§18.7 前置条件 ③）；
6. `systemctl daemon-reload && systemctl enable --now angry-chen-server` → `active (running)`，`enabled`。

启动后 `/var/log/angry-chen/server.log` 的结构化行（`server.err.log` **0 字节**，即无任何非 JSON 输出）：

```json
{"ts":"2026-09-29T14:25:40.906Z","level":"info","evt":"listening","room":0,"tick":0,"pid":0,"detail":{"udpPort":8788,"httpPort":8787,"dataDir":"/var/lib/angry-chen"}}
```

两条**只在 Linux 上才能跑**的静态校验（§18.8-6/§18.8-14 此前登记为"本机（Windows）无法执行"，本轮补上）：

| 命令 | 实测 |
|---|---|
| `systemd-analyze verify /etc/systemd/system/angry-chen-server.service` | **对本报单元零输出**（只报了机器上另外两个无关单元：`tat_agent.service` 的 `PIDFile=` 遗留路径、`snapd.service` 的 `RestartMode`），即单元语法与字段均被 systemd 接受 |
| `logrotate -d /etc/logrotate.d/angry-chen` | `rotating pattern: /var/log/angry-chen/*.log after 1 days (7 rotations)`，并逐个 `considering log server.err.log / server.log`（两份都被纳管；`copytruncate` + `compress` + 保留 7 份与 §18.4 一致） |
| `nginx -t` | `syntax is ok` / `test is successful`（`deploy/nginx.conf` → `/etc/nginx/conf.d/angry-chen.conf`） |

### 14.4 端点与监听（直连与经反代两条路径都验）

| 请求 | 实测 |
|---|---|
| `http://127.0.0.1:8787/health` | **200** `{"status":"ok","protocolVersion":1,"rooms":1,"connections":0,"players":0,"graceActive":0,"recordsRetained":2,"uptimeSeconds":13,"ticks":0}` |
| `http://127.0.0.1:8787/metrics` | **200**，`Content-Type: text/plain; version=0.0.4` |
| `http://127.0.0.1:8787/nope` | **404** `{"error":"not-found","path":"/nope"}` |
| `http://127.0.0.1/health`（经 nginx `:80`） | **200**（同一 JSON） |
| `http://127.0.0.1/metrics`（经 nginx `:80`） | **200** |
| `http://127.0.0.1/nope`（经 nginx `:80`） | **404**（nginx 兜底页） |
| `http://127.0.0.1/api/matches/recent`（经 nginx `:80`） | **200**，`{"ok":true,"entries":[...]}` |
| `ss -lunp` | `udp 0.0.0.0:8788 users:(("angry-chen-serv",pid=…))` |
| `ss -ltnp` | `tcp 0.0.0.0:8787`（服务）与 `tcp 0.0.0.0:80`（nginx） |

### 14.5 真实对局与持久化（4 人真 UDP，2 分钟）

`ac_bot --players 4 --minutes 2 --host 127.0.0.1 --port 8788`：四个会话各
`packetsIn=6410 packetsOut=3631 bytesIn=2681643 bytesOut=123440 snapshots=2399 matchStates=146 commands=3630 hasSession=1`。

| 证据 | 前 | 后 |
|---|---|---|
| `/var/lib/angry-chen/matches.ndjson` 行数 | 2 | **5** |
| `/var/lib/angry-chen/reports/*.json` 份数 | 2 | **5** |
| `/api/matches/recent?limit=1`（经 nginx） | — | `{"matchId":"YM3Z-7072307098","durationMs":29900,"waveReached":1,"winnerTeam":1,"playerCount":4,…}` |
| `/health` | — | `"connections":4,"players":4,"graceActive":0,"recordsRetained":5,"ticks":2440` |

即 §18.6 退役清单 ① 里"战绩入库"这一半（`matches.ndjson` ≥1 行、`reports/` ≥1 份、`/api/matches/recent` 含该
`matchId`）在生产部署形态下**已闭环**，"≥30 分钟"这一半见 §14.7。

### 14.6 公网可达性：TCP 80 通，UDP 8788 被云安全组拦住（**需人工放行**）

按 `deploy/nginx.conf` 的注释与 §18.7，反代只管 HTTP，游戏流量是**客户端直连 UDP 8788**。实测：

| 视角 | 报文 | 结果 |
|---|---|---|
| 目标机回环（`python3 public_probe.py 127.0.0.1 8788`） | 20 字节 Hello（`01010000 00000000 34125a5a 30303030 30303030`，type=1/session=0/nonce=`0x5A5A1234`/token=`"00000000"`） | **收到 HelloAck**：28 字节 `0102 0100 0500 …`，`type=2`、`flags=0x01`（reliable）、`session=5` |
| 开发机（Windows，公网） | 同一 20 字节 Hello | **5 s 无应答** |
| 第二台云主机（`124.221.45.74`，独立网络） | 同一 20 字节 Hello | **5 s 无应答** |
| 目标机 `tcpdump -ni any udp port 8788`（外部探测期间） | — | 只看到 `lo` 上的本机回环包，**没有任何来自公网的包** |
| 目标机 `iptables -L -n` / `nft list ruleset` | — | `INPUT` 链 `policy ACCEPT`，`YJ-FIREWALL-INPUT` 只对若干源 IP 拒绝 `dport 22`，**没有 8788 相关规则** |
| 公网 HTTP | `curl http://43.143.120.65/health` | **200**（同一台机、同一个进程，只是走 TCP 80） |

⇒ 报文**根本没到主机**，拦截点在云安全组（`ufw` 本就是 `inactive`）。**动作**：在腾讯云控制台给
`ins-jcc98nn1` 的安全组加一条入站规则 `UDP:8788`（来源按主办方策略，最小化可只放行客户端出口 IP 段），
另建议保留 `TCP:80`、`TCP:22`。放行后**复验命令**（在任意公网机器上）：

```bash
python3 public_probe.py 43.143.120.65 8788     # 期望 "UDP_PROBE OK"，type=2/flags=0x01/session!=0
```

客户端侧怎么指向它：把 `client/server.txt.example` 复制成 `server.txt` 放在 `angry-chen.exe` **同级**，
内容写成 `43.143.120.65:8788`（ADR-016）。真机已验：出包 player 的日志里会出现
`Ac.Boot: 连接 43.143.120.65:8788（来源 server.txt）`（同一份日志里 0 条异常，见 `client-v2-remaining-work.md` A14–A15）。

> 这条是 §18.7 原先**缺失的部署前置条件**（仓库只写了主机侧 `ufw allow 8788/udp`），已按本次实测回写为
> 前置条件 ④（见 `server/README.md` §18.7）。

### 14.7 S14 性能门槛（部署机上重跑，2026-09-29）

生产单元常驻占用 `8788/8787`，而 `ac_gate` 是**自己承载运行时**的，因此本轮用 `--port-base 8898`
（运行时 UDP 8898 / HTTP 8897、solo CPU 相 8908/8907）与生产进程并存；`AC_DATA_DIR=/tmp/ac-gate-data` 显式注入（§19.2）。
**反例（值得记录）**：不带 `--port-base` 时进程内运行时绑不上 8788，打印
`runtime start failed: udp bind failed on port 8788` 且 `--out` **不留文件** —— 同机跑门禁必须错开端口（§19.6）。

| 场景 | 判定 | 关键量（原 JSON 字段） |
|---|---|---|
| `gate-4p2min` | **`verdict=pass` `exitCode=0`**（`durationSec` 120.000） | G1 **2.255 %**、G2 **24.154 KB/s**、G3 **1135 B**、G4 **1322 B**、G5 **0**、G6 **1 ms**、G8 **0**；`ticks=2480/2400`、`workP95=0 ms`、`simDriftMsMax=3.0`、`cpuP95Pct=2.988` |
| `soak-4p5min` | **`verdict=pass` `exitCode=0`**（`durationSec` 300.001） | G1 **2.150 %**、G2 **24.315 KB/s**、G3 **1153 B**、G4 **1250 B**、G5 **0**、G6 **1 ms**、G7 **0.033 MB/分钟**、G8 **0**；`ticks=6079/6000`、`eventsDropped=0`、`tickSkips=0`、`uncaughtExceptions=0` |

两点口径说明：① 两场景的 `G7` 在 2 分钟门禁里是 `not-measured`（设计如此，斜率只在浸泡场景判）；
② 本轮数字比 §10.3 略高（G2 24.1 vs 22.1 KB/s、G3 1135 vs 1135 B 持平），与 §11 注明的"去重方向修好后机器人真的会动"
是同一类口径差，**不与 §10.3 逐字比较**；限值余量充足（G2 限 40、G3 限 1228）。

### 14.8 30 分钟长局（S13 §14.1 的"≥30 分钟云端复跑"待办）与 7 天窗口

见 §14.8.1（本轮实测）。生产部署下"连续 7 天无未捕获异常、无战绩写失败"（退役清单 ⑤）的观察窗**自本次部署起算**，
本轮只能给出起始基线与本节的中途证据。

#### 14.8.1 30 分钟 4 人真局（同一生产进程，`2026-09-29T15:01:03Z → 15:31:03Z`）

命令：`./server/build/ac_bot --players 4 --minutes 30 --host 127.0.0.1 --port 8788`（真 UDP 打生产单元，
不经任何测试工装）。四个会话的收尾行（每会话各一行）：

```
bot session=6 packetsIn=96064 packetsOut=54433 bytesIn=39409125 bytesOut=1850708 snapshots=36000 matchStates=2155 commands=54432 hasSession=1
bot session=7 packetsIn=96062 packetsOut=54433 bytesIn=39409085 bytesOut=1850708 snapshots=36000 matchStates=2155 commands=54432 hasSession=1
bot session=8 packetsIn=96064 packetsOut=54433 bytesIn=39409125 bytesOut=1850708 snapshots=36000 matchStates=2155 commands=54432 hasSession=1
bot session=9 packetsIn=96069 packetsOut=54433 bytesIn=39409225 bytesOut=1850708 snapshots=36000 matchStates=2155 commands=54432 hasSession=1
```

按 1800 s 折算：`snapshots=36000` = **20 Hz**（§18.2 的稳态快照率）、`commands=54432` = **30.2 Hz**
（C05 §5.1 的 30 Hz 上行）、每会话入向 39.4 MB / 出向 1.85 MB。

| 证据 | 前（15:01:03Z） | 后（15:49:04Z） |
|---|---|---|
| `matches.ndjson` 行数 | 7 | **75** |
| `reports/*.json` 份数 | 7 | **75** |
| 服务进程 RSS（`VmRSS`/`VmHWM`） | 5452 kB（15:07:50Z 的中途采样） | **5452 kB / 5452 kB** |
| `/metrics` | `ac_tick_skips_total 0`、`ac_events_dropped_total 0`、`ac_records_retained 7` | `ac_tick_skips_total 0`、`ac_events_dropped_total 0`、`ac_records_retained 75`、`ticks=39719`、`uptimeSeconds=5003` |
| 日志 | —— | `server.log` **2759 B**、`server.err.log` **0 B**、`error` 行 **0**、`warn` 行 **0**；事件只有 7×`listening` / 6×`shutdownRequested` / 6×`shutdownComplete`（都是本轮重启留下的） |

- **内存斜率 0.000 MB/分钟**（41 分钟两次采样同值，且 `VmHWM == VmRSS` ⇒ 峰值也没涨），
  与 S14 的浸泡（`G7 = 0.033 MB/分钟`）同量级。
- 对局在 30 分钟里自然churn 了 68 局（4 个只会直冲的机器人打不过第一波，每局 15–26 s ⇒ 全流程
  `lobby → playing → ended` 与战绩/报告落盘跑了 68 轮），最后一局的 `leftMidMatch:true` 就是机器人
  到点退出造成的（服务端如实记录）。
- **宽限期重连（退役清单 ① 的"断线重连"服务端半边）**在同一套部署上单独验过：`Hello` →（4 s 静默跨过
  3 s 断线阈值 → 会话进宽限期）→ `Resume(token=salt^nonce)`，服务端**接受**并补发**全量快照**：
  `snapshot tick=37602 serverTimeMs=1880100 lastAckedSeq=0 baselineTick=0 changedCount=2`（`baselineTick=0`
  正是 §5.2 承诺的"补一次全量"），随后稳态 20 Hz 快照继续；探针自报 `RECONNECT_PROBE OK`。
- 观察项（如实登记，未深挖）：整段会话累计 `ac_dropped_frames_total 1479`（同期 `ac_frames_in_total 232269`，
  约 0.6%），集中在"对局结束释放会话后仍在路上的包"这一类（`validateSession` 失败路径）；
  S14 的门禁场景里同一计数器为 **0**（`dropped=0`）。这条口径需要一次专门的长时间带断线演练才能定位，
  不阻塞本次部署验收。

### 14.9 退役判定清单状态变化（§5 表）

| # | 条目 | 本次之后 |
|---|---|---|
| ① | ≥30 分钟联合对局（含断线重连、波间跳过） | **服务端半边已闭环**：30 分钟 4 人真局 + 68 轮战绩入库（§14.8.1）、宽限期 `Resume` 补全量快照（§14.8.1 末条）；"v2 客户端在场 + 波间跳过"仍待客户端侧联调（本文 §14.6 的公网 UDP 放行是前置） |
| ② | 性能门槛 8 条全绿 | **满足**（新增部署机重跑，§14.7） |
| ③ | 对拍向量逐位复现 | 未变（`--filter=fixture` 在本轮 514 例内全绿） |
| ④ | `check-docs`/`check-assets` 全绿 | **满足** |
| ⑤ | 生产连续 7 天无未捕获异常、无战绩写失败 | **观察窗自 2026-09-29 起算**（服务已常驻；本文 §14.8.1 的 48 分钟窗口内 `server.err.log` 0 B、`error` 行 0、内存零增长） |
| ⑥ | 回滚演练 | 未做；本轮已把上一版二进制备份在 `/opt/angry-chen/bin/backup/angry-chen-server.20260929-222540`（回滚 = 覆盖回去 + `systemctl restart`） |
