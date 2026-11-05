# Angry Chen

现役实现是独立的 **Unity 客户端 `client/`** 与 **C++ 权威服务器 `server/`**，两者通过 UDP 协议通信。

| 路径 | 用途 |
|---|---|
| [client/](client/README.md) | 团结引擎/Unity 工程根；客户端构建入口 `client/build.ps1` |
| [server/](server/README.md) | C++ 服务端工程根；开发、测试与部署说明 |
| [docs/](docs/README.md) | 共识、协议、计划与证据 |
| [运维手册](docs/运维手册.md) | 客户端构建、分发、配置与排障 |
| `deploy/`、`tools/` | 部署资产与仓库质量门脚本 |
| `data/`、`ac-*-data/` | 运行时数据目录（脚本按需重建，不入库） |

根目录不再保留空的 `Assets/` 历史目录。`build/`、`client/Build/`、`server/build/`、`client/Logs/` 都是**可再生的本地产物**，不入库；2026-11-05 清洗已删除全部历史产物，不要用历史目录判断最新播放器。旧 TypeScript 实现已移出，源码在 git 历史 `cea8adb^`。

## 仓库卫生与恢复

```powershell
# 默认 dry-run，只列出将删除的可再生产物；确认后加 -Apply
powershell -NoProfile -File tools/clean-artifacts.ps1
powershell -NoProfile -File tools/clean-artifacts.ps1 -Apply
```

- 默认保留 `client/Library`、`client/.codely-cli`、`client/.codely.packages`；后两者是 `client/Packages/manifest.json` 的本地 `file:` 依赖，删除会导致 Unity 无法解析包。
- 仓库外旧备份 `angry-chen-bak` / `angry-chen-fixture` / `angry-chen-permission-recovery` 已于 2026-11-05 删除；v1 源码从 git `cea8adb^` 恢复，步骤见 [docs/evidence/v1-frozen-reference.md](docs/evidence/v1-frozen-reference.md)。
- 当前证据入口：[docs/evidence/README.md](docs/evidence/README.md)。

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
