# O03 出站帧所有权与背压：实跑证据

日期：2026-09-22 ｜ 环境：Windows 11 / Node v24.14.1 / 本机进程内服务器（`node --import tsx src/main.ts`，端口 8787）

本文件回答两件事：①出站帧在「发送方继续复用缓冲」时是否还会损坏；②慢客户端是否真的有界并会被兜底断开。

## 1. 确定性证据（单测，`pnpm --filter @ac/server exec vitest run src/transport`）

| 文件 | 用例 | 断言 |
|---|---|---|
| `send-queue.test.ts` | `send` 后改写源数组 | 回调收到的字节与发送时逐字节一致 |
| `send-queue.test.ts` | 池容量用尽 | 复用同一 `ArrayBuffer`（非新分配），且以独立视图交出 |
| `send-queue.test.ts` | 超出 `maxBytes` / 单帧超池 | `drops` 增长且不入队（调用方据此处置） |
| `send-queue.test.ts` | `clear()` | `queuedBytes === 0`，且已入队的 in-flight 回调不会二次归还 |
| `ws-adapter.test.ts` | 复用一块 scratch 连发 20 帧 | 客户端收到的字节逐帧一致（拷贝语义） |
| `ws-adapter.test.ts` | 慢客户端积压超水位 | 128 帧 × 8KiB 全部写进**同一块** scratch：只有前 32 帧（= 262144 ÷ 8192）进入 socket，**每帧 payload 校验通过**；`slowClientDrops > 0`；关闭码 **1013** |

结果：`Test Files 2 passed / Tests 8 passed`（server 项目 transport 目录）。

### 1.1 反向验证：拷贝点是否真的承重

把 `ws-adapter.send` 临时改成「仍然入队计数，但把**原帧**而不是队列里的拷贝交给 socket」：

```ts
queue.send(frame, (_chunk, done) => { socket.send(frame, { binary: true }, () => done()); }); // 半修复
```

`ws-adapter.test.ts` 立即失败：`AssertionError: expected 127 to be 24`（第 128 次写入 scratch 的字节出现在第 24 帧的 payload 里）。
即：**queue 计数 + 直传原帧**依然会损坏帧，拷贝点不能省。恢复实现后 8/8 通过。

## 2. 端到端证据：慢客户端被丢帧计数 + 1013 断开

慢客户端单独跑时，出站只有 ~1KB/s（20 快照/s × ~47B），对端不读的 OS 缓冲会先吸收 ≥100KB，
所以在 256KiB 队列预算前需要很长时间。可复现的做法是**给同房间的慢客户端加压**：聊天是广播
（`session.ts` 遍历同房间连接发同一块 `broadcastBuffer`），`tools/probe.mjs --chat-rate/--chat-len` 正好用它制造持续出站。

```bash
# 终端 A：2 个正常客户端持续聊天（64 字节文本，20 条/秒/人），它创建房间并打印 room=XXXX
node tools/probe.mjs --url ws://127.0.0.1:8787 --clients 2 --chat-rate 20 --chat-len 64 --duration 300
# 终端 B：2 个慢客户端（读 socket 暂停）加入同一房间
node tools/probe.mjs --url ws://127.0.0.1:8787 --clients 2 --room <CODE> --duration 280 --slow --drain-sec 15
```

结果（`--slow` 进程退出码 **0**，全部 slow 判据通过）：

| 观测量 | 实测 | 含义 |
|---|---|---|
| `pausedSec` | 123.3s | 从暂停读取到服务端开始丢帧 |
| `dropsBefore` → `dropsAfter` | 0 → **2** | 两个慢连接各丢 1 帧（第一帧超出预算的那一帧） |
| `queueBytesMax` | **524247** | = 2 × 262123 ≈ 2 × `LIMITS.maxBufferedBytes`，队列**恰好卡在预算上** |
| `closeCode` | **1013**（两个客户端都是） | 兜底断开生效 |
| `drainedFrames` | 5627 | 断开前积压在链路里的帧，恢复读取后全部收到 |
| `decodeFailures` | **0** | 5627 帧逐帧解码成功 ⇒ 真实背压下出站帧未被复用缓冲污染 |
| `errorFrames` | 0 | 未触发策略/频率类错误码 |

服务器 `/metrics` 采样（本机服务器，2 慢 + 2 正常连接同房间）：

| 时刻 | `ac_send_queue_bytes` | `ac_bytes_out_total` | RSS |
|---|---|---|---|
| t=10s | 0 | 61204 | 81.7MB |
| t=35s | 0 | 233012 | 82.2MB |
| t=60s | 67610 | 404492 | 83.1MB |
| t=85s | 274162 | 579112 | 89.2MB |
| t=110s | 475384 | 747800 | 98.4MB |
| 结束 | 0（连接已断开） | 1121210 | 100.4MB |

RSS 在积压累积期上升（被阻塞的出站帧同时躺在队列与 node 写缓冲里），**上限由策略决定**：
`ac_send_queue_bytes` 顶到 2 × 256KiB 即开始丢帧并断开，不再增长；断开后进程 RSS 保持在高水位（node 未主动归还），但不再随负载增长。

## 3. 慢客户端单独跑（无外部压力）时的观测

```bash
node tools/probe.mjs --url ws://127.0.0.1:8787 --clients 2 --duration 100 --slow --drain-sec 10
```

- `queueBytesMax = 0`、`drops = 0`；`drainedFrames = 5052`、`decodeFailures = 0`（恢复读取后仍无损坏帧）。
- 同期服务器出站 1.93KB/s（2 连接合计，≈0.96KB/s/连接）：对端不读时先由 OS 缓冲吸收（实测 ≥100KB/连接），
  因此在 256KiB 预算前需要数分钟；用 3 个慢客户端的长跑同样观测到 `queue = 0`。
- 这条观测本身是符合预期的：**队列只在写真的完不成时才增长**（正常连接即写即回收，`queue ≈ 0`），
  也说明 `--slow` 单独使用不适合作为「必然触发断开」的回归用例；回归请用 §2 的命令。

## 4. 健康连接下的 RSS（`tools/soak.mjs`，与 §6 #4 同一命令）

`node tools/soak.mjs --minutes 5 --sample 10 --out docs/evidence/soak-5min-o03.md --json docs/evidence/soak-5min-o03.json`

- `verdict = pass`；RSS 稳态斜率 **0.278 MB/分钟**（阈值 <1，剔除前 60s 预热；全窗口参考值 0.417）；RSS 81.75 → 85.67MB。
- CPU 均值 **2.82%** / P95 6.24%；房间峰值 1、结束回落到 0；未捕获异常 0、未处理拒绝 0；`/metrics` 采样失败 0。
- 退出码 1 的唯一原因是 `S5.2-5`（4 人 60 羊单核 CPU）在本机**未测量**（羊峰值 21 < 60）；其余门槛全 pass。
- 结论：健康连接下队列不常驻，池复用没有引入每帧分配（斜率与 O02 的 0.413 同量级且更低）。

## 5. 指标接线自检

```bash
curl -s localhost:8787/metrics | grep -E "slow_client|send_queue"
```

- `ac_slow_client_drops_total`（counter，# TYPE counter）：慢连接因积压被丢弃的帧数累计；
- `ac_send_queue_bytes`（gauge，# TYPE gauge）：所有连接当前积压字节和；健康场景为 0。
- `packages/server/src/metrics.test.ts` 新增 1 例固定这两行的渲染与 type 行。
