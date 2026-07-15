# 18 MegaLights Coverage Matrix（按最终正文重建）

## 重建口径

- 重建日期：2026-07-13。
- 判定对象：`18_MegaLights.md`，669 个物理行，SHA256 `E10A2D493DEBC8411DFAC232C67068A73379C2E6FF161029367B205666E92A3D`。
- 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/18_MegaLights.md`，602 个物理行，SHA256 `245553204930859321C35E5FD07E94747174FD296A3502E22130BA26BADB2494`。
- 审计材料：`.codex/tmp/audit_18_19.md`（274 行）与 `.codex/tmp/review_18_19.md`（39 行），均完整读取成功。
- 行数口径：`.NET File.ReadAllLines(path, UTF8).Length`。旧 CoverageMatrix 与 TeachingEditReport 的结论、状态和落点全部作废；本表从最终正文、冻结原版、指定审计/复核与 UE5.7 源码重新建立。
- `verified` 表示本轮重新打开稳定 `path + symbol` 锚点后，正文的主张、条件与完成深度一致；它不是章节 Gate 或公共状态。
- 表内短文件名沿用本章唯一目录映射：`MegaLights.cpp`、`MegaLightsRayTracing.cpp`、`MegaLightsResolve.cpp`、`MegaLightsDenoising.cpp` 均位于 `Engine/Source/Runtime/Renderer/Private/MegaLights/`；MegaLights `.usf/.ush` 均位于 `Engine/Shaders/Private/MegaLights/`；`LightRendering.cpp` 位于 `Engine/Source/Runtime/Renderer/Private/`；`VirtualShadowMapArray.cpp` 位于 `Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/`。

## 最终教学与事实单元

| ID | 最终教学/事实单元 | UE5.7 源码 path + symbol 锚点 | 成立条件与边界 | 最终正文落点 | verified | 残余风险 |
| --- | --- | --- | --- | --- | --- | --- |
| ML-00 | “固定预算”只约束 sample-domain 每位置槽位及其驱动的昂贵后端；candidate 枚举、active tiles、分辨率、history 与调度并非常数 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp`：`FMegaLightsViewContext`、sample texture/tile allocation；`Engine/Shaders/Private/MegaLights/MegaLightsSampling.usf`：`GenerateLightSamplesCS` | 讨论 GBuffer opaque 主线；slot 数和 downsample 配置可变，不能推出整帧成本与灯数完全脱钩 | 开篇；第 2 节；第 8.2 节；第 9 节 | verified | CVar、平台和场景密度会改变实际成本，正文未给性能承诺 |
| ML-01 | sorted-light ownership：`MegaLightsLightStart` 之前由 classic deferred 等 owner 消费，尾段由 MegaLights 消费，避免同灯重复累加 | `Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`：`GatherAndSortLights` 中 `bHandledByMegaLights`、`MegaLightsLightStart`；`RenderLights` range | 这是本帧分类，不是 Light Component 的永久身份；simple/directional 还受各自路由条件约束 | 第 1 节；第 7 节；第 8.1 节 | verified | 新增 light category 或排序规则时需重核尾段边界 |
| ML-02 | 五层资格门：编译/平台、项目与设备策略、view/family request 与 tracing data、per-light routing、owner 输出 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp`：`ShouldCompileShaders`、`IsRequested`、`HasRequiredTracingData`、`IsEnabled`、`GetMegaLightsMode`；`Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`：`bHandledByMegaLights` | 非 Mobile、SM6/Wave Ops/RT 能力与对应 permutation；`Allowed` 可 veto；PPV 可覆盖项目默认；HWRT 或显式允许且可用的 SWRT representation；directional 独立控制且默认关闭 | 第 1 节“五层资格门”；第 8.1 节 | verified | `IsRequested` 读取 `ViewFamily.Views[0]`；多 view/eye 的 family 语义仍是非阻断风险 |
| ML-03 | `DirectLighting` 是 owner 发布后的 sample 工作门，不参与先前 tracing/单灯 ownership 判定 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp`：`GenerateMegaLightsSamples` 入口；`Engine/Source/Runtime/Renderer/Private/LightRendering.cpp`：deferred lighting 调用点 | 灯已进入尾段后，sample pass 缺失应查 `DirectLighting`、deferred-lighting 调用和调度；不能倒推 tracing gate 失败 | 第 1 节第 141-143 行附近；第 8.1 节；last-valid-state 表 | verified | 调用点还受 deferred renderer 当前路径和尾段非空影响 |
| ML-04 | `FMegaLightsViewContext` 是一帧一 view 的 RDG 工作台；`FMegaLightsViewState` 才持有跨帧 lighting/hash history | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp`：`FMegaLightsViewContext`；`Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.h`：`FMegaLightsViewState` | RDG refs 不能跨 graph 保存；QueueExtraction 只建立后续帧保留，不是 CPU readback 或 GPU idle | 第 2 节；第 6 节 completion 表 | verified | history validity 仍受 camera cut、view state 与重投影条件控制 |
| ML-05 | candidate domain 与 sample domain 分离：light-grid cell 可有 15 个候选，但默认 2x2 下采样位置只保留 4 个 slots | `Engine/Shaders/Private/MegaLights/MegaLightsSampling.usf`：candidate loop、`NUM_SAMPLES_1D`；`Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp`：downsample/sample CVar | 15->4 是概率抽样，不是 top-4；提高 slots/关闭 downsample 以质量换 trace、shade、带宽与过滤成本 | 第 2 节；第 3 节 worked case | verified | 默认值可能随平台/配置改变，正文已把数值限定为当前默认示例 |
| ML-06 | Reservoir/sampler 是 shader invocation 内的临时账本；`WeightSum` 是候选累计量，最终 estimator weight 基线为 `WeightSum / CandidateWeight`，area guiding 可继续修正 | `Engine/Shaders/Private/MegaLights/MegaLightsSampling.ush`：`FLightSampler`、`AddLightSample`、finalize path；`MegaLightsSampling.usf`：sample finalize | 不能把 `LightSample.Weight` 称为 reservoir 累计权重；packed samples 和随机阈值不是最终跨 pass reservoir object | 第 3 节 worked case；“加权蓄水池”与 weight 段 | verified | 具体 guiding 公式可能继续演进，矩阵只承诺当前基线和边界 |
| ML-07 | visible-light guiding history 与 lighting/denoise history 是两个 owner 和两个用途 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsResolve.cpp`：visible-light hash；`Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsDenoising.cpp`：temporal history；`FMegaLightsViewState` | 前者改变下一帧采样分布，不是当前帧遮挡真值；后者重建画面；history 无效时必须能从当前帧重建 | 第 3 节“历史引导”；第 6 节；第 8.5 节 | verified | 高速运动和薄几何仍可能触发 rejection/ghost/noise 权衡 |
| ML-08 | VSM handshake 分成 sample-driven page marking 与 ShadowDepth 后 sample trace；VSM 拥有页表/池/cache，MegaLights 拥有 selected sample 状态 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsRayTracing.cpp`：`VirtualShadowMapMarkLightSamples`、`VirtualShadowMapTraceLightSamples`；`Engine/Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapArray.cpp`：page allocation/raster resources | 只服务选择 VSM shadow method 的 samples；mark 必须早于对应 VSM page allocation/raster，trace 才能消费已兑现页 | 第 4 节“VSM 模式”；第 7 节 | verified | 具体页管理细节属于第 19 章，本章只保留握手边界 |
| ML-09 | Shadow evidence 按 completed 状态与 compact list 有序推进：VSM -> 可选 screen -> world HWRT/SWRT -> 可选 material retrace | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsRayTracing.cpp`：`RayTraceLightSamples`、`VirtualShadowMapTraceLightSamples`、`ScreenSpaceRayTraceLightSamples`、world trace/retrace 调度 | screen miss 不等于可见；仍未完成时更新 `RayDistance` 并交给 world trace；SWRT 是条件性回退，不与 HWRT 无条件等价 | 第 4 节“有序状态机”；第 8.4 节 | verified | representation 覆盖、alpha/material retrace 与平台 RT 能力仍决定结果质量 |
| ML-10 | Resolve 合并同 `LocalLightIndex` 样本并输出 lighting/confidence/visible hash，随后 temporal/spatial reconstruction | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsResolve.cpp`：resolve passes；`Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsDenoising.cpp`：temporal/spatial filters | sample visibility 正常不保证 resolve 非零；resolve 正常也不保证 history 接受或 final target 写入 | 第 5 节；第 6 节；第 8.5 节 | verified | filter 参数改变质量/稳定性，不宜把单一 artifact 归因于算法本身 |
| ML-11 | Opaque 最终由 spatial denoise additive 写 `SceneColor`；RDG declared、GPU wrote、SceneColor UAV consumed、history extracted/imported 是不同完成深度 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsDenoising.cpp`：spatial output/UAV；`MegaLights.cpp`：output target、QueueExtraction | `RenderMegaLights` 被调用不等于 GPU 已写画面；QueueExtraction 不等于 CPU 同步读回 | 第 6 节 completion 四层表；第 8.5 节 | verified | capture 只能证明对应捕获深度，不能替代跨帧 import 检查 |
| ML-12 | Classic deferred、VSM、Lumen 的责任边界：直接光路由、阴影页证据、间接光/反射查询分别由不同系统拥有 | `LightRendering.cpp`：classic range；`VirtualShadowMapArray.cpp`：VSM resources；Lumen 仅作边界事实，不在本章展开 | 共享 light data、scene representation 或 RDG 不代表共享最终 lighting ownership | 第 7 节前三个子节 | verified | 后续系统集成变化需重新核对 handoff，但当前正文未越界展开 |
| ML-13 | Volume 只共享有限槽位思想：3D froxel/voxel domain、专用 sample/resolve/hash/filter 与 world-space tracing；不走 opaque 2D VSM/screen 主线 | `Engine/Shaders/Private/MegaLights/MegaLightsVolumeSampling.usf`；`MegaLightsVolumeRayTracing.usf`；`Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLightsRayTracing.cpp`：Volume branches | 依赖可用 HWRT，或显式允许且 representation 可用的 SWRT；Translucency Volume software world path 仍有未实现边界 | 第 7 节 Volume/Hair 支持矩阵与局部案例 | verified | 未实现组合必须视为功能限制，不能承诺自动 fallback |
| ML-14 | Hair 使用独立 context、sample/output/history；不使用 opaque spatial filter，Hair VSM page marking 尚未实现 | `Engine/Source/Runtime/Renderer/Private/MegaLights/MegaLights.cpp`：Hair context/output、`SupportsSpatialFilter`、Hair VSM warning；Hair CVar branches | Hair+VSM 不能描述为完整组合；最终 target 是 `SampleLightingTexture`，不是 opaque `SceneColor` 合约 | 第 7 节 Volume/Hair 支持矩阵与局部案例 | verified | Hair screen/world evidence 仍受专用开关和 hair data availability 控制 |
| ML-15 | last-valid-state 调试链按 ownership -> budget -> candidate/sample -> evidence -> resolve/history 停在最近 owner 边界 | 以上锚点的状态输出；`MegaLights.cpp`、`MegaLightsRayTracing.cpp`、resolve/denoise files | pass 名存在只证明调度层；必须结合 sample/ray/compact/output/history 数据确认推进深度 | 第 8 节全节；第 9 节主线回放 | verified | GPU capture 与 CPU gate/log 需组合使用，单一工具不能覆盖所有层 |

## Worked Case / Last-Valid-State 覆盖

| 关联单元 | 正文案例 | 状态推进与调试证据 |
| --- | --- | --- |
| ML-01 至 ML-03 | 工厂灯在分界前、尾段中但无 sample、已有 sample 三种状态 | classic/other owner -> MegaLights owner已发布 -> sample work；每步只查下一owner |
| ML-05 至 ML-07 | 15个candidates压入4个slots | candidate存在 -> sampler selection -> finalize/guiding -> lighting history；不是top-4 |
| ML-08 至 ML-09 | VSM/screen/world shadow evidence | page request/trace completion、`RayDistance`与compact list共同确定下一backend |
| ML-10 至 ML-11 | sample正常但resolve/denoise/target/history之一失败 | sample -> resolve -> temporal/spatial -> `SceneColor` -> extraction/import分层 |
| ML-13 至 ML-14 | Volume不进入2D list、Hair无VSM marking | 前者转volume world trace；后者停在明确未实现组合，不误诊整个VSM |

## 残余风险汇总

1. `IsRequested` 当前以 `ViewFamily.Views[0]` 读取 family request；正文用“当前 view”教学概括，若未来专讲 stereo/multiview 需补 first-view/family assumption。
2. Hair VSM page marking、Hair opaque-style spatial filter和部分 Volume software path仍不是完整支持组合；正文已显式限制，不能在后续 sidecar 中改写为已支持。
3. “固定预算”只表示 sample-domain/slot 驱动后端的上界模型，不是总 GPU 时间保证。
4. 源码锚点用于事实定位；最终教学仍由 ownership、数据域、状态机和 worked case 承载。
