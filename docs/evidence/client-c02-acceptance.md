# C02 客户端数学、量化与协议解码 —— 验收记录

> 计划：[C02-客户端数学量化与协议解码.md](../plans-v2/client/C02-客户端数学量化与协议解码.md) ｜ 里程碑：MC02 ｜ 上游移交物：HANDOFF-C01
> 环境：团结引擎 `2022.3.62t16_ab17684acf1d`（`AC_UNITY`）、Node `v24.14.1`、Windows；工作目录为仓库根，引擎批处理一律经 `cmd /c` 代跑（见 `client/README.md`）。
> 本节所有输出为实测粘贴；复跑命令见 §5。评审（双轴）结论与处置见 §4。

## 1. §6 验证（逐条实测）

### 1.1 角度表（§6.1）

```
> node tools/export-trig-table.mjs --check
trig-table.json OK (65536 entries) sin=0x8BD9F737 atan=0x197C3A8D asin=0xAD2BD35E
exit=0
```

`docs/evidence/fixtures/trig-table.json`：709640 字节、NUL 字节 0 个、键 `scale,units,sin,atanUnits,asinUnits`。该文件与生成脚本由 S02 链交付（本份 §3 状态列已改【共用】），客户端只读取与校验形状（`scale`/`units`/三个数组长度，任一不符即抛错）。

### 1.2 核心自检（§6.2）

```
> cmd /c client/Logs/selftest-core.cmd     # -executeMethod Ac.Core.SelfTest.Run
SELFTEST START cases=2
PASS core.json.parse
PASS core.json.reject
SELFTEST OK cases=2
core exit=0
```

### 1.3 禁超越函数扫描（§6.3）

```
> Get-ChildItem client/Assets/Scripts/Sim, client/Assets/Scripts/Net -Recurse -Include *.cs |
    Select-String -Pattern 'Math\.(Sin|Cos|Tan|Exp|Log|Pow|Atan|Atan2|Asin|Acos|Hypot)'
无输出：通过
```

首次扫描命中一处**注释**（`TrigTable.cs` 里写了「禁止回落到 Math.Sin」），改写为「引擎的超越函数」后重扫为空——门禁按字面判定，注释也算命中。

### 1.4 分组自检（§6.4）

```
> cmd /c client/Logs/selftest.cmd          # -executeMethod Ac.Tests.SuiteRegistry.RunAll
SELFTEST START cases=9
PASS c01.assemblies.references / c01.project.identity / c01.project.serialization
     / c01.build.backend / c01.render.pipeline
PASS rng.streams
PASS quantize.edge
[codec] command ok / event_hit ok / fragment ok / hello ok / hello_ack ok
[codec] keepalive ok / resume ok / snapshot_diff ok / snapshot_full ok
[codec] truncated_command 按期望失败
PASS codec.roundtrip
[fixture] none
[fixture] none
[fixture] probe OK
[fixture] probe-mismatch tick=0 field=$.entities[0].xCm expected=150 actual=151
PASS fixtures.loader
SELFTEST OK cases=9
exit=0
```

`codec.roundtrip` 直读两侧共用的 `server/tests/fixtures/*.hex`（S03 §5.5），把每条 `# expect:` 的键逐一断言掉并断言「没有剩余未断言的键」——fixture 新增字段时本端立刻失败而不是静默跳过。`[fixture] probe*` 两行是探针：`probe-mismatch` 故意扰动一个数字，用来逐字验证 §5.7 冻结的不一致行格式（探针名带前缀，避免与真 fixture 混淆）。

### 1.5 仓库质量门（§6.5）

```
> node tools/check-docs.mjs
链接检查：扫描 51 个文档，解析相对链接 156 条
OK：v2 30 份计划（S/C 链） + 10 份前置文档，线性链与链接校验通过。   exit=0

> node tools/check-assets.mjs
素材扫描：170 个受控文件，二进制嗅探 170 个，零素材类扩展名
Unity 依赖：34 个，全部在官方白名单内
OK：仓库零外部素材，依赖白名单未被破坏。   exit=0
```

`check-assets` 的受控文件数在并行会话往 `server/` 增文件时会继续上涨；本份客户端新增 19 个 `.cs` 及其 `.meta`。

### 1.6 构建不回归（§6.6，本份新增）

```
> powershell -NoProfile -File client/build.ps1 -Target Windows64
[build] target=Windows64 project=D:\projects\tmp\angry-chen\client
Build succeeded
build exit=0
exe=client/Build/Windows64/angry-chen.exe 464384 B，临时场景目录已清理
```

增量构建：产物字节与 MC01 一致，故 exe 的 mtime 未变，但引擎日志里 `ProducePlayerScriptAssemblies`、`Build scripts DLLs` 已对本份新增程序集重跑。

## 2. DoD（§7）

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| 1 | §6 六条验证全部通过且输出逐字匹配 | PASS | §1.1–§1.6 |
| 2 | `trig-table.json` 入库、`--check` 通过、纯文本无 NUL | PASS | §1.1（0 个 NUL，709640 B） |
| 3 | 命令/快照/事件三类解码有正例与截断、坏类型反例，反例返回 §5.7 枚举 | PASS | `codec.roundtrip`：10 条共享 fixture 正例 + `truncated_command`(Truncated)、包头 `BadVersion`/`BadType`;命令 13B→Truncated、15B→BadLength、null→Truncated；事件未知类型→UnknownEvent、条目截断→Truncated、帧尾随字节→BadLength；快照截断→Truncated、尾随→BadLength、recordCount=129→BadValue、Id=0→BadValue、removedId 0/乱序→BadValue；MatchState count=5/名称 13B/名称 0B/weapon=3/非法 UTF-8→BadValue、尾随→BadLength、截断→Truncated |
| 4 | 量化往返误差上界：位置 ≤0.5cm、角度 ≤0.0055°、血量 ≤1/255 | PASS | `quantize.edge`：位置 0.005、角度 `0.0055 * π / 180`、血量 1/255 三条上界断言（各配量化幂等断言） |
| 5 | mulberry32 前 8 个输出与 v1 实测值逐位一致，期望值写在用例里 | PASS | `rng.streams`：seed=1234 三流前 3 个 double 的原始位；seed=42 三流各前 8 个值的原始位（Node `DataView` 读 v1 `rng.ts` 实测后抄入） |
| 6 | Sim/Net 内无超越函数调用 | PASS | §1.3 |
| 7 | fixture 加载器重复运行输出一致、不一致时打印首个差异字段路径 | PASS | `fixtures.loader` 连续两次 `CompareAll` 断言一致；探针断言路径 `$.entities[0].xCm`，并逐字校验 §5.7 的两条输出行 |
| 8 | `check-docs`/`check-assets` 全绿 | PASS | §1.5 |
| 9 | 包头 `flags` 与 `type` 不符的帧被拒（含分片例外） | PASS | `codec.roundtrip`：Snapshot+reliable、Command 无 flags、Fragment 无 moreFragments、KeepAlive 仅 reliable → `BadValue`；KeepAlive 合法 flags 但包体截断 → `Truncated` |
| 10 | 事件幂等键跨帧生效 | PASS | `codec.roundtrip`：同一 `EventIdTracker` 下第二帧重复 eventId 被丢弃（`Entries=0`、`DroppedDuplicates=1`、`MaxEventId=7`） |

## 3. 偏离与判断

1. **共享角度表不重复实现**：`tools/export-trig-table.mjs` 与 `docs/evidence/fixtures/trig-table.json` 由 S02 链交付（ADR-010 要求两侧只有一份），§3 两行状态改【共用】、§4 任务 3 由「写脚本」改为「校验脚本」。
2. **`Ac.Core.SelfTest.Run` 只覆盖 Core 层**：C01 §5.2 冻结 `Ac.Core` 的 `references` 为空，故核心自检只能跑受限 JSON 两条用例（`cases=2`）；量化/解码/随机数三组在 `Ac.Tests.SuiteRegistry.RunAll` 下跑（`cases=9`）。§6.2 与 §9 已按此改写。
3. **输出格式的补充**：§5.7 在 C01 §9 冻结的 `PASS`/`FAIL`/`SELFTEST OK` 形状上补了 `cases=<n>`/`failures=<n>` 计数，两个入口共用 `Ac.Core.SelfTest`；C01 的验收记录按「历史记录不改写」保留原样，只在 C01 §9 移交表加了一句指向 C02。
4. **命令载荷长度语义（跨链待收敛）**：本端实现「短于 14B → `Truncated`、长于 14B → `BadLength`」，与共享 fixture `truncated_command.hex`（`failure=0x1`）一致；但**已提交**的服务端 `server/src/net/codec.cpp` 对 `payloadBytes != kCommandPayloadBytes` 一律返回 `BadLength`（该文件自家测试 `server/tests/codec_test.cpp` 与 fixture 期望 `Truncated`）。同一批字节（12B 包头 + 0B 载荷）两侧得 1 vs 4。本端不改：fixture 是两侧唯一的字节真相，且本端与服务端测试一致；请服务端会话在 S03/S04 里收敛（否则两侧对同一畸形帧给出不同 `failure`）。§5.2 已补写该口径。
5. **`wrap` 的口径**：实现取服务端 `wrapAngle`（落在 `(-π, π]`、负整圈保留 `-0.0`），与 §5.4 原写的 `a - floor(a/Tau)*Tau`（`[0, 2π)`）在 `% 65536` 之后逐位等价（评审用 1.2 万–30 万组随机样本复核，零差异）；§5.4 已改写为服务端口径。
6. **客户端解码比服务端严**：`recordCount ≤128`、`eventCount ≤64`、`EntityId 1..1024`（含 removed 列表）、`removed 升序`、`count ≤4`、MatchState 严格 UTF-8 在客户端判 `BadValue`，服务端解码侧不做这些语义检查。同一畸形帧两侧 `failure` 可能不同，正例帧逐字节一致——从严只影响契约禁止的帧。
7. **跨链 fixture 与本份边界**：客户端测试额外读 `server/tests/fixtures/*.hex` 并断言每条 `# expect:`（S03 §5.5「两侧读同一批文件」），这不是 §3 的交付物而是跨链门禁；`hello`/`hello_ack`/`resume` 的载荷字段由测试内联读取，语义解码属 C03。
8. **C02 尚无模拟器，fixture 比较是自比**：`CompareAll` 用 fixture 自身的 `expected` 当 actual，验证「发现 → 解析 → 逐位比较 → 首个差异定位」这条管线；`docs/evidence/fixtures/` 目前只有非 fixture 形状的 `trig-table.json`，因此打印 `[fixture] none`（§8 风险行预期的行为）。C06 起把 actualOf 换成模拟器输出即可。
9. **MatchState 没有共享字节 fixture（跨链缺口）**：S03 §5.5 的十件清单里没有 `matchstate.hex`，本端字段序目前只由自造字节自证。请服务端链在 S03/S09 追加该 fixture（含 `# expect:` 键），本端会自动纳入 `codec.roundtrip` 的同一批断言。
10. **角度表在构建产物里的打包方式未定**：`TrigTable.DefaultPath()` 经 `RepoPaths` 先找「工程根上一级 + docs/evidence/fixtures/」，再退回工作目录；本份只保证「加载失败即抛错、不回落超越函数」，玩家包怎么带表留给 C15。
11. **`.meta` 的 guid 是 88 字符 base64 而非 Unity 的 32 位十六进制**：团结引擎自己写出的格式（42/42 一致，多次批处理未重写，程序集引用走程序集名而非 guid），属引擎约定，不是本份引入的问题。

## 4. 双轴评审与处置

两轴各自独立跑（Standards + Spec，只读、基线 `d1ed853`），结论并列如下。

### 4.1 Standards 轴

| 编号 | 发现 | 处置 |
|---|---|---|
| M1 | `EventCodec.DecodeFrame` 不校验载荷尾部，坏帧被当 `Ok`，与另外三个解码器不对称 | 已修：解码条目块后 `Remaining != 0 → BadLength`；新增用例「事件帧尾随字节 → BadLength」 |
| m2 | 通过行多打 `tick=`（与 §5.7 冻结格式不符），且 `tick` 取自差异路径的第一个下标（那是实体序号） | 已修：通过行改为 `[fixture] <name> OK`；`TickOfPath` 只在 `$.ticks[i]` 上反推，其余退回调用方 tick；探针逐字断言两条冻结行 |
| m3 | `TrigTable.Load` 校验 `units` 与三个数组长度，却不校验 `scale`（幅值硬编码） | 已修：校验 `scale == 1073741824`；新增 4 条失败分支用例（scale/units/长度/文件缺失） |
| m4 | §5.4 冻结的 `wrap` 区间与实现不同 | 已修：§5.4 改写为服务端口径并写明 `% 65536` 后等价 |
| m5 | `RngSuite` 里「fx 不影响 ai」是同义反复，实际没测到 | 已修：改成「交错消费 fx 与 ai 各 8 次，ai 序列必须与干净跑逐位相同」 |
| m6 | 探针在通过的运行里打印伪失败行 | 已部分修：探针保留（这是验证 §5.7 不一致行格式的唯一办法），但用 `Application.logMessageReceived` 逐字断言输出行，且探针名带 `probe-` 前缀；真 fixture 行不受影响 |
| m7 | 「工程根上一级找仓库根」有三份重复实现 | 已修：新增 `Sim/RepoPaths.cs`，`TrigTable`、`FixtureLoader` 与测试共用 |
| m8 | `EventCodec.EntrySize` 的字节数表与字段读取重复知识，客户端只用它判未知类型 | 已修：换成 `IsKnownEvent`，字节数表留在服务端与 §5.2 |
| m9 | `SuiteRegistry` 注释说「注册全部用例分组」，实际不覆盖 Core 层 | 已修：注释与 §9 口径一致（Core 层由 `Ac.Core.SelfTest.Run` 跑） |
| n1 | `CodecSuite.CheckSharedFixture` 137 行、四种载荷混在一个 switch | 未改：拆分只改善可读性，收益不抵 137 行测试的重排风险；本条记录在此，下一份计划若继续扩这条链再拆 |
| n2 | `Quantize` 注释的银行家舍入例子自相矛盾；`TrigTable` 注释里出现 C++ 命名 `kAsinUnits` | 已修：例子改 2.5/3.5，命名改 `asinUnits` |
| n3 | `removedIds` 未套 `MaxEntityId`，与 records 一侧不对称 | 已修：`removed[i] < 1 \|\| > MaxEntityId → BadValue`；新增 `removedId=0 → BadValue` 用例 |
| n4 | 每次加载都完整解析 709 KB 的 `trig-table.json` 再按形状丢弃 | 未改（接受）：fixtures 目录里非 fixture 文件目前只有它一份，测试内耗时无感；按形状判定比按文件名/子串启发式更不容易误判 |
| n5 | `.meta` 的 guid 是 88 字符 base64 | 非缺陷：引擎自身格式（见 §3.11） |

### 4.2 Spec 轴

| 编号 | 发现 | 处置 |
|---|---|---|
| M1 | 命令短载荷失败码两侧分歧 | 见 §3.4：本端按共享 fixture 保持一致，转服务端会话收敛；证据原文「S03 尚未提交」已更正（S03 = `a90e3f6`、S04 = `42212d6` 均已提交） |
| M2 | 未实现 §5.1 的 `flags`↔`type` 映射，畸形帧可能被解成 `Ok` | 已修：`PacketHeader.Read` 加 §5.1 映射（含分片例外），5 条新用例；§5.1 正文补写映射与 `type 10` |
| M3 | `eventId` 幂等只覆盖单帧，无跨帧归属 | 已修：新增 `Net/EventIdTracker.cs`（严格递增，镜像服务端 `codec.hpp`），`EventCodec.DecodeFrame`/`SnapshotCodec.Decode` 增 tracker 重载，§9 写明会话层从 C03 起持有 |
| m1 | §5.4 `wrap` 区间与实现不符 | 已修（同 Standards m4） |
| m2 | §5.7 通过行多了 `tick=` | 已修（同 Standards m2） |
| m3 | 客户端比服务端解码更严，畸形帧无 fixture 覆盖 | 保留从严并补上 removed 上限（见 §3.6）；共享 fixture 由服务端链扩充 |
| m4 | 验收记录数字过期（角度上界、`.cs` 计数、文档/链接数、`[fixture] none` 行数） | 已修：本文件全部重测（0.0055°、19 个新 `.cs`、51/156、两行 `[fixture] none`） |
| m5 | MatchState 无共享 fixture | 无法在本份补（fixture 归服务端链），记为跨链待办（见 §3.9） |
| m6 | `scale` 未校验、`TrigTable` 失败分支无用例 | 已修（同 Standards m3） |
| m7 | §7 仍写「§6 的 5 条验证」 | 已修：改 6 条，并补两条新判据（flags 映射、跨帧幂等） |
| n1 | §5.1 的 type 码表漏 `10` | 已修 |
| n2 | §6.1 期望串只是实际输出的前缀 | 已修：写成整行（含三个 CRC32C） |
| n3 | `PacketReader(null)` 抛异常，与 §9「不抛异常」冲突 | 已修：null 按空缓冲处理；新增 `CommandCodec.Decode(null) → Truncated` 用例 |
| n4 | v1 锚点用 12 位小数，不是原始位 | 已修：改用 Node `DataView` 读 v1 `rng.ts` 的 IEEE754 原始位（三流各前 8 个），`To12` 助手删除 |
| n5 | §3「无 surrogate」措辞歧义 | 已修：写明「不支持落单 surrogate，成对代理项可用」 |

## 5. 复跑命令

```powershell
# §6.1 角度表
node tools/export-trig-table.mjs --check

# §6.2 核心自检（编辑器批处理；用 cmd /c 代跑以取退出码）
$unity = $env:AC_UNITY; if (-not $unity) { $unity = (Get-ItemProperty 'HKCU:\Environment' -Name 'AC_UNITY').AC_UNITY }
$bat = Join-Path (Get-Location) 'client/Logs/selftest-core.cmd'
Set-Content -Path $bat -Encoding ASCII -Value ('"' + $unity + '" -batchmode -quit -nographics -projectPath client -executeMethod Ac.Core.SelfTest.Run -logFile client/Logs/selftest-core.log > client/Logs/selftest-core.stdout.log 2>&1')
& cmd.exe /c $bat; "exit=$LASTEXITCODE"

# §6.3 禁超越函数扫描（期望无输出）
Get-ChildItem client/Assets/Scripts/Sim, client/Assets/Scripts/Net -Recurse -Include *.cs |
  Select-String -Pattern 'Math\.(Sin|Cos|Tan|Exp|Log|Pow|Atan|Atan2|Asin|Acos|Hypot)'

# §6.4 分组自检：把上面的 -executeMethod 换成 Ac.Tests.SuiteRegistry.RunAll（日志 client/Logs/selftest.log）
# §6.5 仓库门禁
node tools/check-docs.mjs; node tools/check-assets.mjs

# §6.6 构建
powershell -NoProfile -File client/build.ps1 -Target Windows64
```

日志落点：`client/Logs/selftest.log`、`selftest-core.log`、`build.log`（均为 gitignore 下的生成物）。
