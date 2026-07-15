# 02 使用基础几何搭建 Rendering Room

## 本篇结果

从空 Level 搭建一个可继续用于材质、灯光、Nanite、Lumen 和性能分析的房间灰盒：

- 6 m × 8 m 房间
- 3 m 层高
- 一扇门
- 一个窗洞
- 清晰的室内外方向
- 统一网格和 Actor 命名

最终地图：`M01_RenderingRoom`

本篇重点是 UE 环境灰盒操作，不追求最终美术质量。

### 为什么从房间开始

一个带门窗的房间足够小，便于控制变量；同时又天然包含室内外可见性、墙体厚度、材质分区、直接光、阴影、GI、反射、玻璃和曝光问题。后续 Lumen 漏光、VSM 阴影、透明和后处理都能在这张地图上得到明确现象。

直接从开放世界开始会同时引入 Landscape、流送、植被、HLOD 和大量资产，使你难以区分问题来自渲染系统还是场景组织。

## 开始前的工程状态

- 已完成 00、01。
- 课程目录已存在。
- 能使用 Viewport、Outliner、Details 和 Transform。

## 1. 启用 Modeling Tools Editor Mode

1. 选择 **Edit > Plugins**。
2. 搜索 `Modeling Tools Editor Mode`。
3. 确认插件已启用。
4. 如果刚启用，按提示重启编辑器。

重启后，编辑器左上角模式下拉菜单中应能选择 **Modeling**。

启用插件修改的是项目的功能模块集合，而不是只把一个按钮临时显示出来。`Modeling Tools Editor Mode` 是随编辑器加载的建模工具插件；项目启用状态会记录在 `.uproject` 的 Plugin 配置中，团队成员和后续打开该项目的编辑器据此加载相同模块。它主要提供 Editor Mode、交互工具和资产生成流程，不会因为启用就把建模代码加入最终游戏的每帧渲染路径。

需要重启的原因是插件 Module 要在编辑器启动和模块加载阶段注册 Mode、命令与工具。勾选后按钮没有立即出现，不代表设置失败；先按提示重启，再用模式下拉菜单验证。成功证据是 Modeling Mode 能进入并显示工具集合，而不只是 Plugins 面板中的复选框保持勾选。

本篇先用基础 Shapes 完成灰盒，再用 Modeling Mode 处理窗洞。这样能同时熟悉 Actor 级搭建和建模工具。

### 为什么选择 Modeling Tools

Modeling Tools 让你在 UE 内完成简单 Boolean、法线和碰撞处理，适合快速环境原型，也能生成真正的 Static Mesh Asset。

替代方案包括 Blender、Maya、Houdini 或 DCC 模块化套件。外部 DCC 更适合复杂拓扑、UV 和正式资产；UE Modeling Tools 更适合现场修改和验证。课程后续会引入外部 Static Mesh 导入，不把引擎内建模当作唯一生产方式。

## 2. 创建正式地图

1. 选择 **File > New Level > Empty Level**。
2. 选择 **File > Save Current Level As**。
3. 保存到：

   `Content/RenderingPractice/Maps/Main`

4. 命名：`M01_RenderingRoom`。
5. 在 World Outliner 创建以下 Folder：

```text
00_Geometry
01_Lights
02_Cameras
03_Volumes
90_Debug
```

本篇只使用 `00_Geometry`，其他 Folder 为后续章节预留。

`M01_RenderingRoom` 是新的 Level Asset，不是 `M00_Sandbox` 的重命名。执行 **New Level** 后，当前 World 切换到新的未保存 Level；执行 **Save Current Level As** 才在 `Maps/Main` 下创建新的 `.umap` Package。保存后检查 Content Drawer 中 `M00_Sandbox` 和 `M01_RenderingRoom` 同时存在，证明练习地图没有被主案例覆盖。

### 为什么创建新地图而不是继续 Sandbox

`M01_RenderingRoom` 是后续十多个章节都会复用的主案例，需要稳定、可比较。Sandbox 中允许随意删除和试验，不适合作为材质、灯光和性能基线。

Folder 使用数字前缀，是为了在 Outliner 中保持固定排序。它只影响编辑器组织，不会自动决定渲染顺序或运行时层级。

## 3. 固定灰盒尺寸和吸附

在 Viewport 工具栏设置：

- 移动吸附：10 cm
- 旋转吸附：10°
- 缩放吸附：0.25

房间目标尺寸：

```text
宽 X：600 cm
深 Y：800 cm
高 Z：300 cm
墙厚：20 cm
门宽：100 cm
门高：210 cm
窗台高：90 cm
窗洞高：120 cm
```

这些数值不是建筑规范标准，而是便于本课程网格化操作和后续光照观察的统一基线。

尺寸表还是后续章节的资产合同。第 04 篇的 2 m 模块必须能重新拼出同一个 6 m × 8 m × 3 m 房间；门窗模块也必须复现本篇开口。若本篇通过自由拖动得到一个“看起来差不多”的尺寸，问题会在模块替换、碰撞、固定相机和光照对比时一起出现，因此这里需要使用 Details 数值和正交视图双重验证。

### 为什么采用这组尺寸

- 6 × 8 m 足够放置多个材质、灯光和重复道具，又不会让室内 GI 过于稀疏。
- 3 m 层高接近常见室内尺度，能直观判断相机高度、门窗比例和灯光衰减。
- 20 cm 墙厚能形成真实内外表面，避免单面 Plane 在阴影、碰撞和 Lumen 中产生特殊问题。
- 100 × 210 cm 门洞和 90 cm 窗台提供明确的人体尺度参照。

如果做工业厂房、古建筑或风格化场景，尺寸应根据案例重设。这里追求的是后续章节结果一致，而不是宣称所有建筑都应使用这些数值。

## 4. 创建地面

1. **Add > Shapes > Cube**。
2. 重命名为 `Room_Floor`。
3. 放入 `00_Geometry` Folder。
4. 在 Details 设置：

```text
Location: 0, 0, -10
Scale:    6, 8, 0.2
```

基础 Cube 默认约为 100 cm，因此该 Scale 得到约 600 × 800 × 20 cm 的地面。

这里的 `Scale` 是对 Engine 基础 Cube Asset 的实例倍率：`6 × 100 cm = 600 cm`、`8 × 100 cm = 800 cm`、`0.2 × 100 cm = 20 cm`。Location Z 为 `-10`，因为 Cube 的 Pivot 在中心；20 cm 厚的地面从 Z `-20` 延伸到 `0`，于是房间可用地面上表面落在 Z `0`。这不是任意偏移，而是在用 Pivot 与半尺寸共同确定边界。

使用有厚度的 Cube，而不是单面 Plane，便于后续观察阴影、Lumen 表面和室内外漏光。

### Cube 与 Plane 的取舍

Plane 顶点和像素成本更低，适合纯地表或远景；但它没有真实侧面和体积，碰撞、背面可见性和 GI 表达更依赖材质与系统设置。当前地面使用薄 Cube，是为了让房间成为封闭体，并为后续室内外光照建立更可靠的几何边界。

代价是多了一些面和一个独立 Actor。对本房间可以忽略，进入大规模模块化环境后必须重新评估合并、实例化和 Nanite。

## 5. 创建后墙和侧墙

### 为什么先使用独立墙体 Actor

独立墙体便于移动、隐藏、替换材质和观察单个对象的可见性。它适合教学和灰盒，但不代表最终最优提交粒度。

后续会比较：

- 多个独立 Actor：编辑灵活，但对象和提交管理更多。
- 模块化 Static Mesh：适合复用、实例化和关卡设计。
- 合并 Mesh：减少对象数量，但降低局部编辑和流送粒度。
- Nanite 资产：改变传统三角形和 LOD 模型，但仍受实例、材质和像素成本影响。

创建三个 Cube：

### 后墙

```text
Name:     Room_Wall_Back
Location: 0, 390, 150
Scale:    6, 0.2, 3
```

### 左墙

```text
Name:     Room_Wall_Left
Location: -290, 0, 150
Scale:    0.2, 8, 3
```

### 右墙

```text
Name:     Room_Wall_Right
Location: 290, 0, 150
Scale:    0.2, 8, 3
```

把它们放入 `00_Geometry`。

这里故意让墙体中心稍微向房间内部偏 10 cm，使外边界接近目标尺寸。灰盒阶段最重要的是规则一致，不要求毫米级建筑精度。

## 6. 创建天花板

1. 复制 `Room_Floor`，快捷键通常为 `Ctrl+W`。
2. 重命名为 `Room_Ceiling`。
3. 设置 Location Z 为 `310`。

天花板底面约位于 300 cm。后续需要测试室内光照时，可以通过 Outliner 眼睛图标临时隐藏天花板观察场景。

天花板不能因为编辑时遮挡视线就永久省略。缺少顶面会改变阴影、SkyLight/Lumen 能量进入室内的方式，也会让“漏光”实验失去真实边界。编辑便利应通过临时隐藏解决，而不是改变正式几何。

## 7. 使用分段墙体创建门洞

### 为什么门洞不用 Boolean

门洞边界规则、位于墙体中央，使用三个矩形墙段更清楚：

- 每个模块尺寸可直接读取和调整。
- 没有 Boolean 产生的拓扑和碰撞问题。
- 后续可以单独替换门框、门顶或两侧模块。

Boolean 更适合快速原型或不规则开洞。下一节故意对窗洞使用 Boolean，是为了学习两种建模路径并比较代价，而不是因为门和窗必须使用不同技术。

前墙不要先做成完整墙再布尔，直接使用三个 Cube 组成门洞，保持简单可编辑。

门洞位于前墙中央，前墙 Y 约为 `-390`。

### 门左侧墙段

```text
Name:     Room_Wall_Front_L
Location: -175, -390, 150
Scale:    2.5, 0.2, 3
```

### 门右侧墙段

```text
Name:     Room_Wall_Front_R
Location: 175, -390, 150
Scale:    2.5, 0.2, 3
```

### 门上方墙段

```text
Name:     Room_Wall_Front_Top
Location: 0, -390, 255
Scale:    1, 0.2, 0.9
```

完成后门洞约为 100 cm 宽、210 cm 高。

如果出现缝隙，使用正交视图和网格吸附检查位置，不要依靠自由拖动猜测。

## 8. 使用正交视图检查结构

在 Viewport 左上角的 **Perspective** 菜单中切换：

- Top
- Front
- Left / Right

检查：

- 墙是否对齐地面边缘。
- 墙体是否有 20 cm 厚度。
- 门洞是否位于前墙中央。
- 天花板和墙顶是否对齐。

选中对象后按 `F` 可以快速聚焦。正交视图比透视视图更适合检查模块边界和尺寸。

透视视图适合判断视觉比例，但会产生透视缩短，难以确认平行面和精确边界。环境搭建通常在透视、正交和数值输入之间切换，不应只依赖一种视图。

## 9. 创建窗洞测试墙

窗洞放在右墙。为了练习 Modeling Mode，创建一个独立墙体副本进行布尔：

1. 先隐藏原来的 `Room_Wall_Right`。
2. 复制它，重命名为 `Room_Wall_Right_Window`。
3. **Add > Shapes > Cube** 创建切割体，命名 `Cut_Window`。
4. 设置切割体大约为：

```text
Location: 290, 100, 150
Scale:    0.6, 1.2, 1.2
```

5. 切割 Cube 要完全穿过墙厚，并形成约 120 cm 宽、120 cm 高的窗口区域。

右墙的厚度沿 World X，长度沿 World Y，高度沿 World Z。基础 Cube 约为 100 cm，因此切割体的 X/Y/Z 尺寸约为 60/120/120 cm：

- X 方向 60 cm 明显大于 20 cm 墙厚，保证切割体从墙两侧穿出。若切割体只碰到墙面或刚好共面，Boolean 容易留下未切开的面或数值不稳定边界。
- Y 方向 120 cm 决定窗宽。
- Z 方向 120 cm 决定窗高；中心 Z 为 150 cm，所以开口从 Z 90 cm 延伸到 210 cm，得到 90 cm 窗台。

这组 120 × 120 cm 开口是第 04 篇 `SM_Wall_Window_200x300x20` 的连续合同，不应在模块替换时悄悄改变。

### 为什么复制墙体再做 Boolean

Boolean 会生成新的几何和 Asset。保留原墙体副本可以：

- 在操作失败时快速回退。
- 对比 Boolean 前后的拓扑、碰撞和包围盒。
- 避免直接破坏主案例中唯一的右墙。

确认新墙稳定后再删除或归档旧墙。正式生产中也应为破坏性建模操作保留可恢复路径。

## 10. 使用 Modeling Mode 执行 Boolean

1. 在模式下拉菜单选择 **Modeling**。
2. 先选中 `Room_Wall_Right_Window`，再按住 `Ctrl` 选中 `Cut_Window`。
3. 在 Modeling 工具中搜索并选择 **Boolean**。
4. Operation 选择 **Difference A - B**。
5. Output Type 选择创建新的 Static Mesh Asset，而不是只产生临时动态对象。
6. 输出目录选择：

   `Content/RenderingPractice/Environment/Architecture`

7. 资产命名为：`SM_Room_Wall_Window`。
8. 点击 **Accept**。

成功后：

- Content Drawer 中出现 `SM_Room_Wall_Window`。
- Level 中出现使用该 Mesh 的 Actor。
- 切割体可以删除或隐藏。
- 窗洞边缘应完整，没有封闭面残留。

Modeling Tools 的具体面板位置可能随编辑器布局变化，使用工具搜索框比记固定分类更可靠。

### Difference A - B、预览和 Accept 分别意味着什么

Boolean 接收两个封闭表面集合。`A - B` 保留 A 中位于 B 外部的部分，并在相交边界补出新的切口表面。本篇中：

- A 必须是要保留的右墙 `Room_Wall_Right_Window`。
- B 必须是只负责定义删除体积的 `Cut_Window`。
- 交换选择顺序后，运算含义会变成保留切割体并减去墙，结果当然不再是一面带窗的墙。

进入工具后看到的是交互预览。此时调整 Operation 或切割体相关选项是在重新计算预览；只有点击 **Accept**，工具才提交结果并按 Output 设置创建或更新输出对象。点击 **Cancel** 应丢弃这次工具结果，不应生成正式 `SM_Room_Wall_Window` Asset。因此 Accept 前应从内外两侧观察开口，并检查工具显示的新边界线是否存在意外破洞。

本篇选择新的 Static Mesh Asset，是因为后续 Level 需要引用一个具有稳定 Package 路径的结果。Output 不是“保存按钮”的同义词：它决定结果属于临时 Dynamic Mesh、覆盖/派生对象还是 Content 中的资产；具体可用选项会随输入对象与工具版本变化。无论面板如何排列，最终验收都必须确认 `Environment/Architecture/SM_Room_Wall_Window` 实际存在，并且关卡 Actor 引用的是这个 Asset。

Boolean 重新构造相交区域的三角形，但它不会替你设计生产级拓扑、UV、Lightmap UV 或围绕开口的简单碰撞。A 的旧碰撞即使被保留，也不代表碰撞体已经同步切出窗口。正因如此，下一节必须分别检查渲染表面与碰撞表面，不能只看 Lit View 中出现了洞。

### 为什么输出新的 Static Mesh Asset

Level 中的临时建模对象不适合作为后续章节稳定依赖。输出 `SM_Room_Wall_Window` 后：

- Mesh 可以被多个地图和 Actor 引用。
- 材质槽、碰撞、Nanite 和 LOD 设置有明确资产归属。
- 资产可以进入版本管理和后续模块化流程。

替代的 Dynamic Mesh 更适合临时编辑或程序化工作流；它是否应长期存在取决于工具和运行时需求。本课程此处选择 Static Mesh，是为了建立标准环境资产路径。

## 11. 检查法线、碰撞与材质槽

双击 `SM_Room_Wall_Window` 打开 Static Mesh Editor：

1. 检查窗洞方向和表面是否正常。
2. 使用 Collision 显示检查简单碰撞。
3. 如果仍存在覆盖整面墙的旧 Box Collision，先移除它；单个包围盒会把可见窗洞重新封死在碰撞世界中。
4. 本篇可临时在 Static Mesh 的 Collision 设置中选择 **Use Complex Collision As Simple**，让静态墙体使用当前渲染三角形回答角色和 Trace 的简单碰撞查询；第 04 篇导入模块后必须改为围绕开口的 UCX 简单凸碰撞。
5. 确认至少有一个 Material Slot。
6. 保存 Static Mesh。

复杂环境 Mesh 不应默认使用过度复杂的逐三角形碰撞；后续会专门讨论环境碰撞和性能。当前只要求房间能用于基本行走和选择。

### 为什么 Boolean 后必须检查这些内容

- 错误法线会导致光照方向、剔除和材质显示异常。
- 碰撞决定角色、射线和部分工具如何与几何交互，但渲染几何和碰撞几何不是同一份职责。
- 材质槽决定 Mesh 如何分配材质，也会影响 Section、Draw 和后续 Nanite Material Bin。

简单 Box Collision 成本低，但一个整体 Box 无法表达门窗开口。可以用左、右、上、下四个凸体围绕窗口组成最终简单碰撞；本篇尚未建立稳定模块资产和 DCC 源文件，因此先用 **Complex As Simple** 保持开口语义，第 04 篇资产导入再用 UCX 建立正式方案。

Complex As Simple 复用渲染三角形进行查询，能准确保留 Boolean 开口，但查询数据更复杂，也不能把该 Mesh 当作需要物理模拟的普通动态刚体方案。它是有明确期限的课程过渡，不是“更精确所以永远更好”。成功检查应打开 Collision 可视化，并使用 Pawn 或 Trace 验证墙面能阻挡、窗洞区域不命中整块 Box；只看到绿色碰撞线并不足以证明开口可通行。

## 12. 添加比例参照物

1. 添加一个 Capsule 或 Cube 作为人物比例参照。
2. 建议高度约 180 cm。
3. 命名为 `Debug_HumanScale`。
4. 放入 `90_Debug` Folder。

观察门高、窗台和房间层高是否合理。环境搭建不能只看数值，还要建立视觉尺度感。

人物参照物只属于 Debug 内容，不应混入正式环境资产或最终画面。后续可以替换为标准 Mannequin 或尺寸标尺；当前简单 Capsule/Cube 足以暴露门窗比例错误。

## 13. 清理 Outliner 与保存

最终 `00_Geometry` 应大致包含：

```text
Room_Floor
Room_Ceiling
Room_Wall_Back
Room_Wall_Left
Room_Wall_Right_Window
Room_Wall_Front_L
Room_Wall_Front_R
Room_Wall_Front_Top
```

删除不用的切割体和失败副本；如果保留备用对象，将其移入明确的 Debug Folder 并隐藏。

使用 **File > Save All** 保存 Level 和新 Static Mesh Asset。

### 为什么必须同时保存 Level 与 Asset

Level 保存 Actor 的位置、引用和 Folder；`SM_Room_Wall_Window` 是独立 Asset。只保存其中一个，重启后可能出现地图引用旧对象、Asset 修改丢失或星号仍存在。

`Save All` 适合本篇这种同时修改地图和多个资产的工作；大型项目中保存前还应检查 Source Control 状态，避免无意提交无关资产。

## 操作结果检查

- 从门外可以看到房间内部。
- 房间地面约 6 × 8 m。
- 层高约 3 m。
- 门洞和窗洞真实贯通。
- 天花板可以单独隐藏。
- 正式房间对象均在 `00_Geometry`。
- Boolean 结果保存在项目 Content，而不是临时对象。

## 常见问题

### Boolean 按钮不可用

确认 Modeling Tools 插件已启用，并按 A、B 顺序选择两个 Mesh Actor。

### Difference 方向反了

交换 A/B 选择顺序，或在 Boolean 工具中切换操作方向。

### 窗洞出现黑面或错误法线

确认切割体完全穿过墙体；在 Modeling Mode 中使用法线修复工具，或撤销后重新执行干净布尔。

### Scale 数值与预期尺寸不一致

基础 Shape 约为 100 cm，但 Modeling 工具生成的新 Asset 可能把缩放烘焙进 Mesh。通过测量工具或 Transform 尺寸检查，不要只依赖 Scale 数值。

### 房间漏缝

切到正交视图，使用 10 cm 或更细吸附检查墙体边界。后续 Lumen 会放大几何缝隙和过薄墙体的问题。

## 对后续渲染的意义

- 墙体厚度会影响阴影、Lumen Surface Cache 和室内漏光。
- 模块边界会影响 Draw、遮挡、Nanite Cluster、HLOD 和流送组织。
- 真实窗洞和门洞为后续室内外曝光、GI 和可见性实验提供条件。
- 独立墙体 Actor 便于早期学习；后续会比较合并 Mesh、实例化和模块化方案。

## 可选延伸

- `RenderingDeep/01_Architecture.md`：Actor/Component 如何进入 Renderer。
- `RenderingDeep/08_FrameInit.md`：房间遮挡结构如何影响当前 View 的可见工作集。
- 这些内容不是完成灰盒的前置要求。

## 完成后的工程状态

- [ ] `M01_RenderingRoom` 已创建并保存
- [ ] 房间、门洞和窗洞尺寸基本符合基线
- [ ] 窗洞为约 120 × 120 cm，窗台约 90 cm，与第 04 篇模块合同一致
- [ ] Modeling Tools 插件已启用
- [ ] `SM_Room_Wall_Window` 已保存到 Architecture 目录
- [ ] 窗墙没有封闭开口的整体 Box Collision，临时 Complex As Simple 已验证
- [ ] Outliner Folder 和命名清晰
- [ ] 天花板和 Debug 参照物可独立隐藏
- [ ] 场景没有未保存 Asset

下一篇将为房间建立固定 Camera、Viewport 观察方式、曝光和画面基准。
