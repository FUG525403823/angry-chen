# 二轮真人试玩六缺陷：根因、修复与浏览器内复验（2026-09-23）

真人试玩反馈原文（逐条照抄）：

1. 我选择步枪开局自动会切换成手枪
2. 弹道在我没子弹点击左键的时候还有
3. 准星并不是十字准星
4. 人物移动画面还是有卡顿
5. 打羊只能打身体才能造成有效伤害，而且羊不会动（拆成 5a 命中体 / 5b 羊不动）
6. 还有，弹道有时候会有两条，一条正常一条随机，也要修复

结论：第 1、2、3、5a、5b、6 条都是确凿的代码缺陷，已修复；第 4 条修掉的是确凿的
「20Hz 和解把渲染节拍清零」根因（本机零延迟下 119 帧里有 79 帧渲染位置原地不动、相机每 3 帧才挪一次）。
修复后在**真实 Edge（硬件 GL：ANGLE + D3D11）+ 生产产物**里 `node tools/e2e-cdp.mjs`
**18/18 断言通过**（报告：`docs/evidence/playtest-cdp-round2.md`），`pnpm test` 77 文件 507 用例全绿。
逐条根因/修复/复验见 §1–§6，门禁见 §7，遗留见 §8。

## 1. 选步枪开局自动变手枪

### 1.1 根因

`packages/server/src/room.ts` 的 `broadcastMatchState` 在**应用会话槽位之前**就用实体槽位回写会话：
只要玩家还活着，每个 tick 都执行 `session.weapon = entity.weapon.activeSlot`。
大厅里选的槽位存在 `session.weapon`，此刻实体还是上一局/默认的手枪（槽位 0），
于是「大厅选步枪(1) → 下一个 tick 被写成 0 → 应用 0 → 开局手枪」，
而且这个覆盖会一直持续到下一次快照，玩家看到的就是"选什么都会变回手枪"。

### 1.2 修复

`packages/server/src/room.ts:750-752`：只有待选槽位**已经落到实体上**（`session.weaponApplied`）之后才允许回写：
`if (alive && session.weaponApplied) session.weapon = entity.weapon.activeSlot;`

### 1.3 复验

- 回归测试：`packages/server/src/match-flow.test.ts` ›「大厅选的武器在开局后仍是所选槽位（不被实体旧槽位回写覆盖）」
  —— 走真实建房流程（`host.welcome()?.pid`）选槽位 1，`harness.advance(1600)` 后读 `getEntity(room.world, pid)?.weapon.activeSlot`。
  修复前实测 `weapon 0`（被写回覆盖），修复后为所选槽位。

## 2. 没子弹点左键还有弹道

### 2.1 根因

曳光生成在**开火裁决之外**：只要采样命令带 `BUTTON.fire` 位（`fireHeld` 一按下就一直带），
每个 30Hz 采样 tick 都会画一条曳光——空弹匣、换弹中、射速节流内的点击全都照画。
客户端武器状态（`localWeapon.tryFire`：空弹匣返回 false）算得对，但没人用它当画弹道的闸门。

### 2.2 修复

`packages/client/src/main.ts:254-286`：曳光与枪口火焰**挪进 `if (localWeapon.onFire(...))` 内**，
与弹药账本（`noteLocalShot`）、枪口特效同一个判定；
全客户端只剩这一个 `fx.spawnTracer` 调用点（`rg spawnTracer packages/client/src` 只命中 `main.ts:276` 与测试）。

### 2.3 复验

- 单测：`packages/client/src/combat/localWeapon.test.ts`（3 例）——「空弹匣恒不开火」「有弹时按射速节流逐发开火，打空后停止」
  「切到没有弹的槽位后立刻不开火」。
- 浏览器内（生产产物，指针已锁定）：按住左键打空弹匣后**连续采样 8 次全部为空弹匣（HUD 显示 0），期间新增曳光 0 条**。
  该断言把「空弹匣状态 + 曳光累计条数」放在**同一次求值**里读，避免两个采样点之间的竞态；
  旧实现（曳光在闸门之外）在空弹匣期间按 30Hz 采样速率出弹道，任意相邻两次采样都会多出 2–3 条。

## 3. 准星不是十字

### 3.1 根因

两处叠加：

- `packages/client/src/ui/hud.ts` 直接把扩散值写成 `--spread-px`（**无单位**的数字字符串），
  而 CSS 里是 `calc(var(--spread-px) + var(--arm-len))` 这类"数字 + 长度"的混合计算。
  按 CSS Values 3 的类型规则，混合加减在**计算值时间**就非法，声明回退成不可用值，
  于是四条臂的 `translateY/translateX` 全部失效、四条 8×2 的短杠叠在中心——看上去就是"一个点/一个方块"，不是十字。
- `hud.ts` 的 `CROSSHAIR_MIN_PX` 是 4，静止时臂与中心的间隙最小只有 4px，也不好看。

### 3.2 修复

- `hud.ts`：写入时带单位（`style.setProperty('--spread-px', String(spreadPx) + 'px')`），`CROSSHAIR_MIN_PX` 4 → 2。
- `packages/client/src/styles.css`（`.hud-crosshair` 段）：改成 `--spread-px / --arm-len(8px) / --arm-thick(2px)` 三个变量，
  四条臂按 side 用 `translateY(calc((var(--spread-px) + var(--arm-len)) * -1))`（上）/`translateY(var(--spread-px))`（下）等，
  并在 `left/top` 上用 `calc(var(--arm-thick) / -2)` 自居中；中心保留 2×2 的点。

### 3.3 复验

- 单测：`packages/client/src/ui/hud.test.ts` 断言语义变量带单位（`--spread-px` === `24px`；修复前实得 `"24.0"`）。
- 浏览器内实测四条臂的**像素几何**（`getBoundingClientRect` 中心距）：
  上 6.0px / 下 6.0px / 左 6.0px / 右 6.0px，`--spread-px = 2px` —— 四臂各在正确一侧、到中心对称（容差 0.75px）。
  这比"看得见十字"更严格：旧实现下上/左两臂会落在中心 0px，断言直接判负。

## 4. 人物移动画面卡顿（20Hz 和解顿挫）

### 4.1 根因

渲染 60fps、采样 30Hz、权威和解 20Hz。预测器用"时间累加器 + 50ms 固定子步"推进，
但 `setAuthoritative()` 每次和解都把 `accumulatorMs` **清零**：3 帧里只有第 3 帧能攒够一个子步，
另外两帧 `steps === 0`、状态与速度都不更新，`renderPosition` 的外推速度还是上一帧的旧值
（水平移动时表现为"每 3 帧才挪一次"，站着不动或急停时表现为"原地卡住"）。
进程内复现（本机零延迟、60fps 渲染 + 30Hz 采样 + 20Hz 和解，120 帧）：**79 帧原地不动**。

### 4.2 修复

`packages/client/src/prediction/predictor.ts`：

- `setAuthoritative` **不再清零 `accumulatorMs`**（`:57-58` 注释说明它是"渲染节拍余量"而不是模拟状态）。
- `advance` 在本帧没攒够子步时（`steps === 0`）仍把当前命令写进状态（`applyCommandToState`，`:73-75`），
  这样 `renderPosition` 用的速度始终是本帧意图，而不是和解后残留的 0 速度。

### 4.3 复验

- 单测：`packages/client/src/prediction/prediction.test.ts` › 新增
  「渲染节拍（60fps 渲染 + 30Hz 采样 + 20Hz 和解）」两例，逐帧统计 `frozen`（位移 < 期望一半）、
  `spikes`（> 期望 1.5 倍）、`hard`（硬纠正次数）与 `ahead`（预测超前量）：
  零延迟确认要求三项全 0 且 `|ahead| < 0.05m`；100ms 下行延迟要求三项全 0 且 `ahead > 0`（方向正确不倒退）。
  还原两处改动后该用例立刻判负（零延迟场景 79 帧 frozen）。
- 浏览器内（生产产物）帧率与分阶段读数：fps 中位 **60.0**、帧间隔 p95 中位 **16.8ms**、
  每帧 JS 工作 p95 ≤ **1.2ms**（stages：predict 0.1 / sync 0.2 / draw 1.1）。

## 5. 打羊只能打身体才有效；羊不动

### 5a. 描头打不出伤害、羊王只有一半体积算命中

#### 根因

服务端射线判定用的是**与羊种无关**的胶囊（半径 0.5m、高 0.9m），而渲染体是按羊种缩放的躯干盒 + 头盒：

| 羊种 | 形体缩放 | 渲染头顶 | 渲染躯干半宽 | 旧口径上界 |
|---|---|---|---|---|
| 咩咩兵 | 1.00 | 1.12m | 0.55m | 1.00m（见下） |
| 冲撞羊 | 1.06 | 1.19m | 0.58m | 1.00m |
| 问界羊 | 1.12 | 1.25m | 0.62m | 1.00m |
| 羊王 | 1.60 | 1.79m | 0.88m | 1.00m |

旧口径 `topM = 0.9 < 2 × radiusM = 1.0`：胶囊轴段首尾颠倒（顶端球落到下端球之下），
实际命中上界只有 1.0m，且比渲染体窄。于是**抬起头瞄准羊头/羊王上半身的射线整段落空**，
只有打躯干中心才"有效伤害"——与反馈完全一致。

#### 修复

- `packages/shared/src/config/sheep.ts`：新增 `SHEEP_HIT`（`radiusM / topM / headMinM / torsoMinM`）按羊种取整：
  咩咩兵 `0.55 / 1.15 / 0.78 / 0.34`、冲撞羊 `0.59 / 1.22 / 0.83 / 0.36`、
  问界羊 `0.62 / 1.29 / 0.87 / 0.38`、羊王 `0.89 / 1.84 / 1.25 / 0.54`
  （半径按渲染躯干半宽**向上取整**，且每个羊种都满足 `topM > 2 × radiusM`，杜绝退化胶囊）。
  `SHEEP.radiusM / heightM` **保持不变**：它们是移动、分离、攻击距离的口径，改它会动到 AI 与手感。
- `packages/shared/src/config/combat.ts`：新增 `partForThresholds(baseY, headMinM, torsoMinM, hitY)`；
  原有 `partForHeight` **逐位不动**，玩家命中判定不受影响。
- `packages/shared/src/combat/resolve.ts`：`traceRay` 里按 `SHEEP_ORDER[target.ai.sheepKind]` 取对应档案
  （未知羊种回退 `grunt`），半径/高度/头躯分界都取自档案。

#### 复验

- 单测（`packages/shared/src/combat/resolve.test.ts` ›「羊的命中体与渲染模型对齐」）：
  每个羊种 × 距离 {4, 10, 20} m 断言"头顶下方 5cm 与头盒中心判头、躯干中心判躯干、躯干下界以下 10cm 判四肢、
  头顶上方 10cm 判 miss、`radiusM − 5cm` 处判躯干、`radiusM + 10cm` 处判 miss"，另加羊王 1.5m 贴脸描头。
  另有一例直接对拍旧口径：**旧 0.5/0.9 通用胶囊在头顶与羊王身上整段落空**。
  用旧口径跑同一组断言 → 5 例失败（形如 `期望 ≥ 1.12，实得 0.9`）。
- 单测（`packages/client/src/test/sheepHit.test.ts`，跨层：渲染盒 × `SHEEP_FORM_SCALE` ↔ `SHEEP_HIT`）：
  命中胶囊必须覆盖头盒与躯干盒、不得比躯干窄、`headMinM` 落在头盒区间内、`torsoMinM` 在腿与躯干之间、
  `topM > 2 × radiusM`。旧口径下 4 例失败。
  （放在 `src/test/` 而非 `src/render/`：`render/**` 按 P04 §5.1 禁止 import `@ac/shared`。）

### 5b. 羊不会动

#### 根因

`packages/shared/src/ai/sheepBrain.ts` 的警戒分支是"每逢发现玩家就把警戒计时重置并转 `alert`"：
`alert → alert` 这种**同状态迁移**也返回 true，于是 `ai.timerMs` 每个 tick 都被重新武装成 300ms，
永远到不了 0；而唯一的"转追击"分支写在 `else if (ai.timerMs <= 0)` 里——**死代码**。
结果：视野内的羊永远停在警戒原地（表现就是"羊不会动"）。

#### 修复

只有**真正迁入**警戒态时才武装计时：`if (entity.state === SHEEP_STATE.graze) { setSheepState(alert); ai.timerMs = SHEEP_ALERT_MS; }`
`else if (ai.timerMs <= 0) { setSheepState(chase); }`——计时到期后正常转入追击。

#### 复验

- 单测：`packages/shared/src/ai/ai.test.ts` ›「羊警戒→追击（真人试玩：羊不会动）」：
  1 只咩咩兵距玩家 5m 时进入 `alert` 且计时为 300ms；随后每个 50ms tick 计时**递减**（不再被重新武装）；
  12 tick 后状态转为 `chase`，且 5 个 tick 内与玩家的距离**真实缩短 > 0.5m**。
  修复前该用例在"计时递减"处即判负（`期望 < 300，实得 300`）。
- 浏览器内：开战后采样 9 只羊的位置序列，**9/9 只都在移动**。

## 6. 一次开火出现两条弹道

### 6.1 根因

一次开火有**两个**曳光生成点：

1. 本地预测：采样命令带 fire 位时按相机方向画一条 30m 长的曳光（§2 修好闸门后仍在）。
2. 权威命中：每个 `playerHit` 事件再画一条——起点在**事件时刻**用 `camera.getWorldDirection` + `muzzleOrigin` 现算，
   终点是模拟胶囊上的命中点（比渲染体小）。相机/枪口位置在事件到达时已经变过，
   于是这条线与本地那条方向、长度都不同——"一条正常一条随机"。
   霰弹一发 8 颗弹丸 ⇒ 一次开火最多 9 条。

### 6.2 修复

`packages/client/src/main.ts:503-519`：命中事件分支只保留 `fx.showHitMarker`（命中标记）、
`combatHud.pushDamage`（伤害数字）、`fx.spawnImpact`（弹着点）与受击闪白；
删掉那条权威曳光。不变式（注释写在代码里）：**一次开火只画一条曳光**，只有本地预测那条。

### 6.3 复验

- 浏览器内：准星对准最近的羊连射 1 秒（手枪 300rpm ⇒ 最多 6 发），
  曳光 mesh 的 `count`（= `min(容量, 累计生成条数)`）**只增加 4 条**，且期间羊的 `hpRatio` 确实下降（命中）。
  旧实现每条命中再补一条 ⇒ 1 秒命中约 10 条，断言（≤ 6）直接判负；霰弹一发更会到 9 条。
- 单测：`packages/client/src/combat/localWeapon.test.ts` 保证"开火闸门"本身正确（§2），
  加上 grep 级别的"唯一生成点"事实（`spawnTracer` 在生产代码里只剩一个调用点）。

## 7. 本次改动的测试与门禁

| 项目 | 结果 |
|---|---|
| `pnpm typecheck` | 3 包全绿 |
| `pnpm lint`（eslint + prettier --check） | 全绿 |
| `pnpm test` | **77 文件 507 用例**全绿（新增 `localWeapon` 3、`sheepHit` 5、`ai` 1、`resolve` 5、`prediction` 2、`hud` 1、`match-flow` 1） |
| `pnpm check:docs` / `check:assets` | 通过 |
| `pnpm check:count` | 通过（490 ≥ 300） |
| `pnpm check:coverage` | 通过（阈值全达标） |
| `pnpm check:build`（`pnpm build:client`） | 通过（`packages/client/dist` 已重建 = 真人实际在玩的那份产物） |
| `pnpm check:alloc` | 通过（稳态每 tick 新增堆 ≈ 0、保留增长 ≈ 0） |
| `node tools/e2e-cdp.mjs --game-port 8799`（Edge + 生产产物） | **18/18 断言通过**（报告：`playtest-cdp-round2.md`） |

复验命令：`pnpm build:client && node tools/e2e-cdp.mjs --game-port 8799 --cdp-port 9444 --out <路径>`
（本机 8787 被真人试玩占用，故换端口；`--headful` 可复验指针锁定相关的鼠标断言）。

## 8. 未验证与遗留

- **第 4 条只修了确凿的根因**：本沙箱里 60fps / 帧间隔 p95 16.8ms / 每帧工作 ≤ 1.2ms 都达标，
  但"真人体感卡顿"还可能来自显示器缩放 > 100% + 大窗口（渲染目标随 DPR² 放大）、
  浏览器硬件加速被关/软件渲染、省电模式降频等环境因素（与一轮结论相同）。
  真机自查：进游戏按 **F3** 看 `fps … p95 …/20ms work …ms` 与 `stages input … draw …` 两行。
- **命中体是胶囊近似**：`SHEEP_HIT` 按渲染盒的外接尺寸向上取整，是"宁可打到、不可打空"的取向，
  因此羊的耳/角等突出部位的边缘会有几厘米的"判定比画面略宽"。真要逐面精确判定需要网格射线求交，
  代价与收益不匹配（且会让"描头打不中"重新出现）。
- **羊追击只由确定性单测保证"靠近"**：e2e 只断言"9/9 只羊在移动"，
  没有断言"一定朝玩家靠近"——追击/冲撞/游走会随状态与距离切换，浏览器里做这条断言会变成随机失败。
- **没有做"按命中距离裁短本地曳光"**：`playerHit` 事件里不带对应命令 seq，霰弹一发 8 颗弹丸仍会画出 8 条，
  还要在 FX 池里维护"最新一条曳光属于哪发"的状态；按"一次开火一条曳光"收敛更简单，也与弹道视觉意图一致。
- **门禁里的端口抖动（环境，非代码）**：本机临时端口段是 1024–15000，其中落在 Node `fetch`（undici）
  "bad port" 黑名单里的端口（6000、6665–6669、10080 等）会被直接拒绝；在受限沙箱里跑整套 `pnpm check` 时
  `listen(0)` 偶尔会抽到这类端口，`packages/server/src/http.test.ts` 便报 `fetch failed: bad port`。
  解除沙箱限制后连跑 3 次 `vitest run` 都是 **77 文件 507 用例全绿**，故未改动该测试。
- 未改动：`@ac/shared` 的坐标/yaw 约定、移动与 AI 的平衡数值（`SHEEP.radiusM/heightM` 等）、
  网络协议与快照格式、竞技场与美术资产、玩家命中判定（`partForHeight` 逐位不变）。
