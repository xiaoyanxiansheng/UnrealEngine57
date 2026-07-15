# 07 MeshDrawCommand Teaching Edit Report

> 依据最终正文重新生成；不继承旧结论，不改变章节状态。

## 最终结构
- 核心问题：候选 mesh 为什么不能直接成为 RHI draw。
- 唯一主线：`FMeshBatch` → `FMeshDrawCommand` → Visible Command/本帧工作集 → RHI recording。
- 三本账：Pass 决策、Command 配方、本帧提交。
- 两种生命周期：scene-cached 与 one-frame dynamic；不等于 Component Mobility。
- 金属板案例贯穿配方、缓存、view 选择、排序、bucket、culling 和录制；procedural mesh 补足 dynamic finalize。
- 调试按“最后成立状态”从三本账走向 Queue Submit 与 GPU consumer。

## 事实与教学修正
- Batch 是候选合同，不承担 pass/shader/order/visibility 最终决定。
- Processor 拥有 pass 解释控制权，不拥有资源生命周期或最终可见性。
- Command 是稳定配方，不拥有引用资源，不等于 native PSO 或 draw call。
- `PrimitiveIdInfo` 连接 06 的 GPUScene 身份，使共享配方与独立 per-object 数据同时成立。
- Bucket 是模板共享；真正 compact 仍受排序、bindings、payload 和身份兼容约束。
- Instance Culling 消费可见命令，不重做材质/pass 决策。
- Renderer submit、RHI recording、Platform Queue Submit、GPU completion 严格分层。

## 信息价值审计
- Origin 与旧稿的原理、条件、例外、案例和调试判断均已映射到最终正文。
- 命令组成、缓存资格、Processor 决策、sort/bucket/instancing/culling 和 submit 含义已从源码走读转译为理论与状态变化。
- 删除/压缩的是函数列表、成员枚举、路径行号、验证记录和重复解释，不是技术含义。
- 当前约 583 行，Origin 区间约 486 行，未发生相对 Origin 缩减；专项审计未发现无落点教学单元。

## 案例、调试与源码克制
- 两块金属板证明共享模板与独立 GPUScene 身份可同时成立；三个反例覆盖参数绑定、透明顺序和本帧 payload。
- 调试覆盖 pass 缺失、缓存失效、bucket/draw 差异、dynamic 串位和 capture 无 draw。
- 性能分析同时观察 build、sort、bucket、culling、draw 与 instance 数。
- 正文不以源码路径、行号、验证日志或成员清单承担教学；符号均转译为责任、数据、条件和调试判断。

## 跨章与验收
- 与 06 一致：GPUScene 提供身份与数据入口；Command 不复制完整 per-object 数据。
- 与 04 一致：recording、Queue Submit、GPU completion 分层。
- 与 08 一致：08 建立本帧输入，07 讲 Command 生产与组织。
- 与 09/10 边界清晰：不展开具体 pass 算法。
- 阻断项：none。

| 维度 | 结果 |
|---|---|
| 双素材信息价值 / 事实安全 / 单一主线 | pass |
| 概念护照 / Owner-Data-Control-Lifetime | pass |
| Worked case / 调试性能价值 / 源码克制 | pass |
| 跨章边界 | pass |
| 06 标定 | reached |

本报告不修改或重新判定章节完成状态。
