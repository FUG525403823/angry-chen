# P07 羊群 AI 与波次导演

> 里程碑：M6 ｜ 预估：2.5 人日 ｜ 上游移交物：HANDOFF-H06

## 1. 目标

本步骤结束后，**对局有节奏了**：羊群从围栏内侧的生成点成群涌来，咩咩兵近身撕咬、冲撞羊蓄力后直线冲锋（有视觉与音效预警）、问界羊在 25m 外发射问号弹、羊王每 5 波登场并分三阶段；波次导演按预算曲线组队，难度随波次与人数上升。AI 完全确定性：同种子同命令序列下，1000 tick 的羊群轨迹逐位复现。

## 2. 入口条件

**入口条件**：HANDOFF-H06

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 战斗结算可产出事件 | `pnpm --filter @ac/shared test` | `resolve` 用例通过 |
| 2 | `EntityKind` 含羊与投射物 | `grep -n "projectile" packages/shared/src/world.ts` | 存在 |
| 3 | 波次事件类型已冻结 | 阅读 P03 §5.5 的 3/4 号事件 | 字段一致 |
| 4 | RNG 三流可用 | `grep -n "stream" packages/shared/src/rng.ts` | 含 `ai`/`spawn`/`fx` |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `packages/shared/src/config/sheep.ts` | 新建 | 四种羊形数值表（需求 §7.3）与 AI 参数（视野、攻击距离、冷却） |
| `packages/shared/src/config/waves.ts` | 新建 | 波次预算公式、人数缩放、Boss 波规则、波间时长（需求 §7.4） |
| `packages/shared/src/ai/steering.ts` | 新建 | seek / arrive / separation / obstacleAvoid，全部输出"期望速度"，不做位移 |
| `packages/shared/src/ai/flocking.ts` | 新建 | 分离/对齐/聚集三力的合成与权重，O(n²) 但 n ≤ 100 |
| `packages/shared/src/ai/targeting.ts` | 新建 | 目标选择：最近可见玩家优先，仇恨表衰减 |
| `packages/shared/src/ai/sheepBrain.ts` | 新建 | 羊的状态机（状态枚举见 §5.1）与转移条件 |
| `packages/shared/src/ai/sheepAttack.ts` | 新建 | 近身撕咬、冲锋判定、问号弹生成与投射物推进 |
| `packages/shared/src/ai/kingPhases.ts` | 新建 | 羊王三阶段：冲撞 → 召唤 4 咩咩兵 → 狂暴提速 |
| `packages/shared/src/ai/director.ts` | 新建 | 波次导演：预算、羊形组队、生成点选择、波次推进与波间隔 |
| `packages/shared/src/sim.ts` | 修改 | 第 2 槽填入 AI 意图求解；第 5 槽加入投射物推进与羊的攻击结算 |
| `packages/client/src/render/sheepViews.ts` | 新建 | 羊的灰盒表现：冲锋蓄力预警环、问号弹、羊王体型放大 |
| `packages/client/src/ui/hud.ts` | 修改 | 波次与剩余敌人数、Boss 血条、蓄力/来袭警示 |
| 上述模块的 `*.test.ts` | 新建 | 转向、群体、状态迁移矩阵、预算、确定性 |

## 4. 任务清单

> 收尾说明（M9 整体校验）：第 11 条「客户端表现」（冲锋预警环、问号弹造型、羊王体型）已在 P09 交付并勾选；本计划的浏览器人工验收仍未执行，见 P09 §5.5 与 P10 §6.1。

- [x] 写 `config/sheep.ts`：四羊形照抄需求 §7.3；追加 AI 参数（`sightM` 35、`attackRangeM` 1.4、`attackCooldownMs` 1200、`chargeWindupMs` 1000、`chargeSpeedMps` 9、`eliteBoltRangeM` 25、`eliteBoltCooldownMs` 2500、`kingSummonCount` 4）。
- [x] 写 `steering.ts`：每个函数输入 `(self, target, neighbors, world)`，输出期望速度向量（不修改实体），便于单测与复用。
- [x] 写 `flocking.ts`：权重 `separation 1.6 / alignment 0.4 / cohesion 0.5`；邻居半径 3m、最大邻居数 12（按距离取最近，避免 O(n²) 的排序开销累积）；力向量写入调用方传入的临时对象。
- [x] 写 `targeting.ts`：目标 = 最近的"可见"玩家（可见性用 P05 的 `rayVsAabb` 做遮挡检测）；仇恨表 `Map<pid, aggro>` 每 tick 衰减 2%，受击 +20，用于让羊不无限换目标（避免抖动）。
- [x] 写 `sheepBrain.ts`：状态机与转移矩阵（§5.1）；`grunt` 用 `graze → alert → chase → attack`；`ram` 增加 `windup → charge`；`elite` 增加 `ranged`（保持 15–25m 距离并发射问号弹）；`king` 由 `kingPhases.ts` 覆盖。
- [x] 写 `sheepAttack.ts`：近身撕咬（1.2s 冷却、8 伤害、命中后玩家被击退 1.5m）；冲锋命中（22 伤害 + 击退 3m + 冲锋者自身硬直 1s）；问号弹（速度 14 m/s、伤害 14、命中或撞墙销毁、可被玩家射击提前引爆——P1 效果好则保留）。所有伤害走 P06 的 `damage.ts` 与事件生产责任表。
- [x] 写 `kingPhases.ts`：阶段 1（生命 100%–66%）冲撞；阶段 2（66%–33%）每 8 秒召唤 4 只咩咩兵；阶段 3（<33%）移速 ×1.4、攻击冷却 ×0.7。
- [x] 写 `director.ts`：`budget(wave) = round(6 + 3.2 * wave + 0.18 * wave^2)`，人数系数 `1 + 0.35 * (playerCount - 1)`；羊形价格（需求 §7.3）；组队规则——前 2 波只出 `grunt`，第 3 波起引入 `ram`，第 5 波起引入 `elite`，`wave % 5 == 0` 必出 1 只 `king`；生成时在 12 个生成点中挑选"离最近玩家 > 15m"的点，每波最多 3 个生成点同时使用；每 tick 最多生成 8 只（防止单帧雪崩）；波次在预算耗尽且场上无羊时判清空。
- [x] 在 `sim.ts` 第 2 槽调用 AI 求解（顺序：先计算所有意图，再统一写入，保证与实体遍历顺序无关）、第 5 槽推进投射物。
- [x] 客户端：冲锋预警（地面红环 + 音效预兆由 P09 补音）、问号弹的可辨识表现（旋转的问号造型，用简单几何 + 自发光）、羊王血条。
- [x] 测试：
  - [x] 转向：`separation` 在两点重合时输出非零向量；`arrive` 在目标距离内速度收敛到 0。
  - [x] 群体：20 只羊聚集在 3m 内时，分离力使最近两点距离在下个 tick 增大。
  - [x] 状态迁移矩阵：每个状态列出允许的下一状态，非法迁移在单测中不存在。
  - [x] 预算：波次 1/5/10 在 1 人与 4 人下的预算值与需求 §7.4 一致；组队结果的价格总和 ≤ 预算。
  - [x] 确定性：同种子跑 1000 tick，两次羊群位置逐位一致；`grep -rn "Math.random" packages/shared/src/ai` 无结果。
  - [x] Boss：羊王按血量阈值进入阶段 2 并生成恰好 4 只咩咩兵。

## 5. 冻结契约

### 5.1 羊的状态枚举（写入 `Entity.state`，同时是快照的 `state` 字段）

| 值 | 名称 | 适用羊形 | 说明 |
|---|---|---|---|
| `0` | `graze` | 全部 | 待机游走（`grunt`/`elite` 用） |
| `1` | `alert` | 全部 | 发现玩家，转向并加速 |
| `2` | `chase` | 全部 | 追击最近可见玩家 |
| `3` | `windup` | `ram` | 蓄力（1s，玩家可躲） |
| `4` | `charge` | `ram` | 直线冲锋（9 m/s） |
| `5` | `attack` | `grunt`/`king` | 近身撕咬 |
| `6` | `ranged` | `elite` | 保持距离并发射问号弹 |
| `7` | `stagger` | 全部 | 受击硬直（0.25s，冲锋撞墙 1s） |
| `8` | `dead` | 全部 | 倒地下沉淡出（1.5s 后回收实体） |
| `9`–`12` | 羊王阶段标记 | `king` | 阶段 1/2/3 与召唤中，配合 `flags` 位使用 |

### 5.2 羊形数值表（= 需求 §7.3，实现唯一真源）

```ts
export const SHEEP = {
  grunt: { hp: 60,  speed: 2.6, damage: 8,  attackCooldownMs: 1200, price: 1,  radiusM: 0.5, heightM: 0.9 },
  ram:   { hp: 140, speed: 3.2, damage: 22, chargeSpeedMps: 9, price: 3, radiusM: 0.55, heightM: 1.0 },
  elite: { hp: 260, speed: 2.4, damage: 14, boltRangeM: 25, boltCooldownMs: 2500, price: 6, radiusM: 0.6, heightM: 1.1 },
  king:  { hp: 2400, speed: 2.0, damage: 30, summonCount: 4, price: 20, radiusM: 1.6, heightM: 2.4 },
} as const;
```

### 5.3 波次规则（= 需求 §7.4，实现唯一真源）

| 规则 | 值 |
|---|---|
| 预算 | `round(6 + 3.2 * wave + 0.18 * wave^2)` |
| 人数缩放 | `× (1 + 0.35 * (playerCount - 1))`；移速额外 `× (1 + 0.02 * (playerCount - 1))` |
| 首次出现 | 第 3 波 `ram`；第 5 波 `elite`；每 5 波 `king` |
| 波间 | 20 秒；全员准备可跳过，但最少 5 秒 |
| 单 tick 最大生成 | 8 只 |
| 同时使用的生成点 | ≤ 3 个，且离最近玩家 > 15m |
| 波次清空条件 | 预算耗尽 **且** 场上无存活羊 |
| 结束条件 | 第 10 波清空 → `matchEnded`（事件 9），或全员倒地且无法救援 |

### 5.4 AI 确定性铁律

1. AI 只用 `rng('ai')` 与 `rng('spawn')`；`fx` 流只允许用于纯视觉触发（音效/粒子提示），不得影响任何实体状态。
2. 意图计算与写入分两阶段，遍历顺序为实体 id 升序（保证与数组顺序无关）。
3. 禁止使用 `Map` 的键遍历来决定影响结果的行为（仇恨表只做查找，遍历时按玩家 id 升序）。
4. 危险半径/邻居查询使用固定半径与"最近 N 个"策略，N 固定为 12。

## 5.5 实现澄清与披露（P07 执行期冻结）

1. 羊形数值与状态枚举照抄 §5.1/§5.2；但实体没有 per-shape 半径/身高字段，碰撞与命中仍走 config/entity.ts 的按 kind 默认值（羊 0.5/0.9），逐羊形的体积差异（冲撞羊 0.55、羊王 1.6/2.4）留给 P09 精修。
2. AI 状态放在新增的 Entity.ai 字段（不在冻结契约内）：羊形码、仇恨表、计时器、冲锋方向、游走目标、羊王阶段。仇恨表实现为「按房间玩家顺序的定长数组（8 槽）+ 最近可见玩家 + 1.5 倍切换阈值」，替代 §4 的 Map 方案，因此天然满足 §5.4 禁止用 Map 遍历决定行为的要求。
3. SimEvent.type 是字符串（'playerHit' / 'waveStart' / ...），net/protocol.ts 的 EVENT_TYPE 数值映射仍为 9 项，未新增事件类型；激活事件的产出集中在 resolve/attack/director。
4. 导演在服务器 runTick 内、stepWorld 之后运行：stepWorld 开头会清空 world.events，放在其前会丢事件，放在其后才能把 waveStart/waveClear/matchEnded 广播出去。
5. 波间固定 20 秒；「全员准备可跳过但最少 5 秒」尚未接线（需要 P08 的准备/开始流程），当前只按 20 秒倒计时。第 10 波清空后导演置 finished 并产出 matchEnded（事件 9），MATCH_PHASE 的 ended 状态留给 P08。
6. 击退实现为「速度冲量 + 时长」，在冲量期间覆盖玩家输入速度：这是为了避免触发 P05 的速度反作弊（若与输入速度叠加，单 tick 位移会超过 maxSpeed×dt×1.15）。客户端未预测击退，被命中时会出现 ≤1.5m 的硬校正。
7. 问界弹是 projectile 实体：14 m/s、3 秒存活、命中玩家或撞谷仓/围栏即销毁；尚不能被玩家射击提前引爆（P1 项，留待 P09/P10 视效果决定）。
8. 羊的 dead 状态（值 8）有进入路径与 1.5 秒回收计时，但 P06 的击杀路径仍是立即 despawn + sheepKilled 事件；尸体淡出与受击白闪属于表现层，留给 P09。
9. 邻居集合每 tick 全量重算（约 60 只时 3.6k 次距离判定/帧），§8 提出的「每 3 tick 错帧更新」未实现。
11. 代码评审修复：导演只在 MATCH_PHASE.playing 阶段推进（此前波间会因 director.wave 与 room.wave 不一致而重复规划并立刻刷羊，等于没有波间休息）；resetCombatState 现在清空击退冲量；羊的攻击判定改用 SHEEP_STATE 常量；删除了 ai/sheepState.ts 中与 sheepBrain 重复且缺 flock 字段的死代码副本。
12. 已知未修（评审记录）：Metrics 的 sheepAlive/waveCurrent 为进程级 gauge，多房间时后写覆盖（单房间即准确）；导演的价格常量仍是字面量（未复用 SHEEP[].price）；flocking 的分离项与 steering.separation 有重复实现；updateSheepIntent 单函数较长（约 180 行）且各羊形的转向收尾重复三次；Entity.ai 为所有 kind 都分配（含玩家与投射物）。
10. 未完成：客户端表现（sheepViews.ts 的冲锋预警环、问号弹造型、羊王血条）与 HUD 波次/Boss 血条未实现；服务器侧波次集成测试未新增（导演逻辑在单元层覆盖）；§6 第 4/5/7 条需人工验收。


## 6.1 实测证据（本次执行）

| # | 命令 | 结果 |
|---|---|---|
| 1 | pnpm check | 全绿：29 测试文件 / 208 用例（shared 129 / server 49 / client 30）+ typecheck / eslint / prettier / check-docs |
| 2 | pnpm --filter @ac/shared exec vitest run -t determinism | 同种子 1000 tick 的羊群位置/速度/状态逐位一致 |
| 3 | grep -rn Math.random packages/shared/src/ai | 无输出（AI 只用 rng('ai') 与 rng('spawn')） |
| 4 | 转向用例 | separation 两点重合输出非零向量；arrive 目标距离内收敛到 0 |
| 5 | 群体用例 | 贴脸邻居产生分离力把两只羊推开；邻居上限固定 12，超出容量被忽略 |
| 6 | 状态矩阵用例 | 13 个状态全部有进入与离开路径（终态 dead 无出边） |
| 7 | 预算用例 | 1/5/10 波基础预算 9/27/56；4 人第 5 波 = round(27×2.05) = 55；1–10 波 × 1–4 人的组队价格总和 ≤ 预算，前两波只出咩咩兵，第 3 波起有冲撞羊，第 5 波起有问界羊且每 5 波恰好 1 只羊王 |
| 8 | 生成约束用例 | 单帧生成 ≤ 8 只；生成点距最近玩家 > 15m |
| 9 | 羊王用例 | 生命 66% 进入阶段 2 并在 8 秒后恰好召唤 4 只咩咩兵，33% 进入阶段 3 |
| 10 | curl -s localhost:8787/metrics | 含 ac_sheep_alive、ac_wave_current、ac_spawns_total |


## 6. 验证

| # | 命令 | 期望输出 | 失败意味着 |
|---|---|---|---|
| 1 | `pnpm --filter @ac/shared test` | AI/波次用例全通过 | AI 行为不可回归 |
| 2 | `pnpm --filter @ac/shared exec vitest run -t determinism` | 1000 tick 羊群轨迹逐位一致 | 确定性破坏，P05 的预测前提被动摇 |
| 3 | `grep -rn "Math.random" packages/shared/src/ai` | 无输出 | 违反 AI 确定性铁律 |
| 4 | `pnpm dev` + Edge 手动：打第 1 波 | 咩咩兵成群逼近并可被击杀；清空后进入波间（20 秒倒计时） | 导演或事件链路错误 |
| 5 | Edge 手动：打到第 5 波 | 出现 1 只羊王与问界羊；冲撞羊蓄力时地面出现红环 | 生成规则或预警表现缺失 |
| 6 | `pnpm --filter @ac/server test` | 波次推进集成测试通过（含人数缩放） | 服务器侧导演未接入 |
| 7 | 手动：4 人局与 1 人局的第 5 波预算对比 | 4 人局羊明显更多（预算 ≈ 2.05 倍） | 人数缩放未生效（FR-08） |
| 8 | `curl -s localhost:8787/metrics` | 含 `ac_sheep_alive`、`ac_wave_current`、`ac_spawns_total` | 缺乏可观测性 |

## 7.1 DoD 状态

| # | DoD | 状态 | 证据 |
|---|---|---|---|
| 1 | pnpm check 全绿 | 通过 | §6.1 #1 |
| 2 | 1000 tick 羊群位置逐位复现 | 通过 | §6.1 #2 |
| 3 | 9 个状态（+ 羊王阶段标记）都有进入与离开路径 | 通过 | §6.1 #6（13 个状态值全覆盖，终态无出边由矩阵定义） |
| 4 | 预算与人数缩放一致，组队价格 ≤ 预算（1–10 波 × 1–4 人） | 通过 | §6.1 #7 |
| 5 | §6 第 4/5/7 条人工验收（第 1 波能打完、第 5 波有羊王与问界羊、4 人局更难） | 未执行 | 需本机 Edge 人工操作，代理无法交互浏览器；客户端表现与 HUD 亦未实现，记为部分通过 |
| 6 | 单帧生成 ≤ 8 只且生成点距最近玩家 > 15m | 通过 | §6.1 #8 |


## 7. DoD（验收标准）

1. `pnpm check` 全绿。
2. 确定性：1000 tick 的羊群位置逐位复现（§6 第 2 条）。
3. 状态机完整性：9 个状态（+ 羊王阶段标记）在单测中都有至少一条进入与离开路径。
4. 波次预算与人数缩放与需求 §7.4 完全一致；组队价格总和 ≤ 预算（属性测试覆盖 1–10 波 × 1–4 人）。
5. §6 第 4、5、7 条手动验收通过：第 1 波能打完、第 5 波有羊王与问界羊、4 人局明显更难。
6. 单帧生成不超过 8 只（单测断言），生成点距最近玩家 > 15m。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 羊群"贴脸堆叠" | 手动观察大量羊体重合 | 分离权重提到 1.6、加入实体间最小距离硬约束（同 tick 内推开） |
| AI 抖动（来回切换目标） | 羊在原地左右摇摆 | 仇恨衰减 + 目标锁定时间（切换需累计仇恨超过当前目标 1.5 倍） |
| 冲撞羊过于致命 | 单人局频繁被冲锋致死 | 蓄力 1s 有明确预警；伤害与击退在 `config/sheep.ts` 可调，配 P10 的胜率基线 |
| 问界羊远程压制导致被迫躲藏 | 玩家反馈"只敢躲" | 问号弹速度 14 m/s 可躲；`elite` 保持 15–25m 距离，允许玩家拉近压制 |
| 生成点在玩家身后导致不公平 | 手动体感"背后刷怪" | 生成点距最近玩家 > 15m 已冻结；必要时提高到 18m（改需求 §7.4 前先记录） |
| AI 性能（O(n²) 邻居） | 100 只羊时帧时间超标 | 邻居上限 12 且每 3 tick 才重算一次邻居集合（错帧更新），仍保持确定性 |

**回滚目标**：`HANDOFF-H06`。删除 `shared/src/ai/**` 与 `config/{sheep,waves}.ts`，`sim.ts` 第 2、5 槽退回空实现（羊静止不动，但射击、网络、渲染均可用）。

## 9. 移交物

**移交物 ID**：HANDOFF-H07

稳定接口与能力清单：

1. 四种羊形的数值表与状态枚举（`config/sheep.ts`、§5.1 的 `state` 值表）。
2. 波次导演：预算公式、人数缩放、生成点选择策略、波间时长、结束条件。
3. AI 模块：转向、群体、目标选择、羊形状态机、羊王阶段、投射物推进。
4. 客户端表现：冲锋预警环、问号弹造型、羊王血条。
5. 新增指标：`ac_sheep_alive`、`ac_wave_current`、`ac_spawns_total`。
6. 已验证能力：第 1 波可清、第 5 波有 Boss、4 人局难度显著上升、1000 tick 羊群确定性复现。
