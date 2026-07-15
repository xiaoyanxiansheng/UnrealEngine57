# 12_Lighting Coverage Matrix

> 重建日期：2026-07-13  
> 判定对象：`12_Lighting.md` 最终正文 764 行  
> 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/12_Lighting.md` 500 行  
> 行数口径：`File.ReadAllLines(path).Length`  
> 证据：`audit_12_result.md` 599 行、`review_11_12.md` 中 12 章 BODY review、列出的 UE5.7 源码锚点  
> 注意：历史 sidecar 结论全部作废；本表不修改章节状态或公共 Gate。

## 状态口径

- **已覆盖 / reviewer 回归**：正文教学闭环，且独立 Reviewer 对相应高风险事实做了源码回归。
- **已覆盖 / 锚点核对**：正文教学闭环，本轮确认 symbol/path；不把源码定位夸大为运行时观测。
- **边界覆盖**：只教授本章消费接口，Lumen、MegaLights、VSM、Atmosphere 内部算法不展开。
- 表内仅写文件名的 Renderer 锚点，默认位于 `Engine/Source/Runtime/Renderer/Private/`；shader 和 shared shader data 路径显式列出。

## Coverage Matrix

| 教学单元 | 事实风险 | 源码 symbol + path 锚点 | 成立条件 / 边界 | 状态 | 最终正文落点 | 残余风险 |
| --- | --- | --- | --- | --- | --- | --- |
| Deferred direct-lighting path gate | 把“deferred renderer”当成当前 view 必然执行 `RenderLights` | `bRenderDeferredLighting`；`Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp` | Lighting、SM5+、DeferredLighting、GBuffer、非 ray-traced overlay；forward/path tracing 是不同合同 | 已覆盖 / reviewer 回归 | 开篇，8-24 | 外层 gate 随 renderer 演进可能变化 |
| 输入账本与 direct/indirect/environment/atmosphere 分工 | 把所有 `SceneColor` 贡献统称 Lighting；把 direct 前后 indirect 接口误解为重复计算 | `RenderDiffuseIndirectAndAmbientOcclusion`、`RenderLights`、`RenderMegaLights`、`RenderDeferredReflectionsAndSkyLighting`；`DeferredShadingRenderer.cpp` | 本章只负责 opaque direct contribution；相邻系统只讲 producer/consumer 边界 | 已覆盖 / reviewer 回归 | 1、9，72-116、541-575 | 后续系统内部算法未在本章验证 |
| Gather：persistent scene light 到 frame-local set | 灯存在于 `FScene` 被误当当前 view 已接受；每个 consumer 独立筛选会漂移 | `FSceneRenderer::GatherAndSortLights`、`FSortedLightSetSceneInfo`；`LightRendering.cpp`、`LightSceneInfo.h` | view-independent 与至少一个 view 条件通过；sorted set 只对当前 frame 有效 | 已覆盖 / reviewer 回归 | 2，120-192 | 多 view 筛选条件版本敏感 |
| SortKey、连续 ranges 与唯一消费权 | 把 SortKey 当全部能力描述；把有 shadow/channel 写成必然 unbatched | `SimpleLightsEnd`、`ClusteredSupportedEnd`、`UnbatchedLightStart`、`MegaLightsLightStart`；`LightRendering.cpp` | unbatched 需 clustered-unsupported、非 MegaLights 且需要特殊输入；capability 不等于实际 consumer | 已覆盖 / reviewer 回归 | 2.1-2.5，130-192 | range 字段和支持位可能随版本改变 |
| 四层 light identity 与 view-local records | 混用 persistent id、sorted position、packed index、cell entry 会读到另一盏灯 | `PrepareForwardLightData`、`PackLightData`、`FLightViewData`；`LightGridInjection.cpp`、`Engine/Shaders/Shared/LightViewData.h` | 当前 view/frame；不同 consumer 不保证完全同构读取 | 已覆盖 / reviewer 回归 | 3.1-3.4，194-241 | buffer mode、stable index 和字段布局版本敏感 |
| Light Grid 的 3D cell、header/index 与 pixel lookup | 把 grid 当最终可见性；把每 cell 扫描全场景灯写成定律 | `ComputeLightGrid`、`FLightGridInjectionCS`、`ComputeLightGridCellCoordinate`、`OverlapsLight`；`LightGridInjection.cpp`、`Engine/Shaders/Private/LightGridInjection.usf`、`Engine/Shaders/Private/LightGridCommon.ush` | 64px/32 slices 是默认工程值；HZB、parent/two-level、fixed/linked、async 都是条件分支 | 已覆盖 / reviewer 回归 | 4.1-4.6，245-320 | 容量、索引位宽和模式是高变配置点 |
| Clustered deferred 取得消费权 | 把 clustered 理论约束与 UE5.7 实现条件混淆；clustered/standard 重复累加 | `ShouldUseClusteredDeferredShading`、`AddClusteredDeferredShadingPass`、`AreLightsInLightGrid`；`ClusteredDeferredShadingPass.cpp`、`LightRendering.cpp` | project/runtime 开关、SM6、平台 VSM support、grid 注入；support classification 与 runtime takeover 分开 | 已覆盖 / reviewer 回归 | 5.1-5.2、7.1，334-354、427-444 | eligibility 与 shader support 是版本敏感实现 |
| Simple-standard、standard、unbatched、MegaLights consumer | 把它们当性能等级；忽略特殊输入窗口和 tail ownership | `RenderLights`、`UnbatchedLightsPass`、`RenderMegaLights`；`LightRendering.cpp`、`MegaLights/MegaLights.cpp` | 每盏灯 direct contribution 只能由一个 consumer 生成；mask/function 资源在当前窗口有效 | 已覆盖 / 锚点核对 | 5.3-5.4、9.1，356-382、551-559 | `simple-standard` 是教学名称，源码组织可变化 |
| Shadow evidence 的资源身份与 binding | 把 atlas、VSM pages、VSM bits、RT mask、Contact 参数说成一张 shadow texture | `GetLightOcclusionType`；`Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`；`FShadowSceneRenderer::RenderVirtualShadowMapProjectionMaskBits`；`Engine/Source/Runtime/Renderer/Private/Shadows/ShadowSceneRenderer.cpp`；`GetLightAttenuationFromShadow`；`Engine/Shaders/Private/DeferredLightPixelShaders.usf` | 必须匹配 current light/view/consumer；Contact 可 shader-local，VSM 可走 packed projection bits | 已覆盖 / reviewer 回归 | 6.1-6.2，384-421 | VSM projection 和 packed id 条件版本敏感 |
| GPU coverage：clustered screen pass 与 standard light volume | 把 coverage geometry 当真实光形状；把 depth bounds/stencil/HiZ 写成无条件 | `RenderLight` 与 light-volume draw state；`LightRendering.cpp` | directional/fullscreen，point/rect sphere，spot cone；depth bounds 需平台+CVar；Substrate tile/stencil 条件化 | 已覆盖 / reviewer 回归 | 7.1-7.4，423-490 | raster state/permutation 会随平台变化 |
| Legacy GBuffer 与 Substrate 表面解释 | 把未知 `ShadingModelID` 返回零推广到 Substrate；误称 Lighting 重跑材质图 | `DeferredLightPixelMain`、`IntegrateBxDF`、`SubstrateDeferredLighting`；`Engine/Shaders/Private/DeferredLightPixelShaders.usf`、`Engine/Shaders/Private/DeferredLightingCommon.ush`、`Engine/Shaders/Private/ShadingModels.ush`、`Engine/Shaders/Private/Substrate/SubstrateDeferredLighting.ush` | legacy switch 与 Substrate closure 是不同合同；producer/consumer layout 必须一致 | 已覆盖 / reviewer 回归 | 8.1-8.3，492-539 | Substrate shader 入口与数据布局版本敏感 |
| SceneColor direct contribution 与相邻 consumer | 把 additive 语义写成 GPU 完成或最终画面完成；classic 与 MegaLights 双重消费 | `RenderLights`、`RenderMegaLights`、SceneColor pass ordering；`DeferredShadingRenderer.cpp`、`LightRendering.cpp` | `SceneColor_next = current + direct contribution` 只描述颜色责任；后续仍有 indirect/environment/atmosphere | 已覆盖 / reviewer 回归 | 9，541-575 | 后续 pass 目标与顺序可随 renderer 配置改变 |
| RDG/RHI/platform queue/GPU completion | 把 AddPass、RHI record、queue submit、GPU pass 与最后 consumer 合并 | `FRDGBuilder::Execute`、`FRDGBuilder::ExecutePass`；`Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp`；`FRHICommandListImmediate::ImmediateFlush`；`Engine/Source/Runtime/RHI/Private/RHICommandList.cpp` | `Execute`/`ExecutePass` 只锚定 RDG 图与 pass 向 RHI command list 的执行；`ImmediateFlush` 只锚定 RHI dispatch/flush，不能单独证明 backend platform queue submit 或 GPU completion；资源仍需活到 atmosphere/translucency/post 等最后 GPU consumer | 已覆盖 / reviewer 回归 | 10，577-611 | 未执行 backend queue submit、项目级 GPU fence/capture 验证 |
| Worked case 与 last-valid-state | 把能力分类、grid membership、consumer、BRDF 和颜色输出互相越级证明 | 上述 route/grid/consumer/shader symbols | 同一 spotlight A/B/C 配置；rect+IES+source texture 只作能力对照 | 已覆盖 / BODY review | 11-12，613-739 | 案例验证的是框架推理，不替代项目内容测试 |

## BODY Review 依据

- `review_11_12.md` 对当前 764 行正文给出 **BODY PASS**，旧 sidecar 不参与判定。
- Reviewer 回归了 deferred gate、range 扫描、unbatched 条件、rect source texture、clustered 条件、Light Grid 模式、`LightViewData`、light volume/depth/stencil、legacy/Substrate、indirect 顺序和 completion 深度。
- 本表不声称章节状态、公共 Gate 或公共索引已经更新。

## 总体残余风险

- clustered eligibility、MegaLights ownership、Substrate 入口与 Light Grid 模式都属于 UE5.7 高变实现点。
- Lumen、AO、reflection、sky、atmosphere、MegaLights 和 shadow producer 内部算法按章节边界未展开。
- 未执行运行时 capture；表中 completion 状态是语义核验，不是整帧 GPU 观测。
