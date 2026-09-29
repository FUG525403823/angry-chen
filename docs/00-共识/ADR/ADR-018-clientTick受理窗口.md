# ADR-018：clientTick 受理窗口（从"严格相等"改为有界窗口）

- 状态：已接受（2026-09-30）
- 影响面：S11 §5 的 `clientTick` 校验、客户端上行 tick 的来源、`ac_dropped_frames_total` 指标口径
- 取代：S11 §5 "clientTick 必须恰好等于服务器 tick" 的原文

## 背景

S11 §5 原规定：客户端命令里的 `clientTick` 与服务器当前 tick **严格相等**，否则整条命令
（连同按键位）被判 `kStaleTick` / `kFutureTick` 丢弃。

真机实测（部署实例 `43.143.120.65:8788`、客户端 30Hz 上行 / 服务端 20Hz 快照）：

- 客户端能拿到的唯一权威 tick 是"最近一条**已应用**快照"的 tick（快照 20Hz ⇒ 50ms 一格）；
  命令按 30Hz 上行，一条 tick 的命令会在下一格之后才到服务端。
- 16ms RTT 下按 W 8 秒：`ac_frames_in_total +279`、**`ac_dropped_frames_total +103`**
  （约 13 条/秒，占上行 **43%**）。
- 丢掉的不只是移动：`buttons` 位一起丢，所以开火/换弹/准备位也会随机失效。
- 症状：客户端本地预测每帧都在前进，服务端却有 43% 的 tick 没收到 ⇒ 和解反复把玩家拽回，
  表现就是玩家反馈的"**移动一顿一顿**"。

## 决策

`clientTick` 的受理条件改为**有界窗口**：

```
受理 ⇔ serverTick - kClientTickSlackTicks ≤ clientTick ≤ serverTick
kClientTickSlackTicks = 2   // 2 格 = 100ms
```

- 低于窗口下界 ⇒ `kStaleTick`（过期，丢弃，不重放）；
- 高于 `serverTick` ⇒ `kFutureTick`（预支，丢弃）——**反作弊语义不变**；
- 窗口内的"迟到但合法"输入正常受理。

## 为什么不是"客户端预支 tick"

另一种解法是客户端把 tick 外推到当前格（`snapshotTick + elapsed/50ms`）。否决理由：外推会
产生未来 tick（每格前半段），而未来 tick 必须继续拒收（否则等于允许预支/重放），于是又变成
丢命令。窗口方案把"时序判断"留在服务端，客户端仍只报权威值，语义更简单。

## 后果

- 30Hz 上行 + 20Hz 快照下，只要 RTT ≤ 100ms，命令不再因时序被判过期；16ms RTT 实测
  `dropped_frames` 归零（同一条压测：按 W 8 秒 ⇒ `frames_in +307`、`dropped_frames +0`）。
- 代价：命令最多可以迟到 100ms 才生效。对"服务端权威 + 客户端预测 + 和解"的模型无影响
  （客户端本来就按预测渲染），只在极端拥塞下让末端手感略软。
- 拒收未来 tick 的规则不变，`security_malicious_11a/11b` 两个反作弊用例语义不变；
  `security_malicious_11c` 与 `security_tick_boundary_accepts_equal` 按窗口边界重写。
- 指标口径不变：窗口外的丢弃仍然计入 `ac_dropped_frames_total`（所以这个数现在直接反映
  "真的丢了输入"，可以当验收判据用）。
