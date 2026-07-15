# 22 Compute Shader Coverage Matrix

> 本矩阵从当前最终正文反向重建。旧 CoverageMatrix、旧 TeachingEditReport、章节状态与公共 Gate 均未作为结论依据；未使用其他章节作标杆，未读取或使用第 06 章。

## 1. 材料基线

| 材料 | 角色 | 物理行数 | SHA-256 |
| --- | --- | ---: | --- |
| `.codex/tmp/renderingdeep_11_24_original_20260713/22_ComputeShader.md` | 冻结原版，信息价值基线 | 580 | `B0DD0CA1CA4286679BD525958149E39E4412361F6699ED974AA926F552B754EE` |
| `Engine/Docs/Tutorial_002_RenderingDeep/22_ComputeShader.md` | 当前最终正文，唯一教学覆盖判定对象 | 749 | `39126036AC51A17E75DDB265374BE5DD0B9FF8F94518B0EA3D585BADB94BE960` |
| `.codex/tmp/audit_22_23.md` | 冻结原版缺陷账本 | 仅作问题线索 | 不继承结论 |
| `.codex/tmp/review_22_23.md` | 当前正文独立复审 | 仅作交叉检查 | 不替代本矩阵逐项重建 |

正文落点均绑定上述 749 行版本；正文变化后必须重新定位，不得机械沿用行号。

## 2. 实际主线与边界

实际主线是：

`shader identity -> parameter/RDG manifest -> permutation -> AddPass/retention -> dispatch shape -> direct/indirect -> barrier -> async eligibility/fork-join -> compute PSO -> RHI/native recording -> finalize/payload/queue/fence -> output verified`

主案例 `view depth copy` 只承担 direct compute 的连续责任交接。Indirect args、无消费者裁剪、UAV overlap、有效 async overlap、cold PSO 等由局部案例承担，正文没有把一个简单案例伪装成覆盖所有机制。

## 3. 完成深度词表

| 状态 | 能证明 | 不能证明 |
| --- | --- | --- |
| Shader permutation present | 目标 shader map 中可取得该变体 | PSO ready、pass retained |
| Pass declared | `AddPass` 已产生候选图节点 | 节点未裁剪、lambda 已进入 |
| Pass retained | 编译后节点进入可执行图 | RHI 命令已记录 |
| Lambda entered | CPU 已开始执行该 pass 的录制逻辑 | native list 已 finalize、queue 已提交 |
| PSO ready/set | native compute PSO 可绑定且 SetPSO 已进入录制链 | dispatch 已提交或完成 |
| RHI recorded | 平台无关 dispatch 节点已记录 | 原生命令已提交 |
| Native encoded/finalized | `Dispatch`/`ExecuteIndirect` 已编码且命令列表进入 finalize/payload | GPU 已消费 |
| Queue submitted | payload 已交给目标 queue，并有完成 fence 值 | fence 已到达 |
| GPU complete | 对应 queue fence 达到目标值且无 device failure | 输出语义正确 |
| Output verified | 下游 consumer/readback/画面验证了预期结果 | 不能自动外推其他平台、规模或缓存状态 |

## 4. 教学与事实覆盖矩阵

| ID | 事实 / 教学单元 | O-D-C-L 与设计条件 | Worked case / 替代 | UE5.7 源码锚点（`path::symbol`） | 当前正文落点 | 状态 / 风险 |
| --- | --- | --- | --- | --- | --- | --- |
| 22-01 | Shader identity、编译资格与 runtime lookup 是不同状态 | **Owner** shader type / shader map；**Data** entry point、frequency、platform、permutation id；**Control** `ShouldCompilePermutation`、compilation environment；**Lifetime** shader map/ref 独立于 RDG 参数。Permutation 可裁剪代码但放大 cook、缓存与 PSO 组合；runtime 参数减少变体但保留动态路径。 | Depth copy shader type；若变化只影响小型数据且无需裁剪代码，优先 runtime 参数。 | `Engine/Source/Runtime/Renderer/Private/DepthCopy.cpp::DepthCopy::FViewDepthCopyCS`; `Engine/Source/Runtime/Renderer/Private/DepthCopy.cpp::DepthCopy::AddViewDepthCopyCSPass` | 76-125、191-219 | **Covered**。风险：取得 `TShaderRef` 不能外推 PSO 或执行完成。 |
| 22-02 | 参数结构同时是 shader binding contract 与 RDG dependency manifest | **Owner** 参数 metadata / RDG builder；**Data** SRV、UAV、uniform、access；**Control** 参数声明与 `ClearUnusedGraphResources`；**Lifetime** graph allocator 持有参数到 execute。没有 manifest，RDG 无法建立依赖、barrier 与寿命。 | Depth copy 的 SceneDepth SRV、RWDepth UAV、View uniform。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::AddPass`; `Engine/Source/Runtime/Renderer/Private/DepthCopy.cpp::DepthCopy::AddViewDepthCopyCSPass` | 129-149、180-187、223-250 | **Covered**。风险：参数存在不代表 pass 一定保留。 |
| 22-03 | Lambda capture 生命周期与 RDG 声明可见性是两条独立失败轴 | **Owner** C++ capture owner 与 RDG resource owner；**Data** 值、引用、裸指针、RDG/RHI ref；**Control** 值捕获、明确保活、graph allocation；**Lifetime** lambda 延迟到 graph execute。值捕获适合小型不可变 POD；引用或裸指针仅在 owner 明确覆盖执行期时可用。 | “依赖声明正确但悬空引用”与“指针仍活着但资源未声明”双反例。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::AddPass`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupParameterPass` | 150-178 | **Covered**。风险：不要把“值捕获”写成自动延长被指对象寿命。 |
| 22-04 | `AddPass` 只声明候选节点；retention 由 consumer、cull root、external output 或副作用决定 | **Owner** RDG compile/culling；**Data** producer-consumer reference count、external outputs、pass flags；**Control** 真实 consumer、extraction/external contract、`NeverCull`；**Lifetime** 被裁节点不进入执行期。优先表达真实数据价值，纯副作用才用 `NeverCull`，因为它会连带保留上游并增加工作和寿命。 | Depth copy output 被真实 consumer 使用；无消费者 pass / external output / side-effect pass 对照。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupPassDependencies`; `Engine/Source/Runtime/RenderCore/Public/RenderGraphDefinitions.h::ERDGPassFlags::NeverCull` | 223-300 | **Covered**。维护风险：不能把所有“external”概念合并成无条件保留。 |
| 22-05 | Group size、group count、数据域和尾部 bounds check 共同定义 dispatch shape | **Owner** CPU helper 与 shader；**Data** domain size、thread-group size、三维 group count；**Control** ceil-div 与 shader early-out；**Lifetime** 无特殊资源寿命。Group size 影响 occupancy、共享内存与尾部浪费，不能只按整除选择。 | Depth copy 的二维 direct dispatch。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::GetGroupCount`; `Engine/Source/Runtime/Renderer/Private/DepthCopy.cpp::DepthCopy::AddViewDepthCopyCSPass` | 304-335 | **Covered**。风险：合法 group count 不证明 shader 索引安全。 |
| 22-06 | Wrapped dispatch 只把超大 X 折到 Y，再把 Y 折到 Z；Z 仍可能超平台上限 | **Owner** helper 计算、调用方限制规模、shader 展开 linear id；**Data** `WrappedGroupStride=128`、目标 group 数、三维平台上限；**Control** `ValidateGroupCount` 对三维使用 `ensure`；**Lifetime** chunk 会引入跨 pass 状态。Wrapped 适合有明确上界的大 1D 工作，不是无限规模保证。 | 大 1D buffer 局部案例；Z 可能超限时改用 chunk、多 pass、work queue 或受控 indirect。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::GetGroupCountWrapped`; `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::ValidateGroupCount` | 336-364 | **Covered**，已修正冻结原版绝对化。残余风险：`ensure` 在目标配置中的行为不得被描述为硬阻止提交。 |
| 22-07 | Direct 与 indirect 不只差 group count 来源；indirect args 是跨 pass 的 GPU 工作合同 | **Owner** producer shader、RDG transition、consumer dispatch、queue fence；**Data** `FRHIDispatchIndirectParameters` 布局、usage、完整初始化、offset、容量、X/Y/Z；**Control** 4-byte 对齐、平台 boundary、UAV -> `IndirectArgs`；**Lifetime** 从创建到最后 consumer fence，复用前必须退休旧使用。 | `compact count -> build args -> indirect dispatch -> consume result`；CPU 已知且稳定时保留 direct，避免额外 producer/barrier/debug 成本。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::ValidateIndirectArgsBuffer`; `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::DispatchIndirect`; `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::AddPass`（indirect overload） | 368-410 | **Covered**。风险：producer recorded、transition planned 均不等于 args 已被 GPU consumer 安全读取。 |
| 22-08 | SRV/UAV transition 与 UAV barrier 来自 graph access contract | **Owner** RDG resource state compiler；**Data** previous/next access、pipeline、subresource、UAV handle；**Control** merge/transition 判定；**Lifetime** barrier 不替代资源保活。默认 barrier 以正确性换取保守同步。 | Depth copy UAV 写后由 consumer 读；缺声明、错 access、跨 pipe 使用作为反例。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphResources.cpp::FRDGSubresourceState::IsMergeAllowed`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphResources.cpp::FRDGSubresourceState::IsTransitionRequired` | 414-445 | **Covered**。风险：看到 transition 只证明同步被计划。 |
| 22-09 | `SkipBarrier` 只对可证明的连续 UAV 使用开放，不是跨 pipe 同步开关 | **Owner** UAV view flag 与 RDG dependency builder；**Data** no-barrier handle、相同 access、连续 producer 链；**Control** `SkipBarrier`；**Lifetime** async fork/join 与 transient alias 仍独立成立。不能列出完整访问闭包时保留默认 barrier。 | 连续 UAV append/clear 局部对照；graphics/async 交接仍需 dependency/fence。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::GetHandleIfNoUAVBarrier`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::AddLastProducer`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphResources.cpp::FRDGSubresourceState::IsTransitionRequired` | 438-468 | **Covered**。风险：相同资源或相同 access 不自动满足 no-barrier 条件。 |
| 22-10 | Async compute 是 eligibility 与调度区间，不是重叠承诺 | **Owner** RDG builder / graphics 与 async queues；**Data** pass flags、platform capability、fork/join、timestamps、queue fence；**Control** `GRDGAsyncCompute`、immediate mode、render-pass merge、efficient async、separate depth/stencil copy access、`GTriggerGPUProfile`；**Lifetime** 资源覆盖整个 async interval。 | Depth copy 作为“可调度但区间太短”的反例；长 compute 与独立 graphics 工作局部案例。紧邻 graphics producer/consumer 的短 pass 可留在 graphics compute。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphPrivate.h::IsAsyncComputeSupported`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::Compile`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::GetDeallocateFences` | 472-519 | **Covered**。正文区分 eligible、scheduled、overlapped、fence-complete。风险：ProfileGPU 形态不能代表正常运行重叠。 |
| 22-11 | Compute PSO cache entry、async completion 与 native readiness 是分层状态 | **Owner** `PipelineStateCache` 与 RHI；**Data** shader key、cache hit/miss、precache state、completion event、`RHIPipeline`；**Control** async compile allowed、file cache、precache；**Lifetime** cache entry 可先存在而 native pipeline 未 ready。同步创建简单但可卡顿；precache/async 降低关键帧成本但必须覆盖实际 permutation。 | Depth copy cold/warm 对照；PSO hit 不作为 dispatch 完成证据。 | `Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp::PipelineStateCache::GetAndOrCreateComputePipelineState`; `Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp::ExecuteSetComputePipelineState` | 523-546 | **Covered**。风险：shader ref、entry、native PSO、SetPSO recorded、dispatch recorded 不得合并。 |
| 22-12 | 参数绑定与 RHI command recording 仍是 CPU 侧命令构造 | **Owner** compute utility / RHI command list；**Data** shader parameters、PSO、dispatch dimensions；**Control** validation、bypass/recorded path；**Lifetime** command list/payload 持有后端所需引用。Bypass 改变线程与 batching，不改变 submit/completion 语义。 | Depth copy lambda 进入后依次 SetPSO、bind、record dispatch。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphUtils.h::FComputeShaderUtils::Dispatch`; `Engine/Source/Runtime/RHI/Public/RHICommandList.h::FRHIComputeCommandList::DispatchComputeShader` | 548-584 | **Covered**。风险：RHI recorded 不能写成“GPU 已收到”。 |
| 22-13 | D3D12 原生 `Dispatch` / `ExecuteIndirect` 之后仍有 finalize、payload、queue submit 与 fence completion | **Owner** D3D12 context、queue、submission/interrupt thread、GPU；**Data** native list、payload、queue、completion fence value、completed value；**Control** finalize/batching/parallel translate/submission；**Lifetime** payload 持有 allocator/resource 引用直到 completion 后释放。 | Depth copy 完成链延伸到 downstream/output verification。 | `Engine/Source/Runtime/D3D12RHI/Private/D3D12Commands.cpp::FD3D12CommandContext::RHIDispatchComputeShader`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Commands.cpp::FD3D12CommandContext::RHIDispatchIndirectComputeShader`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp::FD3D12Queue::FinalizePayload`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp::FD3D12DynamicRHI::FlushBatchedPayloads`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp::FD3D12DynamicRHI::ProcessInterruptQueue` | 588-618、643-653 | **Covered**，已修正冻结原版把 native call 当末端的错误。风险：D3D12对象名不可外推为所有RHI的ABI。 |
| 22-14 | 主案例必须以 output verified 收尾，症状诊断必须停在 last-valid-state | **Owner** 各阶段证据提供者；**Data** permutation、retention、PSO、recording、submission、fence、output；**Control** 逐门验证；**Lifetime** 资源与证据均绑定目标 frame/queue。 | View depth copy 完整回放；症状表把 pass 消失、非法 group、错 barrier、无重叠、PSO hitch、提交后无完成分别路由。 | `Engine/Source/Runtime/Renderer/Private/DepthCopy.cpp::DepthCopy::AddViewDepthCopyCSPass`; 上述各层源码锚点 | 620-724 | **Covered**。风险：主案例仍只代表 direct compute。 |

## 5. 残余风险

1. Cull root、external output、extraction 与 external resource 在实现上不是同义词；正文当前靠对照守住边界，后续编辑不得压成“external 自动保留”。
2. D3D12 payload/fence 用于说明通用完成层级，但具体对象名与异常处理不能外推到其他 RHI。
3. `view depth copy` 没有实际承担 indirect、长 async overlap 或 `SkipBarrier`；这些结论依赖局部案例与源码合同。
4. Wrapped dispatch 的 Z 上限取决于工作规模与平台能力；任何新数值案例都必须重新算上界。

## 6. 独立 BODY 结论

**BODY PASS**。当前正文完整覆盖 wrapped/RDG/async/PSO/completion 指定面，冻结原版的有效主线与主案例价值已迁移，两项高风险事实错误已修正；未发现事实越权、关键教学单元缺失或完成深度跳步。本结论独立于旧 sidecar、章节状态和公共 Gate。
