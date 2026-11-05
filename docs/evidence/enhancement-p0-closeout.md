# P0收尾实证（2026-11-03，PASS）

> 2026-11-05 仓库清洗：文中 `build/**` 本地产物已删除；结论与哈希保留，文件需按文内命令重建。

**P0已通过，P1未开始。** 真实普通CI与真实自托管Windows引擎CI在同一实现提交2eca8bbe7981f79082371852e308dd8170182965均SUCCESS。以下明确区分dirty当前模型构建与clean CI构建，失败轮完整保留。

## 有限范围

按增强计划第9节完成基线修复/脚本/CI，不进入P1。完整P4内容矩阵、30分钟长局及“整个引擎帧零分配”不是本轮追加退出条件。保留原主窗口0B、GC0=0、帧20/33ms、120draw及其他C14预算。release精确分配缺失仍记-1；采用同一冻结投影的release帧/图形与development精确主窗口配对，不把dev叫release。

## 身份与外部修改

实现提交eacbee0（基于dca75799）。本机测试实际使用dca75799加未提交P0改动及外部模型/Shader/配置修改的dirty工程；不称纯净提交构建。外部PresentationLayer/RemotePlayerVisuals/SheepMesh/WeaponMesh/Shader/SuiteRegistry/ModelMeshBuilder/ModelGeometrySuite/BuildInfo/manifest/lock/GraphicsSettings/ProjectSettings/client-baseline-check均未纳入本次提交。生成投影排除两开发工具，原工程引用未动。

冻结输入：`d4700e35b813630c332bfa7196670edae307da034f9276b446e3ca8b21833ac3`，由最初p0-project算法对Assets/Packages/ProjectSettings（含当时BuildInfo）按字典序path+NUL+bytes+NUL所得。两包均由同一不改业务源码的投影串行BuildEntry构建，后续投影脚本改为排除生成BuildInfo的算法不套用此旧指纹。完整player清单已保存，不能仅用Mono启动器exe hash识别业务版本。

## 已实际通过

| 条件 | 命令/场景 | 实证 |
|---|---|---|
| 网络恢复 | player-acceptance 2人delay50/loss5；4人delay100/loss10；均乱序/重复/5秒黑洞/StableSeconds15 | build/reports/enhancement-resume/players2-20261103、players4-20261103，实际exit0；全6个进程同session/playerId恢复，中断0，稳定15秒 |
| 完整引擎自检 | 隔离工程SuiteRegistry.RunAll | build/reports/p0-20261103/selftest-final.log，279/279，exit0 |
| 实际PlayMode | PlayModeSmoke.Run，真实60引擎帧 | 同目录playmode.log，exit0 |
| release三轮 | 1920x1080 quality2，120预热+600采样，EvidenceRole release | release.json/release-runs，三轮PASS；完整600帧图形覆盖，P95中位3.412ms，draw72、tri103201，alloc=-1不判0 |
| dev三轮 | 同投影Mono development，EvidenceRole allocation | allocation.json/allocation-runs，三轮PASS；精确主窗口0B、GC0=0、八段预算通过 |
| 配对 | frame-bench-pair release.json allocation.json paired.json | paired.json，PASS exit0，不证明release精确分配或整个引擎帧0B |
| 依赖门 | test-codely-dependencies/test-check-assets/真实check-assets/官方冷获取 | 13组/6组及36直接依赖门exit0；详见依赖审查 |
| 发布排除 | audit-release . 新release-player和dev-player | 实物157/255文件，均PASS；旧stage反向exit1命中TJGenerators.Runtime.dll |
| 脚本反向 | pair正向+15负例、projection契约、UDP代理真实回环 | 实际exit0 |

网络计数完整保留：两人重传耗尽15/24、收包超时各1；四人18/16/12/21、收包超时各1。发生于故意断流及早期恢复，不是全程无重连；不把“零耗尽”新增为门。

release完整tree（清单JSON的SHA256定义见工具）：`3f24bff102566571d1f1230ef17676b0dacb885b1e4ba2779c035a826a54948e`；development `a1f4c53d388cee29333e459ea70e836b19295f29c240ef6432202d32e660c4d9`。原始清单在同目录release-tree.json/dev-tree.json。新dirty release ZIP为同目录angry-chen-p0-dirty-release.zip，SHA256 `8902fd68ef43b004f493bc3051b9a6b540fd507f845d4f0b2a04981c19333a1d`（未更新原发布latest）。旧网络player归档SHA256沿用交接1bf7234c17b85011219aa76bba087d724434fc8dd9cf90a287be23aa9dbafdfc；新server含窗口外ACK修复。

## 保留的本轮失败与归因

- 11月2日players2-oldack/foreground/complete：代理/进程提前结束或缺JSON，均FAIL，未删除，不能计通过。11月3日完整原参数2/4实际通过才关闭网络项。
- 隔离工程首次selftest.log：265项8失败，缺父目录docs和server fixtures；补只读仓库上下文后selftest-context.log 279项1失败，缺别名检查所需parent/client目录；补该上下文后279全绿。不改测试断言、不隐瞒前两轮。CI投影已采用nested client布局及复制上下文，避免同一环境布局错误。
- pair夹具首次FAIL（正例缺逐轮机器元信息），补正例字段后正向+15负例通过；生产判据未放宽。Mono启动器hash恒相同，移除“两个exe必须不同”的错误要求；用完整player树与构建时指纹记录来源。
- 空场探针进程exit2：按设计无场景实体/图形预算不完整，不当作完整场景PASS；同轮空场帧P95分别3.4764/1.9697ms均低于20ms，只承担ADR-014机制地板检查。

## CI（尚待最终真实运行结果）

普通CI首两次（eacbee0/3a53678）实际失败轮保留：新增Windows MSVC Werror构建发现king_phases将quantizeAngle的uint16值存double再隐转，及测试报告fopen的MSVC弃用警告；另英文Windows PowerShell5.1以ANSI解析无BOM中文字符串导致frame-bench解析失败。301fc58最小修复为auto保留单位类型、MSVC fopen_s分支、两处输出备注改ASCII，未降低Werror或预算；本机重新构建562/562，原脚本发现场景复测通过。随后在301fc58触发真实引擎CI。该次clean自检276、PlayMode60帧、release/dev三轮配对均实际PASS，整job在CMake不在runner PATH失败，未执行CI网络；失败轮artifact及本机副本保留。后续由真实安装定位CMake，不猜缺工具。

MSVC完整构建进一步收敛：UTF-8源编码、标准C API的微软CRT弃用标记、Windows min/max宏污染、变量遮蔽/计数窄化与测试宏内预处理问题；保留/W4 /WX及其他弃用错误。优化MSVC姿态重用输出异常经最小复现，将聚合赋零改为成员显式清零，无规则/阈值变更。首次561/562保留；最终2eca8bb本机MSVC完整build exit0、562/562、CTest2/2，GCC重建562/562。原测试/诊断全部退出码与失败日志保留，不以新增单测替代原场景。

实现与CI修复已推送至301fc58。普通CI最终[run41598880499](https://github.com/FUG525403823/angry-chen/actions/runs/41598880499)在2eca8bb完整SUCCESS（quality、Linux server、Windows MSVC+ctest、client scripts、server-perf含反向/2分钟/5分钟）；先前run41595447038失败保留。真实引擎首轮run41595499496，已实际执行clean源码276/276、PlayMode60帧（dirty本机279/279含外部模型新增项，二者不混数）。最终[引擎run41600039035](https://github.com/FUG525403823/angry-chen/actions/runs/41600039035)在2eca8bb完整SUCCESS，主step全部完成，包括服务端MSVC构建、2/4真实player原损伤+恢复门。clean release三轮P95=4.5825/4.8670/4.4056ms、draw68、tri34819；dev三轮精确主窗口0B、GC0=0，配对PASS。全6个peer同身份恢复、连续15秒稳定、中断0，完整超时/重传计数仍保留。由此关闭最后CI阻断，P0 PASS。第二次真实引擎run41598936870（2eca8bb）在checkout旧Unity深路径清理回退后，HTTPS fetch连接reset/超时失败，所有引擎步骤SKIPPED，不计产品失败或通过。第三次针对已证实环境问题采用新短工作目录与仅runner进程级HTTP/1.1/core.longpaths设置；没有修改产品门或全局Git配置，原失败轮保留。临时runner仅用于本次自托管Windows真实引擎执行，不安装服务，不改公网服务器。clean CI源码不含外部dirty模型/包引用，必须和上述dirty本机证据分开报告。

最终引擎脱敏证据本机保存于build/reports/ci-runs/41600039035/sanitized/build/reports/engine/，包含原始三轮JSON、paired.json、完整tree、两份BuildReport和网络结果；GitHub artifact13214755360，SHA256 `bd3c9cb87b74cf5f94c8a96eb68a0d162b02f5cb0e65ee5c4eccb51978c5e2a6`。clean release tree `af46c68513c8b88985e104793fd1af806747166d1adc00af9baac51f1c3fc0ea`，dev `85d6be2b0be96ce1a28f3bc210efc799e3448bc877d6e03de67c196ee06ad712`。全部后台任务已收回，ephemeral runner退出并清理owned credentials，API runner数量回到0；无服务或公网变更。

截图已保存但当前模型不支持图像输入，未声称人工视觉复核；P4视觉基线不移入P0。结束于P0，不新增远征/构筑/P1玩法。
