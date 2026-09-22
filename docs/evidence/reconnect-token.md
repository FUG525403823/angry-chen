# 会话令牌与重连身份（O08）

> 生成时间：2026-09-22 ｜ 本机 Node v24.14.1 ｜ 对应 ADR-006 / `docs/优化/O08-会话令牌与重连安全-ADR-006.md`

## 1. 结论

宽限期重连的身份判定**只认令牌**：`findGracedSession(room, token)` 要求 `token.length > 0 && session.token === token`，
昵称不参与身份判定（仍用于显示与聊天）。服务器在每次成功 join 时下发 8 字节槽位（8 个十六进制字符）的会话令牌，
客户端存在**每标签页独立**的 `sessionStorage`（`TOKEN_STORAGE_KEY = 'ac.sessionToken.v1'`），重连时随 `join` 上行。

## 2. 能证明该性质的可复现证据

| 证据 | 断言 | 能证明 |
|---|---|---|
| `packages/server/src/rooms.test.ts` ① | 令牌正确 → 复用 `pid`、名额保留、`metrics.graceReconnects = 1` | 令牌能换回原会话 |
| 同上 ② | 令牌错误（同昵称）→ 得到**新** `pid`、房间内 2 个会话、`graceReconnects = 0` | 知道昵称**不能**接管（原会话仍在宽限期） |
| 同上 ③ | 无令牌（同昵称）→ 同样是新玩家、`graceReconnects = 0` | 空令牌不会误命中 |
| 同上 ④ | 宽限期过期（+31s）后再带**正确**令牌 → 新玩家、令牌被重新生成、`graceReconnects = 0` | 过期即失效 |
| `packages/server/src/match/lifecycle.test.ts`（既有） | 10 秒内重连 → 复用 `pid` 与实体、生命恢复到 ≥50% | 正常回座未被令牌改动破坏 |
| `packages/client/src/net/connection.test.ts`（新增 2 例） | welcome 里的令牌写入存储；下一次 Join 带上它；重连被拒后令牌清空 | 客户端确实携带/清理令牌 |
| `packages/shared/src/net/codec.test.ts`（新增 3 例） | `welcome` 24 字节往返、旧 16 字节被拒；`join` 带令牌往返、旧长度解析为空串；非十六进制/非 ASCII 令牌被拒 | 线上布局与长度校验 |
| `node tools/bots.mjs --players 4 --minutes 2 --strict --out docs/evidence/bots-o08.json` | 12 项门槛全 PASS（除既有未测量项 `S5.2-9b`）；`S5.2-9`、`S5.2-10` 不受影响 | 新协议没有破坏现网流程 |

## 3. 人工「两标签页同昵称」测试（**本机未执行**）

本机**没有可用浏览器**，O08 §6 #4 的人工步骤无法执行，如实移交 O10。步骤（目标机）：

1. `pnpm dev`；窗口 A 打开 `http://localhost:5173`，昵称填 `alice`，创建房间并记下 4 位房间码；
2. 窗口 B（同一浏览器**另一个标签页**）打开同一地址，昵称也填 `alice`，房间码填 A 的房间码，点加入；
3. 期望：B 成为**新玩家**（不会顶掉 A 的会话）；关掉 A 后刷新 B，B 也不会继承 A 的 `pid`/位置。

补充观察点：在 B 的 DevTools 里 `sessionStorage.getItem('ac.sessionToken.v1')` 应能看到令牌，且 A/B 的令牌不同（每标签页独立）。

## 4. 已知边界（不在本步解决）

- 令牌熵：线上槽位 8 字节只能装 8 个十六进制字符（32 位）。文档中「`randomBytes(8)`/16 个十六进制字符」与 24 字节 `welcome` 布局互相矛盾，本步取**字节布局为准**（详见 O08 §5.1 #1 与 ADR-006）；32 位仍强于 4 位房间码（≈20 位）这一既有秘密。
- 令牌与房间生命周期一致，无轮换/过期策略；`sessionStorage` 丢失（隐私模式）时退化为新玩家，对局中被拒并提示「会话已失效，请作为新玩家加入」。