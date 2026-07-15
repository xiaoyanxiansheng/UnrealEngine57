# UE5.7 底层渲染完全指南

**版本**: UE5.7  
**源码目录**: `D:\Unreal\EnrealEngine\UnrealEngine`  
**读者背景**: Unity 技术美术，有渲染基础，C++ 不熟  
**目标**: 完整理解 UE5.7 底层渲染的流程、架构、算法设计

---

## 核心原则

- 所有结论必须基于源码分析，正文优先标注 `文件路径 + 函数/类型/宏名`，行号只作为辅助定位
- 不讲基础渲染理论（延迟渲染、GBuffer 概念等读者已掌握）
- C++ 知识随文讲解（用法和设计意图），不深入语言底层（汇编、内存序、模板元编程）
- Unity 对比融入正文，建立概念映射
- 篇幅服从教学边界，以讲透本篇边界内的问题为准，不为字数注水
- 不需要速查卡、不需要冗长实操教程
- 重点：流程、架构设计、算法原理

---

## 推荐阅读顺序（Unity 渲染背景）

文件编号已经按“有 Unity 渲染基础，但不熟 UE”的推荐主线调整：

```text
01_Architecture
  -> 02_SceneProxy
  -> 03_ThreadModel
  -> 04_RHI
  -> 05_RenderGraph
  -> 06_GPUScene
  -> 07_MeshDrawCommand
  -> 08-15 一帧渲染流程
```

原因是：`01_Architecture` 给出全局分层后，`02_SceneProxy` 最快把 Unity 读者熟悉的“场景对象 / Renderer / 材质 / Mesh”落到 UE 的 `UPrimitiveComponent -> FPrimitiveSceneProxy -> FPrimitiveSceneInfo -> FScene` 数据流上。先建立这个具体桥梁，再读 `03_ThreadModel`、`04_RHI`、`05_RenderGraph`，线程、命令、资源和 RDG 的抽象会更容易挂到真实对象生命周期上。

---

## 文档列表

**状态口径**：

- `✅ 完成`：已经通过 Codex 生产、Claude 教学优化、Codex 最终事实回归三步，并已沉淀必要结论。
- `🟡 Codex 初稿完成 / Claude 教学优化未完成（历史状态）`：Codex 已完成 Gate 1 技术初稿和事实校准，但尚未经过 Claude 教学优化和 Codex 最终事实回归，当时不能按完成处理；当前状态以章节头和下表为准。
- `🟡 Codex 重构完成 / Claude 教学优化未完成（历史状态）`：Codex 已按新教学标准重构正文，但尚未经过 Claude 教学优化和 Codex 最终事实回归，当时不能按完成处理；当前状态以章节头和下表为准。
- `🟡 存量待回归`：旧流程已有正文，内容可继续参考，但尚未补齐 Claude 教学优化报告和 Codex 最终事实回归，当时不能按新标准视为完成；当前状态以章节头和下表为准。
- `🟡 Claude 教学优化完成 / 待 Codex 最终事实回归`：Codex 已完成 Gate 1 重构，Claude 已完成教学优化并刷新 Teaching Edit Report（三步门禁的第二步已过），但尚未经过 Codex 最终事实回归，仍不能按完成处理。
- `-`：未开始。

> 2026-06-25：`01_Architecture.md` 至 `15_PostProcessing.md` 已通过 Claude 教学优化与 Codex 最终事实回归，状态统一标记为完成；各章 Teaching Edit Report 与 CoverageMatrix 已同步到当前正文。其他章节状态以表格为准。
> 2026-06-25：`16_Nanite.md` 至 `24_Optimization.md` 已通过 Claude 教学优化与 Codex 最终事实回归，状态统一标记为完成；各章 Teaching Edit Report 与 CoverageMatrix 已同步到当前正文。
> 2026-06-28：`03_ThreadModel.md` 已按“新概念先解释、是什么和为什么讲透、补案例辅助说明”的新教学标准达到质量完成、复审完成，并同步为正式完成。

### Part 1 — 架构、对象桥接与基础设施

建立全局认知，先把 Unity 读者熟悉的场景对象落到 UE 的 Component / Proxy / SceneInfo / FScene 数据流，再深入线程、RHI 和 RDG。

| # | 文件名 | 主题 | 核心问题 | 状态 |
|---|--------|------|----------|------|
| 01 | `01_Architecture.md` | 渲染架构总览 | UE 渲染为什么分五层？每层做什么？完整一帧从 Tick 到 GPU 的执行路径 | ✅ 完成 |
| 02 | `02_SceneProxy.md` | 从 Component 到 SceneProxy | Proxy 的虚函数体系（GetDynamicMeshElements/DrawStaticElements）？各 Proxy 子类的数据差异？Transform 更新的完整生命周期？Proxy 销毁的延迟清理机制？（01 已讲 Proxy 概念和创建时序） | ✅ 完成 |
| 03 | `03_ThreadModel.md` | 三线程模型与渲染命令 | Render Thread 事件循环实现？帧同步（FFrameEndSync）？RHI Thread 独立性？并行命令录制？Task System 与渲染交互？（01/02 已讲对象流和 ENQUEUE 语义，本篇深入线程生命周期和同步机制） | ✅ 完成 |
| 04 | `04_RHI.md` | RHI 抽象层 | 资源管理（FRHITexture/Buffer 生命周期）？PSO 创建与缓存？多后端选择机制？平台差异处理？Shader Binding 模型？（01/03 已讲命令链表和虚分派，本篇深入资源侧和 PSO） | ✅ 完成 |
| 05 | `05_RenderGraph.md` | RenderGraph | 参数声明宏的元编程？AsyncCompute 队列？Transient Aliasing 算法细节？Extract/External 语义？RDG 调试？（01/04 已讲五步概览和 RHI 原语，本篇深入每步的实现细节） | ✅ 完成 |

**Part 1 二版修正方向**：

- `01_Architecture.md`：2026-06-25 已通过最终事实回归；主线为“视图请求流 + 对象数据流 -> 可见工作集 -> RDG/MeshDrawCommand -> RHI/后端”，报告与矩阵已同步。
- `02_SceneProxy.md`：2026-06-25 已通过最终事实回归；对象桥接生命周期、Proxy/SceneInfo 分层、Transform dirty、RenderState dirty 与延迟删除均已回归，报告与矩阵已同步。
- `03_ThreadModel.md`：2026-06-28 已按新教学质量标准达到质量完成、复审完成，并同步为正式完成；当前正文针对 task、queue、pipe、command list、submit、fence 等新概念补入首用解释与案例。
- `04_RHI.md`：2026-06-25 已通过最终事实回归；DynamicRHI、资源生命周期、PSO、shader binding、resource transition 与 command list 回放均已回归，报告与矩阵已同步。
- `05_RenderGraph.md`：2026-06-25 已通过最终事实回归；RDG 声明、编译、culling、transient alias、barrier、external/extract、parallel execute 与调试开关均已回归，报告与矩阵已同步。

### Part 2 — GPU 场景数据与 Draw 数据流

在 02 已经讲清 Component 如何进入 Renderer 之后，继续理解 Renderer 侧场景数据如何进入 GPUScene、MeshDrawCommand 和后续 DrawCall 路径。

| # | 文件名 | 主题 | 核心问题 | 状态 |
|---|--------|------|----------|------|
| 06 | `06_GPUScene.md` | GPUScene 与 GPU-Driven 基础 | InstanceSceneData 的 GPU 端 layout？Large World 坐标精度处理？与 Instance Culling 的交互？PrimitiveID→MaterialID 查找？动态实例的流式上传？（01 已讲 GPUScene 概念和增量上传） | ✅ 完成 |
| 07 | `07_MeshDrawCommand.md` | MeshDrawCommand 与 MeshPassProcessor | MeshPassProcessor 子类系统（各 Pass 的决策逻辑）？静态/动态 MDC 的详细区别？PSO 创建时机和缓存？排序键编码位域？合批条件和 Instance Culling 整合？（01 已讲缓存→提交的完整路径） | ✅ 完成 |

**Part 2 二版修正方向**：

- `06_GPUScene.md`：2026-06-24 已按 Gate 1 标准重构为“Renderer 持久场景如何变成 GPU 可寻址数据库”主线，源码核对记录集中到 `06_GPUScene_CoverageMatrix.md`；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `07_MeshDrawCommand.md`：2026-06-24 已按 Gate 1 标准重构为 `FMeshBatch -> FMeshDrawCommand -> FVisibleMeshDrawCommand -> Instance Culling -> RHI submit` 主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。

### Part 3 — 一帧的完整渲染流程

跟着 `FDeferredShadingSceneRenderer::Render` 的执行顺序，理解每个阶段做什么。

| # | 文件名 | 主题 | 核心问题 | 状态 |
|---|--------|------|----------|------|
| 08 | `08_FrameInit.md` | 帧初始化与可见性 | OnRenderBegin 的完整子系统启动顺序？ComputeViewVisibility 的八叉树遍历实现？FrustumCull vs OcclusionCull 的判定逻辑？动态 MeshElement 收集的异步任务调度？（01 已讲概览和 PrimitiveVisibilityMap） | ✅ 完成 |
| 09 | `09_DepthPrepass.md` | 深度预 Pass 与遮挡 | EDepthDrawingMode 各模式的选择策略？Nanite 的 Two-Pass Culling 实现？HZB 构建的下采样算法？HZB 在各系统中的消费方式？传统 Mesh PrePass 和 Nanite 如何共享同一 Depth Buffer？ | ✅ 完成 |
| 10 | `10_BasePass.md` | BasePass 与 GBuffer | 材质节点图到 GBuffer 写入的完整 Shader 路径？各 ShadingModel 的 GBuffer 编码差异？Nanite Material Binning 的 GPU 实现？DBuffer Decal 如何混入 BasePass？（01 已讲 BasePass 写入 GBuffer 的位置和消费关系） | ✅ 完成 |
| 11 | `11_Shadows.md` | 阴影系统 | CSM 的 Cascade 分割算法？VSM 的虚拟页表和 Page Marking？MegaLights 随机阴影采样？ContactShadow 的屏幕空间射线？各阴影方案的选型条件？ | ✅ 完成 |
| 12 | `12_Lighting.md` | 光照计算 | 延迟光照 Pass 的逐光源循环实现？Clustered Light Grid 的 3D 分配算法？光源形状 Mesh 的绘制和模板优化？BRDF 按 ShadingModelID 分支的 Shader 结构？ | ✅ 完成 |
| 13 | `13_Atmosphere.md` | 体积效果与大气 | Volumetric Fog 的 3D 纹理步进？体积云的 Ray Marching？大气散射的 LUT 预计算？Height Fog 的解析近似？各体积效果之间的合成顺序？ | ✅ 完成 |
| 14 | `14_Translucency.md` | 半透明 | 半透明排序算法（per-object vs per-pixel）？Translucency Pass 的队列和材质限制？OIT 的实现方案？Translucency Lighting Volume？Distortion 和 FrontLayer Translucency 如何接入主场景颜色？ | ✅ 完成 |
| 15 | `15_PostProcessing.md` | 后处理链 | 后处理 Pass 链如何从 SceneColor 走到 BackBuffer？TSR 的输入/输出和时间积累位置？Bloom 的多级模糊如何组织？ToneMapping 的 ACES 曲线在哪里执行？Debug/Visualize Pass 如何插入？ | ✅ 完成 |

**Part 3 边界说明**：

Part 3 跟着 `FDeferredShadingSceneRenderer::Render` 的执行顺序走，讲清每个阶段在一帧里的接入点、输入输出、资源流向和调度位置。Nanite/Lumen/VSM/MegaLights 会在 Part 3 中出现，但只讲它们如何接入当前 Pass；算法细节、数据结构和核心设计留给 Part 4。材质/Shader 也只讲到足以理解 BasePass 和参数绑定，完整编译管线留给 Part 5。

**Part 3 二版修正方向（08-15）**：

- `08_FrameInit.md`：2026-06-24 已按 Gate 1 标准重构为 OnRenderBegin、BeginInitViews、FinishGatherDynamicMeshElements、EndInitViews 三道同步边界主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `09_DepthPrepass.md`：2026-06-24 已按 Gate 1 标准重构为 `SceneDepth.Target -> Resolve -> HZB` 的深度合约主线，区分传统 mesh、velocity、Nanite、PartialDepth 和 HZB history；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `10_BasePass.md`：2026-06-24 已按 Gate 1 标准重构为 SceneDepth、GPUScene、MeshDrawCommand、DBuffer 输入合约到 SceneColor/GBuffer 输出合约的主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `11_Shadows.md`：2026-06-24 已按 Gate 1 标准重构为 Lighting 前的阴影证据所有权主线，串起 CSM、ShadowDepth、VSM Page Marking、MegaLights shadow samples 和 Contact Shadow；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `12_Lighting.md`：2026-06-24 已按 Gate 1 标准重构为 BasePass/GBuffer 后光源数据如何经排序、light grid、standard deferred 和 BRDF 分支累加回 SceneColor 的主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `13_Atmosphere.md`：2026-06-24 已按 Gate 1 标准重构为已光照 SceneColor 如何接入 Sky Atmosphere、Volumetric Fog、Height Fog、Local Fog Volume 和 Volumetric Cloud 的单帧资源流主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `14_Translucency.md`：2026-06-24 已按 Gate 1 标准重构为 lighting/atmosphere 后半透明如何通过 pass 队列、ResourceMap、OIT、TLV、Distortion、FrontLayer/RT 接回 SceneColor 的主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。
- `15_PostProcessing.md`：2026-06-24 已按 Gate 1 标准重构为 resolved HDR SceneColor 如何通过 TSR、Bloom、ToneMapping、Debug/Visualize 和 ViewFamilyTexture 输出收束 Part 3 的主线；2026-06-25 已通过最终事实回归，报告与矩阵已同步。

### Part 4 — UE5 核心技术深入

理解 Nanite/Lumen/MegaLights/VSM 的架构设计和算法。

| # | 文件名 | 主题 | 核心问题 | 状态 |
|---|--------|------|----------|------|
| 16 | `16_Nanite.md` | Nanite 虚拟几何体 | 如何实现像素级 LOD？GPU 管线五阶段？ | ✅ 完成 |
| 17 | `17_Lumen.md` | Lumen 动态全局光照 | Surface Cache + RT + Screen Probe 如何协作？ | ✅ 完成 |
| 18 | `18_MegaLights.md` | MegaLights 随机多光源 | 如何用常数开销处理大量光源？ | ✅ 完成 |
| 19 | `19_VirtualShadowMaps.md` | Virtual Shadow Maps | 虚拟页表如何解决阴影分辨率问题？ | ✅ 完成 |

**Part 4 边界说明**：

Part 4 不再按一帧顺序讲"什么时候调用"，而是讲核心技术自己的数据结构、算法管线、缓存/流送/调度策略。Part 3 已讲过的接入点只用 1-2 句话唤醒，不重复展开。

**Part 4 二版修正方向**：

- `16_Nanite.md` 至 `19_VirtualShadowMaps.md`：2026-06-25 已通过最终事实回归；Nanite、Lumen、MegaLights 与 VSM 的教学主线、报告与矩阵已同步。

### Part 5 — 材质与着色器系统

从 Material Editor 到 GPU Shader 的完整路径。

| # | 文件名 | 主题 | 核心问题 | 状态 |
|---|--------|------|----------|------|
| 20 | `20_MaterialPipeline.md` | 材质编译管线 | 节点图如何变成 HLSL？Permutation 如何管理？ | ✅ 完成 |
| 21 | `21_ShaderSystem.md` | VertexFactory 与 Shader 绑定 | Shader 如何声明、注册、绑定参数？ | ✅ 完成 |
| 22 | `22_ComputeShader.md` | Compute Shader 与 GPU 通用计算 | Dispatch 模型？在 UE 中如何编写和调度？ | ✅ 完成 |

**Part 5 边界说明**：

`10_BasePass.md` 会先建立足够理解 GBuffer 写入的材质/Shader 最小模型；Part 5 再完整展开 Material Editor 到 HLSL、Permutation、VertexFactory、Shader 参数绑定和 Compute Shader 开发模型。不要在 BasePass 篇提前写完整材质编译系统。

**Part 5 二版修正方向**：

- `20_MaterialPipeline.md` 至 `22_ComputeShader.md`：2026-06-25 已通过最终事实回归；材质、Shader 注册/绑定与 Compute Dispatch 分层主线已同步报告与矩阵。

### Part 6 — 调试与性能

具备定位问题和优化的能力。

| # | 文件名 | 主题 | 核心问题 | 状态 |
|---|--------|------|----------|------|
| 23 | `23_Debugging.md` | 调试工具与方法 | RenderDoc/Insights/CVar 如何使用？ | ✅ 完成 |
| 24 | `24_Optimization.md` | 性能分析与优化 | 如何定位瓶颈？优化方向有哪些？ | ✅ 完成 |

**Part 6 二版修正方向**：

- `23_Debugging.md` 与 `24_Optimization.md`：2026-06-25 已通过最终事实回归；调试分层、性能闭环、报告与矩阵已同步。

---

## 依赖关系

```
01 架构总览
    └──→ 02 SceneProxy（Unity 对象心智模型落到 UE 数据流）
            └──→ 03 线程模型 ──→ 04 RHI ──→ 05 RenderGraph
                    └──→ 06 GPUScene ──→ 07 MeshDrawCommand ──→ Part 3 (08-15) 一帧流程
                                                               │
                                                               ├──→ Part 4 (16-19) 核心技术
                                                               │
                                                               └──→ Part 5 (20-22) 材质系统
                                                                          │
                                                                          v
                                                                  Part 6 (23-24) 调试优化
```

推荐主干线按 `01 -> 02 -> 03 -> 04 -> 05 -> 06 -> 07 -> 08-15` 阅读。Part 4/5/6 可以在 Part 3 完成后并行阅读。

---

## 关键源码路径索引

| 系统 | 主文件 | 位置 |
|------|--------|------|
| 引擎主循环 | LaunchEngineLoop.cpp | Engine/Source/Runtime/Launch/Private/ |
| 延迟渲染器 | DeferredShadingRenderer.cpp | Engine/Source/Runtime/Renderer/Private/ |
| 场景管理 | SceneRendering.cpp, ScenePrivate.h | Engine/Source/Runtime/Renderer/Private/ |
| 可见性 | SceneVisibility.cpp | Engine/Source/Runtime/Renderer/Private/ |
| RHI 命令 | RHICommandList.h | Engine/Source/Runtime/RHI/Public/ |
| RenderGraph | RenderGraphBuilder.h | Engine/Source/Runtime/RenderCore/Public/ |
| 渲染线程 | RenderingThread.h | Engine/Source/Runtime/RenderCore/Public/ |
| GPUScene | GPUScene.cpp/.h | Engine/Source/Runtime/Renderer/Private/ |
| MeshDrawCommand | MeshDrawCommands.cpp, MeshPassProcessor.h | Engine/Source/Runtime/Renderer/ |
| BasePass | BasePassRendering.cpp/.h | Engine/Source/Runtime/Renderer/Private/ |
| 光照 | LightRendering.cpp | Engine/Source/Runtime/Renderer/Private/ |
| 阴影 | ShadowRendering.cpp, VirtualShadowMaps/ | Engine/Source/Runtime/Renderer/Private/ |
| Nanite | Nanite/ | Engine/Source/Runtime/Renderer/Private/ |
| Lumen | Lumen/ | Engine/Source/Runtime/Renderer/Private/ |
| MegaLights | MegaLights/ | Engine/Source/Runtime/Renderer/Private/ |
| 后处理 | PostProcess/ | Engine/Source/Runtime/Renderer/Private/ |
| 材质系统 | MaterialShader.h, ShaderMaterial.cpp | Engine/Source/Runtime/Engine/ |
| Shader 系统 | Shader.h, GlobalShader.h | Engine/Source/Runtime/RenderCore/Public/ |
| Shaders (HLSL) | Engine/Shaders/Private/, Engine/Shaders/Shared/ | Engine/Shaders/ |

---

## 维护

- 每篇文档完成后更新状态列
- 如生成过程中发现大纲需要调整（拆分/合并/新增），直接修改本文件

---

## 篇幅与边界指引（生成后续文档前必读）

GENERATION_GUIDE 已经明确：篇幅服从教学边界，**不是要凑的字数指标**。真正的标准是最高原则那句："如果一段文字删掉不影响读者理解主线，它就不该存在。" 篇幅是讲透的**结果**，不是**目标**。判断一篇该多长，先判断它的**教学边界**——哪些属于它、哪些属于别的篇。

### 已确立的边界判例（04_RHI 生成时定下，后续沿用）

04 篇的篇幅明显短于 01/02/03 这类全景型章节，但这是**正确的深度**，不是没写够。原因是 RHI 这个主题的教学边界天然窄于 01/02/03，它的相邻内容都各有专篇归属，不该在 04 里展开：

| 内容 | 归属 | 04 的处理 |
|------|------|----------|
| 两阶段资源创建（Initializer/Finalize） | **04 本职**，无处可推 | 完整展开（是什么/为什么/兜底安全） |
| RDG 如何编排资源状态、Transient Aliasing | 05 RenderGraph | 04 给原语（ERHIAccess/Transition），明确"05 展开" |
| PSO 配方从哪来（材质+顶点工厂收集、预缓存触发） | 07 MeshDrawCommand / 20 MaterialPipeline / 21 ShaderSystem | 04 讲缓存机制本身，配方来源标注出处 |
| D3D12 后端内部（根签名二进制布局、描述符堆实现） | 无专篇，且对 TA 读者非必需 | 点到为止（讲清抽象模型即可，不挖后端实现） |
| Shader 反射、参数绑定细节、VertexFactory | 21 ShaderSystem（并与 20 MaterialPipeline 衔接） | 04 讲 SHADER_PARAMETER→绑定的链路，细节留 20/21 |

**从中提炼的通用规则**，写后续每一篇前先过一遍：

1. **先划边界再动笔**：列出这篇会碰到的所有概念，逐个判断"它的深入展开属于本篇还是后续篇"。属于后续篇的，只讲"它在本篇流程里扮演什么角色"+ 标注出处（符合 GUIDE 第 52 行的唯一例外）。
2. **不为篇幅越界**：把后续篇的内容提前灌进来凑行数，会造成重复、且读者前置不足看不懂——直接违背"循序渐进"。宁可让一篇诚实地短，也不要越界注水。
3. **不挖对读者无用的深度**：读者是 Unity 技术美术。要讲透的是"UE 的抽象模型和设计取舍"，不是"D3D12/Vulkan 后端的二进制细节"。后端实现只在"帮助理解抽象为什么这样设计"时才提。
4. **篇幅自检的正确问法**：不是"够不够 2000 行"，而是"这篇主线涉及的、且属于本篇边界内的每个概念，是否都到了'读者能回答 what/how/why'的程度"。若是，则篇幅已达标，无论行数多少。
5. **相邻篇的衔接靠暗线+出处**：像 04 用"资源状态/命令列表层级"作暗线引出 05、用标注出处把 PSO 配方推给 07/20/21——这种"埋钩子"既保持本篇聚焦，又为后续篇做了铺垫。后续篇开头用 1-2 句唤醒记忆即可接上。

> 一句话：**篇幅服从于边界，边界服从于"读者在这一篇该理解到哪"。** 01/02 长是因为它们是全景和并发主干；越往专题走，单篇边界越清晰、越可能合理地短。判断"短了是不是没写透"时，回到这张边界表的思路：是漏了本职内容，还是本就该由后续篇承接？
