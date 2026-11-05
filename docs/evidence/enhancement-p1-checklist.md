# P1 有限退出清单

> 2026-11-05 仓库清洗：文中 `build/**`、`client/Build/**` 本地产物已删除；结论与哈希保留，文件需按文内命令重建。

状态：实施中，尚未通过。入口HEAD `d82f9a2bc29732b4978dc4eda253fccd4ceac5b5`；P0以[最终收尾](enhancement-p0-closeout.md)为准，不重新开放历史阻断。批准范围见[计划](../enhancement-plan.md)第9节。

## 固定范围

- Classic仍为默认，十波规则与旧fixtures原样保留；Expedition仅显式开发/试玩入口。
- 一个复用布局：80m牧场及现有谷仓碰撞；一个目标：净化设施防守（区域推进/敌人侵入损伤，单人可完成）；一种工具：治疗脉冲（可自用，服务端冷却，不复活倒地，不要求队友）。
- 一幕三遭遇节点，复用同一个目标与布局，三个节点总战斗目标约9分钟，加安全阶段奖励约10分钟；不实现三幕九节点、路线票决、商店、局外成长或其他P2–P4内容。
- 两流派各三张机制卡：精准猎手鼓励选定目标和连续精确命中；破阵冲锋鼓励近距进攻/打断与换位。所有卡有明确收益、代价或条件、有限触发，不做六张通用加伤卡。
- 个人三选一、30秒截止、跳过转补给、一次重抽、前置/去重/上限过滤；未连接玩家不阻塞；重连恢复候选与截止时刻而非重新开奖。
- 最小HUD/奖励采用现有官方uGUI，显示目标、工具冷却、构筑、卡的条件/取舍、pending及拒绝；Classic原界面保留。不提前迁移所有产品屏幕。

## 小包与可核验退出条件

| 包 | 实现/公共测试缝 | 必须证据 | 当前 |
|---|---|---|---|
| 1 契约 | RunState的开始/请求/步进/读取；构筑候选/选择/战斗解析；遭遇开始/步进/读取；版本化双端字节编解码 | 先冻结ADR、稳定ID及内容定义；手写字节期望，截断/坏值/未知ID/内容不符拒绝；旧golden无批量刷新 | 未完成 |
| 2 权威切片 | 生产Room/stepWorld接线，独立战斗与奖励随机；命令经所属会话归属校验 | 每卡成功/边界/非法、幂等/过期/越权、目标成功/失败/超时、第二局清理；小包真实红→绿日志 | 未完成 |
| 3 双端与HUD | 生产GameLoop/UDP请求和完整权威状态；输入模态及安全阶段选卡 | 完整客户端自检/JointSuite，真实PlayMode，HUD截图；未知/旧协议明确拒绝，无客户端自行判伤害奖励 | 未完成 |
| 4 对拍与联机 | Classic旧向量 + P1版本化逐tick向量；真实GameLoop独立player | 1/2/4人战斗→目标→选卡→下一节点→结算，工具/两流派触发，完整构筑/候选/目标重连一致；2人delay50/loss5、4人delay100/loss10，乱序重复/5秒断流/恢复后15秒中断0；所有失败轮保留 | 技术场景已通过，见逐轮证据；不表示随机局必胜 |
| 5 性能及交付 | 同冻结dirty业务源码投影串行release/dev构建，完整player树 | 沿用原硬预算，三轮120预热+600采样、1080p/quality2，release帧图形+dev精确主窗口配对；缺测不PASS；依赖/发布泄漏门；运行包及SHA256 | 未完成 |
| 6 有限人类试玩 | 真实可运行切片，有限步骤及记录表 | 玩家无需口头帮助完成首节点，能说明两种打法差异；记录理解、选卡耗时、失败原因、重开意愿。不由自动化伪造好玩通过 | 待可运行切片后提交人类 |

## 实施中证据快照（不是P1通过）

以下build/reports路径为本机保留的原始运行证据，不随源码提交到GitHub；路径非公开下载链接，不能把源码仓库当成完整验收产物。

- Windows服务端曾完整594/594；review三项真实缺陷（排队工具回执、跨run水位、永久离房pid复用）另有各自RED，修复后独立MSVC **598/598**，见完整日志（本机 `../../build/reports/p1/room-review-full.log`）。此前7/7不能冒充这些缺陷已修复。
- 客户端第三轮真实引擎自检 **285/285，exit0**，见退出码（本机 `../../build/reports/p1/run-20261103-1704/26-engine-selftest.exit.txt`）。前两轮失败保留；生产mock显式迁协议3，旧golden未刷新。
- 独立C++生产纯模块与C# spec逐tick对拍 **24468行、34先验、2负控，exit0**，见结果（本机 `../../build/reports/p1/differential/summary.json`）。不替代Room/网络/真实玩家。
- 首个dirty投影release构建与依赖泄漏审计exit0；首轮真实单人hunter **FAIL**，21.7秒工具请求回8，且导航/瞄准需修驱动。见实际失败结果（本机 `../../build/reports/p1/players/20261103T094837441Z-0cb0bc3a/peer-1.json`）。该轮日志发现JsonUtility遗漏非Serializable权威DTO，不能称完整权威审计；修复后必须新轮重跑，保留本轮。
- 第二次dirty投影自检 **286/286**、release构建、依赖审计exit0；通用Graphics PlayMode **60frames exit0**（仅bootstrap/camera smoke，不冒充P1奖励Canvas视觉验证）。第二真实单人hunter **FAIL** 全员倒地，见结果（本机 `../../build/reports/p1/players/20261103T100051648Z-e5edfcd4/peer-1.json`）。权威Base64与字段已记录，实测工具使HP比率190→253，不能因此宣布整个工具验收齐全。驱动未释放模态关闭后的Fire，保护状态持续报零，60Hz为guard零命令而非有效输入；修驱动遵循释放保护后需新轮复测。
- 2026-11-04完整引擎新增ACK边界回归 **288/288，exit0**，见日志（本机 `../../build/reports/p1/50-engine-ack-regressions.log`）：pending仅exactACK结束，wrongRun/MAX不污染客户端计数，MAX明确拒绝不回绕。
- 第三真实单人轮因server提前退出而中断，原脚本未保存server退出码，不能断言崩溃；新脚本已补进程启动/结束/真实退出及强杀证据。今早相同artifact复现正常结算失败，**35击杀/8工具/43触发后全倒地**，见结果（本机 `../../build/reports/p1/players/20261104T013829449Z-5080f1b6/peer-1.json`）。补给链正用公共Room回归诊断，不将driver能力当真实通关。
- 补给根因已公共Room真实RED确认：远征开局跳过既有四箱生成；产品仅新增调用既有spawnAmmoCrates，公共E1500ms备弹0→120、节点不重生、新局清理/Classic四箱均通过。MSVC完整 **599/599**，见日志（本机 `../../build/reports/p1/54-ammo-server-tests.log`）。修复后首真实solo hunter **三节点技术PASS，exit0**：253击杀、3选卡、28工具接受、428触发，humanFunVerified=false；本机结果 `build/reports/p1/players/20261104T014640029Z-37c4121c/peer-1.json`。
- 隔离暂存源码（不含外部dirty模型/依赖）的完整引擎自检 **285/285，exit0**；比dirty工作树少3项既有ModelGeometry测试，不是删除历史测试。隔离文档/素材门exit0，本机日志 `build/reports/p1/64-clean-index-engine.log`、`63-index-docs.log`、`63-index-assets.log`。
- 首2人损伤轮与带诊断复轮均全倒地FAIL，后者合法解码后BaselineMismatch为864/818，而坏包/解码/分片错误为0；实际证明delta链断问题，不把恢复身份等同完整成功。本机目录 `build/reports/p1/players/20261104T021111222Z-5c9bb02c`。无损对照约380s、20Hz快照、两节点后server退出，真实exit=-1073741510（0xC000013A），不称访问违例崩溃或通关；来源未证实，进程记录保留。
- 真实UDP stale恢复与P1有界600ms时效窗口红→绿，MSVC完整 **602/602，exit0**（本机 `build/reports/p1/76-window-tests.log`）；Classic仍2tick、future拒收、rewind仍200ms。修后真实2人损伤轮到第三节点progress112050ms后全倒地FAIL，hunter217击杀/24工具、稳定恢复478秒，中断0，不能据推进改善宣称完整成功（本机cb868865）。干净暂存源码release构建/依赖门exit0后4人损伤轮亦在第二节点全倒地FAIL，身份恢复true、稳定240秒（本机9c0509a6）。奖励恢复单独2人轮首次窗口恢复true且稳定，但后续request-rejected:4，整轮FAIL且recoveryInterruptions=6（本机b2cf2e71）。三个目录均在 `build/reports/p1/players/`，不随源码发布。
- 冻结0948投影原硬预算三轮配对回归PASS，见[性能checkpoint](enhancement-p1-performance-checkpoint.md)。不证明远征奖励Canvas及最终修复产物性能，不能据此标包5通过。
- 新带箱位/按键诊断的真实2人delay50/loss5/乱序重复/5秒断流 **完整三节点PASS，exit0**，本机1526baa7：hunter249击杀/485触发，breach205击杀/75触发，两端3选卡/27工具、身份保持、恢复后532秒中断0；humanFunVerified=false。产品与上轮相同，新增仅trace，不删除之前失败；不足以证明随机难度始终可通。
- 后续4人损伤轮92a37721第三节点56250ms后全倒地FAIL；27655f98亦第二节点全倒地FAIL。奖励恢复f404cb3c首窗口恢复true，但后续中断9、第三节点全倒地FAIL。合法tool跨phase拒绝造成的driver整轮退出已独立边界测试修复，引擎290/290，不削弱服务端拒绝及整轮成功门。
- 旧outstanding移动消息恢复风暴公开transport RED31→GREEN；仅同session Mode1恢复时退役旧Command，动作持久性/Classic/Mode0/新握手/真正耗尽均回归。最新dirty引擎294/294、干净dabe801源码291/291及release构建/依赖门exit0。服务端603/603。
- 新2人奖励恢复轮69eaf8f5 **三节点PASS，exit0**，两端身份保持、完整首offer恢复/稳定，后续567秒中断0。干净dabe801 release的4人delay100/loss10/乱序重复/5秒断流轮4484ad7b **全员三节点PASS，exit0**、恢复后524秒中断0。4人奖励窗口fce0b861与82a10a31首offer恢复及15秒稳定均通过、全程恢复后中断0，但第三节点全倒地，整轮仍FAIL；不能把恢复子门通过当整轮成功。所有历史FAIL保留，不以成功覆盖。
- 干净dabe801同源码release/dev原合成负载三轮120+600配对PASS，详见[性能checkpoint](enhancement-p1-performance-checkpoint.md)；不冒充远征奖励画面的实测。候选完整Windows包（含server、双击脚本、完整player树与逐文件SHA256）本机 `build/reports/p1/P1-dabe801-candidate.zip`，SHA256 `ac42db1c55572ff86fc3414fbb4ff66583351fb521074ca73b880c6cf05ca7d3`，不是最终验收包，也未上传GitHub构建缓存。
- 最新archetype-aware actual-intent driver（hunter安全站稳、breach正常3–5m接敌，不改产品规则）4人奖励窗口轮4a1b1ec9 **全员完整三节点PASS，exit0**：四端身份/首offer完整恢复、3次选卡、24/27/27/27工具，恢复后558–559秒中断0；hunter324/392击杀、653/790触发，breach98/84击杀、142/142触发。humanFunVerified全部false。此前失败仍保留，不说明任何随机局都能通关。
- 最新干净3dda8cb隔离源码引擎291/291、release构建与发布依赖审计exit0；该clean release在6d164835轮 **4人奖励窗口恢复及完整三节点全员PASS，exit0**，每端3次选卡、26/27/27/27工具、恢复后558秒中断0，击杀351/83/417/61，触发695/99/785/77。不是把dirty投影结果归给clean包。
- 推荐候选更新为本机 `build/reports/p1/P1-3dda8cb-candidate.zip`，SHA256 `a04454e71d77fbf8d5db275ada5d9c7b38c659de7a8c55ca87d5fc034b172723`；含完整client/server、启动脚本、来源身份与逐文件SHA256。旧dabe801包保留作为历史候选，不覆盖其性能证据范围。
- [P1 UI只读审计](enhancement-p1-ui-audit.md)明确旧IMGUI分辨率断言不能替代远征Canvas、现有鼠标奖励入口没有Tab/Enter选卡、长中文与构筑/工具矩形相交需实际视觉核验。P1 HUD截图视觉审阅、远征画面专门性能、人类试玩 **仍未通过**；当前提交为实施checkpoint，不宣称P1收尾。

## 执行与证据纪律

测试仅跨批准计划已明确的模块公共接口、生产协议、生产GameLoop及真实player缝；不测试私有实现，不以单测替代原场景复测。每包先红再最小实现，不一次堆完全部测试。新证据独立目录，真实子进程退出码单独保存。性能与其他player互斥，Unity自检/PlayMode/构建串行，启动前检查现有编辑器。

起点Git status与用户清单一致。外部PresentationLayer、BuildInfo、RemotePlayerVisuals、SheepMesh、WeaponMesh、Shader、SuiteRegistry、manifest/lock、GraphicsSettings、ProjectSettings、client-baseline-check及ModelPreview/ModelMeshBuilder/ModelGeometrySuite不得回退或混入提交；必要重叠读取当前版本最小增量并逐文件核对。构建如包含这些内容明确叫dirty投影构建。

通过判定：包1–5全部有真实证据且包6获得真实人类结果才可标记P1通过。自动化完成但人类试玩未回传时，只报告技术门完成/试玩待验，不等同增强项目完成。用户于2026-11-04新增授权提交并推送GitHub；仅提交隔离后的P1改动，外部dirty保留。不改公网、不采购、不上传秘密、不覆盖供应商依赖。
