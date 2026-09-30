# 圆润卡通羊外观重做

用户确认方向：圆润卡通羊。保留零外部素材、现有实例化绘制和每羊形 512 顶点／512 三角形预算。不改服务端、碰撞规则或前一轮协议修复。

## 原模型的实际问题

- 主体由分散小球组成；旧普通羊上腿最低约0.435m，下腿最高约0.22m，四肢存在断层。
- 羊形Scale已写进mesh顶点，又被SheepVisuals实例缩放重复应用；头／额标还使用另一套偏移。
- UV球在极点堆重复顶点，离线同阈值检查每羊形有72个退化或近退化三角形。
- 羊体使用官方URP Lit材质，本地URP14.2.0-t1的LitForwardPass.Attributes没有COLOR输入，mesh.colors32的眼睛／脸／蹄颜色没有参与主体着色。
- 额标原三角形绕序朝-z，前面观察时可能被背面剔除。

本轮无法通过图像工具查看 tools/failed.png，未据此推断画面；上述结论来自生产代码、本地官方URP源码及离线数值验证。

## 改动

- 完整蛋形躯干、少量叠合羊毛轮廓、深入肩部与头部的脖子，接地四肢和深色蹄。
- 侧伸长耳、白眼与黑瞳／高光、鼻孔与嘴部；主体与面部颜色分开。
- 冲撞羊增加厚肩与卷角；精英羊偏瘦、冰蓝色；羊王更宽厚，卷角和冠状突起。普通羊奶白、冲撞羊驼色、羊王金米色。
- 羊形目标HeightM/RadiusM/Scale等契约不改，局部mesh高度为HeightM/Scale，仅在实例施加一次Scale；个体尺寸变化从±14%缩至±6%。
- 额标以局部锚点贴额头、后倾55度，尺寸随实例抖动／尸体收缩同步，朝向+z。
- 专用Ac/SheepVertexLit：顶点色、SH环境光、主灯漫反射／阴影、雾；保留GPU instancing及ShadowCaster/DepthOnly pass。Resources材质硬引用shader，默认白tint，仅替换羊体，武器／场景／额标材质不变。

## 验证方式与证据

没有启动 Unity、Tuanjie、batchmode 或游戏，没有加载 UnityEngine DLL，没有编译客户端或shader。

`tools/sheep-geometry-check.ps1` 使用已安装的C#编译器，在内存编译真实 SheepMesh.cs、SheepVisuals.cs 原文，并以轻量数学stub提供Vector3/Matrix/Mesh等接口。它不是引擎测试；仅验证几何数组和变换算术。脚本输出源文件SHA-256，防止旧绿结果误当新结果。

- 旧源码快照：`build/sheep-model/baseline/`；旧红报告：`build/sheep-model/meshes-before.json`。每形72个退化／近退化面，非单位法线7/6/6/8，高度未归一。
- 新版预算：Grunt 400顶点/410三角形，Ram 448/478，Elite 400/410，King 466/502。
- 新版坐标有限、索引合法、顶点色完整、三角形非退化、法线单位化、面绕序与法线一致、左右对称、minY=0、局部高度与单次实例缩放均通过。额标位置／倾角／尸体缩放复测见最终JSON。
- 中间版曾因增加嘴部分段超三角形预算，harness实测失败；减少耳部多余环后通过，未提高512上限。
- `SheepSuite` 新增surface_quality、scale_and_emblem_attachment及额标正面法线断言；`MaterialAssetSuite` 新增羊体资源链及未来引擎加载断言。它们已补写但未在Unity运行。
- 差异空白检查与零外部素材／依赖门禁通过。素材门禁只扫描Git已受控文件；新增shader、mat、meta均为本轮原创文本，并单独检查GUID引用链，无外部包。

## 不启动Unity的查看方式

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/sheep-geometry-check.ps1 -GeometryOutFile build/sheep-model/geometry.json
node tools/sheep-preview.mjs
```

手动打开 `build/sheep-model/preview.html` 可查看四羊形的正面、侧面、三分之四视角。页面使用生产几何导出，未自动打开浏览器。它是简化面光照与三角形排序的SVG图，不是游戏截图，不包含额标、动画、URP光照或阴影；各视图独立适配大小。

## 尚未验收

- Unity实际编译、shader编译、Resources打包、GPU实例化、阴影、近／远景观感和动态行走效果。
- 数值stub不能代替Unity的法线／资源生命周期行为；AABB连通图仅诊断，不能证明实体表面无缝。模型是叠合部件，不宣称单一水密拓扑。
- 只有用户允许启动Unity后，才能按实机正／侧／三分之四视角继续调比例和表情。当前不宣称最终美术验收通过。
