# 01 渲染架构总览

> **源码版本**: UE5.7  
> **前置阅读**: 无（本篇是起点）  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）  

---

Unity TA 第一次读 UE 渲染时，最容易误判的是：以为 UE 只是把 Unity SRP 那条“Camera -> Culling -> DrawList -> CommandBuffer”的路换成了更长的 C++ 函数栈。

真正难的不是找到第一个函数，而是理解 UE 为什么不能让 Game Thread 直接把当前世界画出去。Game Thread 手里是会变化的 UObject 世界；Renderer 需要的是跨帧稳定的场景数据库；一帧渲染需要的是当前 View 的可见集合和 Pass 计划；RHI 需要的是平台无关的 GPU 命令；D3D12、Vulkan、Metal 后端只认识自己的原生命令。每一层都在把上一层的数据改造成下一层能稳定消费的形态。

本篇只回答一个主问题：

**当 Game Thread 已经算完这一帧的世界状态后，UE 怎样把“从这个相机渲染这个世界”变成 GPU 能执行的命令？**

先给出一句压缩模型：UE 渲染不是让 Game Thread 直接画，而是把两类输入分开整理，再在 Renderer 内汇合。

- **视图请求**：相机、Viewport、ShowFlags 变成“这一帧从哪里看”的请求。
- **对象数据**：可变的游戏对象变成 Renderer 侧的持久场景。
- **汇合**：两者在可见性阶段相遇，压成本帧工作集，再依次变成 RDG/DrawCommand 执行计划、RHI 命令、平台后端命令。

整篇就是带着金属球，把这两条流从 Game Thread 一路跟到 GPU。

为了避免后面被类型名淹没，先把整条路压成四次“换形”：

1. **请求成形**：Game Thread 把 Camera、Viewport、ShowFlags 和输出目标冻结成一帧视图请求。
2. **场景成形**：Component 不再以 UObject 形态被 Renderer 读取，而是变成 Proxy / SceneInfo，进入 Renderer 维护的持久场景。
3. **工作集成形**：本帧 View 查询持久场景，把“场景里有什么”压成“这个 View 这一帧要处理什么”。
4. **命令成形**：Renderer 把工作集组织成 Pass/资源计划与 mesh draw，再降到 RHI 和平台后端命令。

这四步不是四个函数，也不保证各占一个线程。它们描述的是四种数据状态，以及控制权何时从上一层交给下一层。后文遇到任何类型，都可以先问：它属于哪一次换形？谁拥有它？它成立的前提是什么？失败后会在哪一层留下症状？

为了让这条模型落到具体对象上，本篇一直跟同一个例子走：

> **贯穿案例**：关卡里有一个红色、偏粗糙的金属球。它是一个 `UStaticMeshComponent`，主相机这一帧正看着它。我们关心的不是金属材质的 BRDF，而是它的数据怎样从 Game Thread 的 Component 走到 Renderer，再走到 GPU draw。

---

## 本章边界

01 是全景入口，职责是走通主路径和模块边界，不提前讲完后续专题。它必须讲透：

- UE 为什么把渲染拆成 Engine、Renderer、RenderCore、RHI、平台后端几层。
- Game Thread 如何发起一帧渲染，但不直接执行渲染。
- Engine 如何通过 `IRendererModule` 接口进入 Renderer，而不是直接依赖具体 renderer 实现。
- `FScene` 为什么是跨帧持久场景，`FSceneRenderer` 为什么是一帧执行对象。
- Component 的渲染数据如何进入 Renderer 侧场景，再被本帧可见性筛选。
- RDG、MeshDrawCommand、RHICommandList、平台后端在主路径上的职责边界。

它只点到这些后续内容：

- Component -> Proxy -> SceneInfo 的完整生命周期、Transform 更新、销毁延迟清理属于第 02 篇。
- Render Thread / RHI Thread / Frame sync / RenderCommandPipe 细节属于第 03 篇。
- RHI 资源生命周期、PSO、Shader Binding、barrier 原语属于第 04 篇。
- RDG 编译、transient aliasing、async compute、extract/external 语义属于第 05 篇。
- GPUScene buffer layout 和增量上传策略属于第 06 篇。
- MeshDrawCommand 排序键、缓存键、合批、Instance Culling 属于第 07 篇。
- `FDeferredShadingSceneRenderer::Render` 内每个 Pass 的算法属于第 08-15 篇。

因此本篇不会展开 GBuffer 通道格式、Nanite cluster culling、VSM page marking、Lumen surface cache、BackBuffer present 或帧同步。它们会在主路径中出现，但这里只说明“这一步为什么需要它”和“结果交给哪里”。

---

## 三套坐标轴：过程、责任与寿命

读 UE 渲染源码时，最容易混在一起的是三个不同问题：数据走到哪一步、当前落在哪个责任域、这份状态能活多久。本章固定使用三套坐标轴：

- **过程轴**：请求成形、场景成形、工作集成形、命令成形，回答数据怎样前进。
- **责任轴**：Engine、Renderer、RenderCore、RHI、平台后端，回答谁提供能力、谁做决定。
- **寿命轴**：World/Component、Renderer Scene、Frame/View，回答数据何时成立、何时失效。

三套轴不是数量对应关系。一次换形可能跨多个模块，同一个模块也可能参与多次换形。读完本章，你应该能用自己的话回答下面的问题：

- 为什么 Game Thread 不直接调用 GPU，也不让 Renderer 直接读 `UPrimitiveComponent`？
- `FSceneViewFamily`、`FSceneRenderer`、`FScene`、`FViewInfo` 各自解决什么问题，为什么不能合成一个对象？
- `RenderCore` 为什么既不是 Renderer，也不是 RHI，却要单独存在？
- 金属球怎样从 `UStaticMeshComponent` 一路变成 `FPrimitiveSceneProxy`、`FPrimitiveSceneInfo`、可见性结果、`FMeshDrawCommand`，最后落到 `FRHICommandList`？
- RDG 和 MeshDrawCommand 为什么同时存在，它们各自把“画金属球”变成了什么？
- 如果金属球没画出来，应该沿哪些数据形态往回查，而不是先去翻材质参数？

读后文时，先给这些名字带上一张“概念护照”。重点不是背类型，而是把 owner、data、control、lifetime 和 debug question 放在一起：

| 概念 | 它保存什么 | 谁维护 / 生命周期 | 它控制什么 | 成立条件与调试问题 |
|------|------------|-----------------|------------|--------------------|
| `FSceneViewFamily` | 一帧从哪些 View 看哪个 Scene、画到哪个目标的请求 | Game Thread 构造，交给 Renderer；只服务本次渲染请求 | 决定本帧渲染请求的外部边界，不保存可见性结果 | Scene、View、输出目标必须配对；先查请求是否指向正确场景和目标 |
| `FScene` | Primitive、Light、空间索引、缓存 draw 数据等 Renderer 侧稳定状态 | Renderer / 渲染线程上下文跨帧维护 | 提供“场景里有什么”的可查询事实 | Proxy/SceneInfo 的更新必须先被吸收；对象缺席时不要先查本帧 pass |
| `FSceneRenderer` | 本帧怎样使用 ViewFamily 查询 Scene、组织 View、可见性和 Pass 的执行状态 | Renderer 为本帧创建，用完即结束 | 控制这一帧渲染算法的推进 | 必须基于正确 ViewFamily 和已更新 Scene；它不是场景数据库 |
| `FViewInfo` | Renderer 侧 View 工作区：visibility maps、动态 mesh 收集和 Pass 临时状态 | 随本帧 `FSceneRenderer` 存活 | 把一个 View 的查询结果传给后续阶段 | 依赖正确的 Scene 与视图参数；目标 Primitive 是否被标可见是关键断点 |
| RDG | Pass、资源读写和执行依赖 | 本帧声明、编译、执行 | 决定 GPU 工作按什么资源依赖发生 | Pass 输出必须被需要且依赖成立；查 Pass 是否存在、被裁剪或未执行 |
| `FMeshDrawCommand` | 某个 mesh 在某个 pass 中提交所需的近 RHI draw 描述 | 静态内容可缓存，本帧再筛选；动态内容按帧收集 | 决定一条可见 mesh draw 如何进入 pass | 依赖 visibility、pass relevance 和 command 构建；查 draw 是否进入提交集合 |
| `FRHICommandList` / 平台后端 | 平台无关 GPU 工作、平台命令包与完成证据 | 从命令录制到目标 GPU consumer 完成分阶段维护 | 把 Renderer 意图依次推进到 record、formation、Queue Submit 与 GPU consumption | 到这里已不理解 Component 或 ViewFamily；按 record、formation、submit、consume 分级查证 |

表里最重要的边界是：`FScene` 回答“长期有什么”，`FViewInfo` 回答“这个 View 当前选中了什么”，`FSceneRenderer` 回答“这一帧怎样推进”，RDG 与 MeshDrawCommand 回答“选中的工作怎样被组织”，RHI 回答“怎样落到 GPU”。把这些职责合在一个对象里，会把跨帧状态、本帧临时状态和平台命令绑成同一生命周期，既难并发，也无法让多个 View 复用同一个场景。
---

## 先按数据形态看五层

开篇的“四次换形”和这里的“五层责任”是两条不同坐标轴：四次换形回答**数据怎样沿时间向前变化**，五层责任回答**哪一个责任域有权完成这次变化**。它们不是数量对应关系：一次换形可能跨过多个模块，同一个模块也可能参与多次换形。

UE 的渲染模块名很多，但第一遍不要背模块名，先看每层接收什么、输出什么。一帧渲染不是一条单线，而是两条流在 Renderer 内汇合：

```text
视图请求流
  Camera / Viewport / ShowFlags
    -> FSceneViewFamily               一帧要看哪个场景、哪些 View、画到哪里
    -> FSceneRenderer / FViewInfo     本帧执行对象和 Renderer 侧视图工作区

对象数据流
  UStaticMeshComponent
    -> FPrimitiveSceneProxy           Engine -> Renderer 的渲染数据桥
    -> FPrimitiveSceneInfo
    -> FScene                         Renderer 侧持久场景数据库

汇合点
  FScene + FViewInfo
    -> visibility maps                当前 View 中哪些对象要画
    -> RDG Pass / FMeshDrawCommand    本帧 GPU 工作和 mesh draw 描述
    -> FRHICommandList                记录跨平台 GPU 工作
    -> backend context                消费并翻译 RHI 工作
    -> D3D12 / Vulkan / Metal command 形成平台命令包
    -> platform queue submit          队列接管对应工作
    -> GPU consumed                   GPU 越过这次工作的目标完成点
```

后面说“红色金属球穿过整条路径”时，指的是对象数据流先进入 `FScene`，再在当前 ViewFamily 生成的 `FViewInfo` 下被筛选和提交。金属球不会变成 ViewFamily；ViewFamily 是这一帧的相机请求，金属球是被这个请求查询的场景对象。

这里有一个贯穿全章的必要条件：**对象流必须先形成可查询的持久状态，视图流才能在本帧把它选进工作集。** 如果金属球仍停留在 Game Thread Component，或者 SceneInfo 还只是待处理更新，后续 visibility、BasePass 和 RHI 都没有机会“补救”它。反过来，即使金属球已经在 `FScene` 中，错误的 ViewFamily、被裁掉的 visibility、缺失的 MeshDrawCommand 也会让它在更晚阶段消失。架构分层的调试价值，就在于每一层都能证明上一层产物是否已经满足下一层条件。

这条数据流对应五个职责层：

| 层 | Owner / 输入 | 正向输出 | 下一消费者 / 寿命 | 边界意义 |
|----|------|------|----------|----------|
| Engine | Game Thread；`UWorld`、Viewport、Camera、Component、材质资产、用户设置 | `FSceneViewFamily` 和场景对象更新请求 | Renderer；请求按本次 ViewFamily 存活，对象请求等待 Scene 吸收 | 整理游戏世界和本帧视图，不直接产生 GPU 命令 |
| Renderer | Renderer 执行上下文；`FSceneViewFamily` 和 Renderer 侧 `FScene` | `FSceneRenderer`、可见性结果、Pass 计划、MeshDrawCommand | RDG/Mesh Pass；`FScene` 跨帧，View/工作集通常只属本帧 | 决定这一帧画什么、按什么渲染算法画 |
| RenderCore | 公共基础设施；Renderer 和其他模块的机制需求 | `IRendererModule`、渲染命令投递、`FRenderResource`、RDG、Shader 基础设施 | Renderer/RHI 等调用方；各对象遵守自身资源或帧图寿命 | 提供公共机制，不决定具体场景内容 |
| RHI | RHI command/context；平台无关的 GPU 操作请求与资源合同 | recorded RHI work、`FRHIResource`、后端可消费操作 | 平台后端；命令载荷和资源必须覆盖实际消费期 | 隔离 D3D12/Vulkan/Metal，Renderer 不直接碰平台 API |
| 平台后端 | backend/context/queue；RHI work 与资源 | 可提交原生命令包、Queue Submit 与完成证据 | 平台 queue/GPU；资源有效到最后 GPU consumer 越过使用点 | 形成并提交 draw/dispatch，不理解 UE 场景语义 |

如果你带着 Unity 的概念进来，可以先用下面这张映射表对齐方向。它只帮你找到“大概落在哪一层”，不是严格等价——每条都标出了 UE 的关键差异，后文会逐个展开。

| Unity 侧熟悉的东西 | UE 大致对应 | 关键差异（后文展开） |
|----|----|----|
| Camera + 一次 render request | `FSceneViewFamily` | UE 先把视图请求冻结成请求包，不等于一份完整渲染计划 |
| `CullingResults` | 当前 View 的 visibility maps | UE 静态 mesh 更依赖注册时缓存，每帧只做筛选 |
| `RendererList` / per-frame draw 列表 | `FScene`（持久）+ `FViewInfo`（本帧） | UE 把“场景里有什么”和“这一帧画什么”拆成两个对象 |
| `CommandBuffer` / SRP RenderGraph | RDG Pass 计划 + `FMeshDrawCommand` | UE 用两套计划：RDG 管 Pass/资源，MeshDrawCommand 管单次 mesh draw |
| 底层图形 API（你通常碰不到） | RHI + 平台后端 | UE 让 Renderer 面向 RHI 录命令，后端再翻译到 D3D12/Vulkan/Metal |
| （无对应概念） | `RenderCore` | 多模块共享的渲染基础设施层，是 Unity 读者最难定位的一层 |

知道了每层接收什么、输出什么，下一个问题自然是：UE 为什么要拆出这么多层，而不让 Game Thread 直接把世界画出来？

---

## 为什么 UE 要分这么多层

这套分层不是为了“看起来架构清晰”，而是为了同时解决三个具体问题。

第一，线程安全。Game Thread 可以改 Component，Render Thread 可以准备渲染。如果 Renderer 直接读 UObject，就要在高频路径上加锁，或者承担对象生命周期风险。

第二，模块隔离。Engine 需要知道“有一个 Renderer 接口”，但不应该知道 `FDeferredShadingSceneRenderer`、八叉树、GPUScene 或 cached draw list 这些实现细节。Renderer 可以读取 Engine 提供的 Proxy/Component 接口，但不能把 Renderer 私有数据结构塞回 Engine。

第三，缓存和扩展性。UE 的 Renderer 侧场景不是每帧从零开始的临时列表，而是跨帧维护的 `FScene`。静态 mesh 可以在加入场景或更新时预先沉淀一部分 draw 决策，每帧再按当前 View 筛选。Unity SRP 公开层常让你围绕 `CullingResults` / `RendererList` 建立心智模型；UE 需要先接受“持久场景数据库 + 帧内执行计划”这一层。

这三个问题决定了五层之间的依赖方向：Engine 只朝 Renderer 提需求，不反向依赖；Renderer 借公共机制工作，但不直接碰平台 API。而把“公共机制”从 Renderer 里单独抽出来的那一层，正是五层里最难定位的 `RenderCore`。

---

## 最容易看不懂的一层：RenderCore

五层里，Unity 读者第一次最难定位的就是 `RenderCore`，因为它不对应任何一个 Unity 概念。它的正面角色是 UE 渲染系统的公共运行时底座：凡是多个模块都需要、但又不属于某一种场景渲染算法的机制，都会放在这里。它让 Engine、Renderer、Slate、Niagara、材质系统和插件可以共享同一套渲染基础设施，而不是互相依赖私有实现。

它把“渲染系统必须共用的规则和工具”从具体 Renderer 中抽出来：

- **公共接口**：`IRendererModule` 让 Engine 可以说“请渲染这个 ViewFamily”，而不用知道 `FDeferredShadingSceneRenderer`、`FViewInfo` 或可见性内部结构。
- **渲染线程投递**：`ENQUEUE_RENDER_COMMAND` 和渲染命令调度机制让 Engine、Renderer、插件都能把工作送到渲染线程上下文。
- **渲染资源生命周期**：`FRenderResource`、`InitRHI`、`ReleaseRHI` 等契约让 CPU 侧对象以统一方式拥有或释放 RHI 资源。
- **帧内图和 Shader 基础设施**：RDG、shader 参数宏、global shader、vertex factory 等机制让多个渲染子系统用同一套方式声明资源、shader 参数和 pass 关系。

有了这个正面角色，边界也就清楚了。红色金属球这一帧是否可见、进入哪一个 mesh pass、BasePass 是否写 GBuffer、Lumen 或 VSM 是否插入分支，这些不是 `RenderCore` 的决策，而是 `Renderer` 的场景渲染职责。`RenderCore` 也不负责把 draw 翻译成 D3D12/Vulkan/Metal 原生命令，那是 RHI 和平台后端的职责。

所以一帧主路径可以这样理解：`Renderer` 借助 `RenderCore` 提供的工具组织工作。`Renderer` 决定金属球要不要画、在哪个 pass 画；`RenderCore` 提供如何把渲染命令投到正确线程、如何管理渲染资源生命周期、如何用 RDG 描述 pass 与资源关系、如何表达 shader 参数的公共规则；RHI 再把这些工作落成平台无关的 GPU 命令。

把红色金属球放进这条路里看，会更容易抓住 `RenderCore` 的边界。Engine 通过 `IRendererModule` 说“请渲染这个 ViewFamily”，这是公共接口；Renderer 把真正执行包装成 `ENQUEUE_RENDER_COMMAND`，这是公共线程投递规则；BasePass、阴影或后处理用 RDG 描述自己读写哪些资源，这是公共帧内图规则；Shader 参数、global shader、vertex factory 也靠公共声明和绑定机制协作。可是这些公共机制本身并不判断金属球是否在相机里、是否进入 BasePass、是否使用 Nanite 或 Lumen 分支。那些判断仍然属于 Renderer 的场景渲染算法。

因此调试时可以把 `RenderCore` 问题和 Renderer 问题拆开：如果命令没有进入渲染线程上下文，优先看 render command 投递；如果 BasePass 这个 RDG Pass 被裁剪或资源依赖不对，优先看 RDG 声明和消费关系；如果金属球根本没进可见集合，那不是 `RenderCore` 在决定“不要画它”，而是 Renderer 的场景、View 和可见性路径没有把它推进到提交集合。

如果没有 `RenderCore`，两个坏结果会同时出现。Engine、Slate、Niagara、材质系统和插件等只想使用渲染基础设施的模块，会被迫依赖 `Renderer` 的私有实现；而 `Renderer` 自己也会把线程投递、资源生命周期、RDG、Shader 元数据和场景渲染算法揉成一团。UE 把它拆出来，是为了让“公共渲染机制”和“具体场景渲染算法”分开演进。

这个分层也给调试提供了分叉点：如果 ViewFamily 没进入 Renderer，查模块接口交接；如果命令没到渲染线程，查 render command 路由；如果 Pass 被裁剪或资源依赖不对，查 RDG；如果金属球没进可见集合，查 Renderer 的场景和可见性；如果 RHI draw 没发出去，再往 RHI / 后端查。不同问题不要混在一个“渲染没画”里。

---

## 第一次换形：请求成形

### 从 Engine Tick 到视图请求：请求从哪里开始

“请求成形”并不是凭空发生。Engine 主循环先推进整帧，Viewport 表达输出表面需要重绘，Viewport Client 才决定从哪个世界、哪些相机、以什么显示选项生成视图请求。

这三级入口应按责任理解，而不是按调用栈背诵：`FEngineLoop::Tick` 推进整帧但不拥有场景渲染算法；`FViewport::Draw` 表达重绘需求但不决定具体 Pass；`UGameViewportClient::Draw` 组合 ViewFamily 和 View，但仍不访问平台 GPU API。这样的分离允许一个世界被主视口、编辑器视口、SceneCapture 或离屏请求复用。

### Game Thread 先生成一帧视图请求

一帧开始时，Game Thread 手里有游戏世界、相机、Viewport、ShowFlags 和输出 RenderTarget。它此刻不应该画红色金属球，而是要把“这帧从哪里看、看哪个场景、画到哪里”打包成稳定请求。

这个阶段不要先背调用顺序，先看它冻结出的产物：

```text
Game Thread 当前状态
  相机 / Viewport / ShowFlags / 输出目标
    -> FSceneViewFamily
        一帧要看哪个 Scene、有哪些 View、画到哪个 RenderTarget
    -> IRendererModule::BeginRenderingViewFamily(...)
        把这份视图请求交给 Renderer
```

`FSceneViewFamily` 是这一步的关键产物。它不是完整渲染计划，也不是场景对象列表。它包含一个或多个 `FSceneView`、目标 `FSceneInterface`、输出 `RenderTarget`、`EngineShowFlags`、时间、ViewExtensions 等“一帧视图族”的信息。红色金属球此时还没有被判定可见；ViewFamily 只是告诉 Renderer：这一帧要从哪些 View 看哪一个 Scene。

为什么要先做 ViewFamily？主渲染路径通过把相机、Viewport、ShowFlags 和输出目标等一帧级信息冻结成请求，避免渲染侧消费阶段依赖仍在变化的 Game Thread 状态。参与渲染的扩展机制仍必须遵守各自的同步与生命周期契约，不能把 ViewFamily 的冻结边界理解成所有路径都绝对禁止任何受控交互。

这里要注意一个边界：`FSceneViewFamily` 有 `Scene` 指针，但不是把 `UWorld` 交给 Renderer 随便读。它通过 `FSceneInterface` 指向 Renderer 可使用的场景接口，最终让 Renderer 拿到 `FScene`。从这一刻开始，Renderer 只应该看已经整理好的视图请求和 Renderer 侧场景数据。

---

### Engine 在这里把请求交给 Renderer

`BeginRenderingViewFamily` 的正面职责是把 ViewFamily 的控制权从 Engine 交给 Renderer。Renderer 接收本次请求后，先保证 Game Thread 积累的渲染相关更新已经送出，再创建本帧执行对象，并把真正的 render 节点包装成渲染线程命令。这个交接建立的是 Renderer 可开始组织本帧工作的输入，还不是“GPU 已经开始绘制”。

交接阶段可以按四步理解：

```text
FSceneViewFamily
  -> 发送帧末渲染更新
  -> FSceneRenderBuilder 创建 FSceneRenderer
  -> builder 记录一个 render node
  -> ENQUEUE_RENDER_COMMAND 把 render node 送到渲染线程上下文
```

帧末更新这一步很重要。Game Thread 这一帧可能新增 Component、修改 Transform、改材质参数或销毁对象。Renderer 在真正组织本帧之前，需要先把这些 pending update 送往渲染侧。否则后续 `FSceneRenderer` 看到的 `FScene` 可能还是旧状态。

然后 `FSceneRenderBuilder` 根据 ViewFamily 创建具体的一帧 renderer：deferred 路径会得到 `FDeferredShadingSceneRenderer`，mobile 路径会得到 `FMobileSceneRenderer`。这里容易混淆两个对象：

```text
FScene
  Renderer 侧持久场景数据库，跨帧存在

FSceneRenderer
  本帧执行对象，持有 ViewFamily、Views 和本帧临时执行状态
```

`FSceneRenderer` 不是场景数据库。它更像“这帧查询和执行 `FScene` 的工作对象”。真正跨帧保存 Primitive、Light、cached draw command、GPUScene 等数据的是 `FScene`。所以如果金属球已经注册到场景里，它属于 `FScene`；如果这一帧主相机要不要画它，则是 `FSceneRenderer` 和它的 `FViewInfo` 在这一帧要解决的问题。

最后，builder 记录的 render node 不会在 Game Thread 直接执行。它被包装进渲染命令，进入渲染线程上下文之后，才会创建 RDG builder、调用 render function，并在末尾执行图。这个设计把“本帧要执行什么”和“在哪个线程上下文执行”分开，避免 Engine/Renderer 在 Game Thread 上直接碰 GPU 工作。

如果这一段出问题，常见现象是：ViewFamily 生成了，但 Renderer 没接到；帧末更新没送出，导致对象状态滞后一帧或多帧；render command 没执行，导致 RDG 图根本没建立。它们不是材质问题，也不是 BasePass 算法问题，而是交接边界的问题。

---

### 渲染命令把执行权跨到渲染线程上下文

UE 不把“调用 Renderer”简单等同于“马上在当前线程跑 Renderer”。它会把要在渲染线程上下文执行的工作包成 render command。对本篇来说，只需要先建立三个事实：

- `ENQUEUE_RENDER_COMMAND` 包装的是一个将在渲染线程上下文消费的 lambda。
- render command 内会拿到可用的 RHI command list 环境。
- `FRDGBuilder` 和真正的 `RenderViewFamily_RenderThread` 调用发生在这个上下文里，而不是 Viewport draw 那一层。

这也是为什么 Game Thread 不能把 Component 指针一路传到 GPU 命令。Game Thread 的 UObject 世界是活的，会继续变化；Render Thread 消费的是通过命令边界冻结下来的渲染数据和请求。

第 03 篇会深入 Render Thread、RHI Thread、RenderCommandPipe、Fence 和 Frame sync。本篇只需要记住：**BeginRenderingViewFamily 是 Engine -> Renderer 的请求交接；render command 是执行权跨到渲染线程上下文的边界。**

---

## 第二次换形：场景成形

### Component 必须先变成稳定场景数据

到目前为止，我们只解释了“这一帧从哪里看”。红色金属球作为对象本身，还要走另一条路径进入 Renderer 侧场景。

如果 Renderer 直接读 `UStaticMeshComponent`，问题马上出现：Game Thread 可能在同一帧继续改 Transform、材质或可见性；Component 的 UObject 生命周期也不归 Render Thread 管。UE 的做法是把 Component 的渲染相关状态提取成 Renderer 可消费的对象：

```text
UStaticMeshComponent
  -> CreateSceneProxy()
  -> FPrimitiveSceneProxy
  -> FPrimitiveSceneInfo
  -> FScene
```

`FPrimitiveSceneProxy` 是 Engine -> Renderer 的渲染数据桥。它不是整个 Component，也不是可随便回读 UObject 的代理。它保存 Renderer 需要的、可以跨线程消费的渲染数据，例如网格资源引用、材质相关引用、渲染标志，以及后续在渲染线程侧写入的 Transform/Bounds 状态。

`FPrimitiveSceneInfo` 是 Renderer 侧管理外壳。Proxy 更像“这个 Primitive 能被怎样画”的数据桥，SceneInfo 则把它放进 Renderer 的场景管理系统里：空间索引、dense arrays、GPUScene、cached draw command、静态 mesh 缓存等都需要一个 Renderer 私有的容器来管理。这样 Engine 不需要知道 Renderer 内部有哪些索引结构，Renderer 也不用把这些私有状态塞回 Component。

添加路径与其说是一串调用，不如说是金属球的渲染数据沿着三个所有权阶段往前交接——每跨一格，谁来读写这份数据就换了一次手：

```text
[阶段 1] Game Thread 添加 Primitive
  -> 创建 Proxy
  -> 创建 SceneInfo
  -> 投递 AddPrimitiveCommand
  （此后 Game Thread 不再触碰 Proxy）

[阶段 2] 渲染线程上下文消费 AddPrimitiveCommand
  -> 设置 Transform / Bounds
  -> 创建渲染线程资源
  -> 把 SceneInfo 排进 PrimitiveUpdates
  （此时还只是“排队中”，不在可查询场景里）

[阶段 3] FScene::Update
  -> 批量吸收 PrimitiveUpdates
  -> 进入 Renderer 持久场景结构
  （到这里金属球才成为可见性能查到的稳定条目）
```

这个顺序有两个重点。

第一，`CreateSceneProxy()` 失败或返回空，Renderer 侧就没有这个对象。后续可见性、MeshDrawCommand、BasePass 都不会凭空找到它。

第二，SceneInfo 被创建并不等于它已经进入可见性可查询的稳定场景。它先进入 `PrimitiveUpdates`，等 `FScene::Update` drain 后，才真正合入 `FScene` 的持久结构。这个“先排队、再批量吸收并发布”的模型让 Renderer 可以统一处理增删改，而不是每个 Component 更新都立刻修改复杂的场景索引。

第 02 篇会把 Proxy 具体持有什么、Transform 更新如何跨线程、销毁为什么要延迟清理全部展开。本篇只需要记住：**Component 不能直接被 Renderer 热路径读取；它必须先变成 Proxy/SceneInfo，并在 `FScene::Update` 中进入持久场景。**

---

### 三种寿命在场景成形处完成分工

| 寿命 | 代表对象 | 权威数据与责任 |
|---|---|---|
| 世界寿命 | `UWorld`、Component、资产 | 保存游戏语义，可被 Game Thread 修改 |
| 场景寿命 | `FScene`、Proxy、SceneInfo 与场景级登记 | 保存 Renderer 可持续查询的场景事实，跨多帧复用 |
| 帧寿命 | `FSceneRenderer`、`FViewInfo` | 保存这一次观察、筛选和执行所需的临时答案 |

`FScene` 偏向数组、稳定索引、空间查询结构和缓存，是因为 Renderer 更常做批量扫描、按索引关联和跨帧复用。连续数据服务批处理，空间结构服务区域查询，缓存避免重复生成稳定配方；它们是同一批 primitive 的不同查询视图，不是互不相关的场景副本。

红色金属球此时的权威数据已从可变 Component 转为 Renderer 侧 Proxy/SceneInfo；控制者是 Renderer 的 `FScene` 更新路径；下一状态证据是 `FScene::Update()` 完成 Scene publication，使它进入 `FScene` 的可查询结构。

---

### OnRenderBegin 让 FScene 吸收变化

当 render command 进入渲染线程上下文后，Renderer 需要先组织 `FScene` 对已排队场景变化的吸收，并为 visibility 及其前置依赖建立可推进的起点。

可以把 `OnRenderBegin` 理解成这一帧 Renderer 的阶段门和编排入口：它组织 Scene 更新、visibility 启动与相关前置依赖，但不证明 visibility 已经完成。

```text
进入 render command
  -> 创建 FRDGBuilder
  -> RenderViewFamily_RenderThread
  -> FSceneRenderer::OnRenderBegin
      -> FScene::Update
          -> drain PrimitiveUpdates
          -> 更新 Renderer 侧场景结构
```

`FScene::Update` 做的事情很多，但在 01 的边界内，我们只关心它的架构意义：它把“排队的场景变化”变成“本帧可查询的稳定场景状态”。红色金属球如果是这一帧新加入的对象，只有这一步之后，后续可见性才有机会在 `FScene` 的 Primitive 列表、空间索引和静态 mesh 数据里看到它。

这也是 `FScene` 和 `FSceneRenderer` 的差别再次出现的地方。`FScene` 是被更新的持久数据库；`FSceneRenderer` 是驱动这次更新和随后渲染流程的本帧执行对象。把这两者混在一起，会导致调试时找错方向：对象没有进 `FScene`，不是 `FViewInfo` 的问题；对象进了 `FScene` 但没有被当前相机看到，才轮到 visibility 阶段。

---

## 第三次换形：工作集成形

### 为什么视图请求还要变成 Renderer 工作区

`FSceneViewFamily` 只是 Engine 侧传来的视图请求。Renderer 真正跑可见性和 pass 编排时，需要更贴近内部数据结构的视图对象，这就是 `FViewInfo`。

`FViewInfo` 可以理解为 `FSceneView` 的 Renderer 工作区版本。它继承了相机矩阵、view rect、show flags 等视图输入，同时增加 Renderer 私有状态，例如：

- 当前 View 的 `PrimitiveVisibilityMap`。
- 当前 View 的 `StaticMeshVisibilityMap`。
- dynamic mesh 收集结果。
- pass setup、GPUScene、HZB、view uniform buffer 等后续阶段需要的临时状态。

为什么这些不能放在 `FSceneViewFamily` 或 Engine 侧？因为它们依赖 Renderer 私有的 `FScene` 索引。`PrimitiveVisibilityMap` 和 `StaticMeshVisibilityMap` 的 bit 位对应 `FScene` 内部 Primitive / StaticMesh 的索引，不应该暴露给 Engine。Engine 负责描述“这一帧要看哪里”；Renderer 负责把这个请求变成“本帧具体哪些内部对象要参与绘制”。

这一步之后，红色金属球不再只是“场景中有一个 Primitive”。Renderer 可以用当前 View 的 frustum、show flags、遮挡信息和场景索引，判断它这一帧是否进入提交集合。

到这里，两条输入终于具备汇合条件：`FScene` 负责回答“Renderer 侧稳定保存了什么”，`FViewInfo` 负责回答“当前 View 要怎样查询它”。下一步的可见性，就是把这两个答案压成“这一帧到底画哪些对象”。

---

### 可见性把持久场景压成本帧工作集

`FScene` 可能保存大量 Primitive，但某个 View 某一帧只画其中一部分。红色金属球已经在 `FScene` 里有了 SceneInfo 和可能的缓存 draw command；它是否真正被画，要等可见性阶段把“场景里有什么”压缩成“本帧画什么”。

输入可以理解为：

```text
FScene:
  Primitives / PrimitiveSceneProxies / StaticMeshes / cached draw data

FViewInfo:
  view matrices / frustum / show flags / visibility bit arrays
```

输出是：

```text
PrimitiveVisibilityMap:
  哪些 Primitive 对当前 View 可见

StaticMeshVisibilityMap:
  哪些 static mesh 候选对当前 View 可见

dynamic mesh 收集结果:
  本帧需要从 Proxy 收集出来的 FMeshBatch

visible mesh draw commands:
  后续 MeshPass 可以消费的命令集合
```

这一步不能被缓存完全替代。缓存命令只能说明“这个 mesh 在某个 pass 中可以怎样画”，不能说明“当前相机这一帧应该画它”。可见性阶段把跨帧场景数据库和当前相机连接起来。

对 Unity 读者来说，可以把这一步和 `CullingResults` 的职责做类比，但不能一一对应。UE 的静态 mesh 路径更强调注册/更新时沉淀 draw command，每帧可见性负责筛选和准备提交；动态 mesh 才更接近“本帧收集、本帧转换”。

如果红色金属球没有画出来，可见性是一个关键断点：它可能没有进入 `FScene`，也可能进入了 `FScene` 但当前 View 的 `PrimitiveVisibilityMap` 没有标记它。前者要回查 Component/Proxy/SceneInfo/FScene 更新链；后者要查 frustum、occlusion、show flags、owner visibility 或场景指针是否正确。

---

## 第四次换形：命令成形

### Render() 是一帧条件化依赖骨架

`FDeferredShadingSceneRenderer::Render` 不是所有对象都严格经过的固定函数清单。更安全的模型是“帧级公共阶段 + 受渲染路径、平台能力、ShowFlags 和对象资格控制的条件分支”：

```text
场景变化发布 / 帧级准备
  -> View 初始化与 visibility
      -> 条件分支：动态几何、动态 GPUScene、Nanite、阴影
      -> 条件分支：PrePass / depth consumers
      -> 条件分支：BasePass 或其他主要材质路径
      -> lighting / translucency / post processing
      -> 输出目标
```

金属球是 `UStaticMeshComponent`，不能用它证明所有对象都经过 dynamic primitive collection；PrePass、BasePass 和 GBuffer 也不是所有 renderer path 都无条件存在。

`SceneTextures` 是帧内阶段之间交换场景图像数据的资源集合，不是固定存在、固定格式的 GBuffer 包，也不是跨帧 `FScene` 数据库。具体成员、格式和成立时机由 deferred、mobile、forward 等路径、平台能力、配置与 Pass 需求共同决定；一些资源会随阶段逐步建立，再作为后续 RDG Pass 的输入或输出。

它的最小概念护照是：

| 项目 | 含义 |
|---|---|
| What | 本帧渲染阶段共享的场景纹理与相关资源集合 |
| Why | 让不同 Pass 通过明确资源状态交换结果 |
| Owner / Lifetime | 帧图与相关资源系统；通常是帧级，外部化或提取时另有合同 |
| Conditions | Renderer path、平台能力、配置和具体 Pass 需求 |
| Boundary | 不等于固定 GBuffer，也不等于持久 Scene |
| Debug | 追踪资源何时创建、由哪个 Pass 首次写出有效内容、谁最后消费 |

阅读 `Render()` 时，应问当前阶段消费什么已成立的数据、生产什么状态、分支成立条件是什么。

---

### 为什么一帧渲染需要两套执行计划

| 计划 | Owner / 输入 | 正向输出 | 下一消费者 / 寿命 | 回答的问题 |
|---|---|---|---|---|
| RDG | Renderer/RDG builder；Pass 与资源读写声明 | 帧级 Pass/资源执行组织 | 图执行与 RHI recording；主要属于当前帧图 | 哪些阶段交换哪些资源 |
| MeshDrawCommand | mesh pass 路径；mesh 描述、pass 语义与当前 View 选择 | pass-specific draw recipe 与 visible draw | 目标 Pass 的 command recording；缓存配方可跨帧，visible 选择属于当前 View | 某个可见 mesh 怎样进入提交 |

RDG 在本章只负责建立 Pass/资源组织边界；裁剪、生命周期、barrier、aliasing 和 async compute 留给第 05 篇。MeshDrawCommand 在本章只描述 `registered mesh description → pass-specific draw recipe → current-view visible draw → RHI recording`；随后由 backend 形成平台命令并执行 Platform Queue Submit。processor、缓存、排序、合批和 Instance Culling 留给第 07 篇。

金属球进入 BasePass 时，需要分别证明 BasePass 这类帧级工作成立，以及金属球的 visible draw 被选入该 pass。看见 Pass 名称不能证明对象 draw 已进入。

---

### RHI 把 Renderer 的意图变成跨平台 GPU 命令

当 Pass lambda 消费 mesh draw 配方时，相关工作会被表达进 `FRHICommandList`。到这一步，系统已经不再用 Actor、Component、ViewFamily、BasePass 这些高层语义表达工作，而是进入 GPU 命令层：

```text
设置 graphics / compute pipeline
绑定 shader 参数、uniform buffer、SRV/UAV、sampler
绑定 vertex/index buffer
发 Draw / DrawIndexed / Dispatch
处理资源状态转换
```

RHI 的职责是让 Renderer 面向一套跨平台接口表达与录制命令，再由后端 context 消费这些意图，形成可提交的平台命令。架构上应使用下面的状态链，而不是固定某个 API 的调用栈：

```text
Renderer / RenderCore 录制平台无关意图
  -> RHI 命令被 context / backend 消费或翻译
  -> 平台 command list / encoder 内容形成
  -> 工作提交到平台 queue
  -> GPU 越过对应完成点
```

`FRHICommandList` 是 CPU 侧组织平台无关命令的接口；后端 context 是平台命令形成的边界；platform queue 接管已形成的命令；GPU completion 需要 Fence、完成值、readback 合同或其他与最后消费者匹配的证据。这些状态不能互相代替。

这条边界让 Renderer 仍然控制渲染算法和命令内容，但不直接绑定 D3D12/Vulkan/Metal。平台后端只看到抽象后的 GPU 操作，不需要理解 `FScene`、`FViewInfo`、材质图或 BasePass 的高层意义。调用 RHI draw、结束 CPU 录制或形成平台 command list，都不等于 queue 已提交，更不等于 GPU 已完成。

第 04 篇会深入 RHI 资源、PSO、shader binding、barrier 和后端选择。本篇只需要记住：RHI 是 Renderer 和平台 API 之间的命令/资源抽象边界。

---

## 横切面：逻辑执行域与线程访问契约

本节按**逻辑执行域与所有权上下文**描述责任，不承诺每项工作永远由固定 OS 线程执行。worker 可以承载任务，但必须遵守对应的顺序域和发布边界；物理线程、named-thread 身份与 queue 细节留给第 03 篇。

第 03 篇会完整解释 Render Thread、RHI Thread、Task 和 Fence。本篇只建立理解架构所必需的所有权规则。

### Game Thread：修改游戏世界，发送变化请求

Game Thread 修改 Actor、Component 和大部分 UObject 状态，并把 Renderer 需要的变化封装成可跨执行域传递的请求数据。请求送出之后，不能假设 Render Thread 已经立刻吸收，也不能继续修改已交出的临时载荷。只有 `FScene::Update()` 吸收这些请求并更新 Renderer 侧持久状态时，本章才称它为 **Scene publication**。

### Render Thread：拥有 Renderer 场景与帧编排

`FScene`、SceneInfo、View 工作集和大部分渲染编排在渲染执行上下文中维护。Game Thread 不应绕过同步直接修改这些结构；Render Thread 依赖的是已经稳定交接的数据，而不是任意变化的 UObject 图。

### Worker Task：处理被明确切分的工作

可见性、命令构建和其他阶段可以并行，但运行在 worker 上不等于获得全局写权限。任务只能读取调度者保证稳定的输入，并写入约定的局部输出；任务完成事件是后续消费者可以读取结果的成立条件。

### RHI 与平台执行域：消费已经成形的命令

命令翻译与提交面对的是资源和 GPU 命令，不再是 Component。它们可以异步推进，但前提是命令引用的资源在 GPU 消费完成前仍有效，状态转换与同步关系也已建立。

把四层压成一句话：**跨线程不是把对象指针扔过去，而是交出一份在消费者生命周期内稳定的数据与执行权。**

这解释了三类典型故障：GT 改了 Component 却没走正确 dirty/update 路径，会导致游戏世界正确而 FScene 仍旧；RT 启动任务后过早读结果，会导致持久场景正确而本帧工作集未完成；CPU 已完成命令录制或交接、但尚未获得覆盖最后 GPU consumer 的完成证据时过早复用资源，会导致 Renderer 与命令录制正确而 GPU 消费期被破坏。

---

## 第四次换形的终点：BackBuffer、Present 与完成条件

本章只建立 Present 在架构中的位置，不展开同步实现。

| 完成层级 | Owner / 正向产物 | 下一消费者 | 相关寿命条件 |
|---|---|---|---|
| Renderer 构图/录制完成 | Renderer/RDG/RHI recorder；Pass 工作已描述或写入 CPU 命令载体 | backend context/translator | 捕获数据和资源引用覆盖命令消费期 |
| Platform command formation | backend/context；可提交的平台命令包已形成 | platform queue | native command 与引用资源覆盖提交和 GPU 使用期 |
| Queue Submit 完成 | backend/queue；平台队列已接收对应工作 | GPU queue timeline | 资源仍需保持到最后 GPU consumer 完成 |
| GPU completion 证据成立 | GPU/backend；指定 queue/work 到达匹配 fence 或完成点 | 资源复用、retirement 或输出链 | 证据只覆盖声明的 queue、work 与 consumer 范围 |
| Present/display 阶段成立 | swapchain/display path；图像进入交换链与显示链 | 显示系统 | 输出资源按呈现合同存活，其他资源另按各自消费者判断 |

这条正向链建立后，负向边界才容易判断：Renderer 录制完成不能证明后端或 GPU 已接管；Queue Submit 不能证明 GPU 已越过最后消费者；一个 queue 的 completion 不能外推其他 queue 或显示系统；Present 也不是所有 GPU 资源的通用复用证明。

若捕获中已有正确 draw，但屏幕仍不对，应继续检查输出目标、后处理或拷贝覆盖、BackBuffer 与 Present，而不是退回 Component 注册。

---

## 性能视角：时间花在哪里，要按责任域解释

一帧慢不能只看 `Render()` 总时间。架构分层也提供了性能归因框架：

| 现象 | 优先怀疑的责任域 | 第一批证据 |
|---|---|---|
| Game Thread 长而 Render/GPU 空闲 | 世界更新或请求生产 | CPU profiler、Viewport 调用频率 |
| Render Thread 长而 GPU 未满 | Scene 更新、visibility、draw preparation、图构建 | RenderThread scopes、任务等待、command build 统计 |
| GPU 长而 CPU 提前完成 | Pass 的像素、几何或带宽成本 | GPU profiler、Pass timing、分辨率与 overdraw |
| RT 等待 RHI/Queue | 命令翻译、提交或同步压力 | RHI scopes、submit 与 fence 等待 |
| 多 View 成倍增长 | View-dependent 工作重复 | View 数量、visibility 与 pass 按 View 成本 |
| Scene 更新尖峰 | 大量结构变化或缓存失效 | Primitive add/remove、render-state dirty、缓存重建 |

这张表用于决定先采集哪一层证据，不是让人凭症状直接定罪。“GPU 100%”不能证明 BasePass 有问题，必须看具体 Pass 与瓶颈类型；对象很多但 GPU 很空，也可能卡在 Render Thread 准备工作，而不是 GPU draw。

Renderer 把场景缓存、本帧筛选、RDG 编译、RHI recording、平台命令形成和 Platform Queue Submit 分开，也是在不同变化频率和处理器之间分摊成本：跨帧不变的尽量缓存，View-dependent 的每帧计算，资源生命周期交给图推导，平台 queue handoff 与高层准备解耦。

---

## 架构级调试：寻找最后一个成立的数据状态

四次换形模型可以把分散的调试入口统一起来：不要从坏像素逆向猜所有系统，而要找到最后一个被证据证明成立的状态。

| 已确认成立 | 尚未确认 | 下一步观察 |
|---|---|---|
| Viewport Client 被调用 | ViewFamily 是否正确 | Scene、Views、RenderTarget、ShowFlags |
| ViewFamily 进入 Renderer | 帧工作区是否建立 | renderer 类型、FViewInfo、初始化状态 |
| Primitive 已在 FScene | 当前 View 是否选择它 | bounds、visibility、occlusion、show flags |
| Primitive 对 View 可见 | 是否进入目标 mesh pass | relevance、static/dynamic input、command selection |
| 目标 Pass 有 visible draw | Pass 是否执行 | RDG dependency、culling、resource consumer |
| RHI command 已录制 | 平台命令是否形成并交给 queue | backend translation/encoding、Platform Queue Submit、resource state |
| Platform Queue Submit 已发生 | GPU 是否越过目标 producer/consumer | queue timeline、capture、fence 与资源内容 |
| GPU 输出纹理正确 | 显示链是否正确 | copy/composite、backbuffer、present |

证据必须匹配数据层。看见 bounds 不证明 BasePass draw 存在；捕获里看见 Pass 名，也不证明目标 Primitive 的命令进入该 Pass。排查还必须尊重时间：GT 刚发送变化请求时，RT 可能仍消费旧 Scene；CPU 完成 RHI recording 或 backend 形成平台命令时，Platform Queue Submit 可能尚未发生；queue 已接收工作时，GPU 也可能尚未消费。比较前先确认逻辑帧或资源版本一致。

统一原则是：**找到最后成立的状态，再检查下一次所有权或数据形态转换。** 函数断点、RenderDoc、Unreal Insights、GPU profiler 与可视化命令，只是观察这些状态的不同工具。

---

## 红色金属球的四阶段状态账本

前文已经按四次换形建立主线。这里把同一案例压成状态账本，检查每一步的权威数据、控制者、成立证据和证据边界，而不是再建立第二条函数流程。

### 1. 请求成形：先说明这一帧要看什么

`UGameViewportClient::Draw` 构建 `FSceneViewFamily`，把 Camera、Viewport、ShowFlags、Scene 和输出目标组合成一帧请求，再通过 `IRendererModule::BeginRenderingViewFamily` 交给 Renderer。此时金属球还不是请求的一部分；请求只定义“用哪些 View 查询哪个 Scene”。

这一阶段成立的条件，是 ViewFamily 指向金属球所属的 `FScene`，并带着有效 View 与输出目标。若这里配错，后面即使场景数据库里完整保存着金属球，本帧也会查询错误的世界或把结果写到错误目标。

- 当前权威数据：ViewFamily/View 请求。
- 当前控制者：Engine 到 Renderer 的接口交接。
- 下一状态证据：Renderer 已为正确 Scene 和 View 建立帧工作区。
- 此刻不能推断：金属球已经进入 Scene、已经可见或已经有 draw。

### 2. 场景成形：把游戏对象变成 Renderer 可长期查询的记录

金属球最初是 Game Thread 上的 `UStaticMeshComponent`。加入场景时，它通过 `CreateSceneProxy()` 生成 Renderer 可消费的 `FPrimitiveSceneProxy`，并由 `FPrimitiveSceneInfo` 承担注册与场景索引关系。`AddPrimitiveCommand` 把这次变化送到渲染线程上下文，SceneInfo 先进入待处理更新；到 `FSceneRenderer::OnRenderBegin` 触发 `FScene::Update` 后，这次变化才被吸收进 `FScene` 的持久结构。

这一阶段发生了真正的所有权边界转换：Game Thread 继续拥有并修改 Component；Renderer 不回头读取这个可变 UObject，而是维护自己的 Proxy / SceneInfo / `FScene` 状态。成立条件是 Proxy 有效、更新命令被执行、Scene 更新被吸收。任何一个条件失败，金属球都不会成为 visibility 可以查询的 Primitive。

- 当前权威数据：`FScene` 中的 Proxy/SceneInfo 与登记关系。
- 当前控制者：Renderer 场景更新。
- 下一状态证据：当前 View 可用稳定索引查询到该 primitive。
- 此刻不能推断：当前 View 会选择它，或它已经进入某个 pass。

### 3. 工作集成形：持久存在不等于这一帧要画

Renderer 根据 ViewFamily 创建本帧 `FSceneRenderer`，并把 `FSceneView` 扩展成 `FViewInfo`。随后可见性阶段用当前 View 查询 `FScene`，把跨帧场景压成本帧工作集，结果落在 `PrimitiveVisibilityMap`、`StaticMeshVisibilityMap` 以及相关动态 mesh 收集中。

这一步的关键边界是：`FScene` 中“有金属球”只证明它可被查询，不证明当前 View 要提交它。视锥、遮挡、ShowFlags、pass relevance 等条件会继续缩小工作集。如果金属球在 Scene 中却没有进入 visibility，问题属于“当前 View 如何选择场景”，还没有进入 RDG 或 RHI。

- 当前权威数据：当前 View 的 visibility/relevance 与本帧候选集合。
- 当前控制者：Renderer 的 View-dependent 筛选。
- 下一状态证据：金属球已形成目标 pass 可消费的 visible draw。
- 此刻不能推断：RHI 命令已经录制、queue 已提交或 GPU 已完成。

### 4. 命令成形：把选中的工作变成 GPU 可执行内容

`Render()` 按条件化依赖骨架推进。目标 mesh pass 通过 RDG 声明 Pass、资源读写和执行关系；金属球对应的可见 mesh 则通过 MeshDrawCommand 表达 pipeline state、shader binding、buffer 与 draw 参数。Pass 执行时，这些 draw 被录入 `FRHICommandList`，再由平台后端形成并提交当前图形 API 的原生命令。

这一阶段有两个独立条件：目标 Pass 必须在当前路径中成立并执行；金属球的 draw 必须进入这个 Pass。前者属于 Pass/资源组织，后者属于 visibility、pass relevance 与 draw 组织。两者都成立后，才轮到 RHI 命令录制、平台命令形成与 queue submit。

- 当前权威数据：Pass 资源合同与金属球的 visible draw。
- 当前控制者：Renderer 执行计划，随后交给 RHI/平台后端。
- 下一状态证据：捕获中存在对应 draw，并能继续证明 submit、GPU completion 与显示链状态。
- 此刻不能推断：只凭 record 或 submit 就声称 GPU 已完成，也不能用 Present 证明全部资源可复用。

---

## Worked Case：红色金属球为什么突然消失

假设场景原本正常，修改金属球后它从主相机画面中消失。不要从“材质坏了”开始猜，而要逐层收窄断点。

### 1. Scene 证据：所有 View 都看不到，Scene 中也没有它

这说明失败发生在**场景成形**之前或之中。先确认 `CreateSceneProxy()` 是否返回有效 Proxy，再确认 SceneInfo 是否进入待更新队列，最后确认 `FScene::Update` 是否已经吸收更新。若对象根本不在 `FScene`，visibility、BasePass、RDG 和 RHI 都不是第一现场。

### 2. View 证据：对象在 `FScene`，另一个 View 能看到，主 View 看不到

这把问题收窄到**请求成形或工作集成形**。检查主 ViewFamily 是否查询正确 Scene、View 参数是否正确，以及目标 Primitive 在主 View 的 visibility maps 中是否可见。因为另一个 View 已经证明场景注册和基础 draw 数据存在，不应回退到 Component 生命周期重新排查。

### 3. Pass/Draw 证据：visibility 已成立，但目标 Pass 没有这条 draw

这说明持久场景和 View 选择都已通过，问题位于**mesh pass 选择 / MeshDrawCommand**。检查 pass relevance、静态缓存命令或动态 mesh 收集是否为金属球生成了可提交 draw。此时“目标 Pass 的资源有没有创建”不是首要问题，因为缺的是对象级命令，而不是 Pass 级资源。

### 4. RHI 证据：目标 Pass 有 draw，平台捕获里却没有对应命令

问题已越过场景、可见性和 MeshDrawCommand，进入**命令录制 / 平台形成 / 提交边界**。检查 Pass 是否实际执行、命令是否录入 `FRHICommandList`、平台 command list 是否形成，以及 queue 是否接管。只有在这些条件也成立后，才继续查资源绑定、shader 或平台 API 层。

### 5. Completion/Output 证据：平台命令存在，但画面仍然错误

如果平台命令已经形成，继续区分 queue submit、GPU completion 与 Present。上传或临时资源可能在最后消费者完成前被复用，输出资源也可能在后处理、拷贝或呈现链中被覆盖。屏幕没有正确图像不能反推上游 Scene 一定错误；应找出哪一级完成证据最后成立。

这个 worked case 的价值不是给出一串固定函数，而是建立“证据会排除哪些层”的方法：

```text
不在 FScene
  -> 排除 visibility / RDG / RHI，回查场景成形

在 FScene，但当前 View 不可见
  -> 排除对象注册，回查请求与工作集

当前 View 可见，但无 MeshDrawCommand
  -> 排除场景与 visibility，回查 mesh pass 选择

Pass 有 draw，但无平台命令
  -> 排除 Renderer 上游，回查 record / platform formation

平台命令存在，但输出错误
  -> 回查 queue submit / GPU completion / output / Present
```

函数名只是断点路标；真正决定排查顺序的是数据状态、所有权边界和成立条件。

---
## 章节出口：三条轴共同定位问题

| 换形 | 成立后的数据 | 主要责任域 | 最小证据 |
|---|---|---|---|
| 请求成形 | 正确 Scene、View、ShowFlags 与输出目标 | Engine → Renderer | 帧工作区已建立 |
| 场景成形 | Proxy/SceneInfo/FScene 稳定记录 | Engine 发送变化请求；`FScene::Update()` 完成 Scene publication | primitive 已进入 Renderer 持久场景结构 |
| 工作集成形 | visibility、relevance 与可提交候选 | Renderer | 目标对象进入目标 pass 的 visible work |
| 命令成形 | Pass/资源组织、mesh draw、RHI 工作、平台命令与 queue payload | Renderer → RHI recording → backend formation → Platform Queue Submit | draw 被录制、形成平台命令、交给 queue，并取得覆盖目标 GPU consumer 的完成证据 |

四次换形是过程轴，五层模块是责任轴，世界/场景/帧是寿命轴。三条轴共同回答“数据变成什么、谁有权改变它、它能活多久”。

---

## 下一篇如何接上

读完 01 后，下一步先看第 02 篇。01 只把 `Component -> Proxy -> SceneInfo -> FScene` 作为主路径上的桥说明清楚；02 会把这个桥落到一个具体 Component 的完整生命周期：Proxy 具体持有什么、Transform 更新如何跨线程、`FPrimitiveSceneInfo` 如何注册/移除、销毁为什么需要延迟清理。

然后再进入第 03 篇，渲染命令、Render Thread、RHI Thread、Frame sync、Fence 和 Flush 才会有具体对象可挂靠。换句话说，01 解决“整条路怎么走”，02 解决“对象怎么进入这条路”，03 解决“这条路跨线程时如何安全运行”。
