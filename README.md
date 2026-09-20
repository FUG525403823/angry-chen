# 愤怒的陈sir vs 发疯的羊群

一款**浏览器里的 1–4 人协作波次生存射击游戏**。玩家扮演愤怒的陈sir，在被疯草致疯的羊群围攻中守住牧场——每一只羊的额头都带着「问界」标志，越残血，额标越亮。

- 服务端：Node + TypeScript 权威服务器（20Hz tick，二进制 WebSocket 协议，差分快照）
- 客户端：Vite + three.js，第一人称，客户端预测 + 服务器和解 + 回滚命中判定
- 美术与音效：**全部程序化生成**（零外部素材，克隆下来离线即可构建与运行）
- 持久化：本地 NDJSON 战绩与排行榜

> 当前状态：**P01–P09 已完成（M0–M8）**，P10 质量保障与运维进行中（M9）。
> 玩法、协议、数值已冻结；P10 交付的是压测工具、可观测性、发布运维资产与验收报告。
> 验证状态以 [docs/验收报告.md](docs/验收报告.md) 为准：Docker 与反代配置标注为**静态审查**。

## 快速开始（从零到玩上一局，5 条命令 + 1 次打开浏览器）

```bash
pnpm install                                      # 1. 安装依赖（pnpm 11.22.0，Node ≥ 24）
pnpm check                                        # 2. 质量门：类型 + 风格 + 测试 + 文档链
pnpm dev                                          # 3. 并行起服务端(8787)与客户端(5173)
curl -s http://localhost:8787/health              # 4. 另一个终端：确认服务端活着
# 5. 浏览器打开 http://localhost:5173，点一下画面锁定鼠标即可开始
```

- 端口：**服务端 8787**（HTTP + WebSocket），**客户端 5173**（Vite dev）。本机 8080 常被其它软件占用，请避开。
- 想验证构建产物与体积预算：`pnpm build:client`（输出 `packages/client/dist`，内置 gzip 体积断言）。
- 上面 1–4 条命令与 `pnpm build:client` 均已在本机实测（见 [docs/验收报告.md](docs/验收报告.md)）。

## 常见问题（FAQ）

| 症状                       | 原因                                                                                  | 处理                                                                                                                        |
| -------------------------- | ------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------- |
| 报端口占用（8787）         | 已有服务端实例或其它程序占用                                                          | 换端口起服务端：`PORT=9000 pnpm dev:server`；客户端连接地址跟着改：打开 `http://localhost:5173/?server=ws://127.0.0.1:9000` |
| 报端口占用（5173）         | Vite 固定 strictPort，不会自动换端口                                                  | `pnpm --filter @ac/client exec vite --port 5174`，再打开 `http://localhost:5174`                                            |
| 鼠标不锁、视角不能转       | Pointer Lock 必须由用户手势触发，且页面失焦/按 Esc 后会被释放                         | 用鼠标点一下游戏画面；若浏览器弹出「已退出全屏锁定」提示，点「允许」后再点一次画面                                          |
| 完全没有声音               | 浏览器禁止在用户交互前播放音频（AudioContext 处于 suspended）                         | 先点一下画面或按任意键；客户端会在首次交互时 `resume()` 音频上下文（`packages/client/src/audio/synth.ts`）                  |
| 想重置本地战绩/排行榜      | 战绩落盘在 `DATA_DIR`（默认 `./data/matches.ndjson`）                                 | 停服后删目录：`rm -rf data`；重启服务即从空战绩开始。生产用 systemd 时是 `/var/lib/angry-chen`                              |
| 想重置昵称/设置/上次房间码 | 浏览器 localStorage，三个键：`ac.settings.v1`、`ac.nickname.v1`、`ac.lastRoomCode.v1` | DevTools → Application → Local Storage → 删除这三个键（或控制台执行 `localStorage.removeItem('ac.nickname.v1')`）           |
| 排行榜接口 404 / 无数据    | 还没跑完一整局，`matches.ndjson` 为空                                                 | 打完一波即可；先用 `curl -s localhost:8787/api/leaderboard` 确认接口通                                                      |

## 文档怎么读

| 想了解                 | 看这里                                               |
| ---------------------- | ---------------------------------------------------- |
| 全部文档地图与执行顺序 | [docs/README.md](docs/README.md)                     |
| 用了什么技术、为什么   | [docs/01-技术选型.md](docs/01-技术选型.md)           |
| 要做成什么样、验收标准 | [docs/02-需求分析.md](docs/02-需求分析.md)           |
| 术语表（先读）         | [docs/00-共识/CONTEXT.md](docs/00-共识/CONTEXT.md)   |
| 工程约束与质量门       | [docs/00-共识/工程约定.md](docs/00-共识/工程约定.md) |
| 怎么发布、怎么排障     | [docs/运维手册.md](docs/运维手册.md)                 |
| 每条验收标准的证据     | [docs/验收报告.md](docs/验收报告.md)                 |
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

## 环境要求（已实测）

| 项         | 版本                 | 说明                                                           |
| ---------- | -------------------- | -------------------------------------------------------------- |
| Node.js    | 24.14.1              | 本机已验证；`vite@8` 要求 `^20.19` 或 `>=22.12`                |
| pnpm       | 11.22.0              | workspace 单仓                                                 |
| TypeScript | 5.9.3                | 刻意不用 7.x：`typescript-eslint@8.70` 的 peer 上限是 `<6.1.0` |
| 浏览器     | Edge / Chrome 最新版 | 本机只有 Edge；Chrome 未安装                                   |

不需要 Docker。`Dockerfile`、`docker-compose.yml`、`deploy/**` 是**可选**产物，
本机没有 docker/nginx/caddy，因此这些文件统一标注**静态审查**（未构建、未运行），
首次真实部署前请按 [docs/运维手册.md](docs/运维手册.md) 的「首次验证清单」逐条核对并回填证据。

## 仓库结构（目标形态，由 P01 建立）

```
packages/shared/   纯逻辑：数学、模拟、AI、战斗、协议编解码、配置表（禁 three / DOM / node:* / Math.random）
packages/server/   Node 权威服务器：房间、会话、传输适配器、HTTP 健康检查与指标
packages/client/   浏览器客户端：渲染、输入、UI、音频、预测
tools/             无依赖脚本：文档链校验、协议探针、压测机器人
deploy/            反代与守护样例：Caddyfile、nginx.conf、systemd、PM2（静态审查）
docs/              全部文档
```

## 已知边界

- 不做 PVP、账号系统、云排行、多张地图、掉落经济、观战回放、移动端与手柄（理由见 [需求分析第 8 节](docs/02-需求分析.md)）。
- 「问界」额标为**原创风格化标识**，不使用真实商标素材（见 [ADR-003](docs/00-共识/ADR/ADR-003-美术素材与问界标识策略.md)）。
- 容量上限明确为**单机 + 反代 + 日志文件 + `/metrics`**；扩容路径写在 [运维手册](docs/运维手册.md)，不在本期实现。

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

压测与 soak（`tools/bots.mjs`、`tools/soak.mjs`）与验收证据（`docs/evidence/`）由 P10 交付，
使用方法与阈值判定见 [docs/运维手册.md](docs/运维手册.md) §4；证据齐备前不要对外宣称已达标。
