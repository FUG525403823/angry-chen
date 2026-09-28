# 跨语言对拍向量（fixture）

> 冻结契约：S07 §5（口径 / 比较规则 / 再生成纪律）与 [ADR-010](../../00-共识/ADR/ADR-010-跨语言确定性与对拍.md) §6、§8。
> 真值来源：冻结的 v1 实现 `D:\projects\tmp\angry-chen-bak`——**全程只读**，任何补丁与导出只作用于可写派生副本。
> 本批口径变更：**存「怎么跑」而不是「跑出来的每一帧」**（种子 + 初态 + 命令脚本 + 每 tick 全量投影的哈希链 + 少量关键帧）。
> 两侧都**重算**同一份全量投影再比对，逐位强度不变；每 tick 落到盘上的是 8 字节链节点（16 位十六进制），不再是 ~950 B 的投影文本。

## 1. 清单（14 份，S07 §5.3 全部交付）

| 场景 | 文件名 | 字节 | ticks | 哈希链长度 | 用例名 | 状态 |
|---|---|---|---|---|---|---|
| 静止（缺命令分支） | `still-60t.json` | 5 171 | 60 | 60 | `fixture_still_60t` | 通过 |
| 直线移动（步行/冲刺/反向） | `straight-line-240t.json` | 10 673 | 240 | 240 | `fixture_line_move_240t` | 通过 |
| 谷仓碰撞 | `barn-collision-400t.json` | 14 012 | 400 | 400 | `fixture_barn_collision_400t` | 通过 |
| 栅栏边界 | `fence-bounds-400t.json` | 13 810 | 400 | 400 | `fixture_fence_bounds_400t` | 通过 |
| 步枪连射命中 | `rifle-burst-hit-120t.json` | 10 740 | 120 | 120 | `fixture_rifle_burst_hit_120t` | 通过 |
| 霰弹散布 | `shotgun-spread-60t.json` | 6 771 | 60 | 60 | `fixture_shotgun_spread_60t` | 通过 |
| 倒地救援 | `downed-revive-140t.json` | 10 263 | 140 | 140 | `fixture_downed_revive_140t` | 通过 |
| 咩咩兵追踪与撕咬 | `sheep-grunt-ai-600t.json` | 21 043 | 600 | 600 | `fixture_sheep_grunt_600t` | 通过 |
| 冲锋羊冲撞 | `sheep-ram-charge-300t.json` | 12 565 | 300 | 300 | `fixture_sheep_ram_charge_300t` | 通过 |
| 问界羊保距与问号弹 | `sheep-elite-bolt-300t.json` | 12 581 | 300 | 300 | `fixture_sheep_elite_bolt_300t` | 通过 |
| 羊王阶段与召唤 | `sheep-king-phases-900t.json` | 55 049 | 900 | 900 | `fixture_sheep_king_phases_900t` | 通过 |
| 波次导演 1→5 | `wave-director-1to5-1200t.json` | 56 505 | 1 200 | 1 200 | `fixture_wave_director_1to5_1200t` | 通过（§6 缺口 1） |
| 量化快照 round-trip | `snapshot-roundtrip-240t.json` | 13 161 | 240 | 240 | `fixture_snapshot_roundtrip_240t` | 通过 |
| RNG 三流归属 | `rng-streams-600t.json` | 29 947 | 600 | 600 | `fixture_stream_ownership_600t` | 通过 |

总体积 **272 291 B**；单份最大 **56 505 B**（`wave-director-1to5-1200t`）。门限（S07 §5.5 / ADR-010 §8）：**单份 ≤ 65 536 B、14 份合计 ≤ 524 288 B** → 分别余 8 031 B / 252 000 B。

旧门限「14 份 < 2 MB」**作废**，理由见 §5：旧口径要把每 tick 全量投影文本落盘（实测 ~950 B/tick），14 份必然 > 6 MB；新口径只落哈希链（~8 B/tick）与 3–5 个关键帧，14 份缩到 272 KB，而比较仍是逐 tick、逐字段、逐位。

`--filter=fixture` 共 20 个用例：上表 14 个 + `fixture_fnv_self_test`（FNV-1a 自检）+ 4 个"篡改必须被抓到"的负向用例（`fixture_tampered_projection_reports_diff` / `fixture_tampered_digest_tick_diff` / `fixture_tampered_script_tick_diff` / `fixture_tampered_hash_fails_before_ticks`）+ `fixture_event_kind_optional_key`（B2 收尾：可选键 `events[].kind` 的读法与投影追加，见 §7.2 收口）。

## 2. 向量口径（schema `version = 2`）

一份向量 = 跑这个场景所需的最小信息 + 校验用的压缩产物。键序冻结，读取器逐键校验（`server/tests/fixture_io.cpp`）：

```text
name, version, seed, dtMs, configHash, ticks,
setup { players[{id,hp,armor}], sheep[{kind,x,z}] },
director { startWave },
script [ {from,to,commands[{id,moveX,moveY,yaw,pitch,buttons,switchTo}]} ],   # 按 tick 的 RLE
keyframes [ {tick, entities[], events[], rngState} ],
snapshot [ {tick, records, encodeHash, decodeHash} ],
hashChain [ 每 tick 一个 16 位十六进制 ]
```

- **全量投影（每 tick 的字节口径）**：字段级文本，字段与顺序沿用 v1 冻结 schema（§5.1），double 一律 `%.17g`：
  `tick=` / `dtMs=` / 逐实体 `ent=id,kind,x,y,z,yaw,pitch,hp,flags` / 逐事件 `evt=tick,type,flags,subjectId,targetId,x,y,z,value[,kind]` / 尾行 `rng=ai,spawn,fx`（`,kind` 只对**带种类的模拟事件**追加：目前只有 `sheepKilled`，值 = 羊种类枚举，见 §7.2 收口）。实体顺序 = `activeIds` 顺序。
- **逐帧哈希链**：`h_i = fnv1a64(投影文本_i, h_{i-1})`，`h_0 = 0xcbf29ce484222325`，质数 `0x100000001b3`。两侧都按 `%.17g` 重算同一段文本再串链，链上只落 8 字节/tick。
- **关键帧**：少量 tick 的**全量投影**（实体 + 事件 + 三流 RNG 状态）。命中不一致时先比关键帧，能直接给出**字段名**（例：`entities[2].pos.x`、`snapshot.encodeHash`），比链哈希更好定位；链负责覆盖**每一个** tick。
- **快照组**（仅 `snapshot-roundtrip-240t`）：`encodeHash` = 对 v1 量化后的 15 字节实体记录块直接算 FNV（无报文头）；`decodeHash` = 对解码后的字段投影文本算 FNV。两侧分别走真实量化器/真实解码器。
- **`configHash`** = §5.6 常量表（武器与散布、战斗常数、羊形参数/AI 参数/状态转移表/命中盒、波次规则）的 8 位十六进制摘要，两侧必须一字不差（当前 `19a978ea`）。
- **强度说明**：旧口径把每帧投影**存**下来比，新口径把每帧投影**重算**出来比——比较的字节表示完全相同（同一份 `%.17g` 文本、同一 FNV），所以"逐位"强度没有下降；新口径另外把事件从"只比条数"升级为逐字段比较（x/y/z/value）。**唯一的信息损失**是非关键帧只留 8 字节摘要，字段级差异要靠 §6 的 `[ptext]` 出口再跑一次定位——这是本次体积门换来的取舍，已记在 ADR-010 §8。

## 3. 世界初态（`setup`，两侧同表）

`seed` 交给 `createWorld(seed)`，而 v1 的 `createWorld` 会**立刻按 `arena.playerSpawnPoints` 生成 4 名玩家**（id 1..4 升序，hp/armor 取 `entity.baseStats.player` = 100/50，team 0，y=0）。C++ 侧同语义（`fixture_test.cpp::createFixtureWorld`）。

第二层初态现在**在文件里**（`setup`），读取器逐字段校验，不再是"隐含约定"：

| 场景 | `setup.players` | `setup.sheep`（kind@(x,z)） | `director.startWave` |
|---|---|---|---|
| `still-60t` / `straight-line-240t` / `barn-collision-400t` / `fence-bounds-400t` | — | — | 0 |
| `rifle-burst-hit-120t` | — | `elite@(-4.5,-15)` `grunt@(-9,-2)` `grunt@(35,-35)` | 0 |
| `shotgun-spread-60t` | — | `grunt@(-4.5,-2)` `grunt@(-7,-3)` | 0 |
| `downed-revive-140t` | id1 `hp=8, armor=0` | `grunt@(-4.5,5.8)` | 0 |
| `sheep-grunt-ai-600t` | — | `grunt@(-4.5,-6)` `grunt@(-6.5,-5.5)` `grunt@(-2.5,-6.5)` `grunt@(35,-35)` | 0 |
| `sheep-ram-charge-300t` | — | `ram@(-4.5,-8)` `grunt@(35,-35)` | 0 |
| `sheep-elite-bolt-300t` | — | `elite@(-4.5,-9)` `grunt@(22,0)` | 0 |
| `sheep-king-phases-900t` | — | `king@(22,2)` `grunt@(35,-35)` | 0 |
| `rng-streams-600t` | — | `king@(22,2)` `grunt@(35,-35)` `grunt@(-35,35)` | 0 |
| `snapshot-roundtrip-240t` | — | `grunt@(-4.5,-6)` `grunt@(-7,-3)` | 0 |
| `wave-director-1to5-1200t` | — | — | 1 |

生成原语与 v1 `ai/director.ts` 的生成路径同形（`spawnEntity('sheep')` → `applySheepKind` → `state = graze`；C++ 复用 `waves::spawnSheepAt`）。`(35,-35)` / `(-35,35)` / `(22,0)` 这类远点羊用来保证 `ai`/`spawn` 流有确定次数的抽取（§6 缺口 3）。

**导演约定**：`DirectorState` 在 `stepWorld` **之外**，由外部每 tick 驱动（S09 §5.7 / OQ-11 已冻结）。两侧循环都是 `stepWorld(...)` → `planWave/updateDirector(...)`（首帧之前先 `planWave`），所以 `wave-director-1to5-1200t` 的 `spawn` 流消费与羊群生成能逐位对齐。

## 4. 命令脚本（`script`）

`script` 只记录**实际发出的玩家命令**，按 tick 的 RLE（`from`/`to` 闭区间）。槽位语义与 v1 `applyCommands` 一致：第 k 条命令给**升序第 k 名玩家**，缺命令的槽位速度归零（`commands: []` 即"全缺"分支）。

| 场景 | 命令模式 |
|---|---|
| `still-60t` | tick 1–30 传 0 条（速度归零分支）；31–60 传 4 条全零 |
| `straight-line-240t` | 1–120 `moveX=1, yaw=+PI/2`（+x 步行 4.5 m/s）；121–240 `moveX=1, yaw=-PI/2, buttons=2`（-x 冲刺 6.3 m/s） |
| `barn-collision-400t` | 全程 `moveX=1, yaw=PI`（-z 步行）：x=±1.5 被谷仓挡住并清零 z 速度，x=±4.5 从谷仓外侧走到远端栅栏被夹到 -39.35 |
| `fence-bounds-400t` | 玩家 1/2 `yaw=0`（+z）夹到 +39.35；玩家 3/4 `yaw=-PI/2`（-x）夹到 -39.35 |
| `rifle-burst-hit-120t` | 1 号 tick 1 `switchWeapon→1`，之后 `yaw=PI`、`pitch=-0.03`（**必须俯射**：射击起点眼高 1.6 m，羊命中盒顶 1.15–1.29 m）；2–60 开火（30 发打空）、61–100 换弹、101–120 再开火 |
| `shotgun-spread-60t` | 1 号 tick 1 `switchWeapon→2`，tick 2 起按住开火（rpm 70 → 60 tick 内 3 次击发）；2 号向 -x 冲刺 |
| `downed-revive-140t` | 1 号全程零命令；2 号 1–8 向 -x 走、9–28 按住 `interact`、29–33 松手（中断）、34–140 按住到完成 |
| `sheep-grunt-ai-600t` / `sheep-ram-charge-300t` / `sheep-elite-bolt-300t` | 全 300/600 tick 空命令：玩家不动，纯看羊形 AI 自己走（追踪、冲锋、保距+问号弹） |
| `sheep-king-phases-900t` | 78 段脉冲：每 24 tick 只发 1 tick 的四条开火命令（其余空），把 2400 hp 的羊王压到 66%/33% 两个阈值并触发召唤 |
| `wave-director-1to5-1200t` | 36 段：交替"按住开火 / 切枪 / 停火"，让 1–5 波按预算出生并被清掉 |
| `snapshot-roundtrip-240t` | 4 段（含 1 号 tick 1 切枪），第 1/60/120/180/240 tick 各取一次量化快照 |
| `rng-streams-600t` | 13 段开火/停火交替，既打羊王又让玩家被咬，覆盖 `ai`/`spawn` 两个流的多次抽取 |

## 5. 再生成与体积门（工作目录 = 仓库根）

```text
node tools/export-fixtures.mjs                 # 写盘（缺派生副本时自动从只读源复制，排除 node_modules/.git）
node tools/export-fixtures.mjs --check         # 幂等校验：逐字节比较，不写盘
node tools/export-fixtures.mjs --list          # 清单：name / 字节数 / SHA256
node tools/export-fixtures.mjs --only <name[,name]>   # 只渲染选中的子集（体积门按本次选中的批次判定）
node tools/export-fixtures.mjs --root <副本> --out <目录>
```

**体积门**：单份 ≤ 65 536 B、14 份合计 ≤ 524 288 B（`export-fixtures.mjs` 的 `SIZE_GATE_FILE_BYTES` / `SIZE_GATE_TOTAL_BYTES`），写盘**之前**判定。当前 272 291 B / 最大 56 505 B。

> **旧门限「14 份 < 2 MB」作废**（ADR-010 §8）：它是给旧口径（每 tick 全量投影落盘）定的，在旧口径下 14 份必然放不下（7 份已达 1 950 729 B，实测 ~950 B/tick）。新口径把"逐帧证据"换成"逐帧哈希链 + 少量关键帧"，同一批场景缩到 272 KB，剩下的余量足够后续场景继续加。门限从"2 MB 只为装下文本"改成"64 KB/份、512 KB/批"，判据不变：**仍然是每一 tick 的同一份 `%.17g` 投影逐位比较**。

导出侧对 v1 做了三件事（都只作用于进程内 / 可写副本，源仓库不变）：

1. **共享整数表替换**：把 v1 的 `Math.sin/cos/atan2/asin` 换成 `docs/evidence/fixtures/trig-table.json` 上的 `sinUnits/cosUnits/angleUnitsFromVector/angleUnitsFromRatio`（与 `server/src/core/trig_table.hpp` 1:1 同口径）。
2. **RNG 状态口径**（§5.2）：用计数包装统计每流抽取次数，再用同一推导 `derived = (imul(seed>>>0, 2654435761) + streamId) >>> 0`（ai 1 / spawn 2 / fx 3）重放同样次数得到 `mulberry32` 的内部状态 `a`。C++ 侧直接读 `World::rng.<stream>.a`。
3. **基线纪律（可判定）**：默认拒绝 `--root` 指向只读源；启动时逐文件比对派生副本与只读源（`packages/shared/src`，不一致直接失败，除非显式 `--allow-patched-copy`）；结束时比对只读源的 (文件数, 总字节, 最新 mtime) 指纹，被写入即报错。`Math.round`/`Math.hypot` 这类 JS 语义差异**不在**本批补丁内（见 §6 风险 1/2）。

## 6. 判读不通过、已知缺口与风险

比较器只打印**第一处**差异，格式冻结：`DIFF <fixture> tick=<n> field=<path> expected=<hex> actual=<hex>`（double 用 `bit_cast<uint64_t>` 的 16 位十六进制）。处理顺序：

1. `field=configHash`（`comparedTicks=0`）→ 常量表两侧已漂移：先查 `server/src/config/**`、`server/src/sim/arena.hpp`。
2. `field=hashChain` → 该 tick 的**全量投影**已经不一致。打开两侧的投影文本出口对表（`node tools/export-fixtures.mjs --trace <name> --trace-ticks <n> --trace-text` 与 C++ 的 `AC_FIXTURE_PTEXT=1` + `AC_FIXTURE_DUMP=<tick>`），逐字段找到第一个不同的字符；`field=entities[i].*` / `evt[...]` / `rng` 这类关键帧字段名则直接给出出错字段。
3. `field=snapshot.encodeHash` → 15 字节量化记录块（无报文头、无计数字节）逐字节不一致：查 `quantizeSnapshotEntity` 与 `net::writeEntityRecord` 的字段/位宽。`snapshot.decodeHash` ≠ 则是解码投影字段不一致。
4. fixture 是**生成物**：禁止手工编辑。数值规则变更必须先改 v1 或显式重建向量，再两侧同时改（ADR-010 §7）。**禁止改期望值迁就实现**——本批 4 处不一致全部是 C++ 侧实现缺陷（见下文），改的是实现，不是向量。
5. `entities[].hp` 是 **double**（护甲吸收会产生小数，例：步枪命中后 `96.8`）→ 读取器按 double 解析、比较走 `bit_cast` 逐位。`flags` 位表两侧同源：bit0 `downed` / bit1 `rageMode` / bit2 `reloading` / bit5 `idle`。

**本批由向量抓到的 C++ 实现缺陷（全部已修，改的是实现）**：

| # | 缺陷 | 后果 | 修法 |
|---|---|---|---|
| 1 | `resetWorld` 的 memset 把 `FlockNeighbors::capacity` 清零，而 `resetFlockNeighbors` 没恢复 | 邻居列表恒空 → C++ 羊群聚集完全失效（v1 有 1–2 个邻居） | `server/src/ai/sheep_state.hpp`：`resetFlockNeighbors` 里写回 `kSheepMaxNeighbors` |
| 2 | 空间网格格边长用 4 m / 20×20，v1 是 `createSpatialGrid(SHEEP_AI.neighborRadiusM)` = 3 m / 27×27 | 分离遍历的**配对次序**不同 → 位置差 ~1e-3（tick 269/317/342 起） | `server/src/sim/spatial_grid.hpp`：`kSpatialCellMeters = kSheepAi.neighborRadiusM`、`kSpatialCellsPerAxis = 27` |
| 3 | 分离遍历按"格对"分组（同格、东、南、东南、西南），v1 是"按实体 a：同格 j>i → 东/北/东北/西北" | 浮点累加次序不同 → 1 ULP 级位置差（tick 269 起） | `server/src/sim/collision.cpp`：`separateEntities` 逐字对齐 v1 的两层循环与偏移表；方向先取 `1/distance` 再乘（`dx / d` 与 `dx * (1 / d)` 可能差 1 ULP） |
| 4 | 快照哈希从 `kSnapshotHeadBytes`（22 B，到 `baselineTick` 为止）起算，漏掉了 1 字节记录数 | 记录块整体错位 1 字节 → `snapshot.encodeHash` 必错 | `server/tests/fixture_test.cpp`：块起点 = `kSnapshotHeadBytes + 1`（报文格式本身不动） |

**已知覆盖缺口（诚实记录，不能在报告里含糊）**：

1. `wave-director-1to5-1200t` 名字里的 **1→5 只是意图**：1200 tick 内导演只走到 **wave 1**（18 只已达本波上限，`spawned=18/total=18`）。要真覆盖 1–5 需要 5 波 × （波内清理 + 20 s 间歇），远超 1200 tick；名字按人类裁决保留，缺口记在这里。
2. `sheep-elite-bolt-300t` 的问号弹只在**距离 ≤ ~21 m** 时命中：v1 的 `advanceProjectiles` 把 `aliveMs` 加了两次（`BOLT_LIFE_MS=3000` → 实际 30 tick 生命周期），保距羊停在 ~25–35 m 就永远打不到。该行为是 v1 冻结语义，向量按原样记录（不许"修好"v1）。
3. `rng-streams-600t` 的 `fx` 流**抽取次数为 0**（没有任何表现层消费者），所以它验证的是 `ai`/`spawn` 两流的归属与状态推进；`fx` 只保证"两侧读取器都读到 0 次抽取后的同一状态"。
4. 羊王 2400 hp 与 4 人满 DPS 不相容（4 人连续开火 ~150 tick 就能打死），所以 `sheep-king-phases-900t` 用 24 tick 一次的脉冲射击把节奏压到 900 tick 才跨两个阈值；`playerDowned` 事件因此只在后段出现。

**风险（留给后续计划）**：

1. **`Math.round`**：v1/JS 语义是 `floor(x + 0.5)`（`-1.5 → -1`），C 的 `std::round`/`llround` 是"远离 0"（`-1.5 → -2`）。跨语言量化必须用 `floor(x + 0.5)`（S02 `quantizeAngle` 已经是这个口径）。
2. **`Math.hypot`**：v1 `combat/resolve.ts` 的命中盒对角线用 `Math.hypot`，它**不等于** `sqrt(a*a + b*b)`（逐位）。C++ 侧 `resolve.cpp` 用 `sqrt`，目前只影响"早退候选"（最多 1 ULP，不影响命中集合），本批向量也没有踩到边界；一旦有场景踩到，必须先冻结这条口径。
3. **投影文本出口是调试口**：`--trace` / `--trace-text`（导出侧）与 `AC_FIXTURE_DUMP` / `AC_FIXTURE_PTEXT`（C++ 侧）只影响输出、不参与比较，别把它们当成协议。
4. **只读源指纹**：`export-fixtures.mjs` 结束时比对 `packages/shared/src` 的 (文件数, 总字节, mtime)；CI 上若 v1 源被并行任务动过，导出会直接失败（这是有意的）。

## 7. 客户端读取器迁移要点（v2 契约）

> 面向 `client/**` 的维护者。本节只写**读**什么、与旧版差在哪；本批不改客户端代码（`client/**` 冻结）。
> 契约性质：S07 §5 与 [ADR-010](../../00-共识/ADR/ADR-010-跨语言确定性与对拍.md) §8 —— fixture 是**双侧共享生成物**，schema 换代必须两侧同批迁移。盘上现在已是 v2（14 份 / 272 291 B，`node tools/export-fixtures.mjs --check` → `14/14 与盘上逐字节一致`）。

### 7.1 两套 fixture 不要混

| 目录 | 内容 | 谁在读 | 本批是否受影响 |
|---|---|---|---|
| `docs/evidence/fixtures/*.json` | 本目录 14 份**对拍向量**（v2 schema） | `client/Assets/Tests/FixtureSuite.cs`、`FixturePredictSuite.cs`（经 `client/Assets/Scripts/Sim/FixtureLoader.cs`） | **受影响**（见 §7.2–7.4） |
| `server/tests/fixtures/*.hex` | S03 §5.5 的共享**字节**向量（`expect=` 头 + `hex`） | `client/Assets/Tests/CodecSuite.cs` | 不受影响（格式未变） |

### 7.2 v2 顶层键（键序冻结，读取器逐键校验）

```text
name, version, seed, dtMs, configHash, ticks,
setup { players[{id,hp,armor}], sheep[{kind,x,z}] },
director { startWave },
script [ {from,to,commands[{id,moveX,moveY,yaw,pitch,buttons,switchTo}]} ],
keyframes [ {tick, entities[], events[], rngState} ],
snapshot [ {tick, records, encodeHash, decodeHash} ],
hashChain [ 每 tick 一个 16 位十六进制 ]
```

| 键 | 类型（实测） | 语义 | 与 v1 读取口径的差别 |
|---|---|---|---|
| `version` | number `2` | schema 版本 | 读取器应以 `version == 2` 认形状（旧读取器认的是"有 `ticks` 数组且 `ticks[0].expected`"） |
| `ticks` | number（例 `60` / `120` / `900`） | tick 数 | **不再是数组**；每 tick 的 `expected` 对象**已不存在** |
| `setup` | object | 初态（§3 的表） | 初态在文件里，不再靠"`createWorld` 隐含生成 4 名玩家" |
| `director` | object | `{startWave}` | 导演在 `stepWorld` **之外**，两侧都要外部驱动（§3 末尾） |
| `script` | array | 按 tick 的 **RLE**（`from`/`to` 闭区间）+ `commands[]` | 旧的 `ticks[t].commands[]` 不复存在；v2 条目**没有 `seq`/`clientTick`**，本地复现需自行补（例：`seq = clientTick = t`） |
| `keyframes` | array（本批 3–5 个） | `{tick, entities[{id,kind,pos[3],yaw,pitch,hp,flags}], events[{tick,type,flags,subjectId,targetId,x,y,z,value[,kind]}], rngState{ai,spawn,fx}}` | `entities` 形状与旧 `expected.entities` **相同**（`pos[]`/`yaw`/`pitch`），旧 `FindEntity` 逻辑可直接复用，只需改"从哪里取"；`events[].type` 是**字符串**名（`playerHit`/`sheepKilled`/…）；`events[].kind` 只在带种类的类型上出现（目前只有 `sheepKilled`，见下条收口） |
| `snapshot` | array（仅 `snapshot-roundtrip-240t` 非空） | `{tick, records, encodeHash, decodeHash}` | 量化记录块（无报文头）与解码投影的 FNV |
| `hashChain` | array[string]，长度 = `ticks` | 每 tick 一个链节点（8 B / 16 位十六进制） | **取代**"每帧投影文本落盘" |

- 事件载荷里的 `sheepKilled.kind` **是羊种类枚举**（0 grunt / 1 ram / 2 elite / 3 king，S03 §5.4 / S08）：客户端读取与渲染一律**按枚举解码**，别当数值用；v1 曾把伤害值写在同一字节上，v2 不逐字继承 v1 的字节行为（服务端已裁决，契约见 `server/src/room/event_map.cpp:53-63`，用例 `room_event_sheep_killed_kind_is_sheep_kind_enum_not_damage`，`server/tests/match_flow_test.cpp:1149`）。

> 收口（B 部分 B2 收尾：原先「该字节没有跨语言向量覆盖」的待办**已消除**）：`kind` 已纳入对拍口径 —— ① **投影文本**（每 tick 的字节口径）对带种类的类型在该行末尾追加 `,kind`：导出侧在事件入队**那一刻**用 `target.ai.sheepKind` 补出（v1 的 `sim/events.ts` 不写 `kind`；`tools/export-fixtures.mjs` 的 `KIND_CARRYING_EVENT_TYPES` + `captureEventKinds`，只在进程内挂 `world.events.push`、不改只读源），C++ 侧同式（`server/tests/fixture_io.hpp::eventCarriesKind` + `fixture_io.cpp::projectionText`，值取 `Event::kind`）⇒ 该字节**每 tick 随 `hashChain` 双侧重算**；② `keyframes[].events[]` 也只在带种类的类型上输出 `kind`（C++ `readEvent` 按**可选键**读、`compareKeyframe` 逐字段比）。实测：重导 14 份后有 5 份的 `hashChain` 变了（击杀种类：`rifle-burst-hit-120t` = 2 elite、`rng-streams-600t` = 3 king、`shotgun-spread-60t` / `snapshot-roundtrip-240t` / `wave-director-1to5-1200t` = 0 grunt），`node tools/export-fixtures.mjs --check` → `check ok：14/14 与盘上逐字节一致`，合计/单份最大仍是 **272 291 B / 56 505 B**（只换了链值、没加字节）；两侧逐字段对表实测一致（JS `evt=412,sheepKilled,5,1,5,-3.9873353560822253,0,7.8034467994416268,40,3` == C++ `AC_FIXTURE_PTEXT=1 AC_FIXTURE_DUMP=412` 的同一行）。**边界（如实登记，非待办）**：本批 14 份的关键帧 tick 恰好都没落在击杀 tick 上 ⇒ `keyframes[].events[].kind` 目前「结构就位、无数据实例」；该字节的钉靠**每 tick 的 hashChain**（已含 2/3 两种非 0 种类）+ 服务端 wire 用例。该**可选键**本身的读法（带 `kind` 读得出、不带取默认 0、投影只对带种类的类型追加 `,kind`）由新用例 `fixture_event_kind_optional_key`（`server/tests/fixture_test.cpp`）用一份最小 v2 向量钉住（`--filter=fixture` 19 → 20）；若要在关键帧里也造实例，需把某个场景的关键帧 tick 挪到击杀 tick 上（会改关键帧集合与合计体积），本批按「不动向量场景」处理。依据：`tools/export-fixtures.mjs` 的 `captureEventKinds`、`server/tests/fixture_io.cpp`（`projectionText`/`readEvent`）、`server/tests/fixture_test.cpp`（`projectEvents`/`compareKeyframe`/`fixture_event_kind_optional_key`）。

### 7.3 比较口径：两侧都**重算**，不再读期望值

- 链：`h_i = fnv1a64(投影文本_i, h_{i-1})`，`h_0 = 0xcbf29ce484222325`，质数 `0x100000001b3`；投影文本 = `tick=` / `dtMs=` / 逐实体 `ent=id,kind,x,y,z,yaw,pitch,hp,flags` / 逐事件 `evt=tick,type,flags,subjectId,targetId,x,y,z,value[,kind]` / 尾行 `rng=ai,spawn,fx`，double 一律 `%.17g`，实体顺序 = `activeIds` 顺序。
- 所以"读 `ticks[t].expected` 逐位比"的老路要换成**自己算出同一份投影文本再串链**。要覆盖**每一个** tick，就得写**全量投影**（所有实体 + 事件 + 三流 RNG 状态），只算本机实体是接不上链的。
- 现阶段**最小可用迁移**（不要求一次写完全量投影）：① 形状校验改为 `version == 2` 且 `hashChain.length == ticks`；② 用 `script`（RLE 展开 + 自补 `seq`/`clientTick`）从 `setup` 驱动本地 sim；③ 在 `keyframes[].tick` 那 3–5 个点上对 `entities[]`/`events[]` 逐字段、逐位比（字段名与 v1 投影同名）；④ `hashChain`/`snapshot` 先只校验"长度与十六进制形状"，全量投影就绪后再接链。
- `configHash` 仍必须一字不差（当前 `19a978ea`）：它是 §5.6 常量表摘要，两侧同源。

### 7.4 客户端当前会红的用例（按读取器代码推断，未运行 Unity 用例）

| 位置 | 现状（读的键） | 为什么会红 |
|---|---|---|
| `client/Assets/Scripts/Sim/FixtureLoader.cs:45-46`、`:84-94` | `TicksKey="ticks"`、`ExpectedKey="expected"`；`IsShaped` 要求 `ticks` 是数组且 `ticks[0].expected` 存在 | v2 的 `ticks` 是数字、没有 `expected` ⇒ `Load` 返回 **0 份**（`TickCount` 也会算成 0） |
| `client/Assets/Tests/FixtureSuite.cs:26-31` | 断言"至少 4 份向量、≥ 1000 帧" | 承上 ⇒ 0 份 ⇒ 断言直接红 |
| `client/Assets/Tests/FixturePredictSuite.cs:42`、`:73`、`:80`、`:84-87`、`:180` | `Discover` 要 `ticks` 数组；`ChecksFixture` 读 `ticks[t].commands[]` 与 `ticks[t].expected.entities[]` | 两个键都不存在 ⇒ `fixture_predict.manifest` 的覆盖断言先红，逐份用例拿不到命令/实体同样红 |
| `client/Assets/Tests/FixtureSuite.cs:61-62` | 探针字符串（`$.ticks[1].x` 等） | 只是探针文本，迁移时跟着改路径即可，不是数据问题 |

> 结论：这是 ADR-010 §8 的**双侧契约变更**，不是"服务端单方面换格式"。本批（服务端）只保证 v2 向量在盘上自洽（`--check` 14/14、体积门 272 291 B ≤ 524 288 B）；客户端的读取与比较按 §7.2/§7.3 在客户端批次里迁移。缺口与风险照本目录 §6 的口径登记，本节不放宽任何门限。
