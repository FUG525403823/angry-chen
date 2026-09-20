# 愤怒的陈sir vs 发疯的羊群

一款**浏览器里的 1–4 人协作波次生存射击游戏**。玩家扮演愤怒的陈sir，在被疯草致疯的羊群围攻中守住牧场——每一只羊的额头都带着「问界」标志，越残血，额标越亮。

- 服务端：Node + TypeScript 权威服务器（20Hz tick，二进制 WebSocket 协议，差分快照）
- 客户端：Vite + three.js，第一人称，客户端预测 + 服务器和解 + 回滚命中判定
- 美术与音效：**全部程序化生成**（零外部素材，克隆下来离线即可构建与运行）
- 持久化：本地 NDJSON 战绩与排行榜

> 当前状态：**M0 已完成**（P01 项目基线搭建）——三包骨架可跑、质量门全绿。按 [docs/plans/](docs/plans) 的 P01 → P10 顺序继续施工。

## 文档怎么读

| 想了解                 | 看这里                                               |
| ---------------------- | ---------------------------------------------------- |
| 全部文档地图与执行顺序 | [docs/README.md](docs/README.md)                     |
| 用了什么技术、为什么   | [docs/01-技术选型.md](docs/01-技术选型.md)           |
| 要做成什么样、验收标准 | [docs/02-需求分析.md](docs/02-需求分析.md)           |
| 术语表（先读）         | [docs/00-共识/CONTEXT.md](docs/00-共识/CONTEXT.md)   |
| 工程约束与质量门       | [docs/00-共识/工程约定.md](docs/00-共识/工程约定.md) |
| 十份线性施工计划       | [docs/plans/](docs/plans)（P01 → P10，顺序执行）     |

## 施工路线（十个里程碑）

| 里程碑 | 文档                                            | 完成后能做什么                                                      |
| ------ | ----------------------------------------------- | ------------------------------------------------------------------- |
| M0     | [P01](docs/plans/P01-项目基线搭建.md)           | 骨架跑起来：`pnpm check` 全绿、页面显示协议版本、服务端健康检查通过 |
| M1     | [P02](docs/plans/P02-核心模拟内核.md)           | 无头跑一局确定性模拟，同种子结果逐位复现                            |
| M2     | [P03](docs/plans/P03-网络协议与权威服务器.md)   | 脚本客户端能进房、收 20Hz 快照、上报命令                            |
| M3     | [P04](docs/plans/P04-客户端渲染与输入.md)       | 浏览器里第一人称走动，看到灰盒牧场与羊                              |
| M4     | [P05](docs/plans/P05-客户端预测与服务器和解.md) | 移动零延迟感，远端平滑，命中带回滚补偿                              |
| M5     | [P06](docs/plans/P06-战斗系统与武器.md)         | 三把武器真的能打死羊；倒地可救援；怒气狂暴                          |
| M6     | [P07](docs/plans/P07-羊群AI与波次导演.md)       | 四种羊成形进攻，波次难度曲线生效，每 5 波羊王                       |
| M7     | [P08](docs/plans/P08-大厅匹配与对局流程.md)     | 完整闭环：开房 → 十波 → 结算 → 排行榜                               |
| M8     | [P09](docs/plans/P09-美术音效与问界标识.md)     | 程序化美术与音效、问界额标、HUD 成品化                              |
| M9     | [P10](docs/plans/P10-质量保障发布与运维.md)     | 压测、日志、指标、发布与运维手册                                    |

## 快速开始

```bash
pnpm install          # 安装依赖（pnpm 11；构建设置见 pnpm-workspace.yaml）
pnpm check            # 质量门：typecheck + lint + test + 文档链校验
pnpm dev              # 并行启动服务端(8787)与客户端(5173)
pnpm build:client     # 构建客户端产物
```

- 服务端健康检查：`curl -s http://localhost:8787/health`
- 客户端：浏览器打开 Vite 输出的 `http://localhost:5173`（页面应显示 `protocolVersion 1 / tickMs 50`）
- 端口被占用时：`PORT=9000 pnpm dev:server`；Vite 端口见其自身输出。

## 环境要求（已实测）

| 项         | 版本                 | 说明                                                           |
| ---------- | -------------------- | -------------------------------------------------------------- |
| Node.js    | 24.14.1              | 本机已验证；`vite@8` 要求 `^20.19` 或 `>=22.12`                |
| pnpm       | 11.22.0              | workspace 单仓                                                 |
| TypeScript | 5.9.3                | 刻意不用 7.x：`typescript-eslint@8.70` 的 peer 上限是 `<6.1.0` |
| 浏览器     | Edge / Chrome 最新版 | 本机只有 Edge；Chrome 未安装                                   |

不需要 Docker。Docker 配置文件作为**可选**产物交付并标注"仅静态审查"（本机无 docker，无法构建验证）。

## 仓库结构（目标形态，由 P01 建立）

```
packages/shared/   纯逻辑：数学、模拟、AI、战斗、协议编解码、配置表（禁 three / DOM / node:* / Math.random）
packages/server/   Node 权威服务器：房间、会话、传输适配器、HTTP 健康检查与指标
packages/client/   浏览器客户端：渲染、输入、UI、音频、预测
tools/             无依赖脚本：文档链校验、协议探针、压测机器人
docs/              全部文档
```

## 已知边界

- 不做 PVP、账号系统、云排行、多张地图、掉落经济、观战回放、移动端与手柄（理由见 [需求分析第 8 节](docs/02-需求分析.md)）。
- 「问界」额标为**原创风格化标识**，不使用真实商标素材（见 [ADR-003](docs/00-共识/ADR/ADR-003-美术素材与问界标识策略.md)）。

## 运维端点与协议探针

```bash
pnpm dev:server                       # 默认 8787（PORT/HOST/MAX_ROOMS 可覆盖）
curl -s localhost:8787/health         # JSON：协议版本、房间数、连接数、玩家数、tick 数
curl -s localhost:8787/metrics        # Prometheus 文本：tick 抖动 P95、快照字节均值、非法帧、限流等
node tools/probe.mjs --clients 2 --duration 10           # 两个脚本客户端进同一房间，打印快照速率/带宽/RTT
node tools/probe.mjs --clients 1 --duration 3 --flood     # 洪泛：预期被 Error(4) 断开
node tools/probe.mjs --clients 2 --duration 3 --bad-frame # 恶意帧：未知 opcode/截断帧/9KB 帧
node tools/probe.mjs --clients 1 --duration 5 --room ABCD # 指定房间码并发观察
```
