<a id="topic-unity-rendering-mapping"></a>

# 1.4 架构对比Unity

**UE5.7版本** | 最后更新：2025-10-27

## ⚠️ 性能数据说明

本文档中的毫秒、百分比、内存占用和数量级数字均为**教学示意**，用于说明架构取舍和瓶颈位置，不是官方 Benchmark，也不能直接套用到项目预算。实际结果受平台、RHI、驱动、场景规模、分辨率、资源格式和项目配置影响，需用 Unreal Insights、RenderDoc、平台 GPU 工具和 Unity Profiler 在目标项目中复测。

## 📌 本章目标

- 全面对比UE5.7与Unity URP的渲染架构设计
- 理解两个引擎在关键设计选择上的权衡
- 帮助Unity背景的开发者快速建立UE心智模型
- 掌握两个引擎各自的优势场景

## 🔗 依赖知识

- **前置章节**：[1.1 核心模块划分](./CoreModules.md)、[1.2 线程模型详解](../Phase01_Fundamentals/1.2_ThreadModel_Deep.md)、[1.3 核心概念手册](./CoreConcepts.md)
- **相关章节**：本章是阶段一总结，帮助建立完整的认知框架

---

## 一、架构模式对比

### 模块组织方式

| 维度 | UE5.7 | Unity URP | 设计理念 |
|------|-------|-----------|----------|
| **组织模式** | 模块化（Module） | 包化（Package） | UE编译期强类型，Unity运行期灵活 |
| **依赖管理** | .Build.cs编译期检查 | asmdef运行期解析 | UE严格但安全，Unity灵活但易出错 |
| **代码位置** | Engine/Source/Runtime/ | Packages/com.unity.render-pipelines.universal/ | UE集成在引擎，Unity独立包 |
| **修改难度** | 需要重新编译引擎 | 直接修改Package源码 | Unity更易定制，UE更稳定 |
| **版本管理** | 引擎版本统一 | Package独立版本 | UE一致性好，Unity灵活升级 |

### UE模块系统示例

```
Engine/Source/Runtime/
├── Renderer/          ← 独立模块
│   ├── Renderer.Build.cs    // 依赖声明
│   ├── Public/       // 公共接口
│   └── Private/      // 内部实现
│
├── RenderCore/        ← 另一个独立模块
│   ├── RenderCore.Build.cs
│   ├── Public/
│   └── Private/
│
└── 编译期强制检查依赖关系
```

### Unity Package系统示例

```
Packages/
├── com.unity.render-pipelines.core/    ← 核心包
│   ├── package.json    // 版本声明
│   ├── Runtime/
│   └── ShaderLibrary/
│
├── com.unity.render-pipelines.universal/  ← URP包
│   ├── package.json
│   ├── Runtime/
│   │   ├── ForwardRenderer.cs
│   │   └── UniversalRenderPipeline.cs
│   └── Shaders/
│
└── 运行期动态加载
```

### 为什么UE选择模块化？

1. **大型项目需求**：AAA游戏需要严格的依赖管理
2. **性能优先**：编译期优化，运行期零开销
3. **稳定性**：编译期错误捕获，避免运行期崩溃
4. **团队协作**：清晰的模块边界，减少冲突

### 为什么Unity选择Package化？

1. **快速迭代**：不需重新编译引擎
2. **灵活定制**：用户可轻松修改渲染管线
3. **版本独立**：URP可独立于Unity版本升级
4. **轻量化**：按需加载，减少核心体积

---

## 二、线程模型对比

### 线程架构

| 维度 | UE5.7 | Unity URP | 差异分析 |
|------|-------|-----------|----------|
| **线程数量** | 3线程（Game/Render/RHI） | 2线程（Main/Render） | UE多RHIThread异步提交 |
| **帧并行度** | 3帧重叠（N/N-1/N-2） | 1-2帧重叠 | UE并行度更高 |
| **命令传递** | ENQUEUE_RENDER_COMMAND | ScriptableRenderContext / CommandBuffer | UE把 GT->RT 命令边界暴露在 C++，Unity把组件到渲染命令的同步更多隐藏在引擎内部 |
| **同步机制** | FRenderCommandFence | WaitForEndOfFrame | UE更细粒度控制 |
| **数据传递** | Lambda捕获+拷贝 / SceneProxy | Native renderer data / CullingResults / JobData / GPU buffer 等路径 | 两者都避免渲染线程无保护地读 live 组件，只是暴露层级不同 |

### UE三线程模型

```
时间轴 →
帧N-2:                    [GPU执行]
帧N-1:         [RHIThread] ────────→ [GPU执行]
帧N:   [GameThread] [RenderThread] [RHIThread] ────────→ [GPU执行]

优势：
- CPU/GPU完全并行
- RHIThread减少驱动阻塞
- 适合D3D12/Vulkan等现代API
```

### Unity渲染线程模型（公开层简化）

```
时间轴 →
帧N-1:              [GPU执行]
帧N:   [MainThread] [RenderThread] ────────→ [GPU执行]

特点：
- C#公开层更简单，很多同步边界由引擎内部处理
- 具体线程和提交路径随平台、SRP、Graphics Jobs、BatchRendererGroup / Entities Graphics 配置变化
```

### 代码对比：传递数据到渲染线程

**UE5.7方式**：
```cpp
// ✅ UE代码
FVector Position = Actor->GetActorLocation();
ENQUEUE_RENDER_COMMAND(UpdatePosition)(
    [Position](FRHICommandListImmediate& RHICmdList)
    {
        // RenderThread执行
        SceneProxy->UpdatePosition(Position);
    });
```

**Unity方式**：
```csharp
// Unity C#代码
Vector3 position = transform.position;
// Unity通过CommandBuffer间接传递
var cmd = CommandBufferPool.Get("UpdatePosition");
cmd.SetGlobalVector("_Position", position);
Graphics.ExecuteCommandBuffer(cmd);
```

### 为什么UE需要独立RHIThread？

**核心原因**：D3D12/Vulkan是显式API，需要应用层手动管理大量工作（这些工作在OpenGL ES中由驱动隐式完成）。RHIThread的目的是**将这些准备工作并行化**，而不是"加速驱动调用"。

**源码锚点**：`Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp`，搜索 `FD3D12DynamicRHI::RHIEndDrawingViewport`、`FD3D12CommandContext::RHIPresent`、`Present`。  
**版本**：UE5.7

**显式管理的工作包括**：
1. **内存驻留管理**（Residency Management）：确保资源在GPU内存中 → OpenGL驱动自动管理
2. **描述符管理**：手动创建和更新描述符堆 → OpenGL驱动自动创建
3. **资源屏障**：显式插入Transition/UAV屏障 → OpenGL驱动自动插入
4. **命令列表分批**：控制每次ExecuteCommandLists的数量 → OpenGL驱动自动优化
5. **同步管理**：手动插入Fence和Signal → OpenGL驱动自动同步

**技术对比**：

```cpp
// OpenGL ES（隐式管理 - 看起来简单）
glBindTexture(GL_TEXTURE_2D, texture);  // 驱动内部：检查状态、创建描述符、管理内存
glDrawElements(...);                    // 驱动内部：验证、同步、提交（示意耗时）
// 总CPU时间：示意为数毫秒级，具体需在目标设备实测

// D3D12（显式管理 - 工作量大但可并行）
// RenderThread（示意耗时）
BuildRenderGraph();
GenerateRHICommands();

// RHIThread（与RenderThread并行）
ManageResidency();        // 显式管理
UpdateDescriptors();      // 显式管理
InsertBarriers();         // 显式管理
ExecuteCommandLists();    // 真正的驱动提交

// 总CPU时间取并行流水线的关键路径，需按平台实测
```

**关键点**：
- D3D12/Vulkan把更多责任交给应用层，单次提交路径和驱动开销趋势通常更可控，但需要更多显式准备工作。
- RHIThread的核心价值是把这些准备与提交工作放进并行流水线，最终收益必须结合平台和场景实测。

Unity在移动端不需要RHIThread的原因：
- **API特性**：移动端主要使用OpenGL ES/Metal，驱动自动管理大部分工作
- **硬件限制**：移动CPU核心数有限（4-8核），专用RHIThread的收益不大
- **架构权衡**：Unity优先考虑简单性和内存开销（移动设备RAM有限）
- **性能需求**：移动GPU性能相对有限，命令提交并行度要求较低

---

## 三、渲染管线对比

### 管线类型

| 特性 | UE5.7 | Unity URP | 分析 |
|------|-------|-----------|------|
| **主要管线** | Deferred Shading | Forward+ | UE重Desktop，Unity重Mobile |
| **移动端** | Mobile Forward | Forward+ | Unity统一架构 |
| **光照模型** | 物理PBR（更复杂） | 简化PBR | UE质量优先，Unity性能优先 |
| **透明渲染** | 多Pass分层 | 单Pass | UE质量更高，Unity性能更好 |
| **GI方案** | Lumen动态GI | Lightmapping+Probes | UE完全动态，Unity静态为主 |

### UE延迟渲染流程

```
FDeferredShadingSceneRenderer::Render()
│
├─> 1. EarlyZ/DepthPass
│     └─> 早期深度写入，减少overdraw
│
├─> 2. BasePass (GBuffer)
│     ├─> GBufferA: Normal + ShadingModelID
│     ├─> GBufferB: Metallic + Specular + Roughness
│     ├─> GBufferC: BaseColor + AO
│     ├─> GBufferD: CustomData
│     └─> GBufferE: PrecomputedShadow
│
├─> 3. Lighting Pass
│     ├─> Tiled/Clustered Deferred
│     ├─> MegaLights (5.7新)
│     └─> Lumen GI
│
├─> 4. Translucency
│     ├─> TranslucencyStandard
│     ├─> TranslucencyAfterDOF
│     └─> TranslucencyAfterMotionBlur
│
└─> 5. PostProcessing
      ├─> TAA/TSR
      ├─> Bloom
      ├─> ToneMapping
      └─> FXAA
```

### Unity URP Forward+流程

```csharp
UniversalRenderPipeline.Render()
│
├─> 1. DepthPrepass (可选)
│
├─> 2. ForwardOpaquePass
│     └─> BaseColor + Lighting 一次完成
│          (光照在Pixel Shader中计算)
│
├─> 3. Forward+ Light Culling
│     └─> Tile-based Light List
│
├─> 4. TransparentPass
│
└─> 5. PostProcessing
      ├─> Bloom
      ├─> ToneMapping
      └─> FXAA/SMAA
```

### 为什么UE主推Deferred？

1. **质量优先**：Desktop/Console硬件强大
2. **复杂光照**：支持大量动态光源（MegaLights数百万）
3. **材质复杂度**：解耦材质与光照计算
4. **Lumen集成**：Deferred更适合GI计算

### 为什么Unity主推Forward+？

1. **移动端友好**：减少带宽和RenderTarget占用
2. **性能可控**：光照计算量随场景线性增长
3. **MSAA支持**：Forward天然支持MSAA
4. **简化流程**：降低学习门槛

---

## 四、命令系统对比

### 命令构建方式

| 维度 | UE5.7 | Unity URP | 差异 |
|------|-------|-----------|------|
| **命令类型** | RHICommandList | CommandBuffer | UE更底层，Unity更高级 |
| **构建方式** | 延迟执行（Deferred） | 录制回放（Record & Playback） | UE自动优化，Unity手动管理 |
| **并行性** | 多线程并行构建 | SRP C#入口常在主线程组织，底层可有 Jobs / Graphics Jobs / native 渲染线程 | UE把更多命令构建细节暴露给源码读者 |
| **生命周期** | 自动管理 | 手动Release | UE更安全 |

### UE RHICommandList

```cpp
// ✅ UE代码 - 延迟执行命令
FRHICommandListImmediate& RHICmdList = GetImmediateCommandList();

// 设置RenderTarget
RHICmdList.BeginRenderPass(RenderPassInfo, TEXT("MyPass"));

// 设置PSO
RHICmdList.SetGraphicsPipelineState(GraphicsPSO);

// 绑定资源
RHICmdList.SetShaderTexture(PixelShader, TextureIndex, Texture);
RHICmdList.SetShaderUniformBuffer(PixelShader, BufferIndex, UniformBuffer);

// DrawCall
RHICmdList.DrawIndexedPrimitive(
    IndexBuffer,
    0, 0, NumVertices,
    0, NumPrimitives, NumInstances
);

RHICmdList.EndRenderPass();

// 命令在RHIThread异步执行，GameThread不阻塞
```

### Unity CommandBuffer

```csharp
// Unity C#代码 - 录制命令
var cmd = CommandBufferPool.Get("MyPass");

// 设置RenderTarget
cmd.SetRenderTarget(colorRT, depthRT);

// 清空
cmd.ClearRenderTarget(true, true, Color.black);

// DrawMesh
cmd.DrawMesh(mesh, Matrix4x4.identity, material);

// 设置全局变量
cmd.SetGlobalTexture("_MyTexture", texture);

// 执行命令
Graphics.ExecuteCommandBuffer(cmd);

// 手动释放
CommandBufferPool.Release(cmd);
```

### 性能对比

```
UE优势：
- 多线程并行构建DrawCall
- RHIThread异步提交
- MeshDrawCommand缓存（可显著减少重复构建开销，具体幅度需实测）

Unity优势：
- C#简单易用
- CommandBuffer可复用
- 灵活插入渲染管线任意位置
```

---

## 五、资源管理对比

### RenderGraph vs SRP Resource Pool

| 维度 | UE RenderGraph | Unity SRP | 差异 |
|------|----------------|-----------|------|
| **模式** | 声明式（Declarative） | 命令式（Imperative） | UE自动优化，Unity手动管理 |
| **资源生命周期** | 自动管理 | 手动申请/释放 | UE更安全，Unity更灵活 |
| **内存优化** | Transient Aliasing | RTHandle Pool | UE更激进，Unity更保守 |
| **Barrier插入** | 自动 | 手动 | UE减少手写Barrier负担，Unity需要开发者自行维护 |
| **调试** | 可视化依赖图 | 手动追踪 | UE更友好 |

### UE RenderGraph

```cpp
// ✅ UE代码 - 声明式
FRDGBuilder GraphBuilder(RHICmdList);

// 1. 声明资源（不分配内存）
FRDGTextureRef TempRT = GraphBuilder.CreateTexture(
    FRDGTextureDesc::Create2D(
        ViewSize,
        PF_FloatRGBA,
        FClearValueBinding::Black,
        TexCreate_RenderTargetable | TexCreate_ShaderResource
    ),
    TEXT("TempRT")
);

// 2. Pass使用资源
FMyPassParameters* PassParams = GraphBuilder.AllocParameters<FMyPassParameters>();
PassParams->InputTexture = InputTexture;
PassParams->RenderTargets[0] = FRenderTargetBinding(TempRT, ERenderTargetLoadAction::EClear);

GraphBuilder.AddPass(
    RDG_EVENT_NAME("MyPass"),
    PassParams,
    ERDGPassFlags::Raster,
    [PassParams](FRHICommandList& RHICmdList)
    {
        // Pass执行
        DrawMyEffect(RHICmdList, PassParams);
    });

// 3. Execute自动：
//    - 分配Transient内存
//    - 插入Barrier
//    - 执行Pass
//    - 回收内存
GraphBuilder.Execute();
```

### Unity SRP Resource Pool

```csharp
// Unity C#代码 - 命令式
// 1. 手动申请资源
int tempRTID = Shader.PropertyToID("_TempRT");
cmd.GetTemporaryRT(
    tempRTID,
    Screen.width, Screen.height,
    0, // depth bits
    FilterMode.Bilinear,
    RenderTextureFormat.ARGBHalf
);

// 2. 使用资源
cmd.SetRenderTarget(tempRTID);
cmd.ClearRenderTarget(true, true, Color.black);
cmd.DrawMesh(mesh, Matrix4x4.identity, material);

// 3. 手动释放（容易忘记！）
cmd.ReleaseTemporaryRT(tempRTID);

// 或使用RTHandles（URP新方案）
var rtHandle = RTHandles.Alloc(
    Vector2.one, // 相对尺寸
    TextureXR.slices,
    DepthBits.None,
    GraphicsFormat.R16G16B16A16_SFloat
);
// ... 使用 ...
RTHandles.Release(rtHandle); // 手动释放
```

### 为什么UE选择RenderGraph？

1. **复杂度爆炸**：现代渲染有上百个Pass，手动管理不现实
2. **性能优化**：自动 Aliasing 可降低 RenderTarget 内存峰值，具体幅度需按项目资源生命周期实测
3. **Barrier优化**：D3D12/Vulkan需要精确的Barrier，自动插入避免错误
4. **开发效率**：开发者只需关注算法，不管资源生命周期

### 为什么Unity保留手动管理？

1. **灵活性**：用户可精确控制每个细节
2. **学习曲线**：命令式更直观，符合传统思维
3. **向后兼容**：保持与旧版本的兼容性
4. **移动端**：简单场景不需要复杂的资源管理

---

## 六、Shader系统对比

### Shader组织方式

| 维度 | UE5.7 | Unity URP | 差异 |
|------|-------|-----------|------|
| **文件格式** | .usf/.ush (HLSL) | .hlsl/.hlslinc | UE自定义格式，Unity标准HLSL |
| **编译时机** | 启动时/异步 | 导入时 | UE更灵活，Unity更确定 |
| **平台变体** | FShaderPlatform | ShaderCompiler | UE更底层控制 |
| **参数绑定** | UniformBuffer | MaterialPropertyBlock | UE类型安全，Unity更动态 |
| **生成方式** | MaterialGraph → Code | ShaderGraph → HLSL | 相似的节点式编辑器 |

### UE Shader示例

```hlsl
// ✅ BasePassPixelShader.usf
#include "Common.ush"
#include "BasePassCommon.ush"
#include "DeferredShadingCommon.ush"

void Main(
    FBasePassVSToPS Input,
    out float4 OutGBufferA : SV_Target0,
    out float4 OutGBufferB : SV_Target1,
    out float4 OutGBufferC : SV_Target2
)
{
    // 获取材质参数
    FMaterialPixelParameters MaterialParameters = 
        GetMaterialPixelParameters(Input);
    
    // 材质计算（由MaterialGraph生成）
    CalcMaterialParameters(MaterialParameters);
    
    // 编码GBuffer
    FGBufferData GBuffer = (FGBufferData)0;
    GBuffer.BaseColor = GetMaterialBaseColor(MaterialParameters);
    GBuffer.Metallic = GetMaterialMetallic(MaterialParameters);
    GBuffer.Roughness = GetMaterialRoughness(MaterialParameters);
    GBuffer.WorldNormal = MaterialParameters.WorldNormal;
    
    EncodeGBuffer(GBuffer, OutGBufferA, OutGBufferB, OutGBufferC);
}
```

### Unity URP Shader示例

```hlsl
// Unity Lit.shader
#include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"
#include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Lighting.hlsl"

struct Attributes
{
    float4 positionOS : POSITION;
    float3 normalOS : NORMAL;
    float2 uv : TEXCOORD0;
};

struct Varyings
{
    float4 positionCS : SV_POSITION;
    float3 normalWS : NORMAL;
    float2 uv : TEXCOORD0;
};

Varyings LitPassVertex(Attributes input)
{
    Varyings output;
    output.positionCS = TransformObjectToHClip(input.positionOS.xyz);
    output.normalWS = TransformObjectToWorldNormal(input.normalOS);
    output.uv = input.uv;
    return output;
}

half4 LitPassFragment(Varyings input) : SV_Target
{
    // 采样纹理
    half4 albedo = SAMPLE_TEXTURE2D(_BaseMap, sampler_BaseMap, input.uv);
    
    // 光照计算（Forward，直接输出颜色）
    Light mainLight = GetMainLight();
    half3 lighting = albedo.rgb * mainLight.color * saturate(dot(input.normalWS, mainLight.direction));
    
    return half4(lighting, 1);
}
```

---

## 七、材质系统对比

### 材质编辑器

| 特性 | UE Material Editor | Unity Shader Graph | 差异 |
|------|-------------------|-------------------|------|
| **节点数量** | 500+ | 200+ | UE更丰富 |
| **物理PBR** | 完整支持 | 简化版 | UE质量更高 |
| **性能分析** | Shader Complexity | Frame Debugger | UE更详细 |
| **自定义节点** | Custom Node | Custom Function | 两者都支持 |
| **材质函数** | Material Functions | Sub Graphs | 类似概念 |

### UE材质实例化

```
UMaterial (Master Material)
    ├─> UMaterialInstance (实例1) - 参数化
    ├─> UMaterialInstance (实例2) - 参数化
    └─> UMaterialInstance (实例3) - 参数化

优势：
- 共享Shader代码
- 运行时修改参数（不重新编译Shader）
- 大幅减少Shader变体数量
```

### Unity材质变体

```
Shader (含多个Variant)
    ├─> Material实例1 - Keyword组合A
    ├─> Material实例2 - Keyword组合B
    └─> Material实例3 - Keyword组合C

挑战：
- Shader变体爆炸（组合爆炸）
- 编译时间长
- 运行时切换Keyword开销大
```

---

## 八、性能特性对比

### 现代渲染技术

| 特性 | UE5.7 | Unity 2023+ | 成熟度 |
|------|-------|-------------|--------|
| **虚拟几何** | Nanite（完善） | 无原生支持 | UE领先 |
| **动态GI** | Lumen（完善） | 实验性（HDRP） | UE领先 |
| **Virtual Shadow** | 完善 | 无原生支持 | UE领先 |
| **TSR/DLSS** | 完善 | FSR/DLSS（插件） | UE领先 |
| **Ray Tracing** | 完善（DXR） | 实验性（HDRP） | UE领先 |
| **Job System** | TaskGraph | Job System | Unity更易用 |
| **ECS** | Mass Entity | DOTS/ECS | Unity更激进 |

### 为什么UE5在渲染技术上领先？

1. **目标市场**：AAA游戏需要最尖端技术
2. **投入力度**：Epic大量投入渲染研发
3. **硬件需求**：针对最新硬件优化
4. **工具链**：完整的制作流程支持

### Unity的优势领域

1. **2D游戏**：完善的2D工具链
2. **移动端**：更好的性能与包体优化
3. **快速原型**：C#开发效率高
4. **跨平台**：更广泛的平台支持（WebGL、Switch等）

---

## 九、学习曲线对比

### 入门难度

```
Unity URP:
- C#语言友好
- 组件式编程直观
- ShaderGraph可视化
- 文档丰富易懂
→ 初学者友好 ★★★★★

UE5.7:
- C++语言门槛高
- 模块化架构复杂
- 需要理解引擎源码
- 文档偏向专业开发者
→ 初学者友好 ★★★☆☆
```

### 进阶天花板

```
Unity:
- 渲染管线可定制性高
- SRP允许完全重写管线
- C#脚本灵活
- 但顶级AAA特性有限
→ 天花板 ★★★★☆

UE:
- 完整源码，可自定义引擎
- 顶尖渲染技术
- 工业级工具链
- 但需要深厚的图形学知识
→ 天花板 ★★★★★
```

---

## 十、适用场景总结

### UE5.7适合的项目

```
✅ AAA级PC/Console游戏
✅ 影视级画质需求
✅ 开放世界大场景
✅ 需要Nanite/Lumen等尖端技术
✅ 团队有C++能力
✅ 开发周期较长（1年+）
```

### Unity URP适合的项目

```
✅ 移动端游戏
✅ 2D游戏
✅ 中小型3D游戏
✅ 快速原型验证
✅ 独立开发者/小团队
✅ 需要广泛平台支持（WebGL/Switch）
✅ 开发周期较短（6个月内）
```

---

## 十一、迁移建议

### 从Unity转UE的开发者

**心智模型转换**：
```
Unity GameObject     → UE Actor
Unity Component      → UE Component
Unity Transform      → UE SceneComponent
Unity Renderer       → UE PrimitiveComponent + SceneProxy
Unity CommandBuffer  → UE RHICommandList
Unity SRP            → UE RenderGraph
```

**关键差异**：
1. **编译型 vs 解释型**：UE需要重新编译，Unity即改即测
2. **值类型 vs 引用类型**：UE的FVector是值，Unity的Vector3也是值
3. **指针 vs 引用**：UE大量使用指针，需要注意内存管理
4. **同步 vs 异步**：UE的渲染是异步的，需要理解线程模型

### 从UE转Unity的开发者

**心智模型转换**：
```
UE Actor             → Unity GameObject
UE Component         → Unity Component
UE FVector           → Unity Vector3
UE FMaterial         → Unity Material
UE FShader           → Unity Shader
UE RenderGraph       → Unity RenderGraph (HDRP)或手动管理(URP)
```

**关键差异**：
1. **C++ vs C#**：Unity开发更快，但性能受限
2. **编译时检查 vs 运行时检查**：C#更灵活但易出运行时错误
3. **显式资源管理 vs GC**：Unity有垃圾回收（GC卡顿需要优化）
4. **深度定制 vs 快速开发**：Unity牺牲一些定制性换取开发速度

---

## ✅ 检查点

完成本章后，您将能够：

- [ ] **理解架构差异**
  - UE模块化 vs Unity包化
  - 三线程 vs 双线程
  - Deferred vs Forward+

- [ ] **理解设计权衡**
  - 为什么UE选择Deferred？
  - 为什么Unity选择Forward+？
  - 两者的目标市场差异

- [ ] **建立心智模型**
  - Unity背景快速理解UE概念
  - 知道何时选择哪个引擎
  - 理解各自的优势领域

- [ ] **指导学习路径**
  - 从Unity转UE需要重点学什么
  - 避免常见的思维误区
  - 找到对应的概念映射

---

## 🔗 导航

- **上一章**：[1.3 核心概念手册](./CoreConcepts.md)
- **下一章**：[1.4 初始化流程](../Phase01_Fundamentals/1.4_Initialization_Deep.md)
- **返回目录**：[README.md](../README.md)

---

## 📚 延伸阅读

### 对比文章
- [Unreal vs Unity: Which Engine to Choose](https://www.unrealengine.com/)
- [Unity vs Unreal: A Rendering Comparison](https://blog.unity.com/)

### 渲染技术对比
- [Deferred vs Forward Rendering](https://learnopengl.com/Advanced-Lighting/Deferred-Shading)
- [Modern Graphics API Comparison](https://www.khronos.org/vulkan/)

---

**恭喜！您已完成阶段一的学习。** 🎉

通过对比Unity，您应该已经建立了对UE5.7渲染架构的完整认知。下一步，我们将深入渲染主流程，从引擎启动到每帧渲染的完整调用链。

**进入阶段二**：[2.1 数据流动机制](../Phase02_Pipeline/2.1_DataFlow_Deep.md) →

