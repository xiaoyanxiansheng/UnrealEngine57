# 历史原型：从场景对象到一帧 GPU 工作

> 本文已退出正式课程入口。它保留早期“单篇串联整帧”的设计原型；正式课程从 `Practices/Part01_Foundation/00_CreateProject.md` 开始。

## 本篇要建立的模型

这一篇不以“跑一次 ProfileGPU”为目标，而是从一个空关卡开始，让 UE 的一帧模型随着场景逐步生长出来：

```text
Level 中存在 Actor / Component
        ↓
Camera 产生这一帧的 View
        ↓
Game Thread 的场景状态转换为 Render Thread 可稳定使用的数据
        ↓
Render Thread 组织本帧工作，RHI 提交命令，GPU 执行
        ↓
BasePass 发布表面数据，Lighting 消费表面与灯光
        ↓
Translucency 使用另一条合成路径
        ↓
PostProcessing 持续修改 SceneColor 并形成最终输出
        ↓
RDG / RHI 把这些阶段组织成真正的 GPU 工作
```

每一步只引入当前需要的 UE 概念，并立刻通过编辑器对象、画面或工具观察把它固定下来。

## 本篇关联的 RenderingDeep 章节

本篇不是只关联一篇 `RenderingDeep`，而是用一个连续场景把 12 篇文章中的基础框架串起来。它们并非都要求完整阅读：有些是本篇核心理论，有些只负责解释当前节点的一层边界。

| RenderingDeep | 对应知识节点 | 在本篇中的作用 | 建议阅读深度 |
|---|---|---|---|
| `01_Architecture.md` — 01 渲染架构总览 | 节点一、二 | 建立 Component、View 请求、Renderer 工作区和整帧架构位置 | 核心阅读；重点读正文中引用的小节 |
| `02_SceneProxy.md` — 02 从 Component 到 SceneProxy | 节点一、三 | 解释游戏世界对象为什么要转换成渲染侧快照，以及不同变化怎样进入渲染状态 | 核心阅读；先掌握对象边界和状态线 |
| `03_ThreadModel.md` — 03 三线程模型与渲染命令 | 节点四 | 解释 GT、RT、RHI、GPU 为什么是生产者—消费者时间线，而不是串行函数列表 | 核心阅读；先读新词和主时间线 |
| `04_RHI.md` — 04 RHI 抽象层 | 节点八 | 解释 Renderer/RDG 生成的工作怎样进入 D3D12 等图形后端 | 角色阅读；当前先理解抽象与提交边界 |
| `05_RenderGraph.md` — 05 RenderGraph | 节点八 | 解释 Pass、资源和依赖如何先声明，再编译成本帧执行计划 | 角色阅读；当前先理解声明、编译、执行三层 |
| `08_FrameInit.md` — 08 帧初始化与可见性 | 节点二 | 解释 View 进入 Renderer 后为什么还要形成当前帧工作集 | 角色阅读；当前只读状态台阶总览 |
| `10_BasePass.md` — 10 BasePass 与 GBuffer | 节点五 | 建立不透明表面编码合同，解释 BasePass 为什么不等于最终受光颜色 | 核心阅读；重点读输入合同和发布合同 |
| `12_Lighting.md` — 12 Lighting 与延迟直接光照 | 节点五 | 解释 Lighting 如何消费屏幕表面、灯光和阴影输入 | 核心阅读；重点读 Lighting 输入合同 |
| `14_Translucency.md` — 14 Translucency | 节点六 | 解释透明为什么不能直接复用普通不透明 GBuffer 合同 | 核心阅读；先读输入、队列和调试主线 |
| `15_PostProcessing.md` — 15 PostProcessing | 节点七 | 解释 SceneColor 为什么会被持续修改，以及曝光为何影响前序阶段的视觉判断 | 核心阅读；重点读颜色流和最终输出 |
| `23_Debugging.md` — 23 调试工具与方法 | 节点八 | 规定 GPU Event、Pass 名和工具输出能够证明到什么程度 | 核心阅读；重点读 Capture、时间线和 GPU Visualizer |
| `24_Optimization.md` — 24 Optimization | 节点四 | 用 `stat unit` 先区分 Game、Render/RHI 与 GPU 时间线 | 角色阅读；当前只读时间线分类和工具映射 |

### 建议怎么使用这张表

- 不需要在开始本篇前读完 12 篇。
- 每完成一个知识节点，就按该节点末尾的“深入阅读”进入对应小节。
- 标记为“核心阅读”的章节会构成本篇主要理论骨架，后续值得完整精读。
- 标记为“角色阅读”的章节当前只用于放置概念，不必立即追实现细节。

本篇的理论主骨架是：

```text
01 Architecture
→ 02 SceneProxy
→ 03 ThreadModel
→ 10 BasePass + 12 Lighting
→ 14 Translucency
→ 15 PostProcessing
→ 23 Debugging
```

`04 RHI`、`05 RenderGraph`、`08 FrameInit` 和 `24 Optimization` 在本篇先提供局部支撑，后续会有专门实践继续展开。

---

## 知识节点一：Level、Actor 与 Component

### 当前困惑

当你在 UE 关卡中放入一个 Cube，World Outliner 里出现的对象究竟是什么？它和 Unity 的 GameObject、MeshFilter、MeshRenderer 如何对应？

### 最小原理模型

UE 使用三层概念表达最基础的场景对象：

```text
Level / World
保存当前游戏世界中的对象
        ↓
Actor
关卡中的身份、生命周期和组织单位
        ↓
Component
为 Actor 提供 Transform、Mesh、Light、Camera 等具体能力
```

把一个基础 Cube 拖入关卡时，World Outliner 中看到的是一个 `StaticMeshActor`。真正保存 Static Mesh、材质和可渲染属性的是 Actor 内部的 `StaticMeshComponent`。

Unity 中常把 GameObject 当作容器，再由 Transform、MeshFilter 和 MeshRenderer 等 Component 组合能力。这个总体理解可以复用，但 UE 的 `StaticMeshComponent` 同时承担了更多“几何资源 + 渲染属性 + 场景组件”的职责，不需要机械寻找独立的 MeshFilter 对象。

当前只需要建立一个边界：

> Actor / Component 是游戏世界中的表达，还不是 GPU 直接消费的数据。

渲染侧为什么需要另一份表示，会在第三个知识节点展开。

### UE 操作：创建并观察第一个对象

1. 在编辑器顶部选择 **File > New Level**。
2. 选择 **Empty Level**。
3. 使用 **File > Save Current Level As**，保存为 `L_FrameAndTools`。
4. 点击主工具栏左侧的 **Add**（加号），选择 **Shapes > Cube**。
5. 如果看不到 World Outliner，使用 **Window > World Outliner** 打开。
6. 在 World Outliner 中选中 Cube。
7. 如果看不到 Details，使用 **Window > Details** 打开。
8. 在 Details 顶部查看 Actor 名称；在组件区域选择 `StaticMeshComponent` 或名称相近的 Mesh 组件。
9. 观察 Details 中的：
   - **Transform**：Location、Rotation、Scale。
   - **Static Mesh**：当前使用的几何资源。
   - **Materials**：当前材质槽。
   - **Mobility**：Static、Stationary 或 Movable。
10. 修改 Location 或 Scale，观察 Viewport 中对象变化。

Content Drawer 位于编辑器底部，快捷键通常为 `Ctrl+Space`，相当于 Unity Project；World Outliner 相当于 Hierarchy；Details 相当于 Inspector。

### 你刚刚观察到了什么

- World Outliner 管理的是 Actor。
- Details 中的组件持有具体能力和资源。
- 修改 Transform 是在修改游戏世界中的 SceneComponent 状态。
- 画面随之改变，但这还不能说明 Render Thread 是如何获得新状态的。

这正好产生下一个问题：关卡中已经有 Cube，Renderer 为什么还需要 Camera、View 和其他工作数据？

> **深入阅读：RenderingDeep**
>
> - `01_Architecture.md` — “Component 必须先变成稳定场景数据”
> - `02_SceneProxy.md` — “一个 Unity 心智模型会在这里出错”
> - `02_SceneProxy.md` — “先建立心智模型：四个对象、三本账、一条状态线”
> - 当前理解目标：只确认 Component 属于游戏世界表达，Renderer 不会长期直接读取它。
> - 当前可以跳过：Proxy 创建函数链、SceneInfo 索引、延迟销毁。
> - 源码搜索：`UPrimitiveComponent`、`UStaticMeshComponent`。

---

## 知识节点二：Scene 与 View 为什么必须分开

### 当前困惑

关卡里已经有 Cube，为什么渲染一帧还需要 Camera、ViewFamily、View 和 `FViewInfo`？

### 最小原理模型

“场景里有什么”和“这一帧从哪里看场景”是两件事：

```text
World / FScene
长期描述可渲染世界中有什么

Camera / View 请求
描述从哪里看、投影参数是什么、输出到哪里

FViewInfo
Renderer 为当前 View 建立的本帧工作空间
```

同一个场景可以同时被多个视图观察：编辑器视口、游戏主相机、Scene Capture、立体渲染左右眼、阴影 View。Renderer 因而不能把“场景”和“相机结果”绑成一个对象。

Unity 的 Camera 也会产生自己的 CullingResults 和渲染过程，这个理解可以复用。UE 的额外复杂度在于，Game Thread 先形成 `FSceneViewFamily` / View 请求，Renderer 再把它们转换成 `FViewInfo` 等本帧工作数据。

当前只需要知道 `FViewInfo` 的角色：**它是 Renderer 使用的当前 View 工作区，不是关卡里的 Camera Actor 本身。**

### UE 操作：添加并固定观察视图

1. 先用编辑器自由视口调整到能清楚看到 Cube 的角度。
2. 点击 **Add > Cinematic > Camera Actor**；找不到时在 Add 搜索框输入 `Camera Actor`。
3. 在 World Outliner 选中新建的 CameraActor。
4. 使用 `Ctrl+Alt+NumPad 0` 将相机对齐到当前视口。
5. 如果没有数字小键盘，在 Viewport 左上角的 **Perspective** 下拉菜单中找到 **Placed Cameras**，选择 CameraActor，然后使用 Pilot 调整。
6. 正式观察时始终通过 **Perspective > Placed Cameras > CameraActor** 进入该相机。
7. 切回 Perspective 自由视口，再切回 CameraActor，对比两者观察的是同一关卡，但它们是不同 View。
8. 在 CameraActor 视图下移动 Cube，观察场景对象变化后，该固定 View 的图像也发生变化。

### 你刚刚观察到了什么

CameraActor 是关卡中的 Actor，但编辑器自由视口并不依赖这个 CameraActor。两者可以观察同一个 World，却拥有不同的位置、投影和输出条件。

这说明 Renderer 需要把“场景长期状态”和“本帧 View 请求”分开。后续可见性、BasePass、Lighting 和后处理都以 View 为工作边界，而不是简单对整个 World 无条件执行。

但现在还有一个缺口：Actor 和 Component 属于游戏世界，Renderer 又可能在另一条线程工作，它为什么不能直接读取这些对象？

> **深入阅读：RenderingDeep**
>
> - `01_Architecture.md` — “Game Thread 先生成一帧视图请求”
> - `01_Architecture.md` — “为什么视图请求还要变成 Renderer 工作区”
> - `08_FrameInit.md` — “心智地图：先看状态台阶，不背阶段名”
> - 当前理解目标：区分长期 Scene 与当前 View，知道 `FViewInfo` 是本帧工作区。
> - 当前可以跳过：可见性任务拆分、Occlusion、Dynamic Mesh 收集和 GPUScene Upload。
> - 源码搜索：`FSceneViewFamily`、`FSceneView`、`FViewInfo`。

---

## 知识节点三：Game Thread、Render Thread 与 SceneProxy

### 当前困惑

为什么 Render Thread 不能直接读取刚才在 Details 中看到的 `StaticMeshComponent`？

### 最小原理模型

Game Thread 上的 Actor / Component 会频繁改变，还受 Gameplay、Blueprint、UObject 生命周期和编辑器操作影响。Render Thread 需要一份可以按自己的时间线稳定读取的数据。

UE 因而建立渲染侧表示：

```text
Game Thread
Actor / UPrimitiveComponent
        │ 创建或更新渲染状态
        ↓
FPrimitiveSceneProxy
面向 Render Thread 的渲染快照与查询接口
        ↓
FPrimitiveSceneInfo
把 Proxy 接入 FScene 的关系、索引和生命周期节点
        ↓
FScene
Render Thread 侧长期维护的渲染场景
```

当前不要把 SceneProxy 理解成“另一个 Component”。它的目的不是复刻完整游戏对象，而是摘取 Renderer 真正需要、并能安全跨线程使用的数据。

Unity 的主线程也不会让所有渲染后端代码随意读取任意 MonoBehaviour 状态，但 UE 把这个边界显式表现为 Component → Proxy → Scene 的数据转换和所有权约定。

### UE 操作：比较轻量状态变化和渲染状态变化

先把 Cube 的 **Mobility** 设为 **Movable**，这样运行中修改 Transform 更符合预期。

1. 选中 Cube，在 Details 中将 **Mobility** 设为 **Movable**。
2. 点击编辑器顶部 Play 按钮旁边的下拉箭头，选择 **Simulate**。Simulate 允许游戏世界运行，同时仍可在编辑器中选择 Actor。
3. 在 World Outliner 中选中 Cube，修改 **Transform > Location**。
4. 观察画面中的 Cube 立即移动。
5. 停止 Simulate。
6. 打开 Content Drawer，在 `Content/RenderingPractice` 下创建一个 Material，命名为 `M_TestOpaque`。
7. 双击材质，用一个 Constant3Vector 连接 **Base Color**，点击 **Apply** 并保存。
8. 回到关卡，把 `M_TestOpaque` 从 Content Drawer 拖到 Cube 上，观察外观变化。

这两个操作都会改变最终画面，但概念上并不相同：

- Transform 改变主要更新现有 Primitive 的空间状态。
- Mesh 或材质等变化可能改变 Draw 选择、Shader、材质代理或整个渲染状态，需要更重的更新路径。

本篇不要求你现在证明具体调用链。重要的是：画面更新不是 Render Thread 直接读取刚刚修改的 Component 字段，而是 Game Thread 通过受控更新路径把变化传给渲染侧状态。

### 你刚刚建立了什么模型

```text
Actor / Component 是游戏世界真相
SceneProxy / SceneInfo / FScene 是渲染世界真相
两边通过明确更新路径同步，而不是任意共享可变对象
```

有了两套表示，一帧就不再是单条函数从 Game Thread 跑到 GPU。接下来需要区分各条时间线分别在做什么。

> **深入阅读：RenderingDeep**
>
> - `02_SceneProxy.md` — “组件只是‘提出注册请求’”
> - `02_SceneProxy.md` — “Game Thread 把组件摘成快照”
> - `02_SceneProxy.md` — “移动椅子，不是直接改矩阵”
> - `02_SceneProxy.md` — “换材质 / 换 mesh，旧快照整个作废”
> - 当前理解目标：理解为什么需要渲染快照，以及 Transform 更新和渲染状态重建不是同一类变化。
> - 当前可以跳过：Proxy 删除、延迟清理和各 Proxy 子类虚函数细节。
> - 源码搜索：`CreateSceneProxy`、`FPrimitiveSceneProxy`、`FPrimitiveSceneInfo`、`MarkRenderStateDirty`。

---

## 知识节点四：一帧不是一条时间线

### 当前困惑

Game Thread、Render Thread、RHI Thread 和 GPU 是连续执行的四个函数阶段吗？为什么 `stat unit` 中的数字不能相加？

### 最小原理模型

它们是相互依赖但可以流水、排队和并行的时间线：

```text
Game Thread
更新游戏状态，产生 View 和场景变化
        ↓ 提交渲染工作
Render Thread
吸收场景变化，组织可见性、Pass 和命令
        ↓ 录制 / 提交
RHI / RHI Thread
把跨平台意图转换成后端命令并提交
        ↓
GPU
执行 draw、dispatch、copy 和同步
```

Render Thread 可能在准备 GPU 未来要执行的工作，GPU 也可能仍在执行前面提交的帧。因此 Draw/Render 时间不等于 GPU 时间，也不能把 Game + Draw + GPU 简单相加得到 Frame。

### UE 操作：打开实时统计

1. 确保 Viewport 左上角 **Realtime** 已启用；通常可以用 `Ctrl+R` 切换。
2. 如果控制台快捷键可用，按波浪号/反引号键打开控制台。
3. 如果快捷键没反应，使用 **Window > Output Log**，在底部命令输入框执行命令。
4. 输入：

```text
stat unit
```

5. 在固定 CameraActor 视图下观察 Game、Draw、GPU 和 Frame 数秒。
6. 再移动 Cube 或旋转视图，观察数值可能变化，但不要开始优化。

### 这些数字表达什么

| 项目 | 当前应理解为 | 不能直接推出 |
|---|---|---|
| Game | Game Thread 完成本帧 CPU 工作的时间 | 哪个 GPU Pass 最贵 |
| Draw / Render | Render Thread 侧组织与提交渲染工作的时间 | GPU event 的时间总和 |
| GPU | GPU 完成相关图形工作的时间 | CPU 为什么等待或哪个 GT 函数最慢 |
| Frame | 当前帧节奏受到最长路径和同步关系约束后的结果 | Game、Draw、GPU 的简单加法 |

`stat unit` 的价值是先判断问题属于哪条时间线，而不是解释 Renderer 内部所有阶段。

接下来我们要进入 GPU 时间线内部。但为了让 GPU Visualizer 中的 BasePass 和 Lighting 有明确含义，需要先让场景拥有一个可控的灯光和表面。

> **深入阅读：RenderingDeep**
>
> - `03_ThreadModel.md` — “先用一条生产者—消费者链放稳新词”及其 Task、Queue、Command List、Submit、Fence 小节
> - `03_ThreadModel.md` — 章节收束部分
> - `24_Optimization.md` — “1. 先分清瓶颈属于哪条时间线”
> - 当前理解目标：把 GT、RT、RHI 和 GPU 看成依赖时间线，而不是串行函数列表。
> - 当前可以跳过：TaskGraph 实现、RenderCommandPipe 内部和并行命令录制细节。
> - 源码搜索：`ENQUEUE_RENDER_COMMAND`、`FRHICommandList`、`FFrameEndSync`。

---

## 知识节点五：BasePass 发布表面，Lighting 消费表面

### 当前困惑

GPU 为什么不直接把 Cube 一次画成最终受光颜色？关闭灯光后，为什么 BasePass 通常还存在？

### 最小原理模型

在经典 Deferred 路径中，表面编码和直接光照是两份责任：

```text
可见 Mesh + Material + View
        ↓
BasePass
把表面属性编码到 GBuffer / SceneTextures
        ↓
Lighting
读取屏幕表面数据、灯光和阴影输入
        ↓
SceneColor
```

BasePass 的核心不是“把物体画亮”，而是发布一份后续 Lighting 可以消费的表面合同。Lighting 接手时，很多情况下已经不再逐对象读取原始 Mesh，而是在屏幕空间读取 GBuffer。

Unity Deferred 中 GBuffer Pass 与 Lighting Pass 的分离可以帮助理解。但 UE 的 BasePass 还可能连接 EarlyZ、DBuffer、Velocity、Nanite 和不同材质路径，不能简化为一条固定的 `DrawRenderers` 调用。

### UE 操作：建立可控的不透明受光场景

1. 点击 **Add > Shapes > Plane**，作为地面。
2. 选中 Plane，在 Details 的 **Transform > Scale** 中把 X、Y 设为例如 `10, 10`。
3. 把 Cube 的 **Location Z** 设为约 `50`，避免一半埋进地面。
4. 点击 **Add > Lights > Directional Light**。
5. 在 World Outliner 选中 DirectionalLight。
6. 在 Details 中把 **Mobility** 设为 **Movable**。
7. 修改 **Transform > Rotation**，让地面和 Cube 形成清晰明暗关系。
8. 点击 **Add > Volumes > Post Process Volume**。
9. 选中 PostProcessVolume，勾选 **Infinite Extent (Unbound)**。
10. 在 Details 搜索 `Metering Mode`，展开 **Exposure**，勾选属性左侧 Override，把模式设为 `Manual`。
11. 搜索 `Exposure Compensation`，勾选 Override，先设为 `0`；如果画面过暗可以调整，但后续不再改变。
12. 如果存在 **Apply Physical Camera Exposure**，将其关闭。

固定曝光此时就有了明确作用：关闭灯光后，自动曝光不会重新提亮画面并干扰你判断 Lighting 的真实变化。

### UE 操作：关闭 Lumen 并确认 D3D12

首篇需要观察传统 BasePass / Deferred Lighting 主线，因此先减少 Lumen 和 Nanite 的干扰。

1. 打开 **Edit > Project Settings**。
2. 在 **Engine > Rendering** 中搜索 `Dynamic Global Illumination Method`，设为 `None`。
3. 搜索 `Reflection Method`，设为 `Screen Space` 或项目可用的非 Lumen 选项。
4. 在 **Platforms > Windows** 中搜索 `Default RHI`，设为 `DirectX 12`。
5. 确认 Targeted RHIs 中启用了 DirectX 12 / SM6 对应项。
6. 修改 RHI 后重启编辑器。
7. 点击顶部 **Settings > Engine Scalability Settings**，将各项设为 **Epic**。
8. 等待 Shader 编译完成，在固定 CameraActor 下运行约 10 秒。

这些设置会修改项目配置，建议使用测试项目或记录原值。

### UE 操作：第一次查看 GPU 阶段

在 Output Log 或控制台输入：

```text
ProfileGPU
```

GPU Visualizer 出现后：

1. 展开顶层 GPU event。
2. 先寻找 `PrePass` / Depth、`BasePass`、Lighting 或 Lights、PostProcessing / Tonemap 等父级。
3. 不要求 event 名完全一致；按“它生产或消费什么”判断职责。
4. 保存一张截图即可，不需要整理报告。

然后验证灯光与 BasePass 的责任边界：

1. 关闭 GPU Visualizer。
2. 在 World Outliner 选中 DirectionalLight。
3. 在 Details 搜索 `Visible`，取消勾选。
4. 保持曝光、相机、Cube、材质和其他设置不变。
5. 再次执行 `ProfileGPU`。

### 通常会看到什么，为什么

- 直接光照和动态阴影明显变化。
- 与该灯相关的 Lighting / Shadow 子工作可能减少或消失。
- Lighting 父级可能仍存在，因为场景可能还有其他灯光或固定工作。
- BasePass 通常仍存在，因为表面依旧需要编码。
- PostProcessing 仍存在，因为最终颜色仍需输出。

```text
关闭一个动态灯
≠ 表面不再存在
≠ BasePass 不再需要发布 GBuffer
```

重新勾选 DirectionalLight 的 **Visible**，确认画面和相关 Lighting/Shadow 工作恢复。

这个操作不是为了得出“关灯更快”，而是通过职责变化理解 BasePass 与 Lighting 的数据合同。

> **深入阅读：RenderingDeep**
>
> - `10_BasePass.md` — “核心问题：BasePass 不是‘开始画物体’”
> - `10_BasePass.md` — “第一段：输入合约”
> - `10_BasePass.md` — “8. BasePass 通过 SceneTextures 发布 GBuffer”
> - `12_Lighting.md` — “开篇：Lighting 接手时，世界已经不是 mesh 了”
> - `12_Lighting.md` — “1. Lighting 的输入合约：接住的是‘屏幕’，不是‘场景’”
> - 当前理解目标：掌握 BasePass 发布表面合同、Lighting 消费合同的责任分离。
> - 当前可以跳过：MRT 位布局、Light Grid、Clustered Deferred、Light Shape Mesh 和 shader 细节。
> - 源码搜索：`RenderBasePass`、`SceneTextures`、`DeferredLightPixelMain`。

---

## 知识节点六：Translucency 为什么需要另一条路径

### 当前困惑

为什么半透明物体不能和 Cube 一样，把唯一一份表面属性写进 GBuffer，再由 Lighting 统一处理？

### 最小原理模型

不透明表面在一个像素上通常只保留最前面的可见表面，因此可以把这一层表面编码进 GBuffer。透明则可能同时看到多个层，并且需要读取和混合已经存在的背景颜色：

```text
Opaque BasePass / Lighting
先形成当前 SceneColor 和 SceneDepth
        ↓
Translucency
读取深度、背景颜色和自身材质
按队列与排序规则合成
        ↓
更新后的 SceneColor
```

因此透明不是“Opaque 之后再用同一种合同画一次”，而是依据材质和合成位置进入不同透明队列。

### UE 操作：创建透明材质

1. 打开底部 **Content Drawer**；快捷键通常为 `Ctrl+Space`。
2. 在 `Content` 下新建目录 `RenderingPractice`。
3. 在空白处右键，选择 **Material**，命名为 `M_TestTranslucent`。
4. 双击进入 Material Editor。
5. 选中材质根节点，在 Details 设置：
   - **Material Domain**：`Surface`
   - **Blend Mode**：`Translucent`
   - **Shading Model**：`Unlit`
6. 按住 `3` 在图表空白处单击，创建 Constant3Vector，设为明显的青色或红色，连接 **Emissive Color**。
7. 按住 `1` 单击，创建 Scalar，值设为 `0.35`，连接 **Opacity**。
8. 点击 **Apply**，等待编译完成并保存。

这里使用 Unlit 是为了让实验只关注透明合成路径，而不是同时引入透明光照模型。

### UE 操作：让透明路径进入这一帧

1. 回到关卡，点击 **Add > Shapes > Plane**。
2. 旋转和移动 Plane，使它位于 Camera 与 Cube 之间，但不要完全挡住 Cube。
3. 从 Content Drawer 把 `M_TestTranslucent` 拖到 Plane 上。
4. 如果材质不可见，先旋转 Plane 180 度；仍需双面时，回到材质勾选 **Two Sided** 并重新 Apply。
5. 等待材质编译完成，再执行 `ProfileGPU`。
6. 在 GPU Visualizer 中寻找 Translucency 相关父级或子事件。

现在隐藏这个透明 Plane：

- 固定 Editor Viewport 时，点击 World Outliner 中 Actor 左侧的眼睛图标。
- Play / Standalone 时，选中 Actor，在 Details 勾选 **Actor Hidden In Game**。
- 不要混用不同运行模式的捕获结果。

重新执行 `ProfileGPU`。

### 通常会看到什么，为什么

- 透明物体从画面消失。
- Translucency 子事件、Draw 或耗时可能减少。
- Translucency 父级可能仍存在，因为还有其他透明、雾、调试或固定流程。
- BasePass 与 Lighting 通常仍存在，因为不透明场景没有消失。

如果 event 消失，当前只能证明它没有在这次 GPU scope 树中显示；不能仅凭 GPU Visualizer 证明某个 RDG Pass 被裁剪。

重新点击眼睛图标或取消 **Actor Hidden In Game**，确认透明画面和相关工作恢复。

这个节点建立的模型是：**场景中出现不能由不透明 GBuffer 合同表达的表面时，Renderer 会增加另一条颜色合成路径。**

> **深入阅读：RenderingDeep**
>
> - `14_Translucency.md` — “开篇：透明不是‘opaque 之后再画一遍’”
> - `14_Translucency.md` — “1. 输入合约：半透明进场时手里有什么”
> - `14_Translucency.md` — “2. Pass 队列：材质决定‘何时合成’，队列决定‘在哪排序’”
> - `14_Translucency.md` — “11. 调试主线”
> - 当前理解目标：理解透明为什么不能复用普通不透明 GBuffer 合同，以及它需要读取和修改 SceneColor。
> - 当前可以跳过：OIT、Translucency Lighting Volume、Distortion、FrontLayer 和 Ray Traced Translucency。
> - 源码搜索：`ETranslucencyPass`、`FTranslucencyPassResources`。

---

## 知识节点七：PostProcessing 是 SceneColor 的持续变换

### 当前困惑

为什么添加 Post Process Volume 和固定曝光不是纯粹的实验准备？为什么 Lighting 或透明发生变化后，PostProcessing 仍然存在？

### 最小原理模型

后处理不是最后执行的单个固定 Pass，而是一条根据 View、平台和功能动态构建的颜色处理链：

```text
BasePass 发布表面
→ Lighting 修改 SceneColor
→ Translucency 在特定位置合成
→ TSR / Bloom / Tonemap / Exposure / Debug 等继续修改颜色
→ ViewFamilyTexture
```

`SceneColor` 可以理解为当前阶段正在传递和更新的画面颜色；它并不从一开始就是最终显示结果。

Unity 后处理栈同样会处理 HDR Color Buffer，但 UE 的半透明、TSR、Debug View 和最终 ViewFamily 输出可能在不同位置接入，因此不要把 PostProcessing 理解为一个固定的 `OnRenderImage` 式末端回调。

### UE 操作：比较自动曝光与固定曝光

你在节点五已经创建了 PostProcessVolume。现在先理解它的作用：

1. 选中 PostProcessVolume。
2. 确认 **Infinite Extent (Unbound)** 已勾选。
3. 在 Exposure 中临时把 **Metering Mode** 的 Override 取消，或切回自动曝光模式。
4. 关闭 DirectionalLight 的 **Visible**，观察画面可能在短时间内重新变亮。
5. 恢复灯光。
6. 重新勾选 Metering Mode Override，设为 `Manual`。
7. 再次关闭灯光，观察画面现在不会被 Eye Adaptation 自动补偿到相似亮度。
8. 恢复灯光，并保持 Manual Exposure 作为后续固定条件。

### 现象如何回到原理

自动曝光属于后处理颜色流。关闭灯光改变的是 Lighting 输出，但 Eye Adaptation 可以随后修改曝光，让最终屏幕亮度部分抵消 Lighting 的变化。

所以：

```text
你最终看到的屏幕变化
不只由当前研究的渲染阶段决定
还会被后续颜色链继续修改
```

执行一次 `ProfileGPU`，观察 PostProcessing、Tonemap 或曝光相关工作仍存在。它们不依赖某个动态灯是否开启，也不依赖透明 Plane 是否显示；只要 View 仍需产生最终图像，颜色输出链通常就存在。

> **深入阅读：RenderingDeep**
>
> - `15_PostProcessing.md` — “心智模型：一条不断被重写的颜色流”
> - `15_PostProcessing.md` — “Unity → UE 对照”
> - `15_PostProcessing.md` — “1. 后处理接住的是已解析的 HDR SceneColor”
> - `15_PostProcessing.md` — “8. 最终输出不是 Present，而是写入 ViewFamilyTexture 后收口”
> - 当前理解目标：把 PostProcessing 理解为动态颜色流，并知道自动曝光会改变前序阶段的视觉证据。
> - 当前可以跳过：TSR、Bloom、ACES、CombineLUT 的具体算法。
> - 源码搜索：`AddPostProcessingPasses`、`ViewFamilyTexture`。

---

## 知识节点八：RDG、RHI 与 GPU Event 的证据边界

### 当前困惑

我们已经在 GPU Visualizer 中看到了 BasePass、Lighting、Translucency 和 PostProcessing。Renderer 如何把这些相互依赖的阶段变成真正的 GPU 工作？为什么 GPU Event 不能直接当成源码调用栈？

### 最小原理模型

直到现在再引入 RDG 和 RHI，因为你已经有真实阶段可以放进去：

```text
Renderer
根据 Scene、View 和功能条件决定本帧需要什么
        ↓
RDG
声明 Pass、资源读写和依赖
编译资源生命周期、同步与执行顺序
        ↓
RHI / RHI Thread
把跨平台资源和命令转换为 D3D12 后端工作
        ↓
GPU
执行 Draw、Dispatch、Copy 和同步
```

`AddPass` 只是向 RDG 声明工作，不等于 GPU 已经执行；如果结果没有有效消费端，Pass 可能被裁剪。另一方面，一个 GPU profiling event 可能包住多个 RDG Pass，也可能只是某个功能的父级 scope。

### UE 操作：重新阅读同一棵 GPU Event 树

再次执行 `ProfileGPU`，这次不再只辨认名称，而是按证据层级看：

1. 展开 BasePass、Lighting、Translucency 和 PostProcessing 附近的父子 scope。
2. 观察一个功能可能有多个子事件。
3. 对比隐藏透明物体前后：父级可能保留，子工作可能变化。
4. 对比关闭灯光前后：Lighting/Shadow 子工作变化，BasePass 仍保留。
5. 不尝试从 event 名反推完整 C++ 调用链。

### GPU Visualizer 能证明什么

它能直接告诉你：

- 捕获帧中暴露了哪些 GPU profiling scope。
- Scope 的父子组织和近似耗时。
- 改变场景条件后，相关 GPU 工作是否随之变化。

它不能单独证明：

- Game Thread 上某个 UObject 字段的值。
- 一个 Event 严格对应一个 RDG Pass。
- 某个具体 C++ 条件分支已执行。
- Event 消失一定是 RDG Culling。
- 资源为什么发生某次 Barrier 或 Alias。

工具的正确用法不是“看见名字就得出最终结论”，而是先把问题定位到某一层：

- GT / RT / RHI 时间线问题：Unreal Insights。
- GPU Draw、Dispatch、资源内容问题：RenderDoc 或 PIX。
- RDG Pass、资源生命周期和裁剪问题：RDG Debug 与源码。
- 材质和 Shader 变体问题：MaterialPipeline、ShaderSystem 和 ShaderMap。

> **深入阅读：RenderingDeep**
>
> - `04_RHI.md` — RHI 抽象模型、资源和命令提交边界
> - `05_RenderGraph.md` — RDG 的声明、编译与执行主线
> - `23_Debugging.md` — “Pass 名是证据链的坐标”
> - `23_Debugging.md` — “GPU Visualizer 回答‘GPU scope 如何组织’”
> - 当前理解目标：理解 RDG 负责本帧资源与 Pass 计划，RHI 负责后端提交，GPU Event 只是执行侧的观察坐标。
> - 当前可以跳过：Transient Aliasing、Async Compute fork/join、后端描述符和 Barrier 实现细节。
> - 源码搜索：`FRDGBuilder::AddPass`、`FRHICommandList`、`ProfileGPU`。

---

## 全篇模型回放

现在可以把一帧重新串起来：

```text
Level / World 中存在 Actor
Actor 通过 Component 表达 Mesh、Light、Camera 等能力
        ↓
Camera 产生 View 请求
Renderer 为 View 建立本帧工作空间
        ↓
Game Thread 的 Component 状态
通过 SceneProxy / SceneInfo 同步到 Render Thread 的 FScene
        ↓
Render Thread 组织本帧可见工作
        ↓
BasePass 发布不透明表面合同
Lighting 消费表面、灯光和阴影
Translucency 使用另一条合成路径
PostProcessing 持续更新 SceneColor
        ↓
RDG 组织 Pass、资源和依赖
RHI 转换并提交后端命令
GPU 执行，GPU Visualizer 暴露部分 profiling scope
```

这张图不是要求你背诵，而是后续所有实践的坐标系：

- 对象为什么没有进入画面：从 Actor / Component → Proxy / Scene → View 可见性查。
- GPU 某阶段变贵：从对应数据合同和场景需求查。
- Event 消失：区分场景无工作、条件跳过、Scope 合并和 RDG 裁剪。
- 最终画面和中间阶段不一致：检查 SceneColor 后续颜色流。

## 进入下一篇前应该能够解释

- Actor、Component、SceneProxy 和 `FScene` 为什么不能合成一个对象。
- Scene 与 View 为什么分开，CameraActor 为什么不等于 `FViewInfo`。
- Game、Draw/Render、GPU 为什么是相关但不同的时间线。
- BasePass 与 Lighting 的数据合同为什么分开。
- 透明为什么不能简单复用普通不透明 GBuffer 路径。
- PostProcessing 为什么会改变你对前序阶段的视觉判断。
- RDG、RHI 和 GPU Event 分别处于哪一层，GPU Visualizer 的证据边界在哪里。

下一篇应沿着本篇留下的第一个具体缺口继续：**Component 的变化究竟怎样进入 SceneProxy、SceneInfo 和 FScene，并在移动、换材质、换 Mesh、删除对象时走不同更新路径。**
