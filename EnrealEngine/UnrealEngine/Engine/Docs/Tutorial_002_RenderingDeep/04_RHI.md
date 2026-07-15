
# 04 RHI 抽象层

> **源码版本**: UE5.7  
> **前置阅读**: 01（渲染架构总览）、02（从 Component 到 SceneProxy）、03（三线程模型与渲染命令）  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）  
> **验证记录**: 见同目录 `04_RHI_CoverageMatrix.md`

---

很多 Unity 背景的读者第一次看到 RHI，会把它理解成“UE 版 CommandBuffer”：Renderer 录下一串 draw，RHI 再把函数名翻译成 D3D12、Vulkan 或 Metal 的函数名。

这个模型解释不了几个常见问题：为什么资源创建前必须声明 usage？为什么 CPU 已释放最后一个引用，显存仍不能立即复用？为什么 draw 已录进命令列表，抓帧里却还没有平台命令？为什么命令已经提交，上传环形缓冲仍可能被 GPU 读取？

RHI 真正解决的不是一次函数名翻译，而是**把上层渲染意图逐级降格为当前平台可提交、可同步、可回收的 GPU 工作**。资源、状态、PSO、绑定、命令与完成证据都必须具有明确的数据形态和责任边界。

本篇只使用一条正向主线：

~~~text
上层渲染意图
  -> Record：记录平台无关 RHI 工作
  -> Translate：由 RHI context 消费并翻译
  -> Platform command formation：形成可提交的平台命令包
  -> Queue submit：把命令包交给平台队列
  -> GPU completion：用匹配最后消费者的证据确认完成
~~~

同时追踪一条资源旁线：

~~~text
resource desc
  -> initializer / writable range
  -> Finalize
  -> RHI wrapper
  -> native resource 被平台命令引用和消费
  -> wrapper retirement / native retirement
~~~

两条线在 draw 边界按明确顺序汇合：resource wrapper、合法 access、ready PSO 与 binding 先成为 Record 的输入；recorded work 在 Translate 后形成平台命令包；queue 接管命令包，GPU 完成最后一次资源读取后，上传槽和 native backing 才获得相应复用或回收资格。Wrapper 则沿 CPU 引用与 pending-delete 线独立退休，可以和 native retirement 错开。由此也能看清边界：wrapper 已成立不代表 GPU 已消费初始数据；命令已 submit 不代表资源可以复用；CPU wrapper 被删除更不能当成通用 GPU fence。

贯穿案例是一颗红色粗糙金属 mesh。它的一小段顶点数据来自每帧复用的动态上传环形缓冲。同一条 draw 会迫使我们回答：资源如何创建、数据何时交付、状态何时合法、PSO 与参数何时可用、命令何时形成、上传槽何时才能安全复用。

---

## 本篇边界

04 从上层已经作出渲染决策的位置开始。Renderer 或 RDG 已知道要创建什么资源、需要什么访问、采用哪套 shader 与管线状态、准备录制哪条 draw；本篇解释这些决定进入 RHI 后如何变成可执行合同。

本篇讲清动态后端与能力、desc/initializer/Finalize、wrapper/native 两层退休、access/transition、PSO 与 binding、command-list/pipeline/queue 三轴，以及 record 到 completion 的五种状态。

本篇不展开 RDG 图编译、MeshDrawCommand 生成、平台 API 细节，也不重复第 03 章 task、pipe、named thread 与 fence 的调度模型。04 提供原语与完成深度；05 解释 RDG 如何编排这些原语，07 解释 mesh draw 配方从哪里来。

---

## 一、RHI 是六类相互交接的合同

RHI 是一组相互约束的合同，而不是单个对象。

| 合同 | 输入 | RHI 产物 | 主要问题 |
|---|---|---|---|
| 后端合同 | 平台、启动参数、能力要求 | 当前动态 RHI 与能力集 | 当前是谁在实现操作？ |
| 资源合同 | 类型、尺寸、格式、usage、flags、初始访问 | RHI resource wrapper | 后端必须提供什么能力？ |
| 状态合同 | 资源、子资源、前后访问、pipeline | transition 原语 | 下一次读写何时合法？ |
| Draw 合同 | PSO 配方、shader 参数、资源引用 | 管线与参数包 | 固定状态和逐次数据是否齐备？ |
| 命令合同 | 平台无关 RHI 操作 | context work 与平台命令包 | 意图怎样降格为平台工作？ |
| 完成合同 | submit 顺序、fence/completion value | 有范围的完成证据 | 最后消费者是否越过使用点？ |

Renderer 决定“画什么”，RHI 约束“怎样把这个决定合法地交给当前平台”。RHI 不重新判断 mesh 是否可见，也不替材质系统选择 shader。

后文每到一个阶段，都回答：当前权威数据是什么；谁拥有并能继续推进；新增了什么产物；哪条证据证明状态成立；此刻仍不能推断什么。调用栈会因线程模式、bypass、平台与配置变化，状态与责任必须保持可解释。

---

## 二、后端合同：当前是谁在实现 RHI

UE 在 RHI 初始化阶段选择动态后端。FDynamicRHI 表达实现接口，GDynamicRHI 指向当前激活实例。它们不是 Renderer 到处直接调用的“万能资源工厂”，而是为 RHI API、command list、resource initializer、能力查询和提交路径提供统一实现落点。

上层不能在每条 draw 上重新判断当前是 D3D12、Vulkan 还是 Metal。启动阶段锁定后端，之后平台无关合同才能落到同一实现体系。

后端选择也决定能力。Renderer 选择路径时会综合 backend、feature level、shader platform、格式与原子操作、bindless/async compute 等 capability，以及项目设置和运行环境。NullRHI 等模式甚至可能没有真实呈现目标。功能未启用时，应先确认后端与能力，而不是先假设 shader 出错。

| 字段 | 后端概念护照 |
|---|---|
| What | 当前进程内实现 RHI 合同的动态后端 |
| Why | 隔离平台 API，并让上层按能力选择路径 |
| Owner | RHI 初始化与平台层 |
| Data | backend 实例、shader platform、能力集合 |
| Consumer | Renderer 判断、资源创建、translate/submit |
| Lifetime | 通常覆盖 RHI 的进程级生命周期 |
| Boundary | 不重新做场景决策，也不等于平台 device 本身 |
| Debug | 先查 backend/capability，再追资源、shader 或 pass |

此时只证明平台有能力承接目标资源、shader 与 draw 合同；还没有创建资源，更没有形成 GPU 命令。

---

## 三、资源合同：Desc 固定需求，后端决定 Native 布局

创建 buffer 或 texture 时，上层先描述类型、尺寸、格式、usage、flags、mip/array 信息和预期初始访问。这些字段构成**资源合同**：约束后端必须提供什么能力，也约束资源之后能合法参与哪些操作。

Desc 不是平台原生内存布局。后端仍决定 heap/memory type、allocation、placement、tiling、alignment、residency、辅助 upload/staging 对象与原生 view。

~~~text
上层声明“需要什么”
  -> desc 固定资源合同
  -> 后端选择“怎样满足”
  -> 形成 RHI wrapper 与平台 native resource
~~~

Buffer 合同通常围绕字节大小、stride、usage 和访问方式；texture 还表达维度、format、extent、mip、array slice 与 sample count。两者都可能进一步切分为 range 或 subresource。

资源类型也不等于本次访问状态。Texture 允许作为 UAV，不代表它永远处于 UAV 写状态；usage 说明合法能力范围，ERHIAccess 才描述某个时间点的访问合同。

---

## 四、两阶段创建：Initializer、Writable Range 与 Finalize

当调用方还要填写初始内容时，buffer/texture initializer 把“后端准备资源”和“调用方生产数据”分开：

~~~text
desc
  -> initializer 暂时拥有未完成事务
  -> writable initial-data range
  -> 调用方写入内容
  -> Finalize()
  -> FRHIBuffer / FRHITexture wrapper
~~~

这段创建事务的 producer 是提交 desc 并填写 initial data 的调用方，initializer 是未完成事务的 owner，具体产物是可引用的 RHI wrapper；下一消费者通常是 transition、binding 或 command recording。Initializer 的责任持续到 Finalize 或取消清理，wrapper 则进入独立的引用生命周期。若失败，症状应按阶段区分为 desc 不合法、writable range 内容错误、事务未 Finalize，或 wrapper 成立后后续访问合同不成立。

Initializer 可能持有已验证描述、后端临时状态、可写 range/subresource、完成创建所需上下文，以及未完成时的清理责任。它采用受控移动语义，因为“谁负责 finalize 或取消清理”必须唯一。

Finalize 关闭初始数据生产并交付可引用 wrapper。它能证明初始写入阶段结束、RHI 句柄成立、initializer 不可继续使用。它不能证明数据已被 GPU 消费、copy/upload 已越过 queue、资源已处于后续 access，也不能证明 backing upload 区域可立即覆盖。

快捷创建 API 可以把 desc、初始数据和 finalize 包装成一次调用，但不改变责任分层。

| 案例检查点 | 当前状态 |
|---|---|
| 权威数据 | desc 与 CPU 写完的动态顶点字节 |
| Owner | initializer/上传分配与调用方共同完成事务 |
| Control | 调用方写入，Finalize 关闭事务 |
| 成立证据 | 获得有效 RHI buffer wrapper |
| 不能推断 | GPU 已读取顶点；槽位可被下一帧覆盖 |

---

## 五、两层生命周期：Wrapper 与 Native Resource 分别退休

RHI 资源至少同时存在 UE 的 CPU wrapper 生命周期，以及平台 native resource 的退休生命周期。

### Wrapper 生命周期

FRHIResource 由 CPU 引用系统管理。最终引用归零后，它不会必然在调用点立即 delete，而是进入 RHI pending-delete 处理。统一收集允许 RHI 在合适阶段做最终检查，也避免任意线程破坏仍被命令系统引用的逻辑对象。

~~~text
CPU references
  -> final reference released
  -> pending delete
  -> RHI delete processing
  -> wrapper final check / C++ deletion
~~~

这只回答“UE 是否还需要 wrapper”，不是通用 GPU 完成证明。

### Native Resource 生命周期

后端创建的原生 buffer、texture、allocation 或 descriptor 仍可能被平台命令引用。真正释放取决于后端 retirement policy、队列进度、completion value 和平台同步条件。

~~~text
native resource referenced by platform work
  -> queue consumption
  -> last consumer completes
  -> backend retirement condition
  -> native object/allocation reclaimed
~~~

后端可以维护 delete/retirement queue。Wrapper 的 C++ 删除与 native object 回收不要求同时发生，也不要求所有后端使用固定帧数或同一种 fence。

### 安全复用比删除更常见

上传环形缓冲中，CPU 在第 N+1 帧想覆盖一个槽位，但第 N 帧 draw 可能尚未读取。安全条件不是“CPU 不再持有临时指针”，也不是“命令已 submit”，而是证明最后一个 GPU 消费者已经越过该槽位的读取点。

证据可以是与相应 queue 和提交范围匹配的 GPU fence、completion value 或更高层 reuse contract。若消费者跨多个 queue，还要建立跨 queue 依赖；graphics fence 不能自动证明无关 compute queue 完成。

| 案例检查点 | 当前状态 |
|---|---|
| 权威数据 | wrapper 与仍可能被 GPU 读取的 native backing |
| Owner | CPU 引用管理 wrapper；后端管理 native retirement |
| Control | CPU 释放逻辑引用；后端决定平台对象何时退休 |
| 成立证据 | 命令引用保持资源在消费期有效 |
| 不能推断 | 引用归零或 wrapper 删除意味着 GPU complete |

---

## 六、状态合同：Transition、Subresource 与 UAV

资源创建合同说明“允许怎样使用”，状态合同说明“这一次正在怎样使用”。ERHIAccess 表达访问语义，FRHITransitionInfo 把 resource、before/after access 和 subresource 范围组织成 transition 描述。

状态交接的 producer 是上层调用方或 RDG 已确定的前后访问关系，具体输出是带资源范围、前后 access 与 pipeline 语义的 transition；RHI command list 记录它，后端是下一消费者并形成平台 barrier / 同步。被记录的 transition 数据必须在 command list 消费时仍有效，资源本身则必须覆盖所有前后消费者。这里断链时，典型症状是后续读取旧值、验证层报告状态错误，或跨 pipeline 消费缺少正确等待。

当一段工作写入资源，后续工作要以另一种方式读取或写入时，需要建立顺序、可见性和平台状态转换。RHI 提供跨平台原语，后端映射为 barrier、layout/state 变化或同步操作。

Transition 必须回答：哪个 resource/subresource；之前和之后是什么 access；涉及哪个逻辑 pipeline；在哪里 begin、在哪里 end。Begin/end 可以跨越调度区间，但 begin 不证明 consumer 已可读；完整 transition 与依赖成立后，后续访问才合法。

Mip、array slice 或 range 可以独立跟踪状态，但不表示各自拥有独立 allocation。状态粒度与物理 backing 粒度必须分开。

### UAV 也有依赖

~~~text
Compute A: UAV write
  -> dependency / visibility requirement
Compute B: UAV read or write
~~~

若 B 依赖 A，写后读和写后写都需要正确顺序与可见性。UAV overlap 只在调用方或图编排器能证明访问不冲突、或已有依赖保证正确时放宽保守同步；它不是“所有 UAV 都可并发”的开关。

RHI 提供 access/transition 原语。RDG 只对**纳入图且正确声明**的访问做图级推导；外部资源、手写 RHI 命令或遗漏声明不会被自动兜底。

---

## 七、Draw 合同之一：PSO 从配方到 Native Ready

Graphics PSO 把 shader stages、vertex declaration、rasterizer、depth/stencil、blend、primitive type 和 render-target compatibility 等固定组合收敛成可缓存管线对象。

PSO 不包含 draw 的全部逐次数据：它不拥有当前 object transform、具体纹理内容，也不替代 vertex/index buffer 与 shader binding。

~~~text
pass/draw recipe
  -> graphics PSO initializer
  -> pipeline state cache entry
  -> backend native PSO preparation
  -> native PSO ready for consumer
  -> command binds PSO
~~~

这条链由上层 pass/draw recipe 生产 initializer，Pipeline State Cache 与后端共同推进 cache entry 和 native pipeline，command recording 是下一消费者。Initializer 是配方值，cache entry 与 native PSO 按缓存/后端策略存活，命令消费期必须看到 ready pipeline；失败会分别表现为配方不兼容、cache miss、ready 等待或绑定了错误的 pipeline，而不是同一种“PSO 没有”。

“Cache entry 存在”和“native PSO ready”不是同一状态。某些路径可异步准备 PSO；真正消费时若仍未 ready，调用方可能等待并形成 hitch。不同平台和配置策略不同，不能把所有 miss 描述成同一种后台编译。

调试时应区分：配方不兼容、cache miss、entry 存在但 native 未 ready，以及 PSO 正确但 binding/resource state 错误。对案例而言，这一阶段只证明目标 draw 有可用固定管线，还没有绑定本物体参数或录制 draw。

---

## 八、Draw 合同之二：Shader Binding 与生命周期

材质和 pass 以“粗糙度、颜色、场景纹理、对象变换”等语义组织数据。进入 RHI 前，这些语义依据 shader metadata/layout 转译为后端可消费的常量、资源引用、槽位、偏移和 handle。

~~~text
semantic data
  -> shader metadata/layout
  -> constants + resource references + bindless handles
  -> FRHIBatchedShaderParameters
  -> backend binding operations
~~~

Binding 的 producer 是上层 shader 参数系统，输出是常量字节、资源引用与 handle 组成的 `FRHIBatchedShaderParameters`；command list 记录或直接交给 context，后端 binding 操作消费它。批次容器可以很短命，但延迟命令复制的参数数据、其中引用的 resource/view，以及 handle 指向的目标都必须在实际消费时有效。失败症状因此要分成 constant offset/size 错、resource/view 错、uniform slot 错、bindless handle 失效和资源寿命不足。

- Loose constants 是随绑定提交的小块常量，不必每项成为独立资源；
- Uniform buffer 按既定布局打包一组常量，拥有自己的资源引用与寿命；
- Texture/SRV/UAV/Sampler 提供资源或 view 身份；
- Bindless handle/index 只负责定位，不拥有资源。

FRHIBatchedShaderParameters 把多类参数压成可回放绑定包，不让后端重新理解“红色”或“粗糙度”。语义选择早已在 Renderer、材质与 shader metadata 层完成。

参数包中的常量副本、资源引用、view、uniform buffer 与 bindless entry 必须覆盖命令后续消费期。不能把短命 CPU 临时地址留给延迟命令读取；handle 仍存在也不证明其目标资源仍有效。

| 案例检查点 | 当前状态 |
|---|---|
| 权威数据 | ready PSO、顶点/索引资源、参数包、访问要求 |
| Owner | PSO cache、RHI refs 与 recording scope |
| Control | 上层选择语义，RHI 组织绑定合同 |
| 成立证据 | draw 所需固定状态和逐次数据齐备 |
| 不能推断 | 命令已 record、平台命令已形成、GPU 已绘制 |

---

## 九、Command-list Type、Logical Pipeline 与 Platform Queue

| 轴 | 回答的问题 | 不能推出什么 |
|---|---|---|
| Command-list C++ type | 允许录制哪些命令？ | 必然使用哪条硬件 queue |
| Logical RHI pipeline | 工作属于 Graphics/AsyncCompute 等哪个逻辑域？ | 平台必有独立 queue 或真实重叠 |
| Platform context/queue | 后端最终在哪里形成并提交原生命令？ | 与 C++ 类型固定一一对应 |

FRHICommandListBase 提供公共录制与调度基础；compute-capable list 暴露 compute-compatible 操作；graphics list 增加 render target 与 draw 能力；immediate list 承担全局主交接和提交职责。继承首先是**命令能力合同**。

Compute list 不证明工作必然提交到独立硬件 compute queue。实际落点还取决于 ERHIPipeline、调用方、translate context、后端与平台能力。“Immediate”也不表示 GPU 立即完成。

Parallel/sub command lists 可由多个生产者并行记录，再在确定汇合点合并、翻译或 finalize。并行提高 CPU 吞吐，但必须保持顺序、依赖和提交 prerequisites。不得从 UE5.7 当前实现未使用的 translate priority 或最小 draw 数参数反推实际切分策略；应观察真实 work、jobs、contexts、finalize 结果与 prerequisites。

---

## 十、Bypass：缩短 Record 路径，不绕过 GPU 时间线

正常 deferred 路径中，RHI API 常把操作编码为平台无关 command node，稍后由 context 回放。Bypass 启用时，部分 API 不再创建普通 deferred node，而更直接进入当前 context。

~~~text
正常：API -> RHI command node -> later context
Bypass：API -----------------> current context work
~~~

Bypass 适合调试录制和回放问题，但“直接”只表示更接近 context execution。它不证明平台 command list 已形成、queue 已提交、GPU 已执行，也不允许省略资源生命周期与同步合同。

若 bypass 下问题消失，应优先怀疑 command capture、参数寿命、回放顺序或子列表汇合。Bypass 改变路径形状，不改变 RHI 责任，也不是正常并行性能模型。

---

## 十一、命令的五阶段生命周期

任何“已经执行”“已经提交”“已经完成”都必须落到以下阶段之一。

这五段是一条正向控制权链，而不是五个同义标签：recorder 生产 RHI work，executor/context 消费并翻译，backend finalize 生产可提交平台命令包，platform queue 接管提交顺序，GPU timeline 最后用与目标消费者匹配的完成证据关闭本次使用。每段输出都是下一段输入；资源和捕获数据的 lifetime 必须至少覆盖到它们最后一次被该链消费的位置。

### 1. Record

Command-list API 表达 set PSO、bind parameters、transition、set buffers、draw 等操作。正常路径形成平台无关 command node，并捕获后续消费所需的数据与资源引用；bypass 可能直接形成 context work。

| 输入 | 输出 | 能证明 | 不能证明 | 常见失败 |
|---|---|---|---|---|
| PSO、bindings、resources、draw 参数 | command nodes 或 context work | CPU 侧意图已表达 | context 已消费、平台命令已形成 | 临时参数失效、资源引用遗漏、录错 list |

FinishRecording 最多证明 CPU 录制封口，不证明 translate、submit 或 GPU complete。

### 2. Translate

RHI command work 被 context 消费并转换成后端可组织的命令和状态变化。

| 输入 | 输出 | 能证明 | 不能证明 | 常见失败 |
|---|---|---|---|---|
| RHI work 与资源/状态引用 | context-recorded backend work | Context 已消费平台无关命令 | 平台 list 已 finalize、queue 已接收 | 顺序错误、context/pipeline 不匹配、延迟参数失效 |

Translate 可串行或并行，但策略必须从实际 jobs 和 contexts 判断，而不是从废弃参数名称猜测。

### 3. Platform Command Formation

Contexts 被关闭、汇合或 finalize，形成后端可提交的平台命令包，例如 IRHIPlatformCommandList 所代表的结果。

| 输入 | 输出 | 能证明 | 不能证明 | 常见失败 |
|---|---|---|---|---|
| Backend contexts | finalized platform command lists | 可提交原生工作包成立 | Queue 已接管、GPU 已执行 | 子列表未汇合、finalize prerequisite 未满足 |

“原生命令已形成”和“原生命令已提交”必须分开。

### 4. Queue Submit

后端提交接口消费 finalized lists，把它们交给平台 queue。此刻发生控制权交接：queue 开始拥有执行顺序。

| 输入 | 输出 | 能证明 | 不能证明 | 常见失败 |
|---|---|---|---|---|
| Finalized lists 与 prerequisites | queue-owned work | 指定提交范围已交给 queue | GPU 越过最后消费者、其他 queue 完成 | prerequisite、映射或提交范围错误 |

第 03 章的 Submit 定义在这里得到具体数据形态：Submit 是控制权交接，不是完成通知。

### 5. GPU Completion

只有目标 queue 进度越过与最后消费者匹配的完成点，才能证明资源读取、写入或 copy 已完成到所需范围。证据可能是 GPU fence、completion value、readback contract 或后端等价机制。

| 输入 | 输出 | 能证明 | 不能证明 | 常见失败 |
|---|---|---|---|---|
| 已提交工作与完成标记 | 有 queue/范围含义的证据 | 指定消费者越过完成点 | 所有 queue、全设备或显示系统完成 | 等错 fence、范围过浅、跨 queue 依赖缺失 |

完成证据必须回答“哪个 queue、哪次提交、哪个最后消费者”。泛泛地说“等 GPU”没有调试价值。

---

## 十二、贯穿 Worked Case：一次动态上传缓冲 Draw

1. **声明资源需求**：建立动态 vertex/upload buffer desc。成立的是资源合同，不是固定 native layout。
2. **生产内容**：initializer 暴露 writable range，CPU 写入顶点。成立的是 CPU 字节，不是 GPU 可见性。
3. **交付 wrapper**：Finalize 返回 FRHIBuffer。成立的是 RHI 句柄，不是安全复用。
4. **准备 draw**：PSO cache 提供 ready pipeline，binding 包持有对象变换、红色、粗糙度、纹理与资源引用。成立的是 draw 配方齐备。
5. **建立 access**：资源从 copy/upload 或 UAV 写状态转入 vertex/shader-readable access。成立的是访问合同，不是 GPU 已执行 transition。
6. **Record**：录制 PSO、bindings、vertex/index buffers 与 indexed draw。成立的是 CPU 意图。
7. **Translate**：Context 消费 RHI work。此处最容易暴露延迟参数寿命问题。
8. **Platform formation**：并行子列表汇合并 finalize，形成可提交 native lists。
9. **Queue submit**：后端把 lists 交给平台 queue。成立的是 queue ownership，不是 draw complete。
10. **Completion/reuse**：匹配最后读取者的 fence 满足后，上传槽才能被下一帧覆盖。
11. **分层退休**：wrapper 进入 pending-delete；后端按自己的条件回收 native resource。两层可以错开。

| 状态 | 权威产物 | 控制者 | 成立证据 | 仍不能推断 |
|---|---|---|---|---|
| Described | desc | 调用方/创建 API | 合同验证通过 | native layout 固定为某方案 |
| Content produced | writable range | initializer/调用方 | CPU 写入完成 | GPU 可见 |
| Wrapper delivered | RHI resource ref | 创建路径 | Finalize 返回 | GPU 已消费 |
| Draw ready | PSO + binding + access | Renderer/RHI scope | 消费数据齐备 | 已 record |
| Recorded | RHI command work | command list | 录制封口 | 已 translate |
| Translated | context work | executor/context | context 已消费 | 已 submit |
| Platform-formed | native lists | backend finalize | 可提交包成立 | queue 已接管 |
| Submitted | queue-owned work | backend/queue | submit 顺序成立 | GPU complete |
| Completed | completion evidence | GPU/backend | 最后消费者越过点 | 全设备完成 |
| Retired | 两层结束状态 | ref system/backend | 各自条件满足 | 两层同一时刻结束 |

---

## 十三、调试：寻找最后成立状态

不要从黑屏、闪烁或崩溃直接跳到平台 API；先找最后一个可靠证据。

| 症状 | 最后成立证据 | 下一责任层 | 优先检查 |
|---|---|---|---|
| 功能路径未启用 | RHI 初始化完成 | backend capability | backend、feature level、shader platform、capability |
| 资源创建失败 | capability 成立 | resource contract | desc、format、usage、flags、尺寸 |
| 内容全零或错位 | wrapper 已创建 | initializer/content | writable range、stride、subresource、finalize 前写入 |
| 间歇闪烁或旧数据 | 内容正确 | access/dependency | transition、UAV 写后读、overlap、跨 pipeline 顺序 |
| 首帧 hitch | PSO 配方正确 | native PSO ready | cache miss、ready 状态、消费点等待 |
| 参数串位 | PSO 正确 | shader binding | metadata/layout、常量范围、资源与临时数据寿命 |
| Bypass 正常、普通路径错 | API 参数正确 | record/translate | capture、延迟寿命、子列表汇合、回放顺序 |
| 抓帧没有目标 draw | draw recipe 存在 | record/translate | 是否进入目标 list、是否被 context 消费 |
| Context 有工作但平台包缺失 | translate 成立 | platform formation | finalize、prerequisite、parallel join |
| Native command 存在但无结果 | formation 成立 | submit/state | queue submit、pipeline 映射、barrier 与依赖 |
| 上传槽偶发污染 | queue submit 成立 | GPU completion | 是否等到最后消费者、是否跨 queue |
| 退出或回收崩溃 | CPU refs 归零 | retirement | pending-delete、命令引用、backend retirement queue |

函数只是定位锚点，状态才是判断依据。只要知道“最后成立状态”，就能把问题限制在相邻责任交接，而不是在 Renderer、RHI 与平台后端之间盲查。

---

## 十四、三个不可混淆的判断

### 资源句柄成立，不等于 GPU 已消费

Desc 和 initializer 建立创建事务，Finalize 交付 wrapper。GPU 是否读取或写入，必须沿命令和 completion 主线判断。

### 命令形成，不等于命令完成

Record、translate、platform formation、queue submit、GPU completion 是五个深度。等待、复用或销毁必须指明需要哪一级证据。

### 接口能力，不等于物理载体

Command-list type 限制可录制命令，logical pipeline 表达逻辑执行域，platform context/queue 决定最终载体。三者共同约束工作，但不存在普遍一一映射。

---

## 下一篇如何接上

到这里，RHI 已提供资源、状态、transition、PSO、binding、recording、platform formation、queue submit 和 completion 原语。

但一帧可能有数百个 pass 和大量临时资源。人工决定每个 pass 是否执行、每个资源何时开始和结束、何时插 barrier、何时 alias、Graphics 与 AsyncCompute 在哪里 fork/join，既低效又容易错。

第 05 篇将进入 RDG：它如何从 pass/resource 声明建立依赖，如何在保持构图顺序骨架的同时裁剪无效工作，如何计算引用与生命周期，如何生成 barrier 与跨 pipeline 同步，以及如何安排 transient allocation 和 alias。

需要带到下一篇的边界只有一句：**RDG 能自动编排的前提，是资源被纳入图且访问被正确声明；手写 RHI 路径不会被 RDG 自动兜底。**
