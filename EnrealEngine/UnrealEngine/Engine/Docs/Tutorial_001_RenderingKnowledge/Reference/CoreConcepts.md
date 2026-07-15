<a id="topic-core-rendering-concepts"></a>

# 1.3 核心概念手册

**UE5.7版本** | 最后更新：2025-10-27

## ⚠️ 性能数据说明

本文档中的百分比、内存占用和数量级数字均为**教学示意**，用于帮助建立概念，不是官方 Benchmark，也不能直接套用到项目预算。实际结果受平台、RHI、驱动、场景规模、资源格式、分辨率和项目配置影响，需用 Unreal Insights、RenderDoc、平台 GPU 工具和引擎统计命令在目标项目中复测。

## 📌 本章目标

- 建立UE5.7渲染系统的核心概念心智模型
- 理解FScene、FSceneView、FPrimitiveSceneProxy等关键类型
- 掌握MeshDrawCommand缓存机制
- 理解RenderGraph的资源体系
- 理解UniformBuffer的作用与使用

## 🔗 依赖知识

- **前置章节**：[1.1 核心模块划分](./CoreModules.md)、[1.2 线程模型详解](../Phase01_Fundamentals/1.2_ThreadModel_Deep.md)
- **相关章节**：[1.1 渲染循环调用链](../Phase01_Fundamentals/1.1_RenderLoop_Deep.md)

---

## 一、FScene - 场景容器

### 概念定位

**FScene是UE渲染世界的"数据库"**，包含所有可渲染对象、光源、反射探针等。

### 核心职责

```
FScene的职责：
1. 管理所有Primitive（网格物体）
2. 管理所有Light（光源）
3. 管理ReflectionCapture（反射捕获）
4. 维护空间加速结构（Octree）
5. 提供查询接口（可见性、拾取等）
```

### 源码分析

**来源**： `Engine/Source/Runtime/Engine/Public/SceneInterface.h`  
**源码锚点**：搜索 `class FSceneInterface`、`AddPrimitive`、`UpdatePrimitiveTransform`。  
**版本**： UE5.7

```cpp
// ✅ 可直接使用 (真实代码)
class FSceneInterface
{
public:
    // 【添加/删除Primitive】
    virtual void AddPrimitive(UPrimitiveComponent* Primitive) = 0;
    virtual void RemovePrimitive(UPrimitiveComponent* Primitive) = 0;
    
    // 【批量操作优化】
    virtual void BatchAddPrimitives(TArrayView<UPrimitiveComponent*> InPrimitives) = 0;
    virtual void BatchRemovePrimitives(TArrayView<UPrimitiveComponent*> InPrimitives) = 0;
    
    // 【更新Primitive状态】
    virtual void UpdatePrimitiveTransform(UPrimitiveComponent* Primitive) = 0;
    virtual void UpdateCustomPrimitiveData(UPrimitiveComponent* Primitive) = 0;
    
    // 【添加/删除Light】
    virtual void AddLight(ULightComponent* Light) = 0;
    virtual void RemoveLight(ULightComponent* Light) = 0;
    
    // 【查询接口】
    virtual FPrimitiveSceneInfo* GetPrimitiveSceneInfo(int32 PrimitiveIndex) const = 0;
    
    // 【场景更新】
    // 处理所有Pending的添加/删除/更新操作
    virtual void UpdateAllPrimitiveSceneInfos(
        FRDGBuilder& GraphBuilder, 
        EUpdateAllPrimitiveSceneInfosAsyncOps AsyncOps = EUpdateAllPrimitiveSceneInfosAsyncOps::None
    ) = 0;
};
```

### FScene vs UWorld

```
┌─────────────────────────┐
│        UWorld           │  ← GameThread层（游戏世界）
│  - AActor               │     包含游戏逻辑对象
│  - ULevel               │
│  - GameState            │
└─────────────────────────┘
            │ 1:1
            ↓
┌─────────────────────────┐
│        FScene           │  ← RenderThread层（渲染场景）
│  - FPrimitiveSceneInfo  │     只包含渲染数据
│  - FLightSceneInfo      │
│  - Octree               │
└─────────────────────────┘
```

**设计理念**：GameThread和RenderThread数据分离，避免线程竞争。

---

## 二、FSceneView - 视角抽象

### 概念定位

**FSceneView代表一个相机视角**，包含ViewMatrix、Projection、Frustum等所有渲染所需的视角信息。

### 核心结构

**来源**： `Engine/Source/Runtime/Engine/Public/SceneView.h`  
**源码锚点**：搜索 `struct FSceneViewProjectionData`、`ViewOrigin`、`ProjectionMatrix`。  
**版本**： UE5.7

```cpp
// ✅ 可直接使用 (真实代码)
struct FSceneViewProjectionData
{
    /** 视点位置（世界空间） */
    FVector ViewOrigin = FVector::ZeroVector;
    
    /** View矩阵：世界空间 → 视角空间 */
    FMatrix ViewRotationMatrix = FMatrix::Identity;
    
    /** 投影矩阵：UE使用反向Z（Z=1是近平面，Z=0是远平面） */
    FMatrix ProjectionMatrix = FMatrix::Identity;
    
    /** 视口矩形（屏幕像素坐标） */
    FIntRect ViewRect = FIntRect(0,0,0,0);
    
    /** 计算ViewProjection矩阵 */
    FMatrix ComputeViewProjectionMatrix() const
    {
        // World → View → Projection
        return FTranslationMatrix(-ViewOrigin) 
             * ViewRotationMatrix 
             * ProjectionMatrix;
    }
    
    /** 判断是否透视投影 */
    bool IsPerspectiveProjection() const
    {
        // 透视投影：ProjectionMatrix.M[3][3] < 1
        // 正交投影：ProjectionMatrix.M[3][3] == 1
        return ProjectionMatrix.M[3][3] < 1.0f;
    }
};
```

### FSceneView的关键成员

```cpp
// 📝 简化示例 (核心成员)
class FSceneView
{
public:
    // 【投影数据】
    FSceneViewProjectionData ProjectionData;
    
    // 【视锥体】（用于剔除）
    FConvexVolume ViewFrustum;
    
    // 【视角状态】（持久化数据，跨帧共享）
    FSceneViewStateInterface* ViewState;
    
    // 【后处理设置】
    FFinalPostProcessSettings FinalPostProcessSettings;
    
    // 【Show Flags】（控制渲染特性开关）
    FEngineShowFlags ShowFlags;
    
    // 【UniformBuffer】（传递给Shader）
    TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
};
```

### 多View场景

UE支持多个View同时渲染：

```
单帧多View示例：
┌────────────┐
│ Split Screen│  ← 2个View（2个玩家）
├──────┬─────┤
│View0 │View1│
└──────┴─────┘

┌────────────┐
│ VR Stereo  │  ← 2个View（左右眼）
├──────┬─────┤
│ Left │Right│
└──────┴─────┘

┌────────────┐
│ Reflection │  ← 主View + 多个反射View
│  + Planar  │
│  Reflection│
└────────────┘
```

---

## 三、FPrimitiveSceneProxy - 渲染代理

### 概念定位

**FPrimitiveSceneProxy是GameThread对象在RenderThread侧的显式渲染代理 / 快照**，解决线程安全问题。

### 代理模式设计

```
GameThread (游戏逻辑)          RenderThread (渲染)
┌──────────────────┐           ┌──────────────────┐
│ UPrimitiveComponent│◄────────┤FPrimitiveSceneProxy│
│ - Location       │   创建时   │ - LocalToWorld   │
│ - Material       │   拷贝数据 │ - Materials      │
│ - Visibility     │           │ - Bounds         │
└──────────────────┘           └──────────────────┘
      │ Tick()                        │ DrawDynamicElements()
      │ 可以随时修改                   │ RenderThread读取（线程安全）
      ↓                               ↓
  GameThread可随时修改           RenderThread只读访问
```

### 核心职责

```cpp
// 📝 简化示例 (FPrimitiveSceneProxy核心职责)
class FPrimitiveSceneProxy
{
public:
    // 【构造时摘取稳定的渲染描述；Transform/Bounds通常在注册命令中写入】
    FPrimitiveSceneProxy(const UPrimitiveComponent* Component)
    {
        // 摘取可渲染资源引用、材质代理、可见性/阴影/LOD等标志
        // 注意：真实 StaticMesh 路径会经过 SceneProxyDesc；
        // Transform/Bounds 会在 RenderThread add/update 命令中通过 SetTransform 写入。
    }
    
    // 【提供绘制接口】
    virtual void GetDynamicMeshElements(
        const TArray<const FSceneView*>& Views,
        const FSceneViewFamily& ViewFamily,
        uint32 VisibilityMap,
        FMeshElementCollector& Collector
    ) const {}
    
    // 【提供可见性判断数据】
    virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const {}
    
    // 【更新Transform】
    void UpdateTransform_RenderThread(const FMatrix& NewLocalToWorld)
    {
        check(IsInRenderingThread());
        LocalToWorld = NewLocalToWorld;
    }
    
private:
    FMatrix LocalToWorld;    // Transform（线程安全副本）
    FBoxSphereBounds Bounds; // 包围盒
    TArray<UMaterialInterface*> Materials;
};
```

### 常见Proxy类型

```
UStaticMeshComponent  → FStaticMeshSceneProxy
USkeletalMeshComponent → FSkeletalMeshSceneProxy
UPointLightComponent  → FPointLightSceneProxy
UDecalComponent       → FDecalSceneProxy
```

---

## 四、MeshDrawCommand - 绘制命令缓存

### 概念定位

**MeshDrawCommand是UE5最重要的性能优化**，将DrawCall相关的所有数据（PSO、VB、IB、Shader绑定）缓存起来，避免每帧重建。

### 结构组成

**来源**： `Engine/Source/Runtime/Renderer/Public/MeshDrawCommands.h`  
**版本**： UE5.7

```cpp
// 📝 简化示例 (核心结构)
struct FMeshDrawCommand
{
    // 【图形管线状态对象】
    FGraphicsMinimalPipelineStateId CachedPipelineId;
    
    // 【Vertex/Index Buffer】
    uint32 VertexBuffers;  // FMeshDrawCommandVertexBufferArray索引
    FRHIIndexBuffer* IndexBuffer;
    uint32 FirstIndex;
    uint32 NumPrimitives;
    uint32 NumInstances;
    
    // 【Shader绑定】
    FMeshDrawShaderBindings ShaderBindings; // Uniform Buffer、Texture等
    
    // 【实例化数据】
    uint32 PrimitiveIdBufferOffset; // GPUScene中的Primitive ID
    
    // 【排序键】（用于减少状态切换）
    uint64 SortKey;
    
    // 【提交到RHI】
    void SubmitDraw(
        const FMeshDrawCommand& MeshDrawCommand,
        const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
        FRHICommandList& RHICmdList,
        ...
    ) const
    {
        // 1. 设置PSO
        SetGraphicsMinimalPipelineState(RHICmdList, CachedPipelineId);
        
        // 2. 绑定Shader参数
        ShaderBindings.SetOnCommandList(RHICmdList);
        
        // 3. 绑定VB/IB
        RHICmdList.SetStreamSource(0, VertexBuffer, 0);
        
        // 4. DrawCall
        RHICmdList.DrawIndexedPrimitive(
            IndexBuffer,
            0, 0, NumVertices,
            FirstIndex, NumPrimitives, NumInstances
        );
    }
};
```

### 缓存策略

```
静态MeshDrawCommand（缓存持久化）：
- 静态网格、不变材质
- 只在场景改变时重建（添加/删除物体）
- 存储在FScene中

动态MeshDrawCommand（每帧重建）：
- SkeletalMesh（骨骼动画）
- Particle System
- 动态材质参数
- 每帧在Visibility Pass后构建
```

### MeshPass类型

**来源**： `Engine/Source/Runtime/Renderer/Public/MeshPassProcessor.h`  
**源码锚点**：搜索 `namespace EMeshPass`、`enum Type`、`BasePass`。  
**版本**： UE5.7

```cpp
// ✅ 可直接使用 (真实代码)
namespace EMeshPass
{
    enum Type : uint8
    {
        DepthPass,               // EarlyZ/深度预通道
        BasePass,                // GBuffer Pass（主要渲染）
        SkyPass,                 // 天空盒
        CSMShadowDepth,          // CSM阴影深度
        VSMShadowDepth,          // Virtual Shadow Maps深度
        TranslucencyStandard,    // 标准半透明
        TranslucencyAfterDOF,    // DOF后半透明
        Velocity,                // 运动矢量
        CustomDepth,             // 自定义深度
        LumenCardCapture,        // Lumen Surface Cache
        NaniteMeshPass,          // Nanite几何
        // ... 共39种Pass类型
        Num,
    };
}
```

### 性能优势

```
传统方式（UE4早期）：
每帧每个物体：
  1. 查找Shader
  2. 创建PSO
  3. 绑定参数
  4. DrawCall
→ CPU开销极大

MeshDrawCommand缓存（UE4.22+）：
首次构建：
  缓存所有状态
每帧：
  直接提交DrawCall
→ 可显著降低重复构建开销，具体幅度需按场景实测
```

---

## 五、RenderGraph资源体系

### RDG核心类型

**来源**： `Engine/Source/Runtime/RenderCore/Public/RenderGraphResources.h`  
**版本**： UE5.7

```cpp
// ✅ 可直接使用 (真实代码结构)

// 【RDG Texture】
class FRDGTexture
{
    // RDG管理的纹理，自动处理生命周期
    FRHITexture* GetRHI() const; // 获取底层RHI纹理
    FRDGTextureDesc Desc;        // 纹理描述符
};

// 【RDG Buffer】
class FRDGBuffer
{
    // RDG管理的Buffer（Vertex/Index/Structured/Uniform）
    FRHIBuffer* GetRHI() const;
    FRDGBufferDesc Desc;
};

// 【RDG Texture Ref】（智能指针）
using FRDGTextureRef = FRDGTexture*;
using FRDGBufferRef = FRDGBuffer*;
```

### 资源生命周期

```
RDG资源生命周期：
┌──────────────────────────────────────┐
│ GraphBuilder.CreateTexture()         │ ← 声明资源
│   - 此时不分配物理内存               │
│   - 只记录依赖关系                   │
└──────────────────────────────────────┘
            ↓
┌──────────────────────────────────────┐
│ GraphBuilder.AddPass()               │ ← Pass声明使用
│   - Parameters->InputTexture = Tex   │
│   - 记录读写依赖                     │
└──────────────────────────────────────┘
            ↓
┌──────────────────────────────────────┐
│ GraphBuilder.Execute()               │ ← 执行期
│   - 编译Pass依赖图                   │
│   - 分配Transient资源（内存复用）    │
│   - 插入Barrier/Transition           │
│   - 执行Pass Lambda                  │
│   - 自动回收资源                     │
└──────────────────────────────────────┘
```

### Transient Resource（瞬态资源）

```
Transient资源优化：
Pass A: 创建RT_Temp (1920×1080 RGBA8) → 使用
Pass B: RT_Temp销毁
Pass C: 创建RT_Temp2 (1920×1080 RGBA8) → 复用相同内存！

内存节省：
传统方式：RT_Temp + RT_Temp2 = 16MB
Transient：max(RT_Temp, RT_Temp2) = 8MB
```

---

## 六、UniformBuffer - Shader常量传递

### 概念定位

**UniformBuffer是CPU向GPU传递常量数据的机制**，如ViewMatrix、LightParameters等。

### 定义UniformBuffer

```cpp
// ✅ 可直接使用 (真实代码示例)

// 【定义Uniform Buffer结构】
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FViewUniformShaderParameters, ENGINE_API)
    // View相关
    SHADER_PARAMETER(FMatrix44f, ViewToClip)
    SHADER_PARAMETER(FMatrix44f, ClipToView)
    SHADER_PARAMETER(FMatrix44f, TranslatedWorldToClip)
    SHADER_PARAMETER(FMatrix44f, WorldToClip)
    
    // 相机位置
    SHADER_PARAMETER(FVector3f, WorldCameraOrigin)
    SHADER_PARAMETER(FVector3f, TranslatedWorldCameraOrigin)
    
    // 视口尺寸
    SHADER_PARAMETER(FVector4f, BufferSizeAndInvSize) // (Width, Height, 1/Width, 1/Height)
    
    // 时间
    SHADER_PARAMETER(float, GameTime)
    SHADER_PARAMETER(float, RealTime)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

// 【创建Uniform Buffer】
TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;

FViewUniformShaderParameters ViewParams;
ViewParams.WorldToClip = ViewProjectionMatrix;
ViewParams.WorldCameraOrigin = CameraPosition;
// ... 填充其他参数

ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(
    ViewParams, 
    UniformBuffer_SingleFrame
);
```

### Shader中使用

```hlsl
// ✅ 可直接使用 (真实Shader代码)

// 声明Uniform Buffer
cbuffer View
{
    float4x4 ViewToClip;
    float4x4 WorldToClip;
    float3 WorldCameraOrigin;
    float4 BufferSizeAndInvSize;
    float GameTime;
};

// 使用
float4 VertexShaderMain(float3 LocalPosition : POSITION) : SV_POSITION
{
    // 使用ViewUniformBuffer的数据
    float4 WorldPos = mul(float4(LocalPosition, 1.0), LocalToWorld);
    float4 ClipPos = mul(WorldPos, WorldToClip);
    return ClipPos;
}
```

### UniformBuffer更新策略

```
频率分类：
┌─────────────────────────────────────┐
│ Per-Frame (每帧更新)                 │
│ - ViewUniformBuffer                 │
│ - FrameUniformBuffer                │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ Per-View (每个View)                 │
│ - ViewUniformBuffer (多View各自独立)│
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ Per-Primitive (每个物体)             │
│ - PrimitiveUniformBuffer            │
│ - 包含LocalToWorld、Bounds等        │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ Per-Material (每个材质)              │
│ - MaterialUniformBuffer             │
│ - 材质参数（颜色、标量等）          │
└─────────────────────────────────────┘
```

---

## 七、SceneTextures - GBuffer集合

### 概念定位

**SceneTextures是一帧渲染产生的所有RenderTarget的集合**，包括GBuffer、深度、SceneColor等。

### 主要纹理

```cpp
// 📝 简化示例 (SceneTextures成员)
struct FSceneTextures
{
    // 【深度/模板】
    FRDGTextureRef Depth;           // 场景深度（格式：R32F或D24S8）
    
    // 【GBuffer】（延迟渲染）
    FRDGTextureRef GBufferA;        // Normal + ShadingModelID
    FRDGTextureRef GBufferB;        // Metallic + Specular + Roughness
    FRDGTextureRef GBufferC;        // BaseColor + AO
    FRDGTextureRef GBufferD;        // CustomData
    FRDGTextureRef GBufferE;        // PrecomputedShadow
    
    // 【场景颜色】
    FRDGTextureRef Color;           // 最终渲染颜色（HDR）
    
    // 【运动矢量】
    FRDGTextureRef Velocity;        // 用于运动模糊、TAA
    
    // 【自定义深度】
    FRDGTextureRef CustomDepth;     // 用于描边等特效
};
```

### 访问SceneTextures

```cpp
// ✅ 可直接使用 (真实代码示例)

// 在RDG Pass中访问SceneTextures
GraphBuilder.AddPass(
    RDG_EVENT_NAME("MyLightingPass"),
    PassParameters,
    ERDGPassFlags::Raster,
    [PassParameters](FRHICommandList& RHICmdList)
    {
        // 从GBuffer读取
        FRDGTextureRef GBufferA = PassParameters->SceneTextures.GBufferA;
        FRDGTextureRef Depth = PassParameters->SceneTextures.Depth;
        
        // 写入SceneColor
        FRDGTextureRef SceneColor = PassParameters->RenderTargets[0].GetTexture();
    });
```

---

## 八、核心概念关系图

```
                    ┌─────────────┐
                    │   UWorld    │ ← GameThread: 游戏世界
                    └──────┬──────┘
                           │ 1:1
                    ┌──────▼──────┐
                    │   FScene    │ ← RenderThread: 渲染场景
                    │  (容器)     │
                    └──────┬──────┘
                           │ 包含
          ┌────────────────┼────────────────┐
          │                │                │
  ┌───────▼────────┐ ┌────▼─────┐  ┌──────▼────────┐
  │FPrimitiveSceneInfo│ │FLightSceneInfo│ │ReflectionCapture│
  │   (物体信息)   │ │  (光源信息)│ │   (反射探针)  │
  └───────┬────────┘ └──────────┘  └───────────────┘
          │ 引用
  ┌───────▼────────┐
  │FPrimitiveSceneProxy│ ← 渲染代理（线程安全）
  │  - Bounds      │
  │  - Materials   │
  │  - Transform   │
  └───────┬────────┘
          │ 生成
  ┌───────▼────────┐
  │ MeshDrawCommand│ ← 绘制命令缓存
  │  - PSO         │
  │  - VB/IB       │
  │  - ShaderBindings│
  └────────────────┘

每帧渲染：
┌──────────────┐
│ FSceneView   │ ← 相机视角
│ - ViewMatrix │
│ - Projection │
│ - Frustum    │
└──────┬───────┘
       │ 传递给
┌──────▼───────┐
│FRDGBuilder   │ ← RenderGraph构建器
│ - 添加Pass   │
│ - 创建资源   │
│ - Execute    │
└──────┬───────┘
       │ 产生
┌──────▼───────┐
│SceneTextures │ ← 渲染结果
│ - GBuffer    │
│ - Depth      │
│ - Color      │
└──────────────┘
```

---

## 九、🔬 实践验证

### 实践1：查看Scene内容

```cpp
// ✅ 可直接使用 - 控制台命令

// 查看Scene统计
stat scenerendering

// 输出：
// Primitives: 2543         ← Scene中的Primitive数量
// Lights: 23               ← 光源数量
// StaticMeshes: 1820
// SkeletalMeshes: 15
```

### 实践2：查看MeshDrawCommand统计

```cpp
// 控制台命令
r.MeshDrawCommands.LogMeshDrawCommandMemoryStats 1

// 输出：
// Cached MeshDrawCommands: 15234      ← 缓存的MDC数量
// Memory: 45.2 MB                     ← 示例占用，具体需实测
// Static: 14500 (95%)                 ← 静态缓存
// Dynamic: 734 (5%)                   ← 动态重建
```

### 实践3：可视化UniformBuffer内容

```cpp
// 在Shader中输出View参数验证
float4 DebugPS() : SV_Target
{
    // 输出相机位置（归一化到0-1）
    return float4(frac(WorldCameraOrigin * 0.001), 1);
}
```

---

## ✅ 检查点

完成本章后，您将能够：

- [ ] **理解核心概念关系**
  - FScene是什么：渲染世界容器
  - FSceneView是什么：相机视角抽象
  - FPrimitiveSceneProxy是什么：线程安全的渲染代理
  - MeshDrawCommand是什么：绘制命令缓存

- [ ] **理解数据流**
  - UPrimitiveComponent → FPrimitiveSceneProxy → MeshDrawCommand
  - GameThread → RenderThread的数据传递机制
  - UniformBuffer如何传递常量到Shader

- [ ] **理解RDG资源**
  - FRDGTexture vs FRHITexture
  - Transient Resource的优势
  - SceneTextures的组成

- [ ] **找到关键源码**
  - SceneInterface.h
  - SceneView.h
  - MeshPassProcessor.h
  - RenderGraphResources.h

---

## 🔗 导航

- **上一章**：[1.2 线程模型详解](../Phase01_Fundamentals/1.2_ThreadModel_Deep.md)
- **下一章**：[1.4 架构对比Unity](./CompareWithUnity.md)
- **返回目录**：[README.md](../README.md)

---

## 📚 延伸阅读

### 官方文档
- [Scene Management](https://docs.unrealengine.com/5.7/en-US/scene-management/)
- [Render Dependency Graph](https://docs.unrealengine.com/5.7/en-US/render-dependency-graph-in-unreal-engine/)

### 深入源码
- `Engine/Source/Runtime/Engine/Private/Scene.cpp` - FScene实现
- `Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp` - 场景渲染主流程
- `Engine/Source/Runtime/Renderer/Private/MeshDrawCommands.cpp` - MDC系统

---

**恭喜！您已掌握UE5.7渲染系统的核心概念。** 🎉

这些概念是理解后续所有渲染流程的基础。下一步，我们将对比UE与Unity的架构差异，帮助有Unity背景的开发者快速建立UE心智模型。

**继续前进**：[1.4 架构对比Unity](./CompareWithUnity.md) →

