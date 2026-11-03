# Codely开发工具依赖审查与有限P0验收

## 当前结论（2026-11-03）

本节取代旧1.0.85审查的版本及“必须另获授权才能继续”的推断。当前Bridge 1.0.87与Generators 1.0.27通过**精确开发工具准入**；不属于游戏运行时白名单，不允许`cn.*`通配，不据此宣称P0整体通过。原始manifest、lock及供应商源码未由本任务修改；本任务未启动Unity/player、未提交。边界决策见[ADR-021](../00-共识/ADR/ADR-021-开发工具精确准入与发布隔离.md)。

## 固定来源与字节证据

| 包 | 引用/来源 | 文件树与归档SHA256 |
|---|---|---|
| cn.tuanjie.codely.bridge 1.0.87 | registry `https://packages.tuanjie.cn`；[官方TGZ](https://download-packages.unity.cn/cn.tuanjie.codely.bridge/-/cn.tuanjie.codely.bridge-1.0.87.tgz) | 435文件，树 `594116ac4f65997d451615df203eaad78bc2a81257e98d884e93ad090fbfcda4`；TGZ `439f185b01f24ac6f57869ff695d34bfe85e9fa7f2eb73900c06f8f0ed20e810` |
| cn.tuanjie.ai.generators 1.0.27 | `file:../.codely-cli/extensions/TJGenerators/Packages/cn.tuanjie.ai.generators`；[官方ZIP](https://codely.tuanjie.cn/skills/download/f3f76746-78d0-4e83-9fc0-83e550e9ba62/TJGenerators.zip) | 官方705原文件树 `8af2389f32fe2cb3bc36225dab09d45d7e546a4709305cef76faeb0f3dc2b528`；ZIP `6f910aff63696cd240023fe56a9faf3715822b5db3533bd9360bebe6cfcec589`；本机757文件树 `5b0a91fb2e640e40489dbd7aaca84fb4f8a3152300ea2cf53c31ee2f934f9d9a` |

树算法：相对路径统一`/`，排序，对每项依次SHA256.update(path+NUL)、update(fileBytes)、update(NUL)。Generators的705原文件与官方包完全一致，另外52个Unity生成meta按逐路径SHA256精确登记为可选；缺失这些meta不妨碍冷获取，修改任何一个或新增未知meta均失败，绝非忽略全部meta。记录见[准入记录](../../tools/codely-admission.json)。

[验证器](../../tools/codely-dependencies.mjs)同时检查包名/版本、manifest精确引用、lock source/version/registry、工具的直接依赖闭包和editorcoroutines/nunit固定来源。Generators声明Bridge最低1.0.76，当前直接依赖将其解析为1.0.87；二者分别登记，不把声明的最低版本误作实际已安装版本。

[获取器](../../tools/codely-fetch.mjs)只读记录中的HTTPS官方URL，下载后先验归档SHA256再列出并检查路径、解包、验证包树；默认仅staging，显式`--install-missing`只安装不存在的目标，不覆盖已有目标。不修改manifest/lock、不执行供应商代码。新归档hash即使版本号不变也失败。使用系统tar（Windows bsdtar支持ZIP）；冷网络失败不能替代通过。

## 使用权与开发工具风险

前序实际HTTP200核验的[官方条款](https://www.codely.tuanjie.cn/terms)与[安装指南](https://codely-docs.tuanjie.cn/using-codely/codely-bridge-installation-guide/)继续作为证据上下文：指南提供Cowork自动安装/Package Manager安装；动态条款标题“Tuanjie AI 内测服务条款”，生效2025-11-15；前序保存正文位于build/reports/enhancement-resume/codely-*。本次不声称重新完成法律审查，也不凭包内未找到LICENSE/NOTICE推导用户无权本地使用。条款§9.1竞争服务限制、§9.3输入/上传/生成内容宽泛授权涉及源码隐私；不能自动推导允许把所有供应商库再分发到游戏。

源码定位确认Bridge含Assembly.Load动态编译、Process.Start，以及若干辅助Loopback监听点；这些点**不是主Bridge监听范围的证明**，当前主监听由闭源NativeTcpBridge实现，不能据此声称仅回环或鉴权已审计。Generators含进程启动和联网生成工具。Bridge当前28个PluginImporter meta中有8项Standalone插件启用，Editor asmdef也不能保证它们不会发布；本P0由隔离投影整体排除这两个工具依赖，再执行实物审计控制风险，不修改供应商插件配置，也不把闭源鉴权全面逆向新增为无限安全门。这是具有开发者权限的执行工具，不是无害静态数据；本次没有执行工具、联网上传项目或做完整鉴权/全依赖安全认证。建议仅在可信开发机启用，不给不可信输入/远程访问，不将工程秘密上传；风险披露不无限扩张P0准入条件。

## 发布边界：真实反向已确认，投影后仍需实际stage审计

Generators的Runtime/TJGenerators.Runtime.asmdef includePlatforms/excludePlatforms均空，autoReferenced=false不能证明不会编译入包。2026-11-03实际只读扫描旧stage：

```text
node tools/codely-dependencies.mjs audit-release . client/Build/Windows64/stage/20261029T031527354Z-62132179
CODELY ADMISSION FAIL Development dependency leaked into player: angry-chen_Data\Managed\TJGenerators.Runtime.dll
exit 1（预期反向命中）
```

因此旧记录中“仅15个二进制名未命中”不能套用于当前Generators。Lead负责[一次性发布投影](../../tools/p0-project.mjs)，排除开发工具后重新构建；本任务不改该文件，不删除原发布DLL、不改供应商asmdef/meta、不改外部manifest/lock。

审计接口保持`node tools/codely-dependencies.mjs audit-release <workspace-root> <actual-stage>`，或`auditRelease(root,stage)`返回`{pass:true,stage,files}`，失败throw/CLI exit1。必须传入真实展开的发布目录，不能传ZIP或空目录；递归禁止symlink，检查供应商所有非meta文件SHA256（含重命名原生库）以及已知编译程序集/插件名称（名称大小写不敏感、即使字节已重编译也拒绝）。不承诺检测经改写/重新打包并改名的任意代码，不把此有限泄漏门说成完整软件供应链安全认证。新投影stage审计由Lead在实际构建后调用，未出包前不记通过。

## 本次实际执行

| 命令 | 结果 |
|---|---|
| `node tools/test-codely-dependencies.mjs` | exit0；精确包正向，版本/registry/内容/无扩展名新增/依赖闭包漂移反向；可选meta逐hash；空stage拒绝、干净stage正向、重命名vendor哈希及不同字节Runtime DLL反向；归档hash和路径穿越正反；缺包拒绝 |
| `node tools/test-check-assets.mjs` | exit0；0/1/2未知依赖摘要，精确Bridge正向、版本漂移及未知cn包反向，共6组 |
| `node tools/codely-dependencies.mjs verify .` | exit0；当前两包与锁定信息、文件树一致 |
| `node tools/check-assets.mjs` | exit0；719受控文件，无二进制素材；当前36直接依赖含2个精确开发准入，其余34官方白名单 |
| `node tools/codely-fetch.mjs /tmp/ac-codely-fetch-20261103-1457` | exit0；真实下载两官方归档，hash及解包树均通过 |
| `node tools/codely-fetch.mjs /tmp/ac-codely-fetch-20261103-final --install-missing /tmp/ac-codely-install-20261103-final` | exit0；最终实现真实重新下载、独立临时工作区冷安装、两树复验通过，原项目包未触碰 |
| 上述旧stage audit-release | exit1，真实拒绝TJGenerators.Runtime.dll；不是新投影的结果 |
| 隔离临时Git仓库，使用`git show HEAD:client/Packages/manifest.json`及lock原字节，复制门脚本但不安装工具、不放准入记录，再执行`node tools/check-assets.mjs` | exit0；实际HEAD无两开发工具，34项官方依赖。证明clean质量门不要求引入开发工具；独立`verify`仍保持完整登记组合严格语义，不把缺项当完整准入通过 |

保持限制：准入记录是受审代码配置，不是抵抗有权修改记录的攻击者的签名；不会自动信任新版本。未将开发准入通过冒充发布审计通过或P0完整验收；新投影实物审计及其余性能/CI/联机门由Lead汇总。

## 附录：历史1.0.85证据（不适用于当前）

以下正文取自本轮起点HEAD，原样保留历史失败、官方核查及纠正过程；其旧版本指纹、准入状态、插件数量与“不隔离排除”的阶段性决策不适用于当前1.0.87/1.0.27，以本文前部和ADR-021为准。

# Codely Bridge 本地正式依赖审查

## 决策与范围

用户选择“正式依赖，先审查再决定”。因此保留manifest原有条目，不删除、不隔离排除、不直接加入白名单。此文为本地初步审查，非完整安全或法律认证。

## 本机实际证据

目录：client/.codely.packages/cn.tuanjie.codely.bridge@1.0.85-47f31100。package.json声明1.0.85，作者Yousandi Technologies，描述为Unity Editor TCP socket server；依赖com.unity.ext.nunit1.0.6与com.unity.ugui1.0.0。该目录是本地包，不作为本次新增提交内容。

- Assets下cs/asmdef未检出codely直接引用，不能据此排除反射或工具间接使用。
- 三个asmdef位于Editor目录，包同时提供Codely.Newtonsoft.Json.dll、Codely.Roslyn.dll及平台插件。已抽查Newtonsoft插件meta：Any关闭、Editor启用；不是所有插件平台设置都已审计。
- 本地包文件名未找到LICENSE/NOTICE/COPYING；package.json无license字段。因此使用及再分发授权仍需供应商条款，不能推断免费或可再分发。
- README说明编辑器加载时自动启动TCP服务。源码包含进程启动、动态编译与Assembly.Load能力，属于有执行权限的开发工具，不能等同普通运行时数据依赖。抽查若干监听点使用Loopback，不代表已完成全部监听/身份鉴权审计。
- 当前已生成stage未检出Managed/Codely命名DLL，不能替代全量发布归档依赖审计或证明没有其他重命名插件。

## 守门结论

继续保留本机包，但素材/依赖门保持失败，不放宽未知包规则。接纳前需要：供应商授权与第三方NOTICE、固定可获取包来源与内容哈希、全部插件平台约束、编辑器监听/鉴权/动态执行风险、CI无本地绝对路径可复现、发布包禁止编辑器工具进入的自动检查，以及相应ADR/白名单规则测试。

本轮不安装或启动额外服务，不修改包源码，不将“未找到许可证”解释为无版权。


## 官方来源补充（本次接续）

已实际GET核验 https://www.codely.tuanjie.cn/terms 与 https://codely-docs.tuanjie.cn/using-codely/codely-bridge-installation-guide/ ，均HTTP200。条款页为动态页面，正文来自官方/static/dist/assets/index-BufgFhew.js，标题“Tuanjie AI 内测服务条款”，生效2025-11-15；原网页、脚本及提取文本位于本地build/reports/enhancement-resume/codely-*。安装指南明确Cowork自动安装Bridge，或Window→Package Manager→Tuanjie Registry安装。

纠正此前结论：包内未找到LICENSE不能推导用户无权使用，也不能推导必须另购“Codely授权”；已有官方服务条款应先核对，不再要求用户为本地修复额外申请。官方条款§9.1限制创建同功能软件或竞争服务；§9.3包含输入/上传/生成内容的宽泛授权，需作为源码隐私风险披露。上述证据并不自动证明允许把私有本地包重新分发或把所有原生插件带入游戏。保持原依赖引用，未将“继续完成”解读为允许任意白名单放行。后续审查应聚焦版本固定、编辑器边界、实际发布内容和数据暴露，不将许可猜测作为技术工作停工理由。

精确本地内容指纹：按相对路径排序，对每项依次SHA256.update(path+NUL)、update(fileBytes)、update(NUL)；428个文件（含meta），SHA256 `faa556946fb6709b3d3697f018b14b2978e2ac699387b422528ad1c376103c0d`，15个二进制，未检出LICENSE/NOTICE路径。三个asmdef均includePlatforms=[Editor]；对已构建stage/20261027T083855959Z-71475175按文件名检索Codely/Roslyn/CodeAnalysis/Newtonsoft/Tauri无命中，仅为发布排除初步证据，不能替代全部插件平台审计。

原生插件meta复核补充：15个二进制均Any关闭且Editor开启；其中Windows NativeWindowBridge/datachannel/libcrypto/libssl及macOS对应4项还含Standalone enabled=1。不能据Editor asmdef断言所有原生插件天然Editor-only。当前Windows stage未发现这些原生文件（按全部15个二进制名检查），但需通过PluginImporter实际查询/构建规则锁定平台，而不是只依赖一次未入包。未修改供应商本地包meta，保持上述428文件指纹有效。
