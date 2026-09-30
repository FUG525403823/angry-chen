# 人与羊移动卡顿：实际源码离线复现与修复

## 范围与限制

针对用户报告的移动卡顿，本轮只改客户端 GameLoop 帧循环与 Predictor 时间累加，不改协议、服务端、羊模型、材质、网络发送频率或模拟50ms步长。

绝不启动 Unity/Tuanjie（包括batchmode）、游戏或加载Unity DLL。没有实机帧率、GPU性能或公网抖动的测量，不能据此宣称所有卡顿已经消失。

## 已复现根因

1. **远端插值时钟累计两次**：GameLoop先 `_nowMs += dtMs`，再 `_clock.Advance(_nowMs)`；后者接收的是本帧经过时间并直接累加。运行时间越长，渲染时钟越容易超过快照区间，FindBracket夹到最新帧，羊和远端玩家退化成快照频率的位置跳变。
2. **本地预测被30Hz命令节奏限制**：GameLoop只在当帧产生新命令时调用Predictor.Advance，随后清除标记。没有上行命令的渲染帧既不步进，也不累积渲染外推余量。帧率越高，每次上行帧携带的dt越小，预测时间反而推进越慢。
3. **分数毫秒被截断**：GameLoop把每帧dt强转int，Predictor也使用int累加器。即使修复第二项，60FPS每秒仍丢约40ms，144FPS约丢136ms。

已有Predictor.RenderPosition本来就做 `state.pos + velocity * accumulator`，相机也每帧取RenderX/Y/Z；问题不是完全缺少插值／外推，也没有证据支持先怪服务器或网络。

## 实施

- 渲染时钟只推进 `Advance(dtMs)`。
- 持有最近一条命令，每个渲染帧都推进预测；模拟实际产生一个50ms子步才往未确认命令环写一条，未改成每帧入队。
- 身份清理时清除持有命令，避免旧输入跨身份沿用；正常松键继续由实际InputSampler发零意图。
- Predictor内部使用double时间，保留AccumulatorMs整数读取接口，渲染外推使用完整小数余量。浮点阈值容差仅1e-9ms，固定子步仍是50ms；保留每帧最多5子步和余量上限。
- 非正／非有限dt不污染Predictor；极大输入的丢步计数饱和，不整数溢出。

## 反馈回路

`tools/movement-check.ps1` 使用本机已有Roslyn，在内存编译真实GameLoop、InputSampler、SnapshotView、EntityViews、Interpolation、Predictor、Reconciler、LocalStep与相关纯C#依赖；使用仓库实际三角函数表。只将外围HUD／Transport／Unity输入接口替换为桩，网络不连接、输入通过公开注入接口提供。

这不是复制一份运动算法来测试，而是实际调用 `GameLoop.ApplySnapshot → Frame → Views`。它也不是完整客户端编译，更不是引擎运行。报告包含逐文件SHA-256与局限。

旧源冻结于 `build/movement-stutter/baseline/`，红报告为 `before.json`；新报告为 `results.json`。

首轮三秒实际源码对照（远端统计预热后两秒）：

| 场景 | 修复前 | 修复后 |
|---|---|---|
| 60FPS远端、20Hz快照、3m/s | 120帧中80帧位置重复，最大单帧跳15cm | 0重复／0倒退，每帧5cm |
| 144FPS远端、20Hz快照、3m/s | 288帧中248帧位置重复，最大单帧跳15cm | 0重复／0倒退，最大单帧2.5cm |
| 60FPS本地、真实30Hz输入 | 三秒28子步，行进6.48m | 59子步，13.425m |
| 144FPS本地、真实30Hz输入 | 三秒10子步，行进2.403m | 59子步，13.375m |

本地理论三秒距离13.5m，修复后的少量差距来自首条30Hz命令的起步等待，而非持续丢时间。144FPS远端仍受快照在渲染帧中轮询与已有时钟校正影响，单帧速度并非完全恒定；不宣称已消除网络到达抖动。

扩展至60/120/144/240FPS后，本地松键稳定期漂移均0，Predictor三秒均60子步、13.5m（浮点误差约1e-14m）。最终16项离线回归全部通过，results.json逐文件SHA-256与当前生产及提取测试源码一致；包括直接提取执行的两条新增BootSuite方法与两条既有和解／相机方法。

另发现旧BootSuite合成和解fixture的ack取T-3，却断言只留两个未确认子步；修复预测漏推进后实际留下三步，该旧断言如实变红。先在build下独立副本只把ack水位改为T-2，原两条和解／相机断言全部通过，再同步到正式fixture。未调宽任何位移、误差、领先量或重放条数阈值；注释改为“合成ack保留两步”，不再把它说成完整100ms网络RTT模拟。

## 回归与待验收

- BootSuite新增 `boot.remote_motion_render_cadence`（60FPS真实帧循环恒速远端）及 `boot.local_motion_independent_of_send_rate`（60/120/144/240FPS真实30Hz输入、松键停止）。
- InputCameraSuite新增 `sim.predictor_fractional_time`（30/60/120/144/240FPS，一秒20步、0.5ms余量与非法时间保护）。
- 客户端Unity测试已补写，未在引擎执行。独立离线回放的通过范围由JSON逐条记录，不代替全部BootSuite或InputCameraSuite通过。
- 文档、素材依赖、差异空白检查通过；本轮没有服务端改动，不将上一轮545/545报告冒充本轮移动验收。
- 公网延迟／丢包、差分快照跨帧稀疏记录、重新连接、相机实机帧耗时与GPU负载仍需后续实测。当前修复针对已证实的三处时间推进缺陷，不扩大到未经复现的候选。

复跑（不启动Unity）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/movement-check.ps1
```
