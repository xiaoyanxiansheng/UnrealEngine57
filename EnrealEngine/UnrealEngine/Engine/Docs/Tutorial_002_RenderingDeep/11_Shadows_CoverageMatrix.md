# 11_Shadows Coverage Matrix

> 重建日期：2026-07-13  
> 判定对象：`11_Shadows.md` 最终正文 1180 行  
> 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/11_Shadows.md` 549 行  
> 行数口径：`File.ReadAllLines(path).Length`  
> 证据：`audit_11_result.md` 329 行、`review_11_12.md` 中 11 章 BODY review、列出的 UE5.7 源码锚点  
> 注意：历史 sidecar 的覆盖等级和 PASS 结论全部作废；本表只描述当前正文，不修改章节状态或公共 Gate。

## 状态口径

- **已覆盖 / reviewer 回归**：最终正文完成教学任务，且 `review_11_12.md` 对对应高风险事实给出了源码回归。
- **已覆盖 / 锚点核对**：最终正文完成教学任务，本轮确认最小 symbol 与源码路径存在；不把“symbol 存在”夸大为 GPU 运行验证。
- **边界覆盖**：正文只教授本章需要的接口，算法内部有意留给后续专题。
- 表内仅写文件名的 Renderer 锚点，默认位于 `Engine/Source/Runtime/Renderer/Private/`；shader 锚点显式写出 `Engine/Shaders/Private/`。

## Coverage Matrix

| 教学单元 | 事实风险 | 源码 symbol + path 锚点 | 成立条件 / 边界 | 状态 | 最终正文落点 | 残余风险 |
| --- | --- | --- | --- | --- | --- | --- |
| Shadow request 与 occlusion route | 把 `Cast Shadows` 当成动态 shadow map 必然创建；把 route 选择当成遮挡已经产生 | `GetLightOcclusionType`、`FSceneRenderer::CreateDynamicShadows`；`Engine/Source/Runtime/Renderer/Private/ShadowSetup.cpp` | light/view 可见性、cast flags、mobility、平台、forward/deferred、VSM/MegaLights/RT 配置共同决定 | 已覆盖 / 锚点核对 | 1.1-1.3，103-154 | route 规则版本敏感；升级时需回归分类条件 |
| Light、projected shadow、caster 三层可见性 | 复用主视图 primitive visibility 会漏掉屏幕外 caster；projected object 存在不代表当前 view 消费 | `FVisibleLightInfo::AllProjectedShadows`、`FVisibleLightViewInfo::ProjectedShadowVisibilityMap`、`InitProjectedShadowVisibility`；`SceneRendering.h`、`ShadowSetup.cpp` | dependent view、stereo、relevance、历史 occlusion query；VSM 对传统 query 有条件绕过 | 已覆盖 / reviewer 回归 | 2.1-2.4，158-217 | historical query 与 VSM 特例是高变实现点 |
| `FProjectedShadowInfo` 帧内合约 | 把它说成所有阴影技术的统一对象；对象存在被误当作 allocation、draw 或 GPU depth 已成立 | `FProjectedShadowInfo`、`SetupWholeSceneProjection`、`SetupPerObjectProjection`、`SetupClipmapProjection`；`ShadowRendering.h`、`ShadowSetup.cpp` | 适用于 projected-shadow 家族及部分 VSM 接入；Contact、MegaLights sample、纯 RT 不强行纳入 | 已覆盖 / 锚点核对 | 3.1-3.4，219-273 | 大对象字段并非对每种 shadow type 都有效 |
| CSM request、split、transition 与稳定 bounds | 把 CSM 写成 Directional Light 的硬规则；把 cascade 空间成立当成 caster/depth 成立 | `GetEffectiveCascadeDistributionExponent`、`GetSplitDistance`、`GetShadowSplitBounds`；`Engine/Source/Runtime/Engine/Private/Components/DirectionalLightComponent.cpp` | 动态阴影距离、cascade 数、far cascade、预计算有效性、mobile/forward/deferred 条件 | 已覆盖 / reviewer 回归 | 4.1-4.5，277-336 | split/bounds 参数和平台限制需随版本回归 |
| Static/dynamic/precomputed 与 whole-scene/per-object/preshadow | 把 runtime cached static depth 等同 baked lighting；混淆 caster->receiver 方向 | `bCastStaticShadow`、`bCastDynamicShadow`、`HasStaticShadowing`、`bPreShadow`；`ShadowSetup.cpp`、`ShadowRendering.h` | mobility、precomputed validity、receiver/caster 关系、projected-shadow 类型 | 已覆盖 / 锚点核对 | 5.1-5.4，338-392 | 特殊移动性和预计算 fallback 仍依项目设置变化 |
| Regular atlas、cubemap、preshadow cache 与 border | allocation 被误写成内容已写入；过滤跨 atlas 边界会读到相邻 shadow | `FSortedShadowMaps`、`FProjectedShadowInfo::X/Y/BorderSize`；`ShadowRendering.h`、`ShadowDepthRendering.cpp` | 2D/cubemap/preshadow/translucency/VSM 资源类别不同；border 取决于 filter 与 packing | 已覆盖 / 锚点核对 | 6.1-6.5，394-445 | 具体 atlas packing 与 filter kernel 属实现细节 |
| Runtime cache mode 与跨帧 owner | 把整张阴影缓存成永久有效；混淆 scene cache 与 frame projected work owner | `EShadowDepthCacheMode`、`FCachedShadowMapData`；`ShadowRendering.h`、`ScenePrivate.h`、`ShadowDepthRendering.cpp` | initializer、light/primitive 变化、budget、last-used、static/movable/scrolling/uncached 模式 | 已覆盖 / reviewer 回归 | 7.1-7.3，447-496 | cache invalidation 与滚动策略是版本敏感实现 |
| Caster gather 到 ShadowDepth draw 的 producer 链 | 把 subject primitive、mesh draw command、instance draw、GPU depth 合并为一个“已画”状态 | `BeginGatherShadowPrimitives`、`SetupMeshDrawCommandsForShadowDepth`、`FinishDynamicShadowMeshPassSetup`、`RenderShadowDepthMaps`；`ShadowSetup.cpp`、`ShadowDepthRendering.cpp` | relevance、interaction、mesh/VF 支持、GPUScene data、instance culling、task prerequisite 必须逐层成立 | 已覆盖 / reviewer 回归 | 8.1-8.7，498-663 | Nanite 与兼容 selection mask 内部算法有意不展开 |
| RDG、RHI、平台命令、Queue Submit 与 GPU depth | 把 `bShadowDepthRenderCompleted`、RDG external access submit 或 queue submit 当成 GPU fence | `RenderShadowDepthMaps`、`CheckShadowDepthRenderCompleted`、`bShadowDepthRenderCompleted`；`ShadowDepthRendering.cpp`、`SceneRendering.h` | 每层证据只证明对应控制深度；GPU 内容需 capture/fence/timestamp 等观察 | 已覆盖 / reviewer 回归 | 0、9、13，82-101、664-727、1002-1055 | 文档没有运行 GPU capture；这里只记录验证过的语义边界 |
| Projection/filtering 到 Lighting shadow terms | 把 light-space depth 当最终 attenuation；screen mask 正确被误当 Lighting 绑定正确 | `RenderDeferredShadowProjections`、`FProjectedShadowInfo::RenderProjection`、`GetShadowTerms`；`ShadowRendering.cpp`、`LightRendering.cpp`、`Engine/Shaders/Private/DeferredLightingCommon.ush` | regular mask、VSM packed bits、precomputed/contact 组合依 current light/view/route；filter 参数按路径变化 | 已覆盖 / reviewer 回归 | 10.1-10.6，729-840 | packed-mask 省略 screen mask 的条件是高变实现点 |
| VSM request、mapping、physical pool/cache 与 projection | 把 request、mapping、physical content、projection 合并成 “VSM ready”；把 frame array 当底层 pool 唯一 owner | `FVirtualShadowMapArray::Initialize`、`BuildPageAllocations`、`FVirtualShadowMapArrayCacheManager`；`VirtualShadowMapArray.cpp/.h`、`VirtualShadowMapCacheManager.h` | deferred/VSM enabled；forward UE5.7 不支持 VSM；coarse/保守 marking 可 over-request | 已覆盖 / reviewer 回归；算法边界覆盖 | 11.1-11.6，842-925 | page-table 编码、clipmap、失效算法留给第 19 章 |
| MegaLights、Contact Shadow、RT shadow 分支 | 把 MegaLights 当每灯 shadow map；把 Contact 说成全局“无资源”；把 RT route 当 projected shadow 必然存在 | `GenerateMegaLightsSamples`、`RenderMegaLights`、`GetLightContactShadowParameters`、`ApplyContactShadowWithShadowTerms`、`GetLightOcclusionType`；`MegaLights/MegaLights.cpp`、`LightRendering.cpp`、`Engine/Shaders/Private/DeferredLightingCommon.ush` | light type、平台、route、VSM/RT method、inline/standalone contact、screen-space 可见信息 | 边界覆盖 / 锚点核对 | 12.1-12.4，927-1000 | sample/trace/denoise 与 RT 内部算法留给后续专题 |
| 贯穿案例与 last-valid-state | 最终像素正确被误当所有上游 identity/lifetime 都正确；资源过早退休 | 上述 producer/consumer symbols；资源依赖由 RDG/RHI/backend 共同承接 | 同一太阳、建筑、动态树、地面像素；证据从 route 一直推进到 last GPU consumer | 已覆盖 / BODY review | 14-15，1057-1161 | 证据梯是调试模型，不代替具体项目 capture |

## BODY Review 依据

- `review_11_12.md` 对当前 1180 行正文给出 **BODY PASS**，并明确旧 sidecar 不参与判定。
- Reviewer 核对了 forward/VSM 时序、VSM historical query、CSM exponent、cache mode、shadow-view GPUScene、`bShadowDepthRenderCompleted`、projection/VSM packed mask、VSM cache owner、Contact length 编码等高风险事实。
- 本表不把 BODY PASS 扩展成章节状态、公共 Gate 或公共索引已更新的声明。

## 总体残余风险

- VSM packed mask、cache invalidation、MegaLights/RT 分支和 forward/mobile 条件属于版本敏感实现。
- 正文有意停在第 11 章接口边界；第 18/19 章算法未在此重复验证。
- 未执行运行时 GPU capture；完成深度行记录的是源码与教学语义核验，不是项目场景观测。
