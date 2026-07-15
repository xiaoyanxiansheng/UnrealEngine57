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

### 为什么使用基础 Shape

基础 Shape 是引擎自带的 Static Mesh，适合验证 Actor、Component、Transform 和场景组织，不需要先学习外部 DCC 导入。它不是最终建筑资产方案：后续会比较 Modeling Tools 生成资产、外部导入模块、Nanite 和实例化。

当前 Cube 的作用是减少资产来源变量，让注意力集中在 UE 对象关系上。

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

### 为什么选择 10 cm / 10° / 0.25

- 10 cm 移动吸附足够细，又能让 20 cm 墙厚、100 cm 门宽等尺寸稳定对齐。
- 10° 旋转适合灰盒阶段；模块化建筑常主要使用 90°，后续可临时提高吸附角度。
- 0.25 缩放吸附只用于基础 Shape 灰盒。正式 Static Mesh 更推荐使用规范尺寸资产，避免非统一缩放影响碰撞、光照、纹理密度和实例复用。

这些是当前课程默认，不是生产项目固定标准。建立模块套件后，吸附值应由模块尺寸和项目单位规范决定。

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
- [ ] 能使用 Transform、坐标模式和吸附
- [ ] 地图能保存并重新打开

下一篇将使用基础几何和 Modeling Tools 搭建正式的 `M01_RenderingRoom`。
