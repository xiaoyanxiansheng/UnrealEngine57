# 07 MeshDrawCommand 与 MeshPassProcessor

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: 01 架构总览、02 Component 到 SceneProxy、03 线程模型、04 RHI、05 RenderGraph、06 GPUScene  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）


## 这一篇要解决的困惑

前几章已经把物体送进渲染场景，也解释了 GPUScene 怎样保存 shader 可读取的 primitive / instance 数据。本章接手的正向任务，是把这些场景输入逐层收敛成一次可执行的 mesh draw：SceneProxy / collector 先交出候选几何，目标 pass 的 processor 再决定资格与画法，`BuildMeshDrawCommands` 固化稳定配方，当前 view 建立可见工作项，排序与 Instance Culling 形成实例批次和 direct / indirect 参数，最后 Renderer 把工作记录进 RHI 时间线。

`FMeshBatch` 是这条链的起点。它说明某个 section / LOD 使用什么材质代理、Vertex Factory、顶点与索引范围。后续生产者依次补齐：

- 当前 pass 是否接受它；
- 这一 pass 使用哪组 shader 和固定管线状态；
- 稳定绑定、顶点流与 draw range 怎样保存；
- 当前 view 是否真的需要提交它；
- 它应排在什么位置，是否有机会与别的实例共享一次提交；
- Instance Culling 最终留下哪些 instance；
- 提交时怎样把最小管线配方展开成 RHI PSO 并发出 draw。

因此，本章讨论的是 **mesh 如何在某个 pass 的语义下被编译成可提交命令，又怎样进入本帧执行工作集**。

`FMeshDrawCommand` 就处在这个转换的中间：processor 已经把 shader、稳定 bindings、streams、draw range 与 minimal pipeline identity 写入其中；scene cache 或 one-frame storage 拥有它；当前 view 与 submit 侧是它的后续消费者。这个正向角色确定后，边界也很清楚：它本身不承载场景对象、当前帧可见性结果或已经录制好的 RHI 命令。

### Unity 读者的定位

如果你来自 Unity SRP，可以把它粗略理解为 RendererList 过滤之后、真正调用 `CommandBuffer.Draw*` 之前的 draw item。但 UE 有一个重要取舍：对满足条件的静态 primitive，它会尽量在对象进入场景时完成昂贵的 pass 编译并缓存命令；每帧再围绕当前 view 组织可见命令、排序、实例筛选和提交。

这意味着 UE 的“静态 / 动态”首先是 **命令生产时机与生命周期** 的区别，而不只是物体会不会移动。

### 贯穿案例：两块使用同一网格和材质的金属板

本章跟踪两块普通静态网格金属板。它们使用相同的 mesh section 和红色金属材质，都会进入 DepthPass 与 BasePass；两块板的位置不同，因此在 GPUScene 中拥有不同的 primitive / instance 身份。

这个案例适合揭开四个常见误解：

1. 同一个 `FMeshBatch` 进入两个 pass，不会得到“一条通用命令”，而会得到两份 pass-specific 配方。
2. 两块板材质相同，不代表它们天然属于同一个 draw。
3. 即使两条命令状态完全相同，也要在本帧排序后相邻，并满足实例数据与提交约束，才可能被压缩。
4. Instance Culling 可以决定哪块板的实例真正提交，但不会重新选择材质或 shader。

## 一张过程地图：四次数据换形

下面不是调用栈，而是数据形态、所有权和生命周期的变化：

```text
候选几何
FMeshBatch / FMeshBatchElement
  owner: SceneProxy / mesh collector 路径
  lifetime: 静态收集或本帧收集
        │  Processor 做资格、shader/state 与 command emission 决策
        ▼
pass-specific 稳定 draw 配方
FMeshDrawCommand
  owner: scene cache 或本帧 pass storage
  lifetime: 跨帧缓存，或仅当前帧
        │  当前 view 判定它需要进入这个 pass
        ▼
本帧可见包装
FVisibleMeshDrawCommand
  owner: 当前 view 的 mesh pass 工作集
  lifetime: 当前帧 / 当前 view
        │  排序先满足 pass 顺序，再创造邻接；兼容性判断不由排序代替
        ▼
本帧实例与 draw 输入
Instance Culling output
  surviving instance IDs / ranges
  descriptors / offsets / direct count or indirect args
  compaction / preserve-order metadata
        │  Renderer 录制 RHI draw intent
        ▼
RHI recording
  owner: 当前 render pass 的 RHI command list
  lifetime: 当前 command-list recording / transfer 工作
        │  backend translation / context finalize
        ▼
platform command formation
  owner: Dynamic RHI / platform backend finalized package
  lifetime: 形成后保持到 queue 接管并覆盖相关提交引用
        │  Platform Queue Submit
        ▼
submitted queue payload
  owner: platform graphics / compute queue
  lifetime: queue timeline 推进到所有依赖 consumer
        │  GPU executes producer and dependent consumers
        ▼
GPU completion evidence
  owner: queue timeline / fence 与资源生命周期系统
  lifetime: 资源引用覆盖最后 GPU consumer；fence 完成后才可退休或复用当前版本
```

这四层不能互相替代：

- Batch 回答“有哪些候选几何”；
- Command 回答“在这个 pass 中怎样画”；
- Visible Command 回答“当前 view 准备提交哪条配方”；
- Instance Culling 回答“当前 view 留下哪些实例、形成什么 direct / indirect 输入”；
- RHI recording、平台 Queue 接管和 GPU completion 是更深的命令与完成证据。

四次换形是唯一正向过程。最后一段继续展开 culling、RHI recording、Queue Submit 与 GPU completion，只是在区分同一份本帧工作的完成深度。

## 三本账：过程之外的责任坐标

| 账本 | 记录什么 | 主要控制者 | 不能替代 |
| --- | --- | --- | --- |
| Pass 决策账 | Batch 是否进入此 pass，采用哪组 shader、fallback、render state 与 emission 规则 | 对应 `EMeshPass` 的 Processor | 当前 View 可见性、GPUScene 发布 |
| Command 配方账 | bindings、minimal pipeline identity、streams、range、stencil、primitive lookup contract | command build path、scene cache 或本帧 storage | Native PSO Ready、RHI recording |
| 本帧提交账 | wrappers、sort、bucket identity、实例范围、culling metadata、direct/indirect 参数与录制进度 | View/pass setup、Instance Culling、Renderer/RHI | Pass/material 决策、Scene 权威状态 |

三本账是责任轴，不是第二条流程。两块金属板是否被 BasePass 接受属于 Pass 决策账；旧 shader 被复用属于 Command 配方账；为何没有 compact 或 capture 中没有 draw 属于本帧提交账。

## 本篇边界

本章深入 mesh pass、command cache、state bucket、sort、dynamic instancing 与 Instance Culling 的交接，但不展开：

- 具体 BasePass / DepthPass shader 如何实现；
- 材质如何生成 shader permutation；
- RDG 如何计算 barrier 与 transient aliasing；
- GPUScene record 的字段布局；
- Nanite、VSM 等专用管线如何扩展或替换传统 mesh draw 路径；
- RHI 后端如何创建原生 D3D12 / Vulkan pipeline object。

这些系统会与本章相接，但本章只负责解释传统 mesh draw command 框架中的职责边界。

## 阅读地图：六个对象各推进哪一级状态

这张表只用于定位后续流程；每个概念的条件与案例仍在它真正解决问题的段落展开。

| 概念 | 正向职责 | 生产者与下一消费者 | 生命周期 / 所有者 |
| --- | --- | --- | --- |
| Mesh Pass | 指定用哪组规则重新解释候选 mesh | Renderer 选择，processor 执行，Command 接收结果 | 当前渲染路径 / pass 语义 |
| `FMeshDrawCommand` | 保存已经确定的稳定 draw 配方 | processor / build path 生产，Visible Command 与 submit 消费 | scene cache 或本帧 pass storage |
| Cached Command | 把可复用配方移出每帧重建路径 | scene registration / update 生产，当前 view 按需引用 | 随 primitive / scene cache 的有效性存在 |
| State Bucket | 给完整稳定状态匹配的 cached commands 一个共享模板身份 | `FScene` 的 pass-local bucket 表生产，排序与 culling 使用 | scene 级、pass-local |
| Sort Key | 表达当前 pass 的正确顺序与状态邻接目标 | processor / view setup 生成，本帧排序消费 | 当前 command / view 工作集 |
| Instance Culling | 把可见命令与 GPUScene identity 变成实例批次和 direct / indirect 参数 | CPU context + RDG/GPU 构建，Renderer submit 消费 | 当前 pass / view 的 culling context 与图资源 |

## 第一关：`FMeshBatch` 交出 pass 编译候选

`FMeshBatch` 刻意保留较高层语义，因为同一份候选几何需要被不同 pass 重新解释。红色金属板进入 DepthPass 时，重点是深度写入、masked 条件和适合深度阶段的 shader；进入 BasePass 时，重点变成 GBuffer / scene color 输出、材质 shader 与 BasePass 状态。

一个 Batch 还可以包含多个 `FMeshBatchElement`。Element 分别携带 index range、primitive range、instance/run 信息等 draw 参数，所以“一个 Batch”不保证“一条 Command”。Processor 可以拒绝候选，也可以针对不同 element 或 pass 条件产生一个或多个配方；Batch 存在只证明候选数据已经交出，不证明任何 pass 接受。

所以从 Batch 到 Command 的核心动作是一次 **pass 编译**：processor 读取候选语义，施加目标 pass 的资格、shader 与状态规则，再发布可执行配方。

```text
同一 FMeshBatch
   ├─ DepthPass 规则  → depth command
   └─ BasePass 规则   → base-pass command
```

两条 command 可以引用相同的 vertex / index buffer，却拥有不同的 shader、PSO 配方、排序键和 pass flags。这里的成立条件是：当前 mesh 对该 pass 有 relevance，材质与 Vertex Factory 能提供所需 shader，并且 processor 的筛选规则接受它。

如果一个物体在 BasePass 消失而在 DepthPass 仍存在，优先怀疑的就不是 mesh 是否进入场景，而是 **同一 Batch 在不同 pass 中被怎样解释**。

## MeshPassProcessor：把 pass 规则变成命令生产

`FMeshPassProcessor` 是 pass-local 的编译器接口。Renderer 为某个 `EMeshPass` 选择对应 processor，processor 再把候选 Batch 变成该 pass 的 Command。

它的教学模型可以拆成三层决策，而不是固定调用栈：

```text
AddMeshBatch
  接收候选，处理入口级条件
        ▼
TryAddMeshBatch
  判断材质、blend mode、pass relevance、fallback 等资格
        ▼
Process / BuildMeshDrawCommands
  选择 shader 与 render state，写入稳定 draw 配方
```

这三层不是必须逐个背诵的调用链，而是三类控制权：

1. **资格决策**：Batch、element、材质与 pass relevance 是否允许进入；
2. **shader / state 决策**：选择 fallback、permutation、fill/cull、depth/stencil、blend 等 pass-local 状态；
3. **command emission**：把决定压成一个或多个 Command，写入缓存或本帧 storage。

### processor 的所有权边界

Processor 拥有“怎样解释候选”的控制权，但不拥有场景资源的生命周期，也不拥有最终可见性。它可以拒绝一个 Batch，也可以生成 Command；它不能保证这条 Command 当前帧会被画。

Processor 的选择本身也有一份显式注册合同。`FPassProcessorManager` 用 `ShadingPath + EMeshPass` 找到创建函数，并同时记录 PSO collector 与 pass flags。这里有两个彼此正交的条件：`CachedMeshCommands` 表示这个 pass 允许在场景注册 / 更新期生产缓存配方；`MainView` 表示 main-view pass setup 会组织这个 pass 的本帧工作。DepthPass 和 BasePass 同时具备两者，常规 translucency 主要使用 `MainView`，另一些专用捕获 pass 可以只有缓存资格。这样，“能提前缓存”和“主视图每帧会组织”就不会被误当成同一开关。

这条边界解释了三个现象：

- 有 `FMeshBatch` 没有 Command：在 processor 资格判断或 shader / pass 条件处被拒绝；
- 有 Command 没有本帧 draw：当前 view 没把它放进可见工作集，或后续实例筛选结果为空；
- 有可见命令没有真正 draw：提交阶段的 PSO precache / skip 条件或 RHI 路径阻止了执行。

## BuildMeshDrawCommands：把决定压成稳定配方

当 processor 已经决定“要画、用什么画”，`BuildMeshDrawCommands` 才把结果压进 `FMeshDrawCommand`。这份配方主要保存：

- shader bindings；
- vertex streams 与 index buffer；
- first index、primitive count、instance count 等 draw range；
- stencil ref 与 primitive type；
- `PrimitiveIdStreamIndex` 等 GPUScene 读取接口；
- 指向最小 graphics pipeline state 描述的缓存 id。

这里最重要的设计约束是：**Command 只保存 draw 所需数据，不承担它引用资源的生命周期管理。** 如果 cached command 引用了某个 buffer、uniform buffer 或 shader resource，那么资源所有者必须保证它在 command 有效期间仍然存活。

这也是“能不能缓存”的真实门槛。只有当 Vertex Factory 和 shader bindings 不需要每帧或每 view 改写，并且引用资源拥有足够长的生命周期时，Command 才适合跨帧保存。

### `PrimitiveIdInfo`：06 与 07 的交接

Command 的几何和材质状态可以相同，但两块金属板仍然需要读取各自的 transform、bounds 和 instance data。07 不把这些 per-object 数据复制进 Command，而是携带 primitive id lookup 所需的信息，提交时让 shader 沿 06 的 GPUScene 合约找到正确记录。

`PrimitiveIdInfo` 把 Command 配方交给 GPUScene 消费链。对 persistent primitive，build path 写入稳定的 persistent draw id 与 scene instance-data offset；对 dynamic primitive，它写入带 dynamic 标记的本帧索引和 collector-local instance offset。Visible Command 保留这份身份，Instance Culling 再结合本帧 dynamic base offset，把相对范围解释成当前 GPUScene resource window 中的最终读取位置。

因此它携带的是 lookup contract，而不是 transform、custom data 或完整 instance payload。这个合同只有在 primitive / instance / instance-payload 入口、容量与寻址参数、persistent 或 dynamic range、Scene/View uniform 以及当前 culling view 属于同一发布版本时才成立。若 transform 串位，应先检查最后成立的是 persistent id、dynamic base offset，还是 Scene Uniform 暴露的资源窗口，而不是只验证某个 ID 数值非负。

因此，动态实例化能够共享 draw 状态，正是因为“提交状态”与“场景身份”被拆开了：

```text
共享：shader / PSO / streams / draw range
不同：PrimitiveId / instance data lookup
```

如果把 per-object 数据直接烘进 shader bindings，渲染仍可能正确，但 command 之间更难匹配，也就更难共享一次 instanced submit。

## PSO 交接：Command 发布配方，submit 推进到可执行状态

MeshPassProcessor 负责选择 shader 与 render state，`BuildMeshDrawCommands` 把它们登记成 minimal pipeline state id。这个 id 让缓存、排序、预热和 submit 引用同一份稳定配方；submit 再用当前 render targets 补成 full initializer，并取得或创建 PSO cache entry。

真正提交时，`SubmitDrawBegin` 先检查 precache policy：若结果仍为 `Active` 且启用 skip，本次 draw 到此结束；非 skip 路径则取得或创建 graphics PSO cache entry，设置 shader bindings、vertex streams 和其他状态，随后 `SubmitDrawEnd` 记录 indexed / non-indexed 或 indirect draw。若 cache entry 的 native PSO 仍在异步创建，command list 会持有 completion prerequisite，CPU recording 可以继续，dispatch / translation 在后端消费 native PSO 前等待它完成。

```text
命令生产阶段
processor 选择状态 → minimal PSO id 写入 Command

提交阶段
minimal PSO id + active render targets → full initializer
               → Active + skip：本次不记录
               → 否则取得 / 创建 PSO cache entry
               → async 未完成时附加 dispatch prerequisite
               → 记录 state / bindings / draw
               → translation 等待 prerequisite 后消费 native PSO
```

这里的完成深度依次是 recipe、cache entry、RHI recording、native PSO ready for backend consumption、platform command formation、Queue Submit 与 GPU consume。PSO hitch、precache miss 或 submit skip 应沿这条链找最后成立状态，而不是回到“Command 是否生成”这一问后就停止。

## 两种 Command 生命周期：缓存路径与本帧路径

“静态命令”和“动态命令”描述的是生产时机，不是渲染结果的高低级版本。

Cached 与 Dynamic 不是 Component Mobility 的同义词。Movable primitive 仍可能贡献可缓存的稳定 mesh inputs；Static primitive 也可能因为 view-dependent 几何、每帧数据或特殊 Proxy 契约产生 dynamic command。判断轴是 command identity 能否稳定复用，而不是对象标签。

### 缓存路径：把稳定 pass 决策移出每帧

对支持缓存的 primitive，场景注册或相关渲染状态更新时，会为适用的 pass 生成 cached command。场景侧保存的不是“这块板永远要画”，而是“如果某个 view 要画它，已经有一份稳定配方可引用”。

```text
primitive 加入 / 更新场景
  → 收集静态 FMeshBatch
  → 为 DepthPass、BasePass 等分别编译
  → command 或 state bucket 身份进入 FScene

每帧
  → visibility / relevance 选择当前 view 需要的 cached command
  → 建立 FVisibleMeshDrawCommand
```

缓存路径里容易混淆四种身份：

| 身份 | 表示什么 | 失效或变化时的后果 |
| --- | --- | --- |
| primitive / static-mesh linkage | 哪个场景对象拥有这份 pass 结果 | 撤场或重建时必须解除引用 |
| cached command index | 该 primitive 在目标 pass 的配方位置 | 配方重建后旧索引不可继续使用 |
| `StateBucketId` | 多条稳定配方可共享的模板身份 | 模板内容变化时要重新匹配 bucket |
| primitive / instance identity | shader 最终读取哪份 GPUScene 数据 | transform 或实例内容变化通常更新数据，不必重编译稳定配方 |

缓存失效通常来自会改变配方或 command identity 的状态：材质、Vertex Factory、mesh section、shader permutation、pass relevance、render state、bindings、streams、draw parameters、Scene registration 或 Proxy rebuild。重建必须同时撤销旧 command 与 bucket linkage，再发布新配方，不能只更新其中一个身份。仅仅 transform 改变并不必然要求重编译 draw 状态，因为 transform 可以通过 GPUScene 身份间接读取；但 primitive 的具体更新策略仍由其场景状态管理决定。

### 本帧路径：当配方依赖 view 或每帧数据

不能安全缓存的 mesh 会在当前帧收集 `FMeshBatch`，再由对应 processor 生成 one-frame Command。常见原因不是“物体名字叫 Dynamic”，而是 Command 中存在每帧 / 每 view 变化的 shader binding、几何或提交参数。

本帧 Command 与 cached Command 在提交能力上是同类数据；区别在于谁拥有它、能活多久、下帧能否继续引用。它通常进入当前 pass 的 one-frame storage，相关 command info、dynamic primitive identity 和 culling payload 也必须在本帧消费完成前保持一致；下一帧不能沿用这些临时索引或存储地址。

对于动态 primitive，身份发布还存在 finalize 边界：processor 可以先生成 one-frame 配方，但 Visible / culling 消费前，本帧 primitive range、instance offset 和相关 lookup 必须已经冻结。否则 Command 本身正确，shader 仍可能读取错误 primitive。调试 dangling resource 或错误复用时，这个生命周期差异比“静态 / 动态物体”标签更重要。

## Visible Command：稳定配方进入本帧工作集

`FVisibleMeshDrawCommand` 是当前 view / pass 的本帧工作项：它引用稳定 Command，并补上后续排序、实例筛选和提交需要的本帧元数据。把稳定配方留在原 owner、这里只保存引用，可以避免为每个 view 复制 shader bindings 与 streams。

| 数据形态 | 保存的核心事实 | 谁拥有 / 活多久 |
| --- | --- | --- |
| `FMeshDrawCommand` | shader、稳定 bindings、streams、draw range、minimal PSO 配方 | scene cache 或本帧 pass storage |
| `FVisibleMeshDrawCommand` | Command 引用、当前 pass/view 资格、Sort Key、bucket linkage、primitive identity、culling payload | 当前 view 的 pass 工作集，仅本帧 |
| Culling 构建结果 | surviving instances、packed instance indirection、command info、direct count 或 indirect args | 当前 pass 的 culling / RDG 结果，供 Submit 消费 |

因此“有 cached command”只证明配方可复用；“有 Visible Command”才证明当前 view 已把它纳入本 pass 工作集；两者都不证明 surviving instance count 大于零或 RHI draw 已发出。

## State Bucket：为相同稳定状态建立共享身份

当 cached command 使用 GPUScene 路径时，场景按 pass 计算 dynamic-instancing hash，再用完整匹配确认 pipeline id、stencil、shader bindings、vertex streams、primitive-id stream、index buffer、draw range、instance count，以及 direct / indirect 参数确实一致。匹配成功的命令进入该 pass 的 state bucket；多个 primitive 保存同一个 `StateBucketId`，并从 bucket 中引用同一份 `FMeshDrawCommand` 模板。

这个设计减少 **重复 command 状态的存储与比较成本**，并给排序与 Instance Culling 一个可复用的匹配身份。到这里成立的是模板等价；本帧合并还需要继续检查：

- 这些 primitive 当前 view 都可见；
- 它们排序后一定相邻；
- Instance Culling 一定允许压缩；
- 最后只会发出一个 draw call。

### 匹配到底有多严格

“同材质”只覆盖了 command 状态的一小部分。动态实例化匹配还会比较 pipeline id、stencil ref、shader bindings、vertex streams、primitive id stream、index buffer、draw range、instance count，以及直接 / indirect draw 对应参数。

因此案例中的两块金属板只有在使用同一几何 section、相同 pass 配方和兼容 bindings 时，才可能共享 bucket。若一块板使用不同 LOD、材质参数被烘入不同 binding、或 draw range 不同，即使画面看起来相似，也不会匹配。同 bucket 只证明稳定模板匹配，不证明本帧最终合批；没有持久 bucket identity 也不自动证明本帧动态匹配永远没有实例化机会。

可以把 bucket 理解为一句资格声明：

> “这些 command 的稳定提交模板相同，后续可以尝试一起处理。”

它不是结果声明。

## Sort Key：先建立正确顺序，再创造邻接机会

每条可见命令都带有 64 bit sort key，但不同 pass 会赋予这些位不同语义。BasePass 常把 masked 与 shader hash 等信息压进 key；translucency 更关心距离与优先级；其他 pass 可以使用 generic shader hash 或自己的布局。

所以 Sort Key 不是跨 pass 通用的“材质排序值”，更不是世界空间深度的统一编码。它是 pass 对自身目标的压缩表达：

- 某些 pass 优先减少状态切换；
- 某些 pass 优先满足前后顺序；
- 某些 pass 要把特殊类别放在固定区间。

对 dynamic instancing 来说，排序还有第二个作用：让相同 `StateBucketId` 的可见命令尽量相邻。只有相邻，线性处理工作集时才有机会把后续相同模板压入前一个提交批次。

但排序仍不执行合并。它只改变队列中的邻接关系，不改变 command 内容，也不裁剪 instance。

相同 Sort Key 不证明两条 Command 可合并；状态可实例化匹配也不证明 pass 允许它们相邻。透明顺序、preserve-order、direct / indirect 模式和本帧 work metadata 都可能阻止邻接或 compact。

## Dynamic Instancing：从模板匹配推进到实例化提交

把两块金属板合成一次实例化提交，需要连续满足多层条件：

```text
稳定 command 状态完全匹配
        ↓
获得相同 StateBucketId（或本帧匹配身份）
        ↓
当前 view 都生成 Visible Command
        ↓
pass 正确性允许排序后形成合法邻接
        ↓
primitive / instance lookup 可以区分对象数据
        ↓
culling work metadata、draw range、direct / indirect 与顺序要求兼容
        ↓
Instance Culling 形成可批处理的实例与 draw 输入
```

链条中任一条件失败，结果通常只是多发 draw，而不是渲染错误。这是一个性能优化的典型特征：正确性优先，匹配越严格，合并率可能越低。

### merge 的数据形态

合并不是把两个 `FMeshDrawCommand` 对象物理拼成一个更大的 Command。更准确地说，后续处理发现相邻 Visible Command 使用同一稳定模板，于是保留一次状态提交，并把多个 primitive / instance 的身份整理进实例数据或 indirect 参数。

因此要区分三件事：

- **command deduplication**：bucket 复用同一份稳定模板；
- **queue adjacency**：sort 让相同模板相邻；
- **draw compaction**：Instance Culling / submit preparation 真正减少要提交的 command 数。

三者互相帮助，但不是同一个阶段。

## 把可见工作项推进成实例批次

Instance Culling 从当前 pass 排好序的 Visible Commands 接手。每条包装记录指向稳定 Command，并携带当前 view / primitive 的身份、sort key、state bucket、culling/draw work metadata、instance runs 与 output mapping 等本帧信息；它们共同构成“用哪份配方、处理哪段实例、把结果写到哪里”的本帧工作描述。

这份工作描述按三段推进：

1. **CPU 声明与组织**：读取 Command 所需的 primitive / instance lookup，把 Visible Commands、instance ranges、view inputs、dynamic offsets、work metadata 与 output mapping 组织成构建输入；
2. **GPU / RDG 构建结果**：筛选 surviving instances，生成 packed instance indirection、per-command descriptors、instance/payload offsets、batch count/stride、compaction data、direct count 或 indirect args；
3. **RHI recording 消费**：Renderer helper 读取稳定 Command 与构建结果，取得或准备 PSO，绑定状态并把 draw intent 录制进 RHI 命令时间线。

因此 CPU 侧 context 的完成证据是输入与调度已经建立；packed indirection、compaction data 和 indirect args 要以对应 RDG / GPU 构建输出为证；Renderer submit 再把这些结果与稳定 Command 汇合到 RHI recording。

函数名中的 `SubmitMeshDrawCommands`、`SubmitDrawBegin` 或 `SubmitDrawEnd` 表示 Renderer 正在消费 draw work；它们不等于第 04 章定义的 platform Queue Submit。RHI draw 已录制之后，仍有 context translate、platform command formation、Queue Submit 和 GPU completion。

这一步沿用 Command 中已经固化的 BasePass / DepthPass 资格、材质 shader 与 draw state。GPUScene 继续拥有 primitive / instance 数据，Instance Culling 只通过身份与资源窗口消费它们，并把 surviving instances 整理成后续录制所需的 direct / indirect 输入。

### 相同 bucket 还要怎样才能 compact

Instance Culling 线性处理排序后的 Visible Commands。当前项只有在 compact 开启、`StateBucketId` 与上一条保留项相同、`CullingPayloadFlags` 相同，并且提交模式允许时，才会并入上一条。当前 UE5.7 条件中，**indirect draw 与 preserve-instance-order 同时成立**会禁止这种合并；Uniform Buffer View 路径还受单个批次可容纳实例数的上限约束，并要求当前项不是 indirect draw。任一条件不成立时，系统保留新的 draw-command info，而不是把语义不同的 instance work 塞进旧批次。

所以看到相同 bucket 却有多个 draw，不应立刻判断系统失效。正确问题是：

> “模板是否相同、是否相邻、实例提交语义是否也允许压缩？”

### dynamic primitive 怎样完成身份交接

动态 primitive 没有长期 GPUScene 槽位时，build path 会在 `PrimitiveIdInfo` 中写入带 dynamic 标记的本帧索引，并记录 collector-local instance offset。collector 完成本帧动态 primitive / instance range 后，这段相对身份被冻结；Instance Culling context 同时接收本帧 dynamic primitive base offset，于是可以把 Command 携带的局部范围解释成 GPUScene 当前资源窗口中的最终读取位置。

这次交接还要求 Scene Uniform 指向的 GPUScene resources、dynamic base 与已发布的 instance-data window 属于同一版本。Visible Command 可以先保存身份合同，但 culling / submit 只有在 range 已冻结且资源窗口已发布后，才能用它生成或消费最终实例地址。

这里的 bug 往往不是材质错误，而是 transform 错位、实例串位、随机消失或读取到别的 primitive 数据。调试时应从 `PrimitiveIdInfo` 和 dynamic offset 的发布时序回到 06，而不是重查 processor 的 shader 选择。

## 贯穿案例：两块金属板怎样走完整条链

现在把前面的概念重新落到案例。

### 场景注册：同一物体产生两份 pass 配方

每块板的静态 `FMeshBatch` 分别交给 DepthPass 与 BasePass processor。DepthPass 生成深度阶段配方，BasePass 生成材质输出配方。两者的 mesh buffers 可以相同，但 shader、minimal PSO 与 sort key 语义不同。

### 场景缓存：两块板可能共享稳定模板

因为两块板使用相同 section、材质和 Vertex Factory，它们在同一 pass 中的 command 可能满足完整动态实例化匹配。场景于是让它们引用同一个 state bucket 模板；各自的 transform 不在模板里，而通过 primitive / instance id 指向 GPUScene。

### 当前 view：稳定模板进入本帧工作集

相机只看到第一块板时，只有它生成当前 pass 的 Visible Command。第二块板即使拥有同一 bucket，也不会因为“可合批”而被强行提交。

相机同时看到两块板时，它们都进入本帧列表。排序把相同 bucket 放到邻近位置，但此时仍只是拥有合并机会。

### Instance Culling：决定最终实例集合

如果两块板的实例 work metadata 兼容且不要求保序，Instance Culling 可以把两份 primitive identity 整理进同一批可录制输入，后续可能使用一次稳定状态和一次 instanced / indirect draw。若第二块板在实例级被裁掉，输出只保留第一块板；若约束阻止 compact，则两块板仍会正确地形成两份 draw 输入。

### RHI 录制与更深完成性：配方进入命令时间线

`SubmitDrawBegin` 先用 minimal state 与当前 render targets 形成 full initializer；precache 状态若仍为 `Active` 且策略要求 skip，本次 draw 在这里停止。其余路径取得或创建 graphics PSO cache entry，绑定 shader 参数与 streams；`SubmitDrawEnd` 再使用 Instance Culling 生成的 instance / indirect 参数记录 RHI draw intent。若 native PSO 尚在异步创建，RHI command list 会携带 completion prerequisite，CPU recording 可以继续，translation 则在后端真正消费 native PSO 前等待它完成。此后还要经过 platform command formation、Queue Submit，GPU 才能消费这次工作。

| 最后成立状态 | 能证明 | 不能证明 |
| --- | --- | --- |
| Batch / Element exists | mesh candidate 完整 | pass 接受 |
| Processor accepted | pass-local 资格与 shader/state 决策成立 | Command 已正确保存、当前 View 使用 |
| MDC exists | draw recipe 成立 | Native PSO Ready、当前帧可见 |
| Visible wrapper exists | 当前 View/Pass 候选成立 | 实例筛选完成 |
| Culling output exists | instance、descriptor、offset 与 direct/indirect 参数成立 | RHI draw 已录制 |
| RHI draw recorded | CPU 命令意图已形成 | platform Queue 已接管 |
| Queue submitted | 平台 Queue 接管对应工作 | GPU 已越过最后消费者 |
| GPU completed | 指定范围的最后消费者完成 | 其他 Queue 或全设备完成 |

## 动态路径对照案例：本帧 procedural mesh

为了补齐 cached 路径之外的生命周期，再看一个最小动态对象：**一个支持 GPUScene、由第 08 章 GDME / collector 路径在本帧交付 `FMeshBatch` 的 procedural mesh**。本章从 Batch 已交付开始，不展开 GDME 的调度和收敛过程。

| 阶段 | 静态金属板 | 动态 procedural mesh |
| --- | --- | --- |
| Batch 输入 | SceneProxy 在注册 / 重建窗口提供，Batch 被消费后不长期保存 | collector 本帧提供，frame / collector-owned |
| Processor | 可在缓存构建时完成 pass 编译 | 本帧执行相同的候选资格、pass 语义与配方固化 |
| Command owner | scene cache 或 State Bucket 模板 | 当前 pass 的 one-frame storage |
| identity | 持久 primitive identity 指向 GPUScene | 本帧 dynamic range / offset 必须在 culling 前 finalize |
| Visible 包装 | 当前 view 引用 cached command | 当前 view 引用 one-frame command；不可跨帧保存 |
| 下一帧 | 配方有效时可以继续复用 | Batch、Command、临时 identity 与 payload 都必须重新生成或重新发布 |

### 动态对象怎样走完主线

1. **Batch 已交付**：collector 给出当前帧的 geometry、material proxy、Vertex Factory 和 elements。此时只有候选，没有 pass 配方。
2. **Processor 编译**：目标 pass 先判断资格与语义，再把每个有效 element 固化成 one-frame Command。多个 element 仍可能生成多条 Command。
3. **身份发布**：`PrimitiveIdInfo` 保存 dynamic 标记、本帧索引与 collector-local instance offset；collector 冻结 dynamic range，culling context 接收 dynamic base，Scene Uniform 再发布与这组 offset 同版本的 GPUScene resource window。
4. **Visible 包装**：当前 view 为 one-frame Command 建立 Visible Command，补上 Sort Key、bucket / matching 信息和 culling payload。
5. **Culling 构建**：CPU 侧 context 组织 command 与 range 输入；随后 GPU / RDG 构建阶段产出 surviving instance indirection、command info 与 direct / indirect 参数。
6. **Submit 消费**：提交阶段读取 Command 的稳定配方和 culling 构建结果，绑定最终 PSO 与资源，再发出 draw。

这条动态路径与 cached 路径共享同一份 **Command → Visible → Culling → Submit** 协议，但 owner 和 lifetime 完全不同。若静态物体正常、只有动态对象串位，最有区分度的检查不是材质 shader，而是：one-frame storage 是否仍有效、dynamic identity 是否已 finalize、culling output 使用的 offset 是否属于同一发布版本。

### 两条路径共同成立的完成证据

| 观察点 | 已经证明 | 仍未证明 |
| --- | --- | --- |
| Batch 存在 | geometry 候选已交付 | 当前 pass 接受它 |
| Command 存在 | pass 配方已固化 | 当前 view 需要它 |
| Visible Command 存在 | 已进入本帧 pass 工作集 | culling 后仍有实例 |
| Culling 结果非空 | 实例与 draw 参数已构建 | PSO ready、RHI draw 已记录 |
| Renderer draw helper 已执行 | RHI draw intent 已记录 | platform command 已形成、Queue 已接管 |
| Queue Submit 成立 | 平台 Queue 已接管 | GPU 已完成或最终画面可见 |

## 三个“同材质却不能合并”的反例

### 参数绑定不同

两个组件使用同一父材质，但 material instance uniform buffer 内容不同。Permutation 可能相同，完整 bindings 不匹配，不能共享模板。

### 透明顺序阻止邻接

两块透明板状态相同，但第三个透明物体必须排在中间。Sort 不能跨越正确性约束强行合并。

### 本帧 Payload 不兼容

两条 Visible Commands 使用同 bucket，但一条要求 preserve instance order，另一条采用不同 indirect / culling payload。模板相同，compact 仍不成立。

## 统一调试树：寻找最后一个成立状态

所有症状都沿同一组证据向下定位，不再维护第二套函数检查链：

| 状态证据 | 能证明什么 | 下一个责任层 |
| --- | --- | --- |
| Batch / Element 正确 | 候选几何、材质代理和范围已交付 | Processor 的候选资格与 pass 语义 |
| Command 正确 | shader、bindings、streams、draw range 已固化 | 当前 view 的 Visible 包装 |
| Visible Command 正确 | 已进入当前 pass 工作集，sort / bucket / identity 已附加 | Culling 输入和构建结果 |
| Culling 结果非空 | surviving instances 与 direct / indirect 参数可供消费 | RHI recording |
| RHI draw 已记录 | CPU 命令时间线已形成 draw intent | platform formation 与 Queue Submit |
| Queue Submit 成立 | 平台 Queue 已接管 | GPU completion 与输出正确性 |
| GPU completion 成立 | 指定消费者越过完成点 | 其他 Queue、后续覆盖或 Present |

下面的症状只是在这棵树上选择不同入口：

### DepthPass 有、BasePass 没有

Depth draw 已证明 Proxy、VF 与基本几何资源可用。依次检查 BasePass relevance、material domain / blend mode、shader policy、Command 输出、Visible Command、culling 和 submit，不要从 index buffer 重新开始。

### 修改材质后静态物体仍用旧 shader

```text
Material Render Resource 更新
  → render state / cached commands 标脏
  → 旧 command 与 bucket linkage 移除
  → processor 选择新 permutation
  → visible wrapper 引用新 command
```

若强制 dynamic path 正常，优先调查 cached build 与 invalidation。

### Draw Calls 多于 Bucket 数

分别统计 Visible Command 数、unique bucket 数、排序后的 bucket 邻接段数、compact 被 payload 阻止的次数、culling 后非空段数与最终 `SubmitDrawEnd` 次数。这样能区分模板过多、排序打散、compact 禁止和实例分布。

### 只有 Dynamic Primitive 串位

检查动态 identity 分配时刻、写入 `PrimitiveIdInfo` 的值、dynamic instance data finalize、culling output id 与 submit instance offset。Static path 正常时，不应先怀疑通用 shader。

### Visible Command 正确但 RenderDoc 无 Draw

```text
Visible Command
  → 是否进入 culling context
  → surviving count 是否大于零
  → indirect args 是否写入并同步
  → SubmitDrawBegin 是否获得 PSO
  → SubmitDrawEnd 是否调用 RHI draw
```

RHI draw 已调用后，才继续调查 GPU resource state、depth/stencil rejection 或后续 pass 覆盖。

## 性能分析：不要只数 Draw Calls

| 成本 | 主要阶段 | 常见增长原因 |
| --- | --- | --- |
| Command 构建 CPU | Processor / Build | dynamic meshes 多、cache 频繁失效 |
| 排序 CPU | Visible setup | command 数量大、Sort Key 邻接差 |
| Instance Culling | context / dispatch | instance 多、payload 复杂、输出压力 |
| RHI recording CPU | state apply / draw intent | bucket 多、compact 失败、PSO 切换多 |

减少 draw call 却增加昂贵的每帧 Command 构建未必是净收益。正确目标是让稳定工作跨帧复用，让 view-dependent 工作留在本帧，并让队列拥有良好状态邻接。

不同阶段的统计不能直接比较：一个 Batch 可有多个 elements，候选可能被 processor 拒绝，cached command 可能本帧不可见，Visible Command 可能被 culling 清空，多条相邻命令又可能 compact。所有性能数字都必须注明采样阶段。

## 源码定位原则

需要事实验证时，用最少符号映射责任：

- 具体 pass 的 `AddMeshBatch` / `TryAddMeshBatch` / `Process`：pass 决策；
- `BuildMeshDrawCommands`：bindings、streams、range 与 identity；
- cached command / State Bucket 路径：持久配方和 linkage；
- Visible Command 添加路径：Sort Key、bucket 与 payload；
- Instance Culling setup / build：实例与 indirect 参数；
- `SubmitDrawBegin` / `SubmitDrawEnd`：PSO、绑定和最终 draw。

符号只用于定位“候选 → 配方 → 工作集 → 实例参数 → 提交”中的责任点，不应替代过程解释。

## 本章检查点

读者应能回答：为什么一个 Batch 会产生多个 pass Commands；cached 与 dynamic 路径的共同协议和不同生命周期；Visible Command 新增了哪些本帧信息；Bucket、Sort、Merge 的因果关系；Dynamic Instancing 的完整条件；Visible Command 存在却无 draw 的原因；实例串位为何优先检查 identity 与 GPUScene。

## 本篇专有名词回看

| 名字 | 现在应该怎样理解 | 调试时优先问什么 |
| --- | --- | --- |
| `FMeshBatch` | 可被多个 pass 解释的候选几何 | section、LOD、material proxy、Vertex Factory 是否正确 |
| `EMeshPass` | 当前使用哪套 pass 语义 | 哪个 processor 应接管，relevance 是否成立 |
| `FMeshPassProcessor` | 把 pass 规则施加到 Batch 的编译器 | 在候选、语义还是配方阶段被拒绝 |
| `FMeshDrawCommand` | 稳定 draw 配方 | bindings、streams、range、minimal PSO 是否正确，资源是否仍存活 |
| Cached Command | 跨帧保存的稳定配方 | 缓存资格是否成立，何时失效与重建 |
| `FVisibleMeshDrawCommand` | 当前 view 对某条配方的本帧引用与提交元数据 | 当前 view 是否生成，sort / bucket / payload 是否正确 |
| `StateBucketId` | 相同稳定 command 模板的 pass-local 身份 | 模板是否相同；不要把它当成合并结果 |
| Sort Key | pass-specific 的队列排序语言 | 当前 pass 优先状态、深度还是特殊类别 |
| Dynamic Instancing | 相同状态命令共享一次实例化提交的优化 | 完整匹配、邻接与实例语义是否同时成立 |
| Instance Culling | 把 Visible Commands 转成实例与 indirect 提交参数 | 哪些 instance 被保留，是否允许 compact |

## 主线回顾

这一章最重要的不是记住一组类名，而是建立一条从候选到 GPU 消费的职责链：

```text
Batch 提供候选
  → Processor 赋予 pass 语义
  → BuildMeshDrawCommands 保存稳定配方
  → scene cache / one-frame storage 确定 owner 与 lifetime
  → Visible Command + Sort / StateBucket 组织本帧工作集
  → GPUScene identity 与资源窗口完成对齐
  → Instance Culling 形成实例批次和 direct / indirect 参数
  → Renderer 录制 RHI draw intent
  → translation 等待所需 native PSO，并形成 platform commands
  → Queue Submit 把工作交给平台队列
  → GPU consume / completion 提供最终消费者证据
```

请保留下面六条边界：

1. **一个 mesh 可以被多个 pass 编译成不同 Command。** Command 是 pass-specific 配方，不是 mesh 的通用渲染对象。
2. **Cached 表示配方可复用，不表示本帧可见。** 当前 view 仍要建立 Visible Command。
3. **Bucket 表示稳定状态相同，不表示已经合并。** Sort 只建立邻接，compact 才减少提交。
4. **Dynamic Instancing 合并提交状态，不合并场景身份。** 每个 primitive / instance 仍通过 GPUScene 读取自己的数据。
5. **Instance Culling 决定实例提交，不重做材质与 pass 决策。** 它是 Command 的消费者。
6. **Command 保存 minimal PSO 配方，不拥有最终 RHI PSO。** 真正的 pipeline lookup、binding 与 draw 发生在 submit 阶段。

掌握这条链之后，面对“物体没画、draw call 太多、实例串位、PSO 跳过”时，就能先判断故障落在候选、pass 编译、稳定配方、本帧工作集、实例筛选还是 RHI 提交，而不是把所有问题都归成一句“MeshDrawCommand 出错了”。
