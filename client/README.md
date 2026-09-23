# client —— Unity 客户端

本目录是 v2 重构后的**客户端工程根**（Unity 工程）。它与 `server/`（C++ 权威服务器）**完全分离**，两者只通过 [ADR-009](../docs/00-共识/ADR/ADR-009-UDP传输与协议重构.md) 冻结的 UDP 协议通信，不共享源码。

| 入口                                 | 位置                                                             |
| ------------------------------------ | ---------------------------------------------------------------- |
| 线性制作计划（C01 → C15）            | [docs/plans-v2/client/](../docs/plans-v2/client/)                |
| 计划索引、跨链依赖与推荐执行顺序     | [docs/plans-v2/README.md](../docs/plans-v2/README.md)            |
| 技术前提（引擎/版本/依赖白名单）     | [docs/03-技术选型-v2.md](../docs/03-技术选型-v2.md)              |
| 零外部素材与问界额标策略             | [ADR-003](../docs/00-共识/ADR/ADR-003-美术素材与问界标识策略.md) |
| 跨语言确定性契约                     | [ADR-010](../docs/00-共识/ADR/ADR-010-跨语言确定性与对拍.md)     |
| 旧 TypeScript 客户端（冻结对照实现） | `packages/client/`                          |

**当前状态**：只有本文件与 `.gitignore`。Unity 工程文件（`Assets/`、`Packages/`、`ProjectSettings/`）由 C01 创建，**不预先存在**。

硬约束（来自 ADR-003 / ADR-008 / ADR-010）：

1. 零外部素材：网格、材质、shader、音频全部代码生成；**不导入 TMP Essentials 等素材包**，UI 文本用系统字体（`Font.CreateDynamicFontFromOSFont`）。
2. 依赖白名单：只用 Unity 官方包（URP；输入用内置 Input，不引 Input System），无第三方包、无 Asset Store 素材。
3. 模拟相关代码（预测/和解/数学）必须遵守 ADR-010 的确定性规则，并与服务端吃同一批对拍向量。
4. `Library/`、`Temp/`、`Build*/` 等生成目录不入库（见 `.gitignore`）。
