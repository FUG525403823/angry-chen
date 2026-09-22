# 真人试玩反馈三类缺陷：根因、修复与浏览器内复验（2026-09-22）

真人试玩反馈原文（按缺陷归并）：

1. 鼠标左右滑动跟实际画面相反；WASD 里 W/S 前后相反（A/D 正常）。
2. 画面巨卡无比，一卡一卡的。
3. 羊根本看不到、也打不到、看不到弹道；羊还会离奇消失、不会移动。

结论：第 1、3 条是确凿的代码缺陷，已修复并在**真实 Edge（硬件 GL）**里逐条复验通过（见 §1–§3）；
第 2 条在本次测量中**未能复现**：生产产物与 Vite dev 两种形态、1 人与 4 人局、两种渲染分辨率下
都是锁 60 FPS、帧间隔 p95 16.8ms、每帧工作 p95 ≤ 2.4ms（见 §4）。为此交付了可复现的测量工具
`tools/e2e-cdp.mjs` 与 F3 面板的分阶段读数，便于真机自查定位。

## 1. 视角反向 ⇄ 输入反向 ⇄ 打不到羊（同一个根因）

### 1.1 根因

模拟（`@ac/shared`）约定：前向 `forwardFromYaw(yaw) = (sin yaw, 0, cos yaw)`，
瞄准方向 `yawPitchToDirection(yaw, pitch)` 与之同源，服务端射线判定用的就是它
（`packages/server/src/room.ts`）。而 three.js 相机沿 **-Z** 看，客户端却直接
`camera.rotation.set(pitch, yaw, 0)`，于是相机与权威方向相差 180°。

用仓库真实模块 + 真实 three r186 在 Node 内量化的旧行为：

| 检查 | 结果 | 含义 |
|---|---|---|
| `camera.getWorldDirection() · forwardFromYaw(yaw)`（yaw = 0, π/2, π） | **-1.000** | 相机看向权威方向的反面：贴近战羊也打不中（射线朝身后） |
| `相机屏幕右向 · rightFromYaw(yaw)` | +1.000 | 横移轴恰好是对的（A/D 正常） |
| `addMouse(deltaX > 0)` 使 `yaw` 增大 | 增大 | three 的 yaw 增大 = 左转，于是「鼠标右移 = 视角左转」 |

三条现象（W/S 反、鼠标左右反、打不到羊）都由这一个 180° 失配派生，另加两处符号选择错误。

### 1.2 修复

- `render/scene.ts`：新增 `SIM_TO_VIEW_YAW_OFFSET = Math.PI`，`updateCamera` 用
  `camera.rotation.set(pitch, yaw + SIM_TO_VIEW_YAW_OFFSET, 0)`。
- `input/sampler.ts`：`state.yaw -= deltaX * scale`（鼠标右移 = 右转）；
  横移轴反号 `moveY = (left - right)`（加 π 后「右」落在画面右侧）。
- `main.ts`：开火/枪口特效的瞄准方向改用 `yawPitchToDirection(yaw, pitch)`，
  与服务端射线**同一函数**，成为唯一真源（不再用 `camera.getWorldDirection`）。
- `shared/math.ts`：仅补注释说明 `rightFromYaw` 实际返回模型左手侧（镜像基），**不改模拟约定**
  （服务端射线、AI、快照、既有测试都绑在它上面）。

### 1.3 复验

- 单测：`packages/client/src/test/viewConvention.test.ts` ——
  `相机前向 · yawPitchToDirection(yaw, pitch) > 0.999`（yaw 采样 0/0.7/π2/2.2/-1.4/π × pitch 0/±0.4）、
  `相机屏幕右向 · (-rightFromYaw) > 0.99`、正 pitch 仍为抬头、视图偏移恒为 π。
- 单测：`packages/client/src/input/sampler.test.ts` —— 鼠标右移产生负向 yaw 增量、D 键送负 moveY。
- 浏览器内（`pnpm build:client && node tools/e2e-cdp.mjs --headful`）：

| 断言 | 实测 |
|---|---|
| 鼠标右移（movementX > 0）→ yaw 减小 | Δyaw −0.1200 |
| 按 W 朝视线方向前进（模拟前向 = (sin yaw, cos yaw)） | 位移 3.39m，方向点积 **1.000** |
| 把视角转向羊后羊落在准星中心 | 距离 28.2m，屏幕 NDC x **0.000** |
| **真的打中了羊**：准星对准最近的羊并持续开火，服务端复制的 `hpRatio` 下降 | 1 人局 `1.000 → 0.686`；4 人局（3 bot）`1.000 → 0.584` |

最后一条是端到端判据：客户端只发 `command.yaw/pitch`，命中的判定完全在服务端射线里，
所以 HP 下降同时证明了「客户端准星方向 == 服务端射线方向」与「羊是可被命中的目标」。

## 2. 羊看不到 / 离奇消失

### 2.1 根因

羊由 4 个 `InstancedMesh` 绘制（`sheep-body-*` / `sheep-wool-*` / `sheep-horn-*` / `sheep-emblem`），
它们都设了 `frustumCulled = true`。three r186 的 `Frustum.intersectsObject()` 只在
`object.boundingSphere === null` 时计算**一次**并永久缓存；`InstancedMesh` 按实例算包围球，
官方注释明确要求「改了实例矩阵要自行重算」。

启动首帧羊数为 0 ⇒ 缓存下来的是**空包围球（球心 = 世界原点、半径 −1）**。实测（8 只羊站在玩家正前 12m）：

| 相机朝向 | 4 个羊 mesh 的剔除判定 |
|---|---|
| 朝竞技场中心（世界原点在视锥内） | 不剔除 → 能看到羊 |
| 背对世界原点（正常打羊姿态） | **全部剔除 → 羊整片消失** |

实例矩阵本身是对的（实例位置与快照坐标逐点一致），所以这是纯渲染剔除问题。
「不会移动」是同一现象的观感：羊大部分时间不在画面里，只在转身朝原点时才闪现。

### 2.2 修复

- 4 个实例网格 `frustumCulled = false`（与既有 `smallMesh`/粒子/曳光一致；每形态 1 个 draw call）。
- 顺手去掉空场帧的无谓上传：只有本帧真的写过实例（`count > 0`）或实例数变化时才置
  `instanceMatrix/instanceColor/needsUpdate`（此前每帧无条件重传全部实例缓冲，wool 一块就是 3072×64B）。

### 2.3 复验

- 回归测试 `packages/client/src/test/sheepCulling.test.ts`：先按启动顺序（0 羊）污染缓存，
  再摆 8 只羊在玩家正前并让相机**背对世界原点**，断言 4 个 mesh 均未被剔除
  （判定与 `WebGLRenderer` 同构：`!frustumCulled || frustum.intersectsObject(mesh)`）。
  该用例在修复前为红（旧实现下 12/12 mesh 全部 culled），修复后为绿。
- 浏览器内：`{"body":{"culled":false,"count":9},"wool":{"culled":false,"count":72}}`，
  羊数 9→18 随人数变化，1.5s 内位置发生变化（快照插值生效）。

## 3. 看不到弹道

### 3.1 根因

曳光从 `camera.position` 沿视线方向画 30m ⇒ 与视线**共线**，正面投影退化成屏幕中心的一个点，
肉眼几乎不可见；且 `TRACER_LOCAL_MS = 45`、线宽 0.012m，低帧率下常常不足一帧。

### 3.2 修复

- `render/scene.ts` 新增 `muzzleOrigin()`：起点 = 眼位 + 前 0.55m + 右 0.20m − 上 0.12m（枪口），
  本地曳光、命中曳光与枪口火焰都从这里出发；`main.ts` 用同一个 scratch，无新增每帧分配。
- `render/effects.ts`：`TRACER_LOCAL_MS` 45 → 120（`TRACER_REMOTE_MS` 90 → 120），
  曳光截面 0.012 → 0.02。

### 3.3 复验

- 单测 `packages/client/src/test/viewConvention.test.ts`：枪口起点在近裁剪面之外且与视线不共线
  （`|offset · forward| < 0.99`，理论值 0.55/0.597 = 0.92）。
- 浏览器内：开火后曳光实例存在（count 1），起点距眼位 **0.544–0.617m**、线段长度 **30.00m**
  （旧实现起点距眼位恒为 0）。

## 4. 画面卡顿：测量方法、数据与结论

### 4.1 方法

新增 `tools/e2e-cdp.mjs`（零新依赖：node 内置 + 仓库已有 ws）：起真实服务器 + 启动 Edge
（`--headless=new` 或 `--headful`，`--remote-debugging-port`），用 DevTools 协议驱动真实客户端
打完一局，采集 `renderer.getStats()`（fps / 帧间隔 p95 / 每帧工作 p95）、`getRenderStats()`
（draw calls / 三角形）、`GL renderer` 字符串、分阶段 p95，并对缺陷 1/3 做断言。

```
pnpm build:client && node tools/e2e-cdp.mjs --headful --seconds 12
# 4 人局重载：先 node tools/bots.mjs --players 3 --minutes 4，再
node tools/e2e-cdp.mjs --no-server --headful --join-room <CODE> --seconds 60
```

### 4.2 数据（GL：`ANGLE (AMD, AMD Radeon(TM) Graphics …, D3D11)`，非软件渲染）

| 场景 | 渲染目标 | fps 中位 | 帧间隔 p95 中位 | 每帧工作 p95 | draw | 三角形 |
|---|---|---|---|---|---|---|
| 生产产物 1280×800 窗口 | 1256×708 | 60.0 | 16.8ms | 2.0ms | 23 | 8060 |
| 生产产物最大化（2560×1440 屏） | 2552×1354 | 59.9 | 16.8ms | 2.4ms | 27 | 8164 |
| Vite dev（localhost:5173） | 1256×708 | 60.0 | 16.8ms | 1.1ms | 23 | 8060 |
| 4 人局（3 个 bot + 真人客户端，18 只羊） | 1256×708 | 60.0 | 16.8ms | 0.8–1.5ms | 21 | 4436–5042 |

分阶段 p95 典型值（ms）：`input 0.1 / sync 0.2 / predict 0.1 / fx 0.2 / audio 0.2 / draw 1.2–2.2 / overlay 0.2 / hud 0.1`。
即在一帧 16.8ms 的预算里，**draw 占 7%–13%，全部 JS 阶段合计 < 1ms**，没有任何一项接近阈值。

客户端 JS 侧的独立核对（进程内、真实模块）：64 只羊的 `views.sync` 0.055ms/帧、特效更新 0.006ms/帧、
曳光 0.003ms/帧；客户端源码无强制同步布局调用（无 `getBoundingClientRect`/`offsetWidth`/`getComputedStyle`）。

### 4.3 结论与自查路径

本次在两种产物形态、两种分辨率、1 人/4 人局下都**无法复现**卡顿，与「60 FPS，p95 ≤ 20ms」的
P04 DoD 3 一致（读数与命令见上表）。因此卡顿更可能来自运行环境而非当前代码，按可能性排序：

1. 显示器缩放 > 100% 且窗口很大：渲染目标随 DPR² 放大（`setPixelRatio(min(dpr, 1.5))`，150% 缩放下
   2560×1440 会变成约 3840×2160 且带 MSAA）。本机 Edge 在 `--force-device-scale-factor=1.5` 下把窗口
   压回同等像素，未能量出该组合。
2. 浏览器/GPU 状态：硬件加速被关、驱动回退到软件渲染、省电模式降频、后台标签/其他程序抢占 GPU。
   `tools/e2e-cdp.mjs` 会把 GL renderer 字符串写进报告；含 `SwiftShader`/`llvmpipe` 即为软件渲染。
3. 服务器/网络形态差异（例如另开进程、反向代理、远端主机）。

真人自查步骤（30 秒）：进游戏后按 **F3** 看两行——
`fps … p95 …/20ms work …ms` 与新增的 `stages input … draw …`。
若 `draw` 占大头 → 分辨率/GPU 侧（先降到窗口模式或用 100% 缩放复测）；
若某个 JS 阶段占大头 → 把该行数字反馈即可定位到具体阶段。

## 5. 本次改动的测试与门禁

| 项目 | 结果 |
|---|---|
| `pnpm typecheck` | 3 包全绿 |
| `pnpm lint`（eslint + prettier） | 全绿 |
| `pnpm test` | 74 文件 **488 用例**全绿（新增 viewConvention 5、sheepCulling 1、frameProfiler 2，sampler 增 1） |
| `pnpm check:docs` / `check:count` | 通过（488 ≥ 300） |
| `node tools/e2e-cdp.mjs`（生产产物） | **15/15** 断言通过（报告：`docs/evidence/playtest-cdp.md`） |
| `node tools/e2e-cdp.mjs --no-server --join-room <CODE>`（4 人局 + 3 bot） | **15/15** 断言通过（报告：`docs/evidence/playtest-cdp-multiplayer.md`） |
| `node tools/e2e-cdp.mjs --no-server --url http://localhost:5173/?debug=1`（Vite dev） | 14/14 断言通过（该次运行早于命中断言加入；报告：`docs/evidence/playtest-cdp-dev.md`） |

新增可观测性：`render/frameProfiler.ts`（8 段 p95）+ F3 面板 `stages` 行 + `?debug=1` 下的
`window.__ac`（sampler / view / predictor / profiler / renderer / camera / scene / connection）。
`?debug` 之外的路径不受影响。

## 6. 未验证与遗留

- 缩放 150% + 大窗口（约 3840×2160 渲染目标）这一组合未能在本机量出（Edge 会压回窗口像素）。
  若真机反馈仍卡，这是第一优先复测项。
- 头无头模式下 Edge 会拒绝指针锁定（`pointerlockerror`），因此鼠标/W/曳光三类断言在无头模式下跳过，
  需用 `--headful` 复验（无头模式仍会验证羊、视角、帧率）。
- 未改动：`@ac/shared` 的 yaw/坐标约定、服务端射线与 AI、网络协议与快照格式、竞技场与美术资产、平衡数值。
