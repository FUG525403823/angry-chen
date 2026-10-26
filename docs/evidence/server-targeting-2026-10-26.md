# 服务端选敌兼容性检查（2026-10-26）

## 当前结论：行为迁移已批准，fixture已通过，全量验收仍非全绿

2026-10-26 人类明确选择“版本化更新受影响向量（推荐）”。10条旧JSON逐字节保留于`fixtures/legacy/targeting-v1/`，附旧实现；活动10条由真实新服务端重建，另外4条不变。`fixture_`21/21，幂等10/10；完整CTest实际运行失败，不能宣称全绿。客户端验证由主代理同步记录。

下列“实际命令与结果”至“具体契约冲突与下一步”保留**人类批准前**的红绿证据，不是最终状态。批准后结果、命令、来源与SHA256见末节。本子任务不提交、不修改client、gate或CI。

## 实际命令与结果

复用现有 Ninja + MinGW Debug 构建，不清理、不重新配置：

```powershell
$cmake = 'D:/tools/Visualstudio/software/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
& $cmake --build server/build --target ac_tests
& ./server/build/ac_tests.exe --filter=ai_
New-Item -ItemType Directory -Force server/build/targeting-temp | Out-Null
$env:TEMP = (Resolve-Path server/build/targeting-temp).Path
$env:TMP = $env:TEMP
& ./server/build/ac_tests.exe --filter=fixture_
& ./server/build/ac_tests-targeting-before.exe --filter=fixture_
```

- `ai_`：**36/36，exit 0**，600 tick/60 羊零分配。
- 当前 `fixture_`：**11/21，exit 10**，恰为 10 条 hashChain 回归。
- 旧目标对象 `fixture_`：**21/21，exit 0**。
- 原报告中的 `temp dir not ready` 可通过进程局部 TEMP/TMP 指向构建目录消除；不是必须修改测试或跳过的用例。
- 本轮未重跑完整 CTest；先前完整运行曾因 runtime/store 临时目录问题中止，不能作为当前全量结论。

日志：`server/build/targeting-ai-solo.log`、`targeting-fixtures-solo.log`、`targeting-fixtures-before-temp.log`。构建目录不入库。

## 零／单玩家红绿回归与修复

新增 `ai_target_single_player_preserves_legacy_distance_and_retention`，真实 API + `stepWorld` 同时覆盖：正常距离逐位一致；目标超视距仍续期；零玩家列表时旧目标续期；玩家死亡后 ID 归零但旧距离不清零；羊继续沿原 alert -> chase 状态路径。

新回归在生产修改前 **0/1**（超视距旧目标丢失），修复后 **1/1**。用当前测试对象优先链接旧 `targeting-before.obj` 得到 `ac_tests-targeting-before-solo.exe`，同一回归 **1/1**。

实现仅在 `playerCount > 1` 使用新统一评分；零／单玩家恢复旧 bestId、aggro 门槛、缓存距离初始化和当前目标刷新分支，不引入新状态/协议字段。多人失效列表测试继续覆盖两个槽位（含不存在槽位），零／单玩家失效行为单独断言，不跳过测试。

## 实际逐 tick 差异，不只看 hash

在构建目录生成临时 `targeting-probe-fixture.cpp`，源自现有 fixture replay，仅在每次 stepWorld/导演更新后输出羊的 ID、targetId、targetDistanceM、state、pos。分别链接当前库与旧目标对象，运行完整 `fixture_`：

```powershell
$objects = Get-ChildItem server/build/CMakeFiles/ac_tests.dir/tests/*.obj |
  Where-Object Name -ne 'fixture_test.cpp.obj' | ForEach-Object FullName
# 探针按原测试同样的 include/fixture define 编译为 targeting-probe-fixture.obj。
& 'C:/Program Files/mingw64/bin/c++.exe' -O0 -g @objects server/build/targeting-probe-fixture.obj server/build/libac_core.a -lws2_32 -o server/build/ac_tests-targeting-probe.exe
& 'C:/Program Files/mingw64/bin/c++.exe' -O0 -g @objects server/build/targeting-probe-fixture.obj server/build/targeting-before.obj server/build/libac_core.a -lws2_32 -o server/build/ac_tests-targeting-probe-before.exe
& ./server/build/ac_tests-targeting-probe.exe --filter=fixture_ > server/build/targeting-ticks-after.log
& ./server/build/ac_tests-targeting-probe-before.exe --filter=fixture_ > server/build/targeting-ticks-before.log
```

此探针在零／单玩家修复前运行；10 条向量均传入四人列表，修复后完整 fixture 再跑确认相同 hash 差异。探针只在构建目录，不污染测试源码。

| fixture | 首个内部差异 tick / 羊 ID | 旧 target -> 新 target | 首个 hash 差异 tick |
|---|---|---|---:|
| rifle-burst-hit-120t | 2 / 6 | 2 -> 1 | 8 |
| shotgun-spread-60t | 5 / 6 | 2 -> 1 | 9 |
| downed-revive-140t | 2 / 5 | 2 -> 1 | 8 |
| sheep-grunt-ai-600t | 61 / 6 | 2 -> 1 | 61 |
| sheep-ram-charge-300t | 66 / 5 | 2 -> 1 | 92 |
| sheep-elite-bolt-300t | 2 / 6 | 3 -> 4 | 8 |
| sheep-king-phases-900t | 2 / 5 | 3 -> 4 | 8 |
| wave-director-1to5-1200t | 3 / 6 | 2 -> 1 | 9 |
| snapshot-roundtrip-240t | 5 / 6 | 2 -> 1 | 9 |
| rng-streams-600t | 2 / 5 | 3 -> 4 | 8 |

具体因果：rifle 羊6 tick1 两侧 target1、distance=10.062305898749054、state=1、pos=(-9,-2) 完全相同。tick2 旧规则当前仇恨为零，门槛为零，改选 player2，distance=11.889096895895834；新规则保留 player1，distance=10.062305898749054。tick3 旧规则又选回1，但后访问当前player2覆写 distance=12.064514080558736，出现旧实现 ID/距离不对应；新规则仍正确配对。tick8 alert结束，旧位置=(-8.9202828701649999,-1.8875988469326503)，新位置=(-8.9383739665401052,-1.8767479330802117)，产生首个投影/hash差异。

其他确认：downed tick8 旧state=2，新state=5；elite/king tick2已有目标不同但同距离，tick8移动方向才分叉。ram tick66已有目标差异，tick92进入state3时目标不同，虽位置相同仍出现投影/hash差异。因此并非仅“无目标距离清零”导致，也不能只恢复距离便让多人稳定与全部冻结投影兼容。

## 具体契约冲突与下一步

`fixture_test.cpp::createFixtureWorld` 按 v1 createWorld 在四个出生点生成 id1..4，HP=100、active；`rifle-burst-hit-120t.json` 的 script 明确驱动四个玩家。`stepWorld::collectPlayerIds` 也把它们全部送入真实选敌调用。这些不是单玩家场景。

冻结向量记录了四人零仇恨旧交替选敌的逐 tick 位置/状态/事件；多人稳定修正恰好要消除同一输入上的交替。`selectTarget` 不具有区分“fixture四人”和“真实四人”的模式参数。维持全部旧 hash 与改变相同输入行为不可同时满足，不能通过识别种子/名称、改冻结向量、跳过用例或伪造报告解决。

需人类决定撤回多人行为修正，或独立批准版本化规则/对拍契约。当前保留单玩家兼容修复与多人提案供审查；ADR 标记待决策／受阻。没有全绿或完成声明。

## 批准后的版本化迁移与验证

### 生成机制与审查边界

人类批准targeting-v2；不再要求新的多人稳定选敌逐tick等于旧规则。旧10份原字节归档，不用修改configHash伪装来源（常量相同，仍为19a978ea），schema仍2。新规则来源是工作树`server/src/ai/targeting.cpp`，初态、脚本及采样tick均来自旧JSON。独立C++导出器复用原测试world/setup/投影helper，调用真实stepWorld与外部导演，输出每tick链与必要全量字段；快照调用真实encode/decode。JS仅组装已有采样tick的数据并复用原serializer；没有手写hash，没有测试重录开关。新工具只用于显式离线生成，未接gate/CI。

复现命令（仓库根；实际执行，构建目录现有MinGW Debug ac_core）：

```powershell
& 'C:/Program Files/mingw64/bin/c++.exe' -std=c++20 -O0 -g -Wall -Wextra -Werror -ffp-contract=off -fno-fast-math -I server/src -I server/tests server/tools/export-targeting-fixtures.cpp server/tests/fixture_io.cpp server/build/libac_core.a -lws2_32 -o server/build/ac_export_targeting.exe
node tools/update-targeting-fixtures.mjs > server/build/targeting-export.log
node tools/update-targeting-fixtures.mjs --check > server/build/targeting-export-check.log
$env:TEMP = (Resolve-Path server/build/targeting-temp).Path
$env:TMP = $env:TEMP
& ./server/build/ac_tests.exe --filter=ai_ > server/build/targeting-ai-v2.log
& ./server/build/ac_tests.exe --filter=fixture_ > server/build/targeting-fixtures-v2.log
& 'D:/tools/Visualstudio/software/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe' --test-dir server/build --output-on-failure > server/build/targeting-ctest-v2.log
```

| 检查 | 真实结果 |
|---|---|
| 离线C++ exporter编译 | exit0，使用-Werror与确定性浮点开关 |
| 首次生成 / 再生成--check | 10/10 exit0 / 10/10逐字节一致 exit0 |
| ai_ | 36/36 exit0；600tick/60羊 allocations=0 |
| fixture_（原比较与负向探针不变） | 21/21 exit0 |
| 完整CTest | exit8，0/1 unit，14.32秒；进程0xc0000409中止，无完整总用例计数 |
| 两个mjs的node --check | 均exit0 |

完整CTest中11个runtime.start断言失败，随后`runtime_grace_disconnect_and_same_client_resume`后`std::vector<MatchStatePlayer>::operator[]`断言`__n < size()`中止。数据目录在runtime_test.cpp中为相对`ac-runtime-test-data`，端口均为0（系统分配）；本次已设置TEMP/TMP，不能将这次失败归因于fixture临时目录。未通过改runtime、跳过测试或降低gate/CI来掩盖。失败日志为`server/build/targeting-ctest-v2.log`及`server/build/Testing/Temporary/LastTest.log`。本任务范围只记录失败并通知主代理。

### 新向量审查

- 10条首个hash差异tick与批准前探针完全对应（8/9/8/61/92/8/8/9/9/8），并非整体无差别重录。
- setup、seed、script、ticks、关键帧tick与snapshot tick未变；14条中4条无羊的旧字节未动。活动14份273702 B，最大59115 B，原64KiB/份与512KiB/批门不变。
- 快照roundtrip新encodeHash/decodeHash由真实15字节记录编码/解码计算，测试仍逐字段、逐链比较；原4个tamper探针与FNV自检继续通过。
- 旧选敌实现`legacy/targeting-v1/targeting.cpp.txt` SHA256为`31d9fb6f56e8610fd0c4adb24426eccc1eb2a60ace7205226c21f36dc320344c`。忽略BOM与EOL后等于提交`9aea70b1ad39f1ab7c243d62354471490e7b5f23`的targeting.cpp；旧实现配旧向量21/21的迁移前日志保留在上文。旧二进制在build中，不提交机器产物。
- C# `FixtureSuite`是schema/关键帧自比及反向探针，不运行权威羊AI。`FixturePredictSuite`只有本机玩家LocalStep运动学，5份重放/9份既有不可重放清单不变；elite玩家1的末帧HP由66.40000000000003变77.60000000000002，但所有位置/朝向不变，Compare仅比pos/yaw/pitch。本次没有修改client实现或fixture测试，也不声称C#重算羊AI/hash。

### SHA256（旧档案 / 活动向量）

以下由生成工具计算；不是手工构造hashChain。

| fixture | 首个hash差异tick | legacy SHA256 | targeting-v2 SHA256 | 新末帧链 |
|---|---:|---|---|---|
| rifle-burst-hit-120t | 8 | `ac5f422508726088a4f9013a652b043ef2100926f519cb6534b94a75d925c51a` | `f131f65075a06d6641c2f08a6479714f57d5e165bcb335cd4cfdaed9a4020ca4` | `432bfbac902cffb4` |
| shotgun-spread-60t | 9 | `f5fdb6b41b23f00b55dc439553a32c2e88706190299315bc1c1a9d604875bdf3` | `8441112559d6e3efc07fd68363fbbf7f0d4ce941d7c5d7cbd6725badddaed3a7` | `a3bbd237f2ca3509` |
| downed-revive-140t | 8 | `2ec9c8b44f36e6f2fb092ffb4f166251bb2e4968c802ebdac6171b00e36cbe3e` | `c68e2d1b6de6b004eec4e78ee7c47166baae883017036c47eba5a2751879036c` | `f5ef9334f1a081a6` |
| sheep-grunt-ai-600t | 61 | `3927c73aa2f27744fc7c668de893f7e1c9f0cc91d565b0b9aea9e5bfff34c43d` | `0f472829a261c24bc24c5c2549c4798f5cb88a3e24b8d31e1947a0834327227c` | `acc056c5ac4aa5a1` |
| sheep-ram-charge-300t | 92 | `1fe841260619001d50b2fee8a8aa4cb147b414f74ad58aee76a2f5ffa0327074` | `04bbb47907e3f600655ce7c63ef5c6b4938e7f1c75338b08da1e2c448b480d96` | `cd831f374fe66ad6` |
| sheep-elite-bolt-300t | 8 | `1b0ca0c9b6aa3ee09897d578743b0347c0bc5e03bf8135a70a6eac45c12664c2` | `230b3c9cbb3aaa767dff0d23e754dad02c7c9ae4ba7844880332d28db8a425b6` | `606ee5a25e202e5a` |
| sheep-king-phases-900t | 8 | `1a70ae3fe70e26f2dcf23c1e4af40cf9cec2d20b5479d8c264fd1ed866369982` | `b0b75268db60301491f62875b9e742574f7d1d284601e2f21ec2df64975aae54` | `b0279a71352cd702` |
| wave-director-1to5-1200t | 9 | `53d67d722f913d96c4f98f3bccce142f3492cdc963b617c4bc8485f471050f43` | `10a365086663069638e4cfcffd57bc7935ba26163c38a12c83e901a42b01a425` | `797c56cad342397c` |
| snapshot-roundtrip-240t | 9 | `7f5bdb9f3eaa49faee874f871069c9694aa4053169a2e3f4e339363df92f99a9` | `aabbe5b2d1150aa2a911ea9742d22dbf5fdc0f257c2708dacc77192f0af9907e` | `445822f6d1ba6792` |
| rng-streams-600t | 8 | `e67e3b82c43eeaac4a039bf5ae221e355e38750f329d711c75f9ec932a05d409` | `a62ffec95d455b30e3ac74230e17d6d0fd8f74c6402da9f1750df4f50e6f2853` | `31ee269029ddd47e` |

生成时来源SHA256（原始字节）：

| 来源 | SHA256 |
|---|---|
| server/src/ai/targeting.cpp | `404fb9e5a888d2dac921b276a915934337f902c059a34eb5dea4dcfdda08f92b` |
| server/build/libac_core.a | `c5bfc5666d78346b6a775f76dcdab3d3ef6deb27b52910312b77b781d67f5faa` |
| server/build/ac_export_targeting.exe | `23150e5883811fa90f4e1f20f67f9eb8628c61b6c3a934393639c98ad4dcfb4f` |
| server/build/ac_tests.exe | `5d9f0d03d8bfbfff47ae6675c4be42c8d484a05884cc7c687965b289f7d10932` |

另实测`node tools/export-fixtures.mjs --check`以exit1明确拒绝覆盖/比较targeting-v2活动目录（`targeting-frozen-guard.log`）；这属于预期保护测试，不是新向量失败。两个mjs `node --check` exit0，`git diff --check`针对本任务文件exit0。

程序化不可变输入审计：10/10通过；逐份deepStrictEqual比较name/version/seed/dtMs/configHash/ticks/setup/director/script、关键帧tick及快照tick，链长等于ticks。客户端实际运行结果待主代理补录；此处不把源码范围核实当作实测。

## 主代理最终补充验证

最终Unity完整自检267项：265通过，仅boot.hud_ammo_rage_from_match_state与codec.roundtrip两项失败，独立HEAD对照证实既有；新版14份fixture shape与5份玩家运动重放及9份既有理由均未报告失败。这不等于C#重演羊AI。随后全新build/scratch/server-clean显式MinGW15.2构建ac_tests/ac_server/ac_gate/ac_bot，完整CTest底层561/561通过，耗时10.58s，未复现旧build的cannot-open/vector断言。日志与工具链来源见build/scratch/server-clean/evidence.md。总体详见multiplayer-remediation-2026-10-26.md。
