# P1 HUD / 奖励呈现只读审计

日期：2026-11-04。源码参考：`3dda8cb`。范围：现有P1远征生产uGUI及相关公共测试的覆盖边界，不扩大P1产品屏幕或功能范围。

## 结论与真实未验声明

本记录来自源码及测试阅读，**没有查看截图，没有启动引擎，没有执行本记录中的截图步骤，也没有获得人工视觉或人类试玩通过结论**。已存在的模型、协议、输入及引擎自检通过结果，不等于远征Canvas在真实player中的视觉通过。本文不将技术逻辑测试替代文字可读性、点击命中、目标理解或有限人类试玩。

## 已有覆盖及其边界

| 项目 | 已有公共测试 / 实现 | 不能据此宣称通过的内容 |
|---|---|---|
| 720p / 1080p / 1440p | [RenderSuite](../../client/Assets/Tests/RenderSuite.cs)中的`ChecksOverlayInBounds`、`ChecksLobbyMenuLayout`覆盖1280×720、1920×1080、2560×1440的旧IMGUI OverlayModel矩形及大厅分区 | 这些测试没有驱动远征State14及专用ExpeditionUiRenderer，**不能替代P1 uGUI Canvas的分辨率覆盖** |
| 字体及中文 | [HudSuite](../../client/Assets/Tests/HudSuite.cs)的`ChecksTextAndFont`检查旧HUD字号常量、字体回退配置及UI目录约束 | 没有证明远征卡条款实际字形、长中文换行、截断、字号可读性或目标机器字体可用性 |
| 奖励 / 权威恢复 | [ExpeditionUiSuite](../../client/Assets/Tests/ExpeditionUiSuite.cs)覆盖权威候选倒计时、pending与精确回执、拒绝、旧run过滤、生产GameLoop完整状态恢复等公共行为 | 没有检查真实Canvas的Text生成结果、卡片像素布局、GraphicRaycaster点击命中或按钮显示反馈 |
| 输入模态 | 同一套测试覆盖远征模态禁切枪、Enter/开火抑制、零命令和关闭后的释放帧防穿透 | 注入采样器测试不等于真实uGUI焦点、鼠标事件、设置/聊天叠加及操作系统焦点恢复验证 |
| PlayMode smoke | [PlayModeSmoke](../../client/Assets/Tests/PlayModeSmoke.cs)检查至少60帧、bootstrap及启用的相机 | 没有P1奖励Canvas、卡条款、按钮或目标标记的视觉断言 |

## 生产呈现与待验风险

生产代码为[ExpeditionUiRenderer](../../client/Assets/Scripts/Boot/ExpeditionUiRenderer.cs)，由[GameBootstrap](../../client/Assets/Scripts/Boot/GameBootstrap.cs)接线；[ExpeditionHudModel](../../client/Assets/Scripts/UI/ExpeditionHudModel.cs)只投影权威状态及请求pending，不发放本地奖励。

- Canvas使用`ScaleWithScreenSize`、1280×720参考分辨率及固定矩形。三卡各306×218参考像素、卡条款字号18。源码配置不是三种分辨率真实显示证据。
- 卡条款含收益、条件、代价和前置；需要实际检查换行后内容完整，而不是只确认字符串存在。
- 按钮`Navigation.Mode.None`；自行创建EventSystem时设置`sendNavigationEvents=false`。当前奖励操作为鼠标点击，**没有实现Tab导航或Enter选择卡片**。这属于当前功能缺口，不应声称已具备键盘选卡；如需新增，另行确认范围，不作为本次审计顺手扩展。
- 若场景已有EventSystem，本类不会重新关闭其导航事件。实际Enter焦点行为与误触需要验证；采样器Enter抑制测试不能证明EventSystem行为。
- 构筑文本参考矩形为`x=18..1118, y=590..690`；治疗脉冲按钮为`x=980..1255, y=615..669`，二者矩形相交。**这只是源码几何风险，不是已观察到的遮挡缺陷**；较长构筑文字是否进入按钮区域需截图确认。
- 目标投屏标记、距离/方向、屏边夹取及进入6米防守圈提示已有实现，但其清晰度、遮挡及玩家能否据此找到设施仍未由本审计验证。

## 首次窗口捕获尝试（未通过视觉门）

独立runner增加WindowWidth/WindowHeight参数（默认仍960×540），manifest仅记录requested尺寸，不声称实际尺寸。离线guardrail先RED exit1再GREEN exit0。以干净3dda8cb release请求1280×720启动，真实单人541dac29轮完整三节点技术PASS、exit0。

本机 `build/reports/p1/ui-720-3dda8cb/` 保留14张Win32 PrintWindow(flags3)原图与capture-manifest/pixel-check；按明确exe路径匹配进程，不抓取无关窗口。捕获器GetClientRect及生成位图均为1024×576，而非请求1280×720；尚未区分DPI虚拟化、系统缩放或引擎尺寸差异，不能称720p证据。图像像素非纯色只排除全黑/单色，不能证明中文完整或渲染正确。当前模型不支持图像输入，read_image明确失败，未审阅、不填写视觉通过；5秒间隔且无阶段锁定也不保证捕获到了奖励窗口。原图未上传源码。

DPI-aware对照已进行：新起干净3dda8cb player请求1920×1080，同一个HWND在旧线程context下GetClientRect为1536×864，切换PerMonitorV2(-4)后为1920×1080，GetDpiForWindow=120（125%）。该同窗对照明确证明捕获器存在DPI逻辑坐标虚拟化；首次1024×576恰为1280×720的80%，但没有对首次旧窗做同窗复测，因此保留旧图不追认720p。新本机 `build/reports/p1/ui-1080-dpi-aware-3dda8cb/` 14张原图均1920×1080且非单色，另node2目录保留后续时间窗捕获；PrintWindow返回true仍不是内容正确性证明。此次测到OS物理client尺寸，未新增引擎Screen.width/height遥测；不能将其当内部渲染分辨率、字体清晰度或人工视觉验收。图像仍未审阅，不把放大旧PNG当高分辨率证据。

## 1440p开局高频捕获（未审阅）

捕获器先启动，再启动同一clean release，100ms采集间隔（实际PNG写盘会增加间隔），DPI-aware实际客户区与原图均2560×1440、DPI120。本机 `build/reports/p1/ui-1440-opening-3dda8cb/` 保留239张原图及manifest；9b32b1aa真实单人完整三节点技术PASS、exit0。没有为拍图调整奖励期限、推进速度或候选。

`window-7280-083.png`时间近似关联首offer1、stage1、候选[1,4,0]，`window-7280-090.png`近似关联combat。关联使用进程StartTime与driver.elapsed，二者存在启动偏差，不能当same-frame权威证明；原始timeline与capture UTC均保留。两图非单色只排除纯色截图，不代表奖励确实可读。模型不支持图像输入，所有visualReviewed=false，未获得人类结论。其余尺寸奖励及后续长卡条款仍需精确关联与审阅，不因239张数量多宣布覆盖通过。

## 最小下一步证据方案（尚未全部执行）

保持同一冻结release player及本地权威服务器，Unity / player与其他引擎工作按项目要求串行，由lead安排。不为拍图修改权威奖励、冷却、目标推进或卡内容，也不用纯fixture截图冒充联机状态。

1. **冻结并标识来源**：记录源码commit与dirty投影说明、player可执行文件及包hash、服务器版本、启动参数、目标分辨率及实际窗口尺寸。不同构建的截图不得混作同一版本证据。
2. **仅三个现有目标尺寸**：分别以1280×720、1920×1080、2560×1440运行，不扩充超宽屏或其他新适配矩阵。
3. **每尺寸奖励截图**：通过真实房间显式主机入口进入远征，保存初次三卡（启动卡及补给）截图；后续安全奖励窗口另保存较长中文条款截图，优先覆盖卡6的收益、代价及前置。记录实际候选，不伪造候选或已持卡。
4. **每尺寸战斗截图**：保存目标HP、净化进度、方向/距离或圈内提示、当前权威构筑与治疗脉冲冷却同时可见的画面；检查构筑文本与工具区域是否互相遮挡。构筑只使用该次真实选择所得状态。
5. **1080p最小交互短录屏**：覆盖点击→pending→accepted或reject、一次重抽耗尽、跳过/截止转补给；按住Mouse0及Enter跨奖励关闭，检查释放帧不误开火、切枪或准备。再检查设置/聊天及操作系统焦点恢复不误触。记录实际发生的拒绝；没有发生的分支标记未验，不编造通过。
6. **关联回执与图像**：对每张截图 / 录屏片段记录时间、runId、node、stage、revision、offerId、候选、已持构筑、lastRequestId和lastResult；操作片段同时保留发送requestId、op、choice及收到的State14。图像、日志和关联表使用一致标识，保留原始图像及失败轮，不只保留成功画面。
7. **人工逐项判定**：检查中文字形无缺字、所有收益/条件/代价完整、字号可读、卡片及按钮无裁切、点击与显示相符、pending/拒绝可理解、目标导航可找到设施。记录观察人、尺寸、证据标识、通过/失败/未验及具体问题。未看图不得填写视觉通过。

上述步骤提供呈现与交互证据，仍不替代[有限退出清单](enhancement-p1-checklist.md)中的人类试玩：玩家无需口头帮助完成首节点、能说明两种打法差异，以及选卡耗时、失败原因与重开意愿，需要真实人类记录。自动化不得代填“好玩”或人类通过。
