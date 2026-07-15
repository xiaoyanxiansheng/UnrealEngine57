# 23 Debugging Coverage Matrix

> 本矩阵从当前最终正文反向重建。旧 CoverageMatrix、旧 TeachingEditReport、章节状态与公共 Gate 均未作为结论依据；未使用其他章节作标杆，未读取或使用第 06 章。

## 1. 材料基线

| 材料 | 角色 | 物理行数 | SHA-256 |
| --- | --- | ---: | --- |
| `.codex/tmp/renderingdeep_11_24_original_20260713/23_Debugging.md` | 冻结原版，信息价值基线 | 618 | `2B477AC05357EAA7D6211A4E271C4707E811BD6E97759E582CADEF03A59B7E54` |
| `Engine/Docs/Tutorial_002_RenderingDeep/23_Debugging.md` | 当前最终正文，唯一教学覆盖判定对象 | 713 | `14DB65A5425406F40D37FA6AA7230EBE66676FF7D1FC8479060E9AC37DD3B390` |
| `.codex/tmp/audit_22_23.md` | 冻结原版缺陷账本 | 仅作问题线索 | 不继承结论 |
| `.codex/tmp/review_22_23.md` | 当前正文独立复审 | 仅作交叉检查 | 不替代本矩阵逐项重建 |

正文落点均绑定上述 713 行版本；正文变化后必须重新定位。

## 2. 实际主线与边界

实际主线是：

`symptom routing -> output/pass/resource/capture/timeline/feature-shader/CVar evidence -> single-variable reproduction -> last-valid-state -> completion depth`

输出、pass、resource、timeline、feature/shader 不是固定五层 checklist。黑屏通常从 output/consumer 开始；旧值从 resource producer/lifetime 开始；pass 消失从 RDG retention 开始；GPU hang 从 queue/crash evidence 开始；shader missing 从 permutation/ODSC 开始；CVar 无效从 flags/set-by/consumer 链开始。

## 3. 统一完成词表

| 状态 | 可接受证据 | 不能外推 |
| --- | --- | --- |
| Declared | CPU graph 中存在 pass/resource 声明 | pass retained、命令 recorded |
| Retained | RDG compile 后未被裁剪 | lambda 已进入 |
| Recorded | RHI/native command 或 marker 已录入目标边界 | queue submitted、GPU 到达 marker |
| Submitted | payload/command list 已交给目标 queue | GPU 已进入或退出 scope |
| Marker entered | GPU marker-in / breadcrumb 证明已进入区间 | 区间已结束、写入完成 |
| Marker exited | marker-out / timestamp close 证明离开区间 | 目标 payload fence 已完成、输出正确 |
| GPU complete | 目标 queue fence 达值且无 device failure | 输出内容符合语义 |
| Output verified | 最终 consumer、readback 或画面验证预期结果 | 其他 build/RHI/frame 同样成立 |

## 4. 教学与事实覆盖矩阵

| ID | 事实 / 教学单元 | O-D-C-L 与设计条件 | Worked case / 替代 | UE5.7 源码锚点（`path::symbol`） | 当前正文落点 | 状态 / 风险 |
| --- | --- | --- | --- | --- | --- | --- |
| 23-01 | 症状决定最高信息量入口，层级图不是固定顺序 | **Owner** 当前最可能失效的系统；**Data** 症状类型、最后有效输出、frame/build/RHI；**Control** 选择下一项可证伪测试；**Lifetime** 资源问题、命令问题和跨帧缓存各走不同时间轴。固定从屏幕一路向下会浪费证据并可能改变现场。 | 合成纹理黑屏主案例；GPU hang、shader missing、CVar 无效三种短路入口。 | 无单一实现符号；路由由后续各 owner 源码共同约束 | 31-55、87-100、581-683 | **Covered**。风险：路线名称不能被当成强制执行顺序。 |
| 23-02 | 工具输出必须翻译为引擎状态和证据上限 | **Owner** 工具、引擎层、调查者分开；**Data** observation、expected、owner、last-valid-state、unknown、next test；**Control** 证据链记录模板；**Lifetime** 绑定具体 frame/queue/resource version。 | 黑屏案例从最终 SceneColor consumer 反推。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupPassDependencies`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp::FD3D12DynamicRHI::ProcessInterruptQueue` | 112-164 | **Covered**。风险：相关性不得写成完成证明。 |
| 23-03 | 输出层只确认颜色流是否到最终 consumer | **Owner** renderer output / post chain / viewport-present path；**Data** scene color、override、view rect、format；**Control** bypass/replace/consumer 检查；**Lifetime** final output 可能已被后续 pass 覆盖。 | 黑屏先确认最终输出而不是先看中间纹理。 | `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp::AddPostProcessingPasses`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Viewport.cpp::FD3D12Viewport::Present` | 168-186、585-607 | **Covered**。风险：屏幕正常不能证明所有中间资源正确。 |
| 23-04 | Pass/event 名只是条件成立时的关联键，不是跨工具全局 ID | **Owner** RDG events、GPU profiler、capture backend、breadcrumbs；**Data** string、frame、queue、resource id、marker state；**Control** `RDG_EVENTS`、profile/capture/breadcrumb build 条件；**Lifetime** 名字在 graph、recorded marker 与 GPU marker 中深度不同。 | `CompositeCustomTextureIntoSceneColor` 的 graph 名、capture event、GPU scope、breadcrumb 四列对照。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphDefinitions.h::RDG_EVENTS`; `Engine/Source/Runtime/RenderCore/Public/RenderGraphEvent.h::RDG_EVENT_SCOPE`; `Engine/Source/Runtime/RHI/Public/RHIBreadcrumbs.h::FRHIBreadcrumbEventManual` | 190-216 | **Covered**，已修正冻结原版“共同坐标”绝对化。风险：同名仍可能跨 frame/queue 重复。 |
| 23-05 | RDG 依赖来自参数结构，不来自 lambda 隐式捕获 | **Owner** graph parameter metadata；**Data** SRV/UAV/access；**Control** 参数声明；**Lifetime** graph resource 与 C++ capture 分离。 | pass 存在但依赖缺失的最小反例。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupParameterPass`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupPassDependencies` | 218-240 | **Covered**。风险：修复捕获寿命不等于修复 RDG 合同。 |
| 23-06 | RDG culling、validation 与 clobber 是三种不同证据 | **Owner** graph compiler / validation / auxiliary clobber pass；**Data** reference counts、validation errors、clobber pattern；**Control** debug build、`GRDGValidation`、`GRDGClobberResources`、clobber allowance；**Lifetime** clobber 增加 producer、barrier并改变时序。Validation 低侵入地查合同；clobber 高可见但更侵入；capture 查看实际内容。 | baseline -> validation only -> restore -> clobber only -> restore。 | `Engine/Source/Runtime/RenderCore/Public/RenderGraphDefinitions.h::RDG_ENABLE_DEBUG`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupPassDependencies`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::ClobberPassOutputs` | 234-255、608-628 | **Covered**。风险：无 validation 错误不证明内容正确；clobber 色不能单独定根因。 |
| 23-07 | Resource capture 显示内容/状态快照，RDG解释生产、访问和寿命 | **Owner** capture backend 与 RDG resource state；**Data** resource id/version、subresource、access、producer/consumer；**Control** 选择捕获点；**Lifetime** 别名、transient与跨帧资源会改变“同名资源”含义。 | 旧值/NaN/未初始化资源路线。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphResources.cpp::FRDGSubresourceState::IsTransitionRequired`; `Engine/Source/Runtime/RenderCore/Private/RenderGraphBuilder.cpp::FRDGBuilder::SetupPassDependencies` | 259-295、608-618 | **Covered**。风险：像素值异常不自动指出错误 producer。 |
| 23-08 | Capture 有 GT enqueue、immediate RHI 与 RDG-local 三种边界 | **Owner** `FScopedCapture` 与 provider；**Data** begin/end request、command list/graph builder、目标 frame；**Control** next-frame CVar、immediate-list requirement、`NeverCull` begin/end passes；**Lifetime** 请求时刻不等于实际 capture 边界。 | Deferred renderer next-frame capture；局部 RDG capture；immediate RHI capture。 | `Engine/Source/Runtime/RenderCore/Private/RenderCaptureInterface.cpp::RenderCaptureInterface::FScopedCapture`; `Engine/Source/Runtime/Renderer/Private/DeferredShadingRenderer.cpp::r.CaptureNextDeferredShadingRendererFrame` | 296-319 | **Covered**。风险：capture 中 recorded command 不等于原始运行形态 GPU complete。 |
| 23-09 | Insights、GPU Visualizer/ProfileGPU、capture、breadcrumb、queue fence各自只拥有一段证据 | **Owner** CPU trace / GPU timestamp profiler / backend capture / crash marker / queue completion；**Data** task/wait、scope timestamp、native commands/resources、marker-in/out、fence value；**Control** build/channel/profile trigger；**Lifetime** 都绑定目标运行形态与frame。 | 工具互补矩阵；不让单一工具回答全部问题。 | `Engine/Source/Runtime/Core/Public/ProfilingDebugging/CpuProfilerTrace.h::TRACE_CPUPROFILER_EVENT_SCOPE`; `Engine/Source/Runtime/RHI/Private/GPUProfiler.cpp::FGPUProfilerSink_StatSystem`; `Engine/Source/Runtime/RHI/Private/GPUProfiler.cpp::GCommand_ProfileGPU` | 321-399 | **Covered**。风险：scope time 不是resource bit pattern，CPU wait也不是queue completion。 |
| 23-10 | `ProfileGPU` 会改变 async eligibility 与 parallel translate | **Owner** RDG async capability与RHI submit state；**Data** `GTriggerGPUProfile`；**Control** profile command；**Lifetime** 只影响被profile的运行形态。为可解释scope牺牲部分正常并行，结果用于定位而不是直接当shipping性能基线。 | 同场景normal trace与profile run分开保存。 | `Engine/Source/Runtime/RenderCore/Private/RenderGraphPrivate.h::IsAsyncComputeSupported`; `Engine/Source/Runtime/RHI/Private/RHICommandList.cpp::FRHICommandListExecutor::FSubmitState::Submit`; `Engine/Source/Runtime/RHI/Private/GPUProfiler.cpp::GCommand_ProfileGPU` | 341-386 | **Covered**。残余风险：后续性能文档不得把profile形态时间直接当正常形态。 |
| 23-11 | DebugView/Visualize 是特性分支、shader permutation、pass与输出消费的完整链 | **Owner** feature branch、material shader map、renderer pass；**Data** debug mode、permutation id、shader availability、output；**Control** feature/build/ODSC；**Lifetime** shader request与目标frame pass分离。 | DebugView异常路线。 | `Engine/Source/Runtime/Renderer/Private/BasePassRendering.h::TBasePassCS::FVisualizeDim`; `Engine/Source/Runtime/Renderer/Private/BasePassRendering.h::TBasePassCS::ShouldCompilePermutation`; `Engine/Source/Runtime/Renderer/Private/BasePassRendering.h::TBasePassPS::ShouldCompilePermutation` | 403-460、660-670 | **Covered**。关键事实：visualize permutation是ODSC-only；普通cook不保证常驻。 |
| 23-12 | Shader missing 要区分branch selected、ODSC request、shader map available、pass recorded、output consumed | **Owner** material/shader compiler/ODSC/renderer；**Data** shader types、vertex factory、permutation flags、shader map completeness；**Control** ODSC active/force recompile/default material fallback；**Lifetime** compile request可跨帧完成。 | Visualize selected但shader map未就绪的局部链。 | `Engine/Source/Runtime/Engine/Private/Materials/MaterialShared.cpp::FMaterial::TryGetShaders`; `Engine/Source/Runtime/Renderer/Private/BasePassRendering.h::TBasePassPS::ShouldCompilePermutation` | 437-465、660-670 | **Covered**。风险：HLSL存在不证明目标变体已cook或已到达rendering shader map。 |
| 23-13 | CVar调试是flags -> set-by -> thread shadow -> sink/cache -> consumer -> restart/rebuild/recreate六段链 | **Owner** console manager、thread propagation、sink、consumer；**Data** value、flags、LastSetBy、shadow/cache、消费时点；**Control** priority、`ReadOnly/Cheat/RenderThreadSafe`、sink、startup read、shader key、render-state/resource recreation；**Lifetime** 对象值可先于目标线程/缓存/资源生效。 | CVar值打印正确但consumer仍用旧cache；被更高set-by拒绝；需要restart/recompile的对照。 | `Engine/Source/Runtime/Core/Public/HAL/IConsoleManager.h::EConsoleVariableFlags`; `Engine/Source/Runtime/Core/Public/HAL/IConsoleManager.h::TConsoleVariableData`; `Engine/Source/Runtime/Core/Private/HAL/ConsoleManager.cpp::FConsoleVariableBase::CanChange`; `Engine/Source/Runtime/Core/Private/HAL/ConsoleManager.cpp::FConsoleManager::CallAllConsoleVariableSinks` | 466-515、672-683 | **Covered**。风险：设置接受、sink ran、consumer used必须分别记录。 |
| 23-14 | 复现矩阵必须冻结build/RHI/frame/warmup/CVar/历史与观测工具 | **Owner** 调查者/自动化；**Data** baseline、单变量、恢复、重复run；**Control** baseline -> one change -> restore value/LastSetBy -> baseline reproduction；**Lifetime** cache/history/residency跨run污染。 | 合成纹理黑屏和六条路线共用记录模板。 | 无单一实现符号；受上述工具与owner合同共同约束 | 519-549、564-579 | **Covered**。风险：只恢复数值而不恢复set-by、cache或资源状态不算恢复baseline。 |
| 23-15 | GPU hang必须区分recorded、submitted、marker entered/exited、fence complete与device removed | **Owner** RHI command list、D3D12 queue、GPU、interrupt/crash reporting；**Data** marker-in/out、payload fence、completed value、device-removed reason、DRED/vendor dump；**Control** queue-specific evidence；**Lifetime** payload直到completion才释放。 | GPU hang路线从crash evidence和最后breadcrumb开始，不从屏幕输出开始。 | `Engine/Source/Runtime/D3D12RHI/Private/D3D12Submission.cpp::FD3D12DynamicRHI::ProcessInterruptQueue`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Util.cpp::FD3D12DynamicRHI::DumpActiveBreadcrumbs`; `Engine/Source/Runtime/D3D12RHI/Private/D3D12Util.cpp::FD3D12DynamicRHI::OutputGPUCrashReport` | 341-386、550-562、630-658 | **Covered**。D3D12中`GetCompletedValue()==UINT64_MAX`触发device-removed检查，不能当正常完成。风险：sentinel不可外推所有RHI。 |
| 23-16 | 每条路线必须输出last-valid-state与下一项判别测试 | **Owner**调查记录；**Data**最后确认状态、首个unknown、候选假设、invalidations；**Control**完成词表；**Lifetime**绑定具体artifact与run。 | 六条路线统一收口到Declared/Retained/Recorded/Submitted/Marker/GPU complete/Output verified。 | 上述各层源码锚点 | 550-579、581-706 | **Covered**。风险：不得从marker seen跳到pass complete或output correct。 |

## 5. 残余风险

1. “Capture看到GPU命令”只能解释为捕获边界内的recorded/native command，不得外推原始运行已执行或完成。
2. D3D12的`UINT64_MAX`、DRED和device-removed路径受平台/RHI限制；复现矩阵必须固定RHI。
3. ProfileGPU为了可解释性改变async与parallel translate，不能代替normal-run性能证据。
4. Pass/event字符串适合人工关联，但必须同时记录frame、queue、resource或marker状态。

## 6. 独立 BODY 结论

**BODY PASS**。当前正文完整覆盖症状路由、RDG调试、capture/tool边界、GPU hang完成深度、ODSC shader路径、CVar六段链与受控复现；冻结原版的有效分层价值已迁移，固定checklist与跨工具全局ID等危险表达已被纠正。本结论独立于旧sidecar、章节状态和公共Gate。
