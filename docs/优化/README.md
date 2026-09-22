# 优化计划（O01–O10，线性执行）

本目录是**优化专项**的施工依据，与 [../plans/](../plans/) 的 P01–P10 **并列但独立**：
P01–P10 是"从零把游戏做出来"，O01–O10 是"把已经做出来的东西从功能与性能两个角度查漏并改好"。
两者共享同一套纪律：九段式结构、入口条件 = 上一份的移交物、每份都必须可判定（有命令、有期望、有失败含义）。

## 1. 怎么用（线性执行）

| 步骤 | 动作 |
|---|---|
| 1 | 严格按 O01 → O10 顺序执行；**不要跳号**，每份的 §2 入口条件就是上一份的 §9 移交物 |
| 2 | 每份开工前：读 §0（现状与不足，含 `文件:行号` 证据）→ 读 §1 目标 → 逐条核对 §2 入口条件的验证命令 |
| 3 | 施工：只做 §4 任务清单里的事；§5 冻结契约是硬边界（类型、字段、阈值、语义） |
| 4 | 收工：跑完 §6 全部验证命令，确认 §7 DoD 每一条都能勾；任何一条不满足就**不进入下一份** |
| 5 | 回填：更新 [../验收报告.md](../验收报告.md) 的对应章节 + 本文件的 §4 状态表；打 tag `O<NN>` |
| 6 | 每份预估 1 个会话（O04 为 1.5，含 a/b 两块），工作量按"单次会话可完成"切分，见 §4 的"预估"列 |

## 2. 两轴总览（功能轴 / 性能轴）

| 轴 | 涵盖的 O 文档 | 共同特征 |
|---|---|---|
| 功能与正确性 | O01、O05、O07、O08 | 行为不符合设计意图或存在安全/显示缺口：误伤合法位移、死配置未生效、预测值回跳、身份可冒用 |
| 性能与稳健性 | O02、O03、O04、O05、O06、O09 | 每 tick / 每帧的常数项、调度粒度、背压、内存与查询开销 |
| 工程守门 | O10 | 把上面九步的成果变成"回归会变红"的门禁 |

## 3. 文档清单

| # | 文档 | 轴 | 一句话问题 | 关键证据 | 预估 | 语义依赖 |
|---|---|---|---|---|---|---|
| O01 | [权威姿态校验与硬纠正语义](O01-权威姿态校验与硬纠正语义.md) | 功能 | 权威模拟自身产生的位移（击退/分离/碰撞外推）被当成作弊位移，玩家位置被回退 | `anticheat.ts:3,14-16` 与 `combat.ts:36` 位移预算与狂暴速度**同一个 1.15**；`room.ts:641-698` 按序号配对；`验收报告.md` §3.2 实测 523/分钟/人 | 1 | — |
| O02 | [服务器调度与 tick 抖动](O02-服务器调度与tick抖动.md) | 性能 | 抖动指标测的是 OS 定时器粒度而非调度误差；"跳 tick 后模拟时间永久落后"无指标可见 | `server.ts:11,93-96` 5ms `setInterval`；`room.ts:422-427` 样本定义；证据里 `timerGranularityMs = 15.56` | 1 | — |
| O03 | [出站帧所有权与背压](O03-出站帧所有权与背压.md) | 性能 | 服务端所有出站帧都是零拷贝引用，而发送方复用同一缓冲；慢客户端无界吃内存 | `ws/lib/sender.js:133,155,563-570`（不掩码即不拷贝）；`session.ts:102`、`room.ts:119` 复用缓冲 | 1 | — |
| O04 | [模拟热路径事件池与空间划分](O04-模拟热路径事件池与空间划分.md) | 性能 | 分离与邻居聚集 O(n²)、事件每 tick 新建、计数/排序/净化各重复一遍 | `sim.ts:210-249`、`flocking.ts:75-92`、`resolve.ts:184-205`、`localStep.ts:34` | 1.5 | O01 |
| O05 | [快照编码增量化与自适应快照率](O05-快照编码增量化与自适应快照率.md) | 功能+性能 | 编解码各有 1024 槽全扫；截断路径两次全排序；`snapshotRateX10` 是死配置 | `codec.ts:600-614,640-666`、`snapshot.ts:100-116`、`room.ts:90,130` | 1 | O03、O04 |
| O06 | [客户端每帧分配与 UI 节流](O06-客户端每帧分配与UI节流.md) | 性能 | 每羊每帧一个对象字面量；约 12 处 DOM 写入无脏检查；`renderLine` 先拼字符串后比较 | `entityViews.ts:140-153`、`hud.ts:143-148,367-428` | 1 | — |
| O07 | [武器弹药状态对账与 HUD 时效](O07-武器弹药状态对账与HUD时效.md) | 功能 | 本地弹药被 1Hz 权威值无条件覆盖（回跳）；换弹/狂暴倒计时 1Hz 阶梯直驱视觉 | `localWeapon.ts:21-24`、`main.ts:358-369,651`、`room.ts:69` | 1 | O05 |
| O08 | [会话令牌与重连安全（ADR-006）](O08-会话令牌与重连安全-ADR-006.md) | 功能+安全 | 宽限期重连只按昵称匹配，知道昵称即可接管会话 | `rooms.ts:41-53`、`codec.ts:366-388` 16 字节 welcome、`index.ts:1` | 1 | — |
| O09 | [战绩存储与 HTTP 层](O09-战绩存储与HTTP层.md) | 性能 | 战绩整文件读入且无上限；排行榜每次全量排序；读接口无限流无缓存 | `store.ts:85-108,126-139`、`http.ts:11-48`、`report.ts:6` | 1 | — |
| O10 | [质量门与性能守门](O10-质量门与性能守门.md) | 工程 | 体积预算与分配探针未接入门禁；`--strict` 默认关；无 CI；覆盖率只覆盖 shared | `package.json:14,24`、`bots.mjs:104,1139`、`vitest.config.ts:3,7-14` | 1 | O01–O09 |

> **依赖列的含义**：执行顺序永远是线性的（O01 → O10，"依赖"列只是语义上的强关联，
> 例如 O04 的网格化必须在 O01 的派生位移记账之后做，否则回归测试会同时被两处改动干扰）。

## 4. 状态表

| # | 文档撰写 | 优化执行 | 证据文件 | tag |
|---|---|---|---|---|
| O01 | ✅ 已完成 | ✅ 已完成（含执行期并入的任务 11 = 谷仓边界，见上方结论） | `docs/evidence/bots-pose-validation.json`（原始基线 + 10507 中间态 + 最终 0/0/0 + 边界探针） | `O01` |
| O02 | ✅ 已完成 | ✅ 已完成（`S5.2-4` 走替代判据 pass；200ms 变体 verdict=pass、退出码 0） | `docs/evidence/bots-schedule.json`、`bots-schedule-latency200.json`、`soak-5min-o02.json`、`report-o02.md` | `O02` |
| O03 | ✅ 已完成 | ✅ 已完成（慢客户端 123.3s 顶到预算：丢帧计数 + 两个连接 `1013`，排空 5627 帧 `decodeFailures=0`；健康场景 soak RSS 0.278 MB/分钟） | `docs/evidence/probe-slow-client.md`、`soak-5min-o03.json/.md` | `O03` |
| O04 | ✅ 已完成 | ✅ 已完成（60 羊基准 单 tick p95 0.074ms / p99 0.143ms ≤ 8/12ms，约 2.4× 提速；稳态 raw 分配 1002→296 B/tick；4 人 5 分钟压测 11 项门槛全 pass；`check-alloc` 绝对门槛见 §5 登记） | `docs/evidence/bench-sim-60sheep.json`、`docs/evidence/bots-after-o04.json` | `O04` |
| O05 | ✅ 已完成 | ✅ 已完成（默认仍 20Hz：`S5.2-1/2/3/8` pass、`S5.2-4` 走替代判据 pass；编码段 p95 0.046ms、单 tick 合计 p95 0.094ms；新指标已在 `/metrics` 可见） | `docs/evidence/bots-o05.json`、`bench-sim-60sheep.json`（`runs.after-o05`） | `O05` |
| O06 | ✅ 已完成 | ✅ 已完成（羊群实例池化 + HUD 全量脏检查：600 帧 DOM 写入 6001→1861；`pnpm check` 428 用例；体积 +1,280B / +0.73%，门槛变更见 §5） | `docs/evidence/client-frame-alloc.md` | `O06` |
| O07 | ✅ 已完成 | ✅ 已完成（弹药账本按 ack 水位对账：显示值 = 权威值 − 未确认开火；换弹/狂暴倒计时本地推进；调试面板新增 4 字段） | `docs/evidence/bots-o07.json` | `O07` |
| O08 | ✅ 已完成 | ✅ 已完成（重连只认令牌 + `PROTOCOL_VERSION = 2`；`welcome` 24 字节 / `join` +8 字节；ADR-006；`pnpm check` 447 用例） | `docs/evidence/reconnect-token.md`、`docs/evidence/bots-o08.json` | `O08` |
| O09 | ✅ 已完成 | ✅ 已完成（流式加载 + `maxRecords` 淘汰 + 排序缓存 + 读接口 429/TTL + 报告保留；实测 heapUsed 132.2 MB→6.9 MB、查询 49.46 ms→0.001 ms） | `docs/evidence/store-load-100k.md`、`docs/evidence/http-limit-o09.md` | `O09` |
| O10 | ✅ 已完成 | ✅ 已完成（`pnpm check` 九步含体积与分配；`pnpm check:perf` 退出码 0；覆盖率覆盖三包 + 分支下限；四个反向实验证明门禁会红；五个卫生项收敛；CI 两 job） | `docs/evidence/gate-selfcheck.md` | `O10` |

**O04 执行结论（2026-09-22）**：§4 的 15 条任务全部落地，§7 DoD 除「`check-alloc` 绝对门槛」外全绿（`pnpm check` 退出码 0；shared 146 / server 111 用例）。
分离与邻居聚集统一走同一张均匀网格（cell 边长 = `SHEEP_AI.neighborRadiusM = 3m`，两趟计数排序建表，零稳态分配），事件对象池化（`LIMITS.eventPoolSize = 256`，溢出计
`ac_events_dropped_total`），邻居改为「按距离最近的 12 个」（与 `activeIds` 顺序无关，逆序后逐位一致），活动羊数每 tick 只数一次（director 用 `spawned` 回加，`metrics.sheepAlive` 口径不变），
净化职责收敛到会话边界（`localStep.ts` 0 命中 `sanitizeCommand`）。基准 `tools/bench-sim.mjs`（4 玩家 + 60 羊、固定种子）实测单 tick p95 0.074ms / p99 0.143ms，
基线 0.183 / 0.296 → 约 2.4× 提速；真实 4 人局 `tickWorkP95Ms 0.676`、`roomBudgetExceededTotal 0`。
执行期发现两处口径问题并按本文件 §7 规则 2 登记：① `check-alloc` 的 `rawPerTick ≤ 8` 在本机**基线**上就不满足（`git stash` 复测 42.53/42.77 B/tick），O04 把它降到 22.5–22.9（-47%）、
`retained` 两侧 ≈ 0，故该阈值需 O10 复核探针口径后重定；② 基准的单窗口 `raw` 会被 GC 时机放大到 ±1000 B/tick，`bench-sim.mjs` 改为 3 窗口取最小（全量留档）+ gc 后读 `retained`，基线与改动后两组同脚本重跑。
细节见 O04 §5 调整记录与 §6。

**O05 执行结论（2026-09-22）**：§4 的 11 条任务与 §7 的 8 条 DoD 全部落地（`pnpm check` 退出码 0；shared 149 / server 113 用例）。
线上字节格式**未动**——`snapshot-codec.test.ts` 的既有字节断言一行未改即通过：`presentIds` / `mirror.ids` 改成升序紧凑列表（`Uint16Array` + 计数，插入/删除用二分 + `copyWithin`，两侧都零分配），
解码不再每帧重建 1024 槽整表（`grep -n "id <= MAX_ENTITIES" codec.ts` = 0 命中）；截断路径从两次全排序改为「有界最大堆部分选择 + 一次结果排序」，平局按 id 升序决胜，与旧行为逐位一致（`snapshot.test.ts` 对拍一致）。
`snapshotRateX10` 从死配置变成真档位：默认 200（每 tick 一条），拥塞（慢客户端积压 / `tickSkips` 增长 / 房间预算超出）时每 1000ms 降一档到 150（每 3 tick 发 2 次）/ 100（每 2 tick 发 1 次），连续 3 秒无拥塞逐档升回；新指标 `ac_snapshot_rate_x10`（各房间最小值）与 `ac_snapshot_rate_downshifts_total`。
客户端插值延迟从固定 100ms 改为 `clamp(2 × 60 样本中位到达间隔, 100, 250)ms`（样本不足 60 时用档位推算，welcome 不带该字段故只在 pong 回填，协议未改）。
证据：`docs/evidence/bots-o05.json`（`S5.2-1/2/3/8` pass，`S5.2-4` 走替代判据 pass，`verdict=fail` 仅 `S5.2-9b` 未测）、`docs/evidence/bench-sim-60sheep.json` 的 `runs.after-o05` / `runs.baseline-o04`（编码段 p95 0.046ms、单 tick 合计 p95 0.094 vs 0.091ms、稳态 raw 312.6 vs 336.9 B/tick）。执行期差异见 O05 §5.1 与 §6。

**O06 执行结论（2026-09-22）**：§4 的 10 条任务与 §7 的 7 条 DoD 落地（`pnpm check` 退出码 0；3 project **428 用例**）。
羊群实例改为环形池（`createSheepInstancePool`，`acquire` 单调游标，越界回退最后一个并只告警一次），`SheepInstance` / `ChargeWarning` 字段去 `readonly`，`entityViews.sync` 不再每帧为每只羊新建 9 字段字面量（`flock.push({...})` 命中 0）；
HUD 把「先拼字符串后比较」改成「按显示精度先比较后写」，12 处无条件 DOM 写入全部带上脏检查（探针 600 帧 6001 → 1861 次，恒定值路径 600 → 1–16 次）；`hud.update` / `debug.update` 入参改为复用对象（`Mutable<T>`），`views.sync` 回调提升为模块级 `resolveSheepVisual`，事件名数组复用，`percentile` 改原地插入排序（200 组随机样本与旧实现逐值一致）。
本机**无浏览器**，O06 §6 #4 的 DevTools/帧 p95 人工观察无法执行 → 用探针 + 单测作为代理证据（`docs/evidence/client-frame-alloc.md`），并把浏览器复测移交 O10。
门槛变更 1 项（README §7 规则 2 登记）：客户端 JS gzip 体积由 175,547B 增到 **176,827B（+1,280B / +0.73%）**，仍占 1.5MB 预算 11.8% —— 池模块与脏检查缓存的代码量换来了每帧分配与 DOM 写入的消除，见 O06 §5.1 #8。

**O07 执行结论（2026-09-22）**：§4 的 11 条任务与 §7 的 6 条 DoD 落地（`pnpm check` 退出码 0；客户端 32 文件 / 176 用例全绿）。
新增 `packages/client/src/prediction/ammoLedger.ts`：按快照帧头 `lastAckedSeq` 对账，显示值 `mag = clamp(serverMag − pending, 0, magSize)`，`actualDrop < expectedDrop` 的差额计入 `rejected` 并本帧钳到权威值（被服务器拒绝的本地开火不再静默丢失）；acked 水位只在 seq 严格增大时推进，重连/换房与 `reconciler.reset()` 同点 `reset()`。
新增 `packages/client/src/combat/localTimers.ts`：换弹/狂暴倒计时每帧本地推进，权威值到达时取 `min(本地, 权威 + SERVER_TICK_MS)`，本地归零后采用新一轮权威值（下限 0、迟到帧不变大）。
`main.ts` 每帧 `applyAmmo()` + `advance(dtMs)`；调试面板新增 `pendingShots` / `rejectedShots` / `ammoDivergence` / `resyncCount` 一行。压测 `docs/evidence/bots-o07.json`：`S5.2-1/2/3/4/8/9` + 新 `S5.2-10` 全 `pass`，`ammoDivergenceMax = 2`（阈值 ≤ 2），`verdict=fail` 仅来自既有的 `S5.2-9b` 未测。
**未改协议**（`packages/shared/src/net/**` 无改动、无新增运行时依赖）；本机无浏览器 → 「按住开火弹药不回跳 / 换弹环与狂暴倒计时连续」的人工观察移交 O10。

**O08 执行结论（2026-09-22）**：§4 的 11 条任务与 §7 的 7 条 DoD 落地（`pnpm check` 退出码 0；3 project **447 用例**）。
宽限期重连的身份判定从「昵称匹配」改为**只认令牌**：服务器每次成功 join 生成会话令牌（8 字节槽位 / 8 个十六进制字符）随 `welcome` 下发（帧 16 → 24 字节），客户端存 `sessionStorage`（`TOKEN_STORAGE_KEY = 'ac.sessionToken.v1'`，每标签页独立）并在重连的 `join` 中上行（+8 字节）；`findGracedSession(room, token)` 要求非空且相等，昵称退出身份判定；`roomReconnect` 复制旧令牌（重连不重生成）；`PROTOCOL_VERSION` 升到 2，两端不符按既有路径拒绝并把双方版本写进错误帧与关闭原因。
决策与后果记录在 `docs/00-共识/ADR/ADR-006-会话身份与重连令牌.md`（唯一权威描述，明确「协议格式变更必须成对发布、回滚需两端同版本」）。
证据：`packages/server/src/rooms.test.ts` 四情形（正确令牌复用 pid / 错令牌与无令牌按新玩家 / 过期后不回座，均断言 `metrics.graceReconnects`）、`codec.test.ts` 三条布局用例、`connection.test.ts` 两条客户端令牌用例、`docs/evidence/bots-o08.json`（12 项门槛全 PASS，除既有 `S5.2-9b` 未测量）。
**门槛变更 1 项**：令牌宽度取「8 字节槽位 = 8 个十六进制字符（32 位熵）」，与文档中「16 个十六进制字符 / `randomBytes(8)`」矛盾，取字节布局为准（O08 §5.1 #1、ADR-006）。本机无浏览器 → 人工「两标签页同昵称不可顶替」测试移交 O10（步骤见 `docs/evidence/reconnect-token.md` §3）。

**O09 执行结论（2026-09-22）**：§4 的 11 条任务与 §7 的 7 条 DoD 落地（`pnpm check` 退出码 0；**457 用例**）。
存储内存**有界**：`DEFAULT_MAX_RECORDS = 10000`，超出按 `startedAtMs` 最旧批量淘汰（每 `maxRecords/100` 次 append 摊销一次），NDJSON 文件仍只追加不截断；加载改 `createReadStream` + `readline`（`readFile` 已从 `store.ts` 消失）。
查询走可失效缓存（`topCache` / `recentCache`，`invalidateCaches()` 单点，load/append/淘汰三处触发），结果与「全排序取前 N」逐条对拍一致（并列按 `startedAtMs` 降序 → `matchId` 升序）。
HTTP 读接口加 30 次/分钟/IP 限流（`429` + `retry-after: 60`，**先于**缓存判定）与 60s TTL 缓存（键 `path:limit`，`store.version` 变化即失效）；`reports/` 按 mtime 保留最近 `REPORT_RETENTION`（默认 200，`0` = 不清理），清理失败不抛。
实测（`docs/evidence/store-load-100k.md`、`docs/evidence/http-limit-o09.md`）：10 万行 665 B/行 → `heapUsed` 增量 **132.2 MB → 6.9 MB**、常驻 **100 000 → 10 000** 条、`listTopScores(20)` **49.46 ms → 0.001 ms**；**代价如实记录**：加载 445 ms → 711 ms（×1.6）。真实服务器 `curl` 第 31 次 `429` 且带 `retry-after: 60`，`/metrics` 三条新指标有值。
**未决项（移交 O10）**：反代后的真实客户端 IP（`X-Forwarded-For`）与限流阈值调优；NDJSON 文件本身的轮转/归档与完整历史榜单的离线分析工具。

**O10 执行结论（2026-09-22，O 系列收尾）**：§4 的 11 条任务与 §7 的 8 条 DoD 落地。
门禁从七步扩到**九步**（新增 `check:build` 体积预算与 `check:alloc` 稳态分配），新增 `check:perf`（4 人 2 分钟 `--strict`）；新增最小 CI（`.github/workflows/ci.yml`，`quality` + `perf` 两个 job，Node 24 + pnpm 11.22.0）。
覆盖率门槛从「只有 shared 行覆盖」扩到**三包**（shared lines 80 / branches 65、server lines 85、client lines 65，均 ≤ 当次实测基线 88.91 / 69.65 / 88.56 / 69.39）；测试数地板 120 → **300**。
五个卫生项收敛：三个 `tsconfig.tsbuildinfo` 取消跟踪并进 `.gitignore`；`.env.example` 的 `TICK_MS` 注释化标注「未实现」；`securityChecklist()` 第 8 项文案由 `LIMITS.roomCodeAlphabet.length` 生成；`OPCODE.respawn` 移出 `CLIENT_OPCODE_LIST`（数值保留，`metrics.respawnRequests` 标 `@deprecated` 恒 0，帧按非法帧丢弃）；`removeMember` 的 `leftMidMatch` 按 `room.phase` 计算（playing / intermission = 局中）。
**门禁自检（本步最重要的产物）**：四个「故意破坏 → 必须变红」实验全部变红，记录在 `docs/evidence/gate-selfcheck.md`（体积预算 1000 B → build 失败；`REUSE_RAW_LIMIT=0` → alloc FAIL；`MIN_TESTS=10000` → count 退出码 1；强制一个门槛 `status='fail'` → `check:perf` 退出码 1）。

**O10 复查轮（两轴审查后修正，tag `O10.1`）**：修掉 6 处自查缺陷——`## 6` 标题重复、CI `perf` job 上传空目录（改为 `tee` 真实日志）、`check:build` 与 `build:client` 重复定义、`securityChecklist` 未用同文件 `ROOM_CODE_ALPHABET`、`ac_respawn_requests_total` 的 HELP 未标废弃、O02/O08 遗留注释自相矛盾（令牌「16 个字符」实为 8、`maxBufferedBytes`「超过 2 倍」实为「≥ 且已丢帧」）；`MIN_TESTS` 门槛同步补到 `docs/验收报告.md` 的残留引用，README §5 的 9 行重复登记与 1 行过期值清除；O07 §7 六条 DoD 补勾；O08「波间仍可入房」与 Standards 轴的技术债正式记入 O10 §9 与验收报告 §6。

**O03 执行结论（2026-09-22）**：§4 的 10 条任务全部落地，§7 DoD 全绿（`pnpm check` 退出码 0）。拷贝点唯一化之后，
复用缓冲（`session.outbound` / `room.broadcastBuffer`）在发送后即可安全改写：反向验证把「入队计数但直传原帧」写回去，新用例立刻报 `expected 127 to be 24`；
慢客户端在 256KiB 预算处开始丢帧并收到 `1013`，断开前排空的 **5627** 帧 `decodeFailures = 0`。
执行期发现 §5 的断开判据 `> 2 × maxBufferedBytes` 在数学上取不到（`bufferedAmount = 队列 + node 写缓冲 ≈ 2Q`，上界恰为 `2 ×`），
按本文件 §7 规则 2 收紧为 `drops > 0 && bufferedAmount >= maxBufferedBytes`，并同步 §5 门槛登记表；另确认 §4 任务 4 无需改动（内存适配器本就是 `slice()` 语义）。
为复现端到端触发，`tools/probe.mjs` 新增 `--chat-rate` / `--chat-len`（利用聊天广播给同房间慢客户端加压）。细节见 `docs/优化/O03-出站帧所有权与背压.md` §6 与 `docs/验收报告.md` §3.6。

**O02 执行结论（2026-09-22）**：§4 的 10 条任务全部落地，§7 DoD 全绿。三个新指标（调度误差 / 模拟漂移 / 单 tick 工作量）与
`LIMITS.roomTickBudgetMs = 8` 的让出语义都已接线并有单测；4 人 5 分钟压测 `S5.2-4` 走替代判据 pass（粒度 15.51ms、前后 1/3 差 0.48ms、
`sim_drift` 12ms），补跑 200ms RTT 变体得到 **verdict=pass、退出码 0**（顺带关闭了 P10 遗留的 `S5.2-9b` 未测量项）。
执行期记录了三处「字面实现会走偏」的解释（1ms 轮询 + 5ms 绝对网格、漂移基准取首个 tick 走完、旧口径指标保留同值别名），
并顺带修好 `tools/report.mjs` 的 `--out` 静默失效；细节见 `docs/优化/O02-服务器调度与tick抖动.md` §4 与 `docs/验收报告.md` §3.5。

**O01 执行结论（2026-09-22）**：§4 的 10 条任务 + 执行期新增并经评审登记的任务 11 全部落地，§7 DoD 全绿；
压测指标 `ac_hard_correct_total` 的 3 次连续复跑为 **0 / 0 / 0**（阈值 ≤5/分钟/人，`S5.2-8` 三次 `pass`）。
执行期发现两条**互相独立**的成因：①「派生位移被计入命令预算」（围困探针 10 → 0，`pose-validation.test.ts` 固化）；
②既有缺陷「`collideStatic` 把实体推到 `barn.minX - radius`，而 `isLegalPosition` 用 epsilon 判该点仍在谷仓内 →
贴墙玩家每个 tick 被硬回退」（探针 200 tick → 200 次，玩家被钉死）。只修 ① 时复跑仍是 10507 / 0 / 0，
故按 §7 规则 2 先在 验收报告 §3.2 记录一次冻结契约变更，再把 ② 作为任务 11 并入 O01，并同步边界语义到 O01 §5 与下面的门槛登记表。
`--strict` 退出码仍为 1 的唯一原因是 `S5.2-4`（tick 抖动 P95 ≈ 12ms > 8ms，本机定时器粒度所致），属 O02 范围。

> "文档撰写"指本目录的 O 文档已写完并通过 `node tools/check-docs.mjs`；
> "优化执行"指该文档的 §4 任务清单已全部勾选、§6 验证已实测、§7 DoD 已满足。
> 每完成一份，把对应行的"优化执行"改为 ✅ 并填上 tag 与证据路径。

## 5. 门槛登记（优化期间新增/变更的全部阈值）

集中登记，避免阈值散落在十份文档里难以审计。**改动任何一项都必须同时改本表与出处文档的 §5。**

| 阈值 / 常量 | 取值 | 出处 | 含义 |
|---|---|---|---|
| `LIMITS.poseHardCorrectFactor` | 1.5 | O01 | 位移超过 `limit + 派生预算` 的多少倍才强制回退 |
| `ac_pose_suspect_total` / `ac_pose_rejected_total` | counter | O01 | "可疑"与"强制回退"分离后的两个计数 |
| `isLegalPosition` 谷仓边界 | 与 `collideStatic` 一致（`<= minX - radius` / `>= maxX + radius` 即仓外，无 epsilon） | O01 | 权威模拟自己推出来的贴墙落点必须判合法，否则每 tick 被硬回退 |
| `LIMITS.roomTickBudgetMs` | 8 | O02 | 单次 `updateRoom` 调用的 tick 工作量预算（让出 ≠ 丢 tick） |
| `ac_tick_schedule_error_ms_p95` | ≤ 8ms（粒度 >8ms 时用替代判据） | O02 | 与理想时刻 `首 tick + n×50ms` 的偏差 |
| `ac_sim_drift_ms` | 绝对值 ≤ 50ms | O02 | 模拟时间与真实时间之差不累积 |
| `S5.2-4` 替代判据（仅当定时器粒度 >8ms） | 前 1/3 与后 1/3 的 schedule error p95 差 ≤2ms **且** `|ac_sim_drift_ms|` ≤50ms | O02 | 粒度受限的环境判「误差是否累积」，三件套（粒度、前后段、漂移）缺一即视为验证失败 |
| `LIMITS.maxBufferedBytes` | 262144 | O03 | 单连接出站积压上限；`队列顶到该值且已丢帧`（`bufferedAmount ≥ 该值`）⇒ 按 `1013` 断开（§5 调整记录：原判据 `> 2 ×` 取不到） |
| `LIMITS.eventPoolSize` | 256 | O04 | 事件对象池容量（超出计 `eventsDropped`） |
| bench SLO（4 人 60 羊） | p95 ≤ 8ms、p99 ≤ 12ms | O04 | 与 `roomTickBudgetMs` 对齐的单 tick 工作量（实测 p95 0.074 / p99 0.143 ms） |
| `ac_events_dropped_total` | counter（告警阈值待定） | O04 | 事件池溢出而丢弃的事件数；阈值记入 O10 |
| `check-alloc` 门槛 | `rawPerTick ≤ 25`、`retainedPerTick ≤ 16`（O10 接入 `pnpm check`；原 8 不可达，理由见本表 O10 行与 `docs/evidence/gate-selfcheck.md` §5） | O04 | **本机当前状态下基线即为 42.5–42.8 B/tick，绝对门槛不可达**（O04 后 22.5–22.9，-47%；`retained` 两侧 ≈ 0）→ O10 需先复核探针口径再定阈值 |
| `LIMITS.snapshotRateLevels` | [200, 150, 100] | O05 | 自适应快照率离散档位（默认 200；150 = 每 3 tick 发 2 次，100 = 每 2 tick 发 1 次） |
| `LIMITS.snapshotRateDownshiftMs` | 3000 | O05 | 连续无拥塞多久升一档；降档条件是每 1000ms 评估一次的拥塞（慢客户端积压 / `tickSkips` 增长 / 房间预算超出） |
| 客户端插值延迟 | `clamp(2 × 中位到达间隔, 100, 250)` ms | O05 | 60 样本滑窗中位数自适应；样本不足时用 `snapshotRateX10` 推算（默认 200 → 100ms） |
| `ac_snapshot_rate_x10` / `ac_snapshot_rate_downshifts_total` | gauge / counter | O05 | 各房间快照率档位的最小值（无房间 = 200）与累计降档次数 |
| HUD 脏检查口径 | 数值四舍五入到 UI 显示精度后比较（宽度 1 位小数、CSS 角度 0 位、计数 0 位），布尔/字符串按变更比较 | O06 | 保证不因浮点抖动每帧写 DOM；`STATS_REFRESH_MS = 250` 节流不变 |
| 羊群实例池容量 | 256（= `NET.snapshotMaxEntities`，渲染层写字面量） | O06 | 一帧内有效；越界回退池内最后一个并只 `console.warn` 一次 |
| 客户端 JS gzip 体积 | 原「不增」（基线 175,547B）→ **176,827B（+1,280B / +0.73%）**，预算 1.5MB 不变 | O06 | 池化与脏检查的代码量；预算门仍 pass（11.8%），变更已登记验收报告 §3.7 |
| 弹药显示值公式 | `mag = clamp(serverMag − pending, 0, magSize)`；`reserve = serverReserve` | O07 | `pending` = 已本地开火未被 `lastAckedSeq` 覆盖的命令数（未改协议） |
| 弹药对账水位 | 快照帧头 `lastAckedSeq`，只在 seq 严格增大时推进 | O07 | ack 回退忽略；重连/换房与 `reconciler.reset()` 同点 `reset()` |
| `ammoDivergenceMax` | ≤ 2（2 分钟 4 人压测，`S5.2-10`） | O07 | 本地预测与权威值之差；超限说明账本与服务器行为不一致 |
| 倒计时对齐 | `min(本地, 权威 + SERVER_TICK_MS)`；本地 ≤ 0 时采用权威值；下限 0 | O07 | 换弹/狂暴每帧推进；迟到帧不得让剩余变大 |
| 协议版本 | `PROTOCOL_VERSION = 2`（`welcome` 24 字节 / `join` +8 字节令牌） | O08 | 两端不一致按既有路径拒绝；**协议变更必须两端成对发布/回滚** |
| 会话令牌 | `LIMITS.tokenBytes = 8`（线上 8 字节槽位 = 8 个十六进制字符，32 位熵）；客户端 `TOKEN_STORAGE_KEY = 'ac.sessionToken.v1'` 存 `sessionStorage` | O08 | 只用于**重连身份**，不是账号体系；重连复用不重生成；空令牌一律按新玩家 |
| 重连身份判定 | `findGracedSession(room, token)`：`token.length > 0 && session.token === token` | O08 | 昵称退出身份判定（仍用于显示/聊天）；`grep "session.name === name"` 无命中 |
| 战绩常驻上限 | `DEFAULT_MAX_RECORDS = 10000`（`MATCH_STORE_MAX_RECORDS`）；超出按 `startedAtMs` 最旧**批量**淘汰 | O09 | 只影响内存常驻；NDJSON 文件不截断；常驻条数在 `[max-batch+1, max]` 波动 |
| 读接口限流 | 30 次/分钟/IP → `429` + `retry-after: 60` | O09 | 按 `req.socket.remoteAddress`；反代后的真实 IP 未处理（O10 未决项） |
| 读接口缓存 | TTL 60s，键 = `path:limit`，`store.version` 变化即失效 | O09 | 命中计 `ac_http_cache_hits_total`；限流先于缓存判定 |
| 报告保留 | `DEFAULT_REPORT_RETENTION = 200`（`REPORT_RETENTION`；`0` = 不清理） | O09 | `retainReports` 失败不抛，不影响对局结束路径 |
| 质量门步数 | `pnpm check` = **9 步**（+`check:build` 体积、+`check:alloc` 分配）；`pnpm check:perf` = 4 人 2 分钟 `--strict` | O10 | `check:perf` **不**并入 `check`（耗时）；CI 的 `perf` job 承担 |
| `--strict` 语义 | 默认退出码 0；`--strict` 时**只有 `status === 'fail'`** 置 1（`not-measured` 不拦） | O10 | 否则 4 人 2 分钟场景因 `S5.2-9b` 恒红，门槛失效 |
| 覆盖率门槛 | shared lines 80 / branches 65；server lines 85；client lines 65（均 ≤ 实测基线 88.91 / 69.65 / 88.56 / 69.39） | O10 | 规则＝基线向下取整到 5 的倍数；门槛不得高于基线 |
| 测试数地板 | `MIN_TESTS = 300`（实测 458） | O10 | 地板不是目标，只拦「批量丢测试」 |
| 分配探针上限 | `REUSE_RAW_LIMIT = 25`（原 8）；`REUSE_RETAINED_LIMIT = 16` | O10 | **门槛变更**：原值在接入门禁时已不可达（实测 raw 22.5 B/tick 稳定，retained ≈ 0，对照组 29×）；上限型门槛取基线向上取整到 5 的倍数 |

## 6. 与既有文档体系的关系（为什么放在这里）

| 约束 | 说明 |
|---|---|
| 不放 `docs/plans/` | `tools/check-docs.mjs` 会校验 `plans/` 的编号连续与移交物守恒（P01–P10，`HANDOFF-H00…H10`），插入新文件会破坏该链 |
| 不命名为 `P<NN>-*` | 同上，避免与计划文档编号空间冲突 |
| 链接可达 | `check-docs.mjs` 会解析 `docs/` 下所有相对链接；因此本索引**最后**撰写，正文里待生成的证据文件一律用代码块而非链接 |
| 格式检查 | `docs/**` 在 `.prettierignore` 中且被 eslint 忽略，因此新增 markdown 不会影响 `pnpm lint` |
| 与需求的关系 | 优化不新增 FR；若发现玩法缺口（例如 `OQ-06` 跳跃未实现），按 [../02-需求分析.md](../02-需求分析.md) §12 的开放问题流程登记，不在本目录私自决定玩法 |

## 7. 修改本目录的规则

1. O 文档写完就**冻结**：执行阶段只在正文里勾选 §4 的复选框、把 §6 的实测数字填进证据文件，不改 §5 冻结契约；
2. 若实现过程中发现某条的冻结契约必须变更，先在 [../验收报告.md](../验收报告.md) 记录变更与理由，再同步修改文档与 §5 门槛登记；
3. 新增优化项必须新开 `O<NN+1>` 文档并接上移交物链（`HANDOFF-O<NN>` → 入口条件），不要往已完成文档里塞新任务。
