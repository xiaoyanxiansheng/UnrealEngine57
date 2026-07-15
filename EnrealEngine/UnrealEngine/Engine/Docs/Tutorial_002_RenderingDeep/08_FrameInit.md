# 08 帧初始化与可见性

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: 01 架构总览、02 Component 到 SceneProxy、03 线程模型、04 RHI、05 RenderGraph、06 GPUScene、07 MeshDrawCommand  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `08_FrameInit_CoverageMatrix.md`

## 这一章要纠正的一个直觉

如果你带着 Unity SRP 的习惯来读 UE 的帧初始化，最容易踩的坑是把可见性当成**一个同步函数的返回值**。

在 Unity SRP 里，你通常这样想：

```csharp
CullingResults results = context.Cull(ref cullingParameters);
// 拿到 results 后，立刻就能用它组织 RendererList、提交 pass
```

`Cull()` 调完，结果就在手里，一个数据块，立等可取。于是很自然地会假设 UE 也有一个对应的大函数——比如老资料里常提到的 `ComputeViewVisibility`——调一次，返回一份"可见物体列表"，后面的 pass 直接拿来画。

UE 的实际模型更像一组逐步变稳定的状态。某个时间点，砖墙可能已经通过了 visibility；同一时刻，火焰可能只是被标记为“后面要收集动态几何”。再往后，火焰有了本帧 `FMeshBatch`；接着相应 dynamic collector/context 冻结输入并确定本帧 range；Renderer 在 RDG 中建立 GPUScene resources 与 clear/scatter producer，Scene Uniform 再暴露目标资源窗口，RDG 负责让后续 shader 与 Instance Culling consumer 排在 producer 之后。

所以读这一章的关键，是把“拿到一份列表”的直觉换成“观察状态什么时候变稳定”。不用先记某个大函数叫什么名字，先建立一种新的提问方式：

> 在每一道同步边界之前，数据处于什么状态？谁还能写它？这道边界之后，后面的 pass 可以**相信**什么？

只要你能对任意一个中间状态回答这三个问题，这一章的模型就建立起来了。后文说的“同步边界”，就是这些状态从“还在生产”跳到“后续 pass 可以相信”的门槛。本章只覆盖 PrePass/BasePass 真正开始绘制**之前**的早期主线——把场景里的物体和每个视图的条件，逐步收敛成 per-view、per-pass 的稳定 draw 输入。

## 为什么 UE 要把启动和完成拆开

值得先理解 UE 为什么"故意"把它拆开，否则下面所有的边界都会显得像无谓的复杂度。

先只看两类对象：静态砖墙和动态火焰。在满足 static relevance、缓存资格与失效条件的路径中，砖墙的数据早已在 `FScene`，部分 pass 配方可以预先缓存；本帧仍要确认当前 view/pass 是否选择它。火焰路径也受 Proxy、渲染路径与功能开关约束：GDME 负责生产本帧几何，而 dynamic collector/context 负责另一份 GPUScene identity/range 合同；后者经 RDG resource 注册、clear/scatter producer 声明和 Scene Uniform 窗口暴露后，才成为图内 GPU consumer 的输入。

如果 UE 把这些事串成一个同步大函数，流程会变成这样：

```text
等砖墙/火焰的 visibility 都算完
  -> 等火焰 GetDynamicMeshElements 全部收集完
  -> 等每个 pass 的入口都整理完
  -> 等 GPUScene dynamic range、RDG resources、clear/scatter producer 与读取窗口合同建立
  -> PrePass / BasePass 才能开始消费
```

这样当然安全，但 CPU 会在 PrePass 之前等很久。很多工作明明可以并行，比如视锥剔除、动态 mesh 收集、阴影相关准备、GPUScene 更新前置工作，却被迫排队。

另一个极端是让 `Cull()` 很早返回。此时返回值里可能只有“火焰需要收集”的待办，下一步才由 GDME 生成 `FMeshBatch`；也可能已经有 `FMeshBatch`，下一步才由 GPUScene 上传发布 offset；还可能 GPUScene 的 RDG producer 已经声明，下一步才由 Scene Uniform 暴露其目标资源窗口。PrePass / BasePass 读取的是最终入口，所以它必须等到对应的生产者把这一级状态交账。

所以 UE 的答案是把**启动**和**完成**拆开。早期阶段先把能并行的生产者放出去；后面在几个明确时间点收口，告诉消费者：“从这里开始，这类数据可信了。”**理解这一章，本质上就是理解这些收口点各自冻结了什么。**

## 贯穿全章的两个具体例子

为了让模型可以握得住，全章用同一对物体走完整条流程。它们恰好代表两条命运截然不同的路径：

- **静态砖墙**：一面普通的 opaque StaticMesh。它的"昂贵决策"早就做完了——在满足 static relevance、缓存资格且缓存未失效的路径中，它的静态 MeshDrawCommand 可以在注册/更新阶段（第 07 章）缓存。本章里它每帧只需要被回答两件事：**在当前 view/pass 中它可见吗？** 以及**把可复用配方带入当前 per-pass CPU/draw work 入口**。它是低成本路径的代表。
- **动态火焰**：一团 Niagara 或自定义动态几何。它可能有长期存在的 proxy、材质或资源，但没有像静态砖墙那样在注册阶段就缓存好的完整 per-pass MeshDrawCommand；它的本帧几何和实例范围要临时生产。本章里它要走一条长得多的路：先在剔除后被标记"需要收集"，再在 InitViews 期间真正向 proxy 索要本帧的 `FMeshBatch`，之后才可能生成 pass command，GDME 与 dynamic collector 是两份不同合同：前者交付本帧几何，后者冻结 dynamic identity/range；随后还要建立 RDG resources、声明 clear/scatter producer、由 Scene Uniform 暴露目标窗口并让 RDG 排序消费者。它是高成本、多阶段路径的代表。

砖墙和火焰会在每一道边界上分道扬镳。盯着它们，就能看清"同一帧里，不同物体的数据在不同时刻才变稳定"。

## 心智地图：先看状态台阶，不背阶段名

先把 `BeginInitViews`、GDME、`SetupMeshPass` 这些名字放到后面。08 篇真正要解决的是一个更朴素的问题：**一个场景物体要被 PrePass/BasePass 消费，必须从“场景里有它”逐步变成“这个 view / 这个 pass 有 per-pass CPU/draw work 入口，并且 GPU 能读到它的数据”。**

这条路可以先压成四个状态台阶：

```text
1. 场景账稳定
   这个 primitive 已经在 Render Thread 的 FScene 里，view 条件也推进到本帧。

2. 可见性与去向确定
   当前 view 认为它几何上可能可见、没有被历史遮挡证据剔掉，
   并且 relevance 已经把它分到某些 pass 或动态收集路径。

3. CPU 侧 pass 入口形成
   静态物体复用 cached MeshDrawCommand；
   动态物体先交出本帧 FMeshBatch，再被整理成某个 pass 可消费的入口。

4. GPU 侧可寻址身份发布
   shader / Instance Culling 通过 GPUScene 和 scene uniform 找到 transform、bounds、instance 数据，
   C++ proxy 只是 CPU 侧生产链里的来源之一。
```

静态砖墙和动态火焰在这四个台阶上的差异，就是本章的主线：

| 状态台阶 | 静态砖墙 | 动态火焰 |
| --- | --- | --- |
| 场景账稳定 | 长期存在于 `FScene`，静态 cached command 也早已准备好 | proxy 在场景里，但本帧几何还没生成 |
| 可见性与去向确定 | visibility / relevance 通过后，可以把 cached command 包成当前 view/pass 的入口 | visibility / relevance 通过后，先只是标记“后面要收集动态几何” |
| CPU 侧 pass 入口形成 | 多数情况下只是挑出并包装已缓存的 draw 配方 | 要等 GDME 向 proxy 索要本帧 `FMeshBatch`，再由 pass setup 分到 DepthPass/BasePass 等入口 |
| GPU 侧可寻址合同 | persistent identity 仅在当前 SceneInfo/Proxy 的 Scene 驻留期稳定 | dynamic collector/context 确定本帧 range，RDG resources 与 clear/scatter producer 建立，Scene Uniform 暴露匹配窗口，RDG 再排序 consumer |

这些阶段门共同守住状态台阶，但它们不都在收敛同一种工作：`BeginInitViews` 放行生产者，`FinishGatherDynamicMeshElements` 收敛动态几何，GPUScene / Scene Uniform / Instance Culling 建立图内 GPU 消费合同，`EndInitViews` 最后收敛 CPU visibility task graph。

| 边界 | 它真正守住的状态 | 放回砖墙 / 火焰怎么读 |
| --- | --- | --- |
| `BeginInitViews` | 稳定输入已经准备好，早期任务可以开始读 | 砖墙可以被可见性任务扫描；火焰的动态收集任务可以启动，但还没有本帧 `FMeshBatch` |
| `FinishGatherDynamicMeshElements` | 动态收集结果已完成并合并，pass setup 可以读取动态候选 | 火焰获得本帧几何候选；这不证明独立的 dynamic collector/range 与 GPUScene producer 合同已建立 |
| `EndInitViews` | visibility/relevance/GDME/MeshPassSetup task graph 收敛，临时 packet/allocator 可按其合同结束 | 它位于本节早期 GPUScene publication 与 `BeginDeferredCulling` 之后，只关闭 CPU visibility 任务图；不替这些 RDG 工作证明执行或完成 |

中间的 GPUScene 路径是另一份合同：collector/context 冻结 dynamic identity/range，RDG 注册目标 resources 并声明 clear/scatter producer，Scene Uniform 暴露目标资源窗口，RDG 再保证图内 shader 与 Instance Culling consumer 位于 producer 之后。这个顺序不证明数据已物理写入 buffer，也不证明 RHI、Platform Queue 或 GPU 已完成。


## 唯一主线：Frame Init 状态成立链

本章后续所有实现细节都必须落回同一条状态链，而不是形成第二条函数调用线：

```text
View request ready
→ Renderer Scene version published
→ visibility work launched
→ per-view visibility / relevance ready
→ dynamic geometry gathered and merged
→ MeshPassSetup launched; per-pass CPU inputs continue building asynchronously
→ GPUScene dynamic producer work established
→ Scene Uniform read entrance published
→ BeginDeferredCulling opens the conditional deferred batching context
→ MeshPassSetup / BuildRenderingCommands add per-pass batches to that context
→ EndInitViews waits for MeshPassSetup and finalizes visibility CPU work
→ RDG execute / drain processes registered culling work
→ downstream frame passes consume frozen inputs
```

这条链描述的是**消费者何时可以相信某类输入**。具体任务可以并行、延迟或按条件跳过，但不能跳过数据合同。

在 UE5.7 当前路径中，把这些责任放回源码时间轴，关键顺序是：`OnRenderBegin` 先完成 Scene publication 并启动 visibility，`BeginInitViews` 放行早期 view / GDME 生产者，随后收敛 GDME；Renderer 再提交动态 primitive 数据合同、建立实例状态、更新 view data 与 Scene Uniform，调用 `BeginDeferredCulling`；最后 `EndInitViews` 才等待并关闭 CPU visibility task graph。这个顺序很重要：`EndInitViews` 不是 GPUScene 与 Instance Culling 的前置门，也不是它们的完成证明。

### 三条坐标轴

| 轴 | 回答的问题 | 本章内容 |
|---|---|---|
| 责任轴 | 谁生产、编排、消费 | Scene update、visibility workers、Render Thread、GPUScene、Instance Culling、RDG |
| 数据轴 | 每阶段交付什么 | View request、published Scene、visibility/relevance、dynamic elements、per-pass inputs、GPUScene ranges、Scene Uniform |
| 生命周期轴 | 数据可信多久 | persistent Scene、per-view/per-frame CPU state、graph resources、GPU consumer lifetime |

### 八级完成证据

| 证据深度 | 能证明 | 不能证明 |
|---|---|---|
| Task launched | 工作进入可运行依赖图 | task 已完成 |
| Task completed | 对应 CPU 生产结束 | 局部结果已合并 |
| Data merged | 局部结果进入统一容器 | 消费入口已发布 |
| Data published | 消费者可读取当前版本 | RDG producer 已执行 |
| RDG work declared | 图中已有生产、消费和顺序关系 | RHI 已录制 |
| RHI recorded | CPU 侧 RHI 工作形成或封口 | Platform Queue 已接管 |
| Platform submitted | Queue 已接管对应工作 | GPU 越过最后消费者 |
| GPU consumed | 指定消费者越过完成点 | 其他 Queue 或全设备完成 |

本章主要讨论前五级。后三层只作为完成性边界引用，具体模型属于第 03、04 章。

## 本篇边界

本章只讲 PrePass/BasePass 之前的这段早期主线：从 `FScene` 与 `FViewInfo` 提供本帧输入开始，到每个 view / pass 拿到稳定的 per-pass CPU/draw work 入口，并且动态物体的 GPUScene 身份已经发布为止。

下面这些会**出现**，但本章只点到它们和帧初始化输出的依赖关系：

- DepthPrepass 如何写深度、本帧 HZB 如何构建 -> 第 09 章
- BasePass 如何写 GBuffer -> 第 10 章
- Nanite / Lumen / VSM 的内部算法 -> Part 4
- GPUScene 的 buffer layout、MeshDrawCommand 的生成细节 -> 第 06 / 07 章
- RHI 后端提交细节 -> 第 04 章

读完本章，你应该能自己回答这些问题：

- `OnRenderBegin` 怎样先立起本帧的早期任务边界？
- `BeginInitViews`、`FinishGatherDynamicMeshElements`、`EndInitViews` 三道边界各自**等什么、保证什么**？
- 老资料里的 `ComputeViewVisibility`，在 UE5.7 里为什么更应该理解成一条任务流水线？
- 默认的 frustum cull 为什么选择连续 primitive 数组扫描，八叉树处在什么位置？
- `PrimitiveVisibilityMap` 为什么先作为 CPU 侧 broad-phase 结果，而不是把这一层 visibility 全部交给 GPU 当帧产出？
- occlusion cull 在本章只**消费**什么历史证据，为什么本帧 HZB 的构建被推到第 09 章？
- 动态 mesh 的收集与合并、dynamic collector/range、GPUScene RDG producer、GPU 侧资源窗口，为什么要拆成不同阶段？
- GPUScene 动态上传、scene uniform 更新、`BeginDeferredCulling` 为什么必须按这个顺序发生？

---

## 读前约定：每个名字都落回四个问题

后文会出现不少 UE 名字，但它们都可以落回四个问题：

1. **场景里有没有这个对象？**  
   对砖墙来说，答案通常是 `FScene` 里已有长期 primitive 记录；对火焰来说，场景里有 proxy，但本帧几何还要临时生成。

2. **这个 view 认为它有什么命运？**  
   `PrimitiveVisibilityMap` 是按 primitive index 排列的一组 yes/no 标记。这里说的 bit，可以先理解成“这个 primitive 在当前 view 里是否还留在候选集合中”。它只是几何/遮挡后的候选标记；relevance 才继续回答“它要进 BasePass、DepthPass，还是只是需要动态收集”。

3. **CPU 侧有没有给某个 pass 准备好入口？**  
   静态砖墙可能复用 cached command；动态火焰要先经过 GDME 得到本帧 `FMeshBatch`，再由 pass setup 整理成 per-pass 入口。

4. **GPU 侧从哪里读到它的数据？**  
   对动态火焰来说，有 `FMeshBatch` 还不够。shader 和 Instance Culling 需要 GPUScene id / offset 与 scene uniform 发布，才能读 transform、bounds、instance 数据。

所以，`ViewPacket` 属于任务等待和 view 级生产进度，GDME context 属于动态几何收集现场，GPUScene offset 属于 GPU 数据寻址。读 08 时始终把名字放回这四个问题，后面的流程就会保持成一条状态链，避免散成一串阶段名。

---

## 阶段门 0：`OnRenderBegin` 组织 Scene publication 与前置依赖

> **阶段门护栏**：`OnRenderBegin` 组织 Scene update、prerequisite 与 visibility launch；它不是全局 blocking sync，也不证明 visibility 已完成。`FScene::Update()` 在这里建立的是 Renderer Scene publication，不是 command-list 或 Platform Queue Submit。


整章的第一步发生在 `Render()` 很早的位置。`OnRenderBegin` 真正要解决的问题，是一个时序问题：

> 在可见性任务**并行开始读数据之前**，哪些场景状态必须已经稳定？

为什么这一步不可省？设想一下如果跳过它会怎样：如果排队的 scene changes 尚未被 `FScene::Update()` 吸收并发布，砖墙可能还没进入 `FScene` 的连续数组，它的静态 cached command 任务可能还没排好；view 的遮挡历史也可能还停留在上一帧。此时如果让 frustum/relevance 任务开始读数据，结果不是"少画一个 draw"，而是**竞态**或**读到旧状态**——这类 bug 极难复现，因为它依赖任务调度的偶然顺序。

所以 `OnRenderBegin` 更像帧早期的调度窗口：它先把可见性任务即将读取的 scene/view 输入稳定下来，再把可以并行的 feature task 挂到当前帧。按职责可以分成三类：

| 职责 | 它先稳定什么 | 为什么必须早于 visibility |
| --- | --- | --- |
| 稳定共享场景账 | 把上一段积压的渲染回调和 scene update 推进到可读状态：primitive 增删改要落到 `FScene`，static mesh cache / cached command 相关状态要么已经可读、要么已经安排好安全的异步更新 | frustum / relevance 任务接下来会热遍历 `FScene`、static mesh 和 cached command 状态；如果共享账还在变，worker 读到的就可能是旧状态或竞态状态 |
| 推进 view state | 把当前 view 的矩形、view uniform 输入、scene texture 配置、遮挡帧计数和历史可用性推进到本帧 | occlusion 和 relevance 后面要依赖这些 per-view 条件；如果 view state 没前进，任务可能拿上一帧的遮挡历史或未就绪的 view uniform 状态做判断 |
| 启动早期 feature tasks | 让需要当前 scene/view 快照的系统先拿到起跑条件：有些准备纹理或表，有些启动后续 pass 前要等的异步工作，有些只是登记查询或流送任务 | 这些任务可能与 visibility 并行，也可能在 PrePass/BasePass 前被消费；它们需要绑定到当前帧输入，但不会合并产出一份最终可见列表 |

表里的 feature 名字只是具体例子。exposure LUT、light-function atlas 这类任务是在准备后面 shader 会读的小表或纹理；VSM、Lumen、Nanite、ray tracing gather 这类任务是在拿当前 scene/view 状态启动自己的可见性、场景更新或查询工作；streaming 和 virtual texture 相关任务是在为后续资源可用性铺路。它们共同说明的是同一件事：`OnRenderBegin` 让早期消费者拿到当前帧的稳定输入，feature 自己的最终渲染结果则在后续阶段产出。

这张表可以再压成三类判断：共享场景账和 view state 属于**visibility 启动前必须稳定**的输入；很多 feature task 是**拿到当前帧状态后可以并行启动**的工作；另一些结果则要等后续 PrePass/BasePass 或专门 pass 才会消费。看到“启动了一个早期任务”，先把它放到这三类里：它是 visibility 输入、并行工作，还是后续 pass 的输入。

换成流程图，它应该读成职责转移，而不是函数调用背诵：

```text
共享场景账稳定
  -> view state 前进到本帧
  -> visibility packet / feature task 启动
  -> BeginInitViews 消费这些已就绪的输入
```

这条职责链解释了一类典型 bug 的症状。如果这里顺序错了，你看到的通常不是“某个 pass 少画一个 draw”这么直接，而是后续系统读到了未初始化的 view state、旧的 scene texture 配置、还没建好的 static mesh cache，或者还没绑定到当前帧的任务依赖。也就是说，早期窗口的 debug 问题不是“哪个函数有没有被调用”，而是 `FScene` / static mesh 这类共享场景账、`FViewInfo` 这类 view 状态、以及早期 feature task 的依赖关系，是否已经在消费者读取前稳定。

所以，当你调试“早期阶段某个数据没准备好”时，先把异常数据说具体：是砖墙的 static mesh cache 还是旧的？是 view uniform / view rect 没准备好？还是某个早期系统的任务还没跑完？然后按生产者和消费者来分：

1. 如果它会被 visibility / relevance / GDME 直接读取，比如 `FScene` 里的 primitive 列表、static mesh cache、view rect、view uniform、遮挡历史，那么它必须在这些任务启动前稳定；否则 worker 读到的就是旧状态或半成品。
2. 如果它只是 exposure、VSM、Lumen、Nanite 查询、streaming 这类早期 feature task 的结果，就要再问它是不是 visibility 的输入。很多早期任务只是“拿到当前帧状态后可以并行启动”，并不代表它已经给 visibility 产出最终答案。
3. 最后看真正的读取者在什么时候出现：`BeginInitViews` 之后的任务只能读已经稳定的 view / scene 输入；`FinishGatherDynamicMeshElements` 之后才能相信动态 mesh 局部结果已经完成并合并；随后 GPUScene / Scene Uniform / `BeginDeferredCulling` 建立图内读取合同；`EndInitViews` 再关闭 CPU visibility task graph。不要用最后这一步反推 GPU producer 或 culling 已执行完成。

这套问法的目的不是让你背分类名，而是避免把所有早期问题都归成“InitViews 没跑完”。同样是“数据没准备好”，有的该查 visibility 启动前的场景账，有的该查 view state 初始化，有的该等到 `EndInitViews` 后再判断后续 pass 是否真的拿到了稳定入口。

## 阶段门 1：`BeginInitViews` 启动 per-view 工作，但不保证完成

`BeginInitViews` 这个名字很容易误导人。它在教学上更像给本帧早期任务发出一个开工许可：**当前 view 输入已经立住，visibility / GDME 等生产者可以开始读取；它们的结果会在后面的边界陆续交回。**

先看动态火焰。GDME（Gather Dynamic Mesh Elements）可以先按字面理解成“向动态 proxy 索要本帧几何候选”的工作。火焰的这个工作一旦开始，proxy 可能立刻会问当前 view：相机矩阵是什么，屏幕范围是多少，立体渲染时这个逻辑 view 要展开成几个硬件 view，特效系统或渲染扩展有没有提前准备好的 view 侧资源。注意，这些问题都不是“火焰最后画不画”的答案；它们只是火焰生产本帧 `FMeshBatch` 时要读的输入。

这也解释了 `BeginInitViews` 的顺序：先把 GDME 会读取的 view 资源立住，再放动态收集开跑。假设火焰的 proxy 已经在并行任务里跑了，它要读当前 view 的参数包；同时 FX 或渲染扩展还在准备同一份 view 资源。此时出问题的核心是 worker 可能碰到一份还没建完的 GPU 侧资源。这里的 view uniform buffer 可以先理解成“shader 和部分收集逻辑读取当前 view 参数的统一参数包”；RHI resource 则是这份参数包背后的 GPU 资源句柄和存储。`BeginInitViews` 先把这类输入立住，再允许动态收集开始，就是为了让并行发生在稳定输入之后。

如果你去源码里定位，会看到 `PreVisibilityFrameSetup`、early view uniform、instanced stereo、FX system、view extension 这些名字。它们在本节的教学模型里都落到同一个角色上：把后面 visibility / GDME 会读取的 view 侧输入准备到安全可读。有的名字来自显示开关，有的来自立体 view 展开，有的来自特效或扩展系统提前读取 view 参数的需求；它们是同一个时序约束下的不同来源。

然后 `BeginInitViews` 才启动动态 mesh 收集通道，也就是 `StartGatherDynamicMeshElements`。这里的“启动”很关键：它只是让动态收集任务可以开始调用 proxy，不是说所有动态 proxy 已经把 mesh 交回来了。UE 要这么早启动，是为了把火焰这种可能很重的 `GetDynamicMeshElements`，和阴影准备、ray tracing gather、GPUScene 更新前置工作叠起来跑。如果非要等所有 visibility 都同步完成，再串行向每个动态 proxy 要几何，PrePass 前面就会多出一大段纯等待。

这时再看静态砖墙，它的状态就简单得多。砖墙已经在 `FScene` 的静态账里，也有机会复用第 07 章讲过的 cached command；`BeginInitViews` 之后，它可以被 visibility / relevance 任务扫描。但它还没有“自动进入 BasePass”，因为 relevance 还要把它分到具体 pass，并把可复用的 cached command 包成本 view / 本 pass 的入口。

火焰则只是刚拿到开工机会。动态收集任务可以开始调用 proxy，产出本帧 `FMeshBatch`；但这些 `FMeshBatch` 可能还分散在 worker 的临时 context 里，没有统一提交，也没有被 pass setup 分配到 DepthPass 或 BasePass。换句话说，`BeginInitViews` 之后火焰从“等待被生产”变成“可以开始生产”，还没有变成“已经可提交”。

最后，`BeginInitViews` 还会继续初始化 sky/view 相关资源，并推进 render-thread 上已经排好的可见性任务。源码里的 `ProcessRenderThreadTasks` 也要按这个意思读：它是在推进已经满足前置条件的任务，不是在宣布 visibility、GDME、pass setup 都已经完成。

所以跨过 `BeginInitViews` 之后，最准确的状态是：

```text
view / scene 输入: 已经足够稳定，可以被早期任务读取
静态砖墙: 可以进入 visibility / relevance 扫描，但 pass 入口还要后续形成
动态火焰: GDME 可以开始索要本帧 FMeshBatch，但结果还没有统一提交
整帧流水线: 已经开跑，尚未交账
```

从这里往后，火焰还要顺着一条很具体的生产链继续变稳定：

```text
relevance 只留下“需要动态收集”的待办
  -> GDME worker 调用 proxy，产出本帧 FMeshBatch
  -> FinishGatherDynamicMeshElements 把 worker context 统一交账
  -> 启动 MeshPassSetup；它与后续 frame setup 继续并行建立 DepthPass/BasePass 等入口
  -> dynamic collector / range 冻结本帧 GPU 可寻址身份范围
  -> GPUScene RDG producer 声明生产，scene uniform / view data 暴露目标资源窗口
  -> BeginDeferredCulling 打开条件化 deferred batching context
  -> MeshPassSetup / BuildRenderingCommands 把 per-pass batches 交入 context
  -> EndInitViews 等待 MeshPassSetup 收口
```

这条链说明了一个调试判断：如果断点停在 `BeginInitViews` 刚过的位置，此时已经可信的是“worker 读取 view 输入是安全的”。火焰的稳定 `View.DynamicMeshElements` 要等 `FinishGatherDynamicMeshElements`，DepthPass/BasePass 的火焰入口要等 pass setup，GPUScene offset、scene uniform 和 Instance Culling 读取入口则由 GPUScene upload、scene uniform 更新和 `BeginDeferredCulling` 继续补齐。

所以 `BeginInitViews` 的教学含义是“早期生产者可以安全开工”。它守住的是起跑前的输入稳定性；后面几道边界守住的，才是动态几何提交、pass 入口形成、GPU 身份发布和实例剔除读取安全。

## 可见性流水线：每个 view 局部生产，整帧统一收口

`BeginInitViews` 之后，UE 已经把早期任务放出去跑了。接下来真正需要理解的是两层进度怎样同时存在：

```text
单个 view 的局部进度:
  这个 view 已经完成 frustum / occlusion / relevance 了吗?
  这个 view 里的砖墙是否已经登记到 BasePass 的静态入口?
  这个 view 里的火焰是否已经留下 GDME 待办?

整帧的共同进度:
  所有相关 view 都交出自己的 visibility / relevance 结果了吗?
  动态 mesh 收集是否已经统一提交?
  per-pass 入口是否已经整理到后续 pass 可以消费的形态?
```

这两个问题必须分开看。主视图可能已经把砖墙筛出来，并且把它的 cached command 包成 BasePass 入口；同一帧里，一个反射 view、阴影视图或动态火焰的收集任务仍然可能在路上。此时主视图的局部结果已经有调试价值，但整帧还没有跨过“所有生产者都收口”的边界。PrePass / BasePass 需要的不是某一个 view 的局部成功，而是它要消费的 per-view、per-pass 输入都已经稳定。

源码里的 `FVisibilityTaskData` 和 `ViewPacket` 可以放到这个模型里读：`ViewPacket` 是某个 view 的局部生产包，用来承载这个 view 的输入、临时 bit、relevance 结果和等待点；`FVisibilityTaskData` 是 frame 级的收口控制账，用来判断所有 view packet 与共享 setup 什么时候一起到达边界。它们的教学位置是定位“哪条生产线还没交账”的源码锚点，先理解局部生产与整帧收口，再记名字。

把这个模型放回双 view 场景，会更清楚：

```text
主视图:
  砖墙通过 frustum / occlusion
  relevance 已经把它接到 BasePass 静态入口
  这个 view 的局部路径已经能交出一部分结果

反射或阴影视图:
  可能还在算 relevance
  也可能还在等动态火焰的 GDME submit
  frame 级边界继续等待这条生产线

整帧收口:
  等所有需要参与本轮消费的 view 和共享 setup 都交账
  后续 pass 才按稳定入口读取
```

调试时先问“卡住的是某个 view 的局部生产，还是 frame 级共同收口”。如果主视图已经有 visibility bit，但 `EndInitViews` 之前仍然停住，优先沿未完成的 view 或共享 setup 查：它是在 frustum / occlusion，relevance，GDME submit，还是 mesh pass setup 的等待点上。这样查的是生产进度，而不是反复盯着一份已经完成的局部 bit。

OUTLINE 沿用了 `ComputeViewVisibility` 这个传统名字。放到 UE5.7 的本地路径里，它更适合作为“可见性相关流水线”的入口称呼。流水线内部可以先压成两层：

```text
按 view 各自跑:
  FrustumCull        几何上可能可见吗?
  OcclusionCull      历史遮挡证据是否保守地修正候选?
  ComputeRelevance   它对哪些 pass 或动态路径有贡献?

跨所有 view / 共享 setup:
  GatherDynamicMeshElements   动态物体交出本帧几何
  SetupMeshPasses             组织 per-pass CPU/draw work 入口
```

它的输入来自两边：`FScene` 给出场景对象账，`FViewInfo` 给出当前 view 的条件。这里不用把字段背成清单，只要知道每类输入回答什么问题：

| 输入来源 | 正向作用 | 调试意义 |
| --- | --- | --- |
| `FScene` 的对象账 | 提供 primitive index、bounds、proxy，以及静态 mesh / cached command 这类已经注册到 Renderer 的绘制准备状态 | 物体根本没有进入本帧候选时，先确认 scene 账和静态缓存是否已经稳定 |
| `FScene` 的辅助证据 | 提供可选空间索引、预计算可见性、GPU skin 等筛选或变形前置状态 | 它们帮助本帧筛选或准备输入；真正的 pass 入口和 GPUScene 动态身份由后续阶段补齐 |
| `FViewInfo` 的当前视图条件 | 提供 view 视锥、view rect、stereo 信息、hidden/show-only 集合、show flags 等 view 级条件 | 某个 view 下消失、另一个 view 正常时，优先检查这类 per-view 条件 |
| `FViewInfo` 的历史遮挡证据 | 提供上一帧 query / HZB 历史和遮挡帧计数 | occlusion 在这里读取历史证据，后文第 09 章才处理本帧 depth / HZB 的新结构 |

流水线的输出也按正向状态来记。UE 在同一批 primitive index 上逐层追加答案，把“几何候选”“pass 去向”“消费入口”分开交给后续系统。对调试最有用的是三类状态：

| 正向问题 | 状态含义 | 典型源码锚点 |
| --- | --- | --- |
| 这个 primitive 还活在当前 view 的候选里吗? | 几何和历史遮挡筛选之后，它仍然值得进入 relevance；遮挡证据更确定时，还会留下 unoccluded 相关 bit | `PrimitiveVisibilityMap`、`PrimitiveDefinitelyUnoccludedMap` |
| 它接下来走哪条命运? | 静态砖墙可能接到 cached command，也可能登记 one-frame build；动态火焰先留下 GDME 待办，等后面真正收集本帧几何 | `PrimitiveViewRelevanceMap`、`ViewCommands.MeshCommands[Pass]`、`DynamicMeshCommandBuildRequests[Pass]`、GDME 标记 |
| 后续 pass 有没有可消费入口? | 动态 mesh 已经提交后会进入 view 的 dynamic element；pass setup 收口后，目标 pass 才有 CPU 侧入口 | `View.DynamicMeshElements`、`View.NumVisibleDynamicMeshElements[Pass]`、`View.ParallelMeshDrawCommandPasses[Pass]` |

把砖墙和火焰放进去看，同样一个 visibility bit 会通向两条不同生产链：

```text
砖墙 visibility bit 为真
  -> relevance 判断它对 DepthPass/BasePass 是否有贡献
  -> cached command 条件满足时，包装成当前 view/pass 的静态入口
  -> 条件需要本帧重新决策时，登记 dynamic build request

火焰 visibility bit 为真
  -> relevance 留下“后续需要动态收集”的待办
  -> GDME 调用 proxy，得到本帧 FMeshBatch
  -> FinishGatherDynamicMeshElements 统一提交这些 FMeshBatch
  -> pass setup 再把它们接到 DepthPass/BasePass 等入口
```

这就是 UE 和 Unity 读法最容易分叉的地方。砖墙没进 BasePass 时，visibility bit 只是第一问；后面还要看 relevance、cached command wrapper、build request 和 pass setup。火焰没进 BasePass 时，先确认它是否只停在“需要 GDME”的待办上，再继续查本帧 `FMeshBatch`、动态 pass relevance、GPUScene dynamic range 和 culling offset。调试时倒查的是这几类状态的交接。

## Frustum Cull：建立 View 候选，具体扫描或空间加速取决于条件

流水线的第一站是 frustum cull，它回答最基础的问题：**砖墙或火焰，在几何上有没有可能落入当前 view 的视锥？**

先把两个名字放平。

**视锥**就是当前摄像机能看见的空间体积。它不是一张屏幕矩形，而是从相机向远处张开的一个截头金字塔。frustum cull 做的第一件事，就是拿每个 primitive 的 bounds 去问：这个包围盒有没有可能和视锥相交？完全在外，就可以先从本 view 的候选里剔掉。

**八叉树**是一种空间索引。它把一块 3D 空间递归切成 8 个子盒子，每个 node 代表一块空间，node 下面挂着落在这块空间里的 primitive。它想解决的是“不要每次都逐个问所有 primitive”这个搜索问题：如果一个 node 的大盒子整个在视锥外，那它下面的一批 primitive 可以整组跳过；如果一个 node 整个在视锥内，它下面的 primitive 可以少做一部分精确测试。


为什么 UE 默认走连续数组，而不是默认走八叉树？设计理由和第 02 章的铺垫一脉相承：`FScene` 已经把 primitive 放进了连续数组，`PrimitiveVisibilityMap` 又是按 primitive index 写 bit。也就是说，本章这一步天然适合按连续 index 切任务：worker 拿一段 primitive，连续读 bounds，连续写 bit。它的好处是内存访问简单、并行切分直接、结果形态正好接到后面的 visibility / relevance。八叉树则会引入 node 遍历、父子层级状态、primitive 到 node 的映射；当后续结果最终仍要回到 primitive bit 时，这些层级跳转不一定比平铺扫描划算。

这里还要补上一个 CPU/GPU 分工判断，因为这正是读者最容易疑惑的地方：frustum cull 的数学形态看起来很适合 GPU 并行，为什么本章这张 `PrimitiveVisibilityMap` 仍然先在 CPU 侧形成？

先看这张 bit 后面要喂给谁。`PrimitiveVisibilityMap` 产出后，CPU 马上要沿着它继续做 relevance：砖墙要判断是否能包装 cached MeshDrawCommand，火焰要判断是否留下 GDME 待办，后面的 `SetupMeshPass` 还要整理 CPU 侧 per-pass 入口。也就是说，这张 map 在 08 章的位置不是“最终 GPU 提交参数”，而是 CPU 继续组织本帧生产链的 broad-phase 候选账。把它留在 CPU 侧，后续 relevance / GDME / pass setup 可以直接读同一套 primitive index 和 bit，不需要在这一帧早期插入 GPU -> CPU 的强制交接。

换成四种生产-消费关系就更清楚：

| 生产和消费关系 | 在本章或相邻章节里的例子 | 设计判断 |
| --- | --- | --- |
| CPU 生产，CPU 当帧消费 | frustum / occlusion 修正 `PrimitiveVisibilityMap`，relevance 和 GDME 待办立刻读取 | 适合放在 CPU broad-phase；结果形态直接接后续 CPU 任务 |
| GPU 生产，GPU 继续消费 | 07 章的 Instance Culling 生成 instance id / indirect args，后续 draw 继续用 | 适合留在 GPU / RDG 提交链，不需要当帧读回 CPU |
| GPU 生产，CPU 延迟或保守消费 | 上一帧 query / HZB 历史在本帧 occlusion cull 中作为证据 | 可以用作历史反馈；证据不足时保留可见，不把正确性压在当前帧读回上 |
| GPU 生产，CPU 当帧必须消费 | 如果把本章 `PrimitiveVisibilityMap` 先交给 GPU，再要求 CPU 立刻拿回来做 relevance / GDME / pass setup | 容易形成硬同步点：CPU 早期流水线要等 GPU dispatch 完成和结果交接，后续 CPU 生产链被迫停住 |

所以这里不是“CPU 比 GPU 更适合做所有 culling”，而是“这一层结果的下一位消费者是 CPU”。UE 仍然会在别的位置使用 GPU 侧剔除：比如 07 章的 Instance Culling、Nanite 的 GPU-driven 路径、以及本节后面讲到的历史遮挡反馈。差别在于那些结果要么继续留在 GPU 侧消费，要么延迟到后续帧当作保守证据；本章这张 `PrimitiveVisibilityMap` 则要当帧推动 CPU 侧的 relevance、动态收集和 pass setup。

这不是说八叉树没有价值。它仍然存在，只是定位为**可选加速器**，适合在空间分组能明显跳过大量 primitive bounds 测试时使用。打开 octree 路径后，系统会先给每个 octree node 写上“完全在视锥内 / 部分相交 / 完全在外”的状态；完全包含的 node 可以让其下 primitive 跳过一部分 bounds 测试，部分相交的 node 仍要回到 primitive bounds 级别精确判断。它的遍历可以这样看：

```text
八叉树节点
  -> 测 node bounds 与 view frustum
  -> 完全在内: 标记 inside，子节点和 primitive 继承 inside
  -> 部分相交: 标记 visible + 部分在外
  -> 完全在外: 整个子树剪掉

连续数组扫描
  -> 查 primitive 所属 node 的状态位
  -> node 完全在内: 不再测 primitive bounds 的视锥相交
  -> node 部分相交: 回到 primitive bounds 做精确测试
  -> node 不可见: primitive 直接判不可见
```

关键认识是：**八叉树减少的只是"primitive bounds 与视锥的重复测试"，它没有改变输出形态。** 无论开不开 octree，最后写的都还是同一张 `PrimitiveVisibilityMap`。这也是为什么即使启用了 octree，后面的 occlusion、relevance、GDME、pass setup 仍然从 primitive 的连续索引和 visibility bit 出发，而不是沿着 octree node 继续往下传递——八叉树是一次粗判，不是一条贯穿后续的主线。

从“替代方案”角度看，这里至少有三种思路：

| 方案 | 它怎么找候选 | 优点 | 代价 / 边界 |
| --- | --- | --- | --- |
| 连续数组扫描（UE 默认） | 按 primitive index 分块，并行测试 bounds 与视锥 | 数据连续、任务好切、结果直接写 visibility bit | 仍然要检查大量 primitive bounds，只是靠并行和缓存友好摊薄成本 |
| 八叉树 | 先测试空间 node，再批量跳过或继承 node 结果 | 空间上成片不可见时，可以整组剪掉 | 要维护树和 primitive-node 映射，遍历有层级跳转，最后仍要落回 primitive bit |
| sphere-first / fast intersect 这类测试优化 | 不改变候选组织，只改变单个 bounds 测试的成本 | 可作为小粒度性能开关 | 解决的是“单次测试便宜一点”，不是“少找一批 primitive” |

还有别的空间或批量剔除思路，但这里不需要把它们背成名词表。包围体层级（例如 BVH）解决的是“能不能用一棵层级 bounds 树整组跳过物体”；固定格子或屏幕 tile 解决的是“能不能先把世界空间或屏幕区域分桶，再只查相关桶”；GPU culling 解决的是“当结果可以继续留在 GPU 侧消费时，能不能把大量筛选工作推给 GPU 并行做”。它们各自服务不同规模、不同数据形态和不同消费位置的搜索问题，但都不是本节 UE5.7 CPU primitive frustum cull 的默认主线。这里要抓住的取舍是：**UE 本章这一步默认押在连续 primitive 数组 + 位图输出 + 并行扫描上；八叉树只是可选地减少 bounds 测试，GPU culling 则适合结果不需要当帧回到 CPU 继续分 pass 的路径。**

**无论开不开八叉树**，单个 primitive 要过的"门"大致是同一组，按顺序是：

1. hidden / show-only 的显式过滤：当前 view 可以强制排除或只显示某些 primitive，调试时先确认它有没有被 view 自己点名过滤掉；
2. zero bounds 或非法 bounds 过滤：bounds 是几何可见性测试的输入，如果包围盒本身无效，后面的视锥判断就没有意义；
3. HLOD 的可见性接管：层级代理可能代表一组物体，也可能让原 primitive 暂时 hidden，所以它影响的是“这一层由谁代表”；
4. 自定义 culling、可选的 sphere test、视锥-box 测试：项目或引擎可以先加自己的过滤，再做 bounds 与视锥的几何相交判断；
5. distance cull 与 LOD fade 标记：距离太远可以直接剔除，LOD fade 则把淡入淡出的状态留给后续使用；
6. 写入 `PrimitiveVisibilityMap`，必要时写 ray tracing visibility map：前者给 raster/main view 流水线继续用，后者给 ray tracing 侧的可见性路径作并行标记。

**调试落点：** 砖墙如果不在 `PrimitiveVisibilityMap` 里，倒查顺序应该是 hidden/show-only → bounds → HLOD → 视锥/距离。材质或 RHI 属于更晚的绘制阶段；在当前状态还没通过几何候选时，优先沿候选生产链查。只有在你确认 `UseOctree=true` 时，才值得把 octree node 状态当成主线来查——先看 primitive 所在节点是不是被整块剪掉了；如果节点只是部分相交，问题仍然要回到 primitive bounds、custom culling、HLOD 或距离裁剪。在默认路径下，"八叉树没遍历到它"并不是主要怀疑方向。

## Occlusion Cull：消费条件化历史证据，不假定本帧 HZB 已成立

frustum cull 之后，occlusion cull 只处理那些"几何上仍然可能可见"的 primitive。它的职责，是用**历史证据**保守地修正 `PrimitiveVisibilityMap`——证据来自预计算遮挡、硬件 query、HZB query 历史，或移动端的反馈。

先把 `query` 和 `HZB` 也放平。**硬件 occlusion query** 可以理解成 GPU 在上一段渲染里帮你数过一次：“这个 bounds 对应的东西有多少像素通过了深度测试？”CPU 本帧拿到的通常是之前提交的结果，所以它是历史证据。**HZB** 是 Hierarchical Z-Buffer，可以先理解成一份多级深度金字塔：底层接近原始深度，越往上越粗，每一级把一片屏幕区域的深度范围压缩起来。它解决的是“能不能用较粗的屏幕深度证据快速判断一个 bounds 大概率被挡住”的问题。

这段里确实有 GPU 生产、CPU 消费的证据，所以要把 readback 说准。readback 的成本不只有“搬几个字节慢不慢”，更关键的是它可能让 CPU 等 GPU、或让 GPU 的提交顺序被 CPU 读取需求牵住。UE 在本章用遮挡证据时，避免的是“CPU 当帧必须等 GPU 给出当前帧答案”这个硬依赖：它读的是 view state 里保留下来的旧 query / HZB / feedback 结果，并且这些结果带有缓冲帧、滞后容忍和有效性判断。

Occlusion 还必须按 View 生命周期与路径条件理解：有持久 `ViewState` 的主 View 才可能复用相应历史；Camera Cut、临时 View、SceneCapture 或没有有效 ViewState 时，历史可以失效、被忽略或走保守回退。Hardware Query 与 HZB 是不同证据路径，支持条件、提交方式和历史有效性不能互相代替。

因此，occlusion cull 的生产-消费关系不是“GPU 本帧算完，CPU 本帧立刻等结果，然后才能继续 relevance”。更准确的链条是：

```text
之前的帧:
  GPU 对一批 bounds 做 query 或 HZB 测试，结果进入 view state 的历史证据

当前帧 InitViews:
  CPU 在 frustum cull 之后读取这些历史证据
  证据足够可靠 -> 保守地清掉一部分 PrimitiveVisibilityMap bit
  证据缺失、太旧、被忽略或条件不适合 -> 保留为可见，并可能提交新的 query 给未来帧
```

这就是为什么它可以和前面 `PrimitiveVisibilityMap` 的 CPU broad-phase 共存：`PrimitiveVisibilityMap` 是本帧 CPU 后续生产链的当帧输入；query / HZB 是 GPU 侧较早生产、CPU 本帧延迟参考的历史证据。前者如果改成 GPU 当帧产出，会卡住 relevance / GDME / pass setup；后者即使读不到可靠证据，也可以把 primitive 留在候选里继续往下走，只是少了一次遮挡优化。

这里要把本章的 HZB 放回正确位置：08 里的 occlusion cull 消费的是上一帧遗留下来的遮挡测试结果和历史。它把 HZB 当成历史证据来源之一；本帧 `SceneDepth -> Resolve -> HZB` 的构建，以及“遮挡是否能在 BasePass 之前使用本帧深度证据”这类讨论，属于第 09 章。

可以把这里的遮挡证据当成一张“历史护照”：

| 历史证据 | 本帧能做什么 | 边界 / 后续阶段 |
| --- | --- | --- |
| 预计算可见性 | 在条件匹配时直接影响 visibility bit | 它证明的是历史或预计算条件匹配，当前帧新深度由后续 depth pass 产生 |
| 上一帧 query / HZB 历史 | query 给出上一批 GPU 遮挡测试结果；HZB 历史给出上一帧深度金字塔上的遮挡判断依据 | 它们是延迟反馈，不是当前帧必须等待的 GPU 答案；第 09 章负责本帧 `SceneDepth -> Resolve -> HZB` |
| view state 的开关和帧计数 | 决定旧证据是否可信、是否提交新 query、是否忽略旧 query | `FMeshBatch` 和 pass command 由后面的 GDME / pass setup 生产 |

例如静态砖墙上一帧被可靠证据判定为遮挡，本帧 occlusion cull 可以保守地清掉它的可见 bit；如果相机切换、旧 query 被忽略、或证据不足，它就应该继续留在 `PrimitiveVisibilityMap` 里。这里的原则始终是：历史证据负责保守修正本帧 visibility，本帧 HZB 则在后续 depth/HZB 阶段生成。

它的工作分几层。先决定本帧是否允许遮挡 query：也就是本帧是否继续向 GPU 提交“请你帮我测这些 bounds 是否被挡住”的请求。这个判断要综合当前 view 是否适合相信或提交遮挡证据：

- 调试、截图、拾取相关 view 可能禁用或忽略遮挡证据，因为这类 view 更关心完整性、可选中性或特殊输出，而不是普通主视图的遮挡性能；
- 碰撞 view、hit proxy view、show flag 等特殊视图条件可能改变“哪些东西应该参与遮挡”的语义；hit proxy 这类路径服务的是选中/拾取，不是普通颜色渲染；
- 平台能力和 HZB / query 支持情况决定某类 GPU 遮挡证据能不能走；所谓强制模式只是绕过普通开关去启用某条遮挡路径，不等于本章已经构建了本帧 HZB。

前面 `OnRenderBegin` 里的视图状态准备，已经在启动可见性之前更新过这些开关，并推进了遮挡帧计数器——这正是早期窗口铺垫的回报。

进入遮挡判断后：

- **第一层是预计算遮挡**：如果 view 有预计算可见性数据，且 primitive 声明自己可被预计算遮挡影响，就可以直接把不可见的 bit 清掉。
- **之后才是 GPU 遮挡**：它的原则是**保守**——证据不足时宁可保留为可见，避免漏画。漏画是肉眼立刻能看到的故障，多画只是浪费，两害相权取其轻。

HZB 路径是否启用取决于平台能力、渲染路径、ViewState 和运行配置；Hardware Query 路径也有自己的支持与历史条件。无论哪条路径，本章消费的是可用的历史证据，并可能登记后续测试工作；从本帧 scene depth 构建 HZB 金字塔仍属于后续 depth/HZB 阶段。

对火焰和砖墙来说，occlusion cull 交出的状态是 visibility bit 和 unoccluded bit 的修正。后续的 `FMeshBatch`、draw command 和 per-pass 入口，会分别由 GDME、command build / pass setup 继续生产。

## Compute Relevance：把"可见"切成"哪个 pass 的输入"

一个 primitive 通过 frustum / occlusion 之后，UE 已经知道它在这个 view 里值得继续处理。`ComputeRelevance` 接手的是下一层问题：**这个物体会给哪些后续 pass 或生产线提供输入？**

可以把它想成 per-view 的分拣台。输入是一批仍然活着的 primitive；输出是几条去向明确的后续账：

```text
可见 primitive
  -> 静态路径: 复用或现场生成当前 pass 的 command 入口
  -> 动态路径: 留下“后续需要 GDME 收集本帧几何”的待办
  -> view relevance 记录: 保存这个 primitive 对当前 view 的路径贡献
```

砖墙在这里走静态路径。它的 mesh、材质和第 07 章讲过的 cached MeshDrawCommand 早已在场景注册或更新阶段准备过；这一帧 relevance 要做的是确认：当前 view 和目标 pass 是否允许复用那条稳定配方。如果允许，砖墙会被包装成当前 view / 当前 pass 的可见入口；如果当前 view 条件让缓存配方不合适，UE 会登记本帧现场 build command 的请求。这里的设计收益很直接：静态物体昂贵的 shader、PSO 线索、绑定和排序键决策大多已经提前摊掉，本帧只做“挑出来、接到这个 pass”。

火焰在这里走动态路径。relevance 此时已经能判断“这个动态 primitive 对当前 view 有贡献”，于是它给后面的 GDME 留下一项待办：稍后要向这个 proxy 索要本帧 `FMeshBatch`。这个待办的价值是让动态收集任务知道该找谁要几何；它还没有执行 `GetDynamicMeshElements`，也没有 GPUScene dynamic offset。换句话说，火焰在 relevance 之后的正向状态是“进入动态生产队列”，下一步由 GDME 生产本帧几何。

源码名词放在这个分拣模型里就不难读：

| 调试问题 | 正向含义 | 典型源码锚点 |
| --- | --- | --- |
| 当前 view 认为这个 primitive 有哪些路径贡献? | 记录它对当前 view 的 relevance 结果，供 debug view、动态 pass relevance 和后续系统参考 | `PrimitiveViewRelevanceMap` |
| 静态砖墙有没有接到目标 pass? | cached command 被包装成当前 view/pass 的入口，或者被转成 one-frame build 请求 | `ViewCommands.MeshCommands[Pass]`、`DynamicMeshCommandBuildRequests[Pass]` |
| 动态火焰有没有进入本帧几何生产队列? | relevance 已经留下 GDME 待办，后续动态收集会据此调用 proxy | GDME 待收集标记 |

具体排查时，把问题也按这三类问。砖墙在 `PrimitiveVisibilityMap` 里为真但 BasePass 没有 draw，先看 relevance 是否给了 BasePass 贡献、cached command wrapper 是否生成、是否转成 dynamic build request。火焰在 relevance 后最正常的状态是“等待 GDME”；如果后面缺少 `View.DynamicMeshElements`，要继续查 GDME 是否执行和 submit，而不是把缺失的 `FMeshBatch` 归因到 frustum / occlusion。

## 状态生产 B：Dynamic Mesh Elements 可以并行生产，但必须合并

真正向火焰的 proxy 索要本帧几何的，是 GDME（Gather Dynamic Mesh Elements）。

这里先把 `mesh element` 说清楚：它是 **CPU 侧临时几何候选**，本质上是 proxy 在这一帧交给 Renderer 的 `FMeshBatch` 及其范围记录。RHI draw 是后续提交阶段的事情，GPUScene 数据是后续上传阶段的事情。对火焰来说，这一步回答的是“这一帧我有哪些几何可以被后续 pass 考虑”；pass 去向和 shader 的 GPUScene 读取位置，会在后面的 pass setup 与 GPUScene upload 继续补齐。

GDME 的核心设计原则只有一句话，但它解释了整段代码的形状：

> **收集可以并行，提交必须统一收口。**

更正向地说，UE 希望 worker 各自把几何先收进局部 context，再由 render thread 在收口点统一提交。这样设计是因为收集（调用各 proxy 的 `GetDynamicMeshElements`）天然可并行，不同 proxy 之间互不干扰；提交阶段则会碰到帧级共享对象，需要统一交账。

这里的共享对象先按角色理解：

- immediate command list 是本帧向 RHI 录制/提交命令的主通道，提交动作需要在明确边界上统一进入；
- GPUScene collector 是动态 primitive 本帧身份数据的汇总箱，最后要统一交给 GPUScene upload；
- material update、dynamic vertex/index buffer commit 这类更新会写帧级共享资源，必须在 render thread 的收口点统一推进。

所以 UE 把两者拆开。支持并行 GDME 的 proxy 可以在并行渲染任务里执行；不支持并行的 proxy 会被分流到 render-thread 任务，避免不安全的并发调用。每个动态 mesh context 有自己的 collector，并以**延迟提交**模式工作——并行任务可以各自调用 proxy 收集 mesh batch，写进各自的 context，并记录每个 view 的 start/end element 范围；但 GPUScene 更新、material 更新、动态 vertex/index buffer 的 commit、RHI command list 的 submit，全部**推迟**到 context 的 `Finish` 和容器的 `Submit` 阶段统一做。

这条规则同时解决了两个问题：

- 并行 worker 各自收集 mesh batch，减少主线程等待；
- 共享资源（命令列表、GPUScene collector、材质更新）不会被多个任务同时写。

所以 GDME context 要按“局部写、统一交账”理解：worker 先写自己的动态收集现场，render thread 再把这些结果整理成后续 pass 能读的稳定输入。

| 阶段 | 谁能写 | 写入什么 | 消费边界 |
| --- | --- | --- | --- |
| 并行收集 | 支持并行的 proxy / worker context | 本帧 `FMeshBatch`、每个 view 的 element 范围、动态 primitive 的临时记录 | 这些结果先停在 worker 的临时 context，等待统一 submit 后成为 pass 可读输入；GPUScene offset 由后续上传发布 |
| render-thread 收集 | 不适合并行的 proxy / render-thread context | 同样的动态 mesh element，只是写者换成 render-thread 路径 | 写者更安全，但仍然要和并行 context 一起走统一 submit |
| context finish / submit | render thread 的收口阶段 | collector、dynamic buffer、material update、延迟的共享提交 | 这一步把动态 mesh 交给后续 pass setup；GPUScene dynamic upload 仍然是下一段的身份发布 |

这张表解释了一个常见症状：火焰的 `GetDynamicMeshElements` 已经被调用，说明“动态几何候选”进入了临时箱。接下来它还要经过 context submit，再由动态 pass relevance 判断这些新交出的 `FMeshBatch` 对 DepthPass / BasePass 等 pass 是否有贡献，然后 pass setup 才能把它们整理成 per-pass CPU/draw work 入口，最后还要等 GPUScene 动态上传把本帧 ID 发布出去。少看任何一段，都会把半成品误判成最终入口。

**对火焰来说，GDME 跑完后多了两样东西**：一组 `View.DynamicMeshElements`，和它对应的 `FDynamicPrimitive` 范围记录。后续步骤会继续判断这些 mesh element 应该进哪些 pass，并由 GPUScene 动态上传给它生成本帧的 primitive/instance ID。换句话说，火焰这时已经“有了本帧几何”，接下来还要获得“pass 去向”和“GPU 身份”。

## 阶段门 2：`FinishGatherDynamicMeshElements` 收敛动态几何

> **阶段交付**：这一阶段等待 dynamic gather 并提交各 worker context，使 `View.DynamicMeshElements` 成为 pass setup 可稳定读取的 CPU 候选。visibility 总任务、GPUScene 图内生产以及更深的 RHI / Queue / GPU 时间线各有自己的后续边界。


`FinishGatherDynamicMeshElements` 是动态几何生产线的**显式收敛点**。`BeginInitViews` 内部已经通过 `ProcessRenderThreadTasks` 等待和推进过 ComputeRelevance、FinalizeRelevance、render-thread GDME 等多类依赖，因此这里不能称为 PrePass 前第一道硬同步。它只把 dynamic gather 从“worker 仍可写 context”推进到“render thread 拥有稳定动态候选，并可启动 MeshPassSetup”。

对火焰来说，这道边界前后变化很具体：

```text
Finish 之前:
  火焰可能已经被标记为需要 GDME。
  某些 worker 可能正在调用 proxy 的 GetDynamicMeshElements。
  本帧 FMeshBatch 还可能分散在多个动态 context 里，等待统一提交后才成为 pass 可读输入。

Finish 之后:
  动态 context 被提交回 render thread。
  View.DynamicMeshElements 里有稳定的本帧动态 mesh 候选。
  后续 pass setup 可以问：这些候选应该进入 DepthPass、BasePass、Velocity 还是别的 pass?
```

源码里这道边界还会等待 virtual texture 相关任务、提交 dynamic read buffer、处理延迟的 collector / dynamic buffer / command list 等细节。正文不靠这些细节推进教学，只保留它们的共同意义：**把并行收集阶段产生的半成品，收敛成后续 pass setup 可以读取的稳定候选。**

接下来才轮到 `SetupMeshPass`。这个名字也容易误读成“把 pass 画出来”。它其实是在准备 **某个 view 的某个 mesh pass 的 CPU/draw work 入口**。换成普通话，就是问：

```text
DepthPass / BasePass 这一帧要从哪里拿 draw 候选?

1. 有没有静态砖墙这种可复用 cached command?
2. 有没有火焰这种已经收集完的动态 mesh element?
3. 有没有静态物体因为当前 view 条件特殊，不能复用缓存、需要现场 build command?
```

这三路输入各自来自不同阶段，也回答不同问题：

| `SetupMeshPass` 输入 | 来自哪里 | 它证明了什么 | 缺失时优先查什么 |
| --- | --- | --- | --- |
| 可复用静态 command | relevance 为可见 static mesh 包装 cached MDC | 砖墙这类对象已有稳定 draw 配方，本帧本 view 需要它 | visibility bit、pass relevance、cached command 是否存在 |
| 可见动态 mesh element | GDME 收集并在 Finish 阶段提交后的 `View.DynamicMeshElements` | 火焰这类对象已经交出了本帧几何候选 | GDME 是否被标记、context 是否 submit、element 范围是否对上 |
| dynamic build request | static mesh 可见但不能安全复用 cached command | 这个物体要在本帧现场生成 one-frame command | 缓存资格、view-dependent 参数、目标 pass 是否支持 cached command |

如果这三路在某个 pass 里全为空，UE 可以跳过这个 pass 的 CPU/draw work 入口。这个状态通常说明：当前 view 的这个 pass 本来没有可画输入。排查砖墙没进 BasePass 时，先看 cached command 那一路有没有进入 `SetupMeshPass`；排查火焰没进 BasePass 时，先确认 GDME 是否真的提交了 `View.DynamicMeshElements`，再看它是否被分到 BasePass。

`SetupMeshPass` 不重新决定火焰是否几何可见，也不替 GPUScene 分配 dynamic offset。它做的是 CPU 侧入口整理：把已经收敛到这里的静态 command、动态 mesh、现场 build request，接到 07 篇那套 command / visible command / Instance Culling setup 机制上。

这里要留住一个阶段边界：pass setup 负责生成、排序或挂起提交相关的 CPU 工作；动态火焰的 GPUScene instance range 由独立的 collector/context commit 合同冻结，随后还要建立 RDG resources、声明 producer 并暴露 intended window。此刻火焰有本帧几何，也知道大概要进哪些 pass；下一段要补上的，是“GPU 能从哪里读我”的身份发布。

## 阶段门 3：GPUScene dynamic data 与 Scene Uniform 读取入口

> **发布护栏**：dynamic identity/range、GPUScene clear/upload RDG work、Scene Uniform 读取入口是连续但不同的状态。RDG upload work established 不等于 CPU 阻塞上传完成；Scene Uniform publication 不等于 Queue Submit 或 GPU Completion。


跨过这道动态几何收敛后，火焰已经有了 CPU 侧本帧几何，MeshPassSetup 也已启动，但 per-pass input 与 culling registration 此时仍可在异步建立。GPU 侧并行建立可寻址合同：dynamic collector/context 先 commit 并冻结本帧 identity/range；Renderer 再建立匹配的 GPUScene RDG resources，并声明 clear/scatter producer；Scene Uniform 暴露 intended resource window；RDG 让 shader 与 Instance Culling consumer 排在 producer 之后。真正的 RHI record、Platform Submit 与 GPU execution 位于更深的后续时间线。

这段状态链必须按顺序理解：

```text
1. collector / range commit
   冻结当前路径的 dynamic primitive identity、instance range、payload range 与 CPU upload contract。

2. RDG resources established
   建立目标 primitive / instance / payload resources，使 capacity、SRV 与 range 属于同一窗口。

3. clear / scatter producer declared
   在图中声明清理与数据生产工作；此时不能推断数据已物理写入 buffer。

4. Scene Uniform exposes intended window
   后续 shader、scene extension 与 Instance Culling 获得目标资源入口和寻址参数。

5. RDG orders consumers
   图内 consumer 被约束在 producer 之后；后续才进入 RHI、Platform Queue 与 GPU 执行。
```

静态砖墙与动态火焰在这里使用不同路径合同：

| | 砖墙（persistent primitive） | 火焰（dynamic primitive） |
| --- | --- | --- |
| Scene identity | persistent identity 只在当前 SceneInfo/Proxy 的 Scene 驻留期稳定 | 来自当前 collector/context 的本帧 dynamic identity |
| instance range | 由持久 GPUScene allocation 合同维护，仍可能因实例数量或重建而改变 | 在本次 collector/range commit 中冻结 |
| Frame Init 责任 | 使现有资源窗口与当前 View/consumer 合同匹配 | 建立 range、RDG resources、producer 与 Scene Uniform window |

commit 只证明 dynamic identity/range 与 CPU upload contract 已冻结；RDG resources 只证明目标资源窗口已建立；producer declared 只证明图中存在生产工作；Scene Uniform 只暴露 intended window；RDG 只保证图内顺序。任何一步都不能单独证明 buffer 已物理更新、Queue 已提交或 GPU 已完成。

```text
FinishGDME 之后:
  CPU 已收集并合并本帧 mesh element，MeshPassSetup 已启动但尚未保证完成

collector / range commit 之后:
  dynamic identity、instance range 与 payload range 合同已冻结

RDG resources 之后:
  目标 resources、capacity、SRV 与 range 属于同一 intended window

producer declared 之后:
  clear / scatter 工作已进入图，但尚不能推断已经物理执行

Scene Uniform + RDG ordering 之后:
  consumer 引用目标窗口，并在图内被约束到 producer 之后
```

这条链服务的是“GPU consumer 应通过哪个资源窗口解释 dynamic identity/range”。火焰是否几何可见，前面已由 visibility、relevance 与 GDME 处理；这里不能把 `FMeshBatch` 存在、collector commit、producer declared、Scene Uniform publication 或 RDG ordering 中任一步写成“GPUScene 已完全更新”。

随后 `InitInstanceState` 必须等**所有 view 都 flush 完动态 primitive** 之后才执行。它的教学角色是：在完整的 dynamic range 已经确定之后，才建立本帧实例状态；如果某个 view 的动态 primitive 还没交账，实例状态就会基于缺数据的范围初始化，后面的 Instance Culling 看到的实例表就可能少一段或错一段。

scene extension 的 view data 与 Scene Uniform 必须暴露 intended GPUScene resource window，并由 RDG producer-before-consumer 依赖约束后续读取。这个入口合同不证明 clear/scatter 已经物理执行，也不证明 RHI、Platform Queue 或 GPU 完成。

最后才是 `BeginDeferredCulling`。它读取已经建立的 GPUScene、view data 与 scene uniform 条件，并根据 GPUScene 是否启用、相关 CVar、immediate mode、实例数量和 culling view 等条件打开 deferred context。这个 context 此时是一只允许后续 pass work 进入的“批处理收件箱”，不是已经填好的实例工作清单。随后 MeshPassSetup 中的 `BuildRenderingCommands` 才把 visible commands、instance ranges 与 draw metadata 作为 batches 加入 active context；`EndInitViews` 再等待 MeshPassSetup 完成。这样，动态火焰的 range、资源窗口与后续 batch registration 才能在 RDG execute / drain 前汇合。

本章只覆盖 PrePass 之前的这一次 `BeginDeferredCulling`。后面在调试/后处理附近可能还有 drain 或重新 begin 的调用点，那属于后续阶段的 culling 组织，不改变这里的早期顺序契约。

若火焰实例数不对、transform 错位或整批消失，就沿上面的五级链检查最后一个成立的状态：range 是否冻结、资源窗口是否匹配、producer 是否已声明、Scene Uniform 是否指向该窗口、consumer 是否被 RDG 排在 producer 之后。这样可以定位合同断在哪一级，而不会用“upload 函数调用过”推断 buffer 已物理更新。


## 阶段门 4：Instance Culling 输入成立并启动

前一阶段已经交付 GPUScene / view data / Scene Uniform 资源入口，`BeginDeferredCulling` 据此打开条件化 deferred context。与此同时，MeshPassSetup 继续把 visible command 整理成 per-pass work；`BuildRenderingCommands` 随后将“哪些 draw 需要实例筛选、读取哪些 range、输出接到哪里”作为 batches 加入 context。`EndInitViews` 等待 MeshPassSetup 后，RDG execute / drain 才处理这些登记并生产筛选结果或 indirect parameters。结果始终留在 GPU 侧供同帧 draw 消费，不需要把当前帧结果读回 CPU 形成硬同步点。

对动态火焰而言，当前状态变化是：正确的 dynamic range 与资源窗口先允许 deferred context 打开，随后 per-pass draw work 把对应 batch 登记进 context；RDG 执行时 Instance Culling 才按当前 view 条件筛选这个 range，并把结果接到后续 draw。若这里出现整批消失、实例数不对或 offset 错位，先核对 batch registration 指向的 range 是否与 Scene Uniform 暴露的 GPUScene 窗口一致，再检查 RDG producer-before-consumer 依赖。

`BeginDeferredCulling` 是条件性 context 启动点：GPUScene 启用、运行 CVar 允许、当前不是 immediate mode，并且实例/culling-view 等条件满足时，它创建 deferred context；否则可以合法 no-op。它不以“已有 registration/work”为入口条件，也不在这一刻声明所有实例筛选工作。后续 batches 加入、RDG execute / drain、输出生产、RHI draw 录制、Platform Queue 提交和 GPU 消费各有自己的完成深度。材质、Pass shader 与 MeshDrawCommand 的选择由更早阶段负责，Instance Culling 只消费后续登记的实例工作。

## RDG Frame Resources：从 CPU 状态到图级消费合同

| 护照项 | 定义 |
|---|---|
| What | 后续 Pass 将读取的 RDG 资源、参数入口和生产/消费关系 |
| Why | CPU visibility 状态不能直接成为 GPU 输入；GPU 数据生产必须进入可排序、可同步的图合同 |
| Owner | 当前 `FRDGBuilder` 图与 Renderer frame context |
| Data | Scene Uniform、View data、GPUScene update resources、Instance Culling resources、per-frame scene resource configuration |
| Control | Renderer 声明生产和消费，RDG 组织依赖与执行边界 |
| Lifetime | 当前图或当前帧阶段；图外持有需要显式所有权交接 |
| Boundary | Resource/Pass 已声明不等于 RHI record、Platform Submit 或 GPU Consume |
| Debug | 检查 producer、consumer、资源窗口和依赖，不只检查某个函数是否调用 |

```text
CPU frame state ready
→ RDG resource entrance registered
→ producer and consumer work declared
→ graph dependencies established
→ later Execute records RHI work
→ platform submission and GPU consumption follow deeper timelines
```

这里不重新教授 Pass culling、barrier、alias 或 AsyncCompute fork/join；这些属于第 05 章。

## 阶段门 5：`EndInitViews` 收敛 visibility，而不是结束整帧

> **边界护栏**：`EndInitViews` 证明 visibility/relevance 等要求在此之前完成的 CPU 生产者已经收敛，并允许部分 post-visibility setup 继续。它不证明所有 shadow、SceneTextures、DepthPass/BasePass、RHI record、Platform Queue 或 GPU 工作完成。


`EndInitViews` 是 visibility 初始化链的重要收口。在 UE5.7 当前顺序里，动态 primitive upload contract、实例状态、view data、Scene Uniform 和 `BeginDeferredCulling` 已先推进到各自的图内发布点；随后 `EndInitViews` 调用 visibility 的 `Finish()`，等待 Compute/Finalize Relevance、Dynamic Mesh Elements 与 MeshPassSetup 等 CPU 任务到达合同要求，并结束相应临时 packet/allocator 的生产期角色。

因此，这里的“最后”只针对 CPU visibility task graph：它让 post-visibility setup 与依赖这些 CPU 结果的消费者继续，却不把前面已经声明的 GPUScene clear/scatter producer 或 Instance Culling 工作升级成“已执行完成”。

可以把它读成一次所有权交接：

```text
Visibility task graph:
  visibility / relevance produce per-view facts and destinations
  → GDME produces and merges dynamic mesh elements
  → MeshPassSetup prepares per-pass CPU / draw work inputs
  → EndInitViews waits for this task graph and releases temporary packet / allocator state

RDG / GPU input contract:
  dynamic collector / context commits identity and ranges
  → GPUScene RDG resources are established
  → clear / scatter producer is declared
  → Scene Uniform exposes the intended resource window
  → conditional deferred culling starts, or legally no-ops when the path has no work
  → RDG orders consumers before later RHI / platform / GPU execution
```

放回砖墙和火焰，跨过这道边界之后，调试问题也会换一种问法。砖墙的关键状态是：目标 pass 里是否已经有静态 cached command 或 visible command 入口。火焰的关键状态是：本帧 dynamic mesh 是否已经完成并合并，是否已进入目标 per-pass CPU/draw work，collector/range 是否冻结，GPUScene RDG resources 与 producer 是否建立，Scene Uniform 是否暴露 intended window，以及适用路径中的 culling consumer 是否被 RDG 排在 producer 之后。

源码里的临时名字应该放在“生产现场”里理解。`ViewPacket` 帮你定位某个 view 的局部生产进度；临时 dynamic primitive 范围描述 GDME 交账过程中的局部动态结果；bulk allocator 是给这些生产期临时结构分配内存的池子。它们的调试价值在于定位“生产阶段哪里还没交账”。跨过 `EndInitViews` 后，visibility/relevance/GDME/MeshPassSetup 的 CPU 结果可以按合同交给后续 setup；GPUScene producer、Scene Uniform 窗口和 Instance Culling 仍分别受其 RDG 与路径条件约束。

因此，`EndInitViews` 交付的是已收敛的 visibility/relevance/GDME/MeshPassSetup CPU 状态，并允许 post-visibility setup 继续；它可以结束对应临时 packet/allocator 的生产角色，但不对 GPUScene producer、Instance Culling 输出、RHI/Platform/GPU 完成作统一承诺。

`EndInitViews` 之后还会触发依赖 main view 可见性的后续工作。shadow GDME 可以理解成“shadow 相关 view 继续收集动态几何的后续生产线”；init task data 则把这类后续依赖挂到已经稳定的 frame 级结果上。阴影算法留给后文，这里只抓住一件事：**main view 的可见性会成为多个后续系统的上游输入**。这些系统沿用的是已经稳定下来的 pass 输入、动态 element 结果或任务依赖关系。

这也是为什么帧初始化值得单独成章——它不只服务 PrePass，它是后面一长串系统的共同地基。

---

## 本章状态回看

读完整条流程后，最重要的是把砖墙或火焰放到下面这条状态链上：

| 状态问题 | 砖墙怎么判断 | 火焰怎么判断 | 如果不成立，先查哪里 |
| --- | --- | --- | --- |
| 场景账和 view 输入是否稳定 | `FScene` 里有 primitive，static mesh cache / view rect /遮挡历史可被 worker 读 | proxy 在场景里，view uniform / view state 已准备好让 GDME 使用 | `OnRenderBegin` 的 scene update、view state、早期 feature task 依赖 |
| 几何/遮挡可见性是否通过 | `PrimitiveVisibilityMap` / unoccluded 相关 bit 支持它继续往下走 | 同样先看 visibility bit；此时还没有本帧动态几何 | frustum、distance、hidden/show-only、occlusion 历史 |
| relevance 是否给出后续命运 | 被分到 cached command 或 dynamic build request | 被标记为需要 GDME，等待动态收集 | `PrimitiveViewRelevanceMap`、pass relevance、缓存资格 |
| CPU 侧几何或 command 入口是否形成 | cached command 被包装到目标 pass，或现场 build request 存在 | GDME 已提交 `View.DynamicMeshElements`，pass setup 已把它分到目标 pass | GDME context submit、`SetupMeshPass` 三路输入 |
| GPU 侧身份与范围合同 | persistent identity 仅在当前 Scene 驻留期稳定 | dynamic collector/context 已冻结 range，后续 RDG resources 与 producer 关系已建立 | collector commit、dynamic range、resource window |
| 后续 reader 是否有图内读取合同 | Scene Uniform 暴露目标资源窗口 | RDG producer-before-consumer 关系成立；适用路径下 culling work 可启动或合法 no-op | resource window、RDG dependency、`BeginDeferredCulling` 条件 |
| CPU 初始化生产是否收口 | visibility/relevance/GDME/MeshPassSetup 结果可供后续 setup | 临时 packet/allocator 可结束；不证明 GPUScene producer/culling/全窗口完成 | `EndInitViews` task graph 与 post-visibility setup |

这张表给出的是一条简化判断：**visibility 负责筛候选，relevance 负责分去向，GDME 负责造动态几何，SetupMeshPass 负责整理 CPU 侧 pass 入口，GPUScene 负责给动态数据身份，scene uniform 负责发布读取入口，Instance Culling 负责在这些入口稳定后做实例级消费。**

---

## 调试主线：先定位物体停在哪个状态台阶

把整章的状态边界倒过来用，就是一条现成的调试路线。如果砖墙或火焰在 DepthPass/BasePass 没有 draw，先把它放回本章的生产链，判断它停在哪个台阶：

```text
1. 当前 view 里还有这个候选吗?
   -> 锚点: PrimitiveVisibilityMap / PrimitiveDefinitelyUnoccludedMap。
   -> 失败时优先看 bounds、hidden/show-only、distance、occlusion 历史。

2. 它已经被分配到哪条后续命运?
   -> 砖墙: MeshCommands[Pass] 或 DynamicMeshCommandBuildRequests[Pass]。
   -> 火焰: GDME 待办，或已经提交的动态 element。

3. 火焰是否交出了本帧几何?
   -> 锚点: View.DynamicMeshElements。
   -> 锚点: FDynamicPrimitive 的 start/end 范围。
   -> 锚点: NumVisibleDynamicMeshElements[Pass]。

4. 目标 pass 是否有 CPU/draw work 入口?
   -> 锚点: SetupMeshPass 的三路输入。
   -> 三路都为空时，含义可能是这个 view/pass 本来没有可画输入。

5. 动态对象的位置、实例数或 culling 参数是否来自已发布的 GPUScene?
   -> dynamic collector/context 已冻结正确 identity/range；GDME 与该 collector 合同分别成立。
   -> RDG resources 已注册，clear/scatter producer 已声明，目标 range 与资源窗口匹配。
   -> Scene Uniform 暴露 intended resource window；RDG 负责让 consumer 位于 producer 之后。
   -> 适用路径中 `BeginDeferredCulling` 打开 deferred context。
   -> MeshPassSetup / BuildRenderingCommands 登记 batches，EndInitViews 等待 setup 收口。

6. 以上状态都成立后，问题进入后续章节范围。
   -> DepthPass 的消费过程看第 09 章。
   -> BasePass / GBuffer 的消费过程看第 10 章。
   -> command 形态和 Instance Culling 细节回看第 07 章。
```

这条路线和正文的章节顺序一一对应。它把"数据何时变稳定"反过来读：每一步先确认当前可信状态，再决定下一个该查的生产者或消费者。

## 一句话收束

> **第 08 章把 `FScene` 里的 primitive、和每个 `FViewInfo` 的视图条件，逐步收敛成 per-view / per-pass 的稳定 draw 输入。在满足缓存资格且缓存未失效的路径中，静态砖墙可复用 MeshDrawCommand，但仍有 per-view visibility、relevance、排序/包装等每帧 CPU 工作；动态火焰的 GDME 几何合同与 dynamic collector/GPUScene 合同彼此独立，后者依次冻结 range、建立 RDG resources、声明 clear/scatter producer并暴露 Scene Uniform 窗口；`BeginDeferredCulling` 先打开 context，MeshPassSetup / BuildRenderingCommands 再登记 batches，`EndInitViews` 等待 setup 收口。** `BeginInitViews` 负责放行生产者，`FinishGatherDynamicMeshElements` 收敛动态几何，GPUScene / Scene Uniform / Instance Culling 建立图内消费合同，最后 `EndInitViews` 关闭对应 CPU visibility task graph；每个阶段门都只保证自己的完成深度。

下一篇第 09 章从这些 pass 入口接着往下走：DepthPrepass 如何消费 `View.ParallelMeshDrawCommandPasses[DepthPass]`，写出本帧的 scene depth，并在之后构建 HZB——也就是本章一直说"留给 09"的那部分。
