# 跨语言对拍向量（fixture）

> 冻结契约：S07 §5（schema / 比较规则 / 再生成纪律）与 [ADR-010](../../00-共识/ADR/ADR-010-跨语言确定性与对拍.md) §6。
> 真值来源：冻结的 v1 实现 `D:\projects\tmp\angry-chen-bak`——**全程只读**，任何补丁与导出只作用于可写派生副本。

## 1. 清单（本批 7 份：S07 §5.3 的前 4 行 + 3 份战斗类）

| fixture | 场景 | tick | 实体 | 关键覆盖 | 字节 | SHA256 |
|---|---|---|---|---|---|---|
| `still-60t.json` | 静止 | 60 | 4 | 缺命令分支（前 30 tick 传 0 条命令）、位姿不动 | 54474 | `f8ae76e4e216b06c741c31eb06cdda7e561ccd372c5c4cdcdbd0576b422df3bd` |
| `straight-line-240t.json` | 直线移动 | 240 | 4 | 步行 0.225 m/tick 与冲刺 0.315 m/tick、方向反转、积分精度 | 326155 | `b8d0da2dc708a9d129835c5ae9149631d49fc66a8f55d13fdeea14c8a77148d2` |
| `barn-collision-400t.json` | 碰撞（谷仓） | 400 | 4 | 谷仓推离到 z=±4.4 与速度清零（x=±1.5 两名玩家） | 545965 | `6f67c0e3ec241d32131d474784e9458051af5addf11e97082b595c79d23f4cfc` |
| `fence-bounds-400t.json` | 边界（栅栏） | 400 | 4 | `limit = 40 - 0.25 - 0.4 = 39.35` 的 +z / -x 两侧夹取 | 519906 | `366d5e6d56db9149cb093b3af314c5b649d148f4443fb2777dbebb91e5afa82f` |
| `rifle-burst-hit-120t.json` | 连射命中 | 120 | 4 玩家 + 问界羊 + 2 咩咩兵（+问号弹） | 切枪到步枪、射速节流（rpm 600 → 每 2 tick 一发）、散布累积与爆头×2、换弹 2000ms、移动目标命中与投射物；远处吃草的羊抽 6 次 `ai` 流 | 206114 | `e8c369655072fe7e3b0bea4cc884a494a94e3e81e8d6610815559ffe8005745a` |
| `shotgun-spread-60t.json` | 霰弹散布 | 60 | 4 玩家 + 2 咩咩兵 | 切枪到 3 号槽、8 弹丸与 `seq` 派生抖动（±4°）、rpm 70 的节流（60 tick 仅 3 次击发） | 85188 | `2c05a23deeda14fe3c5b24bb1cfd3d6375ad0b2975c7bc29c9fe6fe8ec35db82` |
| `downed-revive-140t.json` | 倒地救援 | 140 | 4 玩家 + 咩咩兵 | 倒地位 bit0、救援进度事件（每 5%）、松手中断（`reviverId` 归零）、3s 完成后 hp = 50% 上限 | 212927 | `199b8c23cf11b6374a07cc2ac6e4739eb8e04c002b6d64b403bd819e38aebe79` |

总体积 **1 950 729 B**，门限 2 097 152 B（§5.5 / ADR-010 §8）→ **余量仅 146 423 B**，见 §5/§6 的体积门冲突：其后的 7 份场景向量在本门限下已放不进来。

命令流（每份都是 4 名玩家、同一套命令槽位语义）：

- `still-60t`：tick 1–30 传 **0 条命令**（走 v1 `applyCommandToState(state, undefined)` 的速度归零分支）；tick 31–60 传 4 条全零命令。
- `straight-line-240t`：tick 1–120 `moveX=1, yaw=+PI/2, buttons=0`（+x 步行 4.5 m/s）；tick 121–240 `moveX=1, yaw=-PI/2, buttons=2`（-x 冲刺 6.3 m/s）。
- `barn-collision-400t`：全程 `moveX=1, yaw=PI`（-z 步行）。x=±1.5 的两名玩家被谷仓挡住并清零 z 速度；x=±4.5 的两名玩家从谷仓外侧走过、最终被远端栅栏夹到 -39.35。
- `fence-bounds-400t`：玩家 1/2 `yaw=0`（+z）被夹到 +39.35；玩家 3/4 `yaw=-PI/2`（-x）被夹到 -39.35。
- `rifle-burst-hit-120t`：1 号玩家 tick 1 `buttons=64 (switchWeapon), switchTo=1`，之后全程 `yaw=PI`（朝向 -z）、`pitch=-0.03`（**必须俯射**：射击起点是眼高 1.6m，羊的命中盒顶只有 1.15–1.29m）；tick 2–60 按住开火（30 发打空弹匣）、tick 61–100 按住换弹、tick 101–120 再开火。2 号玩家向 +z 步行、4 号玩家向 -x 步行、3 号玩家原地不动。
- `shotgun-spread-60t`：1 号玩家 tick 1 `buttons=64, switchTo=2`，tick 2 起按住开火（rpm 70 → `t=100/957/1814ms` 三次击发）；2 号玩家向 -x 冲刺。
- `downed-revive-140t`：1 号玩家初态 hp=8 / armor=0（§2 的初态约定）后全程零命令；2 号玩家 tick 1–8 向 -x 走（`moveX=1, yaw=-PI/2`）靠近 1 号玩家，tick 9–28 按住 `interact` 推进救援、tick 29–33 松手（中断）、tick 34–140 继续按住直到完成。

## 2. 世界初态（schema 之外的隐含约定）

`seed` 交给 `createWorld(seed)`，而 v1 的 `createWorld` 会**立刻按 `arena.playerSpawnPoints` 生成 4 名玩家**（id 1..4 升序，hp/armor 取 `entity.baseStats.player` = 100/50，team 0，y=0）。C++ 侧必须按同一语义构造（`server/tests/fixture_test.cpp` 的 `createFixtureWorld`）；`commands[]` 的**槽位**语义与 v1 `applyCommands` 一致：第 k 条命令给升序第 k 名玩家，缺命令的槽位速度归零。

羊群/战斗类场景还需要第二层初态（生成羊、覆盖玩家初态），它同样**不在 schema 里**：导出侧是 `tools/export-fixtures.mjs` 每个场景的 `setup(world)`，C++ 侧是 `fixture_test.cpp::applyScenarioSetup(name, world)`，两侧逐字同表。生成原语与 v1 `ai/director.ts` 的生成路径同形（`spawnEntity('sheep')` → `applySheepKind` → `state = graze`；C++ 复用 `waves::spawnSheepAt`）：

| fixture | 初态（`createWorld` 之后、第一个 tick 之前） |
|---|---|
| `rifle-burst-hit-120t` | `elite @ (-4.5, -15)`、`grunt @ (-9, -2)`、`grunt @ (35, -35)`（全程在 35m 视野外的吃草羊，用来抽 `ai` 流） |
| `shotgun-spread-60t` | `grunt @ (-4.5, -2)`、`grunt @ (-7, -3)` |
| `downed-revive-140t` | 1 号玩家 `hp = 8`、`armor = 0`；`grunt @ (-4.5, 5.8)` |

新增场景时**必须两侧同时改这张表**，否则第一个 tick 就会以 `entities[i].*` 失败（这层约定与 4 名玩家出生点一样，是"两侧同表"的隐含初态，不是可以从文件里读出来的东西）。

## 3. 再生成（工作目录 = 仓库根）

```text
node tools/export-fixtures.mjs                 # 写盘（缺派生副本时自动从只读源复制，排除 node_modules/.git）
node tools/export-fixtures.mjs --check         # 幂等校验：逐字节比较，不写盘
node tools/export-fixtures.mjs --list          # 清单：name / 字节数 / SHA256
node tools/export-fixtures.mjs --only <name[,name]>   # 只渲染选中的子集（体积门按本次选中的批次判定）
node tools/export-fixtures.mjs --root <副本> --out <目录>
```

导出侧对 v1 做了两件事（都只作用于进程内 / 可写副本，源仓库不变）：

1. **共享整数表替换**：把 v1 的 `Math.sin/cos/atan2/asin` 换成 `docs/evidence/fixtures/trig-table.json` 上的 `sinUnits/cosUnits/angleUnitsFromVector/angleUnitsFromRatio`（与 `server/src/core/trig_table.hpp` 1:1 同口径）。
2. **RNG 状态口径**（§5.2）：用计数包装统计每流抽取次数，再用同一推导 `derived = (imul(seed>>>0, 2654435761) + streamId) >>> 0`（ai 1 / spawn 2 / fx 3）重放同样次数得到 `mulberry32` 的内部状态 `a`。C++ 侧直接读 `World::rng.<stream>.a`（每 tick 比较三流状态）。
3. **基线纪律（可判定）**：默认拒绝 `--root` 指向只读源；启动时逐文件比对派生副本与只读源（`packages/shared/src`，不一致直接失败，除非显式 `--allow-patched-copy`）；结束时比对只读源的 (文件数, 总字节, 最新 mtime) 指纹，被写入即报错；体积门在写盘**之前**判定（门失败时不留下超限生成物）。

## 4. 判读不通过

比较器只打印**第一处**差异，格式冻结：`DIFF <fixture> tick=<n> field=<path> expected=<hex> actual=<hex>`（double 用 `bit_cast<uint64_t>` 的 16 位十六进制）。处理顺序：

1. `field=configHash` 且 `comparedTicks=0` → 常量表两侧已经漂移（先查 `server/src/config/**`、`server/src/sim/arena.hpp`）。
2. 某个 tick 的 `entities[i].pos/yaw/...` → 先确认 v1 侧没被改动（`git -C D:\projects\tmp\angry-chen-bak status` 必须干净、且该仓库只读），再查 C++ 侧的运算顺序（ADR-010 §2：`pos += vel * dt` 的求值顺序不得"优化"）。
3. fixture 是**生成物**：禁止手工编辑。数值规则变更必须先改 v1 或显式重建向量，再两侧同时改（ADR-010 §7）。
4. `entities[].hp` 是 **double**，不是整数：v1 的护甲吸收会把玩家 hp 打成小数（例：步枪命中后 `96.8`）→ 读取器按 double 解析、比较仍走 `bit_cast` 逐位（`fixture_io.hpp` 的 `FixtureEntity::hp`）。同一 tick 的 `flags` 位表两侧同源：bit0 `downed` / bit1 `rageMode` / bit2 `reloading` / bit5 `idle` 有来源（`reloading` 用该 tick 结束后的 `world.timeMs` 判 `isRageActive`/`isReloading`），bit3/bit4 两侧都不产出（恒 0）。

## 5. 尚未交付的场景与所有者（S07 §5.3 的其余 7 行）

**卡点先说清楚：不是能力缺口，是体积门。** 冻结的 §5.5 体积门是「14 份 < 2 MB」，而 §5.1 又冻结了「每 tick 全量投影 + `%.17g`」：已交付 7 份占 **1 950 729 B**，只剩 **146 423 B**；而每 tick 一条实体投影的实测代价是 **950.2 B/tick**（用 `downed-revive-140t` 反推：212 927 B 减去 560 条命令行 79 903 B 后 / 140 tick，5 实体 + 空命令的口径），所以**最小的**剩余场景（`sheep-ram-charge-300t`：300 tick ×（4 玩家 + 1 冲撞羊）+ 空命令）也要 ≈ **285 051 B > 146 423 B**；连"一只羊都不生成"的下界（300 tick × 4 名常驻玩家 ≈ 240 KB，`fence-bounds-400t` 反推 ≈800 B/tick）也已经超了。C++ 侧的羊形 AI/冲锋/问号弹/羊王/波次导演/救援都已就位（`--filter=ai` 36/36、`--filter=waves` 16/16），这 7 份现在缺的是"放得下的向量格式或分档门限"，见 §6 第 3 条（**需裁决**）。

| fixture（§5.3） | 依赖的 C++ 能力 | 现状与卡点 |
|---|---|---|
| `sheep-grunt-ai-600t` | 羊形 AI 与聚集（S09） | 能力就位（本批已用咩咩兵/问界羊跑通同一条 AI 路径）；卡 §5.5 体积门（600 tick × 5 实体 + 空命令 ≈ 570 KB） |
| `sheep-ram-charge-300t` | 冲锋/硬直（S09） | 同上（≈285 KB，已超余量） |
| `sheep-elite-bolt-300t` | 距离保持与投射物生命周期（S09） | 能力就位（`rifle-burst-hit-120t` 已含问界羊的保距与问号弹投影）；卡体积门 |
| `sheep-king-phases-900t` | 阶段阈值与召唤（S09） | 能力就位；卡体积门（900 tick ⇒ ≥ 855 KB），且要跨阶段阈值必须先造伤害（同一份向量同时要覆盖羊王 + 玩家射击）；召唤抖动是 `spawn` 流的消费方之一 |
| `wave-director-1to5-1200t` | 波次预算与出生点选择（S09） | **两处卡点**：① 体积门（1200 tick ⇒ ≥ 1.1 MB，1–5 波还要几十只羊）；② 导演仍未接进 tick 循环（OQ-11：`DirectorState` 归房间/对局流程，两侧的 `stepWorld` 都不调用 `updateDirector`，见 `server/README.md` §10.1-3/§10.1-4）——要入库得先冻结"外部每 tick 驱动导演"的约定 |
| `snapshot-roundtrip-240t` | 量化快照 round-trip（S12） | **格式卡点**：冻结的 schema（§5.3）只有 `expected.{entities,events,rngState}`，没有承载"编码→解码后位型等价"的字段；要表达它就得往 schema 里加字段组（本批不允许发明新格式），或按 S12 的自有向量类型另立一类 |
| `rng-streams-600t` | 三流归属（依赖上面全部消费方） | `spawn` 流的两处消费方是波次导演（出生点选择/抖动）与羊王召唤抖动，`ai` 流是吃草重选（本批已由 `rifle-burst-hit-120t` 抽到 6 次），`fx` 流按 ADR-010 不得参与模拟 → 要覆盖"三流归属"仍需导演/羊王进场；另加体积门（600 tick） |

导出时刻就有消费者，才能验证"这份向量到底在测什么"——所以它们随各自的计划一起入库，而不是现在冻一批没人验证过的语义猜测。同一原因：§5.6 的 `configHash` 覆盖「武器表与散布常量、战斗常数、羊形参数/AI 参数/状态转移表/命中盒、波次规则」，这些组在 C++ 侧落地前，任何 14 场景的 hash 都不可能通过（本批已全部落地，`configHash = 19a978ea` 两侧一致）。

**本次交付后的现状**：导出器已补上「生成羊群 + 空命令」的驱动（`scenario.setup` + `--only`，见 §2/§3），羊群 AI、冲锋/撕咬、问号弹、倒地救援、护甲吸收小数 hp、`reloading`/`downed` 位都已进入逐位对拍（`--filter=fixture` 9/9、全量 `TESTS 471/471`）；§5.3 的 14 行里 **7 行已交付、7 行待裁决**，卡点如上表（全部是体积门，另有两处格式/链路卡点）。

## 6. 交给后续计划的已知风险

1. **`Math.round`**：v1/JS 的语义是 `floor(x + 0.5)`（`-1.5 → -1`），C 的 `std::round`/`llround` 是「远离 0」（`-1.5 → -2`）。跨语言量化必须用 `floor(x + 0.5)`（S02 `quantizeAngle` 已经是这个口径）。
2. **`Math.hypot`**：v1 `combat/resolve.ts:176` 用 `Math.hypot(halfWidthM, halfDepthM)` 算羊形命中盒对角线；它**不等于** `sqrt(a*a + b*b)`（逐位）。S08 落地前必须先冻结这条口径。
3. **体积门**：§5.5 的「14 份 < 2 MB」与 §5.1 的「每 tick 全量投影 + `%.17g`」不相容——**本批已经撞到墙上**：7 份 = 1 950 729 B（门限 2 097 152 B，余 146 423 B），而剩余 7 份里最小的一份也要 ≈285 KB（§5 的实测口径）。需要裁决：放宽为最短往返表示，或按里程碑分档抬高门限 / 把口径改回"对拍向量文件"。注意 §6 的取证命令是**目录口径**（`Get-ChildItem -Recurse -File | Measure-Object Length -Sum`），当前实测 ≈**2.674 MB / 9 个文件**（含 S02 的 `trig-table.json` 709 640 B 与本 README；精确字节每次由脚本打印，且会随本文件自身的大小微动）——已超门限约 576 KB；`export-fixtures.mjs` 每次都会把这行数字打印出来。
