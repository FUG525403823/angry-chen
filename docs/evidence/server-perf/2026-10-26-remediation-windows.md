# 服务端整改后构建、安装与性能验证（Windows，2026-10-26）

## 结论

- **构建通过**：现有 MinGW/Ninja Debug 配置下，`ac_server`、`ac_gate`、`ac_bot` 定向构建退出 0。
- **临时安装与版本通过（修复后）**：安装产物为 `angry-chen-server.exe`，执行 `--version` 退出 0，输出 `ac_server 0.1.0 protocol=2 tick=50ms`。
- **性能验收阻塞，不能宣布通过**：正常 gate、G3=0 负向与 soak 均在独立 CPU 前置相后因 `runtime start failed: cannot-open:.../matches.ndjson` 退出 1；未产生性能 JSON，也未完成规定时长。负向的退出 1 来自启动失败，**不是** G3 阈值检测成功。
- 未改阈值、未改源码、未提交、未操作线上。本文仅记录本机实测；同工作区其他代理负责源码修改和完整 CTest，本文不代替其证据。

## 环境与证据位置

- Windows 11 专业版，10.0.26200；AMD Ryzen 9 5900HX，8 核/16 逻辑处理器。
- 现有 `server/build/CMakeCache.txt`：`CMAKE_BUILD_TYPE=Debug`，`CMAKE_CXX_COMPILER=C:/Program Files/mingw64/bin/c++.exe`。
- CMake：`D:/tools/Visualstudio/software/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`。
- Ninja：`D:/tools/Visualstudio/software/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe`。
- 根目录：`D:/projects/tmp/angry-chen`。
- 本次 run：`server-remediation-20261026-1126`。
- 原始日志/运行元数据/临时安装：根 `build/reports/server-remediation-20261026-1126/`。
- 隔离运行目录：根 `build/runtime/server-remediation-20261026-1126/<case>/`。
- 时间均为本机 +08:00。11:25:03 首次检查未见 cmake/ninja/ctest 活动进程，已向主代理协调构建占用。

## 构建与安装命令、结果

在仓库根执行（PowerShell；为当前进程添加 MinGW DLL 搜索路径）：

```powershell
$env:PATH='C:\Program Files\mingw64\bin;'+$env:PATH
$cmake='D:/tools/Visualstudio/software/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake --build server/build --target ac_server ac_gate ac_bot -j 2
```

初次构建重新编译 gate 并链接三个程序，退出 0。源码 gate 的 `--out` / `AC_DATA_DIR` 要求已进入新程序，而非运行 10/9 的旧 gate。

初次安装到 `build/reports/<run>/install-prefix` 退出 0，但安装名是无后缀 `angry-chen-server`，PowerShell 直接执行产生 `CantActivateDocumentInPipeline`。不能把随后其他命令留下的 `$LASTEXITCODE=0` 当作该调用成功。已向主代理反馈；其他代理修改安装规则后，再执行同一构建命令（CMake 自动重新生成）及：

```powershell
$reports='D:/projects/tmp/angry-chen/build/reports/server-remediation-20261026-1126'
& $cmake --install server/build --prefix "$reports/install-prefix-fixed"
& "$reports/install-prefix-fixed/bin/angry-chen-server.exe" --version
```

复验构建、安装、版本均退出 0；安装文件 `angry-chen-server.exe`，8,454,480 字节。版本输出：

```text
ac_server 0.1.0 protocol=2 tick=50ms
```

证据：`rebuild-install-fix.log`、`install-fixed.log`、`installed-version-fixed.log`。Windows 验证调用带 `.exe`；Linux CI 模板不带 `.exe`，本次未在 Linux 执行安装，不据此声称 Linux 实测通过。

## 性能执行方法

每个 case 独立创建目录，并在**同一 PowerShell 进程**显式设置三个变量。不存在阈值改写，唯一负向参数是要求的 `--break G3=0`；没有使用 `--minutes` 缩短正式场景。

```powershell
$root='D:/projects/tmp/angry-chen'
$run='server-remediation-20261026-1126'
$reports="$root/build/reports/$run"
$name='gate-normal-slash' # 另两场景改为 gate-break-G3、soak
$runtime="$root/build/runtime/$run/$name"
New-Item -ItemType Directory -Force "$runtime/solo" | Out-Null
$env:TEMP=$runtime
$env:TMP=$runtime
$env:AC_DATA_DIR=$runtime
$env:PATH='C:\Program Files\mingw64\bin;'+$env:PATH
& "$root/server/build/ac_gate.exe" --scenario gate-4p2min --out "$reports/gate-normal-slash.json" --port-base 18788
# 独立下一进程/目录执行：
& "$root/server/build/ac_gate.exe" --scenario gate-4p2min --out "$reports/gate-break-G3.json" --port-base 18788 --break G3=0
# 独立下一进程/目录执行：
& "$root/server/build/ac_gate.exe" --scenario soak-4p5min --out "$reports/soak.json" --port-base 18788
```

上面最后两条仅展示完整 CLI；实际各场景调用前均把三个环境变量换到对应目录。`*-run.txt` 记录了实际环境变量、起止时间、退出码；`gate-break-G3-run.txt` / `soak-run.txt` 还记录实际参数。主场景使用 UDP/HTTP 18788/18787，独立 CPU 相使用 18798/18797，只访问本地。

| case | 起止时间（+08:00） | 实际退出 | 结果 |
| --- | --- | --- | --- |
| gate-normal（Windows 反斜杠绝对路径） | 11:26:02–11:26:25 | 1 | solo 与主 runtime cannot-open，无 JSON |
| gate-normal-slash（正斜杠绝对路径，预建 solo） | 11:27:03–11:27:26 | 1 | 同样 cannot-open，无 JSON |
| gate-break-G3（`--break G3=0`） | 11:28:20–11:28:44 | 1 | 同样启动失败，未到 G3 判定，负向验证不成立 |
| soak（`soak-4p5min`） | 11:28:44–11:29:07 | 1 | 同样启动失败，未完成 5 分钟或 RSS 斜率验证 |

报错核心（路径因 case 不同）：

```text
serve failed: cannot-open:D:/projects/tmp/angry-chen/build/runtime/server-remediation-20261026-1126/gate-normal-slash/solo/matches.ndjson
runtime start failed: cannot-open:D:/projects/tmp/angry-chen/build/runtime/server-remediation-20261026-1126/gate-normal-slash/matches.ndjson
```

bot 日志显示 `session=0 packetsIn=0 snapshots=0 hasSession=0`，不能将零负载 CPU 当作四人真实负载测量。所有 case 未生成预期 `.json`；日志路径是 `gate-normal.log`、`gate-normal-slash.log`、`gate-break-G3.log`、`soak.log`。脚本批处理自身退出 0 不代表场景通过：必须使用其记录的**各 gate 子进程 exit=1**。

## 必填参数行为与启动阻塞最小重现

- 移除 `AC_DATA_DIR` 且不传 `--out`：退出 2。
- 保留非空 `AC_DATA_DIR` 但不传 `--out`：退出 2。
- 两者均输出 `--out PATH and non-empty AC_DATA_DIR are required (see server/README.md)`。
- 证据：`required-arguments.log`、`required-out.log`、`required-arguments-run.txt`。此项只证明必填参数拒绝行为；由于启动失败，不能证明成功运行后的 `--out` JSON 写入行为。

最小重现（不是性能替代测试，0.01 分钟仅用于定位启动阻塞）：

```powershell
$data='D:/projects/tmp/angry-chen/build/runtime/server-remediation-20261026-1126/direct'
& ./server/build/ac_server.exe --serve --auto-ready --minutes=0.01 --data-dir=$data --udp-port=18808 --http-port=18807
```

立即报 `cannot-open:<data>/matches.ndjson`，退出 1（`direct-serve.log`、`direct-serve-run.txt`）。已确认 PowerShell 可以在同目录写文件；预建 `matches.ndjson` 后启动仍失败，改为仓库内相对路径仍失败。正斜杠/反斜杠两轮均失败，因此没有把问题简单归因为路径分隔符。未通过临时源码 instrumentation 或绕过存储来掩盖问题；根因尚未确定，应交由源码/环境负责人修复后重新跑三场完整验证。

## 限制与后续验收条件

1. 这不是 Release 性能验收，不含 Linux 或线上数据；本机 Debug 仅构建/安装可用性已证实。
2. 服务端可执行文件能输出版本，不代表 `--serve` 已可用；本轮持久化启动仍阻塞。
3. 在修复启动阻塞后，必须重新以各自隔离目录完成正常 2 分钟、G3=0 负向 2 分钟、soak 5 分钟，保留 JSON、各阈值结果及实际退出码。不能沿用本轮启动失败的退出 1 作为负向证据。
4. 本报告没有调整任何阈值，没有以调试短跑替代规定场景，没有宣称未完成的指标通过。

## 附录：干净构建目录完整复测（2026-10-26 11:34–11:44 +08:00）

本附录追加后续实测，不抹除上文旧 `server/build` 的 `cannot-open:.../matches.ndjson` 初始失败。使用已存在的干净 `build/scratch/server-clean/` 中 `ac_gate.exe` 及同目录 `ac_bot.exe`、`ac_server.exe`；本次**没有重新 build、没有修改源码或冻结阈值**。CMakeCache 为 `Debug`、`C:/Program Files/mingw64/bin/g++.exe`，报告 compiler 为 `g++ 15.2.0`，seed 均为 20260101。本附录不替代另代理运行的完整 CTest，也不宣称 Release/Linux/线上验证。

三场由后台任务 `pwsh-289` **顺序**执行，各有独立运行/报告目录；没有缩短 `--minutes`。run 为 `server-clean-perf-20261026-1134`。仓库根 `D:/projects/tmp/angry-chen` 下，每场执行前显式设置 `TEMP`、`TMP`、`AC_DATA_DIR` 三者均为绝对路径 `build/runtime/<run>/<case>`，并 `mkdir` 该目录、`solo` 子目录及报告目录。`--out` 为绝对路径 `build/reports/<run>/<case>/server-perf.json`。PATH 加入 `C:/Program Files/mingw64/bin`。

可复现调用（每场前均按上述约定换环境并建目录）：

```powershell
$root='D:/projects/tmp/angry-chen'
$run='server-clean-perf-20261026-1134'
# case / scenario / port 分别取下表；负向额外 --break G3=0
$runtime="$root/build/runtime/$run/$case"
$reportDir="$root/build/reports/$run/$case"
New-Item -ItemType Directory -Force "$runtime/solo",$reportDir | Out-Null
$env:TEMP=$runtime; $env:TMP=$runtime; $env:AC_DATA_DIR=$runtime
$env:PATH='C:\Program Files\mingw64\bin;'+$env:PATH
& "$root/build/scratch/server-clean/ac_gate.exe" --scenario $scenario --out "$reportDir/server-perf.json" --port-base $port
$gateExit=$LASTEXITCODE # 立即保存实际子进程退出，不用包装脚本退出码替代
```

| case | scenario / 参数 | 主 UDP/HTTP（solo UDP/HTTP） | 起止时间 +08:00 | JSON durationSec | 实际 exit / JSON exitCode / verdict |
| --- | --- | --- | --- | ---: | --- |
| gate-normal | gate-4p2min | 19788/19787（19798/19797） | 11:34:12.459–11:36:40.788 | 120.006 | 0 / 0 / pass |
| gate-break-G3 | gate-4p2min --break G3=0 | 19808/19807（19818/19817） | 11:36:40.820–11:39:09.004 | 120.004 | 1 / 1 / fail |
| soak | soak-4p5min | 19828/19827（19838/19837） | 11:39:09.011–11:44:37.163 | 300.002 | 0 / 0 / pass |

每场墙钟还包含独立 server 的 solo CPU 采样相，因此总长比 JSON 主场景时长多约 28 秒。真实起止时间以 `run-start.json` / `run-result.json` 为准；JSON 自带 `startedAt` 值接近生成报告时间，未将其误作外部调用起点。后台包装任务退出 0，不代表负向 gate 退出 0；表中 exit 均立即取自 gate 的 `$LASTEXITCODE`。

### 逐项真实阈值（measured / limit → status）

| 阈值 | gate-normal | gate-break-G3 | soak |
| --- | --- | --- | --- |
| G1 单核 CPU，% | 3.308 / 30 → pass | 3.204 / 30 → pass | 2.811 / 30 → pass |
| G2 每客户端出站，KB/s | 21.712 / 40 → pass | 22.027 / 40 → pass | 21.822 / 40 → pass |
| G3 快照 P95，B | 1069 / 1228 → pass | **1051 / 0 → fail** | 1069 / 1228 → pass |
| G4 单快照上限，B | 1147 / 2048 → pass | 1147 / 2048 → pass | 1165 / 2048 → pass |
| G5 硬纠正，次/分钟/人 | 0 / 5 → pass | 0 / 5 → pass | 0 / 5 → pass |
| G6 首尾 1/3 P95 差，ms | 0 / 2 → pass | 0 / 2 → pass | 0 / 2 → pass |
| G7 RSS 斜率，MB/分钟 | 0 / 1 → not-measured | 0 / 1 → not-measured | 0.042 / 1 → pass |
| G8 丢弃/异常/tick 跳过，次 | 0 / 0 → pass | 0 / 0 → pass | 0 / 0 → pass |

G6 使用工具既有操作判据「首尾 1/3 P95 差 ≤2ms 且 |sim drift| ≤50ms」，没有改变规则。三场 `simDriftMsMax` 分别为 43/30/27ms；参考 `scheduleErrorP95Ms` 为 14/14/15ms（高于严格 8ms 参考值，不隐去），JSON note 均说明替代判据在限内。CPU P95 分别 12.124/25.850/12.207%，G1 使用独立 server 实测均值，三场 Debug 单核 30% 门均通过。

三场均有 `http health=200 metrics=200 counters=ok`；soak 明确 `roomsAfter=0`，G7 实测 0.042MB/分钟。G8 三个原始计数 `eventsDropped` / `tickSkips` / `uncaughtExceptions` 全为 0。正常/负向的 G7 未测是工具场景设计，不冒称全八项均已测。

**结论：本干净目录完整正常 gate 和 5 分钟 soak 通过；负向自检有效，仅 G3 红且实际退出 1，不是 runtime 启动错误。旧 fopen/cannot-open 在这三场未复现，但这本身不证明旧目录失败的具体根因。** 原文初始失败及其退出码仍保留，后续结论以本附录的新证据为准。

原始证据根：`build/reports/server-clean-perf-20261026-1134/`，三场子目录均含 `server-perf.json`、`gate.log`、`run-start.json`（完整参数和环境）及 `run-result.json`（外部实际 exit、起止时间、报告存在性）。持久化数据根：`build/runtime/server-clean-perf-20261026-1134/`。日志中 PowerShell 对 native stderr 的 `NativeCommandError` 包装包含正常结构化服务日志，不能据此把已生成完整 JSON 且退出 0 的场景判成运行时失败。

## 主代理最终复核：干净构建三场已完成

以上启动阻塞是旧server/build的历史实测，不代表最终干净构建不可用。新build/scratch/server-clean全新configure/build，真实socket及完整561/561通过；三场按原时长和阈值重新实跑，证据根build/reports/server-clean-perf-20261026-1134，各case独立TEMP/TMP/AC_DATA_DIR与run-start/run-result。

- gate-normal：120.006s，actualExit0，verdict pass；CPU3.308%，带宽21.712KB/s，snapshot P95=1069/max1147B。
- gate-break-G3：120.004s，actualExit1；唯一失败G3=1051B > override0，其他已测门通过，是有效反向而非启动失败。
- soak：300.002s，actualExit0，verdict pass；CPU均值2.811%，带宽21.822KB/s，snapshot P95=1069/max1165B，RSS斜率0.042MB/min，drift最大27ms，事件丢弃/tick跳过/未捕获异常均0。

仍仅Windows Debug本机，不推断Linux/Release/公网表现；旧fopen故障根因未证实，未改ACL/阈值或绕过持久化。最终无遗留ac_gate/ac_server/ac_bot。
