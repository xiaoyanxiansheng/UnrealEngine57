# 07 MeshDrawCommand Coverage Matrix

> 依据最终正文重建；不改变章节状态。

## 四次换形与三本账
| 单元 | 教学合同 | 结果 |
|---|---|---|
| 候选几何 | SceneProxy/collector → `FMeshBatch`；只表达候选 | pass |
| Pass 配方 | Batch + policy → `FMeshDrawCommand`；Processor 决策，cached/dynamic 生命周期分离 | pass |
| 本帧工作集 | Command + view → Visible Command、sort、bucket、culling input | pass |
| 可执行命令 | culling result → RHI recording；recording ≠ Queue Submit ≠ GPU completion | pass |
| Pass 决策账 | 解释接受/拒绝、material/VF/permutation 与 pass 缺失 | pass |
| Command 配方账 | 解释 bindings/streams/range/`PrimitiveIdInfo`/PSO recipe、缓存与失效 | pass |
| 本帧提交账 | 解释 visible/sort/bucket/payload/args、culling 与录制 | pass |

## 承重概念
| 概念 | Owner / Data / Control / Lifetime / Boundary | 结果 |
|---|---|---|
| Mesh Pass / Processor | Processor 控制 pass 解释与命令生产；不拥有资源生命周期或最终可见性 | pass |
| `FMeshBatch` | 高层候选合同；未决定 shader、固定状态、顺序、本帧可见性 | pass |
| `FMeshDrawCommand` | scene cache 或 one-frame storage 的稳定配方；不拥有引用资源，不等于 RHI PSO | pass |
| `PrimitiveIdInfo` | 连接 06 GPUScene 身份读取；dynamic range 消费前冻结 | pass |
| Visible / Bucket / Sort | 当前 view 工作项、模板共享与正确排序分层；bucket 不等于 draw | pass |
| Dynamic Instancing | bindings、payload、顺序、身份兼容后才可 compact | pass |
| Instance Culling | 消费可见命令，不重做材质/pass 决策；可合法 no-op | pass |
| Cached / Dynamic | 生产时机、Owner、Lifetime 差异；不等于 Component Mobility | pass |

## 金属板案例与调试
- 两块同 mesh/material 金属板贯穿 Depth/Base 配方、缓存、view 选择、排序、bucket、Instance Culling 和 RHI recording。
- 案例证明共享模板与独立 primitive/instance 身份可同时成立；procedural mesh 对照补足 one-frame command 和 dynamic finalize。
- 调试覆盖：Depth 有/Base 无、旧 shader、Draw Calls 多于 Bucket、dynamic 串位、Visible 正确但 capture 无 Draw。
- 统一证据链：pass decision → command recipe → visible/culling → recording → Queue Submit → GPU consumer。

## 双素材与相对 Origin 审计
| Origin/旧稿价值 | 最终落点 | 判定 |
|---|---|---|
| Batch 到 draw 的问题 | 四次换形唯一主线 | preserved |
| Processor/pass 决策 | 决策所有权与边界 | strengthened |
| cached/dynamic 路径 | 生产时机、Owner、Lifetime | strengthened |
| bindings/streams/range/lookup/PSO | Command 数据合同 | translated |
| Visible/sort/bucket/instancing | 因果顺序 | strengthened |
| Culling/GPUScene 交接 | dynamic finalize 与不重做材质决策 | strengthened |
| Submit 展开 | recording、Queue、GPU 完成深度 | corrected |
| 函数清单、路径行号、验证记录 | 删除载体，含义迁入模型和调试树 | no unique value lost |

- 当前正文约 583 行，Origin 区间约 486 行，未触发缩短 15% 预警。
- 仍执行专项审计；被压缩的是重复路径、成员枚举和验证载体，独有原理、条件、例外、案例与调试判断均有落点。

## 验收
| 维度 | 结果 |
|---|---|
| 双素材信息价值 / UE5.7 事实 / 单一主线 | pass |
| 概念首次教学 / Owner-Data-Control-Lifetime | pass |
| 条件边界 / Worked case / 调试价值 | pass |
| 源码克制 / 跨章一致性 | pass |
| 06 标定 | reached |
