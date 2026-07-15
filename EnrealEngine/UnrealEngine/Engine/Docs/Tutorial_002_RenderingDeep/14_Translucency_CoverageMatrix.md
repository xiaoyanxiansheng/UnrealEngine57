# 14_Translucency Coverage Matrix

> 重建日期：2026-07-13  
> 判定对象：`14_Translucency.md` 最终正文 591 行  
> 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/14_Translucency.md` 400 行  
> 行数口径：`File.ReadAllLines(path).Length`  
> 证据：最终正文、冻结原版、`review_13_14.md` 的 BODY review、列出的 UE5.7 源码锚点  
> 注意：`review_13_14.md` 本轮只重新检查 14-N1 及邻近正文；其他行不冒充该轮重新源码终审，不修改章节状态或公共 Gate。

## 状态口径

- **已覆盖 / BODY 记录**：最终正文完成该教学单元，整章 BODY 记录为 PASS；当前 review 的复查范围按残余风险说明。
- **已覆盖 / reviewer 回归**：14-N1 stereo TLV 由 `review_13_14.md` 直接核对源码。
- **已覆盖 / 锚点核对**：最终正文完成教学，本轮确认最小 symbol/path；不等价于运行时 GPU 验证。
- **边界覆盖**：Lumen/RT/Post/OIT 内部算法只讲当前接入合同。
- 表内仅写文件名的 Renderer 锚点，默认位于 `Engine/Source/Runtime/Renderer/Private/`；`Internal`、shader 和其他模块路径显式列出。

## Coverage Matrix

| 教学单元 | 事实风险 | 源码 symbol + path 锚点 | 成立条件 / 边界 | 状态 | 最终正文落点 | 残余风险 |
| --- | --- | --- | --- | --- | --- | --- |
| 透明输入合约与 `SceneColorCopy` consumer gate | 把透明当 BasePass 延长；把同 attachment 读写说成物理绝对不可能；copy 被误写成始终存在 | `FCopySceneColorPS`、`FTranslucentPrimCount::UseSceneColorCopy`；`TranslucentRendering.cpp`、`SceneRendering.h`、`SceneVisibility.cpp` | 普通透明 relevance、支持条件下的 Single Layer Water、underwater skip、平台/RHI 能力 | 已覆盖 / 锚点核对 | 1，76-110 | consumer gate 与平台特化路径版本敏感 |
| 材质意图到 Renderer 最终 schedule | 把材质 flag 等同最终 pass；把 `TranslucencyAll` 当普通材质类别 | `ETranslucencyPass`、`EMeshPass::Translucency*`、`AllowTranslucencyAfterDOF`、`AllowStandardTranslucencySeparated`；`BasePassRendering.cpp`、`SceneVisibility.cpp`、`SceneRendering.cpp` | staged translucency、Auto Before DOF、distortion、underwater、view/platform 条件共同解析 | 已覆盖 / BODY 记录 | 2，112-165 | Auto Before DOF 和特殊 view 路由是高变条件 |
| `FTranslucencyPassResourcesMap` producer/consumer 交接 | 把资源表当全局缓存；CPU 条目存在被误当 GPU 纹理已写或颜色已进 SceneColor | `FTranslucencyPassResourcesMap`；`Engine/Source/Runtime/Renderer/Internal/TranslucentPassResource.h`、`TranslucentRendering.cpp` | current frame/view/pass；color/modulate/depth 按条目能力；RDG lifetime 到最后 consumer | 已覆盖 / 锚点核对 | 3，167-204 | 资源表结构和 consumer 节点可随版本改变 |
| Separate translucency 三种原因与分辨率/depth 合同 | 只写 distortion/post 二选一；忽略 downsampled Standard 的 immediate composite | `UpdateSeparateTranslucencyDimensions`、`CreatePostDOFTranslucentTexture`、`UpscaleTranslucencyIfNeeded`；`TranslucentRendering.cpp`、`DeferredShadingRenderer.cpp` | AfterDOF/AfterMotionBlur、distortion-separated Standard、scale<1；匹配尺度 depth 和最后 consumer 不同 | 已覆盖 / 锚点核对 | 4，206-251 | dynamic resolution/upscale 策略版本敏感 |
| Per-object / per-pass 排序域 | 把所有透明放进同一距离数组；把 priority 当物理深度；把 visible sort 当 queue submit | `FMeshDrawCommandSortKey::Translucent` 与 translucency pass mapping；`MeshDrawCommands.cpp/.h`、`SceneVisibility.cpp` | 每 view、每 pass；priority/distance offset/sort policy；AfterDOF 与 Standard 不同域 | 已覆盖 / 锚点核对 | 5，253-285 | 具体 sort-key 位布局与 OIT 记录顺序可变 |
| OIT sorted triangles 与 sorted pixels | 把两者说成同一层；把 OIT 当无限层精确保证 | `OIT::IsSortedTrianglesEnabled`、`OIT::CreateOITData`、`OIT::AddOITComposePass`；`PrimitiveSceneInfo.cpp`、`OIT/OIT.cpp/.h`、`TranslucentRendering.cpp` | mesh/VF 支持、ROV、非不兼容 MSAA、pass mask、有限 sample storage/budget | 已覆盖 / 锚点核对 | 6，287-332 | sample budget、platform support 与 compose 实现高变 |
| Shading Model 与 Translucency Lighting Mode 双轴 | 把所有透明都说成采 TLV；把 lighting mode 当成能把 Unlit 变 Lit | `ETranslucencyLightingMode`；`Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h`；material translucency lighting state、translucent base pass permutations；`Material.h`、`BasePassRendering.cpp`、`GetTranslucencyVolumeLighting`；`Engine/Shaders/Private/BasePassPixelShader.usf` | 先判 Lit/Unlit，再判 volumetric/surface volume/Surface ForwardShading；平台与 feature 条件 | 已覆盖 / 锚点核对 | 7，334-353 | 材质 shader permutation 与 Substrate 透明路径可演进 |
| TLV 两级 cascade、owner、history 与 stereo sharing | 把 TLV 当透明颜色；把左右眼写成独立整套资源；从共享资源推断 secondary 无 GPU 工作 | `FTranslucencyLightingVolumeTextures::Init`、`ViewsToTexturePairs`、`GetIndex`、`RenderTranslucencyLightingVolume`、`FilterTranslucencyLightingVolume`；`TranslucentLighting.cpp/.h` | volume texture support、TLV/MegaLights/filter 开关；secondary `PrimaryViewIndex` 映射；注入/过滤仍可能按 view 调度 | 已覆盖 / reviewer 回归 | 7，355-393，特别是 379 | 当前 review 仅直接重查此阻断与邻近段；pass 数仍依配置 |
| Distortion offset/apply/merge | 把透明 shader 直接采写当前 SceneColor 当通用路径；offset 有内容被误当玻璃已合成 | `FDeferredShadingSceneRenderer::RenderDistortion`、`DistortionTexture`；`DistortionRendering.cpp` | distorted translucent、stencil、current background、Standard separate/resource map 条件 | 已覆盖 / 锚点核对 | 8，395-423 | RHI 特化和 merge permutation 可变化 |
| FrontLayer narrow evidence | 把 FrontLayer 当第二张透明颜色或完整多层 GBuffer | `EMeshPass::LumenFrontLayerTranslucencyGBuffer`、`FLumenFrontLayerTranslucencyGBufferParameters`；`Lumen/LumenFrontLayerTranslucency.cpp/.h`、`SceneVisibility.cpp` | material/view eligibility；只保存最近透明 normal/depth，后续 Lumen/RT consumer 再产颜色 | 边界覆盖 / 锚点核对 | 9，425-449 | 更深层透明结构不在该缓冲中 |
| Lumen ray-traced translucency 的颜色接入点 | 把 Lumen RT 算出颜色当完成；忽略 distortion/Standard separated 对接入点的约束 | `FDeferredShadingSceneRenderer::RenderRayTracedTranslucency`、`RenderRayTracedTranslucencyView`；`Lumen/LumenReflections.cpp` | Lumen RT route；无 distortion 或 Standard 不分离时以 `FinalRadianceTexture` 更新 `SceneTextures.Color` 并重建 scene texture uniform；否则把 radiance/background visibility 写入 `ResourceMap[Standard]` 后交给 distortion | 边界覆盖 / 锚点核对 | 10，451-476 | Lumen trace/denoise 内部算法不在本章验证 |
| Legacy ray-traced translucency 的颜色接入点 | 把 legacy RT 与 Lumen 的 ResourceMap 分支混为一套终点 | `ShouldRenderRayTracingTranslucency`、`FDeferredShadingSceneRenderer::RenderRayTracingTranslucency`；`RayTracing/RayTracingTranslucency.cpp` | legacy/deprecated view route、RHI ray tracing compatibility 与当前 translucency pass 成立；结果经 `AddDrawTexturePass` 直接写传入的 `SceneColorTexture.Target` 并 resolve，不走 `ResourceMap[Standard]` | 边界覆盖 / 锚点核对 | 10，451-476 | legacy RT trace/denoise 内部算法不在本章验证 |
| Worked Case A：染色折射玻璃 | 一条 primitive 的 Standard/Distortion/FrontLayer 三份语义被误当重复绘制；玻璃固定采 TLV | schedule/resource/distortion/front-layer symbols 同上 | Standard + distortion + Surface ForwardShading + 可选 FrontLayer；排序只在 Standard 域 | 已覆盖 / BODY 记录 | 11，480-517 | 案例配置不是所有玻璃的默认路径 |
| Worked Case B：AfterDOF 烟雾 | separate target 有颜色被误当视觉交付完成；低分辨率收益无边缘代价 | `FTranslucencyPassResourcesMap`、`CreatePostDOFTranslucentTexture`、PostProcessing 的 AfterDOF resources；`TranslucentRendering.cpp`、`PostProcess/PostProcessing.cpp` | AfterDOF 允许、Auto Before DOF 未迁移、TLV mode、separate scale、post consumer | 已覆盖 / BODY 记录 | 12，519-553 | post composite/upscale 细节属于第 15 章 |
| Last-valid-state 与完成深度 | 把 command formed、visible、RHI recorded、queue submit、GPU producer、ResourceMap、composite、last consumer 合并 | RDG/RHI 与上述 producer/consumer symbols | 每一状态只证明自身深度；资源复用需覆盖 post/distortion 等最后 GPU consumer | 已覆盖 / BODY 记录 | 3.1、13-14，188-204、555-591 | 未执行运行时 capture；最终画面不能反推中间身份必然正确 |

## BODY Review 依据

- `review_13_14.md` 记录当前 591 行 `14_Translucency.md` 为 **BODY PASS**。
- 该轮实际重新核验范围是唯一阻断 14-N1：正文 379 行及邻近 361-393 行。Reviewer 直接核对 texture-pair sharing、pair-level clear、per-view injection/filter scheduling，并确认修订后无邻近回归。
- 其余教学单元沿用既有整章 BODY 结论；本轮 sidecar 通过最终正文、冻结原版和源码锚点重建，但不伪称 `review_13_14.md` 对每一行都重新做了源码回归。
- 本表不声明章节状态、公共 Gate 或公共索引已更新。

## 总体残余风险

- staged pass routing、OIT、dynamic separate resolution、Lumen/RT 接入和 post consumer 都是版本敏感实现。
- Stereo TLV 的“按 view 调度”不保证所有 view/配置产生相同 GPU pass 数；正文已使用条件语气。
- 未执行运行时 GPU capture；本表不把源码锚点当作像素输出证据。
