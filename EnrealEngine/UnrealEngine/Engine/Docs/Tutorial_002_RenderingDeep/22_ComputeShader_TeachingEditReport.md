# Teaching Edit Report - 22 Compute Shader

> 本报告描述当前 749 行最终正文实际形成的教学结构，不描述编辑愿望。冻结原版用于检查信息价值迁移；旧 sidecar、章节状态和公共 Gate 不参与结论。未使用其他章节作质量标杆，未读取或使用第 06 章。

## 1. 实际主线

当前正文不是从一个 `Dispatch` API 横向罗列函数，而是沿责任交接推进：

`shader identity -> parameter/RDG manifest -> permutation -> AddPass candidate -> retention -> dispatch shape -> direct/indirect contract -> transition/barrier -> async eligibility/fork-join -> compute PSO -> RHI/native recording -> finalize/payload/queue/fence -> output verified`

这条主线解决的核心误区是：CPU侧“已经写了dispatch”不等于RDG会保留它，不等于PSO已就绪，不等于命令已提交，更不等于GPU完成或输出正确。

## 2. 冻结原版价值与迁移

| 冻结原版独特价值 | 当前正文迁移位置 | 处理结果 |
| --- | --- | --- |
| shader type、参数、RDG pass、group count、transition、RHI/backend的分层骨架 | 76-125、129-187、223-300、304-468、523-618 | 保留并补足责任边界 |
| view depth copy低噪声贯穿案例 | 25-45、251-276、314-335、620-656 | 保留为direct compute主案例 |
| 参数结构体的shader binding与graph dependency双重身份 | 129-149、180-187 | 保留；新增C++ capture寿命独立轴 |
| permutation与compile environment的区别 | 191-219 | 保留；补runtime参数替代与重访条件 |
| `AddPass`不等于立即dispatch | 223-250 | 保留；推进到retention与完成深度 |
| direct/indirect、barrier、async、RHI/backend的入口认知 | 368-618 | 保留并重写为完整合同，不再停在API标签 |
| 按症状反推故障层 | 660-724 | 保留；统一使用last-valid-state与完成词表 |

原版没有因压缩而丢失可迁移价值。原版中错误或危险的结论被替换，不计作“价值保留”。

## 3. O-D-C-L 教学模型

| 单元 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| Shader/permutation | shader type、shader map | platform、frequency、permutation id、defines | compile predicate、runtime lookup | shader map/ref与graph参数分离 |
| Parameters/RDG | metadata、graph builder | SRV/UAV/uniform/access | parameter declaration、graph allocation | 参数活到graph execute；capture对象另算 |
| Retention | RDG compiler/culler | producer-consumer引用、external output、flags |真实consumer、extraction、`NeverCull` | 被裁节点不进入执行期；`NeverCull`可延长上游链 |
| Dispatch shape | CPU helper与shader | domain、group size/count、platform limits | ceil-div、wrapped、chunk、early-out | chunk引入跨pass中间状态 |
| Indirect args | GPU producer、RDG、consumer | args布局、offset、容量、X/Y/Z | 初始化、UAV->IndirectArgs、最后consumer | 活到最后queue fence；复用前退休 |
| Barrier/async | RDG state compiler、graphics/async queues | access、pipeline、fork/join、timestamps | default barrier、`SkipBarrier`、async flags/capability | 资源覆盖跨pipe interval与fence |
| PSO/RHI/backend | PipelineStateCache、RHI、native queue | cache entry、native PSO、command list、payload、fence | precache/async compile、record/finalize/submit | payload和资源引用保持到completion |

正文将O-D-C-L放进每层失败模式和验证点，而不是单独附一份术语表。

## 4. 设计理由、替代与重访条件

| 选择 | 选择理由 | 替代 | 何时重访 |
| --- | --- | --- | --- |
| Permutation | 编译期裁剪代码与功能组合 | runtime uniform/constant | 变体/cook/PSO组合过多，或差异只是一小段动态数据 |
| Graph parameters | 让RDG看见依赖、barrier和寿命 | 无可等价的隐式capture替代 | 参数过大时可重构数据owner，但不能绕开manifest |
| 真实consumer保留pass | 表达数据价值且允许无用工作裁剪 | external output/extraction；纯副作用才`NeverCull` | pass确实只有图外副作用，且能接受上游链被保留 |
| Wrapped 1D dispatch | 在有上界的大工作量中折叠X/Y/Z | chunk、多pass、work queue、indirect | 计算后Z可能超限，或单次工作过大难以调度/验证 |
| Direct dispatch | CPU已知工作量，路径简单 | indirect args | 工作量由GPU生成且readback/CPU同步成本更高 |
| 默认UAV barrier | 保守正确 | 受证明的`SkipBarrier`/overlap | 能列出完整访问闭包、连续UAV条件和跨pipe同步时 |
| Graphics compute | 同queue同步简单 | async compute | 有足够独立graphics区间可覆盖compute，且生命周期/fence成本可接受 |
| PSO precache/async | 避免关键帧同步创建 | 同步创建 | 实际permutation覆盖不足、首次使用仍等待或平台不支持时 |

## 5. 案例结构

**主案例：view depth copy**

- 承担shader identity、参数manifest、permutation取得、direct group count、AddPass、PSO/binding/recording和最终输出验证。
- 最终链是：`permutation present -> pass declared -> retained -> lambda entered -> PSO ready -> parameters bound -> RHI recorded -> native encoded/finalized -> queue submitted -> fence complete -> output verified`。
- 不承担indirect args、长async overlap或UAV skip-barrier的实际实作证明。

**局部案例**

- 大1D buffer：验证wrapped折叠、Z上限、shader early-out与chunk替代。
- GPU compact count：验证args创建/初始化/producer/transition/offset/consumer/退休。
- 无消费者、external output与纯副作用pass：验证retention和`NeverCull`边界。
- 连续UAV与普通写后读：验证默认barrier与`SkipBarrier`证明义务。
- 短compute与长compute区间：区分async eligible、scheduled、overlapped、fence-complete。
- Cold/warm PSO：区分shader ref、cache entry、native ready和dispatch recorded。

## 6. Last-Valid-State

正文采用以下不可跳步状态链：

```text
shader permutation present
-> pass declared
-> pass retained
-> lambda entered
-> native compute PSO ready / set recorded
-> parameters bound
-> RHI dispatch recorded
-> native dispatch encoded and list finalized
-> payload / queue submitted
-> target queue fence complete
-> downstream output consumed and verified
```

Wrapped合法只推进到“dispatch参数通过当前检查”；RDG transition只推进到“同步计划存在”；async timeline出现只推进到“scheduled”；PSO cache hit只推进到“entry/ready状态”；native `Dispatch`只推进到“encoded”。

## 7. 事实修正

1. **Wrapped dispatch**：冻结原版暗示wrapped后必为合法三维group count。当前正文明确helper只将X折到Y、Y折到Z，Z仍可超限；`ValidateGroupCount`使用`ensure`，调用方仍负责规模上界。
2. **Native dispatch完成深度**：冻结原版把D3D12 `Dispatch`/`ExecuteIndirect`写成末端。当前正文补齐list finalize、translation/payload、`ExecuteCommandLists`、completion fence和output verification。
3. **Pass culling**：从“可能被裁”扩展为consumer、cull root、external output、纯副作用与`NeverCull`的选择边界。
4. **Capture lifetime**：从只讲graph allocator扩展为值/引用/裸指针捕获与RDG声明两条独立失败轴。
5. **Indirect args**：从group count来源差异扩展为创建、初始化、producer、UAV->IndirectArgs、offset/容量、consumer与复用/退休全生命周期。
6. **UAV overlap**：明确`SkipBarrier`需要no-barrier handle和连续UAV条件，不能替代跨graphics/async同步。
7. **Async**：补齐eligibility谓词、fork/join、资源寿命，并拆分eligible/scheduled/overlapped/fence-complete。
8. **Compute PSO**：补齐cache miss、precache、async completion与native `RHIPipeline` ready。

## 8. 源码克制

正文只保留帮助建立状态模型的类型、event、flag和命令名，没有堆入`Engine/Source`路径、行号或调用栈。稳定`path::symbol`、实现条件和事实核对集中在`22_ComputeShader_CoverageMatrix.md`。因此源码承担事实验证和debug anchor，不承担教学顺序。

## 9. 残余风险

1. Cull root、external output、extraction/external resource容易被后续编辑误并为同一概念。
2. D3D12完成链用于说明语义层级；其他RHI必须重新映射具体对象与异常路径。
3. 主案例的教学边界必须保持为direct compute，不能声称已实证indirect、长async或skip-barrier。
4. Wrapped的Z上限与平台能力有关，新案例必须重新计算，不得只复用“128”结论。

## 10. 独立 BODY 结论

**BODY PASS**。当前正文主线连续，冻结原版独特价值已迁移，O-D-C-L、设计理由/替代、局部案例、last-valid-state和完成深度均闭环；wrapped与native completion两项高风险事实已修正。未发现需要修改正文的阻断。本结论不修改也不依赖章节状态或公共Gate。
