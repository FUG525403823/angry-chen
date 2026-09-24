# 服务端 v2 验收报告（S15 §7）

范围：`docs/plans-v2/server/S15-发布运维与验收.md`。固定点：S14 提交 `cdf1353`（= 本步改动的父提交）。
发布版号 **0.1.0**，协议版本 **1**。

## 1. 交付物落盘

| 文件 | 状态 | 说明 |
|---|---|---|
| `server/CMakeLists.txt` | 改 | `AC_SERVER_VERSION` 注入（默认 0.1.0）、`install(PROGRAMS $<TARGET_FILE:ac_server> DESTINATION bin RENAME angry-chen-server)`（实测 `cmake --install --prefix build/acprefix` 产出 `bin/angry-chen-server.exe`；构建产物仍叫 `ac_server.exe`，`build.ps1` 的产物断言与计划 §6-1 的命令都不受影响）、Release 仍为 `-O2 -DNDEBUG` + `-ffp-contract=off -fno-fast-math -Werror` |
| `server/src/core/version.hpp` | 改 | `kVersion` 改为读构建注入宏（发布口径的唯一来源；`#ifndef` 回落只服务 g++ 直编兜底） |
| `server/src/persist/match_store.cpp` | 改 | `AC_DATA_DIR` 未设时：POSIX → `/var/lib/angry-chen`（§5 冻结），Windows → `data`（§16.1 声明的平台差异） |
| `deploy/angry-chen-server.service` | 新建 | §5 的 Unit/Service 字段逐条对齐（`Type=simple`、`User=angrychen`、`Restart=on-failure/RestartSec=3`、`KillSignal=SIGTERM`、`TimeoutStopSec=10`、六项加固、stdout/stderr 落盘） |
| `deploy/Caddyfile` | 新建 | 只转发 `/health`、`/metrics`、`/api/*`，其余 404 + `Cache-Control: no-store` |
| `deploy/nginx.conf` | 新建 | 同上；注明 UDP 8788 不经反代、由防火墙放行 |
| `server/README.md` §18 | 改 | 目录布局、端口与端点、环境变量、日志轮转、五条排障命令、退役判定清单、部署步骤 |
| 本文件 | 新建 | 验收报告 |

## 2. §6 验证 1–6 的实测输出

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

## 6. 未验证项与风险

- ①⑤⑥ 三项必须由**真实部署**（Linux + systemd + 反代）与 v2 客户端联调才能关闭；本步只交付可执行的部署单元与验收口径。
- `systemd-analyze verify` 无法在本机（Windows）执行；`deploy/angry-chen-server.service` 已逐字段对照 §5（见 §1 表）。
- 反代两份片段只做**静态**对照（`Select-String` 计数 0）；真实 Caddy/Nginx 语法校验需在 Linux 上 `caddy validate` / `nginx -t`。
- `AC_DATA_DIR` 的默认值在 Windows 上仍是 `data`（平台差异，见 README §16.1）；部署单元显式注入 `/var/lib/angry-chen`。
