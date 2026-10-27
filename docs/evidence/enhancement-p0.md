# 完整增强 P0：基线复测与防假绿加固

## 范围与身份

本轮工作起点HEAD：`9a5cf1f65a22513e4f1f4069dfc185e8e25cc08b`。工作区起初已有BuildInfo.g.cs、manifest.json、ProjectSettings.asset修改，以及未跟踪的远程性能证据，不覆盖。完整批准方案见[增强计划](../enhancement-plan.md)。

本记录只验收下述小范围工作，不宣称P0全部完成或新玩法已实现。原始本地报告：`build/reports/enhancement-p0/`；构建树：`build/scratch/server-clean/`。

## 本次实际复测

| 检查 | 结果 | 原始证据 |
|---|---|---|
| 文档门 | PASS | docs.log |
| 素材/依赖门 | FAIL：用户原有cn.tuanjie.codely.bridge不在白名单；未删除或无条件放宽 | assets.log |
| 服务端重新构建、完整测试（修改前） | 561/561，exit0 | server-build.log、server-tests.log |
| 服务端修改后CTest | 2/2通过：unit与test_harness_contract；unit仍覆盖原完整用例 | server-build-after.log、ctest-after.log |
| Unity完整自检（修改前） | 267项，2失败：HUD expected42 actual0、codec expected3 actual5，exit1 | unity-selftest.log |
| 发布打包回归 | PASS | test-build.log |
| 播放器发现回归 | PASS | test-player-discovery.log |
| 远端姿态内存回放 | 13/13，非真实网络 | remote-pose-check.log |
| 羊状态与移动回归 | PASS；移动16个场景 | sheep-state-check.log、movement-check.log |
| Windows真实播放器构建 | PASS，Mono，归档哈希见下 | client-build.log |
| 三轮真实player性能门 | FAIL，保留现有阈值 | frame-bench.log、frame-runs/ |

构建stage：`20261027T051602926Z-d8458210`；归档SHA256：`d03030117ac731db9cf5670c18909986398bdb7ccb1fd7f8752e319428651d43`。构建发生在HUD/codec修复前，是修复前性能基线，不冒充修复后发布验收。

### 性能明细与测量限制

| 跑次 | frameP95Ms | 每帧分配读数 | GC0 | 判定 |
|---|---:|---:|---:|---|
| player-1 | 4.2385 | 573.44B | 0 | FAIL |
| player-2 | 3.7800 | -1（不可测） | 1 | FAIL |
| player-3 | 3.6513 | 580.2667B | 0 | FAIL |

帧P95中位约3.78ms，不能据此宣称性能达标。分配口径是GC.GetTotalMemory(false)窗口回退，不是可靠逐分配调用栈；第二轮发生GC，分配和回收不可分离。不能把-1算零，不得仅靠净堆差归因生产分配位置。当前只确认性能门未通过，分配来源仍需用可靠Profiler/调用栈定位。空场景UNVERIFIED，编辑器诊断FAIL均不替代player验收。

## 防假绿修复与红绿证据

测试接缝：公开registerCase/runAll，独立进程，未操作框架内部注册表。

1. 未匹配过滤器：修改前exit0、TESTS0/0；增加失败测试后修复为exit1并输出no tests selected。
2. 重复ID：修改前同名probe执行两次并TESTS2/2；修复为注册错误，整轮失败。
3. 容量溢出：修改前丢失第769项却可筛选probe并TESTS1/1；修复为全局注册错误，筛选不能隐藏。
4. 显式空过滤器：修改前等于全量运行；修复为empty filter并exit1。
5. 保留正常TESTS1/1 exit0和断言失败TESTS0/1 exit1两项控制。

红测报告：harness-red-empty.log、harness-red-duplicate.log、harness-red-overflow.log、harness-red-filter.log。六个进程场景汇总为一个CTest契约入口；不是六个新增AC_TEST。框架容量仍768，没有通过扩大容量掩盖溢出。已有CI执行ctest即可运行新契约，不需额外依赖。

复现：

```text
cmake --build build/scratch/server-clean
ctest --test-dir build/scratch/server-clean --output-on-failure
build/scratch/server-clean/ac_tests.exe --filter=p0_unregistered_group
```

最后一条必须exit1，TESTS0/0不是通过。

## Unity失败归因与修复范围

- HUD：GameLoop已约定他人记录不能污染本人读数，且未绑定模板暂不清弹药；FillSample却每帧清怒气。保留已确认本人怒气直到本人记录更新，在ClearIdentity会话清理时归零。不改变协议身份绑定，不按昵称兜底。扩展回归覆盖连续缺本人帧、换到另一权威身份、换传输清理。
- codec：未知类型反例使用已分配的12，改用未分配255，保留BadType断言；合法短命令包头和可靠分片正例仍硬编码协议1，改用当前协议常量。旧版本拒绝反例仍保留，解码器没有放宽。首个失败修复后才显露后续正例失败，逐次复测后两个定向场景均通过。
- client-baseline-check.ps1原先要求HEAD和工作区同样失败；改为保留HEAD证据、要求工作区两项均实际执行并通过，不把历史失败当成功标准。离线桩不替代完整Unity验收。

完整Unity修复后 **268/268、exit0**，见unity-selftest-after.log；包含新增会话清理回归。HEAD/Worktree定向对照exit0，见client-compare-after.log：HEAD保留旧失败，Worktree两项通过。对照脚本同时修正Windows PowerShell读取git中文源码的UTF-8解码，防止行结构被错误解码。

引擎产生的packages-lock.json本地包解析与GraphicsSettings.asset色温开关副作用已按本轮开始时的干净HEAD版本恢复；用户原有三个客户端修改未恢复或覆盖。

## 持续来件分配诊断与事件缓冲修复

断电后核对工作区与上一轮日志，改动和证据保留。临时诊断使用引擎ProfilerRecorder精确计数：无新来件时，纯Frame及Fx/Audio/Overlay/Draw逐段隔离100帧均0B；持续喂来件时关闭全部呈现仍约57549B/100次Tick，排除将分配一概归咎绘制。

源码确认EventCodec每包创建PacketReader、List和事件数组。新增生产OnPacket接缝精确红测：100次事件投递分配20800B。修复为GameLoop自持PacketReader与固定事件缓冲；旧DecodeFrame/DecodeEntries接口仍返回自有数组，解析规则共享，不改变协议。新增DecodeFrameInto仅成功时count有效，容量不足、截断、尾部垃圾均失败且count为0；下一次调用覆盖缓冲。

增加缓冲复用、跨包去重、无tracker不去重、短包/尾部垃圾/容量不足/null载荷/失败后恢复，以及生产坏包不投递测试。临时PlanBench诊断曾使编辑器QualitySettings的vSyncCount改变，现已与packages-lock及GraphicsSettings一并恢复原先干净版本；用户原有三个修改继续保留。临时PlanBench阶段诊断已移除，不把没有断言的诊断当常驻通过用例；精确事件分配断言保留。第一轮修复后完整Unity270/270通过；最终增加生产坏包/去重断言后仍270/270、exit0（event-final-selftest.log）。真实player已从修复后源码重新构建（stage20261027T062920843Z-67c61589；SHA256815c8b20a00092494a46cb4ec904667b365da40c90b39e479db2130a7156f967）。三轮仍FAIL：P95为3.8835/3.2739/3.2706ms；分配窗口读数为-1（GC0=1，不可测）/634.88B/628.0533B。本轮另有drawCalls197>120阻断；帧时间合格并不代表渲染预算通过。该堆窗口不能证明精确分配下降或上升，只能证明整体门未闭合；生产事件100次精确零分配断言不等同整个场景零分配。证据event-player-build.log、event-frame-bench.log、event-frame-runs/。离线对照不编译引擎分配测试，完整Unity才覆盖它们，不用桩假造精确计数器。

## 未闭合项与阶段门

依赖按用户选择保留为正式依赖并先审查，见[Codely依赖审查](codely-dependency-review.md)，未直接放行。

P0仍未全通过：分配/GC性能门；本地依赖与白名单冲突；缺少引擎CI runner实际接线；真实2/4客户端、干扰网络及差分快照恢复验收仍需进行。本轮未新增玩法、未升协议、未部署。性能阻断未闭合前不进入P1内容扩展。
