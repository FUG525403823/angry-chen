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
