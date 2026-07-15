# Tutorial_001 源码反查索引

本文件只收录已在本地 UE5.7 源码树中确认存在的路径。行号不在这里维护，避免版本漂移。

符号列是必验锚点，不是说明文字。多个必需 token 用 `;` 或 `,` 分隔，校验脚本会要求每个 token 都在对应源码文件中命中。

| 001 slug | 源码路径 | 符号/搜索关键词 | 用途 |
|----------|----------|----------------|------|
| `topic-render-loop` | `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` | `FEngineLoop::Tick` | 引擎主循环和每帧 Tick 入口 |
| `topic-render-loop` | `Engine/Source/Runtime/Engine/Private/GameEngine.cpp` | `UGameEngine::Tick` | GameEngine 每帧更新和视口驱动 |
| `topic-render-loop` | `Engine/Source/Runtime/Engine/Private/UnrealClient.cpp` | `FViewport::Draw; EnqueueBeginRenderFrame` | Viewport Draw 和渲染帧边界 |
| `topic-render-loop` | `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp` | `FDeferredShadingSceneRenderer` | 延迟渲染器主体 |
| `topic-thread-model` | `Engine/Source/Runtime/RenderCore/Public/RenderingThread.h` | `ENQUEUE_RENDER_COMMAND; TEnqueueUniqueRenderCommandType` | GT 到 RT 命令投递 |
| `topic-thread-model` | `Engine/Source/Runtime/RenderCore/Private/RenderingThread.cpp` | `StartRenderingThread; StopRenderingThread; RenderingThreadMain` | RenderThread 生命周期 |
| `topic-thread-model` | `Engine/Source/Runtime/RenderCore/Public/RenderCommandFence.h` | `class FRenderCommandFence; BeginFence; Wait` | 渲染命令 Fence 同步 |
| `topic-thread-model` | `Engine/Source/Runtime/Core/Public/Async/TaskGraphInterfaces.h` | `FGraphEventRef; TGraphTask; ENamedThreads::Type` | TaskGraph 调度接口 |
| `topic-rhi` | `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `class FRHICommandListBase; class FRHICommandListImmediate; FRHICommandListExecutor` | RHI 命令列表抽象 |
| `topic-rhi` | `Engine/Source/Runtime/RHI/Public/DynamicRHI.h` | `class FDynamicRHI; GDynamicRHI` | 动态 RHI 后端接口 |
| `topic-rhi` | `Engine/Source/Runtime/RHI/Public/RHIResources.h` | `class FRHIResource; class FRHITexture; class FRHIBuffer` | RHI 资源抽象 |
| `topic-rhi` | `Engine/Source/Runtime/RHI/Public/RHIContext.h` | `class IRHICommandContext; RHIDrawIndexedPrimitive` | RHI 后端命令上下文 |
| `topic-engine-initialization` | `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` | `FEngineLoop::PreInit; FEngineLoop::Init` | 引擎启动和初始化阶段 |
| `topic-engine-initialization` | `Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h` | `GENERATED_BODY; BODY_MACRO_COMBINE; CURRENT_FILE_ID` | UHT 反射宏入口 |
| `topic-engine-initialization` | `Engine/Source/Runtime/RHI/Private/DynamicRHI.cpp` | `RHIInit` | RHI 初始化入口 |
| `topic-engine-initialization` | `Engine/Source/Runtime/RHI/Private/Windows/WindowsDynamicRHI.cpp` | `PlatformCreateDynamicRHI` | Windows RHI 后端选择 |
| `topic-engine-initialization` | `Engine/Source/Runtime/Renderer/Private/RendererScene.cpp` | `FScene::FScene` | 渲染场景初始化 |
| `topic-gt-rt-dataflow` | `Engine/Source/Runtime/Renderer/Private/GPUScene.cpp` | `FGPUScene` | 场景数据同步和 GPUScene 上传 |
| `topic-base-pass` | `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp` | `FDeferredShadingSceneRenderer::RenderBasePass; FBasePassMeshProcessor::AddMeshBatch` | BasePass 渲染和 MeshPassProcessor 消费 |
| `topic-base-pass` | `Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp` | `RenderBasePassInternal; ClearGBufferAtMaxZ; GBufferClear` | BasePass 内部分流和 GBuffer 清理 |
| `topic-base-pass` | `Engine/Source/Runtime/Renderer/Private/BasePassRendering.h` | `FBasePassMeshProcessor; FMeshPassProcessorRenderState` | BasePass MeshProcessor 类型 |
| `topic-base-pass` | `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteShading.cpp` | `GNaniteComputeMaterialsSort; DispatchComputeShaderBundle` | Nanite Shading 与排序 |
| `topic-gbuffer` | `Engine/Shaders/Private/BasePassPixelShader.usf` | `SetGBufferForShadingModel; EncodeGBufferToMRT; EncodeGBuffer` | BasePass Pixel Shader 写入 GBuffer |
| `topic-clustered-deferred-lighting` | `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp` | `FDeferredShadingSceneRenderer::RenderLights` | 延迟光照主路径 |
| `topic-render-graph` | `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | `FRDGBuilder` | RDG 资源和 Pass 构建 API |
| `topic-rdg-addpass` | `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | `FRDGPassRef AddPass; ERDGPassFlags` | AddPass 声明 Pass、参数结构和执行 Lambda |
| `topic-rdg-resource-lifetime` | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp` | `FRDGBuilder::Compile; FirstPass; LastPasses` | Compile 阶段统计资源使用区间和生命周期 |
| `topic-rdg-transient-aliasing` | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp` | `FRDGBuilder::AllocateTransientResources; TransientResourceAllocator->CreateTexture; AddAliasingTransition` | Transient 资源分配和 aliasing transition |
| `topic-rdg-external-extract` | `Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` | `RegisterExternalTexture; QueueTextureExtraction; FExtractedTexture` | External 注册与 Extract 跨图保留 |
| `topic-rdg-barrier-generation` | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp` | `FRDGBuilder::CompilePassBarriers; FRDGBarrierBatchBegin; BarrierBatchMap` | Barrier 批次构建与状态转换 |
| `topic-rdg-execute-compile` | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp` | `FRDGBuilder::Compile; FRDGBuilder::Execute; SetupDispatchPassExecute` | Execute 驱动 Compile、调度和 Pass 执行 |
| `topic-nanite` | `Engine/Source/Runtime/Renderer/Private/Nanite/NaniteCullRaster.cpp` | `FRenderer::AddPass_Rasterize; FMicropolyRasterizeCS` | Nanite 剔除和光栅化调度 |
| `topic-lumen` | `Engine/Source/Runtime/Renderer/Private/Lumen/LumenSceneRendering.cpp` | `FDeferredShadingSceneRenderer::UpdateLumenScene; FLumenSceneData` | Lumen 场景渲染入口 |
| `topic-gpuscene` | `Engine/Source/Runtime/Renderer/Private/GPUScene.h` | `class FGPUScene; FGPUScenePrimitiveCollector; FGPUSceneResourceParameters` | GPUScene 类型、收集器和资源参数 |
| `topic-gpuscene` | `Engine/Source/Runtime/Renderer/Private/GPUScene.cpp` | `FGPUScene::UpdateInternal; FGPUScene::UploadGeneral; FGPUScene::AddPrimitiveToUpdate; FGPUScene::AllocateInstanceSceneDataSlots; AddOrMergeInstanceRange` | GPUScene 更新、上传和 Instance 数据分配 |
| `topic-gpuscene` | `Engine/Source/Runtime/Renderer/Private/PrimitiveSceneInfo.cpp` | `FPrimitiveSceneInfo::AllocateGPUSceneInstances; FPrimitiveSceneInfo::RequestGPUSceneUpdate` | Primitive 到 GPUScene 的实例分配和更新请求 |
| `topic-gpuscene` | `Engine/Source/Runtime/Renderer/Public/InstanceCulling/InstanceCullingContext.h` | `class FInstanceCullingContext; BuildRenderingCommands` | Instance Culling 命令构建 |
| `topic-gpuscene` | `Engine/Source/Runtime/Renderer/Private/SceneCulling/SceneCullingRenderer.h` | `class FSceneCullingRenderer; CullInstances` | Scene Culling 与实例剔除 |
| `topic-gpuscene` | `Engine/Shaders/Private/InstanceCulling/BuildInstanceDrawCommands.usf` | `InstanceCullBuildInstanceIdBufferCS; DrawIndirectArgsBufferOut; InstanceIdsBufferOut` | GPU 侧 Instance DrawCommand 构建 |
| `topic-megalights` | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp` | `FDeferredShadingSceneRenderer::GenerateMegaLightsSamples; FDeferredShadingSceneRenderer::RenderMegaLights` | MegaLights 渲染入口 |
| `topic-mesh-draw-command` | `Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.h` | `class FMeshDrawCommand; BuildMeshDrawCommands; SubmitMeshDrawCommands` | DrawCommand 构建、缓存和提交 |
| `topic-mesh-draw-command` | `Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.inl` | `FMeshPassProcessor::BuildMeshDrawCommands; DrawListContext->FinalizeCommand` | MeshDrawCommand 构建和 DrawList 归档 |
| `topic-mesh-draw-command` | `Engine/Source/Runtime/Renderer/Private/MeshPassProcessor.cpp` | `FMeshDrawCommand::SubmitDrawBegin; FMeshDrawCommand::SubmitDrawEnd; SubmitMeshDrawCommandsRange` | MeshDrawCommand 提交路径 |
| `topic-mesh-draw-command` | `Engine/Source/Runtime/Renderer/Private/PrimitiveSceneInfo.cpp` | `FPrimitiveSceneInfo::AddStaticMeshes; CacheMeshDrawCommands` | 静态网格缓存命令入口 |
| `topic-mesh-draw-command` | `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp` | `r.MeshDrawCommands.DynamicInstancing; r.MeshDrawCommands.UseCachedCommands; r.MeshDrawCommands.LogMeshDrawCommandMemoryStats` | MDC 相关 CVar |
| `topic-material-system` | `Engine/Source/Runtime/Engine/Private/Materials/Material.cpp` | `void UMaterial::CacheShaders; CacheResourceShadersForRendering; FindOrCreateMaterialResource` | 材质编译入口和资源查找 |
| `topic-material-system` | `Engine/Source/Runtime/Engine/Private/Materials/MaterialShared.cpp` | `FMaterial::BeginCompileShaderMap; FMaterial::Translate` | 材质资源编译和翻译 |
| `topic-material-system` | `Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp` | `FHLSLMaterialTranslator::Translate; TranslateMaterial; AddCodeChunkInner` | 材质节点翻译到 HLSL |
| `topic-material-system` | `Engine/Source/Runtime/Engine/Private/Materials/MaterialShader.cpp` | `FMaterialShaderMap::Compile; FMaterialShaderMap::SubmitCompileJobs; FMaterialShaderType::ShouldCompilePermutation` | ShaderMap 编译提交和变体裁剪 |
| `topic-shadow-system` | `Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp` | `FProjectedShadowInfo::RenderProjection; FProjectedShadowInfo::SetupProjectionStencilMask` | 阴影渲染通用路径 |
| `topic-shadow-system` | `Engine/Source/Runtime/Renderer/Private/ShadowRendering.h` | `class FProjectedShadowInfo` | 阴影统一数据载体 |
| `topic-shadow-system` | `Engine/Source/Runtime/Renderer/Private/ShadowSetup.cpp` | `FProjectedShadowInfo::SetupWholeSceneProjection; SetupMeshDrawCommandsForShadowDepth` | 阴影投影和 Shadow MDC 构建 |
| `topic-shadow-system` | `Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp` | `GetShadowDepthPassShaders; FProjectedShadowInfo::RenderDepth` | ShadowDepth Shader 选择和深度渲染 |
| `topic-shadow-system` | `Engine/Source/Runtime/Renderer/Private/Shadows/ScreenSpaceShadows.cpp` | `RenderScreenSpaceShadows` | Contact Shadows 入口 |
| `topic-console-commands` | `Engine/Source/Runtime/Core/Public/HAL/IConsoleManager.h` | `struct IConsoleManager; class TAutoConsoleVariable; class TConsoleVariableData; ECVF_RenderThreadSafe; class FAutoConsoleCommand; class FAutoConsoleVariableRef; class FAutoConsoleVariableSink; DECLARE_DELEGATE` | CVar 和控制台命令接口 |
| `topic-console-commands` | `Engine/Source/Runtime/Core/Private/HAL/ConsoleManager.cpp` | `FConsoleManager::ProcessUserConsoleInput; FConsoleManager::RegisterConsoleCommand; FConsoleManager::AddConsoleObject; FConsoleManager::FindConsoleObject; FConsoleManager::CallAllConsoleVariableSinks` | 控制台输入、注册、查找和 Sink 调用 |
| `topic-console-commands` | `Engine/Source/Runtime/Core/Private/HAL/ConsoleManager.h` | `class FConsoleManager; ConsoleObjects; ConsoleVariableChangeSinks; FTransactionallySafeCriticalSection` | ConsoleManager 存储结构和同步 |
| `topic-post-process` | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp` | `AddPostProcessingPasses; FPostProcessingInputs` | 后处理链入口 |
| `topic-post-process` | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessMaterial.cpp` | `AddPostProcessMaterialPass; AddPostProcessMaterialChain; FPostProcessMaterialShader; FPostProcessMaterialPS` | 后处理材质 Pass 和链式执行 |
| `topic-post-process` | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessTonemap.cpp` | `AddTonemapPass; TonemapperPermutation::FDesktopDomain; FTonemapPS` | Tonemap Pass 和变体域 |
| `topic-post-process` | `Engine/Source/Runtime/Renderer/Public/PostProcess/PostProcessMaterialInputs.h` | `FPostProcessMaterialInputs; EPostProcessMaterialInput` | 后处理材质输入结构 |
| `topic-post-process` | `Engine/Source/Runtime/Engine/Classes/Engine/BlendableInterface.h` | `IBlendableInterface; EBlendableLocation` | Blendable 插入点接口 |
| `topic-custom-shader` | `Engine/Source/Runtime/RenderCore/Public/GlobalShader.h` | `DECLARE_GLOBAL_SHADER; IMPLEMENT_GLOBAL_SHADER; FGlobalShaderType` | 自定义 Global Shader 类型声明和注册 |
| `topic-custom-shader` | `Engine/Source/Runtime/RenderCore/Private/Shader.cpp` | `FShaderType::GetTypeList; FShaderType::Initialize; GlobalListLink.LinkHead` | ShaderType 全局链表和初始化 |
| `topic-custom-shader` | `Engine/Source/Runtime/RenderCore/Public/ShaderParameterStruct.h` | `SHADER_USE_PARAMETER_STRUCT; SetShaderParameters` | Shader 参数结构和提交 |
| `topic-custom-shader` | `Engine/Source/Runtime/RenderCore/Public/ShaderParameterMacros.h` | `BEGIN_SHADER_PARAMETER_STRUCT; SHADER_PARAMETER` | Shader 参数声明宏 |
| `topic-custom-shader` | `Engine/Source/Runtime/RenderCore/Public/ShaderPermutation.h` | `TShaderPermutationDomain; SHADER_PERMUTATION_BOOL; SHADER_PERMUTATION_INT` | Shader 变体域 |
| `topic-custom-shader` | `Engine/Source/Runtime/RenderCore/Public/PixelShaderUtils.h` | `static inline void AddFullscreenPass; DrawFullscreenTriangle` | 全屏 Pixel Shader Pass 辅助 |
| `topic-compute-shader` | `Engine/Source/Runtime/RenderCore/Public/ShaderParameterStruct.h` | `SetShaderParameters; FShaderParametersMetadata` | Shader 参数结构绑定 |
| `topic-compute-shader` | `Engine/Source/Runtime/RenderCore/Public/Shader.h` | `DispatchComputeShader` | RenderCore Compute 调度辅助 |
| `topic-compute-shader` | `Engine/Source/Runtime/RHI/Public/RHICommandList.h` | `DispatchComputeShader; FRHICommandDispatchComputeShader` | RHI Compute Dispatch 命令 |
| `topic-compute-shader` | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h` | `namespace FComputeShaderUtils; inline FRDGPassRef AddPass` | RDG Compute Pass 工具 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp` | `RenderViewFamily_RenderThread; STAT_TotalSceneRenderingTime` | 整帧渲染入口和 CPU 侧计时 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/Renderer/Private/SceneVisibility.cpp` | `FDeferredShadingSceneRenderer::BeginInitViews; ComputeViewVisibility; STAT_InitViewsTime` | 可见性和提交成本入口 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/RenderCore/Public/RenderGraphEvent.h` | `RDG_GPU_STAT_SCOPE; RDG_EVENT_SCOPE; RDG_RHI_GPU_STAT_SCOPE` | RDG event 与 GPU 计时宏 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/RenderCore/Public/ProfilingDebugging/RealtimeGPUProfiler.h` | `DECLARE_GPU_STAT; STATGROUP_GPU; SCOPED_GPU_STAT` | stat gpu 统计声明和作用域 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/RHI/Public/GPUProfiler.h` | `struct FGPUProfiler; PushEvent; PopEvent` | RHI GPU profiler 事件树 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/Core/Private/ProfilingDebugging/TraceAuxiliary.cpp` | `FTraceAuxiliaryImpl; GDefaultChannels` | Unreal Insights 追踪基础设施 |
| `topic-rendering-optimization` | `Engine/Source/Runtime/Engine/Private/Scalability.cpp` | `CVarResolutionQuality` | Scalability 分辨率质量 CVar |
