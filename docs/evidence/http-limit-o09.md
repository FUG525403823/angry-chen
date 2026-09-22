# HTTP 读接口限流与缓存：真实服务器实测（O09 §6 #3/#4）

> 生成时间：2026-09-22 ｜ 本机 Node v24.14.1 ｜ 命令：`node packages/server/src/main.ts`（`PORT=8902`、`LOG_LEVEL=warn`、临时 `DATA_DIR`）+ `curl.exe`

## 1. 原始输出（未删改）

```
health=200
req29=200
req30=200
req31=429
--- headers of next request ---
hdr: HTTP/1.1 429 Too Many Requests
hdr: content-type: application/json; charset=utf-8
hdr: retry-after: 60
--- metrics ---
metric: # HELP ac_http_rate_limited_total HTTP read requests rejected by the rate limiter
metric: # TYPE ac_http_rate_limited_total counter
metric: ac_http_rate_limited_total 2
metric: # HELP ac_http_cache_hits_total HTTP read responses served from the cache
metric: # TYPE ac_http_cache_hits_total counter
metric: ac_http_cache_hits_total 29
metric: # HELP ac_records_retained match records kept in memory
metric: # TYPE ac_records_retained gauge
metric: ac_records_retained 0
```

## 2. 怎么读

- 第 1–30 次 `GET /api/leaderboard?limit=5` 都是 `200`，**第 31 次是 `429`**，且响应带 `retry-after: 60` —— 与冻结契约「30 次/分钟/IP、超限 `429` + `retry-after: 60`」一致。
- `ac_http_cache_hits_total 29`：第 1 次未命中（建立缓存），第 2–30 次全部命中 60s 缓存；被限流的那次**没有**走到缓存（计数停在 29），说明限流在缓存之前判定。
- `ac_http_rate_limited_total 2`：第 31 次请求 + 第 32 次取响应头那次，共 2 次拒绝（窗口内）。
- `ac_records_retained 0`：本次实例用的是空 `DATA_DIR`，常驻战绩为 0 —— 指标接线正确（有值、随 store 变化）。
- 单位是**每个 HTTP handler 实例**一份窗口（`createHttpHandler` 内的 `Map`），测试里每次新建 handler 都是干净窗口。

## 3. 复现命令

```powershell
# 终端 A
$env:PORT='8902'; $env:HOST='127.0.0.1'; $env:DATA_DIR=(Join-Path $env:TEMP 'ac-o09-http'); $env:LOG_LEVEL='warn'
node packages/server/src/main.ts

# 终端 B
$base='http://127.0.0.1:8902'
for ($i=1; $i -le 31; $i++) { curl.exe -s -o NUL -w "$i=%{http_code}`n" "$base/api/leaderboard?limit=5" }
curl.exe -s -i "$base/api/leaderboard?limit=5"   # 看 retry-after: 60
curl.exe -s "$base/metrics" | Select-String 'records_retained|http_cache_hits|http_rate_limited'
```

> 说明：本机 PowerShell 会话里 `Invoke-WebRequest` 不支持 `-SkipHttpErrorCheck`（5.1 语义），因此改用 Windows 自带的 `curl.exe`；这也是 §6 #3 原本要求的命令。