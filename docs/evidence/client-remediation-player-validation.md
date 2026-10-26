# 客户端真实 Player 验证

## 初次构建（非最终版）

- 测试时间：2026-10-26 11:28（Asia/Shanghai）。
- 发布指针：`20261026T032433781Z-2ddc8ebe/ac-client-0.1.0+9aea70b-win64.zip`；latest.txt 时间 11:25:33，Managed/Ac.Boot.dll 时间 11:25:16。
- 命令：临时清空此测试进程的 `AC_UNITY` 后运行 `client/tools/frame-bench.ps1 -Mechanism player -WarmupFrames 120 -Frames 300 -Runs 3 -OutDir build/reports/client-remediation-initial -Out build/reports/client-remediation-initial/summary.json -TimeoutSeconds 120`。
- 清空 AC_UNITY 是为跳过脚本自动运行的 editor 诊断；没有启动 Unity 编辑器，没有构建，没有修改源码或生产环境。
- 使用工具自带 4p60sheep 场景，脚本以 `AC_SERVER=127.0.0.1:0` 保持离线；没有线上联机或公网验证。

### 结果：无法判定性能

第一轮未生成 JSON，后续轮次未执行。日志 `build/reports/client-remediation-initial/run-20261026-112828-player-1.log` 第24行起反复出现：

```text
InvalidOperationException: payload size mismatch: wrote 88 of 108
  at Ac.Boot.PlanBench+PacketEncoder.End ()
  at Ac.Boot.PlanBench.FeedMatchState (...)
  at Ac.Boot.PlanBench.Tick ()
  at Ac.Boot.FrameBenchDriver.Update ()
```

Player 已创建真实 D3D11 图形设备：NVIDIA GeForce RTX 3060 Laptop GPU，驱动 32.0.15.9597。设备初始化不等于性能通过；没有可报告的 draw、triangle、帧时间或 GC 采样数字。

源码检查时 `PlayerCount=4`，合成 MatchState 指定 `localPid=1`，但异常阻止有效 MatchState 完成，因此**尚未运行时确认 3 名队友**。不能用 scenario 四人的配置常量代替实际队友验证。

`FrameBenchDriver.CaptureScreenEvidence` 当前仅将截图纹理用于像素覆盖率计算后释放，并不落盘 PNG。此次又未完成采样，未取得可视截图证据。

## 最终版本

### 构建与实际运行

- 已补齐真实 MatchState 协议每玩家5字节升级字段，保持21B定长/总108B；生产与新增 `perf.plan_match_state_payload` 共用唯一 writer，修复前88/108红、修复后纯Decode与Unity自检绿。
- 最终发布：`20261026T033511195Z-20b54708/ac-client-0.1.0+9aea70b-win64.zip`，Mono，构建exit0；SHA256 `e032394aed77645e1381516f7df09cec019f30f47e2bce1ce2662cf677aab905`。
- 命令（仓库根）：`$env:AC_UNITY=''; powershell -NoProfile -ExecutionPolicy Bypass -File client/tools/frame-bench.ps1 -Mechanism player -WarmupFrames 120 -Frames 300 -Runs 3 -OutDir build/reports/client-remediation-final-retry`。
- 第一轮尝试 `client-remediation-final` 曾无JSON、没有明确异常，不当作通过；随后直接最小player探针成功产JSON，完整脚本重试获得三轮真实player与空场结果。没有编辑器/公网连接。

### 实测结果：性能门 FAIL，不是环境不可判定

| 300帧采样轮次 | P95(ms) | P99(ms) | 托管分配(B/帧) | Gen0 |
|---|---:|---:|---:|---:|
| 1 | 3.2092 | 3.5385 | 587.0933 | 0 |
| 2 | 3.0518 | 3.5602 | 573.44 | 0 |
| 3 | 3.0089 | 3.2673 | 559.7867 | 0 |

中位P95 **3.0518ms**，P99 **3.5385ms**，分配 **573.44B/帧 > 0**。工具exit1，`FRAME-BENCH FAIL`，未放宽预算。空场P95=2.9813ms，未触发无法判定地板；空场图形字段不可测，不虚报空场PASS。

图形预算三轮 drawCalls=63/197/63、triangles=28180/34819/28180、materials=8；引擎计数器不可用时使用羊显式提交+人物/场地剔除批处理前预算，各轮有不同计数来源，不能混称精确GPU工作量。60羊完整写入、未见池溢出；像素覆盖率为有效非空画面证据，但没有落盘截图或人工造型审查。人物模型数量与真实层级统计由完整Unity `presentation.remote_*` 测试覆盖（最终267项只有2项既有失败）。

分配来源尚未做本轮归因，不能凭新增模型的隔离零分配测试将其归零或直接认定全部来自新模型。真实2/4客户端联机、网络干扰、造型观感仍待验收。

原始日志、每轮JSON、空场JSON和summary：`build/reports/client-remediation-final-retry/`。完整Unity最终自检267项265通过，剩余两项已独立HEAD对照为既有失败；详见 `multiplayer-remediation-2026-10-26.md`。
