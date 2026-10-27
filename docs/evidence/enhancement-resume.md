# 增强实施接续：P0 小包验收

## 边界与结论

本轮读取已批准增强计划及两份P0/依赖审查证据，不重新访谈。起点HEAD `9a5cf1f65a22513e4f1f4069dfc185e8e25cc08b`。当前会话Agent Teams工具与Dispatch模式不兼容，经用户选择，本次改为独立执行，不启动子agent；因此不声称完成子agent实施/独立agent复核。既有修改保留，未推送、未部署、未更新公网服务器。

**P0未闭合，P1–P4未开始。** 依赖合规、真实player性能、引擎CI实际运行与真实2/4客户端/干扰网络/差分快照恢复仍为阻断。批准计划第9节要求阻断闭合才进入P1，不能将本小包通过等同P0通过。

## 本小包实现

`tools/check-assets.mjs`在拒绝Codely的同时错误打印“全部在官方白名单内”。新增子进程契约先红，修复摘要为总数/白名单内/白名单外实际数量，白名单正则及exit1规则不变。`tools/test-check-assets.mjs`在独立临时Git工作区覆盖0/1/2个未知依赖，测试不会修改真实manifest；测试后删除其自己创建的临时目录。CI quality job先执行该契约再执行真实门。

红测：`node tools/test-check-assets.mjs` exit1；修复后同命令exit0，三个fixture均通过。真实依赖门仍exit1，35个依赖中34个白名单内、1个白名单外：`cn.tuanjie.codely.bridge`。本修复不构成对该依赖的接纳。

## 本轮实际执行

报告目录：`build/reports/enhancement-resume/`（本地产物，不作为版本控制内容）。

| 命令/检查 | 结果 | 日志 |
|---|---|---|
| `node tools/check-docs.mjs` | exit0，最终复测亦exit0 | docs.log / docs-final.log |
| `node tools/test-check-assets.mjs` 修改前 | exit1，摘要契约失败 | assets-summary-red.log |
| 同命令修改后 | exit0，0/1/2未知依赖三个fixture | assets-summary-green.log |
| `node tools/check-assets.mjs` | exit1，未放宽依赖门 | assets-before.log / assets-after.log |
| `cmake --build build/scratch/server-clean` | exit0，ninja无待构建项 | server-build.log |
| `ctest --test-dir build/scratch/server-clean --output-on-failure` | exit0，unit及harness契约2/2 | ctest.log |
| `build/scratch/server-clean/ac_tests.exe` | exit0，561/561 | server-tests.log |
| `powershell -File client/tools/selftest.ps1` | exit0，270/270；非联合验收 | unity-selftest.log |
| `powershell -File client/tools/test-build.ps1` | exit0 | test-build.log |
| `powershell -File client/tools/test-player-discovery.ps1` | exit0 | test-player-discovery.log |
| `git diff --check` | exit0（LF/CRLF警告非测试失败） | 控制台 |

CMake实际位于`D:/tools/Visualstudio/software/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/`，不是当前PATH命令。Unity入口解析为`D:/tools/unity/unity_install/2022.3.62t16/Editor/Tuanjie.exe`。

## 已有stage真实player性能复测

```text
powershell -NoProfile -ExecutionPolicy Bypass -File client/tools/frame-bench.ps1 -Mechanism player -Player client/Build/Windows64/stage/20261027T062920843Z-67c61589/angry-chen.exe -Runs 3 -Out build/reports/enhancement-resume/frame-bench.json -OutDir build/reports/enhancement-resume/frame-runs
```

exit1。P95为4.2464/4.0264/3.8425ms；三轮分配均为-1（不可测），摘要GC0=1、drawCalls=188>120。P95中位4.0264ms不代表达标。日志frame-bench.log、frame-runs/。该播放器为此前stage，**本轮没有重构建客户端，不冒充当前工作区发布验收**。exe SHA256 `3936cf9527601ec080b59762f8e454b35ce46c194602bc04b4e4e2ff19b7623e`；不是归档哈希。

采样源码核对：FrameBenchPlan.cs的来件投递发生在分配窗口开始前（FeedSnapshot/FeedEvents后才AllocWindowStart），窗口不覆盖整个引擎帧；release回退是净堆大小，不是可靠逐分配计数。不能据此认定所有分配已定位或为零，不能减去基准地板制造通过。需继续建立真实player精确分配/调用栈诊断和绘制调用归因，本轮没有未经验证地改生产渲染或测量门。

## 环境与保留现场

初始workspace-write下创建build/reports/enhancement-resume被Windows拒绝。经授权运行权限诊断，补齐build与build/reports当前用户权限并验证，但受限环境重试仍拒绝；用户切换本会话完全权限后，同一操作成功。权限恢复备份/逐项撤销脚本在`D:/projects/tmp/angry-chen-permission-recovery/`，报告`acl-report-ec714a0b1fc54f359f7fda29829006eb.jsonl`。没有修改所有者或文件内容，没有继续手工改权限。

引擎本轮生成的packages-lock.json修改已恢复到本轮起点（该文件起点内容干净）；GraphicsSettings.asset起点已有修改标记，未覆盖其内容。其余用户原有修改全部保留。


## 后续接续：275例、联调退出码与真实player复核

本节取代上文中“本轮未重新出包/270例”等阶段性结论；原记录保留以便审计。P0仍 **BLOCKED/NOT PASS**，P1未开始。

### 新增修复

- 基准来件坐标/昵称生成去除数组和字符串分配；复用MatchState解码器，未变化的玩家数组/昵称共享，变化发布新数组，拒绝畸形包不修改已发布状态。
- 分配窗口移到合成来件之前，真实包含snapshot/match/event输入。和解字符串日志改为显式TraceReconciliation开关，默认生产与测试一致，不再仅batchmode免分配。
- UdpTransport每次Poll新建可靠流列表的红测为100次4000B；复用快照列表与重发委托后275例全绿，仍保留回调修改可靠流字典时的快照遍历语义。
- 预热 allocation measurement wrapper，真实player仅第0样本460B的测量器首次调用成本消失；未跳过首样本，仍120预热/600采样。
- 自检拒绝空注册、重复ID、执行中修改、递归执行及缺失必须组。日志校验要求单个START/OK、正且相等计数、唯一PASS数相等、无FAIL。
- 联调脚本原末尾强制code=0会覆盖失败；已删除，所有失败锁存。保留子进程Handle，避免Windows PowerShell退出后读ExitCode为空。进程契约验证exit0和exit7。中文脚本保留UTF8 BOM。两人用例不再对未满房间要求第五人拒绝，四人仍强制验证。
- 增加手动触发client-engine.yml：真实引擎自检、发布构建、player门和日志归档；需要self-hosted Windows ac-unity-licensed标签及AC_UNITY。尚无实际远端CI运行，不能替代PlayMode/PR required check。

### 已执行证据

全部路径位于build/reports/enhancement-resume/（忽略的本地产物），未部署公网。外层shell exit0不代表被测命令成功，下表使用内层明确退出码。

| 项目 | 实际结果 | 证据 |
|---|---|---|
| 最终引擎全套 | exit0，275/275 | selftest-final.log、client/Logs/selftest.log |
| 独立注册器反向契约 | exit0，HARNESS CONTRACT OK | harness-contract.log（最终重跑无CS错误） |
| 日志/进程/依赖摘要契约 | 8/2/3项均通过 | test-selftest-result.ps1、test-process-exit.ps1、test-check-assets.mjs |
| 修正后两人联调 | exit0，274例完整，退出码0 | joint2-final.log、joint2-final/ |
| 修正后四人联调 | exit0，274例完整（新增idle-poll回归前） | joint4-final.log、joint4-final/ |
| 服务端gate-4p2min | exit0，完整时长 | server-gates/gate.json、gate.log |
| 服务端soak-4p5min | exit0，完整时长，RSS斜率-0.058MB/min | server-gates/soak.json、soak.log |
| 服务端latency-200 | exit0，原场景参数不变 | server-gates/latency.json、latency.log |
| release构建 | exit0 | release-strict-build.log，SHA256 743a7dacfb17f3ffd06b3562b459d826603a3947a3ba3e96ce2978dcee3cc293 |
| 真实依赖门 | exit1，仅Codely未准入 | assets-final.log |

联调为一个Unity批处理客户端＋bot，**不等于2/4个独立player**；服务端干扰门也不等于真实客户端丢包/乱序/重复及full-diff恢复验收。

### 性能证据与收紧门禁

- 诊断版dev-final三轮：计数器测得主窗口0B/帧、GC0=0、汇总P95=3.2767ms。旧门打印PASS，但draw=63是提交数，非引擎实测，故本报告不认可其为最终P0 PASS。
- 清理临时诊断代码后的release-final三轮：主窗口净堆增量为0，前两轮引擎计数器缺失回落63；第三轮真实draw=198，超过120，整体exit1。净堆增量不是精确分配计数。
- 修复判据：精确分配计数器缺失不许AllocMeasured=true；引擎draw/triangle未测不许仅凭提交数判PASS。全窗净堆数据也不宣称覆盖/证明引擎渲染零分配。
- 最终release-strict重跑前两轮均UNVERIFIED（alloc=-1），第三轮无JSON且进程exit0，脚本正确exit2而非PASS。保留该环境异常，未篡改或以重试筛选为全绿。
- 临时P0AllocationProbe和FrameAllocationProbe及其markers已移除。恢复本次引擎生成的packages-lock和QualitySettings（起点均干净），保留起点已存在GraphicsSettings/manifest等修改。

### 尚未闭合（不得进入P1）

1. Codely缺适用授权/NOTICE、固定可复现来源及后续安全/发布审计；已向用户提供获取材料与供应商询问信模板，未获授权删除或加入白名单。
2. 发布版精确零分配证明、全帧渲染分配证明、198真实draw call降到原120预算，以及偶发无JSON退出；不降低预算、画质或用提交数冒充测量。
3. 已配置/授权runner上的真实引擎CI执行、PlayMode、2/4独立player及网络损伤/恢复证据。

没有提交、推送或发布；存在用户/前序任务修改，不能整体回退工作区。


## 再接续：队友实例化与真实PlayMode

- 保留队友所有48个模型部件、网格、材质、动画变换、投影/接收阴影，按四种cube材质及gun分成5组显式GPU instancing。普通MeshRenderer使用forceRenderingOff避免重复提交，不支持instancing时保留原渲染路径。缓存部件与矩阵数组，稳态100次SyncFrame/TickDraw精确零分配测试通过。新增SubmittedInstances断言，三名队友48个部件全部提交，移除一人后32个部件全部提交；SubmittedDraws不再冒用RendererCount。
- 全套275/275，instancing-verified-suite.log，exit0。release构建instanced-build.log成功，SHA256 53014eb2268b06e2589b7e5ead8929d6014bfe74722248a3afec7acdbe4b25ff。instanced/三轮真实engine draw=68/68/51，低于120；P95=3.8461/3.0465/4.0712ms。发布精确分配仍不可测，三轮均UNVERIFIED，不能以draw修复宣布P0通过。
- 试验过在release校准GC.GetAllocatedBytesForCurrentThread：64B与1024B数组必须读出差960B且静默为0，否则拒绝；本运行时未通过校准，试验代码已移除，没有采用恒0计数器。calibrated场景受环境影响空场P95=25.567ms，门exit2，不能当性能通过证据。
- instanced-dev/三轮精确主窗口alloc=0、GC0=0、P95=5.0523/4.3134/3.3551ms；第一轮engine draw=69判PASS，后二轮engine计数器缺失判UNVERIFIED。汇总exit2 NOT-PASS，未筛选掉失败轮。新增保存实际屏幕PNG（采样窗外），但当前模型不支持图像输入，read_image被工具拒绝，因此不声称完成视觉人工复核。
- 新增PlayModeSmoke与playmode-smoke.ps1，真实EnterPlaymode后等待GameBootstrap.Loop的60个引擎驱动帧，验证启动root和启用相机；超时/进程失败/缺失唯一日志标记均拒绝。实际命令exit0，client/Logs/playmode-smoke.log含PLAYMODE SMOKE OK frames=60，外层证据playmode-smoke.log。CI入口已串接此烟测；它不等于完整PlayMode测试矩阵或远端CI实际通过。
- 已自行抓取Codely官方条款及安装指南并更新codely-dependency-review.md，纠正“包内无LICENSE即必须另办授权”的错误推断，不再要求用户先找额外许可才能继续本地工作。条款、固定内容指纹、三个Editor-only asmdef和实际发布文件排除检查均有证据；依赖门没有无条件放行，固定版本获取/插件审计尚未闭合。

因此旧的“198 draw超预算”已被三轮实测修复证据替代；剩余关键项为可信发布精确计数/全帧分配、稳定图形计数、独立2/4 player网络恢复矩阵、依赖完整准入及实际远端CI。保持P0 NOT PASS，未进入P1。

最终包含截图持久化及SubmittedInstances断言的发布构建：instancing-final-build.log exit0；stage 20261027T085156175Z-16a55c1d，SHA256 7530e15b081f4edd20bfb4b7ca1b38d49956574cb5d2e031432b02e4871a1d99。159个发布文件按供应商15个二进制的精确文件名全部检索，命中0。最终instancing-final三轮plan-scene均已产JSON：engine draw=68/0/68（0轮只给20提交数，不认可实测），P95=4.8733/3.9603/5.8931ms，精确分配均-1，全部UNVERIFIED。三轮截图已保存，未用不可测/零计数冒充通过。

最终性能脚本已收回总退出码：`final-gate=2`，`FRAME-BENCH NOT-PASS`，三轮均UNVERIFIED，无缺失stage，汇总P95=4.8733ms、精确alloc=-1（不可测），空场P95=3.585ms；附加editor诊断P95=79.8255ms，FAIL，仅诊断不替代player判定。证据instancing-final.log与instancing-final.json。此后台任务已结束，无待收集结果，P0仍未通过。


## 用户公网手测后接续：独立player验收入口

用户已完成公网手测并要求继续；不将未给出逐项结果的手测擅自记作完整P0通过。新增PlayerAcceptance（显式-playerAcceptanceOut才启用）与player-acceptance.ps1，使用真正发布player进程、生产GameLoop/UDP，只自动昵称与准备，要求完整队伍、进入Playing至少5秒且收到至少20条新权威快照；每个进程输出PID/session/playerId，脚本验证唯一、真实进程ID、全部exit0与JSON pass，并只清理自己启动的服务端/客户端。每轮独立端口/数据/证据目录，旧目录拒绝覆盖，不修改公网服务器。

首轮两客户端真实握手成功，但准备状态没有跨失焦窗口持续发出，超时并exit1；没有伪装通过。自动化改为使用生产Sampler.Flush显式提交ready意图，不覆盖焦点语义。首轮中出现服务端不可用现象尚未独立归因，脚本已补每秒进程退出观察与server ExitCode报告。第二观测轮服务器仍在运行但客户端未进入预期阶段，同样exit1。修正版构建exit0，SHA256 8f6f68cc2a991511fdafa243c0cd302e749210210318b1e1dd84cc6c91c5c125；随后开始2/4独立player实际复跑，结果以players2-flush/players4-flush记录为准。此入口只覆盖组队进入对局与快照，不冒充完整网络损伤/恢复/结算矩阵。

独立player复跑已完成：players2-flush与players4-flush脚本退出码均0；逐一读取results.json确认2/4不同PID、session与playerId，各进程Playing阶段100条新快照、pass=true。2人PID=7572/10876；4人PID=15484/14552/32988/33972。证明真实发布版多进程组队/准备/进入对局/快照链路通过，不再以bot替代该项。源码随后仅增加阶段切换日志，完整自检复跑证据为player-smoke-final-suite.log。脚本默认不得覆盖证据目录，退出/超时/缺结果均失败，只清理自身进程。仍未覆盖网络损伤、重连一致性、完整结算、第二局，因此P0不宣称通过。


## 外部UDP损伤与断网恢复接续

新增udp-impairment.mjs（仅127.0.0.1监听，每个客户端独立上游socket；不解析/修改协议）。真实2人名义RTT100ms/5%丢包/乱序/重复与4人名义RTT200ms/10%丢包/乱序/重复均满足所有客户端进入Playing、持续权威快照、唯一进程/会话/玩家ID，脚本exit0。2人实际received3960/dropped197/reordered343/duplicated289；4人received11328/dropped1132/reordered926/duplicated784，errors均0。test-udp-impairment.mjs真实回环验证载荷、双向延迟、重复、延后乱序、客户端映射exit0。名义RTT不是实际逐包延迟测量，reordered表示额外延迟注入次数，不声称逐包实际乱序数。

恢复模式由全部player写.ready触发双向断流5秒（超过协议3000ms超时、低于30000ms宽限）。必须观察非Connected→Connected、同session/playerId、新MatchState和>=20新快照。首轮恢复2人exit1（一人恢复、一人超时），保留原始结果；恢复期间可能自然进入结算，验收原先错误地仅在Playing分支判定，现把恢复检查移出该阶段，不强制游戏停留Playing。另抓到真实多进程日志共享写冲突，ClientLog.Handle未捕获IO异常，boot.client_log_wiring持独占锁回归明确red=1；已添加IO/权限异常计数且不回调Debug造成递归，解锁后继续写入。修复后完整套件/构建/恢复复跑待以下证据闭合。验收脚本已追加未处理Exception/Error日志拒绝，因此不会只凭pass JSON漏掉运行期异常。

日志锁回归已完成red→green：log-io-red.log真实exit1，唯一失败boot.client_log_wiring为IOException共享冲突；修复后log-io-green.log真实exit0，275/275含独占锁写失败计数与解锁恢复写入验证。ClientLog只捕获IO/权限异常，绝不在日志回调内Debug递归；原始引擎Player.log继续保留。修正版构建recovery-fixed-build.log exit0，SHA256 e9c954d61264aa19447193605a45c4c18507159164af4d40508e7461a51803f6，stage20261027T094139712Z-4b6f99ba。首次恢复2/4均exit1已保留；更正阶段门限并保护日志回调后复跑记录players2-recovery-fixed及players4-recovery-fixed。

恢复验收入口进一步修正两处观测错误（未改生产网络）：首次中断时保存一次快照/状态水位，不在重试期间反复重置；恢复检查不再被“当前队伍仍满员”阻挡（其他已经完成验收的客户端可能先退出）。仍要求首次确实满队进入Playing、实际外部5秒黑洞、经历非Connected、保留同session/playerId、20条新快照及新MatchState，未放松恢复阈值。此前所有failed/fixed失败轮仍保留。最终稳定源码构建recovery-final-build.log exit0，SHA256 b5252e09573bcbdfe80dd4a499340c5e6205b6a1f4153d92decd6de21f3d5bcd，stage20261027T094501401Z-b2f02a1e。最终复跑报告players2-recovery-final/players4-recovery-final。

最终恢复2/4人脚本均exit0且逐一复核results.json通过：每个真实player经历非Connected、同session/playerId恢复、至少20新快照、新MatchState；未处理Exception/Error扫描无命中。2人实际黑洞丢934包，4人黑洞计数见proxy.json，代理errors均0。最终报告players2-recovery-final和players4-recovery-final。注意日志存在额外反复重连，当前仅证明指定5秒断流后能够恢复，不证明长时间稳定、完整权威状态逐字段一致、超宽限重连或完整网络矩阵通过；此项仍需跟进，P0不整体判通过。所有此前失败轮保留。

最终稳定源码完整自检 recovery-final-suite.log exit0，275/275；代理自测再次exit0；docs与diff检查exit0。
