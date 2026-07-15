<a id="topic-core-rendering-modules"></a>

# 1.1 核心模块划分

**UE5.7版本** | 最后更新：2025-10-27

## 📌 本章目标

- 理解UE5.7渲染系统的四大核心模块：Renderer、RenderCore、RHI、Shaders
- 掌握各模块的职责边界与依赖关系
- 建立渲染系统的宏观架构认知

## 🔗 依赖知识

- **前置章节**：无（本章是学习起点）
- **相关章节**：[1.2 线程模型详解](../Phase01_Fundamentals/1.2_ThreadModel_Deep.md)、[1.3 核心概念手册](./CoreConcepts.md)

---

## 一、UE5.7渲染系统架构全景

UE5.7的渲染系统采用**分层模块化架构**，从上到下分为四个核心层次：

```
┌────────────────────────────────────────────┐
│          Renderer Module                    │  ← 高级渲染器（延迟/前向/移动端）
│  - DeferredShadingRenderer                  │     Nanite、Lumen、后处理等
│  - MobileShadingRenderer                    │
│  - SceneRendering、Lighting、Shadow等       │
└────────────────────────────────────────────┘
                    ↓ 依赖
┌────────────────────────────────────────────┐
│        RenderCore Module                    │  ← 渲染核心层
│  - RenderGraph (RDG)                        │     渲染图、资源管理、Shader系统
│  - Shader System                            │
│  - RenderResource、UniformBuffer            │
└────────────────────────────────────────────┘
                    ↓ 依赖
┌────────────────────────────────────────────┐
│           RHI Module                        │  ← 硬件抽象层
│  - RHICommandList                           │     平台无关的图形API抽象
│  - RHIResource (Texture/Buffer/Shader)      │
│  - Platform RHI: D3D12/Vulkan/Metal         │
└────────────────────────────────────────────┘
                    ↓ 依赖
┌────────────────────────────────────────────┐
│            Shaders                          │  ← Shader代码
│  - .usf/.ush文件（HLSL语法）                │     所有HLSL着色器代码
│  - Private/: 具体Shader实现                │
│  - Public/: 共享头文件                      │
└────────────────────────────────────────────┘
```

### 设计理念

1. **分层解耦**：上层只依赖下层，下层不知道上层存在
2. **平台抽象**：RHI层隔离平台差异，上层代码跨平台
3. **模块独立**：每个模块可独立编译，清晰的.Build.cs定义依赖
4. **扩展性强**：新增渲染特性只需修改Renderer层，不影响底层

---

## 二、Renderer模块详解

### 模块定位

**Renderer模块是UE渲染系统的"大脑"**，实现所有高级渲染算法与管线逻辑。

### 源码分析

**来源**： `Engine/Source/Runtime/Renderer/Renderer.Build.cs`  
**版本**： UE5.7

```csharp
// ✅ 可直接使用 (真实代码)
public class Renderer : ModuleRules
{
    public Renderer(ReadOnlyTargetRules Target) : base(Target)
    {
        // 公共依赖：Engine模块提供Scene/Actor/Component等
        PublicDependencyModuleNames.AddRange(
            new string[] {
                "Core",
                "Engine",   // ← 依赖Engine获取GameWorld数据
            }
        );

        // 私有依赖：核心渲染模块
        PrivateDependencyModuleNames.AddRange(
            new string[] {
                "CoreUObject",
                "RenderCore",  // ← 依赖RenderCore（RDG、Shader系统）
                "RHI",         // ← 依赖RHI（硬件抽象层）
                "ImageWriteQueue",
                "MaterialShaderQualitySettings",
                "StateStream",
                "TraceLog",
            }
        );

        // 编辑器额外依赖
        if (Target.bBuildEditor == true)
        {
            PrivateDependencyModuleNames.AddRange(
                new string[] {
                    "TargetPlatform",
                    "GeometryCore",
                    "NaniteUtilities",  // ← 5.0+ Nanite支持
                }
            );
        }

        // Shader路径配置
        PrivateIncludePaths.AddRange(
            new string[] {
                EngineDirectory + "/Shaders/Private",  // ← 访问.usf文件
            }
        );
    }
}
```

### 核心文件清单

**主渲染器**：
- `Private/DeferredShadingRenderer.h/.cpp` - 延迟渲染器（Desktop/Console主力）
- `Private/MobileShadingRenderer.cpp` - 移动端渲染器（Forward+）
- `Private/SceneRendering.h/.cpp` - 场景渲染基础设施

**关键子系统**：
- `Private/BasePassRendering.cpp` - GBuffer Pass实现
- `Private/LightRendering.cpp` - 光照系统
- `Private/ShadowRendering.cpp` - 阴影系统
- `Private/PostProcess/` - 后处理系统
- `Private/Nanite/` - Nanite虚拟几何（UE5）
- `Private/Lumen/` - Lumen全局光照（UE5）
- `Private/MegaLights/` - MegaLights系统（5.7新增）

---

## 三、RenderCore模块详解

### 模块定位

**RenderCore是渲染系统的"基础设施层"**，提供RenderGraph、Shader系统、渲染资源管理等核心功能。

### 源码分析

**来源**： `Engine/Source/Runtime/RenderCore/RenderCore.Build.cs`  
**版本**： UE5.7

```csharp
// ✅ 可直接使用 (真实代码)
public class RenderCore : ModuleRules
{
    public RenderCore(ReadOnlyTargetRules Target) : base(Target)
    {
        // 公共依赖
        PublicDependencyModuleNames.AddRange(
            new string[] { 
                "RHI",         // ← 核心依赖RHI
                "CoreUObject"
            }
        );

        // 私有依赖
        PrivateDependencyModuleNames.Add("Json");  // ← Shader库元数据
        PrivateDependencyModuleNames.Add("BuildSettings");

        // Shader系统支持
        PrivateIncludePathModuleNames.AddRange(
            new string[] { 
                "Shaders",      // ← 访问Shader编译器
                "TargetPlatform"
            }
        );

        // 开发版本支持Shader调试追踪
        if (Target.Configuration != UnrealTargetConfiguration.Shipping)
        {
            PrivateDefinitions.Add("ALLOW_SHADERMAP_TRACKING=1");
        }

        // 复制GPUDumpViewer工具（用于r.DumpGPU命令）
        if (Target.Configuration != UnrealTargetConfiguration.Shipping)
        {
            RuntimeDependencies.Add(
                Path.Combine(Unreal.EngineDirectory.ToString(), 
                "Extras/GPUDumpViewer/..."), 
                StagedFileType.DebugNonUFS
            );
        }
    }
}
```

### 核心功能模块

#### 1. RenderGraph (RDG) 系统

UE5的**革命性渲染框架**，声明式渲染图系统。

**核心文件**：
- `Public/RenderGraphDefinitions.h` - RDG宏定义与配置
- `Public/RenderGraphBuilder.h` - FRDGBuilder构建器API
- `Public/RenderGraphResources.h` - FRDGTexture/FRDGBuffer等资源类型
- `Private/RenderGraphBuilder.cpp` - Execute执行逻辑

**来源**： `Engine/Source/Runtime/RenderCore/Public/RenderGraphDefinitions.h`  
**源码锚点**：搜索 `RDG_ENABLE_DEBUG`、`RDG_EVENTS_STRING_COPY`、`RDG_EVENTS_STRING_REF`。  
**版本**： UE5.7

```cpp
// ✅ 可直接使用 (真实代码)

// RDG调试开关（非Shipping版本启用）
#define RDG_ENABLE_DEBUG (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)

// GPU事件类型配置
#define RDG_EVENTS_STRING_COPY 2  // 完整字符串格式化（开发版）
#define RDG_EVENTS_STRING_REF 1   // 字符串引用（Test/Shipping）

#if WITH_PROFILEGPU
    #if UE_BUILD_TEST || UE_BUILD_SHIPPING
        #define RDG_EVENTS RDG_EVENTS_STRING_REF  // 性能优化
    #else
        #define RDG_EVENTS RDG_EVENTS_STRING_COPY  // 完整调试信息
    #endif
#endif

// RDG异步执行Pass支持
// Pass Lambda可以在并行任务中执行，提升多核利用率
// 使用FRDGAsyncTask标记的Pass不会在Execute()结束时等待
```

#### 2. Shader系统

**核心文件**：
- `Public/Shader.h` - FShader基类
- `Public/GlobalShader.h` - FGlobalShader（全局Shader）
- `Public/MaterialShader.h` - FMaterialShader（材质Shader）
- `Public/ShaderParameterStruct.h` - Shader参数绑定

#### 3. 渲染资源管理

**核心文件**：
- `Public/RenderResource.h` - FRenderResource生命周期管理
- `Public/UniformBuffer.h` - UniformBuffer系统
- `Public/RenderTargetPool.h` - RenderTarget资源池

---

## 四、RHI模块详解

### 模块定位

**RHI (Rendering Hardware Interface) 是硬件抽象层**，隔离不同图形API（D3D12/Vulkan/Metal）的差异。

### 源码分析

**来源**： `Engine/Source/Runtime/RHI/RHI.Build.cs`  
**版本**： UE5.7

```csharp
// ✅ 可直接使用 (真实代码)
public class RHI : ModuleRules
{
    public RHI(ReadOnlyTargetRules Target) : base(Target)
    {
        // 基础依赖（不依赖任何渲染模块）
        PrivateDependencyModuleNames.Add("Core");
        PrivateDependencyModuleNames.Add("TraceLog");
        PrivateDependencyModuleNames.Add("ApplicationCore");
        PrivateDependencyModuleNames.Add("Cbor");  // GPU Crash分析
        PrivateDependencyModuleNames.Add("BuildSettings");

        // 多GPU支持（仅Windows Desktop）
        PublicDefinitions.AddDefinition("WITH_MGPU", 
            Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) && 
            Target.Platform.IsInGroup(UnrealPlatformGroup.Desktop)
        );

        if (Target.bCompileAgainstEngine)
        {
            // 加载NullDrv（无头渲染，用于服务器）
            DynamicallyLoadedModuleNames.Add("NullDrv");

            if (Target.Type != TargetRules.TargetType.Server)
            {
                // Windows平台动态加载D3D11/D3D12/Vulkan/OpenGL
                if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
                {
                    DynamicallyLoadedModuleNames.Add("D3D11RHI");
                    DynamicallyLoadedModuleNames.Add("D3D12RHI");  // ← 主力RHI
                    DynamicallyLoadedModuleNames.Add("VulkanRHI");
                    DynamicallyLoadedModuleNames.Add("OpenGLDrv");
                }

                // Linux/Unix平台
                if (Target.Platform.IsInGroup(UnrealPlatformGroup.Unix))
                {
                    DynamicallyLoadedModuleNames.Add("VulkanRHI");
                    DynamicallyLoadedModuleNames.Add("OpenGLDrv");
                }
            }
        }
    }
}
```

### RHI核心定义

**来源**： `Engine/Source/Runtime/RHI/Public/RHIDefinitions.h`  
**源码锚点**：搜索 `enum class ERHIInterfaceType`、`RHI_RAYTRACING`。  
**版本**： UE5.7

```cpp
// ✅ 可直接使用 (真实代码)

// 支持的图形API类型
enum class ERHIInterfaceType
{
    Hidden,   // 不显示
    Null,     // 无头渲染
    D3D11,    // DirectX 11
    D3D12,    // DirectX 12 ← Windows主力
    Vulkan,   // Vulkan ← 跨平台
    Metal,    // Metal (macOS/iOS)
    Agx,      // Apple Silicon优化
    OpenGL,   // OpenGL (传统)
};

// RHI特性支持级别
enum class ERHIFeatureSupport : uint8
{
    Unsupported,         // 完全不支持
    RuntimeDependent,    // 取决于硬件/驱动
    RuntimeGuaranteed,   // 运行时保证支持
};

// Shader参数结构对齐要求（必须16字节边界）
#define SHADER_PARAMETER_STRUCT_ALIGNMENT 16

// Shader数组元素对齐
#define SHADER_PARAMETER_ARRAY_ELEMENT_ALIGNMENT 16

// RHI Buffer对齐
#define RHI_RAW_VIEW_ALIGNMENT 16

// 光追支持开关（编译期宏）
#ifndef RHI_RAYTRACING
#define RHI_RAYTRACING 0  // 默认关闭，需平台支持
#endif
```

### RHI核心组件

#### 1. RHICommandList 命令队列

**来源**： `Engine/Source/Runtime/RHI/Public/RHICommandList.h` [[memory:9199239]]  
**源码锚点**：搜索 `class FRHICommandListBase`、`class FRHICommandListImmediate`、`FRHICommandListExecutor`。  
**版本**： UE5.7

这是**UE渲染系统的命令核心**，延迟执行的GPU命令系统。

```cpp
// 📝 简化示例 (核心概念)

// 三种命令列表继承关系
class FRHICommandListBase           // 基类：命令分配与存储
class FRHIComputeCommandList        // 计算命令列表
class FRHICommandList               // 图形命令列表（继承Compute）
class FRHICommandListImmediate      // 立即执行命令列表（RenderThread专用）

// 命令示例：绘制Indexed Primitive
RHICmdList.DrawIndexedPrimitive(
    IndexBuffer,    // IB
    BaseVertexIndex,
    MinIndex,
    NumVertices,
    StartIndex,
    NumPrimitives,
    NumInstances
);
```

#### 2. RHI资源类型

- `FRHITexture2D/3D/Cube` - 纹理资源
- `FRHIBuffer` - Buffer资源（Vertex/Index/Uniform/Structured）
- `FRHIShader` - Shader资源
- `FRHIRenderTargetView` - RenderTarget视图
- `FRHIDepthStencilView` - DepthStencil视图

---

## 五、Shaders模块详解

### 模块位置

`Engine/Shaders/` 目录包含所有HLSL着色器代码（.usf/.ush文件）。

### 目录结构

```
Engine/Shaders/
├── Private/          ← 具体Shader实现
│   ├── BasePassPixelShader.usf        // BasePass PS
│   ├── BasePassVertexShader.usf       // BasePass VS
│   ├── DeferredLightPixelShaders.usf  // 延迟光照
│   ├── PostProcessTonemap.usf         // 后处理
│   ├── Nanite/                        // Nanite Shader (5.0+)
│   ├── Lumen/                         // Lumen Shader (5.0+)
│   └── ...
│
├── Public/           ← 共享头文件
│   └── Platform/     // 平台特定定义
│
└── Shared/           ← C++/Shader共享头文件
```

### Shader文件类型

- **.usf** (Unreal Shader File)：Shader实现文件（类似.hlsl）
- **.ush** (Unreal Shader Header)：Shader头文件（类似.h）

### 示例：BasePass Pixel Shader

**来源**： `Engine/Shaders/Private/BasePassPixelShader.usf`  
**源码锚点**：搜索 `SetGBufferForShadingModel`、`EncodeGBufferToMRT`、`EncodeGBuffer`。  
**版本**： UE5.7

```hlsl
// 📝 简化示例 (核心流程)

// 包含头文件
#include "Common.ush"                    // 通用函数
#include "BasePassCommon.ush"            // BasePass共享代码
#include "DeferredShadingCommon.ush"     // GBuffer编码

// Shader入口函数
void Main(
    FBasePassVSToPS Input,              // VS输出
    out float4 OutTarget0 : SV_Target0, // GBufferA
    out float4 OutTarget1 : SV_Target1, // GBufferB
    out float4 OutTarget2 : SV_Target2, // GBufferC
    // ... 更多GBuffer RT
)
{
    // 1. 获取材质参数
    FMaterialPixelParameters MaterialParameters = 
        GetMaterialPixelParameters(Input);
    
    // 2. 执行材质图计算（MaterialTemplate.ush生成）
    CalcMaterialParameters(MaterialParameters, ...);
    
    // 3. 获取材质属性
    float3 BaseColor = GetMaterialBaseColor(MaterialParameters);
    float Metallic = GetMaterialMetallic(MaterialParameters);
    float Roughness = GetMaterialRoughness(MaterialParameters);
    float3 Normal = MaterialParameters.WorldNormal;
    
    // 4. 编码到GBuffer
    FGBufferData GBuffer = (FGBufferData)0;
    GBuffer.BaseColor = BaseColor;
    GBuffer.Metallic = Metallic;
    GBuffer.Roughness = Roughness;
    GBuffer.WorldNormal = Normal;
    
    // 5. 写入多个RenderTarget
    EncodeGBuffer(GBuffer, OutTarget0, OutTarget1, OutTarget2, ...);
}
```

---

## 六、模块依赖关系图

```
┌─────────────────────────────────────────────────┐
│            Game/Editor Layer                     │
│    (UWorld, AActor, UPrimitiveComponent...)     │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│             Engine Module                        │  ← 游戏引擎核心
│  - FScene (场景容器)                             │
│  - UPrimitiveComponent (组件系统)                │
│  - FPrimitiveSceneProxy (渲染代理)               │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│            Renderer Module                       │  ← 高级渲染算法
│  - FSceneRenderer                                │
│  - FDeferredShadingSceneRenderer                 │
│  - Nanite/Lumen/PostProcess...                   │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│           RenderCore Module                      │  ← 渲染基础设施
│  - FRDGBuilder (RenderGraph)                     │
│  - FShader, FGlobalShader                        │
│  - FRenderResource, FUniformBuffer               │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│              RHI Module                          │  ← 硬件抽象层
│  - FRHICommandList                               │
│  - FRHITexture, FRHIBuffer                       │
│  - IRHIPlatform Interface                        │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│      Platform RHI (D3D12/Vulkan/Metal)          │  ← 平台实现
│  - FD3D12DynamicRHI                              │
│  - FVulkanDynamicRHI                             │
│  - Direct API Calls                              │
└─────────────────────────────────────────────────┘
                     ↓
┌─────────────────────────────────────────────────┐
│        Graphics Driver & GPU                     │  ← 硬件
└─────────────────────────────────────────────────┘

       Shaders (.usf/.ush) 被编译后注入到各层
```

### 依赖规则

1. **单向依赖**：上层可以依赖下层，下层不依赖上层
2. **接口隔离**：RHI通过IRHIPlatform接口隔离平台差异
3. **编译期检查**：.Build.cs文件强制依赖检查
4. **运行期加载**：平台RHI通过`DynamicallyLoadedModuleNames`动态加载

---

## 七、🆚 与Unity URP对比

| 维度 | UE5.7 | Unity URP | 设计思路差异 |
|------|-------|-----------|--------------|
| **架构模式** | 模块化（Module） | 包化（Package） | UE编译期强依赖，Unity运行期Package依赖 |
| **渲染抽象层** | RHI（完整抽象层） | GraphicsDevice（轻量封装） | UE有独立RHI模块，Unity直接封装底层API |
| **Shader组织** | .usf/.ush（独立目录树） | .hlsl（嵌入SRP Package） | UE Shader独立于代码，Unity Shader与SRP绑定 |
| **渲染图系统** | RenderGraph（声明式，5.0+统一） | SRP Context（命令式） | UE自动管理资源，Unity手动管理 |
| **模块依赖** | .Build.cs明确声明 | asmdef运行期解析 | UE编译期检查，Unity灵活但易出错 |
| **平台RHI** | 动态加载（D3D12/Vulkan等） | 编译期选择 | UE运行期切换RHI，Unity编译期确定 |

### 为什么UE这样设计？

1. **工业级需求**：大型项目需要严格的模块边界与依赖管理
2. **性能优先**：RDG自动优化资源生命周期，减少手动错误
3. **跨平台能力**：RHI完全隔离平台差异，上层代码100%跨平台
4. **引擎演进**：模块化便于替换子系统（如UE4→UE5的RDG重构）

Unity的轻量化设计适合快速迭代，UE的工程化设计适合大规模协作。

---

## 八、🔬 实践验证

### 1. 验证模块加载

在游戏启动时，查看日志输出：

```
Log: LogModuleManager: Module 'RHI' loaded in 0.12 seconds
Log: LogModuleManager: Module 'RenderCore' loaded in 0.25 seconds
Log: LogModuleManager: Module 'Renderer' loaded in 0.48 seconds
Log: LogD3D12RHI: [D3D12] Adapter: NVIDIA GeForce RTX 4090
```

### 2. 控制台命令验证

```
// 查看当前RHI类型
r.RHI.Name

// 输出示例：D3D12

// 查看Shader统计
r.ShaderDevelopmentMode 1
```

---

## ✅ 检查点

完成本章后，您将能够：

- [ ] **说出四大模块的名称与职责**
  - Renderer: 高级渲染算法
  - RenderCore: RDG+Shader系统
  - RHI: 硬件抽象层
  - Shaders: HLSL代码

- [ ] **理解模块依赖关系**
  - 单向依赖（上→下）
  - RHI隔离平台差异
  - .Build.cs定义依赖

- [ ] **找到关键源码文件**
  - Renderer.Build.cs
  - RenderGraphBuilder.h
  - RHICommandList.h

- [ ] **对比UE与Unity的架构差异**
  - UE模块化 vs Unity包化
  - RHI vs GraphicsDevice
  - RDG vs SRP Context

---

## 🔗 导航

- **上一章**：无（本章是起点）
- **下一章**：[1.2 线程模型详解](../Phase01_Fundamentals/1.2_ThreadModel_Deep.md)
- **返回目录**：[README.md](../README.md)

---

## 📚 延伸阅读

### 官方文档
- [Unreal Engine Module System](https://docs.unrealengine.com/5.7/en-US/unreal-engine-modules/)
- [RHI Overview](https://docs.unrealengine.com/5.7/en-US/rhi-overview-for-unreal-engine/)
- [RenderGraph Documentation](https://docs.unrealengine.com/5.7/en-US/render-dependency-graph-in-unreal-engine/)

### 源码推荐阅读
- `Engine/Source/Runtime/Launch/Private/LaunchEngineLoop.cpp` - 模块加载流程
- `Engine/Source/Runtime/Core/Public/Modules/ModuleManager.h` - 模块管理器

---

**恭喜！您已完成第一步，建立了UE5.7渲染系统的宏观认知。** 🎉

下一步，我们将深入学习UE的**三线程渲染架构**，理解GameThread/RenderThread/RHIThread如何协作。

**继续前进**：[1.2 线程模型详解](../Phase01_Fundamentals/1.2_ThreadModel_Deep.md) →

