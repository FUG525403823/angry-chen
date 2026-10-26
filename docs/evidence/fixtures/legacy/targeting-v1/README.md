# targeting-v1：ADR-019 之前的证据档案

这10条 JSON 是迁移前活动文件的逐字节副本；不可自动枚举为活动测试，也不可用新服务端规则重写。文件名及 schema `version: 2` 保留原样。`targeting.cpp.txt` 是旧实现取证副本（保留原字节、BOM/EOL）；忽略 BOM/EOL 后与提交 `9aea70b1ad39f1ab7c243d62354471490e7b5f23` 的 `server/src/ai/targeting.cpp` 一致。`.txt` 后缀避免被 C++ 源码自动构建。

旧10条真值来源仍为冻结 TS 导出工具 `tools/export-fixtures.mjs`；旧服务端对象在迁移前运行同一测试得到21/21。活动规则升级为 targeting-v2，详见上两级 README §8、ADR-019 及 `docs/evidence/server-targeting-2026-10-26.md`。外部版本记录与 SHA256 在该 evidence 中；schema 版本不变不代表行为版本不变。

C++ 通过显式名称加载顶层文件；C# `FixtureLoader.Load` / `FixturePredictSuite.DiscoverUnrecognized` 仅扫描顶层 `*.json`。这个子目录不会重复执行，也没有新增忽略/跳过测试分支。

若要重新从冻结 TS 生成用于审查的旧向量，应显式 `--out server/build/targeting-frozen-ts`，再与本目录比对；不要覆盖本档案或活动向量。
