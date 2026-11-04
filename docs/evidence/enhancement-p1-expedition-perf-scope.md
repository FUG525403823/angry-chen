# P1 真实远征性能观测边界与首轮观测

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
