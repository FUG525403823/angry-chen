# Angry Chen

现役实现是独立的 **Unity 客户端 `client/`** 与 **C++ 权威服务器 `server/`**，两者通过 UDP 协议通信。

| 路径 | 用途 |
|---|---|
| [client/](client/README.md) | 团结引擎/Unity 工程根；客户端构建入口 `client/build.ps1` |
| [server/](server/README.md) | C++ 服务端工程根；开发、测试与部署说明 |
| [docs/](docs/README.md) | 共识、协议、计划与证据 |
| [运维手册](docs/运维手册.md) | 客户端构建、分发、配置与排障 |
| `deploy/`、`tools/`、`data/` | 部署资产、仓库质量门与共享数据 |

根目录的空 `Assets/` 及本机 `ac-*-data/` 是历史/本地工作目录，不是现役 Unity 工程或发布入口。根 `build/` 按 `reports/<scenario>/<run-id>/`、`runtime/<run-id>/`、`scratch/` 归纳跨端报告、运行数据和临时实验；`server/build/` 保留 CMake 构建树，`client/Build/` 承担客户端 staging 与发布。旧产物不自动清理；不要从历史目录判断最新播放器。旧 TypeScript 实现已移出，历史对照见备份仓库 `angry-chen-bak`。

## 客户端发布

在仓库根执行：

```powershell
powershell -NoProfile -File client/build.ps1 -Target Windows64 -Output Build/Windows64 -Backend auto
```

`AC_UNITY` 指向编辑器可执行文件；`-Output` 相对 `client/` 根，只接受目录，不接受 exe。每次构建生成独立 `client/Build/Windows64/stage/<run-id>/`，只从该 stage 打包。发布归档、SHA256 与 manifest 在 `release/<run-id>/`，最新入口是 `client/Build/Windows64/release/latest.txt`（`<run-id>/<归档名> <sha256>`，路径相对 release）。校验通过才原子更新指针，失败不破坏旧发布。成功末行 `BUILD OK <归档名>`。

发布包仅带 `server.txt.example`，不含真实 `server.txt`。解压后按运维手册配置连接地址。

无需 Unity 的脚本回归及质量门：

```powershell
powershell -NoProfile -File client/tools/test-build.ps1
powershell -NoProfile -File client/tools/test-player-discovery.ps1
powershell -NoProfile -File tools/remote-pose-check.ps1
powershell -NoProfile -File tools/sheep-state-check.ps1
node tools/check-docs.mjs
node tools/check-assets.mjs
```
