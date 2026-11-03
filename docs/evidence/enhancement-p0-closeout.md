# P0收尾实证（2026-11-03，待CI最终结论）

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

release完整tree（清单JSON的SHA256定义见工具）：`3f24bff102566571d1f1230ef17676b0dacb885b1e4ba2779c035a826a54948e`；development `a1f4c53d388cee29333e459ea70e836b19295f29c240ef6432202d32e660c4d9`。原始清单在同目录release-tree.json/dev-tree.json。旧网络player归档SHA256沿用交接1bf7234c17b85011219aa76bba087d724434fc8dd9cf90a287be23aa9dbafdfc；新server含窗口外ACK修复。

## 保留的本轮失败与归因

- 11月2日players2-oldack/foreground/complete：代理/进程提前结束或缺JSON，均FAIL，未删除，不能计通过。11月3日完整原参数2/4实际通过才关闭网络项。
- 隔离工程首次selftest.log：265项8失败，缺父目录docs和server fixtures；补只读仓库上下文后selftest-context.log 279项1失败，缺别名检查所需parent/client目录；补该上下文后279全绿。不改测试断言、不隐瞒前两轮。CI投影已采用nested client布局及复制上下文，避免同一环境布局错误。
- pair夹具首次FAIL（正例缺逐轮机器元信息），补正例字段后正向+15负例通过；生产判据未放宽。Mono启动器hash恒相同，移除“两个exe必须不同”的错误要求；用完整player树与构建时指纹记录来源。
- 空场探针进程exit2：按设计无场景实体/图形预算不完整，不当作完整场景PASS；同轮空场帧P95分别3.4764/1.9697ms均低于20ms，只承担ADR-014机制地板检查。

## CI（尚待真实运行）

eacbee0已推送。普通CI与引擎CI最终run ID/结论将在实际收回后补；目前不宣称P0完成。临时runner仅用于本次自托管Windows真实引擎执行，不安装服务，不改公网服务器。clean CI源码不含外部dirty模型/包引用，必须和上述dirty本机证据分开报告。
