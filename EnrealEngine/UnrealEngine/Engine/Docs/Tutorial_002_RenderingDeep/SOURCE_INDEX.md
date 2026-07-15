# 源码索引（共享知识库）

> 本文件沉淀已通过源码验证的事实信息。
> 每次生成新文档后，将新发现的路径、行号、类关系追加到对应章节。
> 
> **源码版本**: UE5.7（固定，不会升级）
> **源码根目录**: `D:\Unreal\EnrealEngine\UnrealEngine`
> **路径约定**: 下文路径均相对于 `Engine/Source/Runtime/`，除非特别标注

---

## 1. 模块架构

### 渲染相关模块（自底向上）

| 模块 | 路径 | 职责 |
|------|------|------|
| RHI | `RHI/` | 硬件抽象接口（D3D12/Vulkan/Metal 统一） |
| RHICore | `RHICore/` | RHI 共享工具（描述符分配、诊断、池分配器） |
| RenderCore | `RenderCore/` | 渲染基础设施（RDG、Shader 管理、渲染线程、VertexFactory） |
| Renderer | `Renderer/` | 场景渲染（管线编排、各 Pass 实现、Nanite/Lumen/VSM） |
| Engine (渲染部分) | `Engine/Private/Rendering/` | 游戏侧资源（Mesh Buffer、Nanite Streaming、纹理资源） |
| D3D12RHI | `D3D12RHI/` | D3D12 平台后端 |
| VulkanRHI | `VulkanRHI/` | Vulkan 平台后端 |

### Shader 文件

| 路径（相对 Engine/） | 内容 |
|---------------------|------|
| `Shaders/Private/` | HLSL 着色器实现（.usf） |
| `Shaders/Public/` | 平台层 Shader 工具（Platform.ush） |
| `Shaders/Shared/` | C++ 和 HLSL 共享定义（.h，双端 include） |

---

## 2. 核心类定位

### 渲染器主循环

| 类/函数 | 文件 | 行号 |
|---------|------|------|
| `FDeferredShadingSceneRenderer::Render` | `Renderer/Private/DeferredShadingRenderer.cpp` | 1672 |
| `FDeferredShadingSceneRenderer` 类声明 | `Renderer/Private/DeferredShadingRenderer.h` | — |
| `FMobileSceneRenderer` | `Renderer/Private/MobileShadingRenderer.cpp` | — |
| `FEngineLoop::Tick` | `Launch/Private/LaunchEngineLoop.cpp` | 5536 |

### 场景与数据流

| 类/函数 | 文件 | 行号 |
|---------|------|------|
| `FScene` | `Renderer/Private/ScenePrivate.h` | 2874 |
| `FScene::AddPrimitive` | `Renderer/Private/RendererScene.cpp` | 1302 |
| `FScene::AddPrimitiveSceneInfo_RenderThread` | `Renderer/Private/RendererScene.cpp` | 1037 |
| `FPrimitiveSceneProxy` | `Engine/Public/PrimitiveSceneProxy.h` | 295 |
| `FPrimitiveSceneInfo` | `Renderer/Public/PrimitiveSceneInfo.h` | 265 |
| `FMeshDrawCommand` | `Renderer/Public/MeshPassProcessor.h` | 1222 |
| `FMeshPassProcessor` | `Renderer/Public/MeshPassProcessor.h` | 2198 |
| `FSceneRenderer::SetupMeshPass` | `Renderer/Private/SceneRendering.cpp` | 4705 |
| `FSceneRenderBuilder::CreateLinkedSceneRenderers` | `Renderer/Private/SceneRenderBuilder.cpp` | 983 |
| `FSceneRenderBuilder::AddRenderer` | `Renderer/Private/SceneRenderBuilder.cpp` | 1027 |
| `FSceneRenderBuilder::Execute` | `Renderer/Private/SceneRenderBuilder.cpp` | 1079 |
| `FSceneRenderBuilder` shading path switch | `Renderer/Private/SceneRenderBuilder.cpp` | 511 |
| `FVisibilityTaskData::SetupMeshPasses` | `Renderer/Private/SceneVisibility.cpp` | 4671 |
| `FDeferredShadingSceneRenderer::BeginInitViews` | `Renderer/Private/SceneVisibility.cpp` | 5852 |
| `FDeferredShadingSceneRenderer::EndInitViews` | `Renderer/Private/SceneVisibility.cpp` | 6000 |

### RHI 层

| 类/函数 | 文件 | 行号 |
|---------|------|------|
| `FRHICommandListBase` | `RHI/Public/RHICommandList.h` | — |
| `FRHIComputeCommandList` (继承 Base) | `RHI/Public/RHICommandList.h` | — |
| `FRHICommandList` (继承 Compute) | `RHI/Public/RHICommandList.h` | — |
| `FRHICommandListImmediate` (继承 CommandList) | `RHI/Public/RHICommandList.h` | — |
| `FRHICommandBase` (命令链表节点) | `RHI/Public/RHICommandList.h` | — |
| `TRHILambdaCommand` | `RHI/Public/RHICommandList.h` | — |

### RenderGraph (RDG)

| 类/函数 | 文件 | 行号 |
|---------|------|------|
| `FRDGBuilder` | `RenderCore/Public/RenderGraphBuilder.h` | — |
| `FRDGBuilder::AddPass` | `RenderCore/Public/RenderGraphBuilder.h` | — |
| `FRDGBuilder::CreateTexture` | `RenderCore/Public/RenderGraphBuilder.h` | — |
| `FRDGBuilder::Execute` | `RenderCore/Public/RenderGraphBuilder.h` | — |

### 渲染线程

| 类/函数 | 文件 | 行号 |
|---------|------|------|
| `ENQUEUE_RENDER_COMMAND` 宏 | `RenderCore/Public/RenderingThread.h` | — |
| `FRenderCommandDispatcher` | `RenderCore/Public/RenderingThread.h` | — |
| `FlushRenderingCommands` | `RenderCore/Public/RenderingThread.h` | — |
| `FFrameEndSync::Sync` | `RenderCore/Public/RenderingThread.h` | — |

### GPUScene

| 类/函数 | 文件 | 行号 |
|---------|------|------|
| `FGPUScene` | `Renderer/Private/GPUScene.h` | — |
| `FGPUScene::UploadDynamicPrimitiveShaderDataForView` | `Renderer/Private/GPUScene.cpp` | — |

---

## 3. Render 函数执行顺序（已验证）

`FDeferredShadingSceneRenderer::Render` (DeferredShadingRenderer.cpp:1672-3963)

```
行号范围    阶段
─────────────────────────────────────────────────────────
1672-1725   Ray Tracing 初始化（geometry build, SBT reset, view 注册）
1726        OnRenderBegin → 返回 InitViewTaskDatas
1728-1747   曝光补偿 LUT、VirtualTexture Begin、GPUScene Scope
1749-1762   CommitFinalPipelineState、SystemTextures 初始化
1764-1771   LightFunctionAtlas 异步更新
1777-1813   Sky Atmosphere 更新、VSM 初始化、Lumen Scene 更新开始、Lumen Lights 收集
1816-1842   Nanite BasePass 可见性查询开始
1851        SceneExtensions PreInitViews
1857-1868   DistanceField 准备、ShadingEnergyConservation 初始化
1870-1897   Ray Tracing 实例收集开始
1900-1928   SVT/Nanite Streaming Begin
1943-1955   Subsurface/Specular/RectLight/IES Atlas 更新
1957-1983   SceneTextures 初始化、Substrate PreInitViews
1985-1989   BeginInitViews（可见性命令）
2083        FinishGatherDynamicMeshElements
2087-2096   FX System PreRender
2098-2120   GPUScene 上传动态 Primitive 数据
2135        InstanceCullingManager BeginDeferredCulling
2200-2260   GatherAndSortLights（异步任务）
2305-2359   RenderPrePass + Nanite 光栅化
2542-2658   HZB/遮挡/光源网格/ForwardLightData
2660-2800   LightFunctionAtlas/VolumetricCloud/DistanceField/Lumen 更新/阴影深度
2800-2874   BasePass 渲染、Nanite 可视化
2876-2973   曝光/GBuffer Normal/Substrate 分类/DBuffer
2975-3078   Lumen 间接光异步/VolumetricCloud 异步/VSM PageMarking/MegaLights/ShadowDepthMaps
3080-3188   RT Scene Sync/CustomDepth/Velocity/CompositionLighting（Decals+SSAO）
3190-3308   延迟光照（DiffuseIndirect+AO/Capsule/Lights/MegaLights/Reflections/SSS）
3309-3346   VolumetricFog/HeterogeneousVolumes/VolumetricCloud
3348-3443   高度雾/LocalFogVolume/Cloud 合成
3445-3467   Single Layer Water
3469-3539   TSR/LightShaft/OpaqueFX/HairStrands
3541-3629   半透明渲染（Lumen FrontLayer/RT Translucency/OIT/Distortion）
3631-3768   调试可视化
3770-3896   SceneResolve/后处理（AddPostProcessingPasses）
3898-3963   VT Feedback/Lumen Feedback/RT Cleanup/OnRenderFinish/资源释放
```

---

## 4. 数据流路径（已验证）

```
UPrimitiveComponent::CreateSceneProxy()         [Game Thread, Engine 模块]
    |
    v
FPrimitiveSceneProxy                            [Game Thread 创建，跨线程移交]
    |
    | FScene::BatchAddPrimitivesInternal() 中 new FPrimitiveSceneInfo
    v
FPrimitiveSceneInfo                             [Game Thread 创建，Renderer 管理状态]
    |
    | AddPrimitiveCommand: SetTransform / CreateRenderThreadResources / EnqueueAdd
    v
FScene::Update()                                [Render Thread 统一提交 PrimitiveUpdates]
    |
    | AddToScene() → AddStaticMeshes() → GetStaticMeshElement()
    v
FMeshBatch / FStaticMeshBatch                   [几何+材质描述]
    |
    | FMeshPassProcessor::AddMeshBatch()（每个 Pass 独立处理）
    v
FMeshDrawCommand                                [缓存在 FScene::CachedDrawLists[EMeshPass]]
    |
    | FParallelMeshDrawCommandPass::DispatchPassSetup()
    | BuildRenderingCommands() → 排序/合批/Instance Culling
    v
FRHICommandList 录制                             [RHI 命令]
    |
    v
GPU 执行
```

---

## 5. Nanite 架构（已验证）

### 关键常量（Engine/Shaders/Shared/NaniteDefinitions.h）

| 常量 | 值 | 含义 |
|------|-----|------|
| NANITE_MAX_CLUSTER_TRIANGLES | 128 (1<<7) | 每 Cluster 最大三角形数 |
| NANITE_MAX_CLUSTER_VERTICES | 256 (1<<8) | 每 Cluster 最大顶点数 |
| NANITE_ROOT_PAGE_GPU_SIZE | 32KB | 根页大小 |
| NANITE_STREAMING_PAGE_GPU_SIZE | 128KB | 流送页大小 |
| NANITE_MAX_GPU_PAGES | 131072 (1<<17) | 最大 GPU 页数 |
| NANITE_MAX_HIERACHY_CHILDREN | 64 (1<<6) | 层次节点最大子节点 |
| NANITE_MAX_VIEWS_PER_CULL_RASTERIZE_PASS | 4096 | 每次 Cull/Raster 最大视图数 |

### 管线阶段

```
Instance Culling → Hierarchical Cluster Culling (2-pass HZB)
    → Rasterization (HW+SW 混合) → Shade Binning (按材质分组)
    → Material Shading → GBuffer / ShadowMap / LumenCard
```

### 关键类（Renderer/Private/Nanite/）

| 类/结构 | 文件 | 职责 |
|---------|------|------|
| `IRenderer` | Nanite.h | 每帧创建，调用 DrawGeometry + ExtractResults |
| `FRasterContext` | NaniteCullRaster.h | 光栅化输出（DepthBuffer, VisBuffer64） |
| `FRasterResults` | NaniteCullRaster.h | 聚合结果（VisibleClustersSWHW, ShadingMask） |
| `FPackedView` / `FPackedViewArray` | NaniteCullRaster.h | 紧凑视图（矩阵, LOD, HZB rect） |
| `FConfiguration` | NaniteCullRaster.h | 配置（bTwoPassOcclusion, 调度模式） |
| `FSharedContext` | Nanite.h | 管线类型（Primary/Shadows/Lumen/HitProxy） |
| `FShadeBinning` | NaniteShading.h | 按材质分 bin 数据 |

### 光栅化调度模式（ERasterScheduling）

- Hardware only（纯硬件光栅化）
- Hardware then Software（大三角形 HW，小三角形 Compute）
- Hardware and Software overlapped（并发）

### 与主管线的集成点

- BasePass: `Nanite::DispatchBasePass()` → 写 GBuffer
- Shadows: `Nanite::EmitShadowMap()` → 写深度到 ShadowMap atlas
- Lumen: `ENaniteMeshPass::LumenCardCapture` → 从 Card 视角光栅化

---

## 6. Lumen 架构（已验证）

### 关键常量（Renderer/Private/Lumen/Lumen.h）

| 常量 | 值 | 含义 |
|------|-----|------|
| PhysicalPageSize | 128 texels | 物理页大小 |
| VirtualPageSize | 127 texels | 虚拟页大小（含 0.5 texel 边框） |
| MinCardResolution | 8 texels | 最小 Card 分辨率 |
| MinResLevel | 3 (2^3) | 最小 Mip 级别 |
| MaxResLevel | 11 (2^11=2048) | 最大 Mip 级别 |
| NumResLevels | 9 | Mip 级别数量 |
| CardTileSize | 8 | Card Tile 尺寸 |

### 核心类

| 类 | 文件 | 职责 |
|-----|------|------|
| `FLumenCard` | LumenSceneData.h:273 | 单张 Card（轴对齐平面投影，含 OBB、MipMap 分配） |
| `FLumenMeshCards` | LumenSceneData.h | Card 组（属于同一 Mesh） |
| `FLumenPrimitiveGroup` | LumenSceneData.h:374 | 原语组（含 MeshCardsIndex、标志位） |
| `FLumenCardScene` (GPU 侧) | LumenSceneData.h:44 | Atlas 管理（Albedo/Normal/Emissive/Direct/Indirect/Final） |
| `FLumenSurfaceCacheAllocator` | LumenSceneData.h:520 | 物理页 bin 分配器（支持子页分配） |
| `FLumenPageTableEntry` | LumenSceneData.h:449 | 虚拟→物理页映射 |
| `FLumenSceneData` | LumenSceneData.h:1001+ | 场景数据管理（Add/Update/Remove MeshCards） |

### 光线追踪后端

**软件 RT (Lumen.h:87-97):**
- `UseMeshSDFTracing()` — 逐 Mesh SDF
- `UseGlobalSDFTracing()` — 合并全局 SDF
- `UseHeightfieldTracing()` — 地形高度场

**硬件 RT (Lumen.h:99-119):**
- `UseHardwareRayTracing()` — 主开关
- 逐特性开关: ScreenProbeGather / RadianceCache / Radiosity / Reflections / DirectLighting / ShortRangeAO
- `UseHardwareInlineRayTracing()` — inline (compute) vs RayGen 路径
- `UseReSTIRGather()` — Reservoir 采样

**追踪排列 (ETracingPermutation):**
- Cards — 仅采样 Surface Cache
- VoxelsAfterCards — Cards 未命中后走 Voxel
- Voxels — 仅 Voxel

### 三层缓存架构

```
Surface Cache (Atlas)
    ├── Albedo/Normal/Emissive/Depth (材质捕获)
    ├── DirectLightingAtlas (直接光照)
    ├── IndirectLightingAtlas (Radiosity 间接光)
    └── FinalLightingAtlas (合成)
         |
         v
Screen Probes (屏幕空间短距离采样 Surface Cache)
    + Radiance Cache (世界空间 Clipmap，长距离补充)
         |
         v
Per-pixel Diffuse GI + Rough Specular
```

### Screen Probe 关键信息

- 均匀网格 + 自适应探针（深度/法线不连续处）
- Octahedral 射线布局
- 支持 BRDF 重要性采样
- 输出格式：SH3 或 Octahedral map
- 空间+时域滤波

### Radiance Cache 关键信息

- 世界空间 Clipmap 组织（多层，以相机为中心）
- 3D Indirection Texture 索引探针
- Screen Probe 标记哪些探针需要更新
- 每帧增量更新（只追踪子集）
- 入口：`LumenRadianceCache::UpdateRadianceCaches()`

---

## 7. RHI 命令系统（已验证）

### 类层级

```
FRHICommandListBase                    — 基础：内存分配器、命令链表、前置依赖
  └─ FRHIComputeCommandList            — Compute：Dispatch、Shader 参数绑定
      └─ FRHICommandList               — Graphics：Draw、RenderPass、PSO
          └─ FRHICommandListImmediate  — 单例即时列表：Flush、Sync、Present
```

### 关键设计

- 命令录制为链表（`FRHICommandBase` 节点，线性分配器分配）
- `TRHILambdaCommand` 包装 lambda 零额外分配
- 并行命令列表可在 Worker 线程录制，通过 `QueueAsyncCommandListSubmit` 提交到 Immediate
- Bypass 模式：单线程直接执行，跳过录制
- RHI Thread 三种模式：None / DedicatedThread / Tasks

---

## 8. RenderGraph 系统（已验证）

### 核心 API

| 方法 | 用途 |
|------|------|
| `AddPass(Name, Params, Flags, Lambda)` | 添加延迟执行的 Pass |
| `CreateTexture(Desc, Name, Flags)` | 创建图追踪的纹理（延迟分配） |
| `CreateBuffer(Desc, Name, Flags)` | 创建图追踪的 Buffer |
| `CreateSRV/CreateUAV` | 创建视图 |
| `RegisterExternalTexture/Buffer` | 导入已存在的 RHI 资源 |
| `QueueTextureExtraction` | 标记资源在图执行后存活 |
| `Execute()` | 编译→裁剪→屏障插入→执行 |
| `AddPassDependency(A, B)` | 显式依赖 |
| `AllocParameters<T>()` | 分配 Pass 参数结构体 |

### 执行流程

```
声明阶段: AddPass() × N（记录 Pass 和资源依赖）
    ↓
编译阶段: 推导执行顺序、裁剪死 Pass、计算资源生命周期
    ↓
分配阶段: Transient 资源别名分配（内存复用）
    ↓
屏障阶段: 自动插入 GPU 资源状态转换
    ↓
执行阶段: 按顺序调用每个 Pass 的 lambda
```

### 05_RenderGraph 终审新增事实（2026-06-13）

| 符号 | 文件 | 行号 |
|------|------|------|
| `ERDGPassTaskMode`（`Inline` / `Await` / `Async`） | `RenderCore/Public/RenderGraphPass.h` | 172 |
| `TRDGLambdaPass::TExecuteLambdaTraits::TaskMode`（Immediate -> Inline；`FRDGAsyncTask` tag -> Async；否则 Await；禁止 immediate + async tag） | 同上 | 644-650 |
| `ERDGPassFlags`（`Raster` / `Compute` / `AsyncCompute` / `Copy` / `NeverCull` 等） | `RenderCore/Public/RenderGraphDefinitions.h` | 127 |
| RDG 调试与执行 CVar 默认值（`r.RDG.ImmediateMode=0`、`Validation=1`、`Debug.FlushGPU=0`、`ClobberResources=0`、`OverlapUAVs=1`） | `RenderCore/Private/RenderGraphPrivate.cpp` | 16 / 23 / 32 / 58 / 69 |
| RDG transient / parallel CVar 默认值（`TransientAllocator=1`、`TransientExtractedResources=1`、`AsyncComputeTransientAliasing=1`、`ParallelExecute=2`） | 同上 | 249 / 258 / 267 / 327 |
| `r.RDG.Debug.FlushGPU` 会关闭 async compute；`IsParallelExecuteEnabled` 也会因 `GRDGDebugFlushGPU` 返回 false | 同上 | 225 / 616 |
| `FRDGBuilder::Execute` 顺序：`Compile()` -> `CompilePassBarriers()` / `CollectPassBarriers()` -> `SetupParallelExecute()` | `RenderCore/Private/RenderGraphBuilder.cpp` | 1755 / 1891 / 1895 / 1896 / 1904 |
| `FRDGBuilder::Compile`（裁剪、render pass merge、async compute fork/join） | 同上 | 1316 |
| `FRDGBuilder::IsTransient` / `IsTransientInternal` | 同上 | 491 / 511 / 526 |
| `CollectAllocateTexture` / `CollectDeallocateTexture` / `CollectAllocateBuffer` / `CollectDeallocateBuffer` | 同上 | 3615 / 3656 / 3682 / 3723 |
| `GetAllocateFences` / `GetDeallocateFences` | 同上 | 4583 / 4607 |
| `FRDGBuilder::AddAliasingTransition` | 同上 | 4553 |
| `FRHITransientAllocationFences` / `IRHITransientResourceAllocator` | `RHI/Public/RHITransientResourceAllocator.h` | 22 / 535 |
| `FRHITransientHeapAllocator::Allocate`（通过 allocation fences 判断复用区间） | `RHICore/Private/RHICoreTransientResourceAllocator.cpp` | 132 |
| `CompilePassBarriers` / `CollectPassBarriers` / `AddTransition` | `RenderCore/Private/RenderGraphBuilder.cpp` | 3750 / 3839 / 4406 |
| `FRDGSubresourceState::IsTransitionRequired`（含 UAV barrier 特例） | `RenderCore/Private/RenderGraphResources.cpp` | 138 |
| `FRDGBarrierBatchBegin` / `FRDGBarrierBatchEnd` 声明与 RHI `BeginTransitions` / `EndTransitions` 提交实现 | `RenderCore/Public/RenderGraphPass.h` / `RenderCore/Private/RenderGraphPass.cpp` | 107 / 184；203 / 338 |
| `FRDGBarrierBatchBegin::SetUseCrossPipelineFence` 与 async compute fork/join 落地位置 | `RenderCore/Public/RenderGraphPass.h` / `RenderCore/Private/RenderGraphBuilder.cpp` | 117；1596 / 1650 |
| `FParallelPassSet` / `SetupParallelExecute` / `QueueAsyncCommandListSubmit` / `FinishRecording` / `ExecutePassPrologue` / `ExecutePass` / `ExecutePassEpilogue` | `RenderCore/Private/RenderGraphBuilder.cpp` / `RHI/Public/RHICommandList.h` | 13 / 2831 / 2082 / 3043 / 3395 / 3482 / 3428；4688 |
| `RegisterExternalTexture` / `QueueTextureExtraction` | `RenderCore/Public/RenderGraphBuilder.h` / `RenderCore/Private/RenderGraphBuilder.cpp` | 1086 / 447 / 2192 |

---

## 9. 渲染线程通信（已验证）

### ENQUEUE_RENDER_COMMAND 机制

```cpp
ENQUEUE_RENDER_COMMAND(CommandName)([captures](FRHICommandListImmediate& RHICmdList) {
    // 在渲染线程执行
});
```

- 宏声明一个 Tag 类型用于 Profiling
- 调用 `FRenderCommandDispatcher::Enqueue<Tag>(lambda)`
- 路由到 TLS 的 FRenderCommandList 或全局 FRenderThreadCommandPipe
- lambda 接收 `FRHICommandListImmediate&`

### 同步机制

- `FlushRenderingCommands()` — 阻塞 Game Thread 直到渲染命令完成
- `FFrameEndSync::Sync()` — 帧末同步
- `GIsThreadedRendering` — 运行时标志，false 时命令在 Game Thread 内联执行

### 03_ThreadModel 终审新增事实（2026-06-12）

- `r.RenderCommandPipeMode` 源码默认值为 `2`，先映射到 `ERenderCommandPipeMode::All`；`GetValidatedRenderCommandPipeMode` 会按运行时条件收缩：不能渲染或移动平台降为 `None`，不允许线程化或 RHI 命令列表 Bypass 时 `All` 降为 `RenderThread`。锚点：`RenderCore/Private/RenderingThread.cpp` 的 `CVarRenderCommandPipeMode`、`GetValidatedRenderCommandPipeMode`、`StartRenderingThread`；`RenderCore/Public/RenderingThread.h` 的 `ERenderCommandPipeMode`。
- TaskGraph 的 `RenderThread_Local` 是独立队列；`ProcessThreadUntilRequestReturn(CurrentThread)` 按传入线程身份解析 `QueueIndex`，RT 主循环处理主队列，不会自动先扫 `_Local`。`_Local` 在等待/并行上下文中显式泵动，例如 RHI 提交等待使用 `WaitUntilTaskCompletes(..., ENamedThreads::GetRenderThread_Local())`，`ParallelFor.h` 使用 `ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local())`。
- 并行 mesh draw 录制的关键 CVar 默认值：`r.MeshDrawCommands.ParallelPassSetup=1`、`r.RHICmdWidth=8`、`r.RHICmdMinDrawsPerParallelCmdList=64`；实际并行宽度使用 `min(TaskGraph worker 数, r.RHICmdWidth)`，任务数再受 `ceil(MaxNumDraws / MinDrawsPerParallelCmdList)` 限制。锚点：`Renderer/Private/MeshDrawCommands.cpp` 的 `CVarMeshDrawCommandsParallelPassSetup`、`DispatchPassSetup`，`RHI/Private/RHICommandList.cpp` 的 `CVarRHICmdWidth`，`Renderer/Private/SceneRendering.cpp` 的 `CVarRHICmdMinDrawsPerParallelCmdList`。
- `ERHIThreadMode::Tasks` 不创建独立 `FRHIThread`/OS 线程；RHI 逻辑线程任务在 `GIsRunningRHIInTaskThread_InternalUseOnly` 为 true 时由高优先级 TaskGraph worker（如 `AnyHiPriThreadNormalTask`）执行，并通过 `ETaskTag::ERhiThread` 与 RHI thread ownership scope 保持 RHI 线程语义。锚点：`RHI/Private/RHICommandList.cpp` 的任务包装、`FRHIThreadScope`、`EnqueueDispatchTask`。
- `FRenderingThreadTickHeartbeat` 是向 RT 投递 `HeartbeatTickTickables` 的辅助线程，不是 hang 判定本体；卡死/健康检查由 `FThreadHeartBeat` 与 `CheckRenderingThreadHealth()` 参与。`g.TimeoutForBlockOnRenderFence` 默认值为 `120000` 毫秒，控制 GT 等 render fence 的最大等待；`FThreadHeartBeat` 的 hang threshold 默认 25 秒，两者不可混写。锚点：`RenderCore/Private/RenderingThread.cpp` 的 `FRenderingThreadTickHeartbeat`、`CheckRenderingThreadHealth`、`CVarTimeoutForBlockOnRenderFence`。

---

## 10. Renderer/Private 子目录索引

| 子目录 | 系统 |
|--------|------|
| `Nanite/` | 虚拟几何体（30 文件） |
| `Lumen/` | 动态全局光照（60+ 文件） |
| `MegaLights/` | 随机多光源采样 |
| `PostProcess/` | 后处理链（70+ 文件） |
| `VirtualShadowMaps/` | 虚拟阴影贴图 |
| `RayTracing/` | 硬件光追基础设施 |
| `HairStrands/` | 毛发渲染 |
| `HeterogeneousVolumes/` | 异构体积 |
| `InstanceCulling/` | GPU Instance Culling |
| `SceneCulling/` | 空间哈希网格剔除 |
| `Shadows/` | 屏幕空间阴影、第一人称自阴影 |
| `Substrate/` | 新材质模型 |
| `CompositionLighting/` | Deferred Decals、SSAO |
| `VT/` | Virtual Texture 系统 |
| `OIT/` | 顺序无关透明 |
| `Skinning/` | GPU 蒙皮 |
| `VariableRateShading/` | VRS |
| `StochasticLighting/` | 随机光照 |
| `Froxel/` | Froxel 数据结构 |
| `SparseVolumeTexture/` | 稀疏体积纹理 |
| `MaterialCache/` | 材质缓存 |
| `StateStream/` | 状态流 |

---

## 11. 模块依赖关系（Build.cs 已验证）

| 模块 | PublicDeps | PrivateDeps（渲染相关） |
|------|-----------|------------------------|
| RHI | — | Core, TraceLog, ApplicationCore, Cbor, BuildSettings |
| RenderCore | RHI, CoreUObject | Core, Projects, ApplicationCore, TraceLog, CookOnTheFly, Json, BuildSettings |
| Engine | Core, CoreUObject, RenderCore, RHI 等 | Renderer 仅 PublicIncludePathModuleNames，不作为链接依赖 |
| Renderer | Core, Engine | CoreUObject, ApplicationCore, RenderCore, ImageWriteQueue, RHI, MaterialShaderQualitySettings, StateStream, TraceLog |
| D3D12RHI | Core, RHI | CoreUObject, Engine, RHICore, RenderCore, TraceLog |

### 关键设计：Engine ↔ Renderer 无循环链接

- Engine 通过 `PublicIncludePathModuleNames` 引用 Renderer 头文件（不链接）
- 运行时通过 `GetRendererModule()` 动态加载（`Runtime/Engine/Private/EngineGlobals.cpp:63`）
- `IRendererModule` 接口定义在 `RenderCore/Public/RendererInterface.h:685`
- `BeginRenderingViewFamily` 声明在 `RendererInterface.h:690`

---

## 12. 帧触发调用链（已验证）

```
FEngineLoop::Tick (Launch/Private/LaunchEngineLoop.cpp:5536)
  → ENQUEUE_RENDER_COMMAND(BeginFrame) (line 5688)
  → UGameEngine::Tick → RedrawViewports (Engine/Private/GameEngine.cpp:789)
    → FViewport::Draw (Engine/Private/UnrealClient.cpp:1707)
      → UGameViewportClient::Draw (Engine/Private/GameViewportClient.cpp:1971)
        → GetRendererModule().BeginRenderingViewFamily(...)
          → FRendererModule::BeginRenderingViewFamilies (Renderer/Private/SceneRendering.cpp:5176)
            → SceneRenderBuilder.CreateLinkedSceneRenderers (SceneRenderBuilder.cpp:511)
              → new FDeferredShadingSceneRenderer / FMobileSceneRenderer
```

---

## 13. Renderer 创建分支（已验证）

`Runtime/Renderer/Private/SceneRenderBuilder.cpp:511-518`

```cpp
if (ShadingPath == EShadingPath::Deferred)
    → FDeferredShadingSceneRenderer (line 513)
else
    → FMobileSceneRenderer (line 518)
```

---

## 14. FSceneRenderer 基类（已验证）

| 类 | 文件 | 行号 |
|----|------|------|
| `FSceneRendererBase` | `Renderer/Private/SceneRendering.h` | 2022 |
| `FSceneRenderer` | `Renderer/Private/SceneRendering.h` | 2079 |
| `Render` (纯虚) | 同上 | 2203 |

---

## 15. D3D12 命令翻译（已验证）

| RHI 虚函数 | D3D12 实现位置 | 底层 API |
|-----------|---------------|---------|
| `RHIDrawPrimitive` | `D3D12RHI/Private/D3D12Commands.cpp:1216` | `DrawInstanced` (line 1225) |
| `RHIDrawIndexedPrimitive` | `D3D12Commands.cpp:1270` | `DrawIndexedInstanced` (line 1297) |
| `RHIDispatchComputeShader` | `D3D12Commands.cpp:140` | `Dispatch` (line 146) |
| `RHIDispatchIndirectComputeShader` | `D3D12Commands.cpp:151` | `ExecuteIndirect` (line 164) |

---

## 16. RHI 虚接口（已验证）

| 类 | 文件 | 行号 | 职责 |
|----|------|------|------|
| `IRHIPlatformCommandList` | `RHI/Public/RHIContext.h` | 233 | 平台命令列表基类 |
| `IRHIComputeContext` | 同上 | 256 | Compute 虚接口 |
| `IRHICommandContext` | 同上 | 692 | Graphics 虚接口（继承 Compute） |
| `RHIDrawPrimitive` | 同上 | 790 | — |
| `RHIDrawIndexedPrimitive` | 同上 | 797 | — |

---

## 17. Primitive 注册路径（已验证）

| 函数 | 文件 | 行号 |
|------|------|------|
| `FScene::AddPrimitive(UPrimitiveComponent*)` | `Renderer/Private/RendererScene.cpp` | 1302 |
| `FScene::AddPrimitive(FPrimitiveSceneDesc*)` | 同上 | 1312 |
| `FScene::AddPrimitiveSceneInfo_RenderThread` | 同上 | 1037 |
| `UPrimitiveComponent::CreateSceneProxy()` | `Engine/Classes/Components/PrimitiveComponent.h` | 2306 |

---

## 18. RHI 后端选择（04_RHI 已验证）

| 符号 | 文件 | 行号 |
|------|------|------|
| `FDynamicRHI`（类声明） | `RHI/Public/DynamicRHI.h` | 205 |
| `FDynamicRHI::GetInterfaceType` | 同上 | 224 |
| `RHICreateGraphicsPipelineState` | 同上 | 415 |
| `RHICreateBufferInitializer` | 同上 | 445 |
| `RHICreateTextureInitializer` | 同上 | 501 |
| `RHIGetDefaultContext` | 同上 | 856（inline 全局 :1362） |
| `extern GDynamicRHI` | 同上 | ~1059 |
| `IDynamicRHIModule` | 同上 | 1471 |
| `IDynamicRHIModule::CreateRHI` | 同上 | 1481 |
| `RHIInit` | `RHI/Private/DynamicRHI.cpp` | 278（GDynamicRHI 赋值 :299，Init() :323） |
| `RHIComputeStatePrecachePSOHash`（内容哈希） | 同上 | 584（CityHash64 :643） |
| `PlatformCreateDynamicRHI` | `RHI/Private/Windows/WindowsDynamicRHI.cpp` | 1243 |
| `LoadDynamicRHIModule` | 同上 | 1147 |
| `ChooseDefaultRHI` | 同上 | 830 |
| `GRHISearchOrder` | 同上 | 120（模块名映射 :135，ParseDefaultWindowsRHI :355，IsSupported 校验 :1212-1236） |
| `FD3D12DynamicRHIModule` | `D3D12RHI/Private/D3D12RHIPrivate.h` | 682 |
| `FD3D12DynamicRHIModule::CreateRHI` 实现 | `D3D12RHI/Private/Windows/WindowsD3D12Device.cpp` | 1057（new FD3D12DynamicRHI :1107，GShaderPlatformForFeatureLevel :1059，GMaxRHIFeatureLevel :1079，GMaxRHIShaderPlatform :1087） |

### 特性等级与全局能力

| 符号 | 文件 | 行号 |
|------|------|------|
| `ERHIFeatureLevel`（ES3_1/SM5/SM6） | `RHI/Public/RHIFeatureLevel.h` | 17（SM6 :52） |
| `GMaxRHIFeatureLevel` | 同上 | 109（默认 SM5：`RHI.cpp:1338`） |
| `EShaderPlatform` | `RHI/Public/RHIShaderPlatform.h` | 10 |
| `GMaxRHIShaderPlatform` | 同上 | 86 |
| `ERHIInterfaceType` | `RHI/Public/RHIDefinitions.h` | 156 |
| `FRHIGlobals` / `GRHIGlobals` | `RHI/Public/RHIGlobals.h` | 87 / 747 |
| `bSupportsBindless` | 同上 | 669 |
| `GSupportsEfficientAsyncCompute`（别名） | 同上 | 812 |
| `GRHISupportsAsyncTextureCreation` | 同上 | 787 |

---

## 19. RHI 资源生命周期（04_RHI 已验证）

| 符号 | 文件 | 行号 |
|------|------|------|
| `FRHIResource` | `RHI/Public/RHIResources.h` | 53（virtual 析构 :61，AddRef :73，Release :80，GetRefCount :93，FAtomicFlags :141，MarkedForDeleteBit/DeletingBit/NumRefsMask :143-145，AtomicFlags 成员 :221，MarkForDelete :168，Deleting :184） |
| `PendingDeletes` / `PendingDeletesWithLifetimeExtension` | `RHI/Private/RHIResources.cpp` | 12（TConsumeAllMpmcQueue） |
| `FRHIResource::MarkForDelete` | 同上 | 46 |
| `DeleteResources`（复活检查 Deleting()） | 同上 | 61 |
| 删除提交点 `GatherResourcesToDelete` / `RHIProcessDeleteQueue` | `RHI/Private/RHICommandList.cpp` | 1256 / 1273（区间 1249-1274） |
| `TRefCountPtr` | `Core/Public/Templates/RefCounting.h` | 453（裸指针 :460，拷贝 :469，移动 :488，析构 Release :501，赋值 :509） |
| `FTextureRHIRef` / `FBufferRHIRef` | `RHI/Public/RHIFwd.h` | 131 / 105 |
| `FRHITexture` | `RHI/Public/RHIResources.h` | 2149（TextureDesc 成员 :2367） |
| `FRHITextureDesc` | 同上 | 1688（字段 :1844 起） |
| `FRHITextureCreateDesc` | 同上 | 1937（Create2D/3D/Cube :2063-2089，附加字段 :2131-2146） |
| `FRHIViewableResource::TrackedAccess` | 同上 | 1313 |
| `FRHIBuffer` | 同上 | 1577（Desc :1638） |
| `FRHIBufferDesc` | 同上 | 1323 |
| `FRHIBufferCreateDesc` | 同上 | 1416 |
| `FRHICommandListBase::RHICreateTexture` | `RHI/Public/RHICommandList.h` | 5553→CreateTexture :941→CreateTextureInitializer :925 |
| `CreateBuffer` | 同上 | 800 |
| `Bypass` / `IsBottomOfPipe` / `IsTopOfPipe` | 同上 | 5309 / 653 / 658 |
| `FRHITextureInitializer`（FFinalizeCallback :139/169，Finalize :135） | `RHI/Public/RHITextureInitializer.h` | 49 |
| `CreateTexture`=Initializer+Finalize 二合一外壳 | `RHI/Public/RHICommandList.h` | 941（CreateTextureInitializer :925，Finalize 调用 :944） |
| `FRHITextureInitializer` 移动构造/Reset/析构 RemovePendingTextureUpload | `RHI/Public/RHITextureInitializer.h` | 52 / 155 / 63（NO COPIES :151-153，GetSubresource :84） |
| `RHICreateTextureInitializer` / `RHICreateBufferInitializer`（纯虚） | `RHI/Public/DynamicRHI.h` | 501 / 445 |
| `ERHIAccess` | `RHI/Public/RHIAccess.h` | 10 |
| `FRHITransitionInfo` | `RHI/Public/RHITransition.h` | 118 |
| `Transition()` | `RHI/Public/RHICommandList.h` | 3020 |

---

## 20. PSO 与缓存（04_RHI 已验证）

| 符号 | 文件 | 行号 |
|------|------|------|
| `FGraphicsPipelineStateInitializer` | `RHI/Public/RHIResources.h` | 4571（ctor :4609，operator== :4666，字段 :4754-4816，行为标志 union :4785-4795，StatePrecachePSOHash :4816） |
| `FBoundShaderStateInput` | 同上 | 4361（字段 :4508，mesh/VS 互斥断言 :4395-4434） |
| `FRHIGraphicsPipelineState` | 同上 | 1057 |
| `FGraphicsPipelineState`（包装，RHIPipeline TRefCountPtr :1264） | 同上 | 1229 |
| `SetGraphicsPipelineState`（声明） | `RHI/Public/PipelineStateCache.h` | 141 |
| `SetGraphicsPipelineState`（实现） | `RHI/Private/PipelineStateCache.cpp` | 1478 |
| `TSharedPipelineStateCache`（Find :1601） | 同上 | 1512 |
| `FGraphicsPipelineCache`（typedef） | 同上 | 2879 |
| `GetAndOrCreateGraphicsPipelineState`（未命中 :4239，命中 :4312，提优先级 :4320/4325，懒算内容哈希 :4249） | 同上 | 4193 |
| 运行时指针哈希 `GetTypeHash(Initializer)` | 同上 | 89 |
| `FCompileGraphicsPipelineStateTask` | 同上 | 3256 |
| `GetAndOrCreateComputePipelineState` | 同上 | 3704 |
| `GComputePipelineCache`（typedef） | 同上 | 2877 |
| `FPipelineFileCacheManager`（CacheGraphicsPSO 记账） | `RHI/Public/PipelineFileCache.h` | 312 |
| `FShaderPipelineCache` | `RenderCore/Public/ShaderPipelineCache.h` | 78 |

---

## 21. Shader Binding（04_RHI 已验证）

| 符号 | 文件 | 行号 |
|------|------|------|
| `FRHIShaderParameter` | `RHI/Public/RHIShaderParameters.h` | 15 |
| `FRHIShaderParameterResource`（EType: Texture/ResourceView/UAV/Sampler/UniformBuffer/ResourceCollection） | 同上 | 31 |
| `FRHIBatchedShaderParameters`（ParametersData/Parameters/ResourceParameters/BindlessParameters；SetShaderUniformBuffer :295/SetShaderTexture :301/SetShaderSampler :311） | 同上 | 240 |
| `RHISetShaderParameters`（纯虚，Compute :343 / Graphics :767；RHISetBatchedShaderParameters :345；RHISetStaticUniformBuffers :362） | `RHI/Public/RHIContext.h` | 343 / 767 |
| `SetBatchedShaderParameters`（Compute :2850 / Graphics :3900） | `RHI/Public/RHICommandList.h` | 2850 / 3900 |
| `FRHIUniformBuffer` | `RHI/Public/RHIResources.h` | 1228 |
| `FRHIUniformBufferLayout`（Resources/ConstantBufferSize/StaticSlot/BindingFlags） | 同上 | 1149 |
| `FRHIUniformBufferLayoutInitializer`（资源记录 :12-28） | `RHI/Public/RHIUniformBufferLayoutInitializer.h` | 40 |
| `EUniformBufferBaseType`（UBMT_FLOAT32/TEXTURE/SRV/SAMPLER/RDG_*） | `RHI/Public/RHIDefinitions.h` | 633 |
| `EUniformBufferBindingFlags`（Shader=1<<0 / Static=1<<1） | 同上 | 685 |
| `FUniformBufferStaticSlot` | 同上 | 722 |
| `SetShaderParameters`（运行期填充 batch） | `RenderCore/Public/ShaderParameterStruct.h` | 247 |
| `SHADER_PARAMETER` 系列宏 | `RenderCore/Public/ShaderParameterMacros.h` | — |
| `EShaderParameterType`（含 BindlessSRV/UAV/Sampler） | `RenderCore/Public/ShaderCore.h` | 247 |
| `FRHIDescriptorHandle`（Index/Type） | `RHI/Public/RHIDefinitions.h` | 1400 |
| `ERHIBindlessConfiguration`（Disabled/RayTracing/Minimal/All） | 同上 | 1431 |
| `FD3D12RootSignature`（BindSlotMap :467） | `D3D12RHI/Private/D3D12RootSignature.h` | 72 |
| `FD3D12DescriptorHeap`（bIsGlobal :148） | `D3D12RHI/Private/D3D12Descriptors.h` | 94 |
| `FD3D12BindlessDescriptorAllocator::AllocateDescriptor` / `FD3D12BindlessDescriptorManager`（`InitializeDescriptor` / `UpdateDescriptor` / `DeferredFreeFromDestructor`） | `D3D12RHI/Private/D3D12BindlessDescriptors.cpp` / `.h` | 165 / 809 / 817 / 829 / 841；264 |

---

## 22. 资源屏障与命令列表层级（04_RHI 已验证）

| 符号 | 文件 | 行号 |
|------|------|------|
| `ERHIAccess`（Unknown=0；SRVCompute/SRVGraphicsPixel/RTV/UAVCompute/CopyDest/DSVRead/DSVWrite 等位标志；ReadOnlyExclusiveMask :63 / ReadOnlyMask :69 / WritableMask :81 / IsReadOnlyAccess :90 / IsWritableAccess :100） | `RHI/Public/RHIAccess.h` | 10 |
| `FRHITransitionInfo`（继承 FRHISubresourceRange；union 资源 + EType + AccessBefore/AccessAfter + Flags；构造重载 纹理 :146 / UAV :162 / 缓冲 :170） | `RHI/Public/RHITransition.h` | 118 |
| `FRHITransientAliasingInfo` | 同上 | 281 |
| `FRHIComputeCommandList::Transition`（单个 :3032 / 批量 :3020；BeginTransitions :3022 / EndTransitions :3027；ALLOC_COMMAND FRHICommandEndTransitions :3016） | `RHI/Public/RHICommandList.h` | 3020 |
| 命令列表层级 `FRHICommandListBase` | `RHI/Public/RHICommandList.h` | 454 |
| `FRHIComputeCommandList`（Dispatch/Transition/SetComputePipelineState） | 同上 | 2734 |
| `FRHICommandList`（Draw/RenderPass/SetStreamSource/SetGraphicsPipelineState） | 同上 | 3818 |
| `FRHICommandListImmediate`（单例；ImmediateFlush/QueueAsyncCommandListSubmit/Present） | 同上 | 4625 |
| `QueueAsyncCommandListSubmit`（并行子列表提交到 Immediate；`ETranslatePriority` / `MinDrawsPerTranslate` 在当前 UE5.7 路径中不作为并行翻译控制依据，实际并行宽度由 Submit 路径和相关 CVar 控制） | 同上 | 4688 |

---

## 23. Component 到 SceneProxy 生命周期（02_SceneProxy 已验证）

| 符号 | 文件 | 行号 |
|------|------|------|
| `UPrimitiveComponent::CreateRenderState_Concurrent`（`UpdateBounds` 后通过 `Context->AddPrimitive` 或 `World->Scene->AddPrimitive` 进入 Scene） | `Engine/Private/Components/PrimitiveComponent.cpp` | 643 |
| `UPrimitiveComponent::CreateSceneProxy` 默认返回 `NULL`；`ShouldRecreateProxyOnUpdateTransform` 默认 false | `Engine/Classes/Components/PrimitiveComponent.h` | 2306 / 2326 |
| `UPrimitiveComponent::SendRenderTransform_Concurrent`（更新 Bounds 后调用 `Scene->UpdatePrimitiveTransform`） | `Engine/Private/Components/PrimitiveComponent.cpp` | 678 |
| `UPrimitiveComponent::DestroyRenderState_Concurrent` / `ReleaseSceneProxy` | 同上 | 857 / 5375 |
| `UActorComponent::DoDeferredRenderUpdates_Concurrent`（RenderState dirty 优先于 Transform dirty） | `Engine/Private/Components/ActorComponent.cpp` | 2587 |
| `UActorComponent::MarkRenderStateDirty` / `MarkRenderTransformDirty` | 同上 | 2634 / 2652 |
| `UWorld::SendAllEndOfFrameUpdatesInternal`（创建 `FPrimitiveTransformUpdater` 并批量送出帧末更新） | `Engine/Private/LevelTick.cpp` | 1119 |
| `UStaticMeshComponent::CreateSceneProxy` → `FStaticMeshComponentHelper::CreateSceneProxy` | `Engine/Private/StaticMeshSceneProxy.cpp` / `Engine/Public/StaticMeshComponentHelper.h` | 2918 / 416 |
| `UStaticMeshComponent::CreateStaticMeshSceneProxy`（Nanite 与经典 `FStaticMeshSceneProxy` 分叉） | `Engine/Private/StaticMeshSceneProxy.cpp` | 2899 |
| `UStaticMeshComponent::ShouldRecreateProxyOnUpdateTransform`（非 Movable 返回 true） | 同上 | 2923 |
| `FStaticMeshSceneProxy` 构造函数（从 `FStaticMeshSceneProxyDesc` 取得 RenderData、材质相关性、LOD/Section 等 Proxy 数据） | 同上 | 223 / 229 |
| `FPrimitiveSceneProxyDesc`（通用 Primitive 可见性、光照、DepthPass/MainPass、ComponentId、BoundsScale 等镜像字段） | `Engine/Public/PrimitiveSceneProxyDesc.h` | 11 |
| `FStaticMeshSceneProxyDesc` / `InitializeFromStaticMeshComponent`（从 `UStaticMeshComponent` 摘取 StaticMesh、OverrideMaterials、LOD、WPO、Nanite、BodySetup、Lightmap 等字段） | `Engine/Public/StaticMeshSceneProxyDesc.h` / `Engine/Private/StaticMeshSceneProxyDesc.cpp` | 12 / 45 |
| `FPrimitiveSceneProxy` 类、`DrawStaticElements`、`GetDynamicMeshElements`、`CreateRenderThreadResources`、`DestroyRenderThreadResources` | `Engine/Public/PrimitiveSceneProxy.h` | 295 / 444 / 502 / 586 / 593 |
| `FPrimitiveSceneProxy::SetTransform`（写 LocalToWorld、Bounds、ActorPosition，标记 reflection/uniform 更新并调用 `OnTransformChanged`） | `Engine/Private/PrimitiveSceneProxy.cpp` | 877 |
| `FScene::AddPrimitive` / `BatchAddPrimitivesInternal`（GT 创建 Proxy 与 `FPrimitiveSceneInfo`，enqueue `AddPrimitiveCommand`） | `Renderer/Private/RendererScene.cpp` | 1302 / 1343 |
| `FScene::AddPrimitiveSceneInfo_RenderThread`（`PrimitiveUpdates.EnqueueAdd`，不直接插入 Scene 数组） | 同上 | 1037 |
| `FPrimitiveSceneInfo` 类（Renderer 管理外壳，持有 `FPrimitiveSceneProxy* Proxy`、StaticMeshes、StaticMeshCommandInfos、OctreeId、PackedIndex 等） | `Renderer/Public/PrimitiveSceneInfo.h` | 265 |
| `FPrimitiveSceneInfo` 构造函数（通过 adapter 从 Component/Desc 取 Proxy、ComponentId、SceneData、Instance buffers） | `Renderer/Private/PrimitiveSceneInfo.cpp` | 285 / 367 / 372 |
| `FScene` Primitive dense arrays 与 `PackedIndex` 注释（`Primitives`、`PrimitiveTransforms`、`PrimitiveSceneProxies`、`PrimitiveBounds`、`PrimitiveFlagsCompact` 等） | `Renderer/Private/ScenePrivate.h` | 2936-3027 |
| `FScene::Update`（drain `PrimitiveUpdates`，分 Added/Removed/Deleted，分配 PersistentIndex，维护 dense arrays/type offset table） | `Renderer/Private/RendererScene.cpp` | 5190 / 5225 / 5640 |
| `FPrimitiveSceneInfo::AddToScene`（Indirect Lighting、Reflection、PrimitiveOctree、Bounds/Flags/ComponentId 写入 Scene 数组） | `Renderer/Private/PrimitiveSceneInfo.cpp` | 1765 |
| `FPrimitiveSceneInfo::AddStaticMeshes`（调用 `Proxy->DrawStaticElements`，写 `SceneInfo->StaticMeshes` / `Scene->StaticMeshes`，触发 draw command/Nanite/ray tracing 缓存） | 同上 | 1547 |
| `FPrimitiveSceneInfo::CacheMeshDrawCommands` | 同上 | 593 |
| `FStaticMeshSceneProxy::DrawStaticElements` / `GetDynamicMeshElements` | `Engine/Private/StaticMeshSceneProxy.cpp` | 1382 / 1669 |
| 可见性阶段调用 `Proxy->GetDynamicMeshElements` | `Renderer/Private/SceneVisibility.cpp` | 4157 / 4165 / 4193 / 4201 |
| `FScene::UpdatePrimitiveTransformInternal`（重建 Proxy 或组装 `FPrimitiveUpdateParams`；冗余 Transform CVar 默认值 `Warning=0`、`Skip=1`） | `Renderer/Private/RendererScene.cpp` | 1567 / 1489 / 1496 |
| `FScene::UpdatePrimitiveTransform_RenderThread`（排 `FUpdateTransformCommand` 到 `PrimitiveUpdates`） | 同上 | 1525 |
| `FScene::UpdatePrimitiveTransforms`（批量 updater 一个 render command，完成后 `delete Updater`） | 同上 | 1698 |
| `FScene::RemovePrimitive` / `BatchRemovePrimitivesInternal` / `RemovePrimitiveSceneInfo_RenderThread` | 同上 | 1987 / 2009 / 1982 |
| `FPrimitiveSceneInfo::RemoveFromScene`（撤销 octree、光照交互、静态 mesh 缓存等） | `Renderer/Private/PrimitiveSceneInfo.cpp` | 1979 |
| `FScene::Update` 删除尾声（`BeginCleanup` 处理 HitProxy，删除 Proxy 和 SceneInfo） | `Renderer/Private/RendererScene.cpp` | 6494 |
| `FPrimitiveSceneProxy::DrawStaticElements` / `GetDynamicMeshElements` / `GetViewRelevance` | `Engine/Public/PrimitiveSceneProxy.h` | 终审补充确认 Proxy 向 Renderer 暴露的三类核心契约：静态 draw 缓存、动态 mesh 收集、view/pass relevance 判定。 |
| `FScene::UpdatePrimitiveTransformInternal` -> `UpdatePrimitiveTransform_RenderThread` -> `PrimitiveUpdates.Enqueue(FUpdateTransformCommand)` | `Renderer/Private/RendererScene.cpp` | 终审补充确认 transform dirty 不直接改 GPU 数据，而是经 Render Thread 命令进入 `FScene::Update` 批处理。 |
---

## 24. GPUScene 与动态 Primitive 上传（06_GPUScene 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FGPUScene` | `Renderer/Private/GPUScene.h` / `.cpp` | Scene 的 GPU 可寻址镜像，维护 primitive / instance / payload 上传与 dirty primitive 列表 |
| `FScene::Update` 中 GPUScene 更新 | `Renderer/Private/RendererScene.cpp` | Primitive 更新提交后进入 `GPUScene.Update(...)`，并处理 primitive uniform / dirty 状态 |
| `FPrimitiveSceneInfo::AllocateGPUSceneInstances` / `FreeGPUSceneInstances` | `Renderer/Private/PrimitiveSceneInfo.cpp` | 为 persistent primitive 分配 / 释放 `InstanceSceneDataOffset` 与 payload range |
| `FGPUScene::AllocateInstanceSceneDataSlots` / `FreeInstanceSceneDataSlots` / `AddPrimitiveToUpdate` | `Renderer/Private/GPUScene.cpp` | 管理 instance slot、释放清理和 dirty primitive 合并 |
| `FGPUScene::UpdateInternal` / `UploadGeneral` | `Renderer/Private/GPUScene.cpp` | 按 dirty primitive 上传 persistent primitive / instance / payload 数据 |
| `FUploadDataSourceAdapterScenePrimitives` / `FUploadDataSourceAdapterDynamicPrimitives` | `Renderer/Private/GPUScene.cpp` | persistent primitive 上传时由 persistent id 临时映射回 packed index；dynamic primitive 上传走 view collector 数据源 |
| `FGPUScene::UploadDynamicPrimitiveShaderDataForViewInternal` | `Renderer/Private/GPUScene.cpp` | 按 view commit 动态 primitive collector，并上传动态 primitive 的 GPUScene 数据 |
| `FPrimitiveUniformShaderParametersBuilder::Build` / `FPrimitiveSceneShaderData::BuildDataFromProxy` | `Engine/Public/PrimitiveUniformShaderParametersBuilder.h` / `Engine/Private/PrimitiveUniformShaderParameters.cpp` | 构建 GPUScene primitive data，包含 LWC `PositionHigh`、relative matrix、actor/object high-low 等字段 |
| `FInstanceSceneShaderData::BuildInternal` / `GetDataStrideInFloat4s` | `Engine/Public/InstanceUniformShaderParameters.h` / `Engine/Private/InstanceUniformShaderParameters.cpp` | 构建 instance data，处理 3/4 float4 stride、flags、custom data 与 payload offset |
| `SceneData.ush` 的 `GetPrimitiveData` / `GetInstanceSceneDataInternal` | `Engine/Shaders/Private/SceneData.ush` | Shader 侧通过 `PrimitiveId` / `InstanceId` 读取 GPUScene buffer |
| `FMeshDrawCommandPrimitiveIdInfo` / `FMeshPassProcessor::GetDrawCommandPrimitiveId` | `Renderer/Public/MeshPassProcessor.h` / `Renderer/Private/MeshPassProcessor.cpp` | MeshDrawCommand 与 GPUScene 的 primitive id / instance scene data offset 交接结构 |
| Instance Culling 消费 GPUScene | `Renderer/Private/InstanceCulling/InstanceCullingContext.cpp` / `Engine/Shaders/Private/InstanceCulling/BuildInstanceDrawCommands.usf` | Instance Culling 读取 GPUScene 与 draw command instance range，生成可见 instance / indirect draw 输入 |
| GPUScene CVar 默认值与 tiled primitive data | `Renderer/Private/GPUScene.cpp` / `Renderer/Private/ScenePrivate.h` | 终审补充确认 GPUScene persistent/dynamic upload、dirty primitive 与 tiled primitive data 开关不应写成可见性来源。 |
| `SceneDefinitions.h` GPUScene layout 宏 / `SceneData.ush::UnpackInstanceRelativeIdAndCustomDataCount` / `r.PrimitiveHasTileOffsetData` | `Engine/Shaders/Shared/SceneDefinitions.h` / `Engine/Shaders/Private/SceneData.ush` / `Renderer/Private/GPUScene.cpp` | 终审补充确认 instance stride、relative instance id/custom data count 解包与 tile offset data 的 shader-side 合约。 |
---

## 25. MeshDrawCommand 与 MeshPassProcessor（07_MeshDrawCommand 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FMeshDrawCommand` | `Renderer/Public/MeshPassProcessor.h` | RHI 之上的可提交 mesh pass draw 描述，包含 shader bindings、vertex/index streams、pipeline id、draw 参数等 |
| `FMeshPassProcessor` | `Renderer/Public/MeshPassProcessor.h` | 把 proxy 输出的 `FMeshBatch` 转换成当前 pass 的 `FMeshDrawCommand` |
| `FPassProcessorManager` / `REGISTER_MESHPASSPROCESSOR_AND_PSOCOLLECTOR` | `Renderer/Public/MeshPassProcessor.h` | 以 `EShadingPath + EMeshPass` 注册 pass processor、pass flags 与 PSO collector |
| `FVisibleMeshDrawCommand` / `FMeshDrawCommandPrimitiveIdInfo` | `Renderer/Public/MeshPassProcessor.h` | 每帧可见性包装，附带 primitive id、instance scene data offset、sort key 和 culling payload |
| `FPrimitiveSceneInfo::CacheMeshDrawCommands` | `Renderer/Private/PrimitiveSceneInfo.cpp` | Primitive 注册 / 更新时为支持缓存的 static mesh 预生成 per-pass cached mesh draw commands |
| `FGraphicsMinimalPipelineStateId::GetPersistentId` / `GetPipelineStateId` | `Renderer/Private/MeshPassProcessor.cpp` | `BuildMeshDrawCommands` 阶段保存 minimal pipeline id，不在此时创建后端 RHI PSO |
| `GenerateDynamicMeshDrawCommands` | `Renderer/Private/MeshDrawCommands.cpp` | InitViews 中把动态 `FMeshBatch` / dynamic build request 转成 one-frame draw commands |
| `FSceneRenderer::SetupMeshPass` | `Renderer/Private/SceneRendering.cpp` | 为每个有输入的 main view pass 创建 `FParallelMeshDrawCommandPass` 并派发 setup |
| `FParallelMeshDrawCommandPass::DispatchPassSetup` | `Renderer/Private/MeshDrawCommands.cpp` | 收敛 cached/dynamic command、pass processor、instance culling context，支持并行 pass setup |
| `FParallelMeshDrawCommandPass::BuildRenderingCommands` / `DispatchDraw` | `Renderer/Private/MeshDrawCommands.cpp` | 连接 Instance Culling draw params 与 RHI draw 提交 |
| `FMeshPassProcessor::BuildMeshDrawCommands` | `Renderer/Public/MeshPassProcessor.inl` | 将 pass processor 已选定的 shader、render state、vertex streams、shader bindings、primitive id info 编译成 `FMeshDrawCommand` / visible command |
| `FMeshDrawCommand::MatchesForDynamicInstancing` / `GetDynamicInstancingHash` | `Renderer/Public/MeshPassProcessor.h` | dynamic instancing 的 command 匹配条件，要求 pipeline、shader bindings、vertex/index streams、draw 参数等一致 |
| `FMeshDrawCommandSortKey` | `Renderer/Public/MeshPassProcessor.h` / `Renderer/Private/MeshPassProcessor.cpp` | BasePass / Translucent / Generic 使用不同位域；translucency 还会做 per-view sort key 更新 |
| `FCachedMeshDrawCommandInfo` / state bucket 路径 | `Renderer/Private/PrimitiveSceneInfo.cpp` / `Renderer/Public/MeshPassProcessor.h` | static mesh cached command 不直接塞回 mesh batch，而是保存 command index 或 state bucket id |
| `FDynamicPassMeshDrawListContext` / `FDynamicMeshDrawCommandStorage` | `Renderer/Private/MeshDrawCommands.cpp` / `Renderer/Public/MeshPassProcessor.h` | 动态 one-frame command 的 draw list context 与稳定地址存储，供 `FVisibleMeshDrawCommand` 指针引用 |
| `FInstanceCullingContext::SetupDrawCommands` / `SubmitDrawCommands` | `Renderer/Private/InstanceCulling/InstanceCullingContext.cpp` | Instance Culling 消费 visible commands；`SetupDrawCommands(..., true, ...)` 会 compact 相同 state bucket，不应把 `r.MeshDrawCommands.DynamicInstancing` 写成唯一开关 |
| `FMeshDrawCommand::SubmitDrawBegin` / `PipelineStateCache::GetAndOrCreateGraphicsPipelineState` | `Renderer/Private/MeshPassProcessor.cpp` / `RHI/Private/PipelineStateCache.cpp` | 提交阶段才通过 PipelineStateCache 查找或创建后端 graphics PSO |
| `r.MeshDrawCommands.DynamicInstancing` / `r.AsyncCacheMeshDrawCommands` | `Renderer/Private/SceneRendering.cpp` | 控制 legacy / fallback dynamic instancing 与异步 cached mesh draw command 生成；需与 Instance Culling state bucket compact 区分 |
| `FMeshDrawCommandSortKey` bit layout | `Renderer/Public/MeshPassProcessor.h` / `Renderer/Private/MeshPassProcessor.cpp` | 终审补充确认 BasePass、Translucent、Generic sort key 的位域定义各不相同，不能把排序键解释成单一通用字段。 |
| `FDrawCommandRelevancePacket::AddCommandsForMesh` cached-command gate | `Renderer/Private/SceneVisibility.cpp` | 终审补充确认 cached mesh draw command 被加入可见列表需要同时满足 primitive visibility、pass relevance、command info 和 pass setup 条件。 |
---

## 26. 帧初始化与可见性（08_FrameInit 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 早期顺序 | `Renderer/Private/DeferredShadingRenderer.cpp` | `OnRenderBegin` -> `BeginInitViews` -> `FinishGatherDynamicMeshElements` -> GPUScene update -> scene uniform update -> `BeginDeferredCulling` |
| `FSceneRenderer::OnRenderBegin` | `Renderer/Private/SceneRendering.cpp` | 创建 / 返回 visibility task data，并启动若干帧级系统的 render-begin 工作 |
| `FSceneRenderer::PrepareViewStateForVisibility` | `Renderer/Private/SceneVisibility.cpp` | 在 visibility 前更新 occlusion frame counter、query submission、ignore flags 与 possible pixels 统计 |
| `LaunchVisibilityTasks` / `FVisibilityTaskData::LaunchVisibilityTasks` | `Renderer/Private/SceneVisibility.cpp` / `Renderer/Private/SceneVisibilityPrivate.h` | UE5.7 可见性不是单一 `ComputeViewVisibility` 函数，而是 task data 驱动的 pipeline |
| `FDeferredShadingSceneRenderer::BeginInitViews` | `Renderer/Private/SceneVisibility.cpp` | `PreVisibilityFrameSetup`、动态 mesh 收集启动、view RHI resources 初始化、visibility render-thread tasks 推进 |
| `FVisibilityViewPacket::BeginInitVisibility` / `CullOctree` / `FrustumCull` | `Renderer/Private/SceneVisibility.cpp` | 处理 frustum/distance/hidden/show-only 与可选 octree traversal；`r.Visibility.FrustumCull.UseOctree` 源码默认 false |
| `PrecomputedOcclusionCull` / `FGPUOcclusionPacket::OcclusionCullPrimitive` | `Renderer/Private/SceneVisibility.cpp` | 遮挡裁剪消费预计算遮挡、HZB/query/history 等保守信息并写 per-view visibility bits |
| `FRelevancePacket::ComputeRelevance` | `Renderer/Private/SceneVisibility.cpp` | 将可见 primitive 转换为 pass relevance、static command relevance 与 dynamic mesh 需求 |
| `FDynamicMeshElementContext` / `FVisibilityTaskData::GatherDynamicMeshElements` | `Renderer/Private/SceneVisibility.cpp` | 支持 Render Thread / parallel task 的动态 mesh element 收集与 deferred context 提交 |
| `ComputeDynamicMeshRelevance` | `Renderer/Private/SceneVisibility.cpp` | 为动态 mesh 计算 pass relevance，并累加 `View.NumVisibleDynamicMeshElements[Pass]` |
| `FVisibilityTaskData::FinishGatherDynamicMeshElements` | `Renderer/Private/SceneVisibility.cpp` | 等 dynamic mesh 收集，提交 context，启动 `SetupMeshPasses` 任务 |
| `FVisibilityTaskData::SetupMeshPasses` | `Renderer/Private/SceneVisibility.cpp` | 对每个 view 调 `FSceneRenderer::SetupMeshPass`，生成 pass 级 draw command 入口 |
| `FGPUScene::UploadDynamicPrimitiveShaderDataForView` | `Renderer/Private/GPUScene.cpp` / `.h` | InitViews 后按 view 上传动态 primitive GPUScene 数据 |
| `FInstanceCullingManager::BeginDeferredCulling` 调用点 | `Renderer/Private/DeferredShadingRenderer.cpp` | 发生在 GPUScene update、view data 和 scene uniform buffer 更新之后 |
| `FVisibilityTaskData::ProcessRenderThreadTasks` | `Renderer/Private/SceneVisibility.cpp` | 推进 render-thread / parallel visibility tasks，包括 occlusion、cached command 等待、relevance 和 dynamic mesh gather |
| `FVisibilityTaskData::Finish` | `Renderer/Private/SceneVisibility.cpp` | 等待 compute relevance、finalize relevance、dynamic mesh elements、mesh pass setup，并清理临时 visibility 数据 |
| `ViewDataManager.InitInstanceState` 调用点 | `Renderer/Private/DeferredShadingRenderer.cpp` | 所有 view flush dynamic primitives 后初始化 instance state，位于 scene extension view data 和 scene uniform 更新之前 |
| `GetSceneExtensionsRenderers().UpdateViewData` / `UpdateSceneUniformBuffer` | `Renderer/Private/DeferredShadingRenderer.cpp` | GPUScene fully updated 后更新 view data 与 scene uniform buffer，作为 `BeginDeferredCulling` 前置 |
| `FSceneRenderer::SetupMeshPass` 输入为空跳过逻辑 | `Renderer/Private/SceneRendering.cpp` | `ViewCommands.MeshCommands`、`NumVisibleDynamicMeshElements`、`DynamicMeshCommandBuildRequests` 三者都为空时不创建 pass |

---

## 27. DepthPrepass、SceneDepth 与 HZB（09_DepthPrepass 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `GetDepthPassInfo` / `ShouldRenderPrePass` | `Renderer/Private/DepthRendering.cpp` / `Renderer/Private/DeferredShadingRenderer.cpp` | 从 scene early z 设置得到 `DepthPass`，决定是否需要常规 prepass |
| `FScene::GetEarlyZPassMode` / `UpdateEarlyZPassMode` | `Renderer/Private/RendererScene.cpp` | 从 `r.EarlyZPass`、movable early z 和强制 full depth 条件得到 scene 级 `EDepthDrawingMode` |
| `ShouldForceFullDepthPass` | `RenderCore/Private/RenderUtils.cpp` | Nanite、DBuffer、virtual texturing、compute AO、forward shading 等条件可强制 full depth prepass |
| `RenderPrepassAndVelocity` 局部流程 | `Renderer/Private/DeferredShadingRenderer.cpp` | 清 depth/stencil、调用 `RenderPrePass`、特殊 velocity 补齐、等待 Nanite material bins、调用 `RenderNanite`、resolve depth |
| `FDeferredShadingSceneRenderer::RenderPrePass` | `Renderer/Private/DepthRendering.cpp` | 消费 `EMeshPass::DepthPass` / `SecondStageDepthPass` 的 `FParallelMeshDrawCommandPass`，写 SceneDepth |
| `FDepthPassMeshProcessor` | `Renderer/Private/DepthRendering.cpp` | 根据 `EDepthDrawingMode`、material、occluder 标志、position-only 支持生成 depth pass draw commands |
| `FirstStageDepthBuffer` / `PartialDepth` | `Renderer/Private/DepthRendering.cpp` / `Renderer/Private/DeferredShadingRenderer.cpp` | 二阶段 depth pass 时保留第一阶段 depth；否则 `PartialDepth` 指向完整 SceneDepth |
| `FDeferredShadingSceneRenderer::RenderHzb` | `Renderer/Private/DeferredShadingRenderer.cpp` | 从 resolved SceneDepth 构建 furthest / closest HZB，并更新 `View.HZB`、`View.ClosestHZB` |
| `BuildHZB` / `HZBBuildPS` / `HZBBuildCS` | `Renderer/Private/SceneTextureReductions.cpp` / `Engine/Shaders/Private/HZB.usf` | HZB mip0 尺寸、POT/半分辨率策略、furthest=min device-Z、closest=round-up max device-Z、compute/pixel 路径 |
| Nanite depth export | `Renderer/Private/Nanite/NaniteCullRaster.cpp` / `Renderer/Private/Nanite/NaniteComposition.cpp` | PrePass 窗口中 Nanite 使用 previous/primed HZB two-pass culling，并通过 `EmitDepthTargets` 写入同一 `SceneDepth.Target` |
| `FHZBOcclusionTester::Submit` | `Renderer/Private/SceneOcclusion.cpp` | HZB occlusion 测试提交点 |
| `GetHZBParameters` | `Renderer/Private/HZB.cpp` | 把 HZB texture、view rect、UV scale/bias、valid flags 打包给 shader 消费 |
| `FDepthPassMeshProcessor::ShouldRender` / `TryAddMeshBatch` / `AddMeshBatch` | `Renderer/Private/DepthRendering.cpp` | depth pass 过滤链：depth flag、occluder/movable、masked/non-masked、position-only、WPO/velocity 分工 |
| `FDeferredShadingSceneRenderer::RenderPrePass` parallel / non-parallel 分支 | `Renderer/Private/DepthRendering.cpp` | 两条 RDG raster 提交路径都消费 InitViews 产出的 `View.ParallelMeshDrawCommandPasses[DepthPass]` 和 instance culling draw params |
| `RenderPrePassHMD` / `RenderVelocities` 调用点 | `Renderer/Private/DeferredShadingRenderer.cpp` / `Renderer/Private/DepthRendering.cpp` | PrePass 窗口中 HMD mask 与 `DDM_AllOpaqueNoVelocity` velocity 补齐的接入点 |
| `BuildHZB` 调用点 | `Renderer/Private/DeferredShadingRenderer.cpp` | `RenderHzb` 从 `SceneTextures.Depth.Resolve` 构建 closest/furthest HZB，必要时 extraction 到上一帧 view state |
| `bOcclusionBeforeBasePass` | `Renderer/Private/DeferredShadingRenderer.cpp` | full early depth 或 `DDM_AllOccluders` 决定 occlusion/HZB 是否可在 BasePass 前执行 |
| 09_DepthPrepass 终审整章锚点回归 | `Renderer/Private/DepthRendering.cpp` / `Renderer/Private/DeferredShadingRenderer.cpp` / `Renderer/Private/Nanite/*` / `Renderer/Private/SceneTextureReductions.cpp` / `Engine/Shaders/Private/HZB.usf` | 终审补充确认 depth mode/defaults、PrePass/Velocity/PartialDepth、Nanite two-pass/depth export、HZB 构建常量以及 current/history HZB 消费路径均通过源码回归。 |
---

## 28. BasePass 与 GBuffer（10_BasePass 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 中 BasePass 前后顺序 | `Renderer/Private/DeferredShadingRenderer.cpp` | PrePass / GPUScene / DBuffer / `RenderBasePass` / BasePass 后 `SceneTextures` uniform buffer 重建 |
| `BasePassDepthStencilAccess` | `Renderer/Private/DeferredShadingRenderer.cpp` | 根据 full early depth 与调试模式决定 BasePass 是 depth read 还是 depth write |
| `FDeferredShadingSceneRenderer::RenderBasePass` | `Renderer/Private/BasePassRendering.cpp` | 绑定 SceneColor / GBuffer / Velocity 等 render targets，提交经典 mesh BasePass |
| `FBasePassMeshProcessor` | `Renderer/Private/BasePassRendering.cpp` / `.h` | BasePass 的 mesh processor，筛选 material / pass 条件并生成 BasePass draw commands |
| `FSceneTextures::GetGBufferRenderTargets` / `CreateSceneTextureUniformBuffer` | `Renderer/Internal/SceneTextures.h` / `Renderer/Private/SceneTextures.cpp` | 描述 GBuffer render target 绑定和 BasePass 后的 scene texture uniform buffer 暴露 |
| `FCompositionLighting::ProcessBeforeBasePass` | `Renderer/Private/CompositionLighting/CompositionLighting.cpp` | DBuffer decal 在 BasePass 前写入，供 BasePass shader 混入 GBuffer |
| `HLSLMaterialTranslator` / `MaterialTemplate.ush` | `Engine/Private/Materials/HLSLMaterialTranslator.cpp` / `Engine/Shaders/Private/MaterialTemplate.ush` | 材质节点图最终进入 material HLSL 模板；BasePass 只建立到 shader 输入的最小路径，完整编译留给 20/21 |
| `BasePassPixelShader.usf` / `ShadingModelsMaterial.ush` / `DeferredShadingCommon.ush` | `Engine/Shaders/Private/` | BasePass pixel shader 生成 `FPixelMaterialInputs` / `FGBufferData`，再按 shading model 编码写入 GBuffer |
| `CreateDBufferTextures` / `GetDBufferParameters` | `Renderer/Private/DBufferTextures.cpp` / `.h` | 创建 DBuffer A/B/C/Mask 或 fallback 参数，供 BasePass shader 混入 decal 结果 |
| Nanite material bins | `Renderer/Private/Nanite/NaniteDrawList.cpp` / `Renderer/Private/Nanite/NaniteMaterials.h` | `FNaniteMaterialListContext::AddShadingBin` 将 material section 映射到 `FNaniteMaterialSlot` 的 shading bin |
| Nanite BasePass shading | `Renderer/Private/Nanite/NaniteShading.cpp` | `BuildShadingCommands` / `DispatchBasePass` / `ShadeBinning` / `ShadeGBufferCS` 进入 Nanite BasePass GBuffer 写入路径 |
| `FSceneTextures::GetGBufferRenderTargets` slot 0 / A-E / Velocity 绑定规则 | `Renderer/Internal/SceneTextures.h` / `Renderer/Private/SceneTextures.cpp` | 终审补充确认 slot 0 绑定 `SceneColor.Target`，GBuffer A-E 与 Velocity 只在对应 `Index > 0` 时加入 MRT。 |
---

## 29. 阴影系统（11_Shadows 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 中阴影相关顺序 | `Renderer/Private/DeferredShadingRenderer.cpp` | VSM 初始化、early/default `RenderShadowDepthMaps`、BasePass 后 page marking、MegaLights sample、Lighting 消费的时序锚点 |
| `BeginInitDynamicShadows` / `FinishInitDynamicShadows` | `Renderer/Private/ShadowSetup.cpp` | 动态阴影 primitive gather、`FProjectedShadowInfo` 建立、shadow mesh pass setup 的主入口 |
| `AddViewDependentWholeSceneShadowsForView` | `Renderer/Private/ShadowSetup.cpp` / `Renderer/Private/SceneRendering.h` | Directional light 的 view-dependent whole-scene shadows / CSM projected shadow 创建路径 |
| `GetSplitDistance` / `GetShadowSplitBounds` | `Engine/Private/Components/DirectionalLightComponent.cpp` | CSM split 距离、cascade transition/fade、bounds sphere 计算 |
| `FSceneRenderer::RenderShadowDepthMaps` | `Renderer/Private/ShadowDepthRendering.cpp` | ShadowDepth 总入口，调度 VSM、regular atlas、cached shadows 等输出 |
| `FShadowSceneRenderer::DispatchVirtualShadowMapViewAndCullingSetup` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` / `.h` | VSM shadow view 与 culling setup，消费 virtual shadow map shadows |
| `FShadowSceneRenderer::BeginMarkVirtualShadowMapPages` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` / `.h` | BasePass 后从 scene depth、front layer、froxel 等输入开始标记本帧 VSM page requests |
| `FShadowSceneRenderer::RenderVirtualShadowMaps` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` | 先 `BuildPageAllocations`，再分 Nanite / non-Nanite 写 VSM physical pages |
| `FVirtualShadowMapArray::BuildPageAllocations` / `RenderVirtualShadowMapsNanite` / `RenderVirtualShadowMapsNonNanite` | `Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp` / `.h` | VSM page allocation 与 Nanite / classic mesh 渲染分支 |
| `MegaLights::GetMegaLightsMode` / `GenerateMegaLightsSamples` / `RenderMegaLights` | `Renderer/Private/MegaLights/MegaLights.cpp` | MegaLights 模式判断、sample 生成、lighting resolve 在帧内的位置 |
| `VirtualShadowMapMarkLightSamples` / `VirtualShadowMapTraceLightSamples` / `ScreenSpaceRayTraceLightSamples` | `Renderer/Private/MegaLights/MegaLightsRayTracing.cpp` | MegaLights 对 VSM sample page marking、VSM trace、screen-space trace 的入口 |
| `GetLightContactShadowParameters` / `GetDeferredLightParameters` | `Renderer/Private/LightRendering.cpp` / `Renderer/Private/LightRendering.h` | Contact Shadow length/intensity 写入 deferred light uniform |
| `ShadowRayCast` / `ApplyContactShadowWithShadowTerms` / `GetDynamicLighting` | `Engine/Shaders/Private/DeferredLightingCommon.ush` | Deferred lighting shader 内联 contact shadow ray 与 `FShadowTerms` 修正 |
| `RenderScreenSpaceShadows` / `ScreenSpaceShadows.usf` | `Renderer/Private/Shadows/ScreenSpaceShadows.cpp` / `Engine/Shaders/Private/ScreenSpaceShadows.usf` | standalone/mobile screen-space shadow mask 路径 |

---

## 30. 光照计算（12_Lighting 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 中 light gather / grid / deferred lighting 顺序 | `Renderer/Private/DeferredShadingRenderer.cpp` | `GatherAndSortLights` task、`PrepareForwardLightData`、`RenderLights`、MegaLights/volumetric 交接点 |
| `FSceneRenderer::GatherAndSortLights` | `Renderer/Private/LightRendering.cpp` / `Renderer/Private/SceneRendering.h` | 过滤可见 light、排序、划分 batched / unbatched / clustered / MegaLights ranges |
| `FLightSceneInfo::ShouldRenderLight` | `Renderer/Private/LightSceneInfo.cpp` / `.h` | 每个 view 是否需要渲染某盏光的基础 gating |
| `FSceneRenderer::PrepareForwardLightData` | `Renderer/Private/LightGridInjection.cpp` | 创建 forward / clustered light data，准备 per-view uniform 与 culled light buffers |
| `FSceneRenderer::ComputeLightGrid` | `Renderer/Private/LightGridInjection.cpp` | 按 3D light grid 注入 local lights，产出 clustered / forward light grid |
| `FLightGridInjectionCS` / `LightGridInjectionCS` | `Renderer/Private/LightGridInjection.cpp` / `Engine/Shaders/Private/LightGridInjection.usf` | compute shader 侧将 local lights 写入 light grid cells |
| `ComputeLightGridCellIndex` / `ComputeLightGridCellCoordinate` | `Engine/Shaders/Private/LightGridCommon.ush` | shader 侧按屏幕像素与 depth 定位 3D light grid cell |
| `FDeferredShadingSceneRenderer::AddClusteredDeferredShadingPass` | `Renderer/Private/ClusteredDeferredShadingPass.cpp` / `.h` | clustered deferred full-screen pass，根据 grid 枚举 cell 内 local lights |
| `ClusteredDeferredShadingPixelShader.usf` | `Engine/Shaders/Private/ClusteredDeferredShadingPixelShader.usf` | 读取 light grid header / local light data，按 shading model 进入 lighting |
| `FDeferredShadingSceneRenderer::RenderLights` | `Renderer/Private/LightRendering.cpp` / `Renderer/Private/DeferredShadingRenderer.h` | standard deferred lighting 的逐光源循环入口 |
| `RenderLight` / `InternalRenderLight` | `Renderer/Private/LightRendering.cpp` | 根据光类型、stencil/depth bounds、Substrate tile、permutation 绘制 light volume 或全屏光照 |
| `DeferredLightPixelMain` / `GetDynamicLighting` / `IntegrateBxDF` | `Engine/Shaders/Private/DeferredLightPixelShaders.usf` / `DeferredLightingCommon.ush` / `ShadingModels.ush` | GBuffer + light data + shadow terms 进入 BRDF，并按 `ShadingModelID` 分支 |
| `GetLightOcclusionType` | `Renderer/Private/LightRendering.cpp` | Lighting 阶段判断 shadowmap / MegaLights / ray tracing 等 occlusion 输入的入口 |

---

## 31. 体积效果与大气（13_Atmosphere 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 中 atmosphere / fog / cloud 顺序 | `Renderer/Private/DeferredShadingRenderer.cpp` | Sky LUT、VolumetricFog、HeightFog、LocalFogVolume、VolumetricCloud 与 `SceneColor` 合成顺序 |
| `ShouldRenderSkyAtmosphere` | `Renderer/Private/SkyAtmosphereRendering.cpp` / `.h` | Sky atmosphere 渲染 gating |
| `FSceneRenderer::RenderSkyAtmosphereLookUpTables` | `Renderer/Private/SkyAtmosphereRendering.cpp` / `Renderer/Private/SceneRendering.h` | Transmittance / multi-scattering / sky view / aerial perspective 等 LUT RDG 资源生成 |
| `FSkyAtmospherePendingRDGResources::CommitToSceneAndViewUniformBuffers` | `Renderer/Private/SkyAtmosphereRendering.cpp` / `.h` | 将 pending RDG LUT 转为 scene/view uniform 可读资源 |
| `FSceneRenderer::RenderSkyAtmosphere` | `Renderer/Private/SkyAtmosphereRendering.cpp` | 使用 LUT、cloud shadow / AO、scene textures 合成天空/大气到当前 scene color |
| `ShouldRenderVolumetricFog` / `GetVolumetricFogGridZParams` / `ComputeVolumetricFog` | `Renderer/Private/VolumetricFog.cpp` / `.h` / `VolumetricFogShared.h` | Volumetric Fog gating、froxel Z 分布、VBuffer / light scattering / history / final integration |
| `SetupFogUniformParameters` / `FDeferredShadingSceneRenderer::RenderFog` | `Renderer/Private/FogRendering.cpp` / `.h` | Height Fog 解析参数打包与 fullscreen fog compose |
| `HeightFogCommon.ush` / `HeightFogPixelShader.usf` | `Engine/Shaders/Private/` | shader 侧 Height Fog 解析近似与 volumetric/local fog combine |
| `ShouldRenderLocalFogVolume` / `InitLocalFogVolumesForViews` / `RenderLocalFogVolume` | `Renderer/Private/LocalFogVolumeRendering.cpp` / `.h` | Local Fog Volume culling、GPU instance buffer、在 volumetric fog 或 height fog 后的合成接入 |
| `ShouldRenderVolumetricCloud` / `FSceneRenderer::RenderVolumetricCloud` | `Renderer/Private/VolumetricCloudRendering.cpp` / `.h` | Volumetric Cloud render target / per-pixel tracing 入口，消费 sky/fog/light/shadow 输入 |
| `ReconstructVolumetricRenderTarget` / `ComposeVolumetricRenderTargetOverScene` | `Renderer/Private/VolumetricRenderTarget.cpp` / `.h` | Volumetric cloud VRT 重建与覆盖到 `SceneColor` 的合成路径 |
| `VolumetricFog.usf` / `VolumetricCloud.usf` / `VolumetricRenderTarget.usf` | `Engine/Shaders/Private/` | froxel integration、cloud ray marching、VRT reconstruct/compose 的 shader 侧锚点 |

---

## 32. 半透明（14_Translucency 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 中 translucency block | `Renderer/Private/DeferredShadingRenderer.cpp` | TLV、FrontLayer、RT translucency、OIT、raster translucency、distortion、post-process handoff 的主时序 |
| `ETranslucencyPass` / `FTranslucencyPassResourcesMap` | `Renderer/Internal/TranslucentPassResource.h` | 半透明阶段和 separate translucency resources 的生命周期边界 |
| `FSceneRenderer::ShouldRenderTranslucency` / `RenderTranslucency` / `RenderTranslucencyInner` | `Renderer/Private/TranslucentRendering.cpp` | 半透明 pass gating、资源分配、per-view draw、resource map 写入 |
| `RenderTranslucencyViewInner` | `Renderer/Private/TranslucentRendering.cpp` | per-view translucent draw command 提交入口 |
| `FBasePassMeshProcessor::ShouldDraw` / `SetTranslucentRenderState` | `Renderer/Private/BasePassRendering.cpp` | translucent material 按 `ETranslucencyPass` 路由，并设置 blend/depth/stencil 状态 |
| `CalculateTranslucentMeshStaticSortKey` / `UpdateTranslucentMeshSortKeys` | `Renderer/Private/BasePassRendering.cpp` / `Renderer/Private/MeshDrawCommands.cpp` | per-object translucent sort key 与 per-view distance/axis/projected-Z 更新 |
| `OIT::CreateOITData` / `OIT::AddOITComposePass` | `Renderer/Private/OIT/OIT.cpp` / `.h` | sorted pixels OIT buffer 创建、写入、compose |
| `OITCombine.usf` / `BasePassPixelShader.usf` OIT 分支 | `Engine/Shaders/Private/OITCombine.usf` / `Engine/Shaders/Private/BasePassPixelShader.usf` | shader 侧 OIT sample 写入与合成 |
| `FDeferredShadingSceneRenderer::RenderTranslucencyLightingVolume` | `Renderer/Private/TranslucentLighting.cpp` | TLV light injection/filter/history，供 translucent base pass 采样 |
| `FDeferredShadingSceneRenderer::RenderDistortion` | `Renderer/Private/DistortionRendering.cpp` / `Engine/Shaders/Private/DistortApplyScreenPS.usf` | distortion accumulate / apply / merge 到 `SceneColor` |
| `FDeferredShadingSceneRenderer::RenderFrontLayerTranslucency` | `Renderer/Private/Lumen/LumenFrontLayerTranslucency.cpp` / `.h` | 捕获 front-most translucent normal/depth，供 Lumen/RT translucency 使用 |
| `RenderRayTracedTranslucency` / `RenderRayTracingTranslucency` | `Renderer/Private/Lumen/LumenReflections.cpp` / `Renderer/Private/RayTracing/RayTracingTranslucency.cpp` | Lumen RT translucency 与 legacy full-pipeline RT translucency 两条接入 `SceneColor` 的路径 |

---

## 33. 后处理链（15_PostProcessing 终审索引；当前新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 尾部后处理顺序 | `Renderer/Private/DeferredShadingRenderer.cpp` | `AddResolveSceneColorPass`、debug/postprocessing 分支、`OnRenderFinish`、反馈/清理的主时序 |
| `TryCreateViewFamilyTexture` | `Renderer/Private/ScreenPass.cpp` | 创建或注册 ViewFamily 输出纹理，作为后处理最终输出目标 |
| `FScreenPassRenderTarget::CreateViewFamilyOutput` | `Renderer/Private/ScreenPass.cpp` / `Renderer/Private/PostProcess/PostProcessing.cpp` | 将 ViewFamilyTexture 包装为 post-processing override output |
| `AddResolveSceneColorPass` | `Renderer/Private/SceneRendering.cpp` / `.h` | MSAA scene color resolve，为后处理读取 `SceneColor` 做准备 |
| `AddPostProcessingPasses` | `Renderer/Private/PostProcess/PostProcessing.cpp` / `.h` | 后处理链总入口，组织 TSR、Bloom、Tonemap、后处理材质、visualize 等 pass |
| `TOverridePassSequence` | `Renderer/Internal/OverridePassSequence.h` / `Renderer/Private/PostProcess/PostProcessing.cpp` | 管理 override output pass 序列，决定哪些 pass 直接写 ViewFamily 输出 |
| `AddMainTemporalSuperResolutionPasses` / `AddTemporalSuperResolutionPasses` | `Renderer/Private/PostProcess/TemporalSuperResolution.cpp` / `Renderer/Private/PostProcess/TemporalAA.h` | TSR 主入口与 history extraction / 输出 texture 的时间累积位置 |
| `AddBloomSetupPass` / `AddGaussianBloomPasses` | `Renderer/Private/PostProcess/PostProcessBloomSetup.cpp` / `.h` | Bloom setup、downsample chain、Gaussian blur 组织 |
| `FTextureDownsampleChain::Init` / `AddGaussianBlurPass` | `Renderer/Private/PostProcess/PostProcessDownsample.cpp` / `PostProcessWeightedSampleSum.cpp` | 多级下采样链与 separable blur pass 的源码入口 |
| `AddCombineLUTPass` / `PostProcessCombineLUTs.usf` | `Renderer/Private/PostProcess/PostProcessCombineLUTs.cpp` / `.h` / `Engine/Shaders/Private/PostProcessCombineLUTs.usf` | 色彩校正 LUT 合成，供 tonemap pass 消费 |
| `AddTonemapPass` / `TonemapCommon.ush` | `Renderer/Private/PostProcess/PostProcessTonemap.cpp` / `.h` / `Engine/Shaders/Private/TonemapCommon.ush` | ToneMapping / ACES 输出变换与最终 LDR/HDR 输出写入 |
| `AddDebugViewPostProcessingPasses` | `Renderer/Private/PostProcess/PostProcessing.cpp` / `.h` | Debug/Visualize 后处理链入口，仍使用 ViewFamily output 约束 |
| `FSceneRenderer::OnRenderFinish` | `Renderer/Private/SceneRendering.cpp` / `.h` | 后处理完成后提取 ViewFamilyTexture、反馈资源、执行一帧收尾 |
| `r.HDR.Aces.Version` / `FUseACES2` | `Renderer/Private/PostProcess/PostProcessTonemap.cpp` | 终审补充确认 UE5.7 默认 ACES version 为 1，`FUseACES2` 条件为 `r.HDR.Aces.Version > 1`。 |
---

> 2026-06-25 状态说明：第 34-42 节已随 `16_Nanite.md` 至 `24_Optimization.md` 的 Claude 教学优化与 Codex 最终事实回归同步为当前终审索引。详细核对记录仍以各章 CoverageMatrix 为准，本文件只保留可复用事实入口与调试索引。

## 34. Nanite 虚拟几何体（终审索引 / 当前 16_Nanite 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `Nanite::FResources` / `FPackedHierarchyNode` / `FPackedCluster` | `Engine/Public/Rendering/NaniteResources.h` | Nanite cooked resource、层级节点、cluster 压缩数据与 baked LOD error 的源头 |
| `Nanite::FResources::InitResources` / `Nanite::FSceneProxy` | `Engine/Private/Rendering/NaniteResources.cpp` | 资源初始化、Nanite scene proxy 创建和 render-thread resource 检查 |
| `Nanite::FStreamingManager::Add` / `Remove` / `AddPendingGPURequests` / `AddRequest` | `Engine/Private/Rendering/NaniteStreamingManager.cpp` | runtime resource id / hierarchy offset / root page 分配、GPU streaming request readback 和 parent dependency 请求 |
| `UStaticMeshComponent::ShouldCreateNaniteProxy` / `CreateStaticMeshSceneProxy` | `Engine/Private/StaticMeshSceneProxy.cpp` | StaticMesh proxy 在 Nanite proxy 与经典 static mesh proxy 之间分叉 |
| `FPrimitiveSceneProxy::BuildUniformShaderParameters` | `Engine/Private/PrimitiveSceneProxy.cpp` | 将 `NaniteResourceID` / `NaniteHierarchyOffset` 等资源索引写入 primitive shader parameters |
| `Nanite::FPackedView::UpdateLODScales` | `Renderer/Private/Nanite/NaniteShared.cpp` | 将 view rect / projection / CVar / quality scale 转成 GPU LOD scale |
| `ShouldVisitChildInternal` / `SmallEnoughToDraw` / `EmitVisibleCluster` | `Engine/Shaders/Private/Nanite/NaniteClusterCulling.usf` | shader 侧 hierarchy traversal、cluster cut、visible cluster 输出和 streaming request 触发 |
| `Nanite::FRenderer::DrawGeometry` / `AddPass_PrimitiveFilter` / `AddPass_InstanceHierarchyAndClusterCull` / `AddPass_Rasterize` | `Renderer/Private/Nanite/NaniteCullRaster.cpp` | Nanite 每帧 primitive/instance filtering、two-pass culling、HW/SW raster 调度主线 |
| `Nanite::EmitDepthTargets` | `Renderer/Private/Nanite/NaniteComposition.cpp` | 将 Nanite raster results 导出为 scene depth / shading mask 等消费者输入 |
| `BuildShadingCommands` / `ShadeBinning` / `DispatchBasePass` | `Renderer/Private/Nanite/NaniteShading.cpp` | Nanite material shade binning 与 BasePass compute shading 到 GBuffer 的路径 |
| `FNaniteMaterialListContext::AddShadingBin` | `Renderer/Private/Nanite/NaniteDrawList.cpp` / `Renderer/Private/Nanite/NaniteMaterials.h` | material section 到 Nanite shading bin / material slot 的映射 |

---

## 35. Lumen 动态全局光照（终审索引 / 当前 17_Lumen 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FDeferredShadingSceneRenderer::Render` 中 Lumen 接入点 | `Renderer/Private/DeferredShadingRenderer.cpp` | `FLumenSceneFrameTemporaries`、scene update、surface cache feedback、scene lighting、indirect composite 的帧内时序 |
| `FLumenCardScene` / `FLumenCard` / `FLumenPageTableEntry` / `FLumenSceneFrameTemporaries` | `Renderer/Private/Lumen/LumenSceneData.h` | Surface Cache 的 card/page/atlas 数据形态和每帧 RDG temporaries |
| `FLumenSceneData::FillFrameTemporaries` | `Renderer/Private/Lumen/LumenSceneRendering.cpp` | 将持久 pooled buffers/textures 导入当前 RDG graph |
| `BeginUpdateLumenSceneTasks` / `UpdateLumenScene` | `Renderer/Private/Lumen/LumenSceneRendering.cpp` | Lumen scene 增量更新、card capture setup 和 scene data 生命周期入口 |
| `CopyCardsToSurfaceCache` / dilation 相关 pass | `Renderer/Private/Lumen/LumenSurfaceCache.cpp` | 将 card capture 结果写入 Surface Cache atlas 并处理边界扩张 |
| `RenderLumenSceneLighting` / `BuildCardUpdateContext` | `Renderer/Private/Lumen/LumenSceneLighting.cpp` | Surface Cache direct / indirect / final lighting atlas 更新和 card priority 上下文 |
| `BeginGatheringLumenSurfaceCacheFeedback` / `FinishGatheringLumenSurfaceCacheFeedback` | `Renderer/Private/Lumen/LumenSurfaceCacheFeedback.cpp` | Surface Cache feedback readback 与跨帧更新闭环 |
| `RenderLumenScreenProbeGather` | `Renderer/Private/Lumen/LumenScreenProbeGather.cpp` | Screen Probe placement、final gather 输出和 `FSSDSignalTextures` 入口 |
| `TraceScreenProbes` | `Renderer/Private/Lumen/LumenScreenProbeTracing.cpp` | screen trace、SDF / voxel / radiance cache fallback 的 Screen Probe trace 组织 |
| `FilterScreenProbes` / `InterpolateAndIntegrate` | `Renderer/Private/Lumen/LumenScreenProbeFiltering.cpp` | probe radiance temporal/spatial filtering 与 per-pixel integration |
| `LumenRadianceCache::UpdateRadianceCaches` | `Renderer/Private/Lumen/LumenRadianceCache.cpp` / `.h` | world-space clipmap probe cache、indirection texture 和 Screen Probe / reflection 消费参数 |
| `RenderLumenReflections` / `TraceReflections` | `Renderer/Private/Lumen/LumenReflections.cpp` / `Renderer/Private/Lumen/LumenReflectionTracing.cpp` | Lumen reflection tracing / resolve，与 Screen Probe rough specular 区分 |
| `DispatchAsyncLumenIndirectLightingWork` / `RenderDiffuseIndirectAndAmbientOcclusion` / `FDiffuseIndirectCompositePS` | `Renderer/Private/IndirectLightRendering.cpp` | Lumen diffuse indirect / AO 输出合成回 `SceneColor` 的路径 |

---

## 36. MegaLights 随机多光源（终审索引 / 当前 18_MegaLights 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `MegaLights` CVar / `ShouldCompileShaders` / `IsRequested` / `HasRequiredTracingData` / `IsEnabled` | `Renderer/Private/MegaLights/MegaLights.cpp` | MegaLights project、platform、view、ray tracing data gating 与默认开关 |
| `MegaLights::GetMegaLightsMode` | `Renderer/Private/MegaLights/MegaLights.cpp` | 每盏 light 是否交给 MegaLights 的模式判定 |
| `FSceneRenderer::GatherAndSortLights` | `Renderer/Private/LightRendering.cpp` / `Renderer/Private/SceneRendering.h` | 过滤可见 light 并将 MegaLights light range 放到 sorted light 尾段 |
| `FLightSceneInfo` 中 `bHandledByMegaLights` / `MegaLightsLightStart` | `Renderer/Private/LightSceneInfo.h` | classic deferred lighting 与 MegaLights ownership 分流标记 |
| `FDeferredShadingSceneRenderer::RenderLights` | `Renderer/Private/LightRendering.cpp` | classic deferred light loop 在 MegaLights range 前停止 |
| `FMegaLightsViewContext::Setup` | `Renderer/Private/MegaLights/MegaLights.cpp` / `MegaLightsInternal.h` | downsample、sample buffer、tile list、indirect args、visible light hash 等固定预算资源形态 |
| `GenerateMegaLightsSamples` | `Renderer/Private/MegaLights/MegaLights.cpp` / `Renderer/Private/MegaLights/MegaLightsSampling.cpp` | 生成固定槽位 light samples 的 C++ 入口 |
| `GenerateLightSamplesCS` / `AddLightSample` | `Engine/Shaders/Private/MegaLights/MegaLightsSampling.usf` / `.ush` | shader 侧 light grid 候选枚举、PDF 权重和 weighted fixed-slot replacement |
| `VirtualShadowMapMarkLightSamples` / `VirtualShadowMapTraceLightSamples` / `ScreenSpaceRayTraceLightSamples` | `Renderer/Private/MegaLights/MegaLightsRayTracing.cpp` | VSM page marking / trace、screen trace、HWRT/SWRT shadow evidence 写回 sample buffer |
| `ShadeLightSamplesCS` / `RenderMegaLights` | `Renderer/Private/MegaLights/MegaLightsResolve.cpp` / `Renderer/Private/MegaLights/MegaLights.cpp` | selected samples 的 BRDF resolve 和 lighting 输出 |
| `DenoiseLighting` / `VisibleLightHashCS` | `Renderer/Private/MegaLights/MegaLightsDenoising.cpp` / `Renderer/Private/MegaLights/MegaLightsResolve.cpp` | temporal/spatial denoise、visible light hash 和下一帧 guiding history |
| `FMegaLightsViewState` | `Renderer/Private/MegaLights/MegaLightsViewState.h` | MegaLights view-state history 生命周期 |

---

## 37. Virtual Shadow Maps（终审索引 / 当前 19_VirtualShadowMaps 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| VSM page constants / page table data | `Engine/Shaders/Shared/VirtualShadowMapDefinitions.h` | 虚拟页、物理页、page table entry 和 shader/C++ 共享常量 |
| `FVirtualShadowMapArray` / `BuildPageAllocations` | `Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp` / `.h` | VSM array 生命周期、page allocation pass 顺序和物理页池管理入口 |
| `RenderVirtualShadowMapsNanite` / `RenderVirtualShadowMapsNonNanite` | `Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp` / `.h` | requested physical pages 的 Nanite / classic mesh 渲染分支 |
| `FVirtualShadowMapClipmap` | `Renderer/Private/VirtualShadowMaps/VirtualShadowMapClipmap.cpp` / `.h` | directional light clipmap level、snapping、guardband 和 page address offset 管理 |
| `FVirtualShadowMapCacheManager` | `Renderer/Private/VirtualShadowMaps/VirtualShadowMapCacheManager.cpp` / `.h` | 跨帧 page cache、clipmap/local invalidation、primitive invalidation 和 pool resize 失效 |
| `FShadowSceneRenderer::DispatchVirtualShadowMapViewAndCullingSetup` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` / `.h` | compact VSM views、culling setup 和 shadow view 参数准备 |
| `FShadowSceneRenderer::BeginMarkVirtualShadowMapPages` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` / `.h` | BasePass 后 pixel/froxel/hair/water/MegaLights 等 receiver page marking 入口 |
| `FShadowSceneRenderer::RenderVirtualShadowMaps` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` | build page allocations 后调度 Nanite / non-Nanite 写 physical pages |
| `GetLightOcclusionType` / deferred lighting VSM uniforms | `Renderer/Private/LightRendering.cpp` / `.h` | deferred lighting 阶段选择 VSM occlusion、传递 VSM id / mask bit / uniform |
| `VirtualShadowMapPageAccessCommon.ush` | `Engine/Shaders/Private/VirtualShadowMaps/` | shader 侧 virtual-to-physical page table lookup、mip/clipmap fallback 采样基础 |
| `VirtualShadowMapPageMarking.usf` | `Engine/Shaders/Private/VirtualShadowMaps/` | receiver-driven page request 标记 |
| `VirtualShadowMapPhysicalPageManagement.usf` / `VirtualShadowMapPageManagement.usf` | `Engine/Shaders/Private/VirtualShadowMaps/` | physical page list、cache reuse、新映射和 hierarchical flags 更新 |
| `VirtualShadowMapProjectionCommon.ush` | `Engine/Shaders/Private/VirtualShadowMaps/` | lighting shader 中 VSM projection / sampling 的共享逻辑 |
| `MegaLights::IsMarkingVSMPages` / `FPruneLightGridCS::bIncludeMegaLights` | `Renderer/Private/MegaLights/*` / `Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp` | 终审补充确认 MegaLights 单独标 VSM pages 时，常规 light grid prune 路径通过 `bIncludeMegaLights` 排除 MegaLights 灯。 |
| `AddLocalLightShadow` distant / one-pass local shadow path | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` | 终审补充确认 local light VSM 的 distant single-page 判定与 point light one-pass cube face 路径，供第 19 篇解释局部光页需求来源。 |
---

## 38. 材质编译管线（终审索引 / 当前 20_MaterialPipeline 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `UMaterial::CacheResourceShadersForRendering` / `CacheShadersForResources` / `UMaterial::CompilePropertyEx` | `Engine/Private/Materials/Material.cpp` | UMaterial 触发 shader resource 缓存和属性编译的 game-thread 入口 |
| `UMaterialInterface::CompileProperty` / `FExpressionInput::Compile` | `Engine/Private/Materials/MaterialShared.cpp` | material property 编译派发到 expression graph 的入口 |
| `FMaterial::BuildShaderMapIdOverride` / `CacheShaders` / `BeginCacheShaders` / `FinishCacheShaders` | `Engine/Private/Materials/MaterialShared.cpp` | material shader map id、内存/DDC 查找、缺失后编译和完成回填流程 |
| `FMaterial::Translate` / `Translate_New` / `Translate_Legacy` | `Engine/Private/Materials/MaterialShared.cpp` | UE5.7 新 Material IR generator 与 legacy HLSL translator 分支，Substrate 时回退 legacy |
| `FMaterialShaderMapId` / `FMaterialShaderMap` / `FMeshMaterialShaderMap` | `Engine/Public/MaterialShared.h` | material 编译身份、shader map content 和 per-vertex-factory mesh shader map |
| `FHLSLMaterialTranslator::Translate` / `TranslateMaterial` / `GetSharedInputsMaterialCode` / `GetMaterialShaderCode` | `Engine/Private/Materials/HLSLMaterialTranslator.cpp` | legacy 节点图到 material HLSL / `PixelMaterialInputs` 的转换路径 |
| `FMaterialIRModuleBuilder::Build` | `Engine/Private/Materials/MaterialIRModuleBuilder.cpp` | 新 Material IR generator 的 IR module 构建入口 |
| `FMaterialIRToHLSLTranslation::Run` | `Engine/Private/Materials/MaterialIRToHLSLTranslator.cpp` | Material IR 到 HLSL 的转换入口 |
| `FMaterialSourceTemplate::BeginResolve` | `Engine/Private/Materials/MaterialSourceTemplate.cpp` | material source template 解析和 generated include 组装 |
| `FMaterialShaderMap::Compile` / `SubmitCompileJobs` / `ProcessCompilationResultsForSingleJob` / `SaveToDerivedDataCache` | `Engine/Private/Materials/MaterialShader.cpp` | shader compile jobs、结果回填和 DDC 保存主线 |
| `FMaterialShaderMapLayoutCache::CreateLayout` / `FMaterialShaderMap::IsComplete` | `Engine/Private/Materials/MaterialShader.cpp` | shader type / permutation / vertex factory layout 构造和完整性验证 |
| `FMeshMaterialShaderType::ShouldCompileVertexFactoryPermutation` / `PrepareMeshMaterialShaderCompileJob` | `Engine/Private/Materials/MeshMaterialShader.cpp` | vertex factory 维度参与 material shader permutation 和 compile environment 的入口 |
| `IMPLEMENT_MATERIAL_SHADER_TYPE` / `GetUniformBasePassShaders` / `GetBasePassShaders` / `FBasePassMeshProcessor::Process` | `Renderer/Private/BasePassRendering.cpp` | BasePass shader type 注册和运行时从 material shader map 取 shader 的消费路径 |
| `MaterialTemplate.ush` / `BasePassPixelShader.usf` | `Engine/Shaders/Private/` | generated material include 的模板与 BasePass 消费 `CalcMaterialParameters` 的 shader 入口 |
| `FMaterial::Translate` new HLSL generator gate | `Engine/Private/Materials/MaterialShared.cpp` | 终审补充确认新 generator 条件为 `bUsingNewHLSLGenerator && !Substrate::IsSubstrateEnabled()`；Substrate 启用时回退 legacy translator。 |
---

## 39. Shader 系统（终审索引 / 当前 21_ShaderSystem 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FShaderType` / `DECLARE_SHADER_TYPE` / `IMPLEMENT_SHADER_TYPE` / `SHADER_TYPE_LIST` | `RenderCore/Public/Shader.h` | C++ shader class 注册为 shader meta type 的核心入口 |
| `FShaderTypeRegistration::CommitAll` / `FShaderType::ShouldCompilePermutation` / `FShaderType::ModifyCompilationEnvironment` | `RenderCore/Private/Shader.cpp` | shader type 静态注册提交、permutation 过滤和编译环境修改 |
| `InitializeShaderTypes` | `RenderCore/Private/ShaderCore.cpp` | 引擎初始化期收拢 shader type / vertex factory type / pipeline type 的时序边界 |
| `FGlobalShaderType` / `FGlobalShader` / `DECLARE_GLOBAL_SHADER` / `IMPLEMENT_GLOBAL_SHADER` | `RenderCore/Public/GlobalShader.h` | global shader 声明、注册和 global shader map 查询模型 |
| `FMaterialShaderType` / `IMPLEMENT_MATERIAL_SHADER_TYPE` | `Engine/Public/MaterialShaderType.h` / `Renderer/Private/BasePassRendering.cpp` | material shader type 的注册和 BasePass shader type 使用入口 |
| `FMeshMaterialShaderType` | `Engine/Public/MeshMaterialShaderType.h` | 同时依赖 material 与 vertex factory 的 mesh material shader type |
| `FMaterialShaderType::BeginCompileShader` / `FMaterialShaderMapLayoutCache::CreateLayout` / `FMaterialShaderMap::SubmitCompileJobs` | `Engine/Private/Materials/MaterialShader.cpp` | material shader map layout、compile job 创建和提交主线 |
| `FMeshMaterialShaderType::ShouldCompileVertexFactoryPermutation` / `PrepareMeshMaterialShaderCompileJob` | `Engine/Private/Materials/MeshMaterialShader.cpp` | vertex factory 维度参与 mesh material shader permutation 和 compile environment |
| `FVertexFactoryType` / `FVertexFactoryShaderParameters` / `IMPLEMENT_VERTEX_FACTORY_PARAMETER_TYPE` | `RenderCore/Public/VertexFactory.h` | VertexFactory 作为顶点输入策略、shader permutation 维度和运行时绑定提供者 |
| `FLocalVertexFactoryShaderParameters` / `IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLocalVertexFactoryUniformShaderParameters, "LocalVF")` | `Engine/Private/LocalVertexFactory.cpp` | LocalVertexFactory 的 shader parameter 绑定、uniform struct 和 element binding 入口 |
| `BEGIN_SHADER_PARAMETER_STRUCT` / `SHADER_PARAMETER` / `SHADER_PARAMETER_RDG_*` | `RenderCore/Public/ShaderParameterMacros.h` | shader 参数 metadata 的声明宏，覆盖 loose 参数、UB、resource 和 RDG resource |
| `FShaderParameterStructBindingContext::Bind` / `SetShaderParameters` | `RenderCore/Private/ShaderParameterStruct.cpp` | 参数 metadata 与 HLSL parameter map 绑定，并在提交时写入 RHI batched parameters |
| `FRHIBatchedShaderParameters` | `RHI/Public/RHIShaderParameters.h` | draw/dispatch 提交阶段承载 shader 参数写入的 RHI 批量参数容器 |
| `TBasePassVS` / `TBasePassPS` | `Renderer/Private/BasePassRendering.h` | BasePass mesh material shader 的 vertex/pixel shader class |
| `GetBasePassShaders` / `FBasePassMeshProcessor::Process` | `Renderer/Private/BasePassRendering.cpp` | 从 material shader map 取 BasePass shader，并构建 mesh pass draw command 的入口 |
| `FMaterial::TryGetShaders` / `FMaterial::GetShader` | `Engine/Private/Materials/MaterialShared.cpp` | 运行时从 material shader map 查询 shader ref 和缺失 shader 的调试入口 |
| `FMeshPassProcessor::BuildMeshDrawCommands` | `Renderer/Public/MeshPassProcessor.inl` | 将 shader、VF、material、pass state 收敛为可提交 `FMeshDrawCommand` |
| `FMaterialShader::GetShaderBindings` / `FMeshMaterialShader::GetElementShaderBindings` | `Renderer/Private/ShaderBaseClasses.cpp` | material/VF/primitive/view 参数写入 draw command shader bindings 的路径 |
| `FMeshDrawCommand::SubmitDrawBegin` / `FMeshDrawShaderBindings::SetOnCommandList` | `Renderer/Private/MeshPassProcessor.cpp` | MeshDrawCommand 提交时绑定 PSO、shader bindings、vertex streams 和 RHI 参数 |
| `ShouldCompileVertexFactoryPermutation` caller composition | `Engine/Private/Materials/MeshMaterialShader.cpp` | 终审补充确认 `ShouldCompileVertexFactoryPermutation` 只回答 vertex-factory 侧条件；调用者还会与 shader type 的 `ShouldCompilePermutation` 结果组合。 |
---

## 40. Compute Shader（终审索引 / 当前 22_ComputeShader 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FGlobalShader` / `DECLARE_GLOBAL_SHADER` / `IMPLEMENT_GLOBAL_SHADER` / `GetGlobalShaderMap` | `RenderCore/Public/GlobalShader.h` | compute global shader 的 C++ 类型声明、注册和 shader map 查询 |
| `DepthCopy::FViewDepthCopyCS` / `AddViewDepthCopyCSPass` | `Renderer/Private/DepthCopy.cpp` | 最小真实 compute shader 范式：声明、取 shader、填 RDG 参数并 dispatch |
| `SHADER_USE_PARAMETER_STRUCT` / `SetShaderParameters` / `UnsetShaderUAVs` | `RenderCore/Public/ShaderParameterStruct.h` | shader parameter struct 绑定到 RHI 参数和 UAV 清理的执行入口 |
| `BEGIN_SHADER_PARAMETER_STRUCT` / `SHADER_PARAMETER_RDG_TEXTURE` / `SHADER_PARAMETER_RDG_TEXTURE_UAV` / `FRDGTextureAccess` / `FRDGBufferAccess` | `RenderCore/Public/ShaderParameterMacros.h` | compute shader 参数结构体作为 RDG resource manifest 的声明方式 |
| `FShaderPermutationBool` / `TShaderPermutationInt` / `TShaderPermutationSparseInt` / `TShaderPermutationDomain` | `RenderCore/Public/ShaderPermutation.h` | compute shader permutation domain 的声明和运行时查询维度 |
| `FComputeShaderUtils::GetGroupCount` / `GetGroupCountWrapped` / `ValidateGroupCount` | `RenderCore/Public/RenderGraphUtils.h` | 从数据尺寸换算 dispatch group count，并检查平台维度限制 |
| `FComputeShaderUtils::ValidateIndirectArgsBuffer` / `Dispatch` / `DispatchIndirect` / `AddPass` | `RenderCore/Public/RenderGraphUtils.h` | direct / indirect dispatch helper、参数清理、RDG AddPass 封装和校验 |
| `ERDGPassFlags::Compute` / `ERDGPassFlags::AsyncCompute` / `FRDGAsyncTask` | `RenderCore/Public/RenderGraphDefinitions.h` | RDG compute pass 类型、async compute eligibility 和 async task 标记 |
| `ERDGPassTaskMode` / `FRDGBarrierBatchBegin::SetUseCrossPipelineFence` | `RenderCore/Public/RenderGraphPass.h` | compute pass task mode 与 graphics/async compute 之间的 cross-pipeline fence 标记 |
| `FRDGBuilder::Compile` / `CompilePassBarriers` / `CollectPassBarriers` | `RenderCore/Private/RenderGraphBuilder.cpp` | RDG 编译阶段的 pass 裁剪、barrier 推导和 async compute fork/join |
| `FRHIComputeCommandList` / `SetComputePipelineState` / `DispatchComputeShader` / `DispatchIndirectComputeShader` | `RHI/Public/RHICommandList.h` | RHI compute command list 的 PSO 设置和 direct/indirect dispatch 录制入口 |
| `FRHICommandSetComputePipelineState::Execute` / `FRHICommandDispatchComputeShader::Execute` / `FRHICommandDispatchIndirectComputeShader::Execute` | `RHI/Public/RHICommandListCommandExecutes.inl` | 命令链回放阶段把 compute PSO 与 dispatch 命令落到 RHI context |
| `PipelineStateCache::GetAndOrCreateComputePipelineState` / `ExecuteSetComputePipelineState` | `RHI/Private/PipelineStateCache.cpp` | compute PSO cache 查询、创建和命令列表设置路径 |
| `FD3D12CommandContext::SetupDispatch` / `RHIDispatchComputeShader` / `RHIDispatchIndirectComputeShader` / `RHISetComputePipelineState` | `D3D12RHI/Private/D3D12Commands.cpp` | D3D12 后端把 UE compute command 转为 `Dispatch` / `ExecuteIndirect` 的落点 |
| `FComputeShaderUtils::AddPass` / `Dispatch` / `ValidateIndirectArgsBuffer` constraints | `RenderCore/Public/RenderGraphUtils.h` | 终审补充确认 direct/indirect dispatch helper 在 AddPass/Dispatch 前后承担 group count、indirect args buffer 和参数清理约束。 |
---

## 41. 调试工具与方法（终审索引 / 当前 23_Debugging 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `CVarRDGValidation` / `CVarImmediateMode` / `CVarRDGDebugFlushGPU` / `CVarRDGClobberResources` / `CVarRDGEvents` / `CVarRDGParallelExecute` | `RenderCore/Private/RenderGraphPrivate.cpp` | RDG validation、immediate、flush、clobber、event 和 parallel execute 调试开关 |
| `RDG_EVENT_NAME` / `RDG_EVENT_SCOPE_STAT` / `RDG_GPU_STAT_SCOPE` / `RDG_RHI_EVENT_SCOPE` | `RenderCore/Public/RenderGraphEvent.h` | RDG/RHI event、GPU stat 和 breadcrumb 名字写入的宏入口 |
| `FRDGUserValidation::ValidateCreateTexture` / `ValidateCreateBuffer` / `ValidateAddPass` / `ValidateExecuteBegin` / `ValidateExecuteEnd` | `RenderCore/Private/RenderGraphValidation.cpp` | RDG user validation 对资源创建、pass 添加和 graph 执行边界的检查 |
| `SetupPassDependencies` / `FlushCullStack` / `ClobberPassOutputs` / `CollectPassBarriers` | `RenderCore/Private/RenderGraphBuilder.cpp` | pass culling、resource clobber 和 barrier 编译的调试反查入口 |
| `IRenderCaptureProvider` / `FRenderCaptureInterface` | `RenderCore/Public/IRenderCaptureProvider.h` / `RenderCore/Private/RenderCaptureInterface.cpp` | RenderDoc/PIX 等 GPU capture provider 抽象与 main/render thread 边界 |
| `r.CaptureNextDeferredShadingRendererFrame` / `FScopedCapture` | `Renderer/Private/DeferredShadingRenderer.cpp` | deferred renderer 帧级 capture CVar 和 RDG/RHI/ENQUEUE capture 边界 |
| `Trace.File` / `Trace.Send` / `Trace.Stop` | `Core/Private/ProfilingDebugging/TraceAuxiliary.cpp` | Unreal Insights trace console command 的源码入口 |
| `TRACE_CPUPROFILER_EVENT_SCOPE*` | `Core/Public/ProfilingDebugging/CpuProfilerTrace.h` | CPU profiler scope 写入 Unreal Insights 的宏入口 |
| `FRendererModule::BeginRenderingViewFamily` | `Renderer/Private/SceneRendering.cpp` | ViewFamily 进入渲染线程前后的 CPU trace / frame boundary 定位入口 |
| `FRealtimeGPUProfiler` / `SCOPED_GPU_STAT` / `RHI_BREADCRUMB_EVENT*` | `RenderCore/Public/ProfilingDebugging/RealtimeGPUProfiler.h` | GPU stat scope、breadcrumb event 和旧 draw event 宏迁移边界 |
| `FGPUProfilerSink_StatSystem` / `GCommand_ProfileGPU` | `RHI/Private/GPUProfiler.cpp` | `stat gpu` / `ProfileGPU` 命令、报告和 profiler sink 入口 |
| `UEngine::HandleProfileGPUCommand` | `Engine/Private/UnrealEngine.cpp` | engine console command 层转入 GPU profiler 的路径 |
| `FVisualizeTexture` / `AddVisualizeTexturePass` | `RenderCore/Private/VisualizeTexture.cpp` | VisualizeTexture/Vis 命令和 RDG 可视化 pass 入口 |
| `RenderDebugViewMode` / `FDebugViewModeVS` / `FDebugViewModePS` | `Renderer/Private/DebugViewModeRendering.cpp` / `Renderer/Private/DebugViewModeRendering.h` | DebugView mode 的专用 mesh pass 和 shader 入口 |
| `EngineShowFlagOverride` / `FEngineShowFlags` debug flags | `Engine/Private/ShowFlags.cpp` / `Engine/Public/SceneView.h` | show flag 和 debug visualization 状态进入 renderer 的配置入口 |
| `FRHICommandListExecutor::LatchBypass` / `GetValidatedRenderCommandPipeMode` / `IsParallelExecuteEnabled` | `RHI/Private/RHICommandList.cpp` / `RenderCore/Private/RenderingThread.cpp` / `RenderCore/Private/RenderGraphPrivate.cpp` | RHI bypass、render command pipe 和 RDG parallel execute 运行时退化调试入口 |
| `UseGPUCrashDebugging` / `UseGPUCrashBreadcrumbs` | `RHI/Private/RHI.cpp` | GPU crash debugging 和 breadcrumb 默认/命令行选择路径 |
| `FVisualizeDim` / `ShouldCompilePermutation` / `GetBasePassShaders` / `FMaterial::TryGetShaders` | `Renderer/Private/BasePassRendering.h` / `Renderer/Private/BasePassRendering.cpp` / `Engine/Private/Materials/MaterialShared.cpp` | shader permutation 缺失和 BasePass shader lookup 失败的调试回溯路径 |
| `RDG_ENABLE_DEBUG` / RDG capture and ProfileGPU defaults | `RenderCore/Public/RenderGraphDefinitions.h` / `RenderCore/Private/RenderCaptureInterface.cpp` / `RHI/Private/GPUProfiler.cpp` | 终审补充确认 `RDG_ENABLE_DEBUG = (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)`，capture provider 有 main/render thread 约束，ProfileGPU 默认路径会影响并行/async 形态。 |
---

## 42. 性能分析与优化（终审索引 / 当前 24_Optimization 新稿已通过 Codex 最终事实回归，2026-06-25）

| 符号 | 文件 | 说明 |
|------|------|------|
| `FStatUnitData::DrawStat` / `STAT_Unit*` | `Engine/Private/UnrealClient.cpp` / `Engine/Public/EngineStats.h` | `stat unit` 的 Game / Render / RHI / GPU / Frame 时间分类来源 |
| `FGPUProfilerSink_StatSystem` / `GCommand_ProfileGPU` | `RHI/Private/GPUProfiler.cpp` | `stat gpu`、`ProfileGPU` 和 GPU profiler 报告的命令入口 |
| `RDG_EVENT_SCOPE` / `RDG_EVENT_SCOPE_STAT` / `RDG_GPU_STAT_SCOPE` / `RDG_RHI_EVENT_SCOPE` | `RenderCore/Public/RenderGraphEvent.h` | 把 GPU/RDG event 名字映射回源码阶段的宏入口 |
| `TRACE_CPUPROFILER_EVENT_SCOPE*` | `Core/Public/ProfilingDebugging/CpuProfilerTrace.h` | Unreal Insights CPU scope 写入入口 |
| `FDeferredShadingSceneRenderer::Render` | `Renderer/Private/DeferredShadingRenderer.cpp` | 一帧 deferred 渲染主线和 `VisibilityCommands`、`GPUSceneUpdate`、PrePass、BasePass、Lighting、PostProcessing、FrameRenderFinish 等 scope 的组织位置 |
| `BeginInitViews` / `EndInitViews` / `GatherDynamicMeshElements` / `WaitForVisibilityTasks` | `Renderer/Private/SceneVisibility.cpp` | visibility、view relevance、dynamic mesh 收集和任务等待的性能定位入口 |
| `FSceneRenderer::SetupMeshPass` / `FMeshPassProcessor` / `BuildMeshDrawCommands` | `Renderer/Private/SceneRendering.cpp` / `Renderer/Public/MeshPassProcessor.h` / `Renderer/Private/MeshDrawCommands.cpp` | MeshDrawCommand 构建、cached command、dynamic instancing 和 parallel pass setup 的成本入口 |
| `FInstanceCullingContext::BuildRenderingCommands` / `SubmitDrawCommands` | `Renderer/Private/InstanceCulling/InstanceCullingContext.cpp` | GPU-driven instance culling、occlusion culling 和 draw command submit 的成本入口 |
| `FGPUScene::UpdateInternal` / `UploadGeneral` / `UploadDynamicPrimitiveShaderDataForViewInternal` | `Renderer/Private/GPUScene.cpp` | GPUScene 增量更新、general upload 和 per-view dynamic primitive 上传成本 |
| `RenderPrePass` / `CVarEarlyZPass` / `FScene::GetEarlyZPassMode` | `Renderer/Private/DepthRendering.cpp` / `Renderer/Private/RendererScene.cpp` | EarlyZ / PrePass 模式、depth 写入和后续 HZB/BasePass 依赖的实验入口 |
| `RenderBasePass` / `RenderBasePassInternal` / `FBasePassMeshProcessor::Process` | `Renderer/Private/BasePassRendering.cpp` | BasePass / GBuffer 写入规模、材质复杂度和 mesh command 消费入口 |
| `GatherAndSortLights` / `RenderLights` / `RenderLight` / `InternalRenderLight` | `Renderer/Private/LightRendering.cpp` | deferred lighting 的 light sorting、batched/unbatched/standard/clustered 成本分流 |
| light grid injection CVars / HZB cull / async experiment | `Renderer/Private/LightGridInjection.cpp` | tiled/clustered light grid 的 pixel size、Z slice、HZB cull 和 async compute 实验入口 |
| `FShadowSceneRenderer::*VirtualShadowMap*` / `FVirtualShadowMapArray::BuildPageAllocations` | `Renderer/Private/Shadows/ShadowSceneRenderer.cpp` / `Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp` | VSM page marking、allocation、Nanite/non-Nanite rendering 和 physical page pool 成本入口 |
| `Nanite::FRenderer::DrawGeometry` / `AddPass_InstanceHierarchyAndClusterCull` / `AddPass_Rasterize` | `Renderer/Private/Nanite/NaniteCullRaster.cpp` | Nanite instance/cluster culling、raster 和 GPU-driven geometry 成本入口 |
| `BuildShadingCommands` / `ShadeBinning` / `DispatchBasePass` | `Renderer/Private/Nanite/NaniteShading.cpp` | Nanite material shade binning 与 BasePass compute shading 成本入口 |
| `LumenSceneUpdate` / `LumenSceneLighting` / `LumenScreenProbeGather` / `UpdateRadianceCaches` / `LumenReflections` | `Renderer/Private/Lumen/*` | Lumen scene update、surface cache、screen probe、radiance cache 和 reflections 的成本分解入口 |
| `AddPostProcessingPasses` / `TOverridePassSequence<EPass>` | `Renderer/Private/PostProcess/PostProcessing.cpp` | mutable SceneColor 后处理链、pass sequence、TSR/Bloom/Tonemap/PostProcessMaterial/debug pass 成本入口 |
| `AddMainTemporalSuperResolutionPasses` / `DECLARE_GPU_STAT(TemporalSuperResolution)` | `Renderer/Private/PostProcess/TemporalSuperResolution.cpp` | TSR 主 pass、history extraction 和 GPU stat scope 的性能定位入口 |

---

## 更新日志

| 日期 | 更新内容 |
|------|---------|
| 2026-06-28 | 03_ThreadModel 当前正文已按“新概念先解释、是什么和为什么讲透、补案例辅助说明”的新教学标准同步为质量完成、复审完成、正式完成；`03_ThreadModel.md`、`03_ThreadModel_CoverageMatrix.md`、`03_ThreadModel_TeachingEditReport.md` 与 `OUTLINE.md` 状态统一为完成。 |
| 2026-06-25 | 16_Nanite / 17_Lumen / 18_MegaLights / 19_VirtualShadowMaps / 20_MaterialPipeline / 21_ShaderSystem / 22_ComputeShader / 23_Debugging / 24_Optimization 完成新稿 Codex 最终事实回归；TeachingEditReport 均确认对应当前正文，CoverageMatrix 从 Gate 1/Claude handoff 口径更新为 Gate 3 通过口径，`OUTLINE.md` 与章节头统一标记为完成。本次以各章 CoverageMatrix 为详细核对记录，`SOURCE_INDEX.md` 第 34-42 节同步为当前终审索引。 |
| 2026-06-25 | 06_GPUScene / 07_MeshDrawCommand / 08_FrameInit / 09_DepthPrepass / 10_BasePass / 11_Shadows / 12_Lighting / 13_Atmosphere / 14_Translucency / 15_PostProcessing 完成新稿 Codex 最终事实回归；TeachingEditReport 均确认对应当前正文，CoverageMatrix 从 Gate 1 口径更新为 Gate 3 通过口径，`OUTLINE.md` 与章节头统一标记为完成。本次未新增跨章共享事实块；复用并确认各章 CoverageMatrix 中的 GPUScene、MeshDrawCommand、FrameInit、DepthPrepass、BasePass、Shadows、Lighting、Atmosphere、Translucency、PostProcessing 源码锚点。 |
| 2026-06-25 | 01_Architecture / 02_SceneProxy / 03_ThreadModel / 04_RHI / 05_RenderGraph 完成新稿 Codex 最终事实回归；TeachingEditReport 均确认对应当前正文，CoverageMatrix 更新为通过口径，`OUTLINE.md` 与章节头统一标记为完成。本次未引入新的跨章源码事实块，复用并确认既有 01-05 共享锚点：01 的模块/ViewFamily/FScene/visibility/RDG-MDC-RHI 主线，02 的 Component->Proxy->SceneInfo->FScene 生命周期，03 的 render command / RT / RHI submit / fence 时间线，04 的 DynamicRHI / resource / PSO / binding / transition / command list，05 的 RDG compile / cull / transient / barrier / external-extract / parallel execute。 |
| 2026-06-24 | 06_GPUScene / 07_MeshDrawCommand / 08_FrameInit / 09_DepthPrepass / 10_BasePass / 11_Shadows / 12_Lighting / 13_Atmosphere / 14_Translucency / 15_PostProcessing 已并行重构回 Codex Gate 1 稿；共享文件在 worker 回收后由主流程统一收口，`OUTLINE.md` 与章节头状态降为“Gate 1 重构完成 / 待 Claude 教学优化与 Codex 最终事实回归”。本次不把新锚点沉淀为终审事实；新增核对记录暂存于各章 CoverageMatrix，旧 TeachingEditReport 均视为历史报告，需由 Claude 针对新稿刷新。`SOURCE_INDEX.md` 第 24-33 节保留为历史终审索引，等待新稿完成 Claude 优化与 Codex 最终事实回归后再沉淀。 |
| 2026-06-24 | 02_SceneProxy / 04_RHI / 05_RenderGraph 已并行重构回 Codex Gate 1 稿；共享文件在 worker 回收后由主流程统一收口，新增核对记录暂存于各章 CoverageMatrix。 |
| 2026-06-14 | 历史记录：当时 01_Architecture 至 24_Optimization 完成 Codex 最终事实回归，OUTLINE 与章节头统一标记为完成，并将 06-24 共享索引从 Gate 1 口径更新为终审已验证；其中 16-24 的当前新稿状态已被 2026-06-25 Gate 1 重构记录覆盖。 |
| 2026-06-13 | 并行重生产 21_ShaderSystem / 22_ComputeShader / 23_Debugging / 24_Optimization 为 Gate 1 重构稿；公共文件最后由主线程统一写入，`OUTLINE.md` 状态更新为“Codex 重构完成 / Claude 教学优化未完成（历史状态）”，并新增 ShaderSystem、ComputeShader、调试工具、性能优化的源码锚点索引。 |
| 2026-06-13 | 并行重生产 16_Nanite / 17_Lumen / 18_MegaLights / 19_VirtualShadowMaps / 20_MaterialPipeline 为 Gate 1 重构稿；公共文件最后由主线程统一写入，`OUTLINE.md` 与章节头状态更新为“Codex 重构完成 / Claude 教学优化未完成（历史状态）”，并新增 Nanite、Lumen、MegaLights、VSM、材质编译管线的源码锚点索引。 |
| 2026-06-13 | 并行重生产 11_Shadows / 12_Lighting / 13_Atmosphere / 14_Translucency / 15_PostProcessing 为 Gate 1 重构稿；公共文件在 5 章回收审查后由主线程统一写入，`OUTLINE.md` 状态更新为“Codex 重构完成 / Claude 教学优化未完成（历史状态）”，并新增阴影、光照、体积/大气、半透明、后处理的源码锚点索引。 |
| 2026-06-13 | 并行重生产 06_GPUScene / 07_MeshDrawCommand / 08_FrameInit / 09_DepthPrepass / 10_BasePass 为 Gate 1 重构稿，公共文件最后由主线程统一收口；`OUTLINE.md` 与章节头状态更新为“Codex 重构完成 / Claude 教学优化未完成（历史状态）”，并补充 GPUScene LWC/layout、MDC PSO/Instance Culling compact、visibility pipeline、Depth/HZB、BasePass/Nanite/DBuffer 的新增锚点。 |
| 2026-06-13 | 按 Gate 1 深度标准补强 07_MeshDrawCommand / 08_FrameInit / 09_DepthPrepass：追加 `BuildMeshDrawCommands`、dynamic instancing、cached command state bucket、visibility 三同步边界、GPUScene/scene uniform/Instance Culling 时序、DepthPass processor 过滤链、parallel PrePass、PartialDepth、HZB 构建与 `bOcclusionBeforeBasePass` 等源码锚点。 |
| 2026-06-13 | 06_GPUScene / 07_MeshDrawCommand / 08_FrameInit / 09_DepthPrepass / 10_BasePass 完成 Codex Gate 1 源码锚定初稿；`OUTLINE.md` 状态统一更新为“Codex 初稿完成 / Claude 教学优化未完成（历史状态）”，未标记完成。新增 GPUScene、MeshDrawCommand、帧初始化、DepthPrepass/HZB、BasePass/GBuffer 的源码锚点索引。 |
| 2026-06-13 | 01_Architecture / 02_SceneProxy / 03_ThreadModel / 04_RHI / 05_RenderGraph 通过 Codex 最终事实回归；`OUTLINE.md` 与章节头状态更新为完成。补齐 04_RHI 的 `ERHIAccess` / D3D12 bindless 索引细节，并将 05_RenderGraph 的 RDG task mode、CVar、transient、barrier、parallel execute、external/extract 等锚点结构化沉淀。 |
| 2026-06-13 | 02_SceneProxy / 03_ThreadModel / 04_RHI / 05_RenderGraph 并行重构：02 回到 Component→Proxy→SceneInfo→FScene→draw 收集输入的生命周期主线；03 完成一条 render command 生命周期主线（ENQUEUE→Dispatcher→RT→RHI command list→Submit→Fence/Frame sync→并行录制与数据安全）；04 回到一条 DrawCall/资源请求进入 RHI 后端的生命周期主线；05 回到一个 RDG Pass 从声明到编译再到 RHI 命令的帧编译器主线。 |
| 2026-06-13 | 修正 04_RHI / 03_ThreadModel 相关索引表述：`FRHICommandListImmediate::QueueAsyncCommandListSubmit` 是并行子命令列表汇入 Immediate 的入口；当前 UE5.7 路径中 `ETranslatePriority` / `MinDrawsPerTranslate` 不应写成并行翻译控制事实，并行录制/提交宽度需结合 Submit 路径与 `r.RHICmdWidth`、`r.RHICmdMinDrawsPerParallelCmdList`、`r.MeshDrawCommands.ParallelPassSetup` 等 CVar 理解。 |
| 2026-06-12 | 01_Architecture 终审完成：压缩/迁出 GBuffer 编码、RDG Transient aliasing、GPUScene layout、MeshDrawCommand 排序合批、Nanite/VSM/Present/帧同步等后续专题细节；复核并修正模块 Build.cs 依赖表；修正 StaticMesh 示例中 FPrimitiveSceneInfo 创建时序（GT 在 BatchAddPrimitivesInternal 创建，RT 先 SetTransform/CreateRenderThreadResources，再 AddPrimitiveSceneInfo_RenderThread 排入 PrimitiveUpdates，FScene::Update 统一提交） |
| 2026-06-12 | 02_SceneProxy 文档完成：验证 Component→Proxy→SceneInfo→FScene 注册链路（CreateRenderState_Concurrent、CreateSceneProxy、FScene::BatchAddPrimitivesInternal、AddPrimitiveCommand、AddPrimitiveSceneInfo_RenderThread、FScene::Update dense arrays/type offset table、FPrimitiveSceneInfo::AddToScene/AddStaticMeshes）、StaticMesh Proxy 创建与 Nanite/经典分叉、DrawStaticElements/GetDynamicMeshElements 两条收集路径、帧末 Transform 更新批处理（FPrimitiveTransformUpdater、r.SkipRedundantTransformUpdate 默认 1）、RenderState dirty vs Transform dirty、RemovePrimitive/ReleaseSceneProxy/DestroyRenderThreadResources/DeletedPrimitiveSceneInfos 延迟删除流程 |
| 2026-06-11 | 初始创建：模块架构、核心类定位、Render 执行顺序、数据流路径、Nanite/Lumen 架构、RHI/RDG/线程系统 |
| 2026-06-11 | 01_Architecture：模块依赖关系、帧触发调用链、Renderer 创建分支、FSceneRenderer 基类、D3D12 命令翻译、RHI 虚接口、Primitive 注册路径 |
| 2026-06-11 | 01_Architecture 文档生成：验证 Render() 阶段划分（init/prepass/HZB/basepass/lighting/volumes/translucency/postprocess/cleanup）、RenderCommandPipe 系统（ERenderCommandPipeMode）、SceneRenderBuilder.Execute() 调度路径 |
| 2026-06-11 | 01_Architecture 文档完成：新增验证 — FRenderCommandDispatcher::Enqueue 路由逻辑（RenderingThread.h:1067）、FRenderThreadCommandPipe::Enqueue 分支（:591）、FRenderCommandList TLS 机制（:830）、FRDGBuilder::Execute 编译流程（RenderGraphBuilder.cpp:1755）、Compile() 死 Pass 裁剪（:1316）、FScene::Update 批量处理 PrimitiveUpdates（RendererScene.cpp:5190）、FSceneViewFamily 关键成员（SceneView.h:2211）、FViewInfo 可见性位图成员（SceneRendering.h:1131）、RDG AddPass 模式（LightRendering.cpp:2684）、FRHICommandBase 链表 + FMemStackBase 线性分配器（RHICommandList.h:348-576）、ALLOC_COMMAND 宏（:1337）、FSceneRenderProcessor::Execute Op 循环（SceneRenderBuilder.cpp:759）、RenderViewFamily_RenderThread 虚分派（SceneRendering.cpp:4895）|
| 2026-06-11 | 03_ThreadModel 文档完成：BeginFence 三种深度实现（RenderingThread.cpp:978-1051）、Fence Bundler 优化（:987-994）、LatchBypass 命令投递（:631）、FRenderCommandList 类定义（RenderingThread.h:830）、FRecordScope（:862）、FParallelForContext 示例（:896-919）、FRenderCommandFence 构造（:975-976）、IsFenceComplete（:1054）、RHI Submit + ERHISubmitFlags::SubmitToGPU（RHICommandList.cpp:1297）、GRHIPresentCounter Flip 追踪、RHITriggerTaskEventOnFlip（:1037）、ImmediateFlush DispatchToRHIThread（:1040）、单线程 Bypass 模式完整路径、引擎初始化时序（StartRenderingThread 在 PreInitPostStartupScreen）、GPUScene 上传调用点 Scene->GPUScene.UploadDynamicPrimitiveShaderDataForView（DeferredShadingRenderer.cpp:2108）|
| 2026-06-12 | 04_RHI 补充两个小节：资源状态与屏障（ERHIAccess RHIAccess.h:10 含 ReadOnlyMask/WritableMask/IsReadOnlyAccess、FRHITransitionInfo RHITransition.h:118 含构造重载、FRHIComputeCommandList::Transition/BeginTransitions/EndTransitions RHICommandList.h:3020-3032、引出 RDG 自动屏障）、命令列表类型层级（FRHICommandListBase :454 / FRHIComputeCommandList :2734 / FRHICommandList :3818 / FRHICommandListImmediate :4625、QueueAsyncCommandListSubmit :4688 并行子列表提交入口）。同时修复 04_RHI.md 重复内容（旧草稿与精炼版合并冲突，已统一为精炼版 8 小节结构）|
| 2026-06-12 | 04_RHI 文档完成：后端选择全链（RHIInit→PlatformCreateDynamicRHI→LoadDynamicRHIModule→IDynamicRHIModule::CreateRHI→GDynamicRHI；GRHISearchOrder/ChooseDefaultRHI；FD3D12DynamicRHIModule::CreateRHI 填 GMaxRHIFeatureLevel/GMaxRHIShaderPlatform）、特性等级与 FRHIGlobals 能力标志、资源生命周期（FRHIResource 引用计数 AtomicFlags、PendingDeletes 延迟删除队列 + DeleteResources 复活检查、TRefCountPtr、FRHITexture/Buffer + CreateDesc/Initializer、ERHIAccess/FRHITransitionInfo）、PSO（FGraphicsPipelineStateInitializer + FBoundShaderStateInput mesh/VS 互斥、TSharedPipelineStateCache 两层缓存、GetAndOrCreateGraphicsPipelineState 异步编译 FCompileGraphicsPipelineStateTask、运行时指针哈希 vs 内容哈希 RHIComputeStatePrecachePSOHash、磁盘缓存 FPipelineFileCacheManager/FShaderPipelineCache、Compute PSO）、Shader Binding（FRHIBatchedShaderParameters 批量四数组、RHISetShaderParameters 统一路径、FRHIUniformBuffer/Layout、SHADER_PARAMETER 宏→SetShaderParameters、静态 UB、bindless FRHIDescriptorHandle、D3D12 根签名/描述符堆/BindlessDescriptorManager）、录制翻译（Bypass vs 命令链表 + ALLOC_COMMAND，RHI Thread Execute()→IRHICommandContext→D3D12 DrawIndexedInstanced）|
| 2026-06-12 | 04_RHI 补充：两阶段资源创建（FRHITextureInitializer 移动语义 + GetSubresource 逐子资源写上传堆 + Finalize 交付 + 析构 RemovePendingTextureUpload 兜底；CreateTexture 是 Initializer+Finalize 二合一外壳 RHICommandList.h:941）|
| 2026-06-12 | 05_RenderGraph 文档完成：验证 `SHADER_PARAMETER_RDG_*` 与 `UBMT_RDG_*` 参数元数据、`FRDGBuilder::AddPass` 录制、`TRDGLambdaPass` lambda task mode、`ERDGPassFlags`、`SetupPassResources` / `SetupPassDependencies` 资源访问登记、`Compile` 裁剪和 async fork/join、transient 分配与 aliasing fences、barrier 编译与 split transition、`FRDGBarrierBatchBegin/End`、UAV barrier overlap、`SetupParallelExecute` 并行录制、`QueueAsyncCommandListSubmit` 提交、`ExecutePass` prologue/epilogue、External / Extract 语义，以及 RDG 调试 CVar。 |
| 2026-06-12 | 03_ThreadModel 终审完成：复核并修正 RenderCommandPipe 默认值与运行时退化（`r.RenderCommandPipeMode=2`→`All`，条件降级到 `RenderThread`/`None`）、TaskGraph `_Local` 队列使用范围、RHI Tasks 模式 worker 代跑语义、并行录制 CVar 默认值（`r.RHICmdWidth=8`、`r.RHICmdMinDrawsPerParallelCmdList=64`、`r.MeshDrawCommands.ParallelPassSetup=1`）、`FRenderingThreadTickHeartbeat` 与 `FThreadHeartBeat`/`g.TimeoutForBlockOnRenderFence` 的职责边界；RenderDoc/工具链相关表述已改为非强断言。 |
