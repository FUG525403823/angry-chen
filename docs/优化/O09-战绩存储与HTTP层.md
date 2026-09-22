# O09 战绩存储与 HTTP 层

> 里程碑：O9 ｜ 预估：1 会话 ｜ 上游移交物：HANDOFF-O08

## 0. 现状与不足（证据）

本步处理**长跑后的资源增长**与**公开读接口的开销**：战绩文件整块读入内存且没有上限，
排行榜每次请求都全量排序并重复计算同一条记录，而 HTTP 层没有任何限流与缓存。

### 0.1 结论一：战绩加载是"整文件读入 + 逐行解析"，且记录数组无上限

| 事实 | 证据 |
|---|---|
| `load()` 用 `readFile` 一次读全文件，再 `split('\n')` | `packages/server/src/match/store.ts:85-108` |
| 解析成功的记录直接 `push` 进常驻数组，没有任何上限或淘汰 | `packages/server/src/match/store.ts:75,108,117` |
| NDJSON 是**只追加**格式，文件随时间单调增长 | `packages/server/src/match/store.ts:113-119`（`appendFile`） |
| 选项里也没有容量相关字段 | `packages/server/src/match/store.ts:31-37`（`JsonMatchStoreOptions`） |

**量级**：一条 4 人局记录约 1KB NDJSON；10 万局约 100MB 文件，加载时还要额外持有一份 `string` 副本与 `split` 出的字符串数组。

### 0.2 结论二：排行榜每次请求都全量排序，且重复计算同一量

| 事实 | 证据 |
|---|---|
| `listTopScores` 每次 `records.slice().sort(...)`，比较函数里对 a、b 各调用一次 `recordMaxKills` | `packages/server/src/match/store.ts:126-134`、`recordMaxKills` 定义于 `:42-50`（O(players) 循环） |
| `getRecentMatches` 同样全量 `sort` | `packages/server/src/match/store.ts:135-139` |
| 无任何缓存 | 同上（每次都重新算） |

**量级**：n=10 万时每次请求约 `n log n ≈ 170 万次比较 ×（2 × players 次循环）；`/api/leaderboard` 被轮询或被抓取时会持续吃 CPU。

### 0.3 结论三：HTTP 读接口没有限流、没有缓存

| 事实 | 证据 |
|---|---|
| `parseLimit` 只钳上限 100，不涉及频率 | `packages/server/src/http.ts:11-20` |
| 两个读接口直接查库并 200 返回 | `packages/server/src/http.ts:29-48` |
| 服务器没有任何 HTTP 限流逻辑（`429` 全仓库无命中） | `grep -rn '429' packages/server/src` |

**注意**：`security.ts` 里有 **join 节流**（`createJoinThrottle`）与 `ALLOWED_ORIGINS` 守卫，
但那作用于 WebSocket 加入流程，**不覆盖 HTTP 读接口**——这是本步要补的缺口。

### 0.4 结论四：`reports/` 每局一个文件，没有保留策略

| 事实 | 证据 |
|---|---|
| 对局结束写 `DATA_DIR/reports/<matchId>.json` | `packages/server/src/report.ts:6`、`packages/server/src/report.test.ts:99` |
| 全仓库没有任何删除/保留逻辑（只有测试里的 `rm` 临时目录清理） | `grep -rn 'unlink|rm\(' packages/server/src` |

### 0.5 结论五：坏行计数只在启动时刷新

| 事实 | 证据 |
|---|---|
| `corruptLines` 只在 `load()` 结束时写入 metrics | `packages/server/src/match/store.ts:107-108` |

运行期间若外部误写文件（人工编辑、磁盘半写），要等下次重启才可见——本步顺带修掉（每次 append 前后不做，改为在受控的"重载"路径更新）。

## 1. 目标

本步骤结束后：

1. 战绩存储内存**有界**：默认最多保留 `maxRecords` 条，超出按"最旧优先"淘汰；上限可由环境变量配置；
2. 启动加载**流式化**（不整文件读入、不 `split` 全量字符串）；
3. 排行榜/最近对局查询**不做每次全量排序**：维护可失效的排序缓存，且结果与旧实现逐条一致（对拍）；
4. HTTP 读接口有**限流**（超限 429 + `Retry-After`）与**短 TTL 缓存**；
5. `reports/` 有保留策略（按 mtime 保留最近 N 份）。

判据：新增用例证明"10 万行文件加载后只保留 `maxRecords` 条"、"top-N 与全排序对拍一致"、"超过限流返回 429"；
`pnpm check` 全绿。

## 2. 入口条件

**入口条件**：HANDOFF-O08

| # | 必须已成立的事实 | 验证命令 | 期望 |
|---|---|---|---|
| 1 | 上一步交付可用 | `pnpm check` | 退出码 0 |
| 2 | 存储实现与测试可定位 | `ls packages/server/src/match/` | 含 `store.ts`、`store.test.ts` |
| 3 | 读接口可定位 | `grep -n "api/leaderboard" packages/server/src/http.ts` | 命中 |
| 4 | `security.ts` 的节流原语可复用 | `grep -n "createJoinThrottle" packages/server/src/security.ts` | 命中 |
| 5 | 环境变量约定可查 | `sed -n 1,10p .env.example` | 头部说明"读取点以注释里的文件为准" |

## 3. 交付物

| 文件 | 状态 | 职责 |
|---|---|---|
| `packages/server/src/match/store.ts` | 修改 | 流式加载、`maxRecords` 淘汰、排序缓存（top / recent 两个视图） |
| `packages/server/src/match/store.test.ts` | 修改 | 10 万行加载、淘汰、对拍、缓存失效用例 |
| `packages/server/src/report.ts` | 修改 | 写报告后按 mtime 保留最近 N 份 |
| `packages/server/src/report.test.ts` | 修改 | 保留策略用例（保留 N、删除更旧、失败不影响主流程） |
| `packages/server/src/http.ts` | 修改 | 读接口限流（429 + `retry-after`）+ 60s TTL 缓存；计数入 metrics |
| `packages/server/src/http.test.ts` | 修改 | 429、缓存命中、缓存失效（新对局写入后可见）用例 |
| `packages/server/src/main.ts` | 修改 | 读取 `MATCH_STORE_MAX_RECORDS`、`REPORT_RETENTION` 并注入 |
| `packages/server/src/metrics.ts` | 修改 | `httpRateLimited`、`httpCacheHits`、`recordsRetained` 三个字段与 Prometheus 输出 |
| `.env.example` | 修改 | 新增两个变量及读取点注释 |
| `docs/运维手册.md` | 修改 | §2 环境变量表 + §9 指标速查各补条目；§6 扩容段补"数据量增长"一句 |
| `docs/evidence/store-load-100k.md` | 新建 | 10 万行加载的耗时/内存与查询耗时实测记录 |
| `docs/验收报告.md` | 修改 | 新增/更新"持久化与读接口"结论 |

## 4. 任务清单

- [ ] 1. `store.ts`：`JsonMatchStoreOptions` 增加 `maxRecords?: number`（默认 `DEFAULT_MAX_RECORDS = 10000`）；`records` 超过上限时按最旧（`startedAtMs` 最小）批量淘汰（每次淘汰 `max(1, maxRecords/100)` 条，摊销 O(1)）。
- [ ] 2. `store.ts`：`load()` 改为 `createReadStream(this.filePath, { encoding: 'utf8' })` + `readline.createInterface` 逐行解析；解析后立刻入队并按上限淘汰，不保留整文件字符串；`corruptLines` 语义不变（仍计入 metrics）。
- [ ] 3. `store.ts`：新增内部排序缓存
      `type ScoreEntry = { record: MatchResultRecord; maxKills: number }`；
      `topCache: ScoreEntry[] | null`（按 `maxKills` 降序、`startedAtMs` 降序）与 `recentCache: MatchResultRecord[] | null`（按 `startedAtMs` 降序）；
      `load` / `appendMatchResult` / 淘汰发生后把两者置 `null`；查询时若为 `null` 则重建一次（O(n log n)），命中则 O(limit) 切片。
- [ ] 4. `store.ts`：`listTopScores` / `getRecentMatches` 改为读缓存；保证结果与旧实现的"全排序取前 N"**逐条一致**（含并列时的 id/时间决胜）。
- [ ] 5. `report.ts`：新增 `retainReports(dir, keep: number)`——按 mtime 降序保留前 `keep` 个 `.json`，其余 `unlink`；失败仅 `logger.warn`，绝不抛出到主流程（对局结束路径不能因清理失败而报错）。
- [ ] 6. `http.ts`：读接口加 60s TTL 缓存（键 = `path + ':' + String(limit)`），命中计 `httpCacheHits`；新增读取限流——复用 `security.ts` 的节流模式（令牌桶/滑动窗口按 `req.socket.remoteAddress`），默认 30 次/分钟，超限返回 `429` + `retry-after` 头 + `metrics.httpRateLimited` 自增。`CORS/Cache-Control` 头保持现状。
- [ ] 7. `main.ts`：读 `MATCH_STORE_MAX_RECORDS`、`REPORT_RETENTION`（默认 10000 / 200），非法值回落到默认并 `logger.warn`。
- [ ] 8. `metrics.ts`：新增 `httpRateLimited`、`httpCacheHits`（counter）与 `recordsRetained`（gauge，渲染 `ac_records_retained`），并在 `renderPrometheus` 输出。
- [ ] 9. 测试：
  - `store.test.ts`：①生成 10 万行临时 NDJSON（用 `appendFile` 批量写），加载后 `records.length === maxRecords`；②`maxRecords = 100` 时交替写入 200 条，最旧被淘汰、最新的都在；③top-N 对拍：与"全排序取前 N"逐条比较（含并列）；④缓存失效：写入新对局后 `listTopScores` 立刻反映；⑤坏行计数不变。
  - `report.test.ts`：构造 5 个报告文件、`keep = 2` → 只剩最新的 2 个；`unlink` 抛错时函数不抛。
  - `http.test.ts`：①连发 31 次超过限流 → 第 31 次 429 且带 `retry-after`；②两次相同请求第二次命中缓存（`httpCacheHits` 增长）；③写入新对局后缓存失效。
- [ ] 10. 证据：`docs/evidence/store-load-100k.md` 记录改动前后的加载耗时与 `process.memoryUsage().heapUsed` 差值（脚本化：临时目录 + 生成 10 万行 + 计时），以及 `listTopScores` 的单次耗时（改动前后）。
- [ ] 11. 文档：`.env.example` 增加 `MATCH_STORE_MAX_RECORDS` / `REPORT_RETENTION`（含"读取位置"注释）；`docs/运维手册.md` §2、§9、§6 同步；`docs/验收报告.md` 增补结论。

## 5. 冻结契约

```ts
// packages/server/src/match/store.ts
export const DEFAULT_MAX_RECORDS = 10000;
export const DEFAULT_REPORT_RETENTION = 200;

export interface JsonMatchStoreOptions {
  readonly dir?: string;
  readonly metrics?: Metrics;
  readonly maxRecords?: number;      // 新增：常驻记录上限，超出按最旧淘汰
}

// packages/server/src/report.ts
export function retainReports(dir: string, keep: number): Promise<number>;   // 返回删除数量；失败不抛

// 环境变量（读取位置：packages/server/src/main.ts）
// MATCH_STORE_MAX_RECORDS=10000   常驻战绩上限
// REPORT_RETENTION=200            诊断报告保留份数
```

| 冻结项 | 值 / 定义 |
|---|---|
| 淘汰策略 | 常驻记录数 > `maxRecords` 时淘汰 `startedAtMs` 最小者；文件本身不截断（NDJSON 只追加，历史仍可离线分析） |
| 加载方式 | `createReadStream` + `readline`；不保留整文件字符串 |
| 排序缓存 | `topCache` / `recentCache` 在 `load`、`appendMatchResult`、淘汰后置空；命中时查询复杂度 O(limit) |
| 查询一致性 | 与"全排序取前 N"逐条一致（并列按 `startedAtMs` 降序，再按 `matchId` 升序决胜） |
| HTTP 限流 | 读接口 30 次/分钟/IP；超限 `429` + `retry-after: 60`；`ALLOWED_ORIGINS` 守卫行为不变 |
| HTTP 缓存 | TTL 60s，键 = `path:limit`；写入新对局/触发淘汰即失效 |
| 报告保留 | 按 mtime 保留最近 `REPORT_RETENTION` 份；清理失败只记日志 |
| 指标 | `ac_records_retained`（gauge）、`ac_http_rate_limited_total`、`ac_http_cache_hits_total`（counter） |
| 协议 | 不涉及 |

## 6. 验证

| # | 命令 | 期望 | 失败意味着 |
|---|---|---|---|
| 1 | `pnpm --filter @ac/server test` | 新增 11 组用例通过（含 10 万行与对拍） | 淘汰策略或缓存失效写错（可能出现"新对局看不到"） |
| 2 | `node --expose-gc -e "..."` 或证据脚本 | 加载 10 万行后 `heapUsed` 增量与 `maxRecords` 成正比，不随文件行数线性增长 | 仍在整文件读入 |
| 3 | `curl -s -i "http://127.0.0.1:8787/api/leaderboard?limit=5"` 连续 31 次 | 第 31 次 `429` 且带 `retry-after` | 限流未生效 |
| 4 | `curl -s http://127.0.0.1:8787/metrics | grep -E "records_retained|http_cache_hits"` | 两条指标存在且有值 | 指标未接线 |
| 5 | `pnpm check` | 退出码 0 | 任一门失败 |
| 6 | 人工复核：`grep -n "readFile" packages/server/src/match/store.ts` | 无命中（`readFile` 已换成流） | 流式加载未落地 |

## 7. DoD（验收标准）

- [ ] `pnpm check` 全绿；`store.test.ts` 覆盖 §4 任务 9 的五种情形。
- [ ] 10 万行加载用例证明常驻记录数 = `maxRecords`，且加载走流式（`readFile` 不再出现于 `store.ts`）。
- [ ] top-N 与旧实现逐条对拍一致（含并列决胜）。
- [ ] HTTP 读接口限流（429 + `retry-after`）与 60s 缓存生效，`ac_http_rate_limited_total` / `ac_http_cache_hits_total` 可见。
- [ ] `retainReports` 生效且失败不影响对局结束路径；`REPORT_RETENTION` 已加入 `.env.example` 与运维手册。
- [ ] `docs/evidence/store-load-100k.md` 存在（改动前后耗时/内存对比）。
- [ ] 未新增运行时依赖（`node:fs` / `node:readline` 即可）；未改协议；未改 NDJSON 记录格式。

## 8. 风险与回滚

| 风险 | 触发信号 | 对策 |
|---|---|---|
| 淘汰策略让"历史最高分"消失（排行榜不再包含远古记录） | 运维反馈榜单变短 | 淘汰只影响**内存常驻**，NDJSON 文件不截断；如需完整榜单，提供离线分析脚本（本步只写文档说明，不改行为） |
| 排序缓存失效点漏了一处 → 新对局不可见 | `http.test.ts` 的"写入后可见"用例失败 | 失效统一收敛到 `invalidateCaches()` 私有方法，只有 3 个调用点（load / append / prune） |
| 限流误伤反代后的正常用户（所有请求同一 IP） | 线上 429 比例升高 | 默认阈值 30/分钟只覆盖读接口；文档写明"反代需传 X-Forwarded-For 并同步调整阈值"，本步不上线更激进的策略 |
| 报告清理误删仍需保留的诊断文件 | 对局复盘找不到报告 | 默认保留 200 份；`REPORT_RETENTION=0` 表示不清理（显式约定，写进 §5 与文档） |
| 流式加载改变了坏行计数语义 | `store.test.ts` 现有用例失败 | `corruptLines` 语义保持"本次加载遇到的坏行数"，只换读取方式 |
| 回滚目标 | — | 回滚 = `git revert` 本次提交（store/report/http/main/metrics + 测试 + 文档），内存与 CPU 回到优化前；数据文件格式未变，无需迁移 |

## 9. 移交物

**移交物 ID**：HANDOFF-O09

给下一步（O10 质量门与性能守门）的稳定接口与已验证能力：

**稳定接口**
- `JsonMatchStoreOptions.maxRecords` / `DEFAULT_MAX_RECORDS = 10000`
- `retainReports(dir, keep)` / `DEFAULT_REPORT_RETENTION = 200` / `REPORT_RETENTION=0` 表示不清理
- 环境变量 `MATCH_STORE_MAX_RECORDS`、`REPORT_RETENTION`（读取点在 `main.ts`）
- 指标 `ac_records_retained`、`ac_http_rate_limited_total`、`ac_http_cache_hits_total`
- HTTP 读接口契约：30 次/分钟/IP、超限 429 + `retry-after: 60`、60s TTL 缓存

**已验证能力**
- 战绩加载内存有界且流式；超大文件不再一次性进内存
- 排行榜/最近对局查询命中缓存，不再每次全量排序（结果与旧实现逐条一致）
- 读接口有限流与缓存，且新对局写入后立即可见
- `reports/` 有保留策略，失败不影响主流程

**未决项（移交后续）**
- 反代场景下的真实客户端 IP 获取（`X-Forwarded-For`）与限流阈值调优 → 记入 O10 的待复测清单
- 完整历史榜单的离线分析工具（当前只保证常驻内存有界）→ 不在 O 系列范围内，记入 O10 的"未纳入"清单
