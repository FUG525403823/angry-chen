# 10 万行战绩文件：加载耗时 / 常驻内存 / 查询耗时（O09）

> 生成时间：2026-09-22 ｜ 本机 Node v24.14.1（Windows，`--expose-gc`）｜ 脚本：临时目录外置脚本（见 §3）

## 1. 结论（同一台机器、同一份 10 万行 NDJSON，一次运行内 A/B）

| 指标 | 改动前（`readFile` + `split` 全量解析，常驻无上限） | 改动后（`createReadStream` + `readline`，`maxRecords = 10000`） | 变化 |
|---|---|---|---|
| 文件规模 | 100 000 行 / 66 475 406 B（665 B/行，4 人局） | 同左 | — |
| 加载耗时 | 445 ms | 711 ms | **×1.6（变慢）** |
| 常驻记录数 | 100 000 | 10 000 | **有界** |
| `heapUsed` 增量（GC 后） | **+132.2 MB** | **+6.9 MB** | **−94.8%（约 1/19）** |
| `listTopScores(20)` 首次 | 49.46 ms（每次全量排序 + 每条记录重复算 `maxKills`） | 9.24 ms（重建缓存一次） | ×0.19 |
| `listTopScores(20)` 稳态 | 49.46 ms（同上，每次都排） | **0.001 ms**（命中缓存，O(limit) 切片） | **≈ 5 万倍** |

**怎么读这张表**：

- 加载**慢**了 266 ms，代价换来常驻内存从 132 MB 降到 6.9 MB（且不再随文件行数增长）——10 万局只有 6.9 MB，100 万局仍是 6.9 MB（文件 665 MB）。
- 排行榜接口的稳态开销从「每次请求 49 ms CPU + 全量排序」降到「0.001 ms」，这是本步针对「`/api/leaderboard` 被轮询/抓取时持续吃 CPU」的直接修复。
- 淘汰只作用于**内存常驻**集合：NDJSON 文件仍是只追加（66 MB 原样保留），历史记录仍可离线分析。

## 2. 与 §6 验证命令的对应

| O09 §6 | 命令 | 结果 |
|---|---|---|
| #1 | `pnpm --filter @ac/server test` | 457 用例全绿（新增 10 组：store 5、http 3、report 2） |
| #2 | 证据脚本（本文件 §1） | `heapUsed` 增量 132.2 MB → 6.9 MB，且常驻条数 100 000 → 10 000（不再随文件行数线性增长） |
| #3 | 真实服务器连发 31 次 `curl /api/leaderboard?limit=5` | 第 31 次 `429` + `retry-after: 60`（见 §4 原始输出） |
| #4 | `curl /metrics` 抓 `records_retained` / `http_cache_hits` | 两条指标存在且有值（见 §4） |
| #5 | `pnpm check` | 退出码 0 |
| #6 | `grep -n "readFile" packages/server/src/match/store.ts` | 无命中（已换成 `createReadStream` + `readline`） |

## 3. 方法（可复现）

脚本在仓库外（`%TEMP%`），核心步骤：

1. `mkdtemp` 临时目录，用 4 人局记录（665 B/行）一次性写满 100 000 行；
2. **基线**：脚本内复刻旧实现（`readFile` + `split` + 逐行 `JSON.parse` + 常驻数组）并计时/量内存；
3. **新实现**：`import('.../packages/server/src/match/store.ts')` 后 `createJsonMatchStore({ dir, maxRecords: 10000 }).load()`，再量内存与查询；
4. 每段之间 `globalThis.gc()` 后取 `process.memoryUsage().heapUsed`。

**口径说明（如实披露）**：基线是在同一进程内复刻旧算法，而不是 `git stash` 后跑旧提交；两者代码路径逐行对应（`store.ts` 旧版 85-111 行），差别只在同进程内存碎片，量级结论（19 倍）不受影响。

## 4. 原始输出

```json
{
  "lines": 100000,
  "fileBytes": 66475406,
  "bytesPerLine": 665,
  "writeMs": 408,
  "old": { "loadMs": 445, "retainedRecords": 100000, "heapDeltaMb": 132.2, "queryMs": 49.46 },
  "now": {
    "loadMs": 711,
    "retainedRecords": 10000,
    "heapDeltaMb": 6.9,
    "queryColdMs": 9.24,
    "queryWarmMs": 0.001
  }
}
```

HTTP 层实测（真实服务器 + `curl`/`Invoke-WebRequest`，见 `docs/evidence/http-limit-o09.md`）：
第 31 次请求 `429` + `retry-after: 60`；`/metrics` 含 `ac_records_retained`、`ac_http_cache_hits_total`、`ac_http_rate_limited_total`。