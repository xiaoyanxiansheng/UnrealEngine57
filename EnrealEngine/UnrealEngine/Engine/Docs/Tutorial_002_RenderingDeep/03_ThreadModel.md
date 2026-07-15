# 03 三线程模型与渲染命令

> **源码版本**: UE5.7  
> **前置阅读**: 01（渲染架构总览）；推荐先读 02（从 Component 到 SceneProxy）  
> **当前状态**: ✅ 完成（质量完成 / 复审完成 / Codex 最终完成，2026-06-28）  
> **验证记录**: 见同目录 `03_ThreadModel_CoverageMatrix.md`

---

03 之所以会比 01、02 难，是因为它开始讨论“工作怎样跨线程推进”。01 建立了 Game Thread、Render Thread、RHI、GPU 的责任分层，02 建立了 Component、Proxy、SceneInfo 与 `FScene` 的所有权边界；本章要解释的是：当 GT 上的状态已经变化，而 RT、RHI、GPU 仍在消费旧状态时，新状态怎样沿着这些边界安全前进。

本章先不让这些名字直接承担解释。先把问题放回一个具体场景：

> 场景里已经注册了一个 Primitive。玩家移动它时，Game Thread 立刻知道 Component 的新 Transform；Renderer 侧可能还在使用上一帧稳定的 Proxy、SceneInfo 和 draw 数据；RHI/GPU 甚至可能还在处理更早提交的命令。UE 要做的是：让游戏世界继续往前走，同时让渲染侧在安全时间点收到这次移动，并最终让后续 draw 使用新位置。

这条更新会经历一条状态换手过程。每一步都不是“同一条命令换了一个名字”，而是产物、消费者与完成条件发生了变化：

```text
GT 上的 Component 当前状态
  -> 一份随命令移动的数据快照
  -> 一段等待 RT 上下文执行的 CPU task
  -> RT 拥有的场景更新待办
  -> 帧级渲染中录制出的 RHI command list
  -> submit 把已录制列表交给 RHI 时间线
  -> translate / finalize 形成平台可提交的 command package
  -> Platform Queue Submit 把 package 交给目标 GPU queue
  -> GPU 异步消费；匹配 queue 与最后消费者的 completion evidence 才证明目标使用结束
  -> fence / frame sync 用选定深度的进度点约束各时间线距离
```

这条线贯穿整篇。后文会把 UE 名字逐个贴到这条线上：

```text
Component 当前状态
  -> UpdateParams
  -> ENQUEUE_RENDER_COMMAND(UpdateTransformCommand)
  -> FRenderCommandDispatcher
  -> FRenderCommandList / FRenderThreadCommandPipe / TaskGraph / inline
  -> RenderThread named thread
  -> PrimitiveUpdates
  -> FRHICommandListImmediate
  -> FRHICommandListExecutor::Submit
  -> FFrameEndSync / FRenderCommandFence
```

读这篇时始终问三件事：

```text
当前数据装在哪里?
现在由哪条时间线拥有和修改?
下一步只是交出去，还是对方已经执行完成?
```

如果这三件事清楚，`task`、`pipe`、`fence` 这些名字就会变成路标；如果这三件事不清楚，名字会堆在一起，读者只能靠猜测反推含义。

---

## 先用一条生产者—消费者链放稳新词

本章的调度词都在回答同一组问题：谁生产了一份工作或数据，谁将在何时消费，它等待在哪里，交接动作是否已经等于消费完成。先把这条链放稳，再看 UE 名字：

```text
producer produces work/data
  -> task packages executable CPU work
  -> queue holds work for an identified consumer
  -> pipe batches and forwards render commands to the RT timeline
  -> command list records ordered GPU work without executing it yet
  -> submit transfers recorded work to the RHI/backend timeline
  -> GPU queue is consumed asynchronously by the GPU
  -> fence exposes a progress point that another timeline may wait for
```

这些词不能互换。`task` 回答“哪段 CPU 工作可被调度”，`queue` 回答“它在等谁消费”，`pipe` 回答“一批 render command 怎样进入 RT 时间线”，`command list` 回答“GPU 工作怎样被有序记录”，`submit` 回答“记录结果何时交给下一层”，`fence` 回答“等待方需要确认消费者推进到哪里”。

### Work 与 task：一段可以稍后执行的 CPU 工作

在本章里，先把 **work** 想成“一段 CPU 逻辑”。这段逻辑可以是：

- 在 RT 上把一个 transform update 排进 `PrimitiveUpdates`。
- 在 worker 上录制一段 RHI draw commands。
- 在 RHI 时间线上把一条 command list 翻译到后端 context。
- 在等待链完成后触发一个 fence event。

**task** 是生产者把一段 CPU work 交给调度系统后的载体。消费者可能是某个 named thread，也可能是带特定逻辑标签和所有权的 worker。它通常带着三类信息：

1. 要执行的函数或 lambda。
2. 这段函数需要的输入数据，比如命令捕获的快照。
3. 它依赖谁、完成后通知谁。

UE 需要 task，是因为“生成工作”和“执行工作”经常不在同一个时间点，也不一定在同一个线程。Game Thread 可以在本帧生成一段渲染工作；Render Thread 稍后执行；某些 RHI 翻译工作可以交给高优先级 worker；某些并行 draw 录制任务可以等所有子列表完成后再汇回主列表。

用贯穿案例看会更稳：同一个 Primitive 在一帧里被移动了两次。GT 第一次移动时会捕获一份 transform/bounds 快照，并生成一段“到 RT 上更新这个 Primitive 渲染状态”的 work；第二次移动又捕获另一份快照，并生成另一段 work。进入调度系统后，它们就是两份 task：每份 task 都携带自己那次移动的输入数据、执行函数和依赖关系。

这说明 task 的关键身份是“带着输入数据和依赖关系的 CPU 工作单元”。它可以稍后在 RT 上执行，也可以作为 RHI 时间线上的翻译任务被 worker 承载。调试时要把断点现象转成进度问题：哪一次移动对应的 task 已经被哪个时间线消费了。如果第二次移动的 task 还在等待，画面可能仍然显示第一次移动后的状态。

调试时看到 task，先问它在逻辑时间线里的位置：

```text
这个 task 属于哪条逻辑时间线?
它在等哪个 prerequisite?
它完成后会推进哪个 event 或队列?
```

例如 RHI `Tasks` 模式下，逻辑上仍然是 RHI 时间线的工作，执行载体可以是 TaskGraph worker。即使 threaded rendering 关闭，RHI submit 里的 task 和依赖关系仍然存在，相关任务会被排到 render thread local queue 这类更贴近当前推进路径的位置。

### Queue：面向明确消费者的等待处

**queue** 是 task 或命令在消费者尚未执行前的等待位置。它不只是一个容器；它把工作绑定到一条消费路径。理解 queue 时重点看三件事：

- 谁可以往这里放工作。
- 谁负责消费这里的工作。
- 这里的顺序和依赖怎样保持。

本章会遇到几类 queue。Render Thread 主队列承载普通 render command task；`RenderThread_Local` 是 RT 在等待或嵌套推进时显式使用的本地队列；RHI task pipe 内部也会维护 RHI 时间线上的任务顺序；GPU queue 则是平台后端提交后由 GPU 稍后消费的队列。

UE 需要 queue，是因为多条时间线要并行推进，但共享数据的修改点必须有顺序。GT 可以继续生成命令，RT 按自己的队列消费；RT 可以提交 RHI command list，RHI 时间线按依赖翻译；GPU 可以在更晚时间执行。

把 queue 放进实际场景：一个 worker 正在批量生成 500 个 Primitive 的 transform 更新命令。这 500 段 work 会先交给一个明确的消费方，而共享 Renderer 场景的修改仍然回到拥有者时间线。等待的位置就是 queue 的意义：命令排好顺序，等拥有对应时间线的一方来消费。

因此看 queue 时，第一问题是“这个等待处属于谁”。RT 主队列属于 RT 时间线，里面的 render command 会在 RT 上下文消费；GPU queue 属于平台后端提交后的 GPU 消费路径；`RenderThread_Local` 属于 RT 等待场景中的本地推进路径。归属不同，能证明的进度也不同。

调试时看到“卡住”，queue 的问题通常有三种：

- 工作还没有进入目标 queue。
- 工作在 queue 里，但前置依赖没有完成。
- 消费 queue 的线程或逻辑时间线没有推进。

这比只看某个函数是否调用过更可靠，因为 UE 的很多等待通过队列和事件依赖表达。

### Pipe：批量完成“生产 render command”到“RT 可消费”的交接

**pipe** 在本章里专指 render command 的投递通道。生产者可以连续写入多条要在 RT 上下文执行的 CPU work；pipe 保存这批命令的顺序和批次边界，并在合适时机把它们变成 RT 可消费的工作。写入 pipe 只完成了交接准备，只有 RT 回放对应 work 后，Renderer 状态才真正改变。

UE 需要 pipe，是因为 render command 的来源不只 Game Thread 单点调用。编辑器、异步 worker、批量生成路径、单线程调试、线程化渲染都希望共用 `ENQUEUE_RENDER_COMMAND` 这一类入口。没有 pipe 或类似机制，每条命令都直接争抢 RT 主队列，会让批量生成、运行时模式切换和调试形态都更难控制。

本章会看到两个层次：

- 显式 `FRenderCommandPipe`：某些系统指定的一条 command 通道。
- 主 `FRenderThreadCommandPipe`：普通 render command 的默认入口。

继续用 500 个 transform 更新命令的例子。queue 解决“这些命令在哪里等、由谁按序消费”；pipe 解决“这一批命令怎样被运到 RT 时间线”。如果每个 worker 生成一条命令就立刻争抢 RT 主队列，调度开销和时序观察都会变得碎。pipe 给 UE 一个中间通道：先接住一批 render command，再按当前运行模式启动、回放或转交。

所以 pipe 更像“有策略的运输通道”，queue 更像“有归属的等待处”。调试时这两个问题要分开问：命令有没有进入某个等待队列，是 queue 问题；命令是否仍在 pipe 里等待批量启动或回放，是 pipe 问题。

pipe 的调试含义很直接：`ENQUEUE_RENDER_COMMAND` 调用发生了，只说明命令交给了 render command 系统；它可能先进入 pipe，稍后再回放到 RT；也可能因为运行模式被校验后退化，直接走 TaskGraph 或 inline。`r.RenderCommandPipeMode` 的源码默认值只是期望模式，运行时还会根据 RHI bypass、threaded rendering、平台能力、移动平台和能否渲染等条件收缩。

移动椅子的 transform 命令经过 pipe 时，要分别确认四个状态，不能只问“有没有 enqueue”：

| Pipe 状态 | 产品在哪里 | 能证明 | 不能证明 |
| --- | --- | --- | --- |
| Recording | lambda 已写入当前 pipe 批次 | 生产者已提供稳定快照并记录 work | 批次已提交、RT 已可见 |
| Submit | 当前记录批次封口并交给 pipe 调度 | pipe 接受了完整批次 | 对应 launch/replay 已执行 |
| Launch / replay | 批次被启动，或在目标路径上回放成 RT 可消费 work | 命令已进入目标 RT 消费路径 | 椅子 transform lambda 已完成、Scene 已发布 |
| Completion event | pipe 批次关联的 CPU completion event 满足 | 该批 pipe work 的指定 CPU 消费范围已结束；若覆盖 lambda，可作为第 2 级 RT consumed 证据 | 第 3 级 Scene published、第 5 级 Platform submitted 或第 6 级 GPU consumed |

因此移动物体不更新时，既可能卡在 recording 后尚未 submit，也可能 submit 后尚未 launch/replay，还可能 RT 已回放但 completion event 的消费者范围并不覆盖后续 Scene publication。Pipe event 是 CPU 交接证据，不应升级解释成 GPU fence。

### Command list：把“待执行 CPU work”与“已记录 GPU work”分层

本章会同时出现两个名字很像的列表：

| 名字 | 装的内容 | 所属层级 | 什么时候有用 |
| --- | --- | --- | --- |
| `FRenderCommandList` | 等待 RT 上下文执行的 render command lambda | RenderCore / render command 层 | worker 或系统批量记录“稍后在 RT 上执行的 CPU 工作” |
| `FRHICommandList` / `FRHICommandListImmediate` | Draw、Dispatch、Transition、SetPipelineState 等 RHI 操作 | RHI / GPU work 层 | RT 或 worker 已经决定要画什么，开始记录平台无关 GPU 命令 |

这两层都叫 command list，是因为它们都在“先记录、后交给下一位消费者”。但两层的产物、消费者与完成条件完全不同。

`FRenderCommandList` 记录的是 CPU 逻辑。比如“在 RT 上把这个 Primitive 的 transform update 排入 Renderer 场景更新队列”。这一步还没有产生 draw，也没有交给 GPU。

`FRHICommandList` 记录的是 RHI 操作。比如 draw、dispatch、resource transition、pipeline state 设置。它已经属于后续 RHI submit 链会处理的 GPU 工作描述。

UE 需要分两层，是因为 Renderer 侧状态更新和 GPU 命令录制发生在不同阶段。移动 Primitive 时，RT 先要安全地更新场景结构；只有到了帧级渲染、可见性和 pass 组织完成之后，才会把对应 draw 录进 RHI command list。

一个更具体的案例：场景里有一把椅子。玩家把椅子往右移动，第一层 `FRenderCommandList` 或 render command task 记录的是“稍后在 RT 上把椅子的 transform 更新交给 Renderer 场景”。这一步的结果是 Renderer 侧知道椅子的位置变了，并把相关场景状态安排到安全点吸收。

真正“画椅子”发生在后面的帧级渲染阶段。可见性判断、pass 组织和 draw 数据准备完成后，第二层 `FRHICommandList` 才会记录“在 BasePass 或其他 pass 中 draw 这把椅子需要的 RHI 操作”。因此第一层 render command 执行完，只能证明椅子的渲染状态更新进入了 RT 流程；第二层 command list 才是把 draw/dispatch/transition 这类 GPU 工作描述交给 RHI submit 链。

调试时这一点非常关键：第一层列表里的 render command 执行完，只能说明 RT 已经执行了这段 CPU lambda；第二层 RHI command list submit 之后，也通常只能说明命令交给了 RHI/平台后端时间线。GPU 是否已经执行，要继续看 GPU fence、present 或后端同步点。

两层列表也各有一个叫 `Submit()` 的动作，但它们不是同一深度：

| 动作 | 交出的产品 | 下一消费者 | 能证明 | 不能证明 |
| --- | --- | --- | --- | --- |
| `FRenderCommandList::Submit()` | 一批 render-command lambda | RT 时间线 | 这些 CPU work 已按列表顺序进入 RT 可消费路径 | lambda 已执行、Scene 已发布、RHI 命令已录制 |
| `FRHICommandListExecutor::Submit()` | 一批已封口或待汇合的 RHI command lists | RHI/backend 时间线 | command-list 控制权和依赖进入 dispatch/translate/formation/submit 链 | 平台 queue 已完成、GPU 已越过最后消费者 |

所以看到“Submit 完成”时，第一步不是问“GPU 画完了吗”，而是先确认调用的是哪一层 Submit，以及它把什么产品交给了谁。

### Submit：转移 command list 的控制权，不承诺 GPU 已完成

**submit** 是生产者把 RHI command list 从“仍可由 RT/worker 写入的记录状态”交给 RHI 时间线的控制权边界。交接后，RHI/backend 成为下一位消费者，负责翻译、收束并提交；GPU 再异步消费后端命令。因此 Submit 推进了列表的所有权和顺序，却不承诺 GPU 已完成。

UE 需要 submit 阶段，是因为 RHI command list 可能不止一条。主 immediate list 会有当前帧的主体工作；RDG、MeshDrawCommand 或 parallel pass 可能生成 additional command lists；有些子列表还在等 worker 完成。RHI 不能在这些列表顺序和依赖还没收束时直接交给后端。

全章只使用下面这一张**六级完成证据梯子**。它从 GT 把工作交给 RT 开始，一直追踪到 GPU 最后消费；完成较浅一级绝不能反推较深一级：

| 级别 | 状态成立 | 最小证据 | 能证明 | 不能证明 |
| --- | --- | --- | --- | --- |
| 1. RT queued | render command 已进入 RT 可消费路径 | TLS/list Submit、pipe launch/replay 或目标 render task 已入队 | RT 已能看到完整 CPU work | RT 已执行 lambda |
| 2. RT consumed | RT 已执行对应 render command | RT scope/task/completion event 越过该 lambda | Renderer owner 已消费更新意图 | Scene 当前版本已经发布变化 |
| 3. Scene published | Renderer 场景更新在阶段门处成为后续帧任务可查询状态 | `FScene::Update` 或对应 Scene publication 点已吸收更新 | 后续 visibility/pass 可以查询新状态 | draw 已录入 RHI command list |
| 4. RHI recorded | 当前帧目标 draw/dispatch/transition 已形成平台无关 RHI 工作 | 目标 RHI list 中存在工作并完成 CPU 录制封口 | GPU 工作意图已经记录，能够进入 RHI/backend 消费链 | 平台 queue 已接管 |
| 5. Platform submitted | RHI/backend 已把对应平台工作提交到目标 queue | 后端提交记录或 queue submit completion | 平台 queue 已接管指定工作 | GPU 已越过最后读取 |
| 6. GPU consumed | GPU 已越过该资源或 draw 的最后消费者 | 匹配 queue 与最后消费者的 GPU fence、completion value 或 readback 合同 | 对应 GPU 使用结束，可执行证据覆盖的复用或退休 | 其他 queue、显示扫描或全设备工作全部完成 |

第 4 级到第 5/6 级内部仍包含 RHI/backend 的局部子阶段：

```text
record sealed
  -> dispatch ready
  -> translate complete
  -> platform command formed
  -> queue submitted
  -> GPU completion evidence
```

这些名称只解释 RHI/backend 怎样把“RHI recorded”推进为“Platform submitted”，以及第 6 级证据怎样成立；它们不再作为全章的第二套完成等级。

举一个最小场景：BasePass 的主体工作在 immediate list 里，两个 worker 又各自录了一条 additional command list。submit 要把这三条列表视为一批有依赖关系的输入：immediate list 提供主线命令，两个 additional list 提供并行录制出来的 draw 工作，后端需要看到完成且有序的一批命令。于是 dispatch 先确认三条列表的完成状态，translate 把它们 replay 到具体 RHI context，finalize 把并行结果收束成可提交形态，submit 再把后端可消费的命令交给平台/GPU queue。

这个 BasePass 案例只展开第 4→5/6 级内部：列表封口、dispatch、translate 与 platform formation 都是把第 4 级 RHI recorded 推向第 5 级 Platform submitted 的内部步骤；GPU completion evidence 满足后才进入第 6 级 GPU consumed。

### Fence：消费者进度对等待方可见的刻度

**fence** 的心智模型是“消费者进度刻度 + 可等待事件”。资源所有者或上游生产者先在目标时间线上插入进度点；真正需要复用、销毁数据或限制帧距时，等待方再确认消费者是否越过该点。等待完成只证明指定时间线到达了指定位置，资源是否安全取决于这个位置覆盖了 RT、RHI、GPU/present 中的哪一层。

UE 需要 fence，是因为多条时间线允许重叠。GT 可以领先 RT，RT 可以领先或等待 RHI，GPU 可以稍后执行已经提交的工作。完全不等待会让资源生命周期、输入延迟和内存堆积失控；每一步都等待又会把流水线并行性打掉。Fence 让 UE 能在必要位置等待到合适深度。

本章会遇到三种等待深度：

- `RenderThread`：等 RT 执行到某个 render command 点。
- `RHIThread`：等 RT 到点，并让当前 RHI 提交链也越过相应 CPU 安全点；GPU 完成要看更深的 GPU/present 同步点。
- `Swapchain`：等到更接近 present/flip 的显示端事件。

用资源生命周期看 fence 最容易理解。假设 GT 侧准备销毁一个材质或 mesh 相关资源，而此前已经向渲染侧发过使用它的命令。发起方需要知道“相关时间线是否已经走过不再使用这份数据的点”。如果只关心 render command 是否被 RT 消费，`RenderThread` 深度可以表达这件事；如果还要让当前 RHI 提交链越过 CPU 安全边界，就需要 `RHIThread` 深度；如果问题是显示端排队和 present 延迟，就要看 `Swapchain` 深度。

这个案例也说明 fence 的边界：等待到 `RHIThread` 深度能证明 CPU 侧 RHI 提交链走到了相应点，但它不自动等价于 GPU 已经执行完所有已提交命令。释放资源、排查崩溃或定位显示延迟时，要先说清楚自己需要等待哪条时间线，以及为什么需要这个深度。

调试时 fence 的核心问题是：

```text
这次等待到底要等到哪条时间线?
等待深度是否足够保证资源安全?
等待是否过深，导致 GT 帧末卡住?
```

---

## 本篇边界

本章负责讲清楚一条渲染工作怎样跨过 GT、RT、RHI、GPU 这些时间线，并在每次交接时保持所有权清晰。它会讲透四道门：

| 门 | 本章要讲清楚什么 | 会出现的 UE 名字 |
| --- | --- | --- |
| GT -> render command | GT 为什么先冻结快照，再把 CPU work 交给渲染线程上下文 | `UpdateParams`、`ENQUEUE_RENDER_COMMAND`、`UpdateTransformCommand` |
| render command -> RT | 同一个入口为什么可能先进入 TLS command list、pipe、TaskGraph，或 inline 执行 | `FRenderCommandDispatcher`、`FRenderCommandList`、`FRenderThreadCommandPipe` |
| RT -> RHI 时间线 | RT 执行 CPU lambda 后，什么时候只是更新 Renderer 状态，什么时候开始录 RHI command list，Submit 又怎样推进 | `PrimitiveUpdates`、`FRHICommandListImmediate`、`FRHICommandListExecutor::Submit` |
| 时间线收束 | GT、RT、RHI、GPU/present 怎样保持有限距离，什么时候用 fence，什么时候用 flush | `FFrameEndSync`、`FRenderCommandFence`、`FlushRenderingCommands` |

本章只点到以下内容，不展开成第二主线：

- 02 已经讲过 Component、Proxy、SceneInfo、FScene 的完整生命周期。本章只取“移动 Primitive”作为跨线程命令案例。
- 04 会深入 RHI 资源、PSO、shader binding、barrier 和平台后端。本章只讲 RHI command list 和 submit 边界。
- 05 会深入 RDG pass dependency、parallel execute、async compute 和 transient resource。本章只讲 RDG 可能产生 RHI 子列表并汇入 submit。
- 07 会深入 MeshDrawCommand 排序、合批、Instance Culling 和 draw 数据缓存。本章只讲它会把 pass 内 draw 录到 RHI command list。
- 08 会深入一帧可见性和 `FScene::Update` 周围的帧初边界。本章只讲 transform update 在 RT 侧被排入并吸收。

---

## 本篇必须能回答

读完本篇，你应该能回答：

- `ENQUEUE_RENDER_COMMAND` 生成了什么工作？它保证异步执行吗？
- `task`、`queue`、`pipe`、`command list`、`fence` 分别解决什么问题？
- 同一个 render command 为什么有时 inline，有时进 TaskGraph，有时进 command pipe？
- `FRenderCommandList` 和 `FRHICommandList` 为什么都叫 command list，它们的内容有什么根本差异？
- Render Thread 为什么要作为 TaskGraph named thread 存在？`RenderThread_Local` 在等待场景里承担什么角色？
- `RHIThread` 一定是独立 OS 线程吗？`None`、`DedicatedThread`、`Tasks` 三种模式为什么都能表达 RHI 时间线？
- `Submit` 的 dispatch、translate、finalize、submit 为什么要分成几个阶段？
- fence 等的是 RT、RHIThread 还是 swapchain？等待深度怎样影响资源安全和卡顿？
- 多线程并行录制时，UE 怎样避免多个线程同时改坏同一份 Renderer 状态？
- 物体移动了但画面没动，应该沿哪条状态线排查？

---

## 为什么 UE 要拆成多条时间线

UE 常说 Game Thread、Render Thread、RHI Thread。更准确的读法是：UE 把一帧渲染拆成几条有所有权边界的执行时间线。某些时间线可能落到独立 OS 线程上，某些时间线可能由 TaskGraph worker 承载，某些模式下会更贴近当前线程执行。名字叫“线程”，但调试时要先看逻辑职责。

### Game Thread：拥有可变游戏世界

Game Thread 拥有 Actor、Component、Viewport、Camera、ViewFamily 等游戏侧状态。玩家移动物体时，`UPrimitiveComponent` 的 Transform 首先在这里变化。

GT 的特点是“状态活、变化频繁”。这正是它不能直接成为 Renderer 热路径数据的原因。RT 可能正在使用上一帧稳定的 Proxy 和 SceneInfo；GPU 可能还在执行更早提交的后端命令。如果 GT 直接改 RT 正在读的数据，锁会非常重，生命周期也会变得难以判断。

因此 GT 要影响渲染侧时，通常会先生成快照或命令参数，再把工作交给 render command 系统。

### Render Thread：拥有 Renderer 场景和帧级调度

Render Thread 拥有 Renderer 侧的 `FScene`、Proxy、SceneInfo、cached draw command、可见性结果、pass 组织和 RHI command list 录制入口。

RT 的特点是“渲染状态要稳定”。它可以在安全点吸收 GT 发来的变化，然后基于稳定场景组织一帧的可见性、pass、draw、RHI 命令。RT 也可以调度 worker 并行录制 RHI 子列表，但共享 Renderer 状态的修改点仍然必须受 RT 时间线控制。

### RHI 时间线：拥有平台无关命令到后端提交的边界

RT 录制完 RHI command list 后，还没有完成所有 CPU 工作。这些平台无关的 RHI 命令需要被 replay 到具体后端 context，后端需要处理状态、资源、命令列表和提交。

UE 把这段工作抽成 RHI 时间线，原因有三点：

1. **顺序**：RHI 命令必须按依赖顺序翻译和提交。
2. **所有权**：执行后端 API 的代码必须持有 RHI ownership，不能和其他线程随意同时进入同一个后端 context。
3. **等待点**：GT/RT 需要能在 fence、flush、frame sync 处等待 RHI 时间线走到某个进度点。

`None`、`DedicatedThread`、`Tasks` 是同一条逻辑 RHI 时间线的三种执行载体：

| 模式 | 执行载体 | 为什么存在 | 调试含义 |
| --- | --- | --- | --- |
| `None` | RHI 工作不放到独立 RHI 执行载体上，更贴近 RT 或 local queue 推进 | 适合不需要或不能使用 threaded RHI 的运行形态 | RHI 提交成本可能表现为 RT 上的等待或 local 推进 |
| `DedicatedThread` | 独立 `FRHIThread` OS 线程 | 适合把后端提交成本从 RT 上拆出去 | 重点看独立 RHIThread 的任务、等待和 submit |
| `Tasks` | 高优先级 TaskGraph worker 执行逻辑 RHI 工作 | 保留 RHI 逻辑时间线和 ownership，同时避免固定 OS 线程 | 瓶颈可能显示在 TaskGraph worker，而非独立 RHIThread |

`ENamedThreads::RHIThread` 表达的是逻辑归属：这段 work 要按 RHI 时间线规则执行。它不要求运行时一定存在一个独立 OS 线程；在某些运行形态下，相同的依赖链会由 worker 或 local queue 推进。

### GPU / present：消费已经提交的后端命令

GPU 在更晚的时间消费平台后端提交的 command lists。CPU 侧 submit 完成后，GPU 通常还没有执行完这批工作。present/flip 也可能引入显示端排队。

这解释了为什么本章反复区分：

```text
RT 执行完 render command
RHI command list 被 submit
GPU 真正执行完
present/flip 到达显示端
```

这些点在不同时间线上。调试时说“命令完成了”，必须说明完成到哪一层。

> Unity 对照：Unity 用户通常把 `transform.position` 改动和后续渲染同步看成引擎内部完成的事情。UE 把 GT、RT、RHI、GPU 的边界暴露得更清楚，是为了让它们并行流水。读 UE 源码时也因此需要自己判断数据现在属于哪条时间线。

---

## 第一步：GT 只打包一份命令快照

玩家移动 Primitive 时，GT 看到的是 Component 的当前状态。它可以读取新的 transform、bounds、attachment root、previous transform 等信息，并把这些信息打包成一次更新需要的参数。本文用 `UpdateParams` 指代这份参数快照。

这份快照的意义是：RT 以后执行命令时，不需要回头读取 live Component。它拿到的是命令携带的一份稳定数据。

为什么要这样做？

GT 上的 Component 会继续变化。玩家下一帧可能又移动它，组件也可能被重新注册、销毁、换材质或换 mesh。RT 如果在稍后执行命令时回读 live Component，就会遇到两个问题：

1. 读到的可能已经变成发命令之后的状态。
2. UObject / Component 的生命周期和线程访问规则不适合被 RT 热路径随意读取。

因此 GT 的职责是“摘取这次更新需要的渲染信息”，然后把这份数据随命令交出去。后续 RT 是否已经执行、RHI 是否已经 submit、GPU 是否已经看到新位置，都不影响这份快照的含义。

在贯穿案例里，这份工作会被包装成：

```text
UpdateTransformCommand:
  输入：GT 捕获的 transform/bounds/previous transform 等更新参数
  执行位置：渲染线程上下文
  直接结果：RT 侧收到一次 Primitive transform update
  后续结果：Renderer 在安全点吸收变化，后续 draw 使用新状态
```

这里出现的 `ENQUEUE_RENDER_COMMAND(UpdateTransformCommand)` 可以理解成“创建一段需要在渲染线程上下文执行的 CPU work”。它还没有画任何东西，也没有让 GPU 看到新位置。

---

## 第二步：ENQUEUE 创建 render command

`ENQUEUE_RENDER_COMMAND` 做的核心事情是：把一段 C++ lambda 包装成 render command。lambda 可以理解成“带着捕获数据一起移动的小函数”。它捕获 `UpdateParams`，以后在 RT 上下文执行。

这段 render command 有三个特征：

- **输入稳定**：它携带 GT 当时捕获的快照。
- **执行位置受约束**：它要按渲染线程上下文语义运行。
- **执行时间不固定**：它可能稍后进入 RT，也可能在特定模式下 inline 执行。

UE 需要这样的入口，是因为 GT 不能直接写 RT 拥有的 Renderer 数据；同时 RT 也不能随时回读 GT 的可变 UObject。`ENQUEUE_RENDER_COMMAND` 把“我要影响渲染侧”变成一段可移动、可排队、可等待的 CPU work。

这一步得到本章第一个关键判断：

```text
ENQUEUE_RENDER_COMMAND 保证的是渲染线程上下文语义。
它的运行形态由 Dispatcher 和当前运行模式决定。
```

这句话会在下一节展开。先记住：`ENQUEUE_RENDER_COMMAND` 之后，命令进入了 render command 系统；它是否立刻执行，要看 Dispatcher 怎样选择入口。

---

## 第三步：Dispatcher 选择命令怎样进入 RT 时间线

GT 已经创建了一段 render command。接下来要决定这段 work 先放在哪里。

UE 需要多种入口路径，原因很具体：

- 单线程调试希望命令可以 inline，方便复现和降低调度成本。
- 线程化渲染希望命令进入 RT 时间线，保持 GT/RT 并行。
- worker 批量生成 render command 时，希望先记录到本线程局部列表，最后一次性提交。
- RenderCommandPipe 模式希望把大量 render command 通过 pipe 批量启动和回放。
- 某些系统希望使用显式 pipe 管理自己的一组命令。

`FRenderCommandDispatcher` 的职责就是做入口选择。它不决定渲染算法本身，只决定这段 render command 先落到哪个载体。

### 先看 TLS 的 FRenderCommandList

Dispatcher 的第一层判断是：当前线程是否正在用 TLS 的 `FRenderCommandList` 录制 render command。

TLS 是 thread-local storage，可以理解成“当前线程自己的临时口袋”。如果某个 worker 正在批量生成 render command，它不一定希望每生成一条就投递到 RT 主队列。它可以先把这些命令记到本线程的 `FRenderCommandList`，等到明确提交点再统一交出去。

`FRenderCommandList` 的输入是一批 render command lambda；输出是在 `Submit` 时把这些命令补进父 command list 或全局 pipe。它解决的问题是“命令生成可以并行，插入 RT 时间线要有明确时刻”。

```text
当前线程有 TLS FRenderCommandList?
  -> 有：render command 先写入本地列表
  -> 没有：继续看显式 pipe 和主 pipe
```

调试时，如果你看到 `ENQUEUE_RENDER_COMMAND` 调用了，但 RT 还没有执行对应 lambda，要检查命令是否暂存在 TLS `FRenderCommandList` 里，等待后续 `Submit`。

### 再看显式 pipe

如果当前没有 TLS command list，Dispatcher 会看是否有显式 `FRenderCommandPipe`。显式 pipe 是某个系统主动指定的 render command 通道。它适合一组命令需要由同一条 pipe 管理的场景。

显式 pipe 只有在运行时 pipe 模式允许并且目标 pipe 接受命令时才会接手。否则命令继续回落到主 `FRenderThreadCommandPipe`。

这里的关键是“运行时生效模式”。`r.RenderCommandPipeMode` 的默认值可以表达期望，但 UE5.7 会在运行时根据 RHI command list bypass、平台能力、threaded rendering 状态、移动平台和当前环境能否渲染等条件校验并收缩。写调试结论时要看校验后的模式。

### 最后进入主 FRenderThreadCommandPipe

主 `FRenderThreadCommandPipe` 是大多数普通 render command 的默认入口。它拿到命令后，再根据当前线程和 threaded rendering 条件决定执行形态：

```text
需要使用 Render Thread，并且当前不在 RT 上?
  -> 进入 RT 时间线
       RenderCommandPipe 启用：进入 pipe，等待批量启动/回放
       RenderCommandPipe 未启用：创建 TaskGraph render command task

当前已经在 RT 上，或运行模式不使用 RT?
  -> inline 调用 lambda
```

这解释了为什么同一段代码在不同运行形态下表现不同：

- 单线程或某些调试模式下，lambda 可能立即执行。
- 线程化渲染下，lambda 通常会变成 RT 队列上的 task，稍后执行。
- pipe 模式下，lambda 可能先进入 pipe，由 pipe 批量启动或回放。
- worker 批录路径下，lambda 可能先在本线程 `FRenderCommandList` 里等待 `Submit`。

把同一条 `ENQUEUE_RENDER_COMMAND(UpdateTransformCommand)` 放到三个上下文里看：

| 上下文 | Dispatcher 可能选择的载体 | 调试时会看到什么 |
| --- | --- | --- |
| 单线程或调试路径 | 当前调用点 inline 执行 | 断点像同步调用一样一路走完，调用后状态可能已经更新 |
| 常规线程化渲染 | TaskGraph render command task 或主 pipe | GT 调用点先返回，RT 稍后在自己的时间线上执行 lambda |
| worker 批量生成命令 | TLS `FRenderCommandList` | `ENQUEUE` 已发生，但命令先留在 worker 的本地列表，等列表 `Submit` 后才进入后续载体 |

这个表给出的判断顺序是：先看当前是否在批录，再看是否有显式 pipe，最后看主 pipe 如何根据运行模式落到 inline、TaskGraph 或 pipe 回放。

因此调试 `ENQUEUE_RENDER_COMMAND` 时要问得更具体：

```text
这条命令当前被 Dispatcher 放进了哪个载体?
这个载体什么时候会插入 RT 时间线?
如果 inline 执行，调用点是否已经处在允许的渲染线程上下文?
```

很多线程化 bug 来自同一个根因：代码在单线程调试下依赖了 inline 结果，到了线程化运行中，命令变成稍后执行的 task 或 pipe 回放，调用点立刻读取结果就读到了旧状态。

---

## 第四步：Render Thread 作为 TaskGraph named thread 消费命令

命令进入 RT 时间线后，需要有一个稳定的消费方。UE 让 Render Thread 成为 TaskGraph 里的 named thread。

先解释 TaskGraph。TaskGraph 是 UE 的 CPU 任务调度系统。它可以表达“这段 task 属于哪个逻辑执行上下文”“它依赖哪些前置事件”“完成后触发哪些后续事件”。**named thread 首先是调度身份和顺序域，不应先理解成一根永远固定的物理线程。** 这个身份告诉生产者：工作必须以谁的上下文、顺序和所有权规则被消费；运行模式再决定它由专用 OS 线程、当前线程的 local queue，还是带相应 tag/ownership 的 worker 承载。

GameThread、ActualRenderingThread、RenderThread、RHIThread 等名字因此有两层信息：逻辑上，它们界定谁可以安全读写对应状态；实现上，它们帮助 TaskGraph 选择执行载体和队列。调试时先确认 task 的逻辑归属，再确认当前模式把这个身份映射到了哪里。否则看到 worker 执行 RHI task，容易误判 RHI 所有权已经消失；看到单线程模式 inline 执行 render command，也容易误判 GT 可以直接拥有 Renderer 状态。

RT 需要成为 TaskGraph named thread，原因有三个：

1. 普通 render command task 要按 RT 主队列顺序执行。
2. fence 和等待链要能把“RT 已经走到这里”表达成 TaskGraph event。
3. RT 在等待 parallel/RHI task 时，需要有规定的方式推进 local queue，避免依赖饿死。

线程化渲染启动后，UE 会创建承担 RT 语义的运行体，名字是 `FRenderingThread`。它绑定到 TaskGraph 的实际渲染线程身份，并设置两个重要队列身份：

| 名字 | 心智模型 | 主要用途 |
| --- | --- | --- |
| `RenderThread` | RT 主队列 | 普通 render command、pipe 回放、fence 插入的命令最终在这里推进 |
| `RenderThread_Local` | RT 等待场景里的本地队列 | RT 等待 parallel/RHI task 时显式泵动，防止某些依赖任务饿死 |

`RenderThread_Local` 的常见意义是等待期间的本地推进队列：RT 正在等待某些 task 完成，而这些 task 的完成又依赖 RT 本地队列里的小工作；此时等待逻辑可以显式处理 `RenderThread_Local`，让依赖链继续往前走。它服务的是等待和嵌套推进场景，不承担 RT 主队列那种持续消费职责。

用并行录制场景看：RT 发起几个 worker 去录制 RHI 子列表，然后等待这些子列表完成。等待期间，某个完成事件可能需要 RT 本地队列里的一小段 prerequisite 工作先跑完。如果 RT 只是原地阻塞，这条依赖链就可能停住；`RenderThread_Local` 让等待逻辑能在这个空档推进本地 prerequisite，让等待链继续往前走。

因此 `_Local` 的正面角色是“等待期间的局部推进器”。它帮助 RT 在被 parallel/RHI task 挡住时继续处理与当前等待相关的本地小工作。调试卡顿时，如果 RT 主队列没有继续消费，要区分它是在正常等待外部任务，还是等待路径里还需要 local queue 推进某个 prerequisite。

GT 等待 task 时也可能条件性地帮助推进可执行 task，但它与 `RenderThread_Local` 不是同一机制：

| 等待形态 | 谁在等待 | 可以条件性推进什么 | 主要风险 |
| --- | --- | --- | --- |
| GT task pumping | Game Thread 等待目标 event/task | 仅在等待实现、task 属性和线程策略允许时，执行当前线程可合法帮助的 task | 被泵动任务可能回调游戏逻辑或触发本以为“等待后才发生”的工作，产生重入和锁顺序风险 |
| RT local queue | Render Thread 等待 parallel/RHI prerequisite | 显式处理 `RenderThread_Local` 中与 RT 等待链相关的本地工作 | 若把主队列工作错误依赖到 local queue，可能形成饥饿或嵌套等待 |

所以“GT 正在 Wait”不总等于 OS 线程完全休眠，也不表示所有 task 都允许在 GT 上执行。正确判断要同时看等待谓词、目标 event、task 的线程/优先级约束以及当前等待实现是否允许 pumping。等待期间执行了额外 task 时，代码必须能承受重入：不能持有一个锁再泵动可能反向请求该锁的任务，也不能假设回调只会在 Wait 返回后发生。

相对地，`RenderThread_Local` 是 RT named-thread 模型中的显式本地等待队列。它解决的是 RT 自己的依赖推进，不是让 GT 帮 RT 任意执行 render command。调试死锁时应分别画出 GT wait 的可泵任务集合与 RT local queue 的 prerequisite，检查是否存在相互等待或重入锁环。

可以把 RT 主体想成下面的状态：

```text
RT 启动完成
  -> 获得 TaskGraph named thread 身份
  -> 持续处理 RenderThread 主队列
  -> 在特定等待路径里显式处理 RenderThread_Local
```

在贯穿案例里，如果 `UpdateTransformCommand` 被投递到 RT，它最终会在 RT 主队列上执行 lambda。调试时可以按这条线检查：

```text
命令是否已经离开 Dispatcher 的入口载体?
RT 主队列是否正在推进?
lambda 是否执行到 UpdatePrimitiveTransform_RenderThread?
RT 是否因为等待 RHI/parallel task，需要关注 RenderThread_Local?
```

这一步完成后，GT -> RT 的交接才算真正发生。接下来要看 lambda 在 RT 上做了什么。

---

## 第五步：RT 执行 lambda，先更新 Renderer 侧待办

`UpdateTransformCommand` 的 lambda 在 RT 上执行时，直接目标是把这次 transform 变化交给 Renderer 侧的场景更新系统。UE5.7 里这条路会把更新排进 `PrimitiveUpdates`，后续由 `FScene::Update` 在安全点统一吸收。

为什么 RT 收到命令后不立刻到处改？

Renderer 的场景结构不只一个矩阵字段。一个 Primitive 的 transform 变化会影响 bounds、octree、packed arrays、velocity、cached draw 相关状态、GPUScene 上传输入、可见性判断等多个系统。RT 可以拥有这些数据，但它仍然需要在一个一致的提交点更新它们。

`PrimitiveUpdates` 的角色就是收集这些“场景变化待办”。它让 RT 在命令到达时先记录变化，再在 `FScene::Update` 这样的安全点统一 drain。

这一步的状态变化是：

```text
RT 执行 render command lambda
  -> 读取命令快照里的 transform/bounds 等参数
  -> 创建或填充 Renderer 侧 transform update
  -> 排入 PrimitiveUpdates
  -> 等待 FScene::Update 在安全点吸收
```

完成这一步后，只能说明 Renderer 侧已经收到“这个 Primitive 需要更新 transform”的待办。它还不表示：

- 该 Primitive 已经被当前 View 判定可见。
- 后续 pass 已经录制对应 draw。
- RHI command list 已经 submit。
- GPU 已经执行新位置的 draw。

这正是本章要反复强调的分层完成语义：

| 说法 | 实际含义 |
| --- | --- |
| render command 执行完 | RT 已执行这段 CPU lambda |
| `PrimitiveUpdates` 被 drain | Renderer 场景结构已经在安全点吸收变化 |
| pass 录制完 | RHI command list 中有相关 draw/dispatch/transition |
| Submit 完成 | RHI 时间线已接管 command list 翻译/提交 |
| GPU 完成 | 后端命令真正执行完，需要看 GPU/present/fence 相关点 |

如果物体移动后画面没动，而断点显示 `UpdatePrimitiveTransform_RenderThread` 已执行，下一步就要看 `FScene::Update` 是否在绘制前吸收变化、可见性是否覆盖该 Primitive、对应 pass 是否录制新 draw、RHI/GPU 时间线是否推进。

---

## 第六步：RT 开始录制 RHI command list

移动 Primitive 的 render command 只更新 Renderer 场景待办。真正的一帧渲染会更长：Engine 把 ViewFamily 交给 Renderer 后，RT 会组织可见性、pass、RDG、MeshDrawCommand，并把最终 GPU 工作写进 RHI command list。

这里进入第二层 command list：`FRHICommandListImmediate` 和其他 `FRHICommandList`。

RHI command list 的输入是 Renderer 已经决定好的渲染工作，例如：

- 哪些 mesh 在某个 pass 中可见。
- 某个 pass 要绑定哪些 render targets / textures / buffers。
- 需要执行哪些 draw、dispatch、transition。
- 当前要设置哪个 pipeline state、shader 参数和资源状态。

RHI command list 的输出是一条平台无关的命令记录。它等后续 Submit 阶段交给 RHI 时间线翻译到具体后端，GPU 结果会在更晚时间出现。

RT 需要 RHI command list，是因为 Renderer 不应该直接写 D3D12/Vulkan/Metal 后端命令。Renderer 表达的是“我要画这些东西、做这些资源转换”；RHI 负责把这些平台无关操作映射到具体 API。

可以把这一步的状态流写成：

```text
Renderer 侧场景和本帧 View 已经准备好
  -> 可见性和 pass 决策生成一批要画的工作
  -> RDG / MeshDrawCommand / pass 代码调用 RHI command list API
  -> FRHICommandListImmediate 记录 Draw / Dispatch / Transition / state
  -> Submit 把 command list 交给 RHI 时间线
```

这一步是 03 和 04 的边界。03 只需要读者理解“RHI command list 是 RT 交给 RHI 时间线的 GPU 工作记录”；04 会展开里面的 resource、PSO、shader binding、barrier 和后端抽象。

---

## 第七步：Submit 把 command lists 交给 RHI 时间线

RT 或 worker 录好的 RHI command lists 需要通过 `FRHICommandListExecutor::Submit` 进入 RHI 时间线。Submit 的职责是组织这批已经录好的 RHI 命令怎样被翻译和提交。

### 为什么 Submit 需要多阶段

一个渲染帧中的 command list 可能来自多个地方：

- 主 `FRHICommandListImmediate` 记录帧级主线工作。
- RDG parallel pass 可能产生 additional command lists。
- Mesh draw 并行录制可能让多个 worker 各自录一段 draw。
- 某些列表还带着前置 task，需要等录制完成。

Submit 要做的是把这些列表收束成一条有依赖的提交链。列表上的 `FinishRecording()` 只表示 CPU 录制已经封口：生产者不应再向该列表追加命令。它不表示 translate 已完成，更不表示平台 queue 或 GPU 已经看到这份工作。

完整状态必须拆成五层：

```text
1. dispatch
   任务调度层先确认哪些 command list 已经完成录制。
   没完成的列表要继续等；完成的列表可以进入翻译阶段。

2. translate
   把平台无关 RHI 命令 replay 到具体 RHI context。
   如果条件允许，多条 command list 可以并行 translate。

3. platform command formation
   把并行 translate 的结果收束并 finalize 成后端可提交的平台 command list / encoder work。
   这个阶段建立原生工作包和最后提交所需的顺序，但 queue 还未必接管。

4. queue submit
   把后端可消费的 command lists 交给平台 queue。
   此时能证明 queue 已接管相应工作，不能证明 GPU 已执行完。

5. GPU complete
   由与最后消费者匹配的 GPU fence、completion value 或 readback 合同证明指定工作已经越过完成点。
```

UE 这样拆，是为了同时满足并行、顺序和完成性判断。录制可以并行，translate 可以并行，平台命令形成和 queue submit 必须按依赖汇合，而 GPU complete 不能从 CPU task 完成反推。

仍然用 BasePass 的例子：主 immediate list 里已经记录了本帧主线命令，两个 worker 分别录了“场景左半部分”和“场景右半部分”的 draw 子列表。`dispatch` 先确认左右列表已经 `FinishRecording()`，避免消费未封口的产品；`translate` 把三条平台无关 RHI list replay 到后端 context；platform formation 把结果 finalize 成可提交工作包；queue submit 才把它们交给平台队列。最后还要由 GPU completion evidence 证明这些 draw 的最后消费者已经越过完成点。

### RHI 三种模式怎样影响 Submit

Submit 进入 RHI 时间线后，会受当前 RHI thread mode 影响：

- `DedicatedThread`：RHI 逻辑工作可以落到独立 `FRHIThread` 上。
- `Tasks`：RHI 逻辑工作会跑在高优先级 TaskGraph worker 上，同时带 RHI task tag 和 RHI ownership。
- `None`：没有独立 RHI 执行载体，相关工作更贴近 RT/local queue 推进；Submit 内部的 task 和依赖仍然用来保持顺序。

这三种模式不会改变“Submit 是 RHI command list 的交接边界”这一点。它们改变的是 RHI 时间线的执行载体和调试位置。

### 并行 translate 的控制点

RHI 并行 translate 需要运行时条件支持。UE 会看 RHI 能力、profilegpu 状态、`r.RHICmd.ParallelTranslate.Enable` 等实际控制条件。`QueueAsyncCommandListSubmit` 是并行子命令列表汇入 immediate list 的入口之一，但接口中仍可见的 `ParallelTranslatePriority`、`MinDrawsPerTranslate` 在 UE5.7 当前实现中未被使用，不能根据名字反推 translate 的拆分、粒度或优先级策略。

调试时，如果 RT 已经录完 command list，但 GPU 结果没出现，可以沿 Submit 链继续查：

```text
additional command lists 是否都录制完成?
dispatch task 是否被前置依赖卡住?
translate 是否被禁用或等待 RHI ownership?
finalize 是否完成?
最终 submit 是否交给后端?
GPU/present 是否还没执行到这批命令?
```

---

## 第八步：Frame sync 和 fence 限制时间线距离

GT、RT、RHI、GPU 可以并行，但不能无限拉开距离。UE 的默认策略是让产物尽量留在下一位消费者所在的时间线上异步前进，只在帧末、资源生命周期或确实需要结果的位置建立等待。

先用四类生产者—消费者关系判断是否需要同步：

| 产物关系 | 典型例子 | 同步含义 |
| --- | --- | --- |
| CPU 生产，CPU 同帧消费 | GT 快照交给 RT task，RT 更新 Renderer 场景 | 用 task 依赖、queue 顺序和 RT fence 表达 CPU 时间线进度 |
| CPU 生产，GPU 消费 | RT/worker 记录 command list，RHI submit，GPU queue 执行 | 提交后可异步前进；Submit 证明已交接，不证明 GPU 完成 |
| GPU 生产，GPU 继续消费 | GPU pass 产生 buffer，后续 pass 继续读取 | 数据可留在 GPU resident 路径，通过 GPU 侧依赖和 barrier 保序，无需 CPU 取回结果 |
| GPU 生产，CPU 必须消费 | readback、query 或显示端反馈 | 若 CPU 要当前帧结果，就必须等 GPU 到达对应点，形成硬同步；若允许延迟或可选消费，可用历史结果减少阻塞 |

这四类关系解释了为什么 CPU/GPU 边界不能只说“GPU 更慢”或“readback 很贵”。真正决定 stall 的是：消费者是否必须在当前时刻拿到生产结果，以及结果能否继续留在原时间线上。Fence 和 frame sync 的职责，就是把“必须等到哪里”表达成可控的进度边界。

### Frame sync 控制“最多领先多少”

`FFrameEndSync::Sync` 维护滚动 fence。它关心的是距离：

- GT 不能无限领先 RT，否则 render command 会堆积，RT 使用的对象生命周期也会变得危险。
- GT 不能无限领先 RHI/GPU/present，否则输入延迟、资源释放和显示端排队会失控。

因此它会维护两类 fence：

| fence 组 | 控制的距离 | 心智模型 |
| --- | --- | --- |
| `RenderThreadFences` | GT 与 RT 的距离 | 控制 render command 被 RT 消费到哪里 |
| `PipelineFences` | GT 与 RHIThread 或 swapchain/present 的距离 | 控制 RHI/GPU/present 管线不要落后太多 |

两个 CVar 影响默认重叠：

| CVar | UE5.7 源码默认值 | 含义 |
| --- | --- | --- |
| `r.OneFrameThreadLag` | `1` | 默认允许 GT/RT 有约一帧重叠；设为 0 或 flush all threads 会更强同步 |
| `r.GTSyncType` | `0` | 默认按 RHIThread 深度同步；`NumFramesOverlap = 2 + (-GTSyncType)` |

如果选择 swapchain 深度，但运行时 VSync 关闭或平台条件不满足，UE 会退回 RHIThread 深度。调试显示延迟时要看实际生效路径。

### RenderCommandFence 表达一个可等待的进度点

`FRenderCommandFence` 的 `BeginFence` 会往 RT 时间线插入一个进度点，`Wait` 才真正等待。等待深度决定这个 event 的 prerequisite 链延伸到哪里：

| 深度 | 等到哪里 | 适合的心智模型 |
| --- | --- | --- |
| `RenderThread` | RT 执行到 fence 命令 | 等 render commands 被 RT 消费 |
| `RHIThread` | RT 到点，并让当前 RHI 提交链到达 CPU 安全点 | 等 RHI 时间线越过相关提交边界，不代表 GPU 已完成 |
| `Swapchain` | present/flip 相关事件触发 | 控制更接近显示端的延迟 |

Fence 提供的是消费者进度证明。资源所有者先判断最后一位可能使用数据的消费者是谁，再选择覆盖那条时间线的等待深度；等待完成后，才能把“消费者已越过安全点”转换成“这份资源可以复用或销毁”。资源释放、对象销毁、跨线程可见性和帧延迟控制，都依赖这条从所有权到消费者、再到进度点的判断链。

例如准备销毁一个 mesh resource 时，GT 侧通常最关心的是“还有没有已经发出去的渲染工作会读这份资源”。如果只是确认 RT 已经执行到某条释放前的 render command，等待 `RenderThread` 深度可能足够解释 RT 侧进度；如果这份资源还可能被已经提交到 RHI 时间线的命令引用，就要把等待链延伸到 `RHIThread` 或更深的安全点。显示延迟类问题则不同，它关心的是 present/flip 附近的排队距离，才会落到 `Swapchain` 深度。判断错误会出现两类问题：等浅了，资源生命周期不安全；等深了，GT 帧末可能被不必要地拖住。

### Flush 是更重的全局收束

`FlushRenderingCommands()` 会强行把 RT 相关工作收束到更深的同步点。它适合关闭线程、生命周期边界、调试、必须清空 RT 工作的场景。

热路径里频繁 flush 通常说明所有权设计有问题。普通数据交接应该靠快照、队列、明确的所有权和必要的 fence；flush 会显著减少 GT/RT/RHI/GPU 的重叠。

---

## 并行可以发生，但每层都有汇合点

UE 的渲染线程模型允许多层并行，但每层并行都必须有自己的拥有者和汇合点。

### 第一层：并行生成 render command

`FRenderCommandList` 和 RenderCommandPipe 允许 worker 或系统批量生成“稍后要在 RT 上执行的 CPU lambda”。这些 lambda 先存在各自的列表或 pipe 中，最后在 `Submit` 或 pipe 回放时进入 RT 时间线。

这一层并行发生在 render command 的生成阶段；`FScene` 这类共享 Renderer 状态仍然回到 RT 安全点修改。

### 第二层：并行录制 pass 内 RHI command list

当 Renderer 已经知道某个 pass 要画哪些 draw 后，可以把 RHI 命令录制拆给多个 worker。每个 worker 写自己的 `FRHICommandList`，最后汇回 immediate list 或 RDG parallel pass 的集合。

并行的是 command list 记录。共享 Renderer 状态仍然由 RT 在安全点修改。

### 第三层：并行 translate RHI command list

Submit 收到多条 command list 后，如果 RHI 能力和 CVar 条件允许，可以把 translate 分给多个任务。Finalize 再把结果汇合，最终 submit 给平台后端。

并行的是 RHI 命令翻译。GPU 完成仍然发生在后端队列之后。

三层对齐看：

| 并行层 | 并行对象 | 汇合点 | 调试时先问 |
| --- | --- | --- | --- |
| render command 生成 | RT 上下文 CPU lambda | `FRenderCommandList::Submit` 或 pipe 回放 | 命令是否已经插入 RT 时间线 |
| pass 内 RHI 录制 | draw/dispatch/transition command list | RDG / parallel command list set / immediate list | 子列表是否完成并汇回 |
| RHI translate | RHI command list replay | Submit 的 finalize/submit | RHI 任务是否越过依赖并提交后端 |

UE 能并行，是因为每个 worker 先写自己的列表；UE 能安全，是因为共享状态修改和最终提交都有规定的汇合点。

并行子列表还必须知道自己汇入哪条 parent command list。parent 提供批次顺序、提交上下文和最终汇合点；子列表只拥有自己的录制区间。TLS 中的当前 `FRenderCommandList` 解决“当前生产者把 render work 记到哪里”，parent RHI list 解决“这些并行 GPU work 最终归到哪一批”。二者都不是允许多个 worker 同时改同一份 Renderer 共享状态的许可证。

| 层级 | 局部产品 | Parent / 汇合者 | 成立证据 |
| --- | --- | --- | --- |
| render-command 生产 | TLS 或显式 `FRenderCommandList` 中的 lambda | pipe 或 RT 时间线 | 列表 Submit / pipe 回放后进入 RT 可消费路径 |
| pass 内 RHI 录制 | worker 私有 `FRHICommandList` | immediate/parent list 或 parallel pass 集合 | `FinishRecording()` 只证明该子列表 CPU sealed |
| RHI translate | context 上的后端工作 | Submit 的 formation/finalize 阶段 | 前置 translate 完成并形成可提交平台工作包 |

### 工具模式不会取消边界，只会改变载体

抓帧、单线程排障和平台限制可能关闭部分并行或改变 bypass。此时 producer、product、consumer 和 completion evidence 仍然存在，只是距离缩短：

- 抓帧工具可能要求更连续、可截断的命令流，因此禁用某些并行录制或 translate 路径；不能据此把工具模式当成正常性能模型。
- `-onethread` 或 threaded rendering 关闭时，render work 可能 inline 或在更近的 local queue 推进；这有助于判断竞态，但不证明原代码的生命周期契约正确。
- RHI bypass 让许多 RHI 调用更接近当前 context 执行，减少普通 deferred command node；它仍不等于平台 queue submit，更不等于 GPU complete。
- Dedicated Server 或不能渲染的运行形态可能根本不创建完整渲染时间线；残余入口的同步退化不能外推到正常客户端帧。

因此“单线程时没问题”只能作为竞态线索，不能作为修复。正确修复仍要让快照、所有权和等待深度在线程化模式下成立。

运行中需要改变 threaded rendering、RHI mode、parallel translate 或 bypass 时，也不能把开关看成一次无成本赋值。安全切换遵循同一条生命周期协议：

```text
停止接受依赖旧模式的新生产
  -> 排干旧模式已记录但未消费的 render/RHI work
  -> 等到切换合同要求的 CPU/RHI 安全深度
  -> 切换 mode / bypass 和相关执行载体
  -> 重建或重新锁存依赖该模式的队列、context、TLS/ownership 状态
  -> 恢复生产
```

如果在旧批次仍处于 recording、pipe launch/replay 或 RHI translate 时直接切换，新生产者可能按新规则写入，而旧消费者仍按旧规则读取，造成列表丢失、顺序断裂或 ownership 判断错误。这里“排干”应匹配切换对象：只改变 CPU 投递形态时未必需要全 GPU idle；若重建会影响仍被 GPU 使用的 native context/resource，则必须再取得覆盖最后消费者的第 6 级 GPU consumed 证据。禁止用每帧 `FlushRenderingCommands()` 模拟模式切换安全，它既扩大同步范围，也掩盖真实生命周期错误。

### 启动、关闭与健康检查也是产消协议

启动时必须先让 RHI 能力和 Renderer 所需基础设施成立，再建立 RT/RHI 的执行载体；启动画面或早期平台绘制可能使用特殊路径，不能假设完整 Renderer 主线已经可用。

关闭时顺序相反：停止继续生产新 work，排干仍可能消费共享状态的 RT/RHI 工作，再关闭对应执行载体，最后才释放其依赖的资源。这里的 `FlushRenderingCommands()` 是生命周期边界工具，不是“任何时候调用都能证明全 GPU 完成”的万能栅栏；若 native resource 的最后消费者是 GPU，仍需后端退休策略或匹配的 GPU 完成证据。

心跳、线程健康检查和断言用于判断“消费者是否还在推进”。它们能把死锁、无限循环或长时间无进度从静默卡死变成可定位故障，但不能说明某条 GPU queue 已完成。Profiling 标记则把生产、RT 消费、RHI translate/submit 与 GPU 执行分开放置；看到 `stat unit`、CPU scope 或 GPU pass 名称时，必须先确认样本属于哪条时间线，不能把 RHI CPU 时间直接当成 GPU 时间。

---

## 数据安全来自所有权契约

线程模型最终要解决的是数据安全。UE 的核心契约可以写成四条：

1. GT 拥有 UObject、Actor、Component 这些可变游戏状态。
2. RT 拥有 Renderer 侧 `FScene`、`FPrimitiveSceneProxy`、`FPrimitiveSceneInfo`、cached draw command 和帧级渲染调度状态。
3. GT 要影响 RT 数据时，传递快照、拥有权明确的对象，或由更高层生命周期保证的指针。
4. 释放可能仍被 RT/RHI/GPU 使用的数据前，用 fence、deferred cleanup 或拥有线程的安全提交点确认时间线已经越过。

回到移动 Primitive：

```text
GT 修改 Component Transform
  -> 捕获 transform/bounds 等快照
  -> render command 携带快照进入 RT 上下文
  -> RT 把变化排入 PrimitiveUpdates
  -> FScene::Update 在安全点吸收
  -> 后续 pass 基于新的 Renderer 状态录制 draw
```

这条契约把跨线程修改固定成“命令快照 + Renderer 侧待办”：GT 不直接写 Proxy 的矩阵，RT 也不回读 live Component。

`FScene::Update` 在修改 proxy、scene arrays 等共享 Renderer 结构前，还需要确认 outstanding RHI command lists 已经收束到安全点。原因也属于同一契约：并行录制的 RHI 子列表可能还持有旧场景数据引用；修改共享结构前要确认这些并行工作已经完成。

一个常见坏案例是：GT 创建 render command 时捕获了某个可变 UObject 或 Component 的裸指针，然后调用点很快又修改或销毁了这个对象。单线程调试时 lambda 可能 inline 执行，所以问题不暴露；线程化运行时 lambda 稍后才在 RT 上执行，此时指针指向的对象已经变了或失效，结果就是偶发崩溃、闪烁或读到旧状态。

对应的好做法是把跨线程需要的数据变成命令自己的输入：捕获 transform、bounds、上一帧 transform 等值快照，或者传递生命周期由引擎契约保护的渲染侧对象，并在释放前使用合适深度的 fence、deferred cleanup 或 RT 安全提交点。这样 RT 执行 lambda 时读到的是这条命令随身带来的稳定数据，读取对象也有明确的生命周期保护。

跨线程 bug 常落在这些位置：

| 现象 | 典型原因 |
| --- | --- |
| 单线程调试正常，线程化运行错 | 代码依赖了 inline 执行，线程化后命令稍后才跑 |
| 偶发崩溃或闪烁 | lambda 捕获了 RT 执行前可能失效的指针 |
| 资源释放后 GPU 崩溃 | 只等到 RT，实际还需要 RHI/GPU 安全点 |
| 并行录制时结果错乱 | worker 写了共享 Renderer 状态，破坏了“并行任务只写自己的 command list”契约 |
| GT 帧末长时间卡住 | fence 等待过深、RHI/GPU 落后、热路径 flush |

这些问题都能用同一组问题定位：

```text
这份数据现在属于谁?
命令携带的是快照还是指针?
等待点等到了哪条时间线?
并行任务是否只写自己的列表?
```

### 局部案例：动态 mesh 上传缓冲何时才能复用和退休

假设一段动态 mesh 顶点数据写入上传环形缓冲的 slot A，本帧 draw 会从这个 slot 读取。这个案例的关键不是“引用归零”，而是谁是最后消费者：

```text
CPU producer 写 slot A
  -> 停止继续写入并封口本帧 mesh 数据
  -> RT/worker 录制引用 slot A 的 draw
  -> 第 4 级 RHI recorded
       (内部经过 record sealed / dispatch / translate / platform formation)
  -> 第 5 级 Platform submitted
  -> 第 6 级 GPU consumed：draw 最后一次读取 slot A
  -> slot A 才可被下一帧 CPU 安全覆写复用
```

这里至少有三种不同的“停止”：

1. **停止生产**：CPU 不再修改 slot A，保证录制看到稳定内容；这只保护 CPU 生产阶段。
2. **停止持有 wrapper**：业务代码释放 `FRHIResource` wrapper 的 CPU 引用；这只改变 UE 逻辑对象的引用关系。
3. **停止 native 消费**：GPU 越过最后读取 slot A 的 draw；只有与该最后消费者匹配的第 6 级 GPU consumed 证据才能证明安全复用。

因此生命周期必须分层：

| 层级 | 退休对象 | 控制者 | 安全条件 |
| --- | --- | --- | --- |
| 上传 slot / 数据范围 | 可被 CPU 覆写的环形缓冲区间 | 上传分配器或资源 owner | GPU completion evidence 已覆盖最后读取该区间的工作 |
| UE RHI wrapper | CPU 侧 `FRHIResource` 逻辑对象 | 引用计数与 RHI pending-delete 路径 | 最终 CPU 引用释放，并在允许的 RHI 删除阶段完成逻辑退休 |
| Platform native resource | 后端 buffer、allocation 或相关原生对象 | 具体 RHI backend | 后端根据 queue completion 和自身 retirement policy 确认原生使用结束 |

wrapper 进入 pending delete，不表示 native resource 立即释放；wrapper 被删除，也不能当成通用 GPU fence。反过来，slot A 已可复用也不一定意味着整个 buffer wrapper 应销毁：环形缓冲可能继续服务其他 slot。调试偶发顶点污染时，应先确认第 6 级 GPU consumed 证据是否覆盖“最后读取 slot A 的 draw”，而不是只检查 CPU 引用是否归零。

---

## 一条移动物体的调试路线

假设运行时移动一个物体，画面没有跟着动。按状态线排查：

```text
1. RT queued 是否成立?
   Component 是否进入 UpdatePrimitiveTransform 路径?
   是否因为 redundant transform skip 被跳过?
   是否因为 ShouldRecreateProxyOnUpdateTransform 变成 remove + add?
   是否在 TLS FRenderCommandList 中等待 Submit?
   是否进入显式 pipe 或主 FRenderThreadCommandPipe?
   pipe 是否完成 recording、submit、launch/replay；其 completion event 覆盖到哪一级 CPU 消费?
   是否变成 TaskGraph render command task?
   是否在当前模式下 inline 执行?

2. RT consumed 是否成立?
   RenderThread 主队列是否推进?
   lambda 是否执行到 UpdatePrimitiveTransform_RenderThread?
   等待路径是否涉及 RenderThread_Local?

3. Scene published 是否成立?
   FScene::Update 是否在该 Primitive 被绘制前 drain PrimitiveUpdates?
   Proxy、packed arrays、octree、velocity 等空间状态是否更新?

4. RHI recorded 是否成立?
   当前 View 是否可见该 Primitive?
   对应 pass 是否录制 draw/dispatch?
   并行 RHI 子列表是否完成并汇回 immediate list?

5. Platform submitted 是否成立?
   第 4→5 级内部的 record sealed、dispatch、translate、platform formation、queue submit 是否走完?
   当前 RHI mode 是 None、DedicatedThread 还是 Tasks?

6. GPU consumed 是否成立?
   frame end fence 等待深度是 RenderThread、RHIThread 还是 Swapchain?
   GPU/present 是否还没消费到这批后端命令?
```

不要只凭函数调用判断，最终按“最后成立状态”收束，并为每层保留一个最小观察证据：

| 最后成立层 | 最小观察证据 | 若下一层未成立，优先检查 |
| --- | --- | --- |
| 1. RT queued | transform 快照已生成；TLS/list Submit、pipe launch/replay 或 render task 入队能看到对应批次身份 | 捕获数据、pipe recording/submit、task prerequisite、GT wait pumping/reentry |
| 2. RT consumed | 对应 lambda 的 RT scope/task/completion event 已越过；`PrimitiveUpdates` 出现该对象更新意图 | RT 主队列、pipe replay、`RenderThread_Local` 等待链 |
| 3. Scene published | `FScene::Update` 或对应 publication 点已吸收该对象更新，后续查询看到新 Scene 状态 | Scene 阶段门、更新合并、重建路径与当前帧查询版本 |
| 4. RHI recorded | 目标 draw 已存在于 RHI list，且相关列表完成 CPU 录制封口 | visibility/pass 资格、子列表 parent 汇合、record sealed/dispatch/translate 内部进度 |
| 5. Platform submitted | capture 或后端提交记录中存在对应平台 command work，目标 queue 已接管 | platform formation、queue 选择、资源状态和提交顺序；此时仍不能覆写上传 slot |
| 6. GPU consumed | 与该 draw/资源最后读取匹配的 GPU fence/completion value 已满足，或 GPU capture/timestamp 证明越过目标点 | GPU dependency、错误 fence 范围、跨 queue 等待、资源过早复用；Present 不是通用全 GPU 完成证据 |

这张表是调试时唯一的观察证据模型：先找到最深的已成立层，再检查它与下一消费者之间的交接。日志、CPU profiler、GPU capture 和 fence 都只是不同层的观察工具，不能越级替代完成证据。

---

## 主线压缩回顾

把整篇压回一条线：

```text
GT mutable Component state
  -> capture UpdateParams snapshot
  -> create render command lambda
  -> Dispatcher chooses TLS list / explicit pipe / main pipe / TaskGraph / inline
  -> RenderThread named thread consumes command
  -> PrimitiveUpdates records scene-side change
  -> FScene::Update absorbs change at safe point
  -> frame rendering records FRHICommandListImmediate / additional RHI lists
  -> RHI Submit dispatches and translates recorded lists
  -> backend finalize forms platform command packages
  -> Platform Queue Submit hands packages to the target GPU queue
  -> GPU consumes asynchronously; matching completion evidence closes the target use
  -> fence / frame sync bounds how far timelines can drift at the selected depth
```

这条线给出的是所有权和时间线的转移：

- GT 拥有可变游戏状态，所以先做快照。
- render command 是要在 RT 上下文执行的 CPU work，所以需要 Dispatcher 决定入口载体。
- RT 拥有 Renderer 场景，所以先把变化排进 RT 侧更新待办，再在安全点吸收。
- RHI command list 记录平台无关 GPU 工作，所以需要 Submit 交给 RHI 时间线翻译，再由后端形成可提交的平台命令包。
- Platform Queue Submit 只证明目标 queue 已接管；GPU 稍后消费，匹配该 queue 与最后消费者的完成证据才证明目标使用结束。
- fence 和 frame sync 选择需要等待的深度，用来限制各条时间线距离；CPU 侧 fence 不能自动升级为 GPU 完成证据。
- 并行发生在明确列表和任务边界内，共享状态修改要回到拥有线程和安全点。

本章和相邻章节的关系也可以这样记：

```text
02: Component 怎样变成 Renderer 可用的 Proxy / SceneInfo / FScene 数据
03: 命令怎样跨 GT / RT / RHI / GPU 时间线安全推进
04: RHI command list 里记录的资源、PSO、binding、barrier 和后端抽象是什么
05: RDG 怎样组织 pass、资源依赖和并行执行
07: MeshDrawCommand 怎样把可见 mesh 变成可提交 draw
```

---

## 下一篇如何接上

03 停在 `FRHICommandListImmediate` 和 `Submit` 这道边界上。下一篇 04_RHI 会展开 RHI command list 内部：RHI 资源怎样创建和释放，PSO 怎样缓存，shader 参数怎样绑定，resource transition 怎样表达，最终怎样落到 D3D12/Vulkan/Metal 等平台后端。

换句话说：03 解决“命令在哪条时间线上运行，谁等谁，谁拥有数据”；04 解决“命令列表里记录的 GPU 工作到底是什么”。
