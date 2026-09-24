# 服务端性能门禁证据（S14）

本文件是 `server/README.md` §15 与 `docs/plans-v2/server/S14-压测基准与性能守门.md` §4/§6 的原始证据：
环境、命令、四个跑次的阈值表、基准三段耗时、反向自检，以及报告 JSON 的字段清单。

固定点：`1a4fcb7`（S13 收尾）+ 本轮 S14 工作区改动。数字都来自**同一构建**的一次连续会话；
命令的**空格分隔**与 `--key=value` 两种写法都验证过（计划 §6/§9 用前者）。

## 1. 环境

| 项 | 值 |
|---|---|
| 主机 | Windows 开发机；4 个机器人进程 + 60 羊 + 服务器全在同一台机器上 |
| 编译器 | mingw g++ 15.2.0，`-O2 -std=c++20 -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math` |
| 构建 | `powershell -NoProfile -File server/build.ps1 -Config Release`（CMake + Ninja） |
| 传输 | 真实 UDP 8788 + HTTP 8787（回环），机器人是独立进程 `ac_bot.exe` |
| 计时粒度 | 实测 `timerGranularity = 12–15 ms`（Windows 默认定时器 15.6ms）→ G6 走冻结的替代判据 |

单核占用/内存按 **ac_gate 进程自身**统计（服务器与门禁同进程），机器人进程不计入 G1/G7 —— 与 §15.3 的声明一致。

## 2. 命令

```powershell
.\server\build\ac_gate.exe --scenario=gate-4p2min --out=build/server-perf-gate.json
.\server\build\ac_gate.exe --scenario=gate-4p2min --break=G3=0 --out=build/server-perf-break.json
.\server\build\ac_gate.exe --scenario=soak-4p5min --out=build/server-perf-soak.json
.\server\build\ac_gate.exe --scenario=latency-200 --out=build/server-perf-latency.json
.\server\build\ac_bench.exe --ticks=6000 --sheep=60 --players=4
```

## 3. 跑次结果（全部为最终构建）

### 3.1 `gate-4p2min`（4 机器人 + 60 羊，120s，seed 20260101）— **verdict=pass，exit 0**

```
verdict=pass exit=0 duration=120.0s cpuMean=8.55% cpuP95=41.62% bw=20.15KB/s snapP95=1000B
snapMax=1015B schedP95=69.00ms drift=0.0ms rss=0.000MB/min dropped=0 skips=0
  G1 4 人 + 60 羊单核占用              measured=8.553   limit=30.000  %          -> pass
  G2 每客户端出站带宽                   measured=20.146  limit=40.000  KB/s       -> pass
  G3 快照帧字节 P95                    measured=1000.000 limit=1228.000 B         -> pass
  G4 单快照帧字节上限                   measured=1015.000 limit=2048.000 B        -> pass
  G5 硬纠正                            measured=0.000   limit=5.000   次/分钟/人  -> pass
  G6 tick 调度误差 P95                 measured=0.000   limit=2.000   ms         -> pass
  G7 RSS 增长斜率                      measured=0.000   limit=1.000   MB/分钟     -> not-measured（2 分钟跑次不判 G7）
  G8 事件丢弃 / 未捕获异常 / tick 跳过   measured=0.000   limit=0.000   次          -> pass
note: G6 用替代判据：前后 1/3 P95 差与 sim_drift 均在限内; http health=200 metrics=200
      counters=ok; ticks=2480 expectedTicks=2400 intervalP95=13ms workP95=0ms
      headTailGap=36ms timerGranularity=14ms
```

### 3.2 反向自检 `--break=G3=0` — **verdict=fail，exit 1**

```
verdict=fail exit=1 duration=120.0s cpuMean=5.69% cpuP95=35.89% bw=20.53KB/s snapP95=1000B
  G1 measured=5.694  limit=30.000   -> pass
  G2 measured=20.527 limit=40.000   -> pass
  G3 measured=1000.000 limit=0.000 B -> fail        <= 上限被压到 0，按预期红
  G4..G6, G8 -> pass（G7 not-measured）
```

其余阈值仍按实测判定；整跑次退出码 1 —— 门禁对「阈值失败」敏感，不是对「场景失败」敏感。

### 3.3 `soak-4p5min`（4 机器人 + 60 羊，300s，1s 采样）— **verdict=pass，exit 0**

```
verdict=pass exit=0 duration=300.0s cpuMean=7.00% cpuP95=32.44% bw=20.19KB/s snapP95=955B
snapMax=1015B schedP95=78.00ms drift=0.0ms rss=0.024MB/min dropped=0 skips=0
  G1 measured=6.998   limit=30.000   -> pass
  G2 measured=20.191  limit=40.000   -> pass
  G3 measured=955.000 limit=1228.000 -> pass
  G4 measured=1015.000 limit=2048.000 -> pass
  G5 measured=0.000   limit=5.000    -> pass
  G6 measured=0.000   limit=2.000    -> pass
  G7 measured=0.024   limit=1.000    MB/分钟 -> pass    <= 1s 采样线性回归的 RSS 斜率
  G8 measured=0.000   limit=0.000    -> pass
note: ticks=6080 expectedTicks=6000 intervalP95=13ms workP95=1ms headTailGap=33ms
      timerGranularity=12ms; soak roomsAfter=0
```

### 3.4 `latency-200`（200ms RTT ±20ms，1% 丢包）— **verdict=pass，exit 0**

```
verdict=pass exit=0 duration=120.0s cpuMean=9.44% cpuP95=48.35% bw=20.17KB/s snapP95=985B
snapMax=1015B schedP95=183.00ms drift=0.0ms rss=0.000MB/min dropped=0 skips=0
  G1 measured=9.443   limit=30.000   -> pass
  G2 measured=20.175  limit=40.000   -> pass
  G3 measured=985.000 limit=1228.000 -> pass
  G4 measured=1015.000 limit=2048.000 -> pass
  G5 measured=0.000   limit=5.000    -> pass
  G6 measured=0.000   limit=2.000    -> pass
  G7 not-measured
  G8 measured=0.000   limit=0.000    -> pass
note: latency-200：隔墙命中与回滚夹取的判定留在 S07/S11 的用例组（step/security 全绿）;
      ticks=2477 expectedTicks=2400 intervalP95=13ms headTailGap=16ms timerGranularity=12ms
```

### 3.5 修复前的历史（如实留档）

G6 的替代判据修好之前，`latency-200` **连续三次判红**（`schedP95` 174–184ms、`|sim_drift|` 峰值 131–136ms），
而同代码的 `gate-4p2min`/`soak-4p5min` 一直是 `drift=22–30ms` 的绿。差异正好等于「第一个玩家进场到开球」
（loading）的那一段：延迟场景里机器人要在 200ms RTT 下握手，loading 长得多（~135ms vs ~22ms）。
根因是 `ac_sim_drift_ms` 的墙钟基准取自 `startScheduler` 的时刻，而 tick 记账基准取自开球时刻，两者不同源；
修法是把两个基准绑到同一时刻（见 §6-6）。修后四个跑次的 `drift` 都是 `0.0ms`。

## 4. 基准三段（`ac_bench --ticks=6000 --sheep=60 --players=4`）

```
bench ticks=6000 sheep=60 players=4 entities=1024 wall=0.242s (24751 ticks/s)
step    p50=13us p95=15us
project p50=1us  p95=2us
encode  p50=24us p95=28us
snapshot bytes avg=913.7 p95=925 max=925 samples=23600
```

一次 tick 的定长工作 ≈ 38us（p50 合计）而预算 50ms/tick，余量 3 个数量级。
（`max` 偶尔出现 ms 级尖刺，来自与测试进程共用一台机器的宿主抢占，p50/p95 不受影响。）

## 5. 报告 JSON 字段（`build/server-perf-gate.json`）

顶层 14 键：`tool, version, scenario, seed, buildType, compiler, host, startedAt, durationSec,
verdict, exitCode, note, thresholds, metrics`；
`thresholds[]` 每项 `{id, label, measured, limit, unit, status}`；
`metrics` 的 12 个冻结字段与 §15.1 的表格一一对应。

读该文件要用 UTF-8（`[System.IO.File]::ReadAllText(path,[Text.Encoding]::UTF8)`）：
Windows PowerShell 5.1 的 `Get-Content -Raw` 按 ANSI 解码，会假装 JSON 坏了。

**note 里的 `headTailGap` 不是 G6 判的那个值**：note 打的是 S12 快照里 `ac_tick_*` 误差环的极差（30ms 量级），
G6 替代判据判的是 gate 自己按 1s 序列算的「首尾 1/3 的 P95 差」（0.000ms，就是 `thresholds` 里的 `measured`）。
两个名字撞车是本批遗留的文案问题，登记在 §15.4 D6。

## 6. 本轮为此修掉的实现缺陷（都是跑门禁才暴露的）

1. **会话存活与命令校验耦合**：命令被 tick/seq 校验拒掉时**不刷新心跳**，于是 soak 跑到 ~33s 会话掉宽限期、
   房间停摆（`ticks=660 / expectedTicks=6000`，drift 266s）。改为「任何带在册 session 的包都算存活证据」。
2. **机器人不会取服务器 tick**：`clientTick` 用本地钟外推，而 `security::validateClientTick` 是**严格相等**，
   708/708 条命令被判 staleTick（`ac_dropped_frames_total=708`）。改为从快照帧读权威 tick，修后 `dropped=0`。
3. **调度误差记账口径**：原先用 `world->tick` 增量，而房间在 loading/intermission 也按 50ms 走 tick ⇒
   开局约 30 tick 被算成假误差（`schedP95=1523ms`）。改为记 `match.counters.ticks` 增量 + 开球对齐基准
   （`schedP95=74ms`）。
4. **G6 替代判据**：原先无条件启用、且拿 `core::scheduleHeadTailGapMs`（极差）当「首尾 1/3 P95 差」。
   改为按本机定时器粒度开关，并按 1s 采样的 `|sim_drift|` 序列算首尾 1/3 的 P95 差与全程最大值。
5. **G8 两项假绿**：`uncaughtExceptions` 硬编码 0（现读 `ac::log::uncaughtCount()`）；tick 跳过没人喂
   （现把房间 `match.counters.skipped` 的增量交给 `noteTickSkip`）。
6. **`ac_sim_drift_ms` 的基准不同源**（本轮最后修的一项，见 §3.5）：漂移的墙钟基准与 tick 记账基准
   现在都在开球那一刻取；同时给增量加了「计数回退只取 0」的保护 —— 房间重启会把
   `match.counters.ticks` 清零，无符号相减回绕会让 `noteTickRun` 循环 40 亿次（实测挂死）。

## 7. 既有门禁

`ac_tests` 全量 `TESTS 468/468`；32 组冻结 `--filter` 逐组与 §15.3 第 21 条记录的数量一致
（本轮抽查 `size`=5、`match`=53、`security`=51、`room`=8、`replication`=19、`schedule`=15、`log`=25、
`store`=17、`http`=17、`report`=16），新增组 `listener`=7、`threshold`=11、`runtime`=6。
