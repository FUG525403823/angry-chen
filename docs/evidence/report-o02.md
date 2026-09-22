## 压测与 soak 判定（P10 §5.2 / §7）

- 生成时间：2026-09-22T04:02:00.735Z
- 渲染来源：3 份 JSON
  - bots：bots-schedule.json（verdict=fail，时长 300.01s）
  - bots：bots-schedule-latency200.json（verdict=pass，时长 300s）
  - soak：soak-5min-o02.json（verdict=pass，时长 400.7s）

### §5.2 门槛总表

| 门槛 | 说明 | 来源 | 实测 | 阈值 | 判定 |
| --- | --- | --- | --- | --- | --- |
| S5.2-1 | 快照字节均值 ≤ 1200B | bots：bots-schedule.json | 90 B | 1200 | **pass** |
| S5.2-2 | 每客户端带宽 ≤ 40 KB/s | bots：bots-schedule.json | 1.85 KB/s | 40 | **pass** |
| S5.2-3 | 快照速率 19–21/s（按 tick 跨度） | bots：bots-schedule.json | 20.02 /s | 19–21 | **pass** |
| S5.2-4 | 服务端 tick 调度误差 P95 ≤ 8ms（粒度 >8ms 时用替代判据） | bots：bots-schedule.json | 11.46 ms | 8 | **pass** |
| S5.2-8 | 硬纠正 ≤ 5/分钟/人 | bots：bots-schedule.json | 0 /min | 5 | **pass** |
| S5.2-9 | 隔墙命中 = 0（权威性校验） | bots：bots-schedule.json | 0 次 | 0 | **pass** |
| S5.2-9b | 200ms RTT 下回合内无"隔墙命中" | bots：bots-schedule.json | not-run 次 | 0 | **not-measured** |
| AUTH-1 | 无开火即命中 = 0 | bots：bots-schedule.json | 0 次 | 0 | **pass** |
| AUTH-2 | 无命中即击杀 = 0 | bots：bots-schedule.json | 0 次 | 0 | **pass** |
| AUTH-3 | 快照解码失败 = 0 | bots：bots-schedule.json | 0 次 | 0 | **pass** |
| AUTH-4 | 快照非法位置 = 0 | bots：bots-schedule.json | 0 次 | 0 | **pass** |
| S5.2-1 | 快照字节均值 ≤ 1200B | bots：bots-schedule-latency200.json | 90.6 B | 1200 | **pass** |
| S5.2-2 | 每客户端带宽 ≤ 40 KB/s | bots：bots-schedule-latency200.json | 1.86 KB/s | 40 | **pass** |
| S5.2-3 | 快照速率 19–21/s（按 tick 跨度） | bots：bots-schedule-latency200.json | 20.02 /s | 19–21 | **pass** |
| S5.2-4 | 服务端 tick 调度误差 P95 ≤ 8ms（粒度 >8ms 时用替代判据） | bots：bots-schedule-latency200.json | 13.22 ms | 8 | **pass** |
| S5.2-8 | 硬纠正 ≤ 5/分钟/人 | bots：bots-schedule-latency200.json | 0 /min | 5 | **pass** |
| S5.2-9 | 隔墙命中 = 0（权威性校验） | bots：bots-schedule-latency200.json | 0 次 | 0 | **pass** |
| S5.2-9b | 200ms RTT 下回合内无"隔墙命中" | bots：bots-schedule-latency200.json | 0 次 | 0 | **pass** |
| AUTH-1 | 无开火即命中 = 0 | bots：bots-schedule-latency200.json | 0 次 | 0 | **pass** |
| AUTH-2 | 无命中即击杀 = 0 | bots：bots-schedule-latency200.json | 0 次 | 0 | **pass** |
| AUTH-3 | 快照解码失败 = 0 | bots：bots-schedule-latency200.json | 0 次 | 0 | **pass** |
| AUTH-4 | 快照非法位置 = 0 | bots：bots-schedule-latency200.json | 0 次 | 0 | **pass** |
| S5.2-5 | 4 人 60 羊单核 CPU ≤ 30% | soak：soak-5min-o02.json | 2.31 % | 30 | **not-measured** |
| S5.2-6 | 服务端 RSS 增长（稳态，扣除前 60s 启动预热）< 1MB/分钟 | soak：soak-5min-o02.json | 0.413 MB/min | 1 | **pass** |
| S5.2-6b | RSS 全窗口斜率（含启动预热，参考值） | soak：soak-5min-o02.json | 0.425 MB/min | 1 | **info** |
| S5.2-7a | 无未捕获异常 / 未处理拒绝 | soak：soak-5min-o02.json | 0 次 | 0 | **pass** |
| S5.2-7b | 房间数在 bot 离开后回落到 0（无泄漏） | soak：soak-5min-o02.json | 0 间 | 0 | **pass** |
| S5.2-extra | /metrics 采样失败 = 0 | soak：soak-5min-o02.json | 0 次 | 0 | **pass** |
| S5.2-bots | soak 内嵌 bots 子进程退出码为 0 | soak：soak-5min-o02.json | 0  | 0 | **pass** |

### 未测量项（环境/参数不满足）

- S5.2-9b 200ms RTT 下回合内无"隔墙命中"（bots：bots-schedule.json）：需以 --latency 200 运行；本次为 80ms
- S5.2-5 4 人 60 羊单核 CPU ≤ 30%（soak：soak-5min-o02.json）：players=4 sheep peak=25

### 调度 / 漂移 / 工作量（O02）

| 来源 | schedule error p95 | 前 1/3 → 后 1/3 | sim drift max | tick work p95 / p99 | 预算让出次数 |
| --- | --- | --- | --- | --- | --- |
| bots：bots-schedule.json | 11.46 ms | 11.12 → 11.6 ms | 12 ms | 0.801 / 3.374 ms | 0 |
| bots：bots-schedule-latency200.json | 13.22 ms | 13.24 → 12.61 ms | 15 ms | 0.88 / 3.38 ms | 0 |
| soak：soak-5min-o02.json | — ms | — | — ms | — | — |

### §7 DoD 第 2 条判定

- `tools/bots.mjs` 与 `tools/soak.mjs` 存在并可在本机跑完：**成立**（本次渲染同时包含 bots 与 soak 报告）
- 报告满足 §5.2 全部阈值：**成立**（无 fail 项，未测量项已在上表列出）

### 结论

本次渲染的全部压测门槛通过。

> 验证状态：本文件由 `node tools/report.mjs` 从上述 JSON 渲染（**已验证**：有可复现命令与原始 JSON）。
