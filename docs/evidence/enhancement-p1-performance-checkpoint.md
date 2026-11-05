# P1 性能 checkpoint（非最终退出）

> 2026-11-05 仓库清洗：文中 `build/**`、`client/Build/**` 本地产物已删除；结论与哈希保留，文件需按文内命令重建。

2026-11-04对冻结dirty业务投影执行原P0硬预算，不改阈值。源码fingerprint `7ebf896f916eae0fc341d541ba7b09069a53f94e63e3863d4d87a614638b1571`；同投影串行release与development构建，均为Windows Mono。

本机生成报告（不随源码发布，已于 2026-11-05 清洗删除）：`build/reports/p1/perf-0948/{release-summary.json,dev-summary.json,paired.json}`。

## 实测

1920×1080、quality2、原bench-4p60sheep场景、每种构建3轮、每轮120预热+600采样，含同轮空场景底噪控制。

| 指标 | release三轮中值 | 原硬预算 |
|---|---:|---:|
| frameP95Ms | 3.1507 | 20 |
| frameP99Ms | 3.5847 | 33 |
| drawCalls | 72 | 120 |
| triangles | 103201 | 180000 |
| particles | 256 | 256 |
| materials | 8 | 24 |
| 采样区间GC0 | 0 | 0 |

配对development精确主窗口 managedAllocBytesPerFrame=0、GC0=0，八段stage均有测值且小于既有预算。配对判定PASS，failures/missing均空，真实exit0。

## 干净源码 dabe801 配对复测

排除外部dirty后，隔离Git源码自检291/291、release/dev串行构建exit0。最终投影fingerprint（排除生成BuildInfo）`04e346f4081e7d207091a267d48cc8d0ffcf2d90ec0adb3a5809941e9218a187`。同样1920×1080/quality2、每种3轮120+600，真实配对exit0/PASS，missing/failures空。本机报告 `build/reports/p1/perf-clean/{release-summary.json,dev-summary.json,paired.json}`。

release三轮中值frameP95=4.866ms、P99=5.7331ms、drawCalls68、triangles34819、particles256、materials8、GC0=0；development精确主窗口0B/frame、GC0=0，八段齐全。该干净源码已含600ms受理窗口、P1恢复过时移动退役及最新intent driver。旧dirty与新clean视图模型不同，三角面下降不作为优化成果。该轮沿用原合成负载硬预算，只证明其覆盖范围，不伪装远征全部画面的实测。

## 不证明什么

- 这是原合成负载场景在P1二进制上的回归，不证明远征奖励Canvas打开或远征三节点最坏压力的性能。
- 不证明release精确分配，也不证明整引擎每帧零分配；精确分配仅为合成输入+GameLoop.Frame主窗口。
- 包含用户原有dirty视图/模型/配置改动，不能拿这个fingerprint冒充GitHub干净提交。
- 后续网络修复或业务改动后需重新匹配最终source/artifact；本checkpoint不自动覆盖后续构建。

release launcher SHA256 `3936cf9527601ec080b59762f8e454b35ce46c194602bc04b4e4e2ff19b7623e`；development launcher SHA256 `aa134a7f951ead5ac97a6bec1dc3c03709ce0eba39545cb41d53632133d89b3f`。完整player树另由每轮验收保存，launcher hash不能代替Mono业务程序集身份。
