# 19 Virtual Shadow Maps Coverage Matrix（按最终正文重建）

## 重建口径

- 重建日期：2026-07-13。
- 判定对象：`19_VirtualShadowMaps.md`，633 个物理行，SHA256 `24D77ECCBFD53F3F88ABF7FAFAA3F5435CE007196E8A77458243E77772D71F96`。
- 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/19_VirtualShadowMaps.md`，497 个物理行，SHA256 `DDB030CB43B412825BCE108F7B0193D1C4A590370E39F91DF069D179BEC07773`。
- 审计材料：`.codex/tmp/audit_18_19.md`（274 行）与 `.codex/tmp/review_18_19.md`（39 行），均完整读取成功。
- 行数口径：`.NET File.ReadAllLines(path, UTF8).Length`。旧 sidecar 结论、状态和正文落点全部作废；本表只记录最终正文与本轮源码复核。
- `verified` 是事实单元的当前源码核验状态，不是章节 Gate 或公共完成状态。
- 表内短文件名均按唯一目录展开：`VirtualShadowMapArray.*`、`VirtualShadowMapCacheManager.cpp`、`VirtualShadowMapProjection.cpp` 位于 `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/`；VSM `.usf/.ush` 位于 `Engine/Shaders/Private/VirtualShadowMaps/`；`ShadowSceneRenderer.cpp` 位于 `Engine/Source/Runtime/Renderer/Private/Shadows/`；`RenderUtils.cpp` 位于 `Engine/Source/Runtime/RenderCore/Private/`；`LightRendering.cpp` 位于 `Engine/Source/Runtime/Renderer/Private/`。

## 最终教学与事实单元

| ID | 最终教学/事实单元 | UE5.7 源码 path + symbol 锚点 | 成立条件与边界 | 最终正文落点 | verified | 残余风险 |
| --- | --- | --- | --- | --- | --- | --- |
| VSM-00 | 运行资格门先于所有页工作：VSM 需项目开关与平台/Nanite runtime 支持；non-Nanite caster 另有独立 gate | `Engine/Source/Runtime/RenderCore/Private/RenderUtils.cpp`：`UseVirtualShadowMaps`、`DoesPlatformSupportVirtualShadowMaps`、`UseNonNaniteVirtualShadowMaps` | `r.Shadow.Virtual.Enable` 与 `DoesRuntimeSupportNanite(..., atomics/project checks)` 成立；项目 CVar源码默认 0，配置可覆盖；non-Nanite 路径还需对应 read-only config | “先过运行资格门”；第 9.0 节 | verified | 项目设置和 shader platform 改变时需重新核对；persistent pool 是否已存在不等于本帧 gate 已开 |
| VSM-01 | virtual address、physical page pool、page table 三者解耦；16k 虚拟承诺不等于预先分配完整深度图 | `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.h`、`Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`：`FVirtualShadowMap` constants、uniform/page resources | level-0 128x128 pages、physical page 128x128 texels 等为当前实现常量；实际物理存储只服务请求页 | 第 1、2 节 | verified | 常量可随实现版本改变；教学结论是解耦而非固定数值本身 |
| VSM-02 | `FVirtualShadowMapArray` 组织本帧 RDG refs；cache manager 持 persistent pool、metadata 与 previous-frame evidence | `VirtualShadowMapArray.cpp`：external texture/buffer registration、`ExtractFrameData`；`VirtualShadowMapCacheManager.cpp`：pool/cache ownership | RDG handle 生命周期与 underlying external allocation 生命周期必须分开；禁用 persistent data或 resize会丢缓存 | 第 1 节“两个所有权不同的对象”；第 8 节 | verified | extraction 只表示保留给后续帧，不是 CPU readback/GPU idle |
| VSM-03 | 页表 entry 记录 physical address、`LODOffset`、本层可写与任意更粗层有效状态；完全无映射返回 invalid | `Engine/Shaders/Private/VirtualShadowMaps/VirtualShadowMapPageAccessCommon.ush`：`FShadowPhysicalPage`、`ShadowDecodePageTable`、`ShadowVirtualToPhysicalUV` | 只有 `bAnyLODValid=true` 时才可按 `LODOffset` 退到更粗页；完全缺页不能概括为“自动不黑屏” | 第 2 节；第 7 节 | verified | invalid 后的最终视觉还取决于 projection/filter/lighting consumer |
| VSM-04 | clipmap 是重叠 nested levels，不是物理环形存储；local light 选择 full 或 single-page，point light可有多 face | `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapClipmap.cpp`：clipmap levels；`VirtualShadowMapArray.cpp`：full/single-page allocation | directional 使用 world-space clipmap；local full/single-page 按投影/屏幕需求；“环”仅能描述覆盖直觉 | 第 3 节前两子节 | verified | clipmap level 默认和选择策略受配置影响 |
| VSM-05 | VSM ID 是当前 `FVirtualShadowMapArray` 的帧内地址索引，不是跨帧灯身份 | `Engine/Source/Runtime/Renderer/Private/Shadows/ShadowSceneRenderer.cpp`：`AllocateVirtualShadowMapIds`；`VirtualShadowMapCacheManager.cpp`：`UpdateVirtualShadowMapId` | directional、local、unreferenced cache entries 每帧重新组成当前集合；数值 ID 可变化 | 第 3 节“VSM id 是本帧地址索引” | verified | single-page 仍有独立 invalidation 条件，不能从 ID 模型推出其复用策略 |
| VSM-06 | 跨帧复用依赖 cache identity、previous/current ID 与 page-address remap，而不是保持数值 ID | `VirtualShadowMapCacheManager.cpp`：cache entries、`UpdateVirtualShadowMapId`、`NextVirtualShadowMapData`；`VirtualShadowMapPhysicalPageManagement.usf`：`UpdatePhysicalPageAddresses` | cache key/identity 仍匹配且 pool/metadata可保留；remap 把 previous mapping 转到 current ID/address | 第 3 节；第 8 节三种复用 | verified | cache key、projection data或 pool resize变化会使复用失效 |
| VSM-07 | Page marking 拥有 demand，不拥有物理页；screen、froxel、hair/water/front-layer、MegaLights 等可贡献 request | `VirtualShadowMapArray.cpp`：marking passes；`Engine/Source/Runtime/Renderer/Private/Shadows/ShadowSceneRenderer.cpp`：`BeginMarkVirtualShadowMapPages` | 当前帧 request 尚未建立不等于 persistent pool“一个物理页都没有”；receiver/detail/coarse demand粒度不同 | 第 4 节 | verified | 新 demand source 可能扩展列表，但不改变 demand/allocation ownership 分离 |
| VSM-08 | coarse pages 提供低频/远距/体积证据；directional clipmap coarse-level marking 与 local `MarkCoarsePagesLocal=2` 是两套适用域 | `VirtualShadowMapArray.cpp`：`r.Shadow.Virtual.MarkCoarsePagesLocal`、`r.Shadow.Virtual.NonNanite.IncludeInCoarsePages`；`ShadowSceneRenderer.cpp`：directional coarse levels/local projection flags；`VirtualShadowMapPhysicalPageManagement.usf`：`bForceCacheDynamicCoarse` | mode 2 只为 local VSM non-detail coarse 动态失效设置抑制；directional clipmap 由 first/last coarse levels 产生 demand，不使用该 local force-cache flag | 第 4 节；第 8 节；第 9.5 节 | verified | single-page 还有独立 skip-invalidation 条件；正文未把它归给 mode 2 |
| VSM-09 | Allocation 以 GPU 状态机把 request 推进为 mapping：列表复用、页表写入、mapped-mip propagation 和初始化集合各有职责 | `VirtualShadowMapArray.cpp`：`BuildPageAllocations`、allocation passes、`SelectPagesToInitialize`；`VirtualShadowMapPhysicalPageManagement.usf` | “需要初始化/重建的页”不等于“仅新分配页”；request 存在不证明 entry valid，entry valid 也不证明 depth已写 | 第 5 节 | verified | 具体列表名/算法可能演进，正文依赖的是状态转换和证据深度 |
| VSM-10 | Nanite 与 non-Nanite caster 走不同 submission/raster 路径，但写同一 physical pool；render 不拥有 demand | `VirtualShadowMapArray.cpp`：Nanite/non-Nanite render entry；`RenderUtils.cpp`：`UseNonNaniteVirtualShadowMaps` | non-Nanite 路径默认配置开启但仍需总 VSM gate；普通 mesh 缺影先查 gate，再查 culling/raster | 第 6 节 | verified | visible-instance overflow、HZB/culling 和平台支持仍可造成路径内缺影 |
| VSM-11 | Lighting 以虚拟坐标查页表、退层、翻译到 physical UV、采样 depth；physical depth ready 与 projection ready、lighting consumed 不同 | `VirtualShadowMapPageAccessCommon.ush`：`ShadowVirtualToPhysicalUV`；projection shaders；`LightRendering.cpp`：deferred consumer | coarse fallback 仅在更粗 valid mapping 存在时成立；完全 invalid 要回到 request/allocation | 第 7 节 | verified | filter/bias 只应在 mapping/depth/projection 证据都成立后调试 |
| VSM-12 | One-pass projection 聚合 local VSM，在 pruned light grid 上生成 packed mask bits；默认每像素16盏，硬 clamp 32 | `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`：`CVarVirtualShadowOnePassProjectionMaxLights`、`PackedShadowMaskMaxLightCount`；`VirtualShadowMapProjection.cpp`：one-pass projection；`LightRendering.cpp`：mask handoff | 仅 local VSM；满足条件时 deferred lighting可跳过传统逐灯 screen mask；directional 不走 local OPP；可视化/其他条件可使其禁用 | 第 7 节“one-pass projection”；第 9.4 节 | verified | 第17盏及以后缺影需结合 configured max 和 overflow；提高到32增加 transient VRAM |
| VSM-13 | Cache invalidation按 clipmap movement、local projection/key、primitive change、coarse policy 与 pool resize分层 | `VirtualShadowMapCacheManager.cpp`：cache entry/update/invalidation；`VirtualShadowMapPhysicalPageManagement.usf`：dynamic/coarse invalidation | detail 正确而 coarse stale 时查 coarse policy；整灯错位先查 ID remap/cache identity；不能把所有 moving geometry概括为“相关页必重画” | 第 8 节 | verified | local single-page 的独立失效条件仍是后续展开风险 |
| VSM-14 | Pool pressure 先通过 GPU feedback 提高 global resolution LOD bias 降需求；overflow 仍可直接产生 missing-shadow artifacts | `VirtualShadowMapArray.cpp`：`r.Shadow.Virtual.MaxPhysicalPages` 默认2048、`GlobalResolutionLodBias` uniform；`VirtualShadowMapCacheManager.cpp`：page-pool load feedback、dynamic resolution、overflow status | 默认 load threshold 0.85、最大 global bias 2.0；反馈跨帧生效，不保证挽救已经 overflow 的当前帧 | 第 8 节“Pool pressure”；第 9.2 节 | verified | 平台/项目可能覆盖默认值；增池以显存换覆盖，提高 bias 以细节换容量 |
| VSM-15 | 完成深度分为 CPU/RDG 组织、GPU request/table/depth/mask 写入、lighting消费、跨帧 extraction/import | `VirtualShadowMapArray.cpp`、`VirtualShadowMapProjection.cpp`、`VirtualShadowMapCacheManager.cpp` 对应 passes/extraction | 任一前层不能替代后层证据；QueueExtraction 不等于 GPU 空闲 | 第 5-8 节状态链；第 9 节调试 | verified | 多队列/RDG调度细节未在本章展开，但未被错误压成“已执行” |
| VSM-16 | last-valid-state 调试从 gate -> request -> mapping -> initialized/rasterized depth -> projection/OPP -> lighting -> cache/remap | 上述各稳定锚点 | bias/filter 仅用于 mapping/depth/projection 都有效后的采样问题；GPU capture、VSM visualization、CPU gate/log各覆盖不同证据 | 第 9 节；第 10 节主线回放 | verified | 单一最终缺影症状不能跳过中间 owner 直接归因 |

## Worked Case / Last-Valid-State 覆盖

| 关联单元 | 正文案例 | 状态推进与调试证据 |
| --- | --- | --- |
| VSM-00 | unsupported项目/平台 | gate=false时停止，不进入request/page table调试 |
| VSM-04 至 VSM-06 | 太阳、角色与塔灯跨帧 | directional clipmap/local single-page分流；塔灯cache identity不等于稳定ID，错位查remap |
| VSM-07 至 VSM-10 | 角色脚下detail、远处coarse、Nanite角色/non-Nanite地面 | request -> mapping -> init/raster；detail正常而coarse stale查对应coarse owner |
| VSM-11 至 VSM-12 | 更粗mapping与第17盏local light | 完全invalid回request/allocation；local packed-mask缺位查OPP max/overflow，directional不查OPP |
| VSM-13 至 VSM-14 | 当前帧正确但下一帧stale、pool接近/超过容量 | extraction/remap/invalidation与pressure feedback分开；overflow可直接缺影 |

## 残余风险汇总

1. `MarkCoarsePagesLocal=2` 只适用于 local VSM non-detail coarse 动态失效抑制；directional clipmap coarse demand与single-page特殊失效条件必须继续分开。
2. OPP 是 local-light projection 优化，不覆盖 directional；configured max、硬上限和 overflow 都可能改变最终阴影覆盖。
3. Dynamic resolution 是接近容量时的预防性调节，overflow 仍可能产生缺影，且反馈不能保证同帧修复。
4. VSM ID 只在当前 array/frame 内有效；任何跨帧记录必须以 cache identity 和 remap 解释。
