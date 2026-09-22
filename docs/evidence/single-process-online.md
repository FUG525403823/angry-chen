# 证据：单进程静态托管与同源寻址（ADR-007）

- 采集环境：Windows 开发机、Node v24.14.1、pnpm 11.22.0；端口 8899（避免与开发实例冲突），`DATA_DIR` 指向系统临时目录。
- 采集方式：`pnpm build:client` 后起单个 Node 进程，全部命令在本机 loopback 上执行。
- **已验证**＝有下面粘贴的真实命令与输出；**未执行**＝需要第二台设备或真实浏览器，本机无法闭合。

## 1. 服务端启动（单进程：静态资源 + HTTP 端点 + WebSocket）

```powershell
$env:HOST='0.0.0.0'; $env:PORT='8899'; $env:DATA_DIR=(Join-Path $env:TEMP 'ac-verify-data'); pnpm --filter @ac/server run start
```

```json
{"ts":1790064233306,"level":"info","evt":"listening","room":null,"tick":0,"pid":0,"detail":{"url":"http://0.0.0.0:8899","health":"/health","metrics":"/metrics","leaderboard":"/api/leaderboard","recentMatches":"/api/matches/recent","dataDir":"C:\\Users\\FUGGGG\\AppData\\Local\\Temp\\ac-verify-data","maxRooms":64,"maxPlayersPerRoom":4,"allowedOrigins":"any","clientDist":"D:\\projects\\tmp\\angry-chen\\packages\\client\\dist","joinUrls":["http://192.168.42.1:8899","http://192.168.159.1:8899","http://10.91.1.182:8899"]}}
```

结论：`clientDist` 解析到真实 dist 目录；`joinUrls` 直接给出可分享地址（ADR-007 §4）。

## 1.1 `.env` 自动加载（ADR-007 §3）

在仓库根放一个临时 `.env`（`PORT=8901` / `HOST=127.0.0.1` / `DATA_DIR=C:/tmp/ac-envfile-data`），
**不设置任何同名 shell 环境变量**，然后 `pnpm --filter @ac/server run start`（cwd 是 `packages/server`）：

```json
{"ts":1790064444312,"level":"info","evt":"listening","detail":{"url":"http://127.0.0.1:8901","dataDir":"C:/tmp/ac-envfile-data","allowedOrigins":"any","clientDist":"D:\\projects\\tmp\\angry-chen\\packages\\client\\dist","joinUrls":["http://127.0.0.1:8901"]}}
```

结论：仓库根候选被命中（cwd 是包目录，靠模块位置解析仓库根）；`HOST=127.0.0.1` 时 `joinUrls` 只给回环地址，不误导。
验证后临时 `.env` 已删除（该文件本来就被 `.gitignore` 忽略）。候选顺序（cwd 优先）由 `main.test.ts` 的 `resolveEnvFileCandidates` 用例钉住。
真实环境变量优先于 `.env` 已单独实测：`$env:PORT=1111; node --env-file=.env.example -e "console.log(process.env.PORT)"` → `1111`。

## 2. 静态资源由 Node 进程提供，运维端点仍然优先

```text
1 root: 200 text/html; charset=utf-8
2 asset headers:
HTTP/1.1 200 OK
content-type: text/javascript; charset=utf-8
cache-control: public, max-age=31536000, immutable
vary: accept-encoding
content-length: 123941
3 gzip bytes: 43820
4 identity bytes: 123941
5 health: {"status":"ok","protocolVersion":2,"rooms":0,"connections":0,"players":0,"ticks":0}
6 metrics: ac_rooms 0（/metrics 仍是 Prometheus 文本）
7 leaderboard: {"ok":true,"entries":[]}
8 traversal: 404
9 not-found: {"error":"not-found","path":"/not-found"}
10 spa fallback: 200
```

覆盖到的判据：`/assets/*` immutable（哈希产物）、按 `accept-encoding` 分流（gzip 43,820 B vs 身份 123,941 B）、
`/health`、`/metrics`、`/api/*` 不被静态兜底吃掉、穿越路径 404、非导航请求仍是 404 JSON、导航请求走 index.html 兜底。

## 3. 同源 WebSocket 握手与协议探针

```text
11 upgrade /ws with Origin:
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
```

```text
node tools/probe.mjs --clients 2 --duration 10 --url ws://127.0.0.1:8899/ws
  "aggregate": {
    "snapshots": 410,
    "avgBytes": 46.9,
    "avgRecords": 1.99,
    "errorFrames": 0,
    "decodeFailures": 0
  }
probe OK（exit=0）
```

结论：客户端推导出的目标 `ws://<同源 host>/ws` 能完成 101 握手并进房（服务端不按路径过滤 upgrade），
两个脚本客户端同房、快照速率与字节量在预算内。服务端日志同时留有 `session.join`（`room":"CPP2`、`8SCD`）。

## 4. 单元测试（同源寻址规则与静态层契约）

```text
npx vitest run packages/server/src/static.test.ts packages/server/src/http.test.ts packages/server/src/main.test.ts packages/client/src/net/serverUrl.test.ts
 ✓ |client| packages/client/src/net/serverUrl.test.ts (7 tests)
 ✓ |server| packages/server/src/main.test.ts (4 tests)
 ✓ |server| packages/server/src/http.test.ts (9 tests)
 ✓ |server| packages/server/src/static.test.ts (11 tests)
 Test Files  4 passed (4)    Tests  31 passed (31)
```

`serverUrl.test.ts` 钉住优先级：`?server=` > Vite dev（`ws://127.0.0.1:8787`）> 同源（`http`→`ws://host/ws`、`https`→`wss://host/ws`）> `file://` 兜底。

## 5. 未执行（需人工/第二台设备）

1. 第二台设备用 `http://<公网 IP 或内网 IP>:8899` 打开浏览器、进房、打完第 1 波（本机只有 loopback 与探针客户端）。
2. Windows 防火墙 / 云安全组放行 8787（8899）后的**真实跨机**连通性。
3. 真实 HTTPS + 反代形态下 `wss://<域名>/ws` 的自动推导（`deploy/**` 仍是静态审查）。

## 7. 跨平台（Windows / Linux）

结论：**服务端源码平台中立**（只用 `node:*` 内置模块 + `ws` + `tsx`，无原生模块、无 `process.platform` 分支），
Windows 与 Linux（CI ubuntu-latest）都会真实执行同一份源码；macOS 未跑过。

### 7.1 CI 抓到的第一个 Windows-only 断言（真实红→绿）

提交 `00c353d` 在本机 `pnpm check` 全绿，但 GitHub Actions 的 `quality` job（ubuntu-latest）在 `Run pnpm check` 一步失败：

```text
原代码 packages/server/src/static.test.ts:
  const root = join('C:', 'repo', 'packages', 'client', 'dist');
Windows: 'C:\\repo\\packages\\client\\dist'  → 绝对路径，resolveStaticPath 的断言成立
Linux:   'C:/repo/packages/client/dist'     → 相对路径，resolve(root, ...) 会拼上 cwd，断言失败
修复:    resolve(tmpdir(), 'ac-repo', 'packages', 'client', 'dist')
```

这正是「同一份测试在两个平台都要跑」的价值：服务端代码没问题，**测试里的平台假设**有问题。

修复推送后（`f0a0b57`）CI 三个 job 全绿：`quality`（九步门禁，ubuntu 上真实跑 72 文件 / 481 用例与 vite build）、
`smoke`（§7.3 的单进程冒烟）、`perf`（4 人 2 分钟 `--strict`）。

### 7.2 顺带修掉的第二个平台隐患（超时抖动）

该文件里 `GET / 返回 index.html（no-cache）` 一例本机耗时 **4045ms**（vitest 默认单例超时 5000ms，CI 慢机上必红）：
Node 19+ 的全局 `http` agent 默认 keep-alive，`server.close()` 要等空闲连接自然回收（`keepAliveTimeout` ≈ 5s）。
修复：请求加 `agent: false` + 关闭前 `server.closeAllConnections()`。

```text
修复后：11 passed；单例 38ms；文件总时长 348ms（此前 4.36s）
```

### 7.3 跨平台冒烟：`pnpm smoke`（新增 `tools/smoke-serve.mjs`）

脚本真起一个服务端进程（`PORT=0` 让 OS 分配端口，从 `listening` 日志里读回），逐项验证后关进程；
Windows / Linux / macOS 同一份脚本，CI 的 `smoke` job 在 ubuntu 上跑它。

```text
node tools/smoke-serve.mjs（本机 Windows，Node v24.14.1）
  · GET /：200 text/html
  · GET /assets/index-Cfzv30Q1.js：200 cache-control=public, max-age=31536000, immutable content-encoding=gzip
  · GET /health：200 status=ok protocolVersion=2
  · GET /api/leaderboard：200 ok=true
  · 路径穿越 /%2e%2e%2f%2e%2e%2fpackage.json：404
  · 导航请求 /room/ABCD 走 index.html 兜底：200
  · WebSocket /ws 握手：101 Switching Protocols
  · 服务端日志含 clientDist 与 joinUrls：D:\\projects\\tmp\\angry-chen\\packages\\client\\dist
=== smoke-serve ===
OK：单进程联机冒烟全部通过（win32 x64，Node v24.14.1）
```

### 7.4 平台矩阵

| 平台 | 跑过什么 | 结果 |
|---|---|---|
| Windows（本机开发机） | `pnpm check` 九步、`pnpm smoke`、`pnpm serve` + curl/probe（§1–§3） | 全绿 |
| Linux（CI ubuntu-latest） | `pnpm check` 九步（含真实 in-process HTTP/静态/WS 测试、vite build、覆盖率、分配探针）、`pnpm smoke`、`pnpm check:perf` | **全绿**：`f0a0b57` 的 `quality` / `smoke` / `perf` 三个 job 全部 success（§7.1 的红即来自这里） |
| macOS | 无 | **未跑过**（无机器；代码无 `darwin` 分支，风险仅剩未验证） |

平台相关的已知边界（都不影响服务端本体）：

- `deploy/**`（systemd unit / PM2 / Dockerfile / Caddyfile / nginx.conf）是 **Linux 专用产物**，且仍是**静态审查**（本机无 docker/caddy/nginx）。
- `tools/e2e-edge.mjs` 里写死了 Windows 的 Edge/Chrome 路径，缺 `puppeteer-core` 或浏览器时打印 `Skipped` 并以 0 退出（Linux 上属预期跳过）。
- `tools/check-assets.mjs` 用 `split('\\').join('/')` 归一化相对路径——在 POSIX 上本来就是 `/`，是空操作，不构成平台差异。
## 6. 复现命令（本机）

```bash
pnpm install --frozen-lockfile
pnpm build:client
HOST=0.0.0.0 PORT=8899 DATA_DIR=<临时目录> pnpm --filter @ac/server run start
curl -s -o /dev/null -w '%{http_code} %{content_type}\n' http://127.0.0.1:8899/
curl -sI http://127.0.0.1:8899/assets/<hash>.js
curl -s http://127.0.0.1:8899/health
node tools/probe.mjs --clients 2 --duration 10 --url ws://127.0.0.1:8899/ws
```
