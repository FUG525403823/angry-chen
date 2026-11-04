# P1 真实远征性能观测边界（方案，未实施）

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

只读方案已核对生产调用边界，**未实现observer、未取得真实远征性能数字、未判定PASS**。若实施，先通过批准的生产Frame公共调用与实际player seam做红→绿，不测试私有字段，不改变Classic、原规则或预算。视觉及真人验收仍另行记录。
