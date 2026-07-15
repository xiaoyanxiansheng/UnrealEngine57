# 13_Atmosphere Coverage Matrix

> 重建日期：2026-07-13  
> 判定对象：`13_Atmosphere.md` 最终正文 423 行  
> 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/13_Atmosphere.md` 341 行  
> 行数口径：`File.ReadAllLines(path).Length`  
> 证据：最终正文、冻结原版、`review_13_14.md` 的继承性 BODY 记录、本轮定位的 UE5.7 源码锚点  
> 注意：`review_13_14.md` 明说本轮未重新验收 13；本表不会把 symbol 定位伪装成一次新的完整事实终审，也不修改章节状态或公共 Gate。

## 状态口径

- **已覆盖 / 锚点核对**：最终正文完成该教学单元，本轮确认最小 symbol/path 与责任边界。
- **边界覆盖**：只讲一帧接入与资源语义，不展开 Lumen/VSM/MegaLights/云材质等专题算法。
- **BODY 记录（继承）**：`review_13_14.md` 沿用此前 BODY PASS，但该文件没有重新阅读或复审 13 正文。
- 表内仅写文件名的 Renderer 锚点，默认位于 `Engine/Source/Runtime/Renderer/Private/`；shader 路径显式列出。

## Coverage Matrix

| 教学单元 | 事实风险 | 源码 symbol + path 锚点 | 成立条件 / 边界 | 状态 | 最终正文落点 | 残余风险 |
| --- | --- | --- | --- | --- | --- | --- |
| 五种数据形态与 producer/consumer 地图 | 把天空、雾、局部雾、云按“透明度”排队；把中间资源和直接写色混成一种输出 | `RenderSkyAtmosphereLookUpTables`、`ComputeVolumetricFog`、`RenderFog`、`RenderLocalFogVolume`、`RenderVolumetricCloud`；`SkyAtmosphereRendering.cpp`、`VolumetricFog.cpp`、`FogRendering.cpp`、`LocalFogVolumeRendering.cpp`、`VolumetricCloudRendering.cpp` | desktop deferred 主线；相邻系统仅作为输入或后续 consumer | 已覆盖 / 锚点核对 | 1，57-87 | 表格是接口地图，不代表所有配置都执行全部系统 |
| 早期资源准备与晚期颜色合成双窗口 | 把 LUT 准备误当天空已写入；把晚段 pass 顺序推广到 forward/overlay/特殊 view | `FSceneRenderer::RenderSkyAtmosphereLookUpTables`；`DeferredShadingRenderer.cpp`、`SkyAtmosphereRendering.cpp` | async/graphics 位置、desktop deferred、forward、water-behind、reflection capture 条件不同 | 已覆盖 / 锚点核对 | 2，89-143 | 具体 pass location 和异步策略是版本敏感实现 |
| Sky Atmosphere LUT 生命周期与可读交接 | 把“RDG 内生成/外部访问 commit/queue submit/GPU completion”都叫提交；把共享 LUT 写成每帧必算 | `CVarSkyAtmosphereStateVersioning`、`RenderSkyAtmosphereLookUpTables`、`FSkyAtmospherePendingRDGResources::CommitToSceneAndViewUniformBuffers`；`SkyAtmosphereRendering.cpp/.h` | versioning 开启时按初始化/输入版本重算；关闭时共享 LUT 每帧重算；per-view LUT 随 view | 已覆盖 / 锚点核对 | 3，145-180 | cache version 输入集合与 external-access 实现可演进 |
| Volumetric Fog froxel、局部散射与 Z 积分 | 把 `LightScattering` 当 Height Fog 最终接口；把低分辨率 3D grid 当最终颜色 | `FSceneRenderer::ComputeVolumetricFog`、`IntegratedLightScatteringTexture`、`LightScattering`；`VolumetricFog.cpp`、`FogRendering.h/.cpp`、`SceneRendering.h` | Volumetric Fog 条件、grid 参数、光照/阴影输入、history 可用性 | 已覆盖 / 锚点核对 | 4，182-228 | Lumen/MegaLights/VSM 等光照输入内部算法未在本章验证 |
| Height Fog 解析积分与体积雾组合 | 把 Height Fog 当低配 Volumetric Fog；重复计算同一近段；误用普通 alpha 语义 | `FDeferredShadingSceneRenderer::RenderFog`、`CalculateHeightFog`、`CombineVolumetricFog`；`FogRendering.cpp`、`Engine/Shaders/Private/HeightFogCommon.ush`、`Engine/Shaders/Private/HeightFogPixelShader.usf` | 指数高度密度可解析积分；Volumetric Fog permutation、start/max distance、pre-exposure 关系 | 已覆盖 / 锚点核对 | 5，230-271 | 数学近似和 shader permutation 细节可能变化 |
| Local Fog Volume 的区间协作与唯一覆盖责任 | 把 froxel/Height Fog/独立 pass 说成三选一；独立 pass 不执行被误判为漏画 | `InitLocalFogVolumesForViews`、`ShouldRenderLocalFogVolumeInVolumetricFog`、`ShouldRenderLocalFogVolumeDuringHeightFogPass`、`RenderLocalFogVolume`；`LocalFogVolumeRendering.cpp/.h`、`DeferredShadingRenderer.cpp`；`LFVRenderInVolumetricFog`、`View.VolumetricFogMaxDistance` 解析区间 clamp；`Engine/Shaders/Private/LocalFogVolumes/LocalFogVolumeCommon.ush` | froxel 最大距离、解析补段、underwater 强制组合、当前 fog pass 是否已承担 LFV | 已覆盖 / 锚点核对 | 6，273-304 | 不同平台和 CVar 会改变具体覆盖分工 |
| Volumetric Cloud VRT 与 per-pixel 路径 | 把 cloud alpha 当 coverage、cloud depth 当 SceneDepth；trace 有内容被误当已进入 SceneColor | `InitVolumetricCloudsForViews`、`RenderVolumetricCloud`、`RenderVolumetricCloudsInternal`、`ReconstructVolumetricRenderTarget`、`ComposeVolumetricRenderTargetOverScene`；`VolumetricCloudRendering.cpp`、`VolumetricRenderTarget.cpp/.h` | view state、VRT mode、平台、reflection capture、water/underwater、fog/AP 开关 | 已覆盖 / 锚点核对 | 7，306-347 | VRT 模式、重建和 history 是高变实现点 |
| 单射线 worked case 与 SceneColor 合成顺序 | 把 visual order 当资源依赖；忽略 LFV 段重叠、VRT compose 或透过率顺序 | 上述 LUT/fog/LFV/cloud symbols；SceneColor 顺序见 `DeferredShadingRenderer.cpp` | desktop deferred 默认路径；special target/view 按第 2 节分支表解释 | 已覆盖 / BODY 记录（继承） | 8-9，349-380 | worked case 是责任模型，不替代所有平台时序图 |
| Last-valid-state 与完成深度 | 把资源 identity、可读 binding、GPU producer、SceneColor composite、queue submit 合并 | `FRDGExternalAccessQueue::Submit` 调用点与定义、各 producer/consumer symbols；`DeferredShadingRenderer.cpp`、`Engine/Source/Runtime/RenderCore/Private/RenderGraphUtils.cpp` | external-access queue 的 `Submit` 只把资源交给 RDG external-access mode，不是 platform queue submit 或 GPU completion；资源复用仍需覆盖最后 GPU consumer | 已覆盖 / 锚点核对 | 9，390-415 | 未执行运行时 capture；没有新增 GPU 完成观测 |
| 与 Lighting、Water、Translucency、PostProcessing 的边界 | 在 13 章展开相邻专题算法，或把主 SceneColor 与 water-behind/capture target 混同 | `RenderLightShaftSkyFogAndCloud` 调度区、`SingleLayerWaterRendering.cpp`、`ReflectionEnvironmentRealTimeCapture.cpp` | water-behind 写 `SceneWithoutWater`；reflection capture 与 forward 改变 consumer/target | 边界覆盖 / 锚点核对 | 边界表、2、10 | 特殊 view 组合是版本敏感路径 |

## BODY Review 依据

- `review_13_14.md` 记录 `13_Atmosphere.md` 为 **BODY PASS**，但明确说明该轮未重新读取或复审 13，只沿用上一轮结论。
- 本轮重建 sidecar 时完整对照了当前 423 行正文与 341 行冻结原版，并定位了表中最小源码锚点；这不等价于虚构一份不存在的独立 Reviewer 全量终审。
- 本表不声明章节状态、公共 Gate、`OUTLINE.md` 或 `SOURCE_INDEX.md` 已更新。

## 总体残余风险

- Sky Atmosphere state versioning/external access、LFV 路由、VRT mode 与特殊 view target 都是版本敏感实现。
- `review_13_14.md` 对 13 的证据是继承性记录；若需要新的正式终审，应另做完整 BODY review。
- 未运行 GPU capture；完成深度仅记录源码和正文建立的证据边界。
