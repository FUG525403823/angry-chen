# C15 联合验收报告

> 状态：**部分验收**。构建与日志链路已在本机实测通过；需要服务端在场的六步 checklist 尚未执行（服务端链由另一条会话推进），因此 **C15 未打 `MC15` 标签**。

## 1. 已实测通过

环境：Tuanjie 2022.3.62t16 / commit `2bb9d18` / 本机 PlaybackEngines 只有 `win64_player_*_mono` 变体（无 `win64_il2cpp`）。

| 项 | §5/§6 要求 | 实测 | 判定 |
|---|---|---|---|
| 一条命令出包 | `pwsh -File client/build.ps1 -Backend auto` 退出码 0 | 退出码 0 | PASS |
| 末行 | `BUILD OK <归档名>` | `BUILD OK ac-client-0.1.0+2bb9d18-win64.zip` | PASS |
| 归档名 | `ac-client-<semver>+<sha7>-win64.zip` 逐字符一致 | 逐字符一致 | PASS |
| 后端 | IL2CPP 优先，缺变体时 Mono 兜底并标注 | `backend=mono (win64_il2cpp present=False)`，`manifest.json` 里 `backend=mono` | PASS（标注为 Mono） |
| 自包含 | 归档内含 exe 与 `_Data` | 158 条目，`angry-chen.exe` 1 个，`angry-chen_Data\` 存在，`*DoNotShip*` 0 个 | PASS |
| sha256 清单 | 与清单一致 | `latest.txt` = `ac-client-0.1.0+2bb9d18-win64.zip 81538ab7…`，与归档实算一致 | PASS |
| 版本行注入 | 日志首个 `version` 字段等于 `VersionInfo.VersionLine` | 由常驻用例 `c15.log_sink` 守着（首行注入版本行） | PASS |
| 版本行口径 | 与服务器行 `proto`/`MAJOR.MINOR` 判定规则 | 常驻用例 `c15.version_line` 覆盖五条规则（含 `+sha7` 不参与判定的正例） | PASS |

复现命令：

```
$env:AC_UNITY = "<编辑器可执行文件>"
pwsh -File client/build.ps1 -Backend auto
Get-Content client/Build/Windows64/latest.txt
```

## 2. 尚未验收（需要服务端在场）

六步 checklist（连接 → 大厅 → 对局 → 波次 → 结算 → 重连）每一步都要贴原始输出，且必须把**两侧版本行同时**写进本报告：

```
客户端行：ac-client 0.1.0+<sha7> proto=1     # 来源 Core/VersionInfo.cs
服务器行：ac_server 0.1.0 protocol=1 tick=50ms   # 来源 S01/S15 冻结口径
核对：proto == protocol 且 MAJOR.MINOR 相等（+<sha7> 与 tick=50ms 不参与）
```

在服务端可用之后，按 `docs/运维手册.md` 的"版本核对"一节执行，并把每步的原始输出（客户端日志行 + 服务器 `/metrics`/控制台输出）粘到本节下方。

## 3. 本轮发现并已修的发布链路问题

| 问题 | 现象 | 处理 |
|---|---|---|
| 归档含 `*_BurstDebugInformation_DoNotShip` | 发布包里带了 Unity 明确标注"不要发"的目录 | `build.ps1` 打包时排除，并新增"归档必须自包含"自检 |
| zip 条目分隔符 | 自检用 `/` 匹配 `_Data` 会假阴性（zip 里是 `\`） | 自检改用 `angry-chen_Data*` |
| **无 BOM 的 UTF-8 脚本含中文** | `build.ps1` 被 Windows PowerShell 5.1 按 ANSI 解码，中文字节解出引号 ⇒ 直接解析失败、构建根本没跑起来 | 脚本存为**带 BOM 的 UTF-8**；已同时修 `client/tools/frame-bench.ps1`，并把这一条写进运维手册故障表 |
