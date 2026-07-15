# Teaching Edit Report - 24 Optimization

> 本报告描述当前 591 行最终正文实际形成的教学结构。冻结原版只作为信息价值基线；旧 sidecar、章节状态和公共 Gate 不参与结论。未使用其他章节作质量标杆，未读取或使用第 06 章。

## 1. 实际主线

当前正文的主线是：

`measurement contract -> real timelines and critical path -> tool depth/observation cost -> cost owner and input data -> cross-frame state -> falsifiable hypothesis/last-valid-state -> single-variable experiment -> product decision -> automation/completion`

因此本章不再把优化理解为“从`stat unit`找到高event，再关一个CVar”。它要求先定义可比较的实验，再把时间归属推进到owner/data，处理cold/warm与跨帧状态，最后通过质量、正确性、内存、平台和自动化门。

## 2. 冻结原版价值与迁移

| 冻结原版独特价值 | 当前正文迁移位置 | 处理结果 |
| --- | --- | --- |
| 六步优化闭环 | 369-421、423-519、576-591 | 保留并升级为合同、证伪、产品决策与自动化 |
| Game/Render/RHI/GPU分流 | 91-145 | 保留；补真实overlap、Present、async/copy、IO/streaming |
| 工具到event再回源码阶段 | 147-197 | 保留；补owner、观测深度、成本和build/channel条件 |
| Visibility、MeshDrawCommand、GPUScene、BasePass、Lighting、VSM、Nanite、Lumen、PostProcess导航 | 198-324 | 保留；改为O-D-C-L和成本输入，而非功能清单 |
| CVar作为实验开关而非结论 | 403-421 | 保留；补mutability、set-by、platform override与side effects |
| 路口spike贯穿案例 | 423-519 | 保留并升级为双profile cold/warm产品案例 |
| 判断瓶颈是否转移 | 497-519、557-574 | 保留；扩展到Present、streaming、memory、quality和rollback |

冻结原版的主要导航价值全部迁移。原版中Game wait解释、visibility总闸门、dynamic instancing、EarlyZ实验条件等事实风险被修正，不作为需保留的价值。

## 3. O-D-C-L 教学模型

| 单元 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| Measurement | perf/product owner | target、workload、platform/SKU、Build/RHI、resolution、run distribution | run matrix、warmup、noise/percentile/hitch threshold | cache/residency/history跨run |
| Timelines | Game/Render/RHI、graphics/async/copy、Present、IO | task/wait、queue timestamp/fence、present wait、pending IO | lag/sync、async、VSync/cap | 多帧overlap和queue完成 |
| Tooling | stat/trace/profiler/capture/CSV owner | summary、CPU scopes、GPU scopes、resource/native command、long-run series | channel/build/profile trigger | instrumentation可改变运行形态 |
| Renderer cost | Visibility/MDC/GPUScene/pass subsystem owner | object、command、dirty upload、pixel、light/page/cache/history input | content/view/quality/resolution/CVar | scene/view/cache/history状态不同 |
| Cross-frame hitch | residency/streaming/PSO/shader/dynamic-res owner | budget、pending、IO、miss/too-late、compile/finalize、fraction/history | prefetch/precache/budget/time slice | cold/warm、pending清空、resource retirement |
| Product completion | platform/graphics/content/product/CI owner | perf、latency、pacing、quality、correctness、memory、build/cook/package | rollout/rollback、threshold、automation | 基线随版本受控更新 |

## 4. 设计理由、替代与重访条件

| 选择 | 选择理由 | 替代 | 何时重访 |
| --- | --- | --- | --- |
| 先写测量合同 | 没有冻结条件的数字不可比较 | 探索阶段可快速采样，但不能作结论 | workload、平台、RHI、resolution、cold/warm任一变化 |
| 多run分布 | 单帧或平均值掩盖抖动与hitch | median用于稳态，p95/p99/hitch count用于尾部 | 分布多峰、噪声带过大或样本不足 |
| 工具逐层加深 | 控制观测成本并保持现场 | capture/profile用于判别，不长期常开 | 浅层证据不能区分候选owner时 |
| 从event回owner/data | event名只给相关系统，不给内容根因 | 内容隐藏、输入计数、资源状态单变量 | event下降但其他时间线或质量反弹 |
| CVar只作实验控制 | 值可能不可变、被覆盖、需restart/rebuild/recreate | 内容、视图、分辨率或代码实验 | consumer未使用目标值或副作用改变多条因果边 |
| 双profile分别A/B | 避免跨RHI/分辨率比较绝对值 | 同一profile内部比较 | capability、quality或memory合同不同 |
| 产品硬门与rollback | 性能收益不能覆盖质量/正确性失败 | 平台特化方案 | 新平台、内容分布或目标预算变化 |

## 5. 成本单元与事实修正

1. **测量合同**：补齐latency/throughput/pacing、workload、平台/SKU、Build/RHI、分辨率、VSync/cap/dynamic res、冷热态、样本与噪声。
2. **真实时间线**：从四个HUD数字扩展到Game/Render/RHI、graphics/async/copy、Present、IO/streaming与三帧overlap。
3. **`stat unit`**：明确普通Game时间已扣除tracked waits；critical-path另表达依赖wait。`stat Raw`、`stat UnitMax`、`stat UnitCriticalPath`不作为Shipping稳定合同。
4. **工具边界**：每个工具记录owner、深度、观测成本、build/channel条件；ProfileGPU不是normal-run性能基线。
5. **Scene与view**：Scene change和view visibility拆为两条链，不再把visibility写成所有场景变化的总闸门。
6. **Dynamic instancing**：修正为共享draw state匹配，不要求primitive data值相同；per-primitive数据通过primitive-id路径索引。
7. **成本输入**：Visibility/MDC/Instance Culling/GPUScene/BasePass/Lighting/VSM/Nanite/Lumen/PostProcess都按owner/data/control/lifetime解释。
8. **跨帧状态**：新增memory/residency、Texture/Nanite streaming、PSO runtime hitch、shader compile/finalization、dynamic resolution与history。
9. **CVar**：补mutability、set-by、thread/cache owner、platform override、restart/recompile/recreate和cache side effects；EarlyZ不再作为无条件runtime开关。
10. **产品完成**：新增自动化基线、平台产品矩阵、rollback和completion checklist。

## 6. 双 Profile 路口案例

案例不是只证明“VSM event下降”，而是展示证据如何推进到产品决策：

- 在结果前冻结两个profile各自的Build/RHI/capability、resolution/sync、同一逻辑replay、cold/warm定义、baseline/改后各7 runs、噪声带、warm预算、cold p95、IO peak、pending清空帧、residency/pool、latency、质量和内存门。
- 只在各profile内部做A/B，不跨RHI或分辨率比较绝对值。
- 先从统计定位non-Nanite VSM raster work，再检查caster group、page demand、allocation/residency、CPU与其他GPU阶段，避免从event直接跳“foliage太多”。
- 主SKU cold p95 `48.2 -> 39.6 ms`，通过预置`<=42 ms`；Profile B `34.0 -> 28.9 ms`，通过预置`<=30 ms`。
- 两个profile同时记录IO peak、pending完成帧、0 residency fault、pool未over-budget与无eviction/residency pop。
- Profile B虽然通过性能、cold、IO/streaming/residency、latency和内存门，但预先冻结的质量门失败，因此回滚；性能收益没有覆盖质量失败。

这些数字明确属于教学示例，不能替代实际项目artifact。

## 7. Last-Valid-State

统一记录模板是：

```text
Observation: 哪个分布、percentile或hitch触发调查？
Last valid state: 证据最后确认到哪个timeline、owner、data状态和完成深度？
First invalid/unknown state: 从哪一步开始只有相关性或猜测？
Next discriminating test: 哪个单变量实验能区分剩余假设？
Invalidation conditions: 哪些platform/cache/resolution/build/streaming条件会使证据失效？
Artifact: CSV/trace/ProfileGPU/capture/config/revision在哪里？
```

例如`RenderVirtualShadowMaps(Non-Nanite)`高，last-valid-state只是“目标GPU work高”；只有caster group实验同步降低对应command/instance/page输入，且allocation、lighting、CPU、quality、memory没有等量反弹，才能推进内容根因。

## 8. 自动化与完成门

当前正文把“优化完成”定义为同时满足：

- 合同冻结且baseline/改后都有多run统计与artifact。
- 目标frame/latency/pacing预算达成，不只是局部event下降。
- Game/Render/RHI/GPU/Present/streaming没有不可接受的新critical path。
- memory/residency/pool/IO、cold启动、PSO/shader、dynamic-res/history无回归。
- 质量、正确性、平台capability、build/cook/package/预热和维护成本通过产品矩阵。
- rollback条件明确，自动化保存配置、revision、硬件和冷热态metadata。

## 9. 源码克制

正文不以源码文件清单或调用顺序组织教学。`FStatUnitData::DrawStat`、`MatchesForDynamicInstancing`、`ProcessAsyncResults`、EarlyZ CVar等符号只在需要精确边界时出现。完整`path::symbol`、条件和风险集中在`24_Optimization_CoverageMatrix.md`，源码用于事实核验和debug anchor，不替代成本模型。

## 10. 残余风险

1. 案例数值是教学示例；实际项目必须生成自己的CSV、trace、profile/capture、reference和平台数据。
2. Profile/capture会污染normal-run形态，尤其async、parallel translate、residency与IO时序。
3. Dynamic resolution、streaming、PSO/shader和temporal history跨run延续，仅恢复CVar不足以恢复baseline。
4. 子系统event与符号可能随UE重构；应维护owner/data/control/lifetime模型，而不是把字符串当永久合同。

## 11. 独立 BODY 结论

**BODY PASS**。当前正文保留冻结原版六步闭环、子系统导航与CVar实验价值，并完成measurement/time/cost/CVar/platform/product completion指定面；O-D-C-L、设计替代、双profile案例、last-valid-state、事实修正、自动化与产品门均闭环。未发现需要修改正文的阻断。本结论不修改也不依赖章节状态或公共Gate。
