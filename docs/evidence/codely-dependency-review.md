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
