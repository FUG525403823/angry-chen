# S09 羊群 AI 与波次导演
> 里程碑：MS09 ｜ 预估：2 人日 ｜ 上游移交物：HANDOFF-S08

## 1. 目标

本步骤结束后，**场地里会有一群会发疯的羊**：咩咩兵追击撕咬、冲撞羊蓄力冲锋、问界羊保持距离发射问号弹、羊王三阶段并召唤小羊；它们按预算成波生成、成群移动、按仇恨选目标，且整套行为在同一个种子下**逐 tick 可复现**（只用 `ai` 与 `spawn` 两条 RNG 流，`fx` 流恒不被模拟触碰）。

## 2. 入口条件

**入口条件**：HANDOFF-S08

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 战斗结算与命中盒表已冻结 | 阅读 S08 §5.4 的羊形命中盒表 | 四种羊形的盒体参数齐备 |
| 2 | 权威步进阶段 4/5/11 为空实现待填 | 阅读 S06 §5.1 的阶段表 | 阶段 4/5/11 存在且为空函数 |
| 3 | 空间网格、邻居聚集入口与投射物生成可用 | `Select-String -Path server/src/world.hpp -Pattern "spawnEntity"` | 命中，且支持 `ownerId` |
| 4 | v1 羊群与波次数值可查 | 阅读 `sheep.ts`、`waves.ts` | 与本份 §5.1、§5.2、§5.4 逐条一致 |
| 5 | 三流 RNG 与对拍向量可用 | `server/build/ac_tests.exe --filter=fixture` | `TESTS 14/14` |

跨链前置：无（羊群只依赖本链已冻结的战斗与模拟契约）。

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `server/src/config/sheep.hpp` + `waves.hpp` | 新建 | 羊形参数、AI 参数、状态码与转移表（抄 v1 `sheep.ts`，命中盒沿用 S08）；波次上限、预算公式、波间时长、生成节流与出生点规则（抄 v1 `waves.ts`） |
| `server/src/ai/steering.cpp` + `flocking.cpp` | 新建 | 转向原语（`seek` / `arrive` / `separation` / `obstacleAvoid`，零分配）与有界邻居收集、flock 三权重组 |
| `server/src/ai/targeting.cpp` | 新建 | 仇恨槽衰减/命中加成、可见性遮挡判定、目标选择（含切换比） |
| `server/src/ai/sheep_brain.cpp` | 新建 | 状态机与意图计算 `updateSheepIntent`、羊形落地 `applySheepKind`、状态转移校验 |
| `server/src/ai/sheep_attack.cpp` + `king_phases.cpp` | 新建 | 撕咬/冲锋命中、问号弹生成与推进、击退；羊王阶段阈值、阶段状态与召唤（`spawn` 流） |
| `server/src/waves/director.cpp` | 新建 | `WaveDirector`：预算组队、按 tick 节流生成、清波与下一波、`waveStart` / `waveClear` / `matchEnded` 事件 |
| `server/tests/ai_test.cpp` + `waves_test.cpp`（注册进 `main_test.cpp`） | 新建/修改 | §6 断言；运行 `--filter=ai` / `--filter=waves` |

## 4. 任务清单

- [ ] 写 `server/src/config/sheep.hpp` 与 `waves.hpp`：§5.1、§5.2、§5.4 的全部数值与状态转移表逐条落地。
- [ ] 写 `server/src/ai/steering.cpp`、`flocking.cpp`、`targeting.cpp`：转向原语、有界邻居收集（禁分配、禁哈希容器）、仇恨衰减与目标选择（谷仓遮挡用射线 × AABB）。
- [ ] 写 `server/src/ai/sheep_brain.cpp`：按 §5.3 实现状态机与意图，做到「遍历顺序 = `EntityId` 升序、每只羊只消费自己那条意图槽」。
- [ ] 写 `server/src/ai/sheep_attack.cpp`、`king_phases.cpp` 与 `server/src/waves/director.cpp`，并接进 `step.cpp` 阶段 11：命中判定、击退、问号弹生命周期、羊王阶段与召唤、§5.4 的组队与生成节流、清波推进。
- [ ] 写 `server/tests/ai_test.cpp` 与 `waves_test.cpp`（§6 断言），并按 S01 的 `AC_TEST` 注册进 `main_test.cpp`。
- [ ] 更新 `server/README.md` 的「AI 与波次」一节，写入 `--filter=ai` / `--filter=waves` 的运行命令与 §5 的数值来源说明。

## 5. 冻结契约

### 5.1 四种羊形参数表（抄 v1 `sheep.ts`；命中盒见 S08 §5.4）

| 羊形 | 码 | hp | speed (m/s) | damage | price | radiusM | heightM |
|---|---|---|---|---|---|---|---|
| `grunt` 咩咩兵 | 0 | 60 | 2.6 | 8 | 1 | 0.5 | 0.9 |
| `ram` 冲撞羊 | 1 | 140 | 3.2 | 22 | 3 | 0.55 | 1.0 |
| `elite` 问界羊 | 2 | 260 | 2.4 | 14 | 6 | 0.6 | 1.1 |
| `king` 羊王 | 3 | 2400 | 2.0 | 30 | 20 | 1.6 | 2.4 |

### 5.2 羊形 AI 参数表（抄 v1 `SHEEP_AI`，数值不得再调）

| 组 | 字段与值 |
|---|---|
| 感知与攻击 | `sightM 35`、`attackRangeM 1.4`、`attackCooldownMs 1200`、`staggerMs 250`、`deadFadeMs 1500`；冲锋 `chargeWindupMs 1000`、`chargeSpeedMps 9`、`chargeStaggerMs 1000`、触发 `12` m、最长 `1600` ms |
| 问号弹 | `eliteBoltRangeM 25`、`eliteBoltCooldownMs 2500`、`eliteKeepMinM 15`、`eliteKeepMaxM 25`、`boltSpeedMps 14` |
| 羊王 | `kingSummonCount 4`、`kingSummonIntervalMs 8000`、`kingPhase3SpeedMultiplier 1.4`、`kingPhase3CooldownMultiplier 0.7` |
| 击退与聚集 | `knockbackVelocityMps 7`、`neighborRadiusM 3`、`maxNeighbors 12`、`grazeRadiusM 6` |
| 仇恨 | `aggroSlots 8`、`aggroDecayPerTick 0.02`、`aggroPerHit 20`、`targetSwitchRatio 1.5` |
| 局部常量 | 吃草重选 `2500` ms、警戒 `300` ms、精英侧移 `1200` ms、集结权重 分离 1.6 / 对齐 0.4 / 凝聚 0.5、flock 分量混合系数 0.5、转向 `arrive` 减速半径 2.0 m |

### 5.3 状态机与转移（v1 转移表逐字；越界转移返回 `false` 且不改状态）

```cpp
// server/src/config/sheep.hpp —— kSheepTransitions[13]
0 graze:[1,2,7,8]   1 alert:[2,0,6,7,8]  2 chase:[3,5,6,7,8,9]  3 windup:[4,2,7,8]
4 charge:[2,7,8]    5 attack:[2,7,8]     6 ranged:[2,1,7,8]     7 stagger:[2,8,9]
8 dead:[]           9 kingPhase1:[4,10,7,8]  10 kingPhase2:[4,12,11,7,8]
11 kingPhase3:[4,12,7,8]                    12 kingSummoning:[2,5,7,8]
```

- 每 tick 顺序：计时器递减（`timerMs` / `cooldownMs`，下限 0）→ 死亡分支（`speed = 0`，`timerMs >= 1500` 时回收实体）→ 硬直分支（`timerMs <= 0` 回 `chase`）→ 仇恨衰减与目标选择 → 按羊形分支（吃草/警戒/追击/冲锋/攻击/远程）→ flock 混合 → `obstacleAvoid`。
- 吃草：无有效目标时回 `graze`，`timerMs <= 0` 时用 **`ai` 流** 在 `±6` m 内重选吃草点并重置 2500ms；吃草速度 = `speed * 0.4`。
- 警戒：从 `graze` 进入 `alert` 的那一帧才武装 300ms 计时（同态转移不得重复武装，v1 曾因此"羊不会动"）。
- 冲锋羊：距离 ≤ 12m 且可转移进 `windup` 时锁定方向（本 tick 归一化），1000ms 后进 `charge` 并计时 1600ms；撞到玩家或超时/进场/越界即进 `stagger` 1000ms。
- 攻击：`grunt` / `king` 在 ≤ 1.4m 时进 `attack`（速度 0，朝向目标用 `ac::angleUnitsFromVector(dx, dz)` 求角单位，需要弧度时走 `dequantizeAngle`，禁用 `atan2`）；距离 > 1.4 × 1.4 时回 `chase`。
- 问界羊：< 15m 后退、> 25m 前进、15–25m 内按 `strafeSign` 侧移且每 1200ms 翻号；距离 ≤ 25m 时进 `ranged`。

### 5.4 波次与预算（抄 v1 `waves.ts` 与 `director.ts`）

| 规则 | 值 |
|---|---|
| 波次上限 | `WAVE_MAX = 10`；每 5 波为 Boss 波 |
| 波间 | `WAVE_INTERMISSION_MS = 20000`，最短 5000ms（全员准备可跳过） |
| 基础预算 | `round(6 + 3.2 * w + 0.18 * w * w)` ⇒ w=1..3、5、10 → 9 / 13 / 17 / 27 / 56 |
| 人数缩放 | 取整次序固定：先 `base = round(6 + 3.2 * w + 0.18 * w * w)`，再乘人数系数后整体 `round`，即 `budget = round(base * (1 + 0.35 * (players - 1)))`（4 人 w=1：`base = 9` ⇒ `round(9 * 2.05) = 18`）；`speedMult = 1 + 0.02 * (players - 1)` |
| 组队顺序 | 先羊王（Boss 波且预算 ≥ 20 → 1 只，扣 20）→ 问界羊（w ≥ 5，`cap = min(2 + floor(w/5), floor(剩/6))`，各扣 6）→ 冲撞羊（w ≥ 3，`cap = min(floor(剩/3), max(1, floor(w/3)))`，各扣 3）→ 余量全给咩咩兵（各扣 1） |
| 生成节流 | 每 tick 最多 `8` 只；同时最多选 `3` 个出生点；每个出生点与最近玩家的距离必须 > `15` m；候选点取自 S05 冻结的 12 个 `spawnPoint` |
| 抖动与流 | 出生点下标起点与位置抖动全部消耗 **`spawn` 流**（`±1.5` m） |
| 出怪表 | 全部羊按 `grunt, ram, elite, king` 的顺序消耗平面数组；生成即 `applySheepKind` 并置 `graze` |
| 清波 | 计划生成完毕且活动羊数为 0 → `waveClear`；`w >= 10` 再发 `matchEnded` 并置 `finished`，否则立刻为下一波组队并等波间结束 |

### 5.5 羊王阶段

| 项 | 规则 |
|---|---|
| 阈值 | `phase = 1 if hpRatio > 0.66 else 2 if hpRatio > 0.33 else 3`（0.66 → 2，0.33 → 3） |
| 状态 | 阶段切换即写 `kingPhase1/2/3` 状态并把召唤计时器置为 8000ms（phase 1 置 0） |
| 召唤 | phase ≥ 2 时计时器每 tick -`dtMs`，到点重置 8000ms 并按 4 个等分角度（半径 2.6m + `spawn` 流 ±0.3）生成 4 只咩咩兵 |
| 移动与攻击 | phase 3 速度 ×1.4、攻击冷却 = 1200 × 0.7 = 840ms |

### 5.6 问号弹 `questionBolt`

| 项 | 值 |
|---|---|
| 生成 | 精英羊在 `ranged` 状态且冷却为 0 时，对 25m 内最近的玩家生成投射物 |
| 初速与位置 | 水平朝向目标，速度 `14` m/s；出生点 `(x, pos.y + 0.6, z)`；生成后朝向写为目标方向 |
| 生命周期 | `3000` ms；越界（`halfSize - thickness = 39.5`）或飞入谷仓 AABB 即销毁 |
| 命中 | 与玩家的水平距离 ≤ `0.22 + 0.4 = 0.62` m 即命中：按 `elite` 的 `damage 14` 结算（无部位、无衰减、无击退），命中后销毁 |
| 伤害来源 | 归因到投射物 `ownerId`，事件 `subjectId` 为精英羊、`targetId` 为受害者 |

### 5.7 遍历顺序与 RNG 流归属

- 实体遍历一律按 `activeIds` 的严格升序（S05 §5.1，由空闲表二分维护）；禁止遍历 `unordered_map` 等哈希容器；邻居收集按 `(distanceSq, EntityId)` 排序破平局，保证结果与遍历顺序无关。
- 意图计算与落地分两趟：先按升序算全部意图（只读世界），再按升序写速度与朝向，避免"先动的羊影响后动的羊"。
- 流归属（冻结）：**`ai` 流**只服务吃草点重选与任何单羊随机；**`spawn` 流**服务波次出生点选择、出生抖动、羊王召唤抖动；**`fx` 流**禁止被模拟读取，测试断言其抽取次数恒为 0。
- 目标选择打分：`score = aggro[i] + 1 / (1 + distance)`，仅对 `sightM` 内且可见（谷仓遮挡判定通过）的玩家参与；现目标保留门槛为其 `aggro * 1.5`。
- 角度与超越函数（S 条）：AI 需要的 `atan2` / `asin` 一律走 S02 的 `ac::angleUnitsFromVector(dx, dz)` / `ac::angleUnitsFromRatio(r)`（基于 `kAtanUnits` / `kAsinUnits` 查表，结果经 `dequantizeAngle` 变回 double 弧度）；`server/src/ai/**` 与 `server/src/waves/**` **禁用** `sin` / `cos` / `atan2` / `asin` / `exp` / `pow`（需要三角时用 S02 的 `sinUnits` / `cosUnits`）。

## 6. 验证

| # | 命令 | 期望输出 | 失败意味着什么 |
|---|---|---|---|
| 1 | `powershell -NoProfile -File server/build.ps1 -Config Release` 后运行 `server/build/ac_tests.exe --filter=ai` 与 `--filter=waves` | 末行 `[build] ok`；`TESTS 22/22`；`TESTS 16/16` | 常量头/签名与 §5 不符，或状态机、转向、目标选择、预算规则偏离 §5 |
| 2 | 断言：预算公式的 5 个采样 | w=1/2/3/5/10 → 9 / 13 / 17 / 27 / 56；4 人 w=1 → 18（取整次序见 §5.4）；移速系数 1.06 | 预算公式或人数缩放错 |
| 3 | 断言：`planWave(5, 1)` 的计划与预算和 | king 1 / elite 1 / ram 0 / grunt 1（共 3 只），预算和 27 | 组队顺序或余量分配错 |
| 4 | 断言：连续生成 20 tick | 每 tick ≤ 8 只、出生点 ≤ 3 个、每点距最近玩家 > 15m | 生成节流或出生点规则错 |
| 5 | 断言：`hpRatio = 0.70 / 0.66 / 0.33`；phase 2 跑满 8000ms | 阶段 1 / 2 / 3；恰好召唤 4 只咩咩兵（半径 2.6 ± 0.3） | 阶段阈值或召唤规则错 |
| 6 | 断言：问号弹每 tick 位移与寿命；命中距离 0.62/0.63 | 0.7 m；3000ms 后销毁；0.62 命中、0.63 不命中 | 投射物速度、寿命或命中半径错 |
| 7 | 断言：冲撞羊 12m 触发蓄力 1000ms 后以 9 m/s 冲锋（命中 22 伤害、击退 3m、硬直 1000ms）；咩咩兵贴到 2.3m 内 | 与 §5.2、§5.3 一致；咩咩兵伤害 8、冷却 1200ms、击退 1.5m | 冲锋状态机、攻击距离或击退错 |
| 8 | 断言：同一 `seed` 跑 600 tick 两次；`fx` 流抽取次数 | 实体/事件/RNG 状态位型全等；`fx` 抽取次数 == 0 | 遍历顺序不定或流归属被破坏 |
| 9 | `server/build/ac_tests.exe --filter=fixture`；`node tools/check-docs.mjs` | `TESTS 14/14`；文档打印 `OK` | AI 数值与 v1 基线漂移；文档链或链接坏了 |

## 7. DoD（验收标准）

- [ ] 上表 9 条验证全部通过，`--filter=ai` ≥ 22 用例、`--filter=waves` ≥ 16 用例，均 0 失败，`--filter=fixture` 仍 `TESTS 14/14`。
- [ ] [工程约定](../../00-共识/工程约定.md) §5 的质量门 `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。
- [ ] 本份新增门：`--filter=ai` / `--filter=waves` 已注册且 `server/README.md` 有可复制的运行命令；AI 热路径零分配（600 tick × 60 只羊后分配计数增量为 0），`fx` 流抽取次数恒为 0。
- [ ] 四种羊形参数、`SHEEP_AI` 全部字段、状态转移表与 v1 `configHash` 一致；`Get-ChildItem server/src/ai,server/src/waves -Recurse -Include *.hpp,*.cpp | Select-String -Pattern "unordered_map|std::sin|std::cos|atan2|asin|exp|pow"` 无输出。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 遍历用了哈希容器或排序容器 | 同种子两次运行结果不同 | 只遍历 `activeIds` 与定长数组；邻居按 `(distanceSq, EntityId)` 插入排序 |
| 同态转移反复武装计时器（羊卡在警戒不动） | 羊群不追人，`--filter=ai` 的追击用例超时 | 只在"从 `graze` 进入 `alert`"的那一帧武装；用例专门覆盖这一帧 |
| 意图计算与落地混在一趟，结果依赖遍历顺序 | 换一个实体顺序结果就变 | 两趟结构（先算全部意图，再统一写入），并把顺序写进 §5.7 |
| 问号弹数量爆炸导致实体池耗尽 | 实体池满、`spawnEntity` 失败计数非 0 | 冷却 2500ms + 范围 25m 双限制；实体池耗尽时丢弃本次生成并计入统计，不重试 |

回滚目标：回到 `HANDOFF-S08` 的状态（阶段 4/5/11 为空实现、无羊）。回滚方式为删除 `server/src/ai/`、`server/src/waves/` 与两个常量头，阶段 4/5/11 还原为空函数；战斗与对拍不受影响。

## 9. 移交物

**移交物 ID**：HANDOFF-S09

给下一步的稳定接口：`void updateAiIntents(World&, uint32_t dtMs, const EntityId* playerIds, uint32_t, const SpatialGrid&)` 与 `void applyAiIntents(World&)`；`SheepIntent updateSheepIntent(...)`；`void applySheepKind(Entity&, int kind)` / `bool setSheepState(Entity&, int next)`；`int resolveSheepAttacks(World&, const EntityId*, uint32_t)` / `int resolveEliteFire(...)` / `int advanceProjectiles(World&, uint32_t dtMs, ...)` / `int updateKing(World&, Entity&, uint32_t dtMs)`；`DirectorTick updateDirector(World&, DirectorState&, int playerCount, Rng&, ...)` / `int planWave(DirectorState&, int wave, int playerCount)`。冻结数值：§5.1 羊形表、§5.2 AI 参数、§5.3 转移表、§5.4 波次与预算、§5.5 羊王、§5.6 问号弹、§5.7 流归属。

已验证能力清单（同段）：四种羊形行为、群体聚集、仇恨选择、波次预算与组队、生成节流、羊王三阶段与召唤、问号弹生命周期、冲锋与撕咬伤害/击退全部有断言覆盖；同种子 600 tick 两次运行逐位一致；AI 热路径零分配；`fx` 流不参与模拟；AI 与波次数值与 v1 对拍向量逐位一致。
