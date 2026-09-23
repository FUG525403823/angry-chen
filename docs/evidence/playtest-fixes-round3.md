# 三轮真人试玩四缺陷：根因、修复与浏览器内复验（2026-09-24）

真人试玩反馈原文（逐条照抄）：

1. 第一个问题，羊受击体积太小了，我打头还是达不到
2. 枪的有效射程太近了，远处根本打不到
3. 问界标志没了
4. 弹药显示有问题，跟实际弹药不符

追问澄清：第 3 条是误报（原话「原来是精英，我搞错了」，指的是问界羊本身）；第 4 条补充为
「开枪的时候子弹数跳动」。

结论：第 1、2、4 条是确凿缺陷，已修复；第 3 条经用户澄清为非缺陷（额头问界徽标一直在渲染，
浏览器内实测实例 9 个、贴图 128px、材质 opacity 1）。第 4 条查出**两个根因**：一是客户端
「按键 → 命令」的 `switchTo` 被硬编码为 0（按 Q 永远切回手枪，HUD 弹匣数从 30 跳到 12，
这就是"跟实际弹药不符"）；二是弹药账本把 "ack 水位越过" 直接当成 "服务器已消耗"，而 ack 与
权威弹匣分属相邻两个包，于是每一发都先记一次"被拒"、显示值随即弹回权威值（数字上下跳）。

复验：`pnpm check` 全绿（77 文件 514 用例）；`node tools/e2e-cdp.mjs --game-port 8799`
**23 条断言全部通过**（报告：`docs/evidence/playtest-cdp.md`）。

## 1. 羊受击体积太小、打头打不到

### 1.1 根因

羊的命中体是"竖胶囊 + 按高度比例算半径"（`SHEEP_HIT.radiusM` / `heightM` 加一组比例阈值）。
拿它和渲染出来的羊比：羊毛实体盒半宽 0.86 m、头部盒前脸在 0.91 m 处，而胶囊在头部高度
（0.68–1.12 m）的**有效半径只有 0.18–0.46 m**，越往头顶越细。于是"看着明明描到头、子弹却从旁边飞过"，
距离一远（23 m 外配合 0.8° 散布 ≈ 0.32 m 抖动）几乎整轮打空。

### 1.2 修复

- `packages/shared/src/config/sheep.ts`：`SheepHitProfile` 改为**盒体几何**（`halfWidthM`/`halfDepthM`/`topM`
  身体盒；`headHalfWidthM`/`headMinYM`/`headMaxYM`/`headMinZM`/`headMaxZM` 头部盒；`headMinM`/`torsoMinM` 部位阈值），
  四种羊形按 `SHEEP_FORM_SCALE` 缩放。取整规则：**最小值向下取整到 cm、最大值向上取整**（宁可打到、不可打空）。
- `packages/shared/src/combat/resolve.ts`：羊分支先做"世界 → 羊局部"旋转（`sinYaw/cosYaw`），用 `reach` 提前剔除，
  再依次求交身体盒与头部盒，取更近者作为 `part`（头盒更近 ⇒ head），命中点由 `origin + dir * t` 重算。
  玩家分支（竖胶囊 + `partForHeight`）保持逐位不变。
- 渲染侧尺寸统一从 `packages/client/src/render/sheepModel.ts` 导出（`SHEEP_BODY_BOX`/`SHEEP_HEAD_BOX`/
  `SHEEP_CROWN_BOX`/羊毛 `WOOL_SPREAD_*`），命中盒与渲染盒共用同一组常量。

### 1.3 复验

- `packages/shared/src/combat/resolve.test.ts`：羊的判据整段按"渲染盒体"重写 —— 4 种羊形 × 4 个距离
  （4/10/20/30 m）逐点断言正面盒体边缘命中、盒外 5 cm 打空、头/躯干高度阈值、头盒比躯干盒更近时判 head、
  以及头盒凸出躯干的那一段单独可命中；`referenceTrace` 保留无提前剔除版本作为对拍参考。
- `packages/client/src/test/sheepHit.test.ts`：断言命中盒**覆盖**渲染盒（身体、羊毛轮廓 = 散布 + 团簇半径 × 最大缩放、
  头部盒、羊王冠盒），以及头盒确实凸出躯干盒（`headMaxZM > halfDepthM`）。
- 浏览器内：`瞄头部高度（+1.0m）连射仍能命中（旧胶囊该高度有效半径仅 0.18–0.46m）` ——
  23 m 外一轮连射把目标 hpRatio 从 1.000 打到 0.169；`开火命中最近的羊` —— 连续命中致死。

## 2. 有效射程太近

### 2.1 根因

距离衰减斜率按"30/40/12 m 之后每米 -8%/-6%/-15%"、下限只有基础伤害的 **20%**：45 m 外步枪命中
只剩 20% 伤害、霰弹枪更早触底。竞技场 80 m × 80 m、敌人沿围栏内侧（r≈38 m）生成，常见交战距离
20–45 m，于是玩家感受就是"远处根本打不到（打了也不掉血）"。

### 2.2 修复

- `packages/shared/src/config/weapons.ts`：`falloffPerM` 手枪 0.08 → **0.04**、步枪 0.06 → **0.03**、
  霰弹枪 0.15 → **0.075**（起点不变：30/40/12 m）。
- `packages/shared/src/config/combat.ts`：`FALLOFF_MIN_MULTIPLIER` 0.2 → **0.5**。
- 文档同步：`docs/02-需求分析.md` §7.2 表格、`docs/plans/P06-战斗系统与武器.md` §5.3 数值表与 §5.4 公式。

### 2.3 复验

`packages/shared/src/combat/combat.test.ts` 的参考实现改为引用 `FALLOFF_MIN_MULTIPLIER`，手算样例重新冻结
（手枪 40 m 躯干 15 / 40 m 头 30 / 60 m 躯干 12.5，步枪 45 m 躯干 17 / 100 m 头 20，霰弹枪 100 m 6），
并新增「远距离命中仍有有效伤害」用例。

## 3. 问界标志：非缺陷（用户澄清）

用户澄清"原来是精英，我搞错了"。浏览器内探针一直是绿的：
`羊额头「问界」徽标已上传且可见（实例 > 0、贴图 128px、材质可见）` —— 实例 9，贴图 128 px，
opacity 1，首实例 [-39.25, 0.90, 3.76]。未改动任何相关代码。

## 4. 开枪时子弹数跳动（含"按 Q 切枪总变成手枪"）

### 4.1 根因 A：切枪目标槽位被写死

`packages/client/src/input/sampler.ts` 的 `emit()` 里是 `command.switchTo = 0`，`KeyQ` 只置了
`BUTTON.switchWeapon` 位。而服务端 `packages/shared/src/combat/resolve.ts` 用的是**绝对目标**：
`if ((buttons & switchWeapon) !== 0) switchSlot(weapon, switchTo, now)` —— 也就是每次按 Q 都收到
"切到 0 号 = 手枪"。玩家在局内切枪永远回手枪，HUD 弹匣数从 30 跳到 12（"跟实际弹药不符"）。

### 4.2 根因 B：账本把 ack 水位当成"已消耗"

`packages/client/src/prediction/ammoLedger.ts` 旧实现：`noteServerAck(seq)` 把被 ack 越过的本地开火
直接结算成"服务器应该扣了这么多弹"，`reconcile` 再拿它和权威弹匣的实际下降比对。但 ack 随**快照包**到达、
权威弹匣随**比赛状态包**到达，两者分属相邻两个包 ⇒ 几乎每一发都会先判一次"服务器没收下这发"：
`rejected + 1`、`pending` 被清空、显示值弹回权威值。浏览器内实测：一轮 2 s 连射 rejected +4 ~ +8、
显示值多次回升（`4→5`）。

### 4.3 修复

- `sampler.ts`：新增 `getActiveSlot` 依赖；**按下 Q 的那一刻**按当前槽位锁定绝对目标槽位
  （`sanitizeSlot(sanitizeSlot(current) + 1)`）。按住不再连跳（浏览器按键自动重复也不会再切一把），松开清零。
  `main.ts` 注入 `getActiveSlot: () => weaponSlot`。
- `ammoLedger.ts`：
  - ack 水位只用于**过期判定**：越过 `AMMO_ACK_GRACE_SEQ = 20`（20 Hz 快照 + 30 Hz 采样下约 0.7 s）
    仍没有对应权威下降的本地开火，才计 `rejected`（= 本地幽灵开火）。
  - 消耗只按权威弹匣的**实际下降**逐发消账（按记账顺序消最早的 N 发）。
  - **显示值恒等于权威弹匣**；未确认开火只收紧**开火闸门** `gateMag = 权威 − pending`，
    `localWeapon.reconcileAmmo` 用闸门值（宁可少打一发，也不产生幽灵弹道）。

### 4.4 复验

- `packages/client/src/input/sampler.test.ts`（三条在旧实现下全红）：按 Q 写入"下一把"的绝对槽位、
  按住不连跳、0→1→2→0。
- `packages/client/src/prediction/ammoLedger.test.ts`（①③④⑧ 为本次新增/改写）：
  「ack 先到、权威弹匣后到 ⇒ 不记被拒、显示不回跳」（旧实现下红）、「权威只降 1 发 ⇒ 只消 1 发」、
  「ack 远远越过且权威始终不降 ⇒ 过期计被拒」、「显示值恒等于权威弹匣，未确认开火只收紧闸门」。
- 浏览器内：`弹药显示与权威弹匣逐帧一致（HUD 数字 = 权威弹匣，不受未 ack 开火影响）`
  —— 显示 12→3 = 权威 12→3，HUD 不一致 0 次；`连射期间弹药显示不跳动`
  —— 手枪/步枪各 1.2 s 连射：**回升 0 次**、权威消耗 = 显示下降；
  `按 Q 切到下一把武器后武器名与弹匣口径同步` —— 切换后为步枪、权威 30。

## 5. 门禁

- `pnpm check`（typecheck / lint / test / check:docs / check:assets / check:count / check:coverage /
  check:build / check:alloc）：**77 文件 514 用例全绿**。
- `node tools/e2e-cdp.mjs --game-port 8799`（Edge + CDP，生产产物）：**23 条断言全部通过**；
  报告 `docs/evidence/playtest-cdp.md`（含截图路径与 fps/分阶段耗时）。

## 6. 遗留

- **射速闸门相位差**：客户端本地节流与服务端（按 50 ms tick 结算）会有最多一个 tick 的错位，
  1.2 s 连射实测"本地开火数 − 权威消耗数"在 ±2 发内。显示值已不再受影响（恒等权威），
  但要让曳光与服务端逐发对齐，需要把服务端 `nextFireAllowedAtMs` 下发给客户端（协议变更，未做）。
- **命中部位（head vs torso）的端到端断言**仍靠 pure-sim 单测（盒体边缘逐点断言）+ 跨层对齐测试；
  浏览器内只断言"瞄头部高度能命中/致死"，因为 0.8° 散布下单发命中部位不可稳定复现。
