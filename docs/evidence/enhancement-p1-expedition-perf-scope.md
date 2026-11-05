# P1 真实远征性能观测边界与首轮观测

> 2026-11-05 仓库清洗：文中 `build/**`、`client/Build/**` 本地产物已删除；结论与哈希保留，文件需按文内命令重建。

源码参考3dda8cb。现有[性能checkpoint](enhancement-p1-performance-checkpoint.md)仅证明原合成负载硬预算；本文件不增加PASS，不把方案当结果。

## 不可复用为真实远征的证据

FrameBench会禁用生产driver并喂入synthetic快照，不覆盖真实ExpeditionCanvas。GameLoop.FrameProfiler仅包住GameLoop.Frame，不含前置ExpeditionAcceptance.Drive、设置持久化、ExpeditionUiRenderer.LateUpdate及Canvas渲染。现有DiagnosticFrameBegin/DiagnosticStageMarked字段未实际调用，不能凭字段存在假装观测已接线。

## 最小公开观测窗口

1. 保留真实GameBootstrap、生产Frame唯一调用、State14与原Canvas。opt-in observer预分配每段120预热+600样本，三轮；reward/combat及人数/构筑分组，记录实际场景，不把单人低负载称最大压力。
2. Whole-frame wall interval：下一Update采上一completed frame，引擎Draw Calls Count/Triangles Count的LastValue包含Canvas；frameCount必须连续否则记missing。相邻Stopwatch interval含LateUpdate、Canvas、渲染调度、验收驱动、I/O与VSync等待，不是GPU耗时或loop CPU。沿用20/33ms、draw120、tri180000，不放宽。counter不可用标UNMEASURED。
3. Exact GameLoop allocation：仅紧包现有唯一loop.Frame，以公开BenchAlloc.Begin/BytesSince校验同Time.frameCount；不重复Frame。该窗口不含Drive、设置和Canvas。development计数器可用记录均值/最大值/非零帧；release不可测=-1/UNMEASURED，禁止heap差冒充精确0。
4. 验收驱动另包时间与分配窗口，不能从whole-frame结果按均值扣除。其Trace会Entry/EncodeState/Base64/闭包/JSON/File IO分配，必须公开污染。进程GC0不得单独归因于loop。
5. 输出及截图在采样外执行，采样内只记primitive与预分配数组。不得剔除trace尖峰洗P95；loop0不等于Canvas0，driver开启的整帧0不能代替driver关闭或预分配replay对照。

## 当前状态

已实施默认关闭的`-expeditionPerformanceOut`observer，经批准的公开primitive采样器与实际player接口验证：缺类型编译RED→实现GREEN，临时副本删除missing-counter防护的负控确实exit1；缺帧/坏时间/阶段中断/无样本不得PASS，原阈值超限FAIL。完整引擎295/295、release构建exit0。默认不加组件、不开recorder，不改VSync/targetFPS或模拟。独立runner用ObservePerformance开关传报告路径。

首轮本机 `build/reports/p1/players/20261104T062556747Z-29ec9a6b/peer-1.performance.json`：真实单人、node1、cardMask1、1920×1080、引擎quality5、VSync1、targetFPS=-1；不是原quality2同场景回归。3×(120预热+600样本)、missing0；wall P95=17.1312/17.1159/17.033ms，P99=18.2348/17.7065/17.7265ms，drawMax57/46/64、triMax8077/5127/7763。按原20/33ms、120draw、180000tri，`scopedFrameGraphicsVerdict=PASS`。

该结果含验收driver/trace/I/O/调度，且来自dirty视图投影，不能归给clean候选；仅首节点低负载，不证明第三节点满构筑、4人最大压力或奖励阶段。报告rewardStatus=UNVERIFIED、allocation=UNMEASURED、canvasHumanReview=UNREVIEWED，不赋予P1整体PASS。精确loop分配与reward专门窗口未实施，视觉及真人验收另行记录。

## 第三节点4人满构筑观测：图形门FAIL

新增目标节点参数1或3（默认1），公开行为测试先复现node1错误填充node3窗口RED，再过滤GREEN；非法节点与未到目标节点始终UNVERIFIED。完整引擎295/295、release构建exit0。真实4人无损隔离性能轮4d9eab01全员完整三节点技术PASS、真实exit0，但**四端scopedFrameGraphicsVerdict均FAIL**，不得将进程成功当性能成功。

本机报告 `build/reports/p1/players/20261104T064234194Z-4d9eab01/peer-{1,2,3,4}.performance.json`：每端requestedNode/nodeMin/nodeMax=3、playerCount4，hunter cardMask7、breach56；1920×1080、引擎quality5/VSync1；每端3轮、missing0。十二个窗口P95范围16.9923–17.0726ms、P99范围17.3898–18.0302ms；triMax不超过53801。四端draw最高162/153/163/167，超过原120阈值；原阈值保留，不以帧率尚可为理由放宽。

该轮为dirty视图投影，尚未归因到P1 Canvas、羊表现、远程玩家、质量设置或外部模型修改；不将其直接判为P1规则回归，也不能用clean旧候选合成场景PASS掩盖。下一步须构建更短的实际图形反馈环，并以相同快照/视角/质量对照clean与dirty及Canvas开销，先测后改；不得降低目标难度、减少羊或关界面来伪造通过。全P1性能门保持未通过。

## 干净源码对照亦FAIL

隔离Git2f45ef0源码（排除外部dirty表现/依赖/配置）完整自检292/292、release构建exit0。真实4人同样1080p/quality5/VSync1、目标node3、满构筑，d99ae8e1全员完整三节点技术PASS、exit0；每端3轮、missing0。但draw最高141/133/137/131，四端仍scopedFrameGraphicsVerdict=FAIL。十二窗口P95范围17.0835–17.2751ms，P99范围17.7089–20.1606ms，triMax20179；帧/面数子项通过不抵消draw超限。

本机 `build/reports/p1/players/20261104T065621924Z-d99ae8e1/peer-{1,2,3,4}.performance.json`。该对照排除了“失败完全由外部dirty模型引起”的假设；因两轮非同seed/视角重放，不把dirty→clean差值当优化成果，不直接给Canvas定罪。

## 缩短反馈环的现有边界

GameLoop公共OnPacket/ApplySnapshot/Frame可喂入真实表现，但当前没有node3录制/回放CLI。现有FrameBench每帧强制生成4p60羊及MatchState/Event，会覆盖外部注入，不能直接冒充真实node3重放。现有acceptance trace仅有State14 base64/本地视角与汇总，缺全实体Snapshot，不能从中重建原峰值。

下一最小诊断片段需全量起点及有序delta/removed、完整MatchState10/State14/Event6与到达时刻、输入/视角/dt、特效预卷、实际图形配置与构建身份，以及峰值frame号/engine draw/原图。可先2–5秒固定视角片段构建公开渲染反馈环，但不当正式预测或性能门；最终仍复测原四人节点场景。没有该反馈环前不盲改渲染或关功能，也不无限重跑以等待偶然PASS。

## 短合成诊断复现阈值悬崖

已在隔离clean源码加throwaway player hook（仅scratch，非产品入口），实际Bootstrap/Canvas、4玩家满构筑、固定相机(12,1.6,-12)/yaw0、1080p、quality5、supportsInstancing=true。保留场地、远端玩家及光影，按公开ApplySnapshot/State14/Frame接口扫0/12/24/25/32只可见同形羊，每档60预热+120实际引擎帧。下一Update读completed frame计数与唯一驱动上一帧呈现tuple关联，全部valid、实际culled/drawn数等于目标、remoteCount3、remoteSubmitted5。

本机 `build/reports/p1/172-draw-probe.json`、构建及player真实exit0。引擎draw各档稳定为54/97/145/58/58，Presentation submitted为5/29/53/7/7。24羊为Single，25羊转Instanced；阈值两侧87draw下降支持原25只实例化阈值造成中小羊群预算悬崖。该诊断不是原网络现场重放，不证明所有超预算均为该原因；没有关闭Canvas、减少目标压力或更改阴影来伪造改善。已仅为远征支持实例化硬件启用小羊群实例化，Classic阈值与历史回退保持。公开Batching策略编译RED→GREEN，clean投影引擎293/293。短环复测176-draw-probe-fixed.json：0/12/24/25/32羊draw54/58/58/58/58，全部valid，24羊145→58；未关闭Canvas、减少羊或更改阴影。原真实4人1080p/quality5/VSync1第三节点满构筑复测92f240db：全员完整三节点exit0，每端3轮missing0，四端scopedFrameGraphicsVerdict=PASS，最大draw74/65/73/72；P95范围16.9681–17.0597ms，P99最高17.7916ms，triMax21599。原FAIL与短环均保留。

本机 `build/reports/p1/players/20261104T072109002Z-92f240db/peer-{1,2,3,4}.performance.json`。该隔离clean构建额外包含默认不运行的scratch诊断hook，实际联机轮无hook参数，原生产driver仍运行；不能据此称最终发布包全部退出。奖励阶段、精确分配与视觉/真人门仍未完成。本次支持“中小可见羊群Single路径”为预算失败来源之一，不声称排除了所有可能峰值。

## 无诊断hook的干净候选确认

干净Git15c3939、无scratch hook发布构建，完整引擎293/293、构建exit0、依赖泄漏审计exit0。实际4人完整三节点a054423b技术PASS、exit0，1080p/quality5/VSync1第三节点满构筑每端3轮missing0，四端scopedFrameGraphicsVerdict=PASS；最大draw71/65/70/70，全窗口P95最高17.486ms、P99最高21.8384ms，原20/33ms及120draw门保持。原本失败轮未删除，未将奖励/分配/真人状态改成PASS。

本地候选 `build/reports/p1/P1-15c3939-candidate.zip` SHA256 `a7229340de550bbcf48cc68ac045b0a4938dfd3dc752498b03b418703b3d908c`，含完整client/server及启动脚本、BUILD-IDENTITY/逐文件哈希；运行包未上传GitHub。隔离导出首次审计因缺开发包缓存未能执行，后以现有已校验vendor内容对同一发布目录完成审计，不把缺测记PASS。

## 奖励独立观测：一轮完整，不足三轮

默认仍combat，新增显式Stage=reward选择；reward忽略combat节点过滤并报告requestedNode=0。同一reward中offer改变、离开奖励阶段会清未完成窗口与旧峰，完整轮次可来自不同offer，但不拼接部分窗口。公开采样行为RED→GREEN，完整引擎294/294、release构建exit0；runner opt-in参数/manifest guardrail RED→GREEN，不改权威30秒期限、推进速度或选卡流程。

真实4人1080p/quality5/VSync1奖励重连损伤轮ef782d29（delay100/loss10/5秒断流及15秒恢复稳定窗口），完整三节点技术PASS、实际exit0。四端奖励各仅1轮完整120预热+600样本、missing0，因后续奖励自动正常选卡没有足够连续窗口，**scopedFrameGraphicsVerdict/rewardStatus/combatStatus均UNVERIFIED**。已完整一轮P95为17.0745/17.0424/17.0531/17.1247ms，P99为18.4218/17.9613/18.4199/17.4155ms，drawMax均36、triMax4797/4797/4797/4809；只作已测一轮数据，不称三轮预算PASS。报告数组余下0是未填窗口，必须连同runs=1解读，不能当0ms实测。

本机 `build/reports/p1/players/20261104T082737445Z-ef782d29/peer-{1,2,3,4}.performance.json`。下一步可仅让诊断驱动像玩家阅读一样在每次合法奖励内等待后再正常选卡，累计三个完整窗口；不得延长期限、抹掉不完整样本或调用游戏后门。精确分配与视觉/真人仍另待。
