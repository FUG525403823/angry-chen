# 跨语言对拍向量（fixture）

> 冻结契约：S07 §5（schema / 比较规则 / 再生成纪律）与 [ADR-010](../../00-共识/ADR/ADR-010-跨语言确定性与对拍.md) §6。
> 真值来源：冻结的 v1 实现 `D:\projects\tmp\angry-chen-bak`——**全程只读**，任何补丁与导出只作用于可写派生副本。

## 1. 清单（本批 4 份，S07 §5.3 的前 4 行）

| fixture | 场景 | tick | 实体 | 关键覆盖 | 字节 | SHA256 |
|---|---|---|---|---|---|---|
| `still-60t.json` | 静止 | 60 | 4 | 缺命令分支（前 30 tick 传 0 条命令）、位姿不动 | 54474 | `f8ae76e4e216b06c741c31eb06cdda7e561ccd372c5c4cdcdbd0576b422df3bd` |
| `straight-line-240t.json` | 直线移动 | 240 | 4 | 步行 0.225 m/tick 与冲刺 0.315 m/tick、方向反转、积分精度 | 326155 | `b8d0da2dc708a9d129835c5ae9149631d49fc66a8f55d13fdeea14c8a77148d2` |
| `barn-collision-400t.json` | 碰撞（谷仓） | 400 | 4 | 谷仓推离到 z=±4.4 与速度清零（x=±1.5 两名玩家） | 545965 | `6f67c0e3ec241d32131d474784e9458051af5addf11e97082b595c79d23f4cfc` |
| `fence-bounds-400t.json` | 边界（栅栏） | 400 | 4 | `limit = 40 - 0.25 - 0.4 = 39.35` 的 +z / -x 两侧夹取 | 519906 | `366d5e6d56db9149cb093b3af314c5b649d148f4443fb2777dbebb91e5afa82f` |

总体积 **1 446 500 B**，门限 2 097 152 B（§5.5 / ADR-010 §8）——见 §6 的体积门冲突。

命令流（每份都是 4 名玩家、同一套命令）：

- `still-60t`：tick 1–30 传 **0 条命令**（走 v1 `applyCommandToState(state, undefined)` 的速度归零分支）；tick 31–60 传 4 条全零命令。
- `straight-line-240t`：tick 1–120 `moveX=1, yaw=+PI/2, buttons=0`（+x 步行 4.5 m/s）；tick 121–240 `moveX=1, yaw=-PI/2, buttons=2`（-x 冲刺 6.3 m/s）。
- `barn-collision-400t`：全程 `moveX=1, yaw=PI`（-z 步行）。x=±1.5 的两名玩家被谷仓挡住并清零 z 速度；x=±4.5 的两名玩家从谷仓外侧走过、最终被远端栅栏夹到 -39.35。
- `fence-bounds-400t`：玩家 1/2 `yaw=0`（+z）被夹到 +39.35；玩家 3/4 `yaw=-PI/2`（-x）被夹到 -39.35。

## 2. 世界初态（schema 之外的隐含约定）

`seed` 交给 `createWorld(seed)`，而 v1 的 `createWorld` 会**立刻按 `arena.playerSpawnPoints` 生成 4 名玩家**（id 1..4 升序，hp/armor 取 `entity.baseStats.player` = 100/50，team 0，y=0）。C++ 侧必须按同一语义构造（`server/tests/fixture_test.cpp` 的 `createFixtureWorld`）；`commands[]` 的**槽位**语义与 v1 `applyCommands` 一致：第 k 条命令给升序第 k 名玩家，缺命令的槽位速度归零。

## 3. 再生成（工作目录 = 仓库根）

```text
node tools/export-fixtures.mjs                 # 写盘（缺派生副本时自动从只读源复制，排除 node_modules/.git）
node tools/export-fixtures.mjs --check         # 幂等校验：逐字节比较，不写盘
node tools/export-fixtures.mjs --list          # 清单：name / 字节数 / SHA256
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

## 5. 尚未交付的场景与所有者（S07 §5.3 的其余 10 行）

| fixture（§5.3） | 依赖的 C++ 能力 | 所有者 |
|---|---|---|
| `rifle-burst-hit-120t` | 武器状态、射线、伤害（S08） | S09（原 S08：需要会移动的羊，见 server/README §9.1-2） |
| `shotgun-spread-60t` | 8 弹丸与抖动派生（S08） | S09（原 S08：需要会移动的羊，见 server/README §9.1-2） |
| `downed-revive-140t` | 倒地/救援（S08） | S09（原 S08：需要会移动的羊，见 server/README §9.1-2） |
| `sheep-grunt-ai-600t` | 羊形 AI 与聚集（S09） | S09 |
| `sheep-ram-charge-300t` | 冲锋/硬直（S09） | S09 |
| `sheep-elite-bolt-300t` | 距离保持与投射物生命周期（S09） | S09 |
| `sheep-king-phases-900t` | 阶段阈值与召唤（S09） | S09 |
| `wave-director-1to5-1200t` | 波次预算与出生点选择（S09） | S09 |
| `snapshot-roundtrip-240t` | 量化快照 round-trip（S12） | S12 |
| `rng-streams-600t` | 三流归属（依赖上面全部消费方） | S12 |

导出时刻就有消费者，才能验证"这份向量到底在测什么"——所以它们随各自的计划一起入库，而不是现在冻一批没人验证过的语义猜测。同一原因：§5.6 的 `configHash` 覆盖「武器表与散布常量、战斗常数、羊形参数/AI 参数/状态转移表/命中盒、波次规则」，这些组在 C++ 侧落地前，任何 14 场景的 hash 都不可能通过。

**S09 落地后的现状（2026-09-24）**：S09 交付的是 `configHash` 的七组新覆盖（`sheep` / `sheep.ai` / `sheep.states` / `sheep.local` / `sheep.attack` / `waves` / `waves.scaling`，`configHash = 19a978ea`，见 `server/README.md` §10）——只把本文件 §1 的 4 份向量按新哈希重新导出了一遍（字节数不变、SHA256 全部换新）。上表挂在 S09 名下的 5 份**场景向量仍未导出**：导出器的场景集目前只有「4 名玩家的移动/碰撞」，跑羊群要先给它加「生成羊 + 空命令」的驱动；`wave-director-1to5-1200t` 另外依赖把导演接进 tick 循环（S09 按计划把 `DirectorState` 留给 S10 的比赛控制器，见 `server/README.md` §10.1-3/§10.1-4）。

## 6. 交给后续计划的已知风险

1. **`Math.round`**：v1/JS 的语义是 `floor(x + 0.5)`（`-1.5 → -1`），C 的 `std::round`/`llround` 是「远离 0」（`-1.5 → -2`）。跨语言量化必须用 `floor(x + 0.5)`（S02 `quantizeAngle` 已经是这个口径）。
2. **`Math.hypot`**：v1 `combat/resolve.ts:176` 用 `Math.hypot(halfWidthM, halfDepthM)` 算羊形命中盒对角线；它**不等于** `sqrt(a*a + b*b)`（逐位）。S08 落地前必须先冻结这条口径。
3. **体积门**：§5.5 的「14 份 < 2 MB」与 §5.1 的「每 tick 全量投影 + `%.17g`」在 1200 tick 场景下不相容（4 份移动向量已占 1 446 500 B = 69%）→ 需要裁决：放宽为最短往返表示，或按里程碑分档抬高门限 / 把口径改回"对拍向量文件"。注意 §6 的取证命令是**目录口径**（`Get-ChildItem -Recurse -File | Measure-Object Length -Sum`），实测约 **2.16 MB**（含 S02 的 `trig-table.json` 709 640 B 与本 README，精确值每次由脚本打印；它会随本目录里文件的大小微动）——今天就已超门限约 65 KB；`export-fixtures.mjs` 每次都会把这行数字打印出来。
