# 01 认识编辑器与项目结构

## 本篇结果

完成后你将：

- 能在 UE 编辑器中定位常用面板。
- 理解 Level、World、Actor、Component 和 Asset 的基本关系。
- 建立课程统一资产目录。
- 创建并保存 `M00_Sandbox`。

## 开始前的工程状态

- 已完成第 00 篇。
- `RenderingPractice.uproject` 能正常打开。
- 编辑器已完成主要 Shader 编译。

## 1. 认识主界面

打开工程后，先确认以下区域：

| UE 面板 | 作用 | Unity 近似对应 |
|---|---|---|
| Level Viewport | 观察和编辑当前 Level | Scene View |
| World Outliner | 当前 World 中的 Actor | Hierarchy |
| Details | 当前选中对象或组件的属性 | Inspector |
| Content Drawer | 工程 Asset | Project |
| Main Toolbar | 保存、Add、模式、Play、设置 | 主工具栏 |

如果面板不可见：

- **Window > World Outliner**
- **Window > Details**
- 底部点击 **Content Drawer**，快捷键通常为 `Ctrl+Space`

拖动标签页可以重新停靠。布局混乱时使用 **Window > Load Layout > Default Editor Layout** 恢复默认布局；具体菜单层级可能随 5.7 小版本略有差异。

### 为什么先固定面板认知

后续教程会频繁在“场景实例”和“磁盘资产”之间切换：

- Outliner/Details 处理当前 World 中的 Actor 和 Component。
- Content Drawer 处理可被多个 Level 复用的 Asset。
- Viewport 显示当前 View 下的场景结果。

很多 UE 新手问题并不是功能不会，而是把 Asset、Actor 和 Component 的操作位置混在一起。例如在 Outliner 删除 Actor 不会删除 Static Mesh Asset；在 Content Drawer 移动 Material 则会改变资产路径和引用。

## 2. 理解 Level、World、Actor、Component 与 Asset

先建立操作中会反复使用的关系：

```text
Project
├── Content 中保存 Asset
└── 当前运行时创建 World
    └── World 加载一个或多个 Level
        └── Level 中放置 Actor
            └── Actor 通过 Component 获得 Mesh、Light、Camera 等能力
```

- **Asset** 是 Content Browser 中保存到磁盘的资源，例如 Static Mesh、Material、Texture、Blueprint 和 Level。
- **Actor** 是 World 中的实例，出现在 World Outliner。
- **Component** 属于 Actor，决定它具有什么能力。
- **Level** 本身也是一种 Asset，扩展名在磁盘上表现为 `.umap`。

Unity 的 Prefab/Scene/GameObject/Component 思路可帮助入门，但 UE 的 Level、Actor 和 Asset 生命周期并非一一对应。

这里的层次图描述的是本课程当前的简单关卡，不是说一个 World 永远只能有一个 Level。World 是当前运行和渲染上下文，可以同时管理 Persistent Level、流送进来的 Level，或由 World Partition 管理的空间内容。现在使用单一 Level，是为了先把“磁盘上的地图资产”和“打开后存在于 World 中的 Actor”区分清楚；后续进入流送与 World Partition 时，这一层会扩展。

### 为什么需要这些层次

- Asset 负责可保存、可引用和可复用的数据。
- Actor 负责当前 World 中的身份、Transform、网络和生命周期。
- Component 把 Mesh、Light、Camera、Collision 等能力组合到 Actor。
- Level 负责保存一组 Actor；World 负责当前运行环境和已加载 Level。

如果把所有内容都做成独立 Actor，组织和提交成本可能上升；如果把整个环境合成单一 Asset，又会失去流送、遮挡、实例化和局部编辑能力。后续模块化环境和大世界章节会重新讨论粒度，而本篇先建立这些对象的操作边界。

## 3. 创建课程资产目录

### 为什么采用这套目录

目录按“资产职责”和“案例阶段”组织，而不是按教程章节编号组织：

- `Core` 保存跨地图复用的基础系统和工具资产。
- `Maps` 分离主案例与隔离实验。
- `Environment` 按环境资产类型组织，便于后续迁移到 Building、District 和 World。
- `Tools` 保存只用于分析和调试的内容，避免与正式环境资产混合。

替代方案包括按功能包、按关卡或按美术团队组织。大型生产项目常会使用更严格的 Feature/Plugin 边界；当前结构优先服务单人连续学习，又保留后续扩展空间。

1. 打开 Content Drawer。
2. 在左侧选择项目的 `Content` 根目录。
3. 空白处右键选择 **New Folder**，创建 `RenderingPractice`。
4. 进入该目录，依次创建：

```text
Core
Maps
Environment
Tools
```

5. 在 `Core` 下创建：

```text
Materials
Meshes
Blueprints
RenderTargets
```

6. 在 `Maps` 下创建 `Main` 和 `Labs`。
7. 在 `Environment` 下创建：

```text
Architecture
Props
Foliage
Landscape
Water
```

如果创建到了错误位置，可以在 Content Drawer 中移动 Asset，但应避免在资源大量被引用后频繁改目录。

UE 移动 Asset 时通常会留下 Redirector，帮助旧引用找到新位置。频繁重构会增加 Redirector 和版本管理噪声，因此应尽早建立稳定目录；需要清理时可在目标目录右键使用 **Fix Up Redirectors**，但不要在多人协作或未提交改动时盲目批量执行。

Content Drawer 中的目录不是只服务界面显示的标签。`/Game/RenderingPractice/...` 形式的 Asset Package 路径会参与资源引用、Cook 和版本管理。移动 Asset 后，引用方仍可能暂时指向旧 Package 路径，Redirector 用一个轻量中转对象把旧引用导向新位置；**Fix Up Redirectors** 会尝试把引用方改写到新路径，再删除不再需要的中转对象。因此它可能修改多个引用资产，执行前必须先保存、检查 Source Control 状态，并确认没有其他人仍在使用旧路径。

### 目录创建的成功检查

创建完成后，不要只看当前展开的文件夹。折叠并重新展开 `Content/RenderingPractice`，确认目录树与 `PROJECT_STATE.md` 一致，并确认没有误建成 `Content/Core` 或 `Content/RenderingPractice/RenderingPractice/Core`。这里验证的是 Package 路径；目录名字看起来相同但父路径不同，后续教程引用会失败。

## 4. 新建 M00_Sandbox

### 为什么需要独立 Sandbox

`M00_Sandbox` 用于练习删除、移动、材质替换和临时工具，不承载正式主案例。这样错误操作不会污染 `M01_RenderingRoom`，也不需要每次为了试一个功能复制整个正式地图。

另一种方式是在主地图中建立 Debug Folder 或 Data Layer。等课程进入 Data Layers 和大世界后会使用这些方式；当前单独 Sandbox 更直观，也更容易恢复。

1. 选择 **File > New Level**。
2. 选择 **Empty Level**。
3. 选择 **File > Save Current Level As**。
4. 保存到：

   `Content/RenderingPractice/Maps/Main`

5. 命名为：

   `M00_Sandbox`

6. 点击 **Save**。

在 Content Drawer 中确认出现 `M00_Sandbox` Level Asset。

`M00_Sandbox` 只用于熟悉编辑器和临时操作；正式房间会在下一篇创建为独立地图。

选择 Empty Level 是为了让你看见每个 Actor 是如何加入场景的。Basic/Open World 模板会预置灯光、天空、雾、World Partition 或地面，适合快速开始项目，但会隐藏本课程后面要逐项建立的环境系统。

**New Level** 只是在编辑器内创建一个尚未命名、尚未保存的 Level；直到执行 **Save Current Level As**，它才以 `M00_Sandbox.umap` 对应的 Package 进入项目 Content。保存路径决定 Asset 身份，而不是当前 Viewport 中看到了什么。若在未保存时关闭编辑器，自动恢复记录不等同于正式 Level Asset，也不能作为下一篇稳定依赖。

## 5. 放置 Actor 并观察 Component

1. 点击 Main Toolbar 的 **Add**。
2. 选择 **Shapes > Cube**。
3. World Outliner 中应出现一个 Actor。
4. 选中 Cube，观察 Details 顶部的 Actor 属性。
5. 在 Details 的组件区域选择 `StaticMeshComponent` 或名称相近的 Mesh 组件。
6. 观察：
   - **Transform**
   - **Static Mesh**
   - **Materials**
   - **Mobility**
   - **Rendering**

基础 Cube 是一个 Actor 实例；真正持有 Mesh 和材质槽的是它的 StaticMeshComponent。

更具体地说，**Add > Shapes > Cube** 创建的是一个放在当前 Level 中的 StaticMeshActor，并让它的 StaticMeshComponent 引用 Engine Content 中已有的基础 Cube Static Mesh。此时发生了两件事：Level 新增了一个 Actor 实例；项目 `Content/RenderingPractice` 没有新增 Static Mesh Asset。理解这条引用关系后，你才能判断删除、复制或移动操作究竟是在改变场景实例，还是在改变所有实例共享的源资产。

### 为什么使用基础 Shape

基础 Shape 是引擎自带的 Static Mesh，适合验证 Actor、Component、Transform 和场景组织，不需要先学习外部 DCC 导入。它不是最终建筑资产方案：后续会比较 Modeling Tools 生成资产、外部导入模块、Nanite 和实例化。

当前 Cube 的作用是减少资产来源变量，让注意力集中在 UE 对象关系上。

### Mobility 是什么，为什么现在先观察它

`Mobility` 属于 Scene Component 的运行时可变性合同，不是简单的性能质量档位。它告诉引擎这个组件的 Transform 和相关渲染状态在游戏运行期间是否允许变化，并影响静态光照、阴影、场景更新和缓存可以采用哪些路径。

- **Static**：对象在游戏中不应移动或改变。它允许依赖静态几何的预计算，并让引擎采用最稳定的场景状态；运行时强行修改会违背这个合同。
- **Stationary**：这个语义主要用于灯光。灯光位置保持不变，但颜色、强度等部分属性可以变化，并可组合预计算与动态结果。不要把它理解成所有 Component 的“中等性能”通用档位。
- **Movable**：对象允许在运行时移动和改变，需要动态更新场景表示以及相关光照、阴影或缓存状态，灵活性最高，但不能依赖只适用于静态对象的处理。

本篇不要求修改 Cube 的 Mobility，只要求你找到当前值并知道它是一份承诺。第 02 篇的房间壳体和第 04 篇的建筑模块没有运行时移动需求，应保持 Static；第 03 篇临时灯为了立即调节而使用 Movable。后续比较光照和阴影成本时，必须同时记录 Mobility，否则看似相同的 Actor 可能进入不同路径。

## 6. 学习 Viewport 基础操作

常用操作通常为：

- 按住鼠标右键 + WASD：飞行浏览
- 鼠标右键拖动：旋转视角
- 鼠标滚轮：调整移动速度或缩放
- 选中 Actor 后按 `F`：聚焦
- `W`：移动工具
- `E`：旋转工具
- `R`：缩放工具
- `Q`：选择模式
- `Delete`：删除 Actor
- `Ctrl+Z`：撤销
- `Ctrl+S`：保存

若快捷键与系统或键盘布局冲突，以工具栏按钮和 Editor Preferences 中的快捷键为准。

### 为什么需要同时掌握快捷键和数值输入

- Viewport Gizmo 适合快速构图和直观调整。
- Details 数值适合模块化、对齐和可复现尺寸。
- 正交视图适合检查建筑边界。

环境搭建如果只依靠自由拖动，早期看不出问题，后续在门窗拼接、Lumen 漏光、HLOD 和 World Partition 单元边界上会放大误差。

## 7. Transform、局部坐标和吸附

选中 Cube：

1. 在 Details 的 **Transform** 中输入 Location `0, 0, 50`。
2. 使用 Viewport Gizmo 移动 Cube。
3. 切换工具栏中的 World/Local 坐标模式，观察 Gizmo 轴变化。
4. 打开移动吸附，先使用 `10` cm。
5. 打开旋转吸附，先使用 `10` 度。
6. 打开缩放吸附，先使用 `0.25`。

UE 默认使用厘米：

```text
1 Unreal Unit = 1 cm
100 units = 1 m
```

环境模块尺寸、门高、墙厚和网格吸附都应围绕这一单位建立。

### World 与 Local 坐标切换改变了什么

Actor 的 Transform 最终仍表示它在 World 中的位置、旋转和缩放；World/Local 按钮改变的是编辑 Gizmo 使用的轴基底，不会在切换按钮时自动改写 Actor Transform。

- **World**：Gizmo 轴与关卡全局 X/Y/Z 对齐。把对象沿 World Z 移动，含义始终是向世界上方移动，与对象当前朝向无关。
- **Local**：Gizmo 轴跟随对象自身旋转。墙体旋转 90° 后，沿 Local X 移动仍是沿墙体自己的 X 方向，而不一定是 World X。

先把 Cube Rotation Z 改为 `45°`，再切换 World/Local，应该看到 Gizmo 轴方向改变，但 Details 中的 Location、Rotation 和 Scale 不因为切换而改变。这个对比验证了“编辑参考轴变化”和“对象状态变化”是两件事。模块化搭建中，放置位置常用 World 网格保证全局对齐，沿已经旋转的模块自身方向微调时才使用 Local 轴。

### 吸附实际约束什么

Viewport 吸附是编辑器对交互增量和放置结果的量化辅助，不是运行时物理约束，也不会自动修复已经存在的任意小数 Transform。打开 10 cm 移动吸附后，用 Gizmo 拖动会按该步长变化；在 Details 中直接输入 `13` cm、由 Blueprint 在运行时移动，或导入一个 Pivot 偏移的 Mesh，都不会因为这个按钮自动回到 10 cm 网格。

因此成功检查不能只是“按钮亮了”。拖动 Cube 后查看 Details，确认 Location 落在预期网格；再手动输入一个非网格值，确认你理解数值输入仍可以绕过交互吸附。后续出现接缝时，应检查实际 Transform 和资产 Pivot，而不是只检查吸附按钮状态。

### 为什么选择 10 cm / 10° / 0.25

- 10 cm 移动吸附足够细，又能让 20 cm 墙厚、100 cm 门宽等尺寸稳定对齐。
- 10° 旋转适合灰盒阶段；模块化建筑常主要使用 90°，后续可临时提高吸附角度。
- 0.25 缩放吸附只用于基础 Shape 灰盒。正式 Static Mesh 更推荐使用规范尺寸资产，避免非统一缩放影响碰撞、光照、纹理密度和实例复用。

这些是当前课程默认，不是生产项目固定标准。建立模块套件后，吸附值应由模块尺寸和项目单位规范决定。

`0.25` 表示 Scale 数值的交互增量，而不是“把对象缩放到 25%”。从 `1.0` 拖动一次可能到 `1.25` 或 `0.75`。Scale 是无单位倍率；Location 使用厘米；Rotation 使用角度。把三个吸附值写在同一排只是界面方便，不代表它们共享同一种物理量。

## 8. Actor 命名与 Outliner Folder

1. 在 World Outliner 中选中 Cube，按 `F2`，重命名为 `SM_TestCube_Actor`。
2. 在 Outliner 空白处右键，新建 Folder，命名为 `Sandbox`。
3. 把 Cube 拖入该 Folder。

Outliner Folder 只组织当前关卡中的 Actor，不会改变 Content 中 Asset 的路径。

### 命名需要区分 Actor 与 Asset

这里的 `SM_TestCube_Actor` 只是为了显式提醒它是使用 Static Mesh 的场景实例。正式项目中 Actor 名称可以按语义命名，如 `Room_Wall_Left`；Asset 才使用 `SM_`、`M_`、`MI_` 等类型前缀。

Outliner Folder 主要服务编辑器组织，不等同于运行时层级、流送单元或 Data Layer。后续 World Partition 章节会分别处理这些概念。

## 9. 保存与关闭验证

1. `Ctrl+S` 保存当前 Level。
2. 关闭编辑器。
3. 重新双击 `RenderingPractice.uproject`。
4. 在 Content Drawer 双击 `M00_Sandbox`。
5. 确认 Cube、命名和 Outliner Folder 保留。

如果打开工程后不是该地图，这是正常的；默认启动地图会在后续确定主场景后配置。

### 保存时究竟保存了什么

在 Level Editor 中，`Ctrl+S` 会保存当前正在编辑的 Level Package，也就是 Actor、Transform、Folder 和引用关系。它不会把 Engine 自带 Cube 复制进项目，也不会自动保存所有其他处于 Dirty 状态的 Asset。**File > Save All** 才会遍历当前需要保存的项目 Package。

Asset 或 Level 标签上的星号表示内存中的对象已经修改，但对应 Package 尚未写入磁盘。星号消失证明本次修改已保存；关闭并重新打开工程后对象仍存在，进一步证明保存的 Package 能被重新加载。自动保存和 `Saved/Autosaves` 只用于恢复，不是正式项目状态。

### 为什么现在不设置默认地图

`M00_Sandbox` 是临时练习地图，不应成为最终 Editor Startup Map 或 Game Default Map。下一篇创建 `M01_RenderingRoom` 后，再根据主案例工作流决定默认打开哪张地图。

默认地图属于项目配置，会影响编辑器启动和打包运行；它与“当前最后打开的地图”不是同一概念。

## 常见问题

### Actor 放进场景但 Content 中没有新增资源

你创建的是 Level 中的 Actor 实例，使用了引擎已有基础 Shape Asset，不会自动生成项目 Static Mesh。

### 移动的是 Asset 还是 Actor

Viewport 和 Outliner 修改 Actor；Content Drawer 管理 Asset。两者名称相似时先确认当前焦点。

### 保存后出现星号

标签页或 Asset 名后的星号表示有未保存修改，使用 `Ctrl+S` 或 **File > Save All**。

### 删除 Actor 是否删除 Mesh Asset

不会。删除 Level 中的 Actor 不等于删除 Content 中被引用的 Asset。

## 对渲染和工程的意义

- Actor/Component 是后续进入渲染场景的游戏世界表达。
- Mobility 会影响对象能否移动、光照和阴影如何处理以及某些缓存是否可复用。
- 统一目录与命名能避免后续材质、Nanite、Lumen、HLOD 和 World Partition 资产失控。

## 可选延伸

- `RenderingDeep/01_Architecture.md`：Component 为什么需要转换成稳定渲染数据。
- `RenderingDeep/02_SceneProxy.md`：Component、Proxy、SceneInfo 和 FScene 的边界。

## 完成状态

- [ ] 认识 Viewport、Outliner、Details、Content Drawer 和 Toolbar
- [ ] 创建完整课程目录
- [ ] 创建 `M00_Sandbox`
- [ ] 能区分 Asset、Actor 和 Component
- [ ] 能解释 World/Local Gizmo、Transform 数值与编辑器吸附的边界
- [ ] 能区分 Static、Stationary 和 Movable 的职责，不把 Mobility 当作普通质量档位
- [ ] 地图能保存并重新打开

下一篇将使用基础几何和 Modeling Tools 搭建正式的 `M01_RenderingRoom`。
