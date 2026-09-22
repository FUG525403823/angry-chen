# 客户端每帧分配与 UI 节流（O06）

> 生成时间：2026-09-22 ｜ 本机 Node v24.14.1 ｜ 命令可复现（§2、§3 各给出）

## 1. 测量边界（先说清能证明什么）

本机**没有可用浏览器**，无法执行 O06 §6 #4 要求的「4 人局 + DevTools Performance 看 GC 次数 / 调试面板帧 p95」人工观察。
因此本文件用三条**可复现的代理证据**替代，并如实标注边界：

| 证据 | 能证明 | 不能证明 |
|---|---|---|
| `tools/probe-client-dom.mjs`（假 DOM 驱动真实 `hud.ts`） | 每帧 DOM 写入次数（改动前后同场景对比，§2） | 浏览器里的样式重算/合成成本、GC 次数 |
| 单元测试 `sheepInstancePool.test.ts` / `hud.test.ts` / `percentile.test.ts` | 羊群实例引用帧内恒等且跨帧复用、同值不触达 DOM、分位统计与旧实现逐值一致 | 真实渲染耗时 |
| `packages/client/vite.config.ts` 的体积门 | 体积仍在 1.5MB 预算内（§3） | 每帧分配字节数 |

## 2. 每帧 DOM 写入（同场景同 600 帧）

```bash
node tools/probe-client-dom.mjs --frames 600          # 改动后
# 改动前（把 hud.ts 换回 O05 版本再跑同一探针）
git stash push -- packages/client/src/ui/hud.ts
node tools/probe-client-dom.mjs --frames 600
git stash pop
```

| 版本 | 合计写入（600 帧） | 每帧 | 明细 |
|---|---|---|---|
| 改动前（O05 的 `hud.ts`） | **6001** | **10.002** | 10 个节点每帧无条件写：healthFill / healthAfter / healthText / armorFill(display+width) / ammoText / ammoReserve / rageFill / rageText / reloadRing.opacity 各 600 次 |
| 改动后（O06） | **1861** | **3.102** | ammoText 600、rageFill.width 600、rageText 600（探针样本流里这三项**每帧数值真的在变**）、reloadRing.opacity 21、healthFill.width 16、healthText 16、healthAfter.width 4、line.textContent 1、armorFill.display 1、armorFill.width 1、ammoReserve 1 |

- 恒定值路径（血量台阶、护甲、备弹、状态行）从**每帧 600 次**降到**只在数值变化时写**（本样本流 1–16 次）；
  探针刻意让弹药/怒气/准星每帧微动，这三项仍是每帧写 —— 这正是 §5「四舍五入到 UI 显示精度后再比较」的预期行为（值变了就该写）。
- `hud.test.ts` 的 spy 计数用例固定了边界：同值两次 `set()` → `style.width` / `textContent` 各只写 1 次；值变化 → 必须写。

## 3. 帧统计与体积

| 项 | 改动前 | 改动后 |
|---|---|---|
| `renderer` 分位统计 | 每 30 帧把 240 个样本拷进 `scratch` 再对 `subarray` 排序 | `percentileOf` 原地插入排序，零拷贝零分配；200 组随机样本与旧实现逐值一致 |
| 客户端 JS gzip 体积（index + three） | 175,547 B（index 41,039 + three 134,508） | **176,827 B**（index 42,319 + three 134,508）＝ **+1,280 B（+0.73%）**，占 1.5MB 预算 11.8% |

> 体积**未做到「不增」**：池模块 + 脏检查缓存的代码量换来了每帧分配与 DOM 写入的消除，+0.73% 已记入 O06 §5.1 与优化 README §5 门槛登记。预算 1.5MB，门仍 pass。
> 复现命令：`pnpm --filter @ac/client build`（`vite.config.ts` 的 `ac-size-budget` 插件打印每个 chunk 的 gzip 字节与合计）。

## 4. 未测量项（移交）

`p95IntervalMs` / `p95WorkMs` 面板值与 DevTools 的 GC 次数都需要在浏览器里跑（`pnpm dev` + Edge 或 `pnpm e2e:edge`）——本机未执行，移交 O10 的待复测清单。
建议复测口径：同场景 30 秒窗口、改动前后各录一次 Performance profile，比较 Major GC 次数与帧 p95；若差异小于本机噪声，按 O06 §7「如实写明」处理。