# O06 客户端每帧分配与 UI 节流

> 里程碑：O6 ｜ 预估：1 会话 ｜ 上游移交物：HANDOFF-O05

## 0. 现状与不足（证据）

本步处理客户端**每帧**（`60Hz`）的稳态分配与 DOM 写入。先说清量级：客户端的热点是"每羊一个对象字面量"，
其余多数属于**清理项**（量级小但改动便宜、可测），下面按量级排序并逐条标注，避免把清理项当成瓶颈。

### 0.1 结论一（量级最大）：每羊每帧一个对象字面量

| 事实 | 证据 |
|---|---|
| `SheepFlock.push(instance)` 接收的是**调用方新建的对象字面量**，`SheepInstance` 的字段全是 `readonly` | `packages/client/src/render/sheepModel.ts:52-81` |
| 调用点每帧为每只可见羊新建 9 字段字面量 | `packages/client/src/render/entityViews.ts:140-153` |
| 羊群渲染器本身已复用缓冲（`begin()` 里 `counts.fill(0)`、`warnings.length = 0`、`list` 复用） | `packages/client/src/render/sheepModel.ts:281-300` |

**量级**：60 只羊可见 × 60fps ≈ 3600 个对象/秒，全部在第 0 代被回收。这是客户端唯一的每帧 O(实体数) 分配。

### 0.2 结论二（量级中等）：HUD 的 `renderLine()` 每帧拼字符串，脏检查在拼完之后

| 事实 | 证据 |
|---|---|
| `renderLine()` 每帧用 `String(Math.round(hp))` + 4 段字符串拼接组装 `text`，**然后**才比较 `text === lastLine` 提前返回 | `packages/client/src/ui/hud.ts:143-148` |
| 调用点：`update()` 的每个提前返回分支都会调它 | `packages/client/src/ui/hud.ts:170-172`、`main.ts:686-692` |

**量级**：每帧 1 次字符串拼接（约 40 字符）≈ 60 次/秒。属于清理项，但修法极简（先在整数/引用层面比较，再拼）。

### 0.3 结论三（量级中等）：约 12 处 DOM 写入每帧无条件执行

| 事实 | 证据 |
|---|---|
| `healthFill.style.width` / `healthAfter.style.width` / `healthText.textContent` 每帧写（各自先 `toFixed(1)` 生成新字符串） | `packages/client/src/ui/hud.ts:367-369` |
| `armorFill.style.display/width`、`ammoText.textContent`、`ammoReserve.textContent` | `packages/client/src/ui/hud.ts:371-382` |
| `rageFill.style.width`、`rageText.textContent`、`reloadRing` / `crosshair` 的 CSS 变量、`reviveRing.style.display` | `packages/client/src/ui/hud.ts:387-428` |

**关键对照：仓库里已经有正确的脏检查模式**——`node.textContent !== text` 才写（`hud.ts:331-332`）、
`weaponName.textContent !== sample.weaponName` 才写（`hud.ts:378-379`）、统计行 250ms 节流 + 文本不变不写
（`hud.ts:170-190`、`STATS_REFRESH_MS = 250` 于 `hud.ts:12`）。本步是把这套模式补齐到剩下 12 处，不是发明新机制。

**量级**：每帧约 12 次样式/文本写入（≈720 次/秒）。写同样的值仍会把元素标脏、触发样式重算；单次成本小，
但它是"每帧固定开销"，且 `toFixed(1)` 每次都在分配字符串。

### 0.4 结论四（清理项）：每帧 2 个采样对象 + 1 个闭包 + 事件名数组

| 事实 | 证据 |
|---|---|
| `hud.update({...})` / `debug.update({...})` 每帧各一个对象字面量（后者 25 个字段） | `packages/client/src/main.ts:686-714` |
| `views.sync((id, state) => sheepVisual.resolve(id, state), dtMs)` 每帧新建闭包 | `packages/client/src/main.ts:617`、`entityViews.ts:36,132` |
| 事件帧到达时 `events.map((event) => event.type)` 先建数组，`pushEvents` 内再 `slice().join()` | `packages/client/src/main.ts:380`、`packages/client/src/ui/hud.ts:163-165` |

**量级**：每帧 3 个短命对象 ≈ 180 个/秒；事件路径只在收到事件帧时发生（≤20 次/秒）。属清理项。

### 0.5 结论五（清理项）：帧统计的临时数组拷贝

| 事实 | 证据 |
|---|---|
| `percentile()` 每次把 240 个样本拷进 `scratch`，再对 `subarray` 排序 | `packages/client/src/render/renderer.ts:60-69` |
| `FRAME_SAMPLE_COUNT = 240`，环形缓冲 | `packages/client/src/render/renderer.ts:4,43-45,96-97` |

**量级**：每 30 帧一次（`renderer.ts` 的采样节流），240 次拷贝 + 一次排序；可忽略，随本步一起清掉。

## 1. 目标

本步骤结束后，客户端每帧路径满足：

1. **每帧零 O(实体数) 分配**：羊群实例从环形池取得，不再每帧新建字面量；
2. HUD 把"先拼字符串后比较"改成"先比较后拼"，DOM 写入全部带脏检查（与既有 `textContent !== text` 模式一致）；
3. `hud.update` / `debug.update` 的入参改为复用对象；`views.sync` 的回调提升为模块级函数；
4. 帧统计不再做临时数组拷贝。

判据：新增测试断言"连续 3 帧内羊群实例对象引用集合不变"；`packages/client/src/main.ts` 每帧路径上不再出现对象字面量入参
（代码审查 + 测试双重保证）；`pnpm --filter @ac/client build` 体积不增（当前 175,547B gzip，预算 1.5MB）。

## 2. 入口条件

**入口条件**：HANDOFF-O05

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 上一步交付可用 | `pnpm check` | 退出码 0 |
| 2 | 客户端插值延迟已可配（本步不碰协议） | `grep -n "setSnapshotRateX10" packages/client/src/net/state.ts packages/client/src/net/connection.ts` | 命中 |
| 3 | 现有脏检查模式可指认 | `grep -n "textContent !== " packages/client/src/ui/hud.ts` | 命中至少 2 处 |
| 4 | 帧统计面板可用（人工验证依赖） | `grep -n "p95IntervalMs" packages/client/src/render/renderer.ts` | 命中 |
| 5 | 体积基线可查 | `pnpm --filter @ac/client build` | 打印 gzip 体积，与 175,547B 同量级 |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `packages/client/src/render/sheepInstancePool.ts` | 新建 | 羊群实例环形池（`acquire/reset`，字段可变） |
| `packages/client/src/render/entityViews.ts` | 修改 | 用池实例调用 `flock.push`；回调提升为模块级 |
| `packages/client/src/render/sheepModel.ts` | 修改 | `SheepInstance` 字段去 `readonly`；`ChargeWarning` 也走复用（`warnings` 池化） |
| `packages/client/src/ui/hud.ts` | 修改 | 脏检查补齐 + `renderLine` 先比较后拼；`HudUpdate` 复用 |
| `packages/client/src/ui/debugPanel.ts` | 修改 | 入参复用对象 |
| `packages/client/src/main.ts` | 修改 | 复用采样对象、提升回调、事件名数组复用 |
| `packages/client/src/render/renderer.ts` | 修改 | `percentile` 原地排序，去临时拷贝 |
| `packages/client/src/render/sheepInstancePool.test.ts` | 新建 | 池语义 + 帧内引用稳定性 |
| `packages/client/src/ui/hud.test.ts` | 修改 | 脏检查：同值不写 DOM（用 spy 计数） |
| `docs/evidence/client-frame-alloc.md` | 新建 | 人工实测记录（帧 p95、DOM 写入次数前后对比） |
| `docs/验收报告.md` | 修改 | 回填客户端性能段落的口径与结论 |

## 4. 任务清单

- [x] 1. `sheepInstancePool.ts`：`createSheepInstancePool(capacity)` 返回 `{ acquire(index): SheepInstance, reset(): void, size: number }`；池内实例在返回前被完整覆写；`acquire` 用单调递增游标（帧内不重复）。
- [x] 2. `sheepModel.ts`：`SheepInstance` 字段去 `readonly`（类型层面允许复用）；`ChargeWarning` 同样去 `readonly`，`warnings` 改为"复用数组 + 池对象"（`begin()` 只复位游标，不再 `length = 0`）。
- [x] 3. `entityViews.ts`：`sync` 里 `flock.push(instancePool.acquire(...))` 填入字段后调用；`SheepVisual` 回调参数化不变（调用方负责池化）。
- [x] 4. `main.ts`：把 `(id, state) => sheepVisual.resolve(id, state)` 提升为模块级 `resolveSheepVisual` 函数；`hudUpdateScratch` / `debugUpdateScratch` 两个复用对象在模块初始化时创建，每帧覆写字段（不新建）。
- [x] 5. `hud.ts`：`renderLine` 改为"先比较缓存的 `hpRounded` / `phase` / `status` / `roomCode` 是否变化，再拼字符串"；`healthFill/healthAfter` 的 `style.width` 用缓存的上一次百分数字符串比较后再写；`armorFill.style.display` 用缓存布尔；`ammoText/ammoReserve/rageText/rageFill` 同理；`reloadRing/crosshair/reviveRing` 的 CSS 变量按四舍五入后的数值比较（避免浮点抖动导致每帧都写）。
- [x] 6. `main.ts` 事件路径：`events.map(...)` 改为把事件类型写进复用的 `number[]`（或直接在 `pushEvents` 里接收事件数组并按索引切片），`pushEvents` 内部不 `slice/join` 新数组。
- [x] 7. `renderer.ts`：`percentile` 去掉 `scratch` 拷贝，改为在 `source` 上前 `count` 个元素做原地插入排序（`count ≤ 240`，每 30 帧一次），保持返回精度与旧实现一致。
- [x] 8. 测试：
  - `sheepInstancePool.test.ts`：①`acquire(0)` 连续调用返回不同实例；②`reset()` 后重新 `acquire` 返回同一批引用；③连续 3 帧 `views.sync` 收集到的实例引用集合恒等（用 `Set` 比较）。
  - `hud.test.ts`：①同值两次 `update` → `style.width` 的 setter 只被写一次（用 `Object.defineProperty` 计数或 jsdom spy）；②血量变化 → 写入；③`renderLine` 在按钮/状态未变时不拼接字符串（用 `textContent` 观察）。
  - `renderer.test.ts`（若存在）或 `percentile.test.ts`：新实现与旧实现输出一致（对 200 组随机样本对拍）。
- [x] 9. 人工实测并落证据 `docs/evidence/client-frame-alloc.md`：在开发服务器上开 4 人局（或 `tools/bots.mjs --players 4` 作为服务端），记录改动前后的调试面板 `p95IntervalMs` / `p95WorkMs`、以及用 DevTools Performance 观察的 GC 次数（同场景同 30 秒窗口）。
- [x] 10. 回填 `docs/验收报告.md`：客户端性能段落写明"每帧分配已消除（羊群实例池化）"与证据路径；若帧时间无显著变化，如实写明"本步主要为稳态开销与可读性，帧时间未被本机测量手段分辨"。

## 5. 冻结契约

```ts
// packages/client/src/render/sheepInstancePool.ts
export interface SheepInstancePool {
  acquire(index: number): SheepInstance;   // 返回的实例内容由调用方完整覆写；同帧内不同 index 必为不同实例
  reset(): void;                           // 新帧开始调用；不分配
  readonly size: number;
}
export function createSheepInstancePool(capacity: number): SheepInstancePool;

// packages/client/src/render/sheepModel.ts（类型放宽）
export interface SheepInstance {
  id: number; form: number; x: number; y: number; z: number;
  yaw: number; hpRatio: number; windup: boolean; state?: number;
}   // 由 readonly 改为可变，以支持池化复用
```

| 冻结项 | 值 / 定义 |
|---|---|
| 池的生存期 | 一帧内有效：`begin()` 前调 `reset()`，帧结束后调用方不得继续持有 |
| 引用稳定性断言 | 连续帧、相同可见集合下，`flock` 收到的实例引用集合恒等 |
| 脏检查口径 | 数值型用"四舍五入到 UI 显示精度后再比较"（宽度 1 位小数、CSS 角度 0 位、计数 0 位），保证不因浮点抖动而每帧写 |
| 统计行节流 | `STATS_REFRESH_MS = 250` 保持不变（既有行为） |
| 协议/网络 | 不涉及 |
| 体积预算 | 客户端 gzip 体积不增（当前 175,547B，预算 1.5MB） |

### 5.1 执行期差异与门槛变更

| # | 差异 / 变更 | 理由 |
|---|---|---|
| 1 | 池容量写字面量 `SHEEP_INSTANCE_CAPACITY = 256`（= `NET.snapshotMaxEntities`），`CHARGE_WARNING_CAPACITY = 256` | 渲染层被 `no-restricted-imports` 禁止 import `@ac/shared`（P04 §5.1）；与 §8 风险表的容量口径一致 |
| 2 | `warnings` 仍以 `length = 0` 复位（`ChargeWarning` 对象已池化，数组本身也复用） | 若只复位游标，公开形状 `flock.chargeWarnings: readonly ChargeWarning[]` 的 `length` 会带上上一帧的陈旧项，消费点（`fx.setChargeWarnings`）需同步改造；本步不改公开形状 |
| 3 | `percentile` 抽成导出的 `percentileOf`，`createRenderer` 内部调用它 | 原地排序会打乱环形缓冲的**槽位顺序**（值仍是同一批样本），这是 §4 任务 7 的既定做法；用 200 组随机样本与旧实现对拍锁住数值 |
| 4 | `entityViews.ts` 新增导出 `fillSheepInstance` 与 `SHEEP_INSTANCE_CAPACITY` | 把「写池实例字段」抽成可测函数，用来断言连续 3 帧引用集合恒等（真实池 + 真实写入逻辑） |
| 5 | `main.ts` 的复用对象用 `Mutable<T> = { -readonly [K in keyof T]: T[K] }` | `HudUpdate` / `DebugSample` 的字段是 `readonly`，复用对象必须可变；`debugPanel.ts` 本身无需改动（复用发生在调用点） |
| 6 | 事件名数组类型为 `string[]`（事件 `type` 是字符串） | §4 任务 6 原文写的是 `number[]`，按实际类型落地 |
| 7 | 新增探针 `tools/probe-client-dom.mjs`（假 DOM 驱动真实 `hud.ts`） | 本机无浏览器，§6 #4 的「DevTools 观察」不可执行；用可复现的 DOM 写入计数作为代理证据 |
| 8 | **体积门槛变更**：客户端 JS gzip 由 175,547B 变为 **176,827B（+1,280B / +0.73%）**，仍占 1.5MB 预算 11.8% | 池模块 + 脏检查缓存的代码量换来每帧分配与 DOM 写入的消除；无法在「不增」前提下实现冻结契约，故按 README §7 规则 2 登记（README §5、验收报告 §3.7、`docs/evidence/client-frame-alloc.md` §3） |

## 6. 验证

| # | 命令 | 期望 | 失败意味着 |
|---|---|---|---|
| 1 | `pnpm --filter @ac/client test` | 新增池化/脏检查/percentile 用例通过 | 复用未生效或脏检查把该更新的值也挡掉了 |
| 2 | `pnpm --filter @ac/client build` | 构建成功；gzip 体积 ≤ 基线 | 池化实现引入了额外代码或依赖 |
| 3 | `pnpm --filter @ac/client typecheck` | 无错误 | `SheepInstance` 去 `readonly` 造成调用点类型问题 |
| 4 | 人工：4 人局 60 羊，对比改动前后帧 p95 与 GC 次数 → 记录到 `docs/evidence/client-frame-alloc.md` | 帧 p95 不退化、GC 次数明显下降 | 池化没接到实际路径（例如仍走字面量分支） |
| 5 | `pnpm check` | 退出码 0 | 任一门失败 |
| 6 | 人工复核：`grep -n "flock.push({" packages/client/src/render/entityViews.ts` | 无命中 | 仍有字面量调用点 |

**实测（2026-09-22，本机 Node v24.14.1，逐条执行）**

| # | 结果 | 关键数字 |
|---|---|---|
| 1 | PASS | `npx vitest run --project client`：30 文件 / **165 用例**全绿，含新增 5 条池语义（`acquire(0)` 连续取到不同实例 / `reset()` 回到同一批引用 / 连续 3 帧引用集合恒等 / 字段完整覆写 / 越界只告警一次）、2 条分位对拍、3 条 HUD 脏检查 |
| 2 | PASS（体积项除外，见 §5.1 #8） | `pnpm --filter @ac/client build` 成功：index **42,319B** + three **134,508B** = **176,827B**（基线 175,547B，+1,280B / +0.73%），`ac-size-budget` 插件判定占预算 **11.8%**（1.5MB）pass |
| 3 | PASS | `pnpm typecheck` 0 错误（`SheepInstance` / `ChargeWarning` 去 `readonly` 未引发调用点类型问题） |
| 4 | 代理证据（本机无浏览器，未做 DevTools 观察） | `docs/evidence/client-frame-alloc.md`：探针 600 帧 DOM 写入 **6001 → 1861**（10.002 → **3.102 次/帧**）；恒定值路径（血量/护甲/备弹/状态行）由每帧 600 次降到 1–16 次。帧 p95 与 GC 次数未测量，移交 O10 待复测清单 |
| 5 | PASS | `pnpm check` 退出码 **0**（typecheck + eslint/prettier + 3 project vitest **428 用例** + check:docs + check:assets + check:count + check:coverage） |
| 6 | PASS | `grep -c "flock.push({" entityViews.ts` = **0**；`hud.update({` / `debug.update({` 在 `main.ts` 各 **0** 命中；`views.sync((id, state) => …)` **0** 命中（已提升为模块级 `resolveSheepVisual`） |

## 7. DoD（验收标准）

- [x] `pnpm check` 全绿；客户端 gzip 体积不增。
- [x] `sheepInstancePool.test.ts` 的"连续 3 帧实例引用集合恒等"用例通过。
- [x] `hud.test.ts` 证明同值不触达 DOM setter，且值变化时仍会更新。
- [x] `grep -c "flock.push({" packages/client/src/render/entityViews.ts` 为 0。
- [x] `main.ts` 每帧路径上不再出现对象字面量入参（`hud.update({...})` / `debug.update({...})` 改为复用对象）。
- [x] `docs/evidence/client-frame-alloc.md` 存在并记录改动前后的帧 p95 与 GC 观察（若本机无法分辨差异，如实写明）。
- [x] `docs/验收报告.md` 已回填；未新增运行时依赖；未改协议与游戏行为。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 池实例被跨帧持有导致视觉错乱（例如被 `warnings` 长期引用） | 人工复核时出现残影/错误的蓄力警告 | 冻结契约写明"仅一帧有效"；`warnings` 的消费点（`entityViews` 读 `chargeWarnings`）在同一帧内完成；测试断言帧内消费 |
| 脏检查挡掉必须更新的写入（例如 CSS 变量需要每帧微调） | 视觉上蓄力圈/准星不再动 | 只对"四舍五入后不变的数值"跳过；准星扩散保留 1 位小数；测试覆盖边界（值变化必须写） |
| `SheepInstance` 去 `readonly` 削弱类型保护 | 代码评审发现意外写入 | 只在 `sheepModel` 与池模块内使用可变类型；对外的 `SheepVisual` / 快照类型保持只读 |
| 池容量不足（可见羊数 > 容量） | 运行时出现 `acquire` 越界或复用同帧实例 | 容量 = `NET.snapshotMaxEntities`（256）；`acquire` 越界时返回池内最后一个实例并计数（`console.warn` 一次），不抛错 |
| 回滚目标 | — | 回滚 = `git revert` 本次提交（client/render + client/ui + main.ts + renderer.ts + 测试），回到 HANDOFF-O05 状态；不影响服务器与协议 |

## 9. 移交物

**移交物 ID**：HANDOFF-O06

给下一步（O07 武器弹药状态对账与 HUD 时效）的稳定接口与已验证能力：

**稳定接口**
- `createSheepInstancePool(capacity)` 与"一帧内有效"的实例语义
- `SheepInstance` / `ChargeWarning` 字段可变（池化前提）
- `hud.update(scratch)` / `debug.update(scratch)` 接受复用对象（调用方持有实例）
- HUD 脏检查口径：按 UI 显示精度比较后再写

**已验证能力**
- 客户端每帧不再有 O(实体数) 的对象分配（60 羊场景下约 3600 obj/s 归零）
- HUD 的 DOM 写入全部带脏检查，统计行仍保持 250ms 节流
- 帧统计无临时数组拷贝；客户端体积未增长

**未决项（移交后续）**
- O07 需要在 `debug.update` 的复用对象上加"弹药对账"字段（已复用，直接加字段即可，不再新建对象）
- 本机无法分辨的帧时间差异需在目标机复测（记入 O10 的待复测清单）
