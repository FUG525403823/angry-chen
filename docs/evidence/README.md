# docs/evidence 索引

本目录保存**当前仍需被 ADR、计划或验收引用的证据**。2026-11-05 仓库清洗后，已完成里程碑的旧证据已移出工作树（可从 git 历史恢复），本页是唯一入口。

## 1. 当前 P1（进行中）

- [enhancement-p1-agent-handoff.md](enhancement-p1-agent-handoff.md)：P1 代理交接、已完成项与下一轮入口。
- [enhancement-p1-checklist.md](enhancement-p1-checklist.md)：P1 验收清单与门禁口径。
- [enhancement-p1-human-playtest.md](enhancement-p1-human-playtest.md)：人工试玩记录与待复测项。
- [enhancement-p1-ui-audit.md](enhancement-p1-ui-audit.md)：UI 审计结论与截图批次说明。
- [enhancement-p1-performance-checkpoint.md](enhancement-p1-performance-checkpoint.md)：帧预算/分配检查点。
- [enhancement-p1-expedition-perf-scope.md](enhancement-p1-expedition-perf-scope.md)：远征性能范围与判据。
- [p1-network-followups.md](p1-network-followups.md)：P1 网络后续项。

P1 的目标与范围见 [../enhancement-plan.md](../enhancement-plan.md)。

## 2. P0 与工程基线

- [enhancement-p0.md](enhancement-p0.md)：P0 小范围验收与原始本地报告说明。
- [enhancement-p0-closeout.md](enhancement-p0-closeout.md)：P0 收口结论。
- [enhancement-resume.md](enhancement-resume.md)：断点续跑记录、构建/测试命令与证据路径。
- [env-bootstrap.md](env-bootstrap.md)：本机工具链/环境变量基线。
- [plan-audit.md](plan-audit.md)：计划审计与台账口径。
- [codely-dependency-review.md](codely-dependency-review.md)：Codely 依赖准入复核（ADR-021）。

## 3. 客户端 v2 验收

- [client-c01-acceptance.md](client-c01-acceptance.md)、[client-c02-acceptance.md](client-c02-acceptance.md)、[client-c03-acceptance.md](client-c03-acceptance.md)：C01–C03 基线、复审与传输验收。
- [client-v2-acceptance.md](client-v2-acceptance.md)：C14/C15 联合验收。
- [client-v2-frame.md](client-v2-frame.md)：帧预算测量与 CPU 回路证据。
- [client-v2-frame-acceptance.md](client-v2-frame-acceptance.md)：帧门禁裁定（含 Null Device 限制）。
- [client-v2-remaining-work.md](client-v2-remaining-work.md)：v2 剩余工作清单。
- [client-plan-alias.md](client-plan-alias.md)：计划别名对照。

## 4. 服务器 v2 验收与性能

- [server-v2-acceptance.md](server-v2-acceptance.md)：S01–S15 汇总验收。
- [server-perf-4p2min.md](server-perf-4p2min.md)：4 人 2 分钟性能场景与阈值。

## 5. Fixtures

- [fixtures/README.md](fixtures/README.md)：协议/目标选择/重放 fixture 的版本、字节门与缺口登记。
- `fixtures/legacy/targeting-v1/`：ADR-019 之前的 targeting 证据档案。

## 6. 2026-11-05 移出的旧证据

以下文档已从工作树删除，**内容仍完整保存在 git 历史**。需要时按文件名恢复：

```powershell
git log --all --oneline -- docs/evidence/<文件名>
git show <commit>:docs/evidence/<文件名>
```

- `client-c04-acceptance.md` … `client-c13-acceptance.md`（10 份客户端里程碑验收）
- `client-audit-consolidated.md`
- `client-remediation-player-validation.md`
- `multiplayer-remediation-2026-10-26.md`
- `movement-stutter-repair.md`
- `obvious-defects-repair.md`
- `settings-reload-repair.md`
- `sheep-model-rework.md`
- `s16-upgrades-ammo-crates.md`
- `server-targeting-2026-10-26.md`
- `server-perf-remote-2026-10-12.md`
- `server-perf/2026-10-26-remediation-windows.md`

删除口径：这些记录对应的里程碑已完成，结论已被 ADR/计划或当前 P1 文档吸收；保留它们只会让读者把历史路径当成当前证据。

## 7. 本地产物说明

2026-11-05 清洗同时删除了仓库内 `build/**`、`client/Build/**`、`server/build/**`、`client/Logs/**`。本目录文档中出现的这些路径是**历史输出位置**，不是仍在磁盘上的文件；需要时按对应文档的命令重新生成。所有结论、哈希和原始日志摘录以文档正文为准。

## 8. 相关文档

- [../README.md](../README.md)：文档地图与质量门。
- [v1-frozen-reference.md](v1-frozen-reference.md)：已删除的仓库外 v1 备份的恢复方式。
