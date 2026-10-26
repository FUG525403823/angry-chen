# 目录、队友模型与多人羊整改验证（2026-10-26）

## 范围与保护

按人类批准计划实施。唯一客户端工程仍为 `client/`；未搬迁或清理未知旧产物，未操作线上，未提交。原有 `BuildInfo.g.cs`、`manifest.json`、`ProjectSettings.asset` 和 `server-perf-remote-2026-10-12.md` 保留。客户端构建暂时写入 BuildInfo 后按原始字节恢复。

## 实现

- 客户端发布独立 `stage/<run-id>` / `release/<run-id>`；归档只包含本次 stage，排除真实 server.txt，校验成功后原子更新 latest。日志按 run-id 隔离。帧基准只发现 latest 对应成功 stage；显式 Player 优先。
- `RemotePlayerVisuals` 程序化陈sir、统一持枪、位置与朝向、步态、倒地，排除本机，按 ID + Lifetime 池化并释放 Unity 资源。真实模型几何纳入剔除/批处理前预算，不冒充 GPU draw calls。
- SnapshotView 保留原始线差分查询，额外物化声明基线的完整渲染历史；缺基线不污染镜像，等待全量恢复。实体生命周期隔离，空历史清除旧视图，自动重连重置播放时钟。
- 播放时钟以 0.9–1.1 倍速吸收普通偏差，普通到包不回拨；大于既有 500ms 阈值允许显式重锚。无未来样本不外推羊。
- 羊的死亡时间/额标平滑按实体身份保存，不再按本帧绘制槽位串用，额标依真实 dt 更新。
- 服务端多人选敌使用一致评分和滞回；零/单玩家保留旧数值路径。人类另行批准版本化10条受影响的四玩家向量，旧数据和旧实现保存于 fixtures/legacy/targeting-v1，不改变 schema 或线协议。

## 实际验证

| 检查 | 实际结果 |
|---|---|
| `tools/remote-pose-check.ps1` | 13/13 PASS；同一 C# 方法也注册 Unity 自检（另含连续full保留本机预测/平滑和空会话清视图）。覆盖固定 seed 内存抖动/乱序/丢链/集中到达、两羊稀疏差分、历史滚动、全量恢复、生命周期；不是公网测试 |
| `tools/sheep-state-check.ps1` | 5/5 PASS；真实 SheepVisuals/Pool/Mesh，Unity 数值桩，含重排红→绿和零分配断言 |
| `tools/movement-check.ps1` | 16/16 PASS；未放宽原断言，修复离线桩与抽取范围漂移 |
| `client/tools/test-build.ps1` | PASS；隔离、归档、路径、BuildInfo恢复、构建/归档失败时保留旧发布 |
| `client/tools/test-player-discovery.ps1` | PASS；成功 stage、失败 stage 排除、旧exe排除、损坏指针/归档拒绝、显式覆盖 |
| 客户端9程序集离线编译 | ALL PASS；存在原有编译 warnings |
| Unity无头完整自检（新版fixtures已落盘） | 最终267用例，265通过，2失败；不是整套绿（最后轮含基准载荷、重连、full预测保护和空会话回收）。见下方基线对照 |
| `node tools/check-docs.mjs` | PASS |
| `node tools/export-trig-table.mjs --check` | PASS；Resources镜像逐字节一致 |
| `node tools/check-assets.mjs` | FAIL：用户原有 manifest 的 `cn.tuanjie.codely.bridge` 不在白名单；未删依赖或放宽白名单 |
| 服务端AI/fixture | 36/36、21/21 PASS；新向量重算10/10，旧10条归档；详见 server-targeting 证据 |
| Windows真实客户端构建 | 最终20261026T033511195Z-20b54708，Mono，exit0，归档哈希校验通过；三轮真实player帧P95中位3.0518ms，但分配573.44B/帧超零分配门，性能FAIL，详见player证据 |
| Windows服务端构建/临时安装 | PASS，安装名保留.exe；旧构建启动失败后，在全新 `build/scratch/server-clean` 显式MinGW15.2工具链构建4目标成功，真实socket1/1及完整CTest底层561/561 PASS（10.58s）。干净构建正常2分钟gate PASS，G3=0反向门仅G3超限如期exit1，5分钟soak PASS（300.002s；CPU均值2.811%，RSS斜率0.042MB/min，事件丢弃/tick跳过/未捕获异常均0）；不以旧启动失败冒充负向门通过 |

### 两个客户端既有失败：独立 HEAD 对照

`powershell -NoProfile -ExecutionPolicy Bypass -File tools/client-baseline-check.ps1` 连续对照通过：HEAD `9aea70b1ad39f1ab7c243d62354471490e7b5f23` 与工作区均产生同样失败。退出0表示对照一致，不表示测试通过。

- `boot.hud_ammo_rage_from_match_state`: expected42 actual0。第一包本人怒气42通过，第二包只有他人/localPid0时生产先清零，旧测试期望保留42。本轮不擅改无关玩法/测试语义。
- `codec.roundtrip`: expected3 actual5。11条共享codec fixture通过后，旧反例把已分配的UpgradeSelect(type12)当未知类型，实际非法flags返回BadValue。未修改合法type12协议。

原始编译/断言/源哈希：`build/scratch/client-baseline/{HEAD,Worktree}/`。Unity日志 `client/Logs/selftest.log`；移动报告 `build/movement-stutter/results.json`。

## 未闭合验收与取舍

- 服务端差分基线是上一次成功编码而非ACK：漏掉一个delta后会拒绝依赖链，可能等待约2s周期全量。该策略保证状态不被错误基线污染，不等于已解决丢包时冻结；改善应单独设计有界恢复/基线协议，不在此伪造成功。
- 初轮真实player基准发现原有MatchState基准写入器漏5字节/玩家，已修复、补回归并最终重建；真实三轮性能门因托管分配573.44B/帧超零分配预算失败，尚未归因，不宣称性能达标。
- 真实2/4客户端联机、网络干扰、人物造型观感、GPU实际预算与公网羊抽搐仍需人工验收；无头与内存回放不能替代。
- 引擎验证解析原有本地Codely依赖产生的 `packages-lock.json` 副作用已恢复，`git diff --quiet`确认回到原先干净状态；manifest保持用户原始修改，不夹带新依赖。

关联：[ADR-019](../00-共识/ADR/ADR-019-羊选敌统一评分与滞回.md)、[ADR-020](../00-共识/ADR/ADR-020-完整渲染历史与单调播放时钟.md)、[服务端选敌](server-targeting-2026-10-26.md)、[player验证](client-remediation-player-validation.md)、[服务端性能](server-perf/2026-10-26-remediation-windows.md)。
