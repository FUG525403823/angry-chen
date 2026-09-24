# S07 旧 TS 模拟对拍
> 里程碑：MS07 ｜ 预估：1 人日 ｜ 上游移交物：HANDOFF-S06

## 1. 目标

本步骤结束后，跨语言确定性有了**可机器判定的唯一来源**：冻结的 v1 `packages/shared` 的权威模拟结果被导出为纯文本对拍向量并入库，C++ 权威内核加载同一批向量**逐位复现**；任何数值漂移都会在 CI 里以「第一个差异字段」的形式精确报出，而不是以"手感变了"的形式被发现。

## 2. 入口条件

**入口条件**：HANDOFF-S06

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 权威步进与共享角度表可用 | `powershell -NoProfile -File server/build.ps1 -Config Release` 后 `server/build/ac_tests.exe --filter=trig` | 末行 `[build] ok`；`TESTS 4/4`（与 S02 §6 同数） |
| 2 | v1 冻结实现可读：`D:\projects\tmp\angry-chen-bak` **全程只读**，打补丁与导出只在可写派生副本 `D:\projects\tmp\angry-chen-fixture` 上做 | `Test-Path D:\projects\tmp\angry-chen-bak\packages\shared\src\index.ts`；`Test-Path D:\projects\tmp\angry-chen-fixture\packages\shared\src\index.ts` | 两处都 `True` |
| 3 | Node 能直接 import `.ts` 源码（类型剥离） | 在备份仓库根执行 `node -e "import('./packages/shared/src/index.ts').then(m=>console.log(m.SERVER_TICK_MS))"` | 打印 `50` |
| 4 | 模拟内核可编译 | [server/README.md](../../../server/README.md) 的「构建」章节 | 产出服务器可执行文件 |
| 5 | 零素材约束与确定性规则已冻结 | `ls docs/evidence`（`docs/evidence`）、[ADR-010](../../00-共识/ADR/ADR-010-跨语言确定性与对拍.md) §6 | 目录存在；fixture schema 与逐位比较规则明确 |

跨链前置：无（对拍向量的来源是**冻结的 v1 代码**，不是某份 v2 计划）。

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `tools/export-fixtures.mjs` | 新建 | 读 `--root` 指定的 v1 派生副本（`D:\projects\tmp\angry-chen-fixture`；只读源仓库 `angry-chen-bak` 不得写入），导出 fixture JSON 与 `configHash`；`--check` 幂等校验、`--list` 清单 |
| `docs/evidence/fixtures/*.json`（14 份）+ `README.md` | 新建（入库） | 纯文本对拍向量（schema 见 §5.1）与清单/再生成说明 |
| `server/tests/fixture_io.hpp/.cpp` | 新建 | 只认 §5.1 固定 schema 的极简 JSON 读取、`bit_cast` 比较、`DIFF` 报告、`configHash` 重算 |
| `server/tests/fixture_test.cpp` + 构建脚本登记 | 新建/修改 | 逐 fixture、逐 tick、逐字段比较，任一不等即打印首个差异并退出 1 |

## 4. 任务清单

- [ ] 写 `tools/export-fixtures.mjs`：先在可写派生副本 `D:\projects\tmp\angry-chen-fixture` 上按 S02 的约定用共享整数表**替换** v1 侧的 `Math.sin/cos/atan2/asin`（表内容取自 `docs/evidence/fixtures/trig-table.json`；`atan2`/`asin` 用 S02 的 `angleUnitsFromVector` / `angleUnitsFromRatio` 整数查表实现），再用 v1 的 `createWorld/stepWorld/spawnEntity` 构造 §5.3 的 14 个场景并推进（只读的 `angry-chen-bak` 全程不得写入），按 §5.1 写出期望投影。
- [ ] 实现 §5.6 的 `configHash`（CRC32C、固定键序与 `%.17g` 格式）与 §5.2 的 RNG 计数包装与状态重放，两者都写在 `tools/export-fixtures.mjs` 里。
- [ ] 写 `server/tests/fixture_io.cpp`：固定 schema 解析（键序固定、未知键即失败）、逐位比较、`DIFF` 单行报告、用 S02 的 `ac::crc32c` 重算 `configHash`。
- [ ] 写 `server/tests/fixture_test.cpp` 并按 S01 的 `AC_TEST` 注册进 `main_test.cpp`：`configHash` → 逐 tick → 实体/事件/RNG 的比较顺序，首个差异即失败。
- [ ] 写 `docs/evidence/fixtures/README.md`：清单表、每份场景与 tick 数、再生成命令、体积门、不通过时的处理流程。
- [ ] 跑一次导出并把 14 份 JSON 与 README 一起入库；在 `server/README.md` 写入 `--filter=fixture` 命令与再生成流程。

## 5. 冻结契约

### 5.1 fixture JSON schema（逐字采用 ADR-010 §6 字段）

```json
{
  "name": "straight-line-240t",
  "seed": 11,
  "configHash": "8f2a41d0",
  "ticks": [
    { "dtMs": 50, "commands": [
      { "id": 1, "seq": 0, "clientTick": 0, "moveX": 1.0, "moveY": 0.0,
        "yaw": 0.0, "pitch": 0.0, "buttons": 0, "switchTo": 0 } ] }
  ],
  "expected": {
    "entities": [ { "id": 1, "kind": "player", "pos": [0.0, 0.0, 0.0],
                    "yaw": 0.0, "pitch": 0.0, "hp": 100, "flags": 0 } ],
    "events": [ { "tick": 3, "type": "playerHit", "flags": 0,
                  "subjectId": 1, "targetId": 2, "value": 20 } ],
    "rngState": { "ai": 1234567, "spawn": 2345678, "fx": 3456789 }
  }
}
```

| 字段 | 类型 | 规则 |
|---|---|---|
| `name` / `seed` | string / uint32 | 文件名 = `name + ".json"`（只含 `[a-z0-9-]`）；`seed` 与 v1 `createWorld(seed)` 一致 |
| `configHash` | 8 位小写十六进制 | §5.6 的 CRC32C（S02 `ac::crc32c`）；不匹配则**不跑 tick** 直接失败 |
| `ticks[]` | 对象数组 | 每项一个 tick；`dtMs` 恒为 50（其他值即 schema 失败）；`commands[]` 按实体 `id` 升序，字段与 S06 冻结的命令结构同名（`clientTick` 即 v1 `Command.tick`，`yaw` / `pitch` 为 double 弧度） |
| `expected.entities[]` | 对象数组 | 该 tick 结束时的活跃实体投影：`id`、`kind`、`pos`[3]、`yaw`、`pitch`、`hp`、`flags` |
| `expected.events[]` | 对象数组 | 该 tick 入队的事件（按入队顺序）：`tick`、`type`、`flags`、`subjectId`、`targetId`、`value` |
| `expected.rngState` | 对象 | 三条流的 `mulberry32` 内部状态 `a`（uint32），口径见 §5.2 |
| `yaw` / `pitch` | double 弧度 | 写盘用 `%.17g` 可无损往返；与线上 u16 角度单位经 `quantizeAngle` / `radiansFromUnits` 精确互换（S02 §5.3、§5.4）；逐位比较的是弧度 double |
| `flags` | 位域 | `expected.entities[]` 与 `expected.events[]` 共用同一张表：bit0 `downed`=1 / bit1 `rageMode`=2 / bit2 `reloading`=4 / bit3 `charging`=8 / bit4 `fading`=16 / bit5 `idle`=32（与 S03 §5.3 实体记录 `kindFlags` 的 flags 位完全一致），其余位保留为 0 |
| 键序与格式 | — | 键序固定如本节的 JSON 示例；缩进 2 空格；整数不带小数点，浮点统一 `%.17g` |

### 5.2 RNG 状态口径

v1 `mulberry32` 的闭包状态不可观测，因此导出脚本用计数包装（`world.rng.ai` 等的可写属性）统计每流**抽取次数**，再用同一推导重放同样次数得到内部状态 `a` 并写入 `rngState`。推导式与流编号取自 v1 `rng.ts`，C++ 侧已有实现：`derived = (imul(seed >>> 0, 2654435761) + streamId) >>> 0`，`streamId` 为 `ai 1 / spawn 2 / fx 3`。C++ 侧必须暴露「读内部状态」的接口；`fx` 流只允许被表现层消费，不得参与模拟（状态不同即失败）。

### 5.3 覆盖场景与 fixture 清单（14 份，逐字节入库）

| fixture | 场景 | tick 数 | 关键覆盖 |
|---|---|---|---|
| `still-60t.json` | 静止 | 60 | 缺命令分支、位姿不动 |
| `straight-line-240t.json` | 直线移动 | 240 | 移动/冲刺速度、积分精度 |
| `barn-collision-400t.json` | 碰撞（谷仓） | 400 | 谷仓推离与速度清零 |
| `fence-bounds-400t.json` | 边界（栅栏） | 400 | `limit = 39.35` 夹取 |
| `rifle-burst-hit-120t.json` | 连射命中 | 120 | 射速节流、散布累积、伤害 |
| `shotgun-spread-60t.json` | 霰弹散布 | 60 | 8 弹丸、抖动派生 |
| `downed-revive-140t.json` | 倒地救援 | 140 | 倒地、救援进度、中断后重置 |
| `sheep-grunt-ai-600t.json` | 咩咩兵 AI | 600 | 吃草/警戒/追击、聚集 |
| `sheep-ram-charge-300t.json` | 冲撞羊冲锋 | 300 | 蓄力、冲锋、硬直 |
| `sheep-elite-bolt-300t.json` | 问界羊问号弹 | 300 | 距离保持、投射物生命周期 |
| `sheep-king-phases-900t.json` | 羊王阶段 | 900 | 阶段阈值、召唤与 `spawn` 流 |
| `wave-director-1to5-1200t.json` | 波次导演预算 | 1200 | 预算、组队顺序、出生点选择 |
| `snapshot-roundtrip-240t.json` | 快照编码 round-trip | 240 | 量化字段编解码后位型等价 |
| `rng-streams-600t.json` | 三流归属 | 600 | 抽取次数与内部状态 |

### 5.4 比较规则（容差 0）

- 只有相等与不等：double 用 `std::bit_cast<uint64_t>` 逐位比较（`-0.0` 与 `0.0` 视为不等），整数与字符串精确比较。
- 比较顺序：`configHash` → 逐 tick（`entities` 按 `id` 升序 → `events` 按序 → `rngState` 三流）；每个 fixture 必须提供**每个 tick** 的期望投影，缺项按失败处理（禁止抽样跳过）。
- 差异报告格式（只打印第一处，然后汇总）：

```text
DIFF straight-line-240t tick=37 field=entities[0].pos.z expected=0x4001800000000000 actual=0x40017fffffffffff
fixtures: 13/14 passed, 1 failed
```

### 5.5 再生成与入库纪律

- 命令（工作目录 = 仓库根 `D:\projects\tmp\angry-chen`）：`node tools/export-fixtures.mjs --root D:\projects\tmp\angry-chen-fixture --out docs/evidence/fixtures`（写盘）、`--check`（只比较）、`--list`（清单：`name`、字节数、SHA256）。`--root` 指向可写派生副本；`D:\projects\tmp\angry-chen-bak` 只读，不得作为补丁或导出目标。
- fixture 是**生成物**：禁止手工编辑；数值规则变更必须先改 v1 或显式重建向量，再两侧同时改（ADR-010 §7）。
- 体积门：14 份 fixture 总字节 < 2 MB（ADR-010 的复查触发条件）。

### 5.6 `configHash` 定义

CRC32C（S02 `server/src/core/hash.hpp`，反射多项式 `0x82F63B78`，输出 8 位小写十六进制）作用于固定顺序的常量表遍历：场地、实体半径/高度/基础属性、玩家、输入位域、武器表与散布常量、战斗常数、四种羊形参数、羊形 AI 参数、羊形状态转移表、羊形命中盒、波次规则；每项先写键名再写值、各占一行（整数十进制，double 用 `snprintf(buf, "%.17g", v)`，数组与对象按声明序）。任一侧数值改动而另一侧未同步，`configHash` 立即不等，比较在跑 tick 之前就失败。

## 6. 验证

| # | 命令 | 期望输出 | 失败意味着什么 |
|---|---|---|---|
| 1 | `node tools/export-fixtures.mjs --root D:\projects\tmp\angry-chen-fixture --out docs/evidence/fixtures`（cwd = 仓库根） | `exported 14 fixtures, bytes=…`，退出 0 | v1 冻结包被改动，或场景构造不符合 §5.3 |
| 2 | `node tools/export-fixtures.mjs --root D:\projects\tmp\angry-chen-fixture --check` 与 `--list` | `14/14 identical`；14 行场景名与 §5.3 逐字一致 | 导出存在非确定来源（时间、哈希序、locale）或场景缺失 |
| 3 | `Get-ChildItem docs/evidence/fixtures -Recurse -File \| Get-FileHash -Algorithm SHA256`，与 `--list` 打印的 SHA256 清单逐条比对 | 14 行一致 | 有人在手工改生成物 |
| 4 | `powershell -NoProfile -File server/build.ps1 -Config Release` | 末行 `[build] ok ac_server.exe`，退出 0 | 读取器或内核接口未对齐，或 `AC_TEST` 未注册 |
| 5 | `server/build/ac_tests.exe --filter=fixture` | `TESTS 14/14` | 数值漂移，看首行 `DIFF` 定位字段 |
| 6 | `Get-ChildItem docs/evidence/fixtures -Recurse -File \| Measure-Object Length -Sum`；`node tools/check-docs.mjs` | 总和 < 2097152；文档打印 `OK` | 向量过大（需按 ADR-010 复查条件抽样）；文档链或链接坏了 |

## 7. DoD（验收标准）

- [ ] 上表 6 条验证全部通过，`--filter=fixture` 输出 `TESTS 14/14`。
- [ ] [工程约定](../../00-共识/工程约定.md) §5 的质量门 `node tools/check-docs.mjs` 与 `node tools/check-assets.mjs` 全绿。
- [ ] 本份新增门：`--check` 幂等（连跑两次字节相同）；C++ 逐位比较 14/14 通过。
- [ ] 本份新增门（反例自检，已记录在 `docs/evidence/fixtures/README.md`）：手工改一位数字能让测试以 `DIFF` 首行失败；改 `configHash` 能让比较在跑 tick 之前失败。
- [ ] 14 份 fixture 入库；只读源仓库 `D:\projects\tmp\angry-chen-bak` 全程未被写入（生成前后 `packages/shared/src/index.ts` 的 `LastWriteTime` 不变），补丁只打在可写派生副本 `D:\projects\tmp\angry-chen-fixture` 上。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| v1 副本被无意写入导致基线漂移 | 只读副本文件的 `LastWriteTime` 变化 | 从 `angry-chen-bak` 重新复制一份干净派生副本到 `D:\projects\tmp\angry-chen-fixture` 再导出；基线只认 `angry-chen-bak` |
| 导出存在非确定来源（键序、locale、浮点格式） | `--check` 两次结果不同 | 数字固定 `%.17g`、只遍历数组与插入序、禁用 `toLocaleString` 与排序容器 |
| C++ 读取器过宽，坏 fixture 也能通过 | 反例自检改一位后仍通过 | 未知键、未知 kind、缺失 tick 一律失败；反例自检进 README |
| 14 份向量跑得过慢 | 单次 `--filter=fixture` > 30s | 先定位最慢场景；仅对超时场景抽样并记录覆盖率 |
| v1 侧没有换成共享整数表 | 所有移动类 fixture 全红且偏差恒定 | 导出脚本必须先替换 v1 的 `Math.sin/cos/atan2/asin`；`--filter=trig` 先于对拍运行，表与向量同源 |

回滚目标：回到 `HANDOFF-S06` 的状态（无 fixture、无对拍测试）。回滚方式为删除 `tools/export-fixtures.mjs`、`docs/evidence/fixtures/` 与 `server/tests/fixture_*.cpp`，`main_test.cpp` 去掉对应 `AC_TEST` 注册；模拟内核不受影响。

## 9. 移交物

**移交物 ID**：HANDOFF-S07

给下一步的稳定接口：`docs/evidence/fixtures/*.json`（14 份）与 §5.1 冻结 schema；`configHash`（CRC32C）定义见 §5.6；再生成命令 `--root <派生副本>` / `--out` / 校验 `--check` / 清单 `--list`；`server/tests/fixture_io.hpp` 的读取与位型比较接口（供后续回归测试复用）；差异报告格式 `DIFF <fixture> tick=<n> field=<path> expected=<hex> actual=<hex>`。

已验证能力清单：静止、直线移动、谷仓碰撞、栅栏边界、连射命中、霰弹散布、倒地救援、四种羊形 AI、羊王阶段、波次导演、快照 round-trip、三流 RNG 共 14 个场景在 C++ 侧逐位复现；`configHash` 在数值漂移时先于 tick 比较失败；fixture 为纯文本、入库、可幂等再生成，总体积受 2 MB 门约束。
