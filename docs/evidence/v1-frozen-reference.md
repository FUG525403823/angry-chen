# v1 冻结实现恢复说明

2026-11-05 仓库清洗删除了仓库外的 v1 备份与派生目录：

- `D:\projects\tmp\angry-chen-bak`（v1 TypeScript 全栈冻结副本）
- `D:\projects\tmp\angry-chen-fixture`（v1 可写派生 fixture 副本）
- `D:\projects\tmp\angry-chen-permission-recovery`（历史 ACL 恢复备份/撤销脚本）

这些目录不再存在。**v1 源码没有丢失**：它仍在当前 git 历史中，只是不在 HEAD 工作树。

## 恢复源码

v1 全栈实现（`packages/`、`tools/` 等）由提交 `cea8adb` 移出。其父提交 `cea8adb^` 含完整 v1 树：

```powershell
# 方式一：临时工作树（推荐）
git worktree add --detach C:\temp\angry-chen-v1 cea8adb^

# 方式二：只导出归档
New-Item -ItemType Directory -Force C:\temp\angry-chen-v1 | Out-Null
git archive cea8adb^ | tar -x -C C:\temp\angry-chen-v1
```

## 运行 v1 的依赖

备份目录里曾带 `node_modules`（其中含 pnpm 目录联接）。这些依赖**没有**进入 git；恢复后需要重新安装：

```powershell
Set-Location C:\temp\angry-chen-v1
pnpm install --frozen-lockfile
```

需要网络或可用的 pnpm store。安装完成前，v1 只能阅读代码，不能直接跑测试/服务。

## 边界

- 本次不恢复任何旧构建产物（`build/`、`dist/`、玩家包等）；需要时从 v1 源码重建。
- `angry-chen-permission-recovery` 删除后，历史 ACL 改动无法再按原脚本撤销；当前仓库 ACL 已可用。若未来需要审计权限，按当前 ACL 重新取证。
- 本说明不改变 v2 的当前实现与验收口径；v2 代码、协议和计划全部保留。
