# 愤怒的陈sir vs 发疯的羊群

一款**浏览器里的 1–4 人协作波次生存射击游戏**。玩家扮演愤怒的陈sir，在被疯草致疯的羊群围攻中守住牧场——每一只羊的额头都带着「问界」标志，越残血，额标越亮。

- 服务端：Node + TypeScript 权威服务器（20Hz tick，二进制 WebSocket 协议，差分快照）
- 客户端：Vite + three.js，第一人称，客户端预测 + 服务器和解 + 回滚命中判定
- 美术与音效：**全部程序化生成**（零外部素材，克隆下来离线即可构建与运行）
- 持久化：本地 NDJSON 战绩与排行榜

> 当前状态：**M0–M8（P01–P09）已完成**，M9（P10 质量保障、发布与运维）施工中。
> 玩法、协议、数值已冻结；**每条交付物的验证状态以本文件「交付物与验证状态」与
> [docs/验收报告.md](docs/验收报告.md) 为准**，Docker 与反代配置一律标注**静态审查**。

## 从零到跑通（≤10 步）

```bash
# 1. 安装依赖（Node ≥ 24、pnpm 11.22.0；需要执行安装脚本的只有 esbuild，已在 pnpm-workspace.yaml 放行）
pnpm install
# 2. 质量门：类型 + 风格 + 测试 + 文档链（唯一入口，CI 与本地都用它）
pnpm check
# 3. 一条命令并行起服务端（8787，HTTP + WebSocket）与客户端（Vite dev，5173）
pnpm dev
# 4. 另开一个终端，确认服务端活着（期望 {"status":"ok","protocolVersion":2,...}）
curl -s http://localhost:8787/health
```

5. 浏览器窗口 A 打开 `http://localhost:5173`，填昵称后点**「创建房间」**，记下页面显示的 **4 位房间码**（字母表 `ABCDEFGHJKMNPQRSTUVWXYZ23456789`，不含 `0/1/I/L/O`）。
6. 浏览器窗口 B 打开同一个地址（建议用无痕窗口，避免和 A 共用 localStorage），输入 A 的房间码后点**「加入房间」**。
7. 两个窗口都点**「准备」**，房主窗口再点**「开始对局」**。
8. 鼠标点一下游戏画面**锁定指针**即可移动、开火；出现第 1 波的羊就说明整条链路通了。

- 也可用深链接省掉手动输入：`http://localhost:5173/?room=<房间码>`；换服务端地址用 `?server=ws://127.0.0.1:9000`。
- 第 1–4 条命令与 `pnpm dev` 的双进程形态在本机实测过；`pnpm check` 是否全绿取决于并行施工的 `tools/**`，
  逐项证据与阻塞项见 [docs/验收报告.md](docs/验收报告.md)。

## 常见问题

| 症状                       | 原因                                                                                 | 处理                                                                                                        |
| -------------------------- | ------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| 端口 8787 被占用           | 已有服务端实例，或本机其它软件占用（8080 尤其常见）                                  | 换端口起服务端：`PORT=9000 pnpm dev:server`，客户端打开 `http://localhost:5173/?server=ws://127.0.0.1:9000` |
| 端口 5173 被占用           | Vite 配置了 `strictPort: true`，不会自动换端口                                       | `pnpm --filter @ac/client run dev -- --port 5174`，再打开 `http://localhost:5174`                           |
| 鼠标不锁、视角不能转       | Pointer Lock 必须由用户手势触发；页面失焦、按 Esc 或被其它窗口抢焦点后会释放         | 用鼠标点一下游戏画面；浏览器若弹出「已退出指针锁定」提示，点「允许」后再点一次画面                          |
| 完全没有声音               | 浏览器自动播放策略禁止在首次用户交互前启动 `AudioContext`                            | 先点一下画面或按任意键；音频在首次交互时才解锁（`packages/client/src/audio/synth.ts`）                      |
| 想重置本地战绩/排行榜      | 战绩落盘在 `DATA_DIR`（默认 `./data/matches.ndjson`）                                | 停服后删目录：`rm -rf data`；用 systemd/PM2 部署时改删 `/var/lib/angry-chen`，然后重启服务                  |
| 想重置昵称/设置/上次房间码 | 浏览器 `localStorage`（键 `ac.nickname.v1`、`ac.lastRoomCode.v1`、`ac.settings.v1`） | DevTools → Application → Local Storage 删除对应键，或执行 `localStorage.removeItem('ac.nickname.v1')` 等    |
| 排行榜接口空/404           | 还没打完整一局（`DATA_DIR/matches.ndjson` 为空），或 `DATA_DIR` 不可写               | 先打完一局；用 `curl -s localhost:8787/api/leaderboard` 确认接口通；检查 `DATA_DIR` 目录权限与磁盘          |

## 交付物与验证状态（P10 §3 / §5.5）

验证状态只有三种：**已验证**（有可复现命令与输出）、**静态审查**（无环境验证）、**未验证**。

| 交付物                                                     | 验证状态                                | 依据 / 缺失说明                                                                                 |
| ---------------------------------------------------------- | --------------------------------------- | ----------------------------------------------------------------------------------------------- |
| `README.md`、`docs/运维手册.md`、`docs/验收报告.md`        | 已验证                                  | `node tools/check-docs.mjs` 退出码 0（本轮本机运行）                                            |
| `.env.example`                                             | 已验证                                  | 逐变量对照 ADR-004 与 `packages/server/src/main.ts` 的真实读取点                                |
| `tools/probe.mjs`                                          | 已验证                                  | P03 §6.1 实测记录：快照 20.39/s、avg 46.9 B、洪泛被 Error(4) 断开                               |
| `packages/client/vite.config.ts`（体积预算）               | 已验证                                  | 构建插件断言 gzip JS ≤ 1.5MB；P09 §6.1 记录 175,547 B                                           |
| `packages/server/src/{metrics,log,http}.ts`                | 已验证                                  | P03 §6.1：`/metrics` 含 `ac_tick_jitter_ms_p95` 等指标、结构化 JSON 行日志                      |
| `tools/bots.mjs` / `tools/soak.mjs` / `tools/report.mjs`   | 未验证                                  | 文件存在，但本轮未运行（需先起服务端并跑满 5 / 30 分钟，见运维手册「发布清单」与「压测用法」）  |
| `tools/e2e-edge.mjs`                                       | 未验证                                  | 可选 E2E，需本机 Edge 与真实会话；设计为缺浏览器时打印 `Skipped` 并以 0 退出                    |
| `deploy/Caddyfile`、`deploy/nginx.conf`                    | 静态审查                                | 本机无 caddy / nginx，未 `caddy validate`、未 `nginx -t`、未做 WSS 101 握手                     |
| `deploy/angry-chen.service`、`deploy/ecosystem.config.cjs` | 静态审查                                | 本机是 Windows，无 systemd / PM2，未启停验证                                                    |
| `Dockerfile`、`docker-compose.yml`                         | 静态审查（本机无 docker，未做构建验证） | 从未执行 `docker build` / `docker compose up`；首次部署前按运维手册「首次部署验证清单」逐条回填 |
| `pnpm check` 的类型/风格/测试/文档链四项                   | 未验证（本轮未复跑）                    | 并行施工期 `tools/**` 可能未收敛；逐项证据见 [docs/验收报告.md](docs/验收报告.md)               |

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

## 环境要求（本机已实测）

| 项         | 版本                 | 说明                                                           |
| ---------- | -------------------- | -------------------------------------------------------------- |
| Node.js    | 24.14.1              | 本机已验证；`vite@8` 要求 `^20.19` 或 `>=22.12`                |
| pnpm       | 11.22.0              | workspace 单仓                                                 |
| TypeScript | 5.9.3                | 刻意不用 7.x：`typescript-eslint@8.70` 的 peer 上限是 `<6.1.0` |
| 浏览器     | Edge / Chrome 最新版 | 本机只有 Edge；Chrome 未安装                                   |

不需要 Docker。`Dockerfile`、`docker-compose.yml`、`deploy/**` 是**可选**产物，本机缺少 docker / nginx / caddy / systemd / PM2，因此统一标注**静态审查**。

## 仓库结构（目标形态）

```
packages/shared/   纯逻辑：数学、模拟、AI、战斗、协议编解码、配置表（禁 three / DOM / node:* / Math.random）
packages/server/   Node 权威服务器：房间、会话、传输适配器、HTTP 健康检查与指标
packages/client/   浏览器客户端：渲染、输入、UI、音频、预测
tools/             无依赖脚本：文档链校验、协议探针、压测机器人、soak、报告
deploy/            反代与守护样例：Caddyfile、nginx.conf、systemd、PM2（静态审查）
docs/              全部文档
```

## 运维端点与协议探针

```bash
pnpm dev:server                                          # 默认 8787（PORT / HOST / MAX_ROOMS 可覆盖）
curl -s http://localhost:8787/health                     # JSON：协议版本、房间数、连接数、玩家数、tick 数
curl -s http://localhost:8787/metrics                    # Prometheus 文本：抖动、快照字节、非法帧、限流等
curl -s http://localhost:8787/api/leaderboard            # 本地排行榜（NDJSON 战绩）
node tools/probe.mjs --clients 2 --duration 10           # 两个脚本客户端进同一房间，打印快照速率/带宽/RTT
node tools/probe.mjs --clients 1 --duration 3 --flood     # 洪泛：预期被 Error(4) 断开
node tools/probe.mjs --clients 2 --duration 3 --bad-frame # 恶意帧：未知 opcode / 截断帧 / 9KB 帧
node tools/probe.mjs --clients 1 --duration 5 --room ABCD # 指定房间码并发观察
```

## 已知边界

- 不做 PVP、账号系统、云排行、多张地图、掉落经济、观战回放、移动端与手柄（理由见 [需求分析第 8 节](docs/02-需求分析.md)）。
- 「问界」额标为**原创风格化标识**，不使用真实商标素材（见 [ADR-003](docs/00-共识/ADR/ADR-003-美术素材与问界标识策略.md)）。
- 容量上限明确为**单机 + 反代 + 日志文件 + `/metrics`**；扩容路径写在 [运维手册](docs/运维手册.md)，不在本期实现。
