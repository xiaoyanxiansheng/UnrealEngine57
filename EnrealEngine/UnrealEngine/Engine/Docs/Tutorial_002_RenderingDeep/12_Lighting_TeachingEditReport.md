# Teaching Edit Report - 12_Lighting.md

> 重建依据：最终正文 764 行、冻结原版 500 行、`audit_12_result.md`、`review_11_12.md` 与必要 UE5.7 源码。  
> 本报告只记录最终正文已发生的教学编辑；不修改章节状态，不声称公共 Gate 或公共文件已更新。

## 1. 最终主线

最终正文从真正的 deferred path gate 起步，而不是默认 `RenderLights` 必然执行：

```text
deferred gate -> surface/light/shadow/SceneColor inputs
-> gather/sort ownership ranges -> view-local light records
-> cell-to-light Light Grid -> one direct-light consumer
-> current shadow/function evidence -> GPU coverage
-> legacy or Substrate surface interpretation
-> direct contribution into current SceneColor
-> later indirect/environment/atmosphere consumers
-> exact completion depth
```

这条主线把 light-data infrastructure、clustered、standard、MegaLights 和后续颜色系统从“一个 Lighting pass”中拆开。

## 2. O-D-C-L 与过程教学

- **Owner**：persistent `FScene` light、frame-local sorted set、view-local packed records、cell index list、direct-light consumer 和 SceneColor resource chain 各自独立。
- **Data**：表面记录、sort bits/ranges、packed light payload、grid header/index、shadow evidence、coverage state、BRDF/closure output 分层传递。
- **Control**：deferred gate、range ownership、clustered runtime takeover、standard fallback、unbatched special-input window 和 MegaLights tail 明确转移消费权。
- **Lifetime**：持久 scene light 跨帧；sorted/view/grid 数据服务当前 frame/view；shadow mask 与 SceneColor 版本持续到各自最后 GPU consumer。

正文用“capability classification 不等于本帧 consumer”纠正了最常见的路由误解。

## 3. 设计理由与替代方案

最终正文实际解释了：

- 集中 gather/sort 可避免各 consumer 重复筛选和 ownership 漂移；替代是 bucket、stable partition、GPU 分类。
- 一份 packed light record 被多个 cell index 引用，避免每 cell 复制完整灯数据；替代是 per-tile AoS、bindless per-light object、per-draw uniform。
- 3D Light Grid 用 Z slice 减少纯 2D tile 的深度假阳性；替代是 tiled、light volume 或 GPU BVH。
- clustered 减少大量 local-light draw/PSO/GBuffer 重读，standard/unbatched 保留复杂功能和特殊资源窗口；二者不是质量等级。
- deferred 存表面后多灯复用，forward 把材质与灯光同阶段求值；取舍落在带宽、计算、MSAA、灯数和材质能力。
- RDG 依赖与延迟提交换取并行和资源复用；逐 pass fence 更易观察但会损失吞吐。

同一盏灯只允许一个 direct producer、表面语义必须匹配、资源必须活到最后 consumer 是硬约束；具体 sort key、grid、pass 组织是 UE 工程选择。

## 4. Worked Case

最终正文将原版多个孤立灯案例融合为同一 spotlight 的三种配置：

- **A clustered**：packed record -> grid cell -> clustered ownership -> fullscreen/tile coverage -> direct SceneColor。
- **B standard fallback**：能力仍可 clustered，但 runtime/grid gate 不接管；standard cone volume 取得唯一消费权。
- **C unbatched**：clustered-unsupported 且需要独立 shadow/function 输入，先准备证据再 `RenderLight`。

Rect light + IES + source texture 只作局部事实对照：clustered 可使用 IES，但当前 shader 不采 source texture；source texture 不会自动触发 standard 补画。

## 5. Last-Valid-State

最终证据梯从 deferred gate、SceneTextures、sorted set、range、packed records、cell list、consumer、shadow evidence、coverage、surface decode、BRDF/closure、SceneColor，一直推进到 queue submit 和覆盖最后 consumer 的 GPU completion。

症状路由也按状态层级组织：整灯消失先查 gate/gather/owner；局部漏灯查 bounds/grid/coverage；rect 图案缺失查实际 consumer 能力；shadow 像另一盏灯查 identity/binding；GPU capture 正确仍不能反证 CPU ownership 设计必然正确。

## 6. 事实修正

相对冻结原版，最终正文实际修正了：

- 增加 `Lighting && SM5+ && DeferredLighting && GBuffer && !RayTracedOverlay` 的本章外层 gate。
- SortKey 只编码当前分类需要的特征，不包含全部 shader 能力。
- persistent scene light 与 frame-local sorted/view/grid identities 分开。
- rect source texture 不被 clustered shader采样，但不会因此自动 fallback 到 standard。
- clustered UE5.7 当前实现要求 project/runtime、SM6、平台 VSM support 和 grid injection；这不是理论硬约束。
- unbatched 不是“有 shadow/channel 就进入”，而是 clustered-unsupported、非 MegaLights 且需要特殊输入时才推进。
- Light Grid 可使用 parent/two-level、HZB、fixed/linked 等分支；不保证每 cell 扫描全场景灯。
- standard light volume 使用 raster depth 与条件化 depth bounds，不把 HZB 写成其固定依赖。
- unknown `ShadingModelID` 返回零只属于 legacy `IntegrateBxDF`；Substrate 使用独立合同。
- SceneColor additive 只是 direct contribution 的语义，不是 GPU 或最终画面完成。
- direct 前后的 indirect 接口承担不同 composite/overlap 目的，不是同一贡献重复计算。

## 7. 原版价值迁移

| 冻结原版价值 | 最终迁移 |
| --- | --- |
| Lighting 接手屏幕表面而非 mesh | 扩为 deferred gate + 四类输入和 SceneColor 版本链 |
| sorted light 连续区间 | 保留并补唯一消费权、capability/runtime 区分和四层 identity |
| Light Grid 3D cell、header/index、pixel lookup | 保留并补理由、替代、容量、HZB/two-level 条件 |
| clustered 接管 supported range | 保留并补全部当前实现门槛和 source-texture 纠正 |
| standard/unbatched 特殊输入窗口 | 重排为完整 consumer ownership 模型，并补 simple/MegaLights |
| light shape、depth/stencil 裁剪 | 保留为 coverage 单元，移除无证据 HiZ 强断言 |
| GBuffer 到 BRDF | 扩成 legacy 与 Substrate 两种表面解释合同 |
| 两盏灯案例 | 融合为同一 spotlight 的 A/B/C 配置，保留 rect 局部对照 |
| 相邻系统边界与排查线 | 扩为 SceneColor 责任链、worked case 和 last-valid-state |

## 8. 源码克制

正文仅用 `bRenderDeferredLighting`、`GatherAndSortLights`、`PackLightData`、`ShouldUseClusteredDeferredShading`、`RenderLight`、`IntegrateBxDF`、`FRDGBuilder::Execute` 等最小锚点定位责任。源码路径、行号和事实回归保留在 review/sidecar，正文由状态链和案例承担教学。

## 9. BODY Review 依据与残余风险

`review_11_12.md` 对当前 764 行正文给出 BODY PASS，并逐项列出高风险事实回归。残余风险集中在 clustered eligibility、MegaLights ownership、Substrate shader entry、Light Grid mode 和平台条件的版本变化；相邻专题内部算法有意不在本章展开。

本报告不改变正文、章节完成状态或公共文件。
