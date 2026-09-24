# 客户端 C02 验收记录（HANDOFF-C02）

- 计划：[`C02-客户端数学量化与协议解码.md`](../plans-v2/client/C02-客户端数学量化与协议解码.md)
- 修订：本节记录的是 `MC02` 标签指向的修订（首版 `be6a906` + 第二轮复审修复，见 §4）。
- 环境：Windows + 团结引擎 `2022.3.62t16`（`D:\tools\unity\unity_install\2022.3.62t16\Editor\Tuanjie.exe`）。
- 采集方式：命令逐条复跑，正文粘关键行与摘要（不是整份日志逐字节转载）；完整日志留在 `client/Logs/`（生成物，不入库）。

## 1. §6 六条验证（实测）

### 1.1 §6.1 共享角度表一致

```
$ node tools/export-trig-table.mjs --check
trig-table.json OK (65536 entries) sin=0x8BD9F737 atan=0x197C3A8D asin=0xAD2BD35E      exit=0
```

`docs/evidence/fixtures/trig-table.json`：709640 B、无 NUL 字节、键为 `scale`/`units`/`sin`/`atanUnits`/`asinUnits`。
抽样断言（`QuantizeSuite` 内）：`sin[8192]=759250125`、`sin[49152]=-1073741824`、`atan[512]=4836`、`asin[512]=5461`。

### 1.2 §6.2 核心自检（`Ac.Core.SelfTest.Run`）

```
SELFTEST START cases=2
PASS core.json.parse
PASS core.json.reject
SELFTEST OK cases=2                                                    exit=0，error CS 0 条
```

`Ac.Core` 的 asmdef 是 `references: []`（C01 §5.2 的分层是机器判据），所以核心入口只跑 `core.json.*`；
其余用例组走 §1.4。这不是覆盖缺口，而是分层规则的直接后果（C01 §9 已把末行格式指向本份 §5.7）。

### 1.3 §6.3 禁超越函数扫描

```
$ Get-ChildItem client/Assets/Scripts/Sim, client/Assets/Scripts/Net -Recurse -Include *.cs |
    Select-String -Pattern '(Math|MathF)\.(Sin|Cos|Tan|Sinh|Cosh|Tanh|Asin|Acos|Atan|Atan2|Exp|ExpM1|Log|Log2|Log10|Log1p|Pow|Cbrt|Hypot)'
（无输出）
```

模式在第二轮复审后从「只有 `Math.` 的 11 个函数」扩到「`Math`/`MathF` + 双曲/指数/对数/幂/开方」，本轮实测仍为空。
计划 §6 第 3 条与 `client/README.md` 已同步为同一模式。

### 1.4 §6.4 分组自检（`Ac.Tests.SuiteRegistry.RunAll`）

```
SELFTEST START cases=11
PASS c01.assemblies.references   PASS c01.project.identity    PASS c01.project.serialization
PASS c01.build.backend           PASS c01.render.pipeline     PASS vec3.ops
PASS rng.streams                 PASS quantize.edge           PASS quantize.rounding
PASS codec.roundtrip             PASS fixtures.loader
SELFTEST OK cases=11                                                   exit=0，error CS 0 条
```

共享字节 fixture（`server/tests/fixtures/`，10 件）逐键复现：`hello`、`hello_ack`、`keepalive`、`command`、
`truncated_command`、`snapshot_full`、`snapshot_diff`、`event_hit`、`fragment`、`resume`；每个 `# expect:` 键都被消费，
`expect.Count` 必须归零（新增键不会被静默跳过）。

fixture 输出行（`[fixture]` 前缀）：

```
[fixture] none
[fixture] none
[fixture] probe OK
[fixture] probe-mismatch tick=42 field=$.entities[0].xCm expected=150 actual=151
[fixture] probe-ticks tick=1 field=$.ticks[1].x expected=2 actual=9
```

后三行是自造探针（`fixtures.loader` 用来证明「同一文档通过」「差异定位 + 上下文 tick」「`$.ticks[i]` 反推 tick」三条路径），
`probe*` 不是真 fixture，按 `field=` 直接 grep 日志的门禁要排除这个前缀。

### 1.5 §6.5 仓库门禁

| 门 | 输出 | 退出码 |
|---|---|---|
| `node tools/check-docs.mjs` | 扫描 51 个文档、解析相对链接 157 条，v2 30 份计划线性链通过 | 0 |
| `node tools/check-assets.mjs` | 本轮实测 209 个受控文件（复核时已涨到 217，随并行会话往 `server/` 加文件）、二进制嗅探同数、零素材类扩展名、依赖白名单未破坏 | 0 |

受控文件数从首版的 170 涨到 209：并行服务端会话在同一工作树上新增了 `server/src/sim/*`，属已知漂移（不是本份引入）。
本份新增源码文件 20 个 `.cs` + 20 个 `.meta`。

### 1.6 §6.6 构建不回归

```
$ powershell -NoProfile -File client/build.ps1 -Target Windows64
Build succeeded                                                        exit=0
```

产物 `client/Build/Windows64/angry-chen.exe`；构建后 `client/Assets/__BuildEntryTemp` 不存在（C01 §7 的临时场景不留痕）。

## 2. §7 DoD 逐项

| # | 判据 | 实测 | 判定 |
|---|---|---|---|
| 1 | §6 六条验证全过、输出与期望逐字匹配 | §1.1–§1.6 全部 exit 0，冻结行逐字比对 | PASS |
| 2 | 角度表入库、`--check` 通过、纯文本无 NUL | 709640 B、0 个 NUL、`--check` exit 0 | PASS |
| 3 | 命令/快照/事件三类解码都有正例与截断、坏类型反例 | `codec.roundtrip`：三类各含正例 + `Truncated`/`BadType`/`BadLength`/`BadValue` 反例，断言具体枚举值 | PASS |
| 4 | 包头 `flags` 与 `type` 不符被拒（含分片例外） | `PacketReader.IsFlagsValidForType`；用例覆盖 hello/snapshot 的 0、keepalive 的 `Reliable\|AckOnly`、默认 `Reliable`、分片 `moreFragments`、多余位 0x6 | PASS |
| 5 | 幂等键跨帧生效（事件帧 + 快照内嵌事件块） | 两条路径各有「同一个 `EventIdTracker` 连解两帧 → 第二帧 `Entries=0`、`DroppedDuplicates=1`」断言 | PASS |
| 6 | `Vec3` 运算与零长度归一化有断言，逐分量除的位级结果被钉住 | `vec3.ops`：`BitEqual(3.0/5.0, unit.X)` 等 | PASS |
| 7 | §5.4 取整只有一处实现，五个量化入口都调它 | `Quantize.RoundHalfUp` 是唯一 `floor(x+0.5)`；`quantize.rounding` 钉住负半值朝 +∞ | PASS |
| 8 | 畸形输入（包头优先级、分片边界）由自造用例覆盖 | `{1,9,6,…}`/`fragCount=0`/`=9`/`fragIndex>=fragCount` → `BadValue`；6 字节包头 → `Truncated` | PASS |
| 9 | 量化往返误差上界（0.5cm / 0.0055° / 1/255） | `quantize.edge` 用 `AngleToleranceRad = 0.0055°` 与位置/血量上界断言 | PASS |
| 10 | mulberry32 与 v1 实测值逐位一致 | `rng.streams`：种子派生、`seed=1234` 三流前 3 值、`seed=42` 三流前 8 值的 IEEE754 原始位、`rngInt(0,99)` 前 6 次 `53,69,83,62,24,82` | PASS |
| 11 | `Sim/`、`Net/` 内无超越函数调用 | §1.3 扫描为空（含 `MathF`） | PASS |
| 12 | fixture 加载器重复运行输出一致，不一致时打印首个差异路径 | `fixtures.loader` 连跑两次结果相同；探针断言 `field=$.entities[0].xCm` | PASS |
| 13 | `check-docs`、`check-assets` 全绿 | §1.5 | PASS |

## 3. 施工偏差与跨链待办

1. **共用产物只读**：角度表生成脚本、`trig-table.json`、`server/tests/fixtures/*.hex` 都由服务端链交付；本份只读取与校验，不重复实现。
2. **新增共用定位实现**：`client/Assets/Scripts/Sim/RepoPaths.cs`（工程根上一级 / 退回工作目录两条候选）不在 §3 交付物表里，
   但角度表与 fixture 两个调用方都需要它，故新增并登记进 §9；测试侧也改用它，保证「只有一处定位实现」。
3. **冻结输出行的形态扩充**：C01 冻结的末行是 `SELFTEST OK`，C02 §5.7 把它扩成 `SELFTEST OK cases=<n>`；
   C01 的验收记录已加注指向本份，避免复跑者误判 FAIL。非断言异常（如角度表缺失抛的 `TrigTableException`）也统一落成
   `FAIL <caseId> expected=<异常类型> actual=<消息>` 这一种形状。
4. **命令短载荷失败码：跨链分歧，客户端按 §5.2 口径**（第二轮更正）。
   `truncated_command.hex`（12 B 包头）两侧都返回 `Truncated`——服务端在 `decodePacket` 阶段就因 `size < payloadOffset` 拒绝，
   并没有违反自家测试。**真正的分歧输入是「包头完整（20 B）+ 命令载荷 1..13 B」**：服务端 `server/src/net/codec.cpp`
   的 `payloadBytes != kCommandPayloadBytes → kBadLength` 判 `BadLength`，客户端按 §5.2「短于契约 = 帧被截断」判 `Truncated`。
   该输入目前没有任何共享 fixture；建议服务端链补一件（例：`command.hex` 前 21 字节，即 20 B 包头 + 1 B 载荷）后两侧同跑。
   客户端这一口径已在 `codec.roundtrip` 里用自造字节钉住。
5. **MatchState 缺共享字节 fixture**：S03 §5.5 的十件清单里没有 `matchstate.hex`，字段序只有自造字节自证。
   本轮补了「4 名玩家（上限）+ 12 字节名（上限）」的边界正例，但跨语言锚点仍缺，建议服务端链补一件。
6. **tracker 默认参数两侧语义不同**（第二轮新增）：S03 §5.4 要求「重复 `eventId` 必须被接收侧静默丢弃」；
   服务端 `decodeEventFrame`/`decodeSnapshot` 只有在传入 tracker 时才去重，不传就保留重复条目；客户端不传 tracker 也做帧内幂等
   （与 C02 §5.2 一致）。这是「同一批字节不同结果」，比失败码分歧更重，建议服务端链把 tracker 变成必传或补默认实例。
7. **服务端解码器在语义不变量上更松**：`recordCount`/`eventCount` 与实际字节数是否自洽、`EntityId` 是否在 `1..1024`、
   `removed` 是否升序、玩家名是否合法 UTF-8，客户端都判 `BadValue`/`BadLength`，服务端未判。同一畸形载荷可能一侧 `Ok`、一侧拒绝。
8. **分片包的 `flags` 取值**：服务端恒发 `reliable|moreFragments`(0x3) 或 `moreFragments`(0x2)（`fragment.cpp`），
   客户端两者都接受，其余组合一律 `BadValue`——已按 `wire.hpp` 的分支逐条对齐。
9. **共享字节 fixture 覆盖缺口**（第二轮新增）：10 类事件只有 `event_hit.hex` 一条锚点，type 2..10 的字段序只在客户端自证；
   两条快照 fixture 的 `eventcount` 都是 0，内嵌事件块也没有跨语言锚点。建议服务端链每种事件类型至少补一条
   （一帧多类型也可），客户端的 `codec.roundtrip` 会自动把它们纳入同一批断言。
10. **`Vec3` 的零长度语义**：零长度归一化返回零向量（逐分量除法的前置分支），与 v1 `math.ts` 及服务端 `math.hpp` 同式，
    比「返回 NaN 让调用方自己兜底」更保守。

## 4. 双轴审查

两轴都以「只读 + 冻结修订」的方式跑（第一次审首版 `be6a906`，第二次审修复后的修订），发现按 Blocker/Major/Minor/Nit 分级，
逐条处置如下。

### 4.1 第二轮 · Spec 轴

| 级别 | 发现 | 处置 |
|---|---|---|
| Major | **F1** `FixtureLoader.Compare` 的 `tick` 是死参数：`TickOfPath` 对非 `$.ticks[` 路径恒返回 0，§5.7 的「退回调用方 tick」没实现（且 `CompareAll` 比较 `expected` 子树，反推分支原本不可达） | 已修：`TickOfPath(path, fallback)`，非 ticks 路径返回 `fallback`；探针补 `tick=42` 与 `$.ticks[1].x` 两条断言（§1.4 后两行实测） |
| Major | **F2** 快照内嵌事件块与 `Decode(payload, tracker, out …)` 重载零覆盖（`eventcount` 全为 0） | 已修：新增内嵌 1 条 `playerHit` 的快照用例（字段内容 + `DroppedDuplicates`）、同一 tracker 连解两帧、事件块后尾随字节 → `BadLength` |
| Major | **F3** 本文件旧 §3.4 的跨链例子与结论错误（12 B 包头两侧都是 `Truncated`），真正分歧输入无 fixture | 已修：§3.4 重写为「20 B 包头 + 1..13 B 载荷」，并给出建议 fixture 与字节；客户端口径已由自造用例钉住 |
| Minor | F4 C01 期望串 `SELFTEST OK` 不可复现 | 已修：C01 验收记录加注指向 §5.7 的新形态 |
| Minor | F5 测试侧仍有第 4 份定位副本 | 已修：`CodecSuite.FixtureDirectory()` 改调 `RepoPaths.Locate` |
| Minor | F6 死代码（`EventCodec.Contains`） | 已修：删除；`MatchStateCodec.FixedRecordBytes` 作为公开布局常量保留 |
| Minor | F7 不传 tracker 时两侧重复事件结果不同 | 已记账：§3.6，转服务端链 |
| Minor | F8 分片 `reliable\|moreFragments` 正例、多余位反例、type 8/10 包头正例缺失 | 已修：补齐四条断言 |
| Minor | F9 探针日志断言数条数、对无关日志脆弱 | 已修：只收 `[fixture] ` 前缀并按三条精确断言 |
| Minor | F10 §6.3 门禁模式不覆盖双曲/其它超越函数与 `MathF` | 已修：计划 §6、README 与本文件同步扩模式，本轮重跑为空 |
| Nit | N1 ADR-010 允许子集未列 `floor` | 已修：ADR-010 §2 显式允许 `floor`（精确舍入，不引入超越函数） |
| Nit | N2 `FAIL` 行对非断言异常的形状不符 §5.7 | 已修：`SelfTest.Describe` 统一包装成 `expected=/actual=` |

### 4.2 第二轮 · Standards 轴

| 级别 | 发现 | 处置 |
|---|---|---|
| Major | M1 命令短载荷失败码两侧分歧（非本端缺陷） | 已记账并更正：§3.4 |
| Major | M2 10 类事件只有 1 类有共享字节 fixture | 已记账：§3.9，转服务端链 |
| Minor | m1 快照内嵌事件块零覆盖 | 已修（同 F2） |
| Minor | m2 测试侧仍有定位副本 | 已修（同 F5） |
| Minor | m3 包头校验顺序与服务端不同（同帧两侧 5 vs 1） | 已修：通用包头 8 字节整块读完再判 version/type/flags，两侧同序；§5.1 已写死优先级 |
| Minor | m4 非断言异常的 `FAIL` 行不符 §5.7 | 已修（同 N2） |
| Minor | m5 `RoundHalfUp` 未被内部复用（四处内联） | 已修：五个量化入口都调 `RoundHalfUp`，并加 `quantize.rounding` 绑定口径 |
| Minor | m6 `Encode` 不做 `switchTo` 规范化，注释自称「写端口径」 | 已修：`Encode` 同样回落非法槽位，两侧只有一条规则 |
| Minor | m7 新增的分片头校验无用例 | 已修：`fragCount=0`/`=9`/`fragIndex>=fragCount` 三条反例 |
| Minor | m8 覆盖缺口合集（`Vec3` 零用例、`TickOfPath` 正分支、MatchState 边界、`truncated_command` 恒真断言） | 已修：新增 `vec3.ops`、探针 `probe-ticks`、MatchState 4 玩家/12 字节正例；删掉恒真断言 |
| Minor | m9 ADR-009 的分片头条件「或 `fragCount > 1`」不可判定 | 已修：ADR-009 勘误 |
| Minor | m10 C01 §9 的场景归属指向过期 | 已修：改指 C07（程序化场地） |
| Nit | n1 `TrigTable.cs` 未使用的 `using UnityEngine` | 已修：删除 |
| Nit | n2 `PacketReader.Empty` 命名不符 §11 | 已修：`_empty` |
| Nit | n3 零引用成员清单 | 部分修：删 `EventCodec.Contains`；其余公开 API（`AsLong`/`ButtonFlags`/`FixedRecordBytes` 等）保留作 C03+ 的契约面 |
| Nit | n4 `matchEnded.durationMs` 复用 `ElapsedMs` 字段名 | 接受：S03 §5.4 的 union 复用本身就这样定，字节序由 fixture 与条目长度断言覆盖 |
| Nit | n5 探针日志计数脆弱 | 已修（同 F9） |
| Nit | n6 本文件自称「实测粘贴」但省略了堆栈 | 已修：抬头改为「粘关键行与摘要」 |
| Nit | n7 `ContractSuite` 的允许引用表是 C01 §5.2 的手抄副本 | 接受（C01 遗留）：机器判据本身按 asmdef 读，文档改动不会静默失效 |

### 4.3 第一轮（首版 `be6a906`）结论

两轴各发现 3 条 Major，全部当轮修复并复验：事件帧尾部字节未校验（现 `Remaining != 0 → BadLength`）、
§5.1 的 `flags`↔`type` 映射缺失（现与 `wire.hpp` 逐分支等价）、`eventId` 幂等只覆盖单帧（现共享 `EventIdTracker`）；
以及 `RepoPaths` 去重、fixture 输出行与 tick 语义、RNG 原始位锚点、冻结文档口径补齐等 Minor/Nit。
本轮 §4.1/§4.2 里标注「已修」的项即两轮合计的处置结果，未标注「已修」的只有 §4.2 的 n4、n7（接受）与 n3（部分）。

## 5. 复跑命令

```powershell
node tools/export-trig-table.mjs --check                                  # §6.1
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest-core.cmd')   # §6.2（Ac.Core.SelfTest.Run）
Get-ChildItem client/Assets/Scripts/Sim, client/Assets/Scripts/Net -Recurse -Include *.cs |
  Select-String -Pattern '(Math|MathF)\.(Sin|Cos|Tan|Sinh|Cosh|Tanh|Asin|Acos|Atan|Atan2|Exp|ExpM1|Log|Log2|Log10|Log1p|Pow|Cbrt|Hypot)'   # §6.3
& cmd.exe /c (Join-Path (Get-Location) 'client/Logs/selftest.cmd')        # §6.4（Ac.Tests.SuiteRegistry.RunAll）
node tools/check-docs.mjs; node tools/check-assets.mjs                    # §6.5
powershell -NoProfile -File client/build.ps1 -Target Windows64            # §6.6
```

两条 `.cmd` 是 `client/Logs/` 下的生成物（不入库），内容就是 §6 表格里那两条 `Tuanjie.exe -batchmode` 调用；
重新生成它们时注意用**绝对路径**调 `cmd.exe /c`，否则 GUI 子系统的编辑器不会被拉起、日志会保持旧内容。
