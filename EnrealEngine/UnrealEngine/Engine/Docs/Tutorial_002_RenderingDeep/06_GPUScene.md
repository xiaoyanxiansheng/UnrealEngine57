# 06 GPUScene 与 GPU-Driven 基础

> **源码版本**: UE5.7  
> **前置阅读**: 01 架构总览、02 Component 到 SceneProxy、03 线程模型、04 RHI、05 RenderGraph  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `06_GPUScene_CoverageMatrix.md`

---

## 这一篇要解决的困惑

到第 02 章为止，一个 `UPrimitiveComponent` 已经能够通过 Proxy、SceneInfo 和 `FScene::Update()` 成为 Renderer Scene 中可查询的状态。但 shader 不能解引用 C++ 指针，也不能遍历 `FScene` 的 CPU 容器。它只能读取显式绑定的 buffer、uniform、纹理和本次 invocation 的索引。

Unity 读者很容易由此得到另一个直觉：既然 draw 已经知道对象和材质，把 transform、bounds 与 custom data 临时塞进每条 draw 的参数即可。这个模型对少量对象尚可工作，却无法解释 UE 为什么让 visibility、材质表达式、速度、阴影和 GPU-driven culling 共享同一份对象状态。若每个 pass、每条 draw 都重新打包，CPU 会重复查找与绑定，GPU 也会面对多份难以保持一致的数据副本。

GPUScene 的核心不是“把 Scene 搬到 GPU”，而是：

> **把已经发布的 Renderer Scene 状态，转换成带身份、区间、布局和消费条件的 GPU 可寻址数据库；本帧工作只携带 ID、range 与少量消费元数据。**

它不是第二棵 Scene 树，不是当前 View 的可见列表，也不是每条 draw 的参数包。CPU 仍然负责对象生命周期、身份分配、dirty 判断和上传内容构造；GPU 获得的是可由多个消费者并行寻址的 primitive、instance 与可选 instance payload 数据。

本章用一个固定案例贯穿全部状态：

> 一个远离世界原点的 Instanced Static Mesh 草丛 primitive，包含 800 叶草；其中第 317 叶发生局部 transform 与 custom data 更新。

我们将始终问五件事：当前权威数据是什么、谁控制下一步、产生了什么新状态、该状态能证明什么、还不能证明什么。

## 本章边界

本章回答 **GPUScene 数据怎样成立并被消费**。

- 第 02 章已经解释 Scene publication、Proxy/SceneInfo 生命周期与索引边界；本章从 Scene 已发布开始。
- 第 05 章已经解释 RDG 的依赖、生命周期、barrier 与执行边界；本章只说明 GPUScene 如何在图中建立 clear/upload producer 与 consumer 关系。
- 第 07 章解释 MeshBatch、MeshDrawCommand、Visible Command、排序、实例化与提交工作；本章只定义 draw/culling 需要携带的 lookup 与 range 合同。
- 第 08 章解释 Frame Init 在何时启动、等待和收口这些生产者；本章不重建 `OnRenderBegin`、GDME 或 `EndInitViews` 时间线。

还必须继承 03/04 的完成深度：

`RDG producer work established` 不等于 RHI 已录制；RHI 已录制不等于 platform queue 已接管；queue submit 不等于 GPU 已越过最后消费者。

## 唯一主地图：七个状态门

全文只使用下面一条正向过程：

```text
Scene published
→ identity and allocation established
→ upload set prepared
→ GPUScene RDG resources registered or resized
→ clear / scatter GPU work established
→ resource entrances and dynamic ranges exposed
→ Instance Culling and shaders consume the matching window
```

这七个状态门不是七个函数，也不保证全部串行阻塞。先把它们当成一组生产者—消费者交接：每一门都接收上游已经成立的输入，由明确的 owner 推进，产生下一位 consumer 可以使用的新状态。

| 状态门 | Owner / Controller | 输入 | 正向输出 | 下一位 Consumer |
|---|---|---|---|---|
| Scene publication / identity | `FScene` 与 GPUScene 身份账 | 已提交的 Proxy、SceneInfo 与 Scene 变化 | 当前驻留期身份及其到 packed storage 的映射 | instance/payload allocation 与 upload preparation |
| Instance / payload allocation | GPUScene CPU allocators | primitive identity 与当前 instance 数据合同 | instance range、可选 payload range、待清理旧 range | upload preparation |
| Upload set | GPUScene upload preparation | 已发布 Scene 快照、dirty state 与当前 allocation | CPU upload records、scatter destinations、clear ranges、setup prerequisites | GPUScene resource setup 与 upload producer |
| GPUScene resources | GPUScene 与 RDG | 容量需求、layout 合同和 upload set | 当前图中的 buffers/SRVs、capacity 与寻址参数 | clear/scatter producer 和资源入口发布 |
| Producer work | GPUScene upload path 与 RDG | upload records、目标资源和 clear ranges | clear/scatter passes 及其到 reader 的图内依赖 | Scene/View resource entrances 与其他图内 readers |
| Resource entrances | Scene Uniform、View state 与相关 context | 当前 GPUScene resources、layout、frame 和 dynamic ranges | 指向同一资源窗口的 Scene/View 参数 | shader 与 Instance Culling |
| Consumer output | shader、Instance Culling | lookup/range、View、draw work 与 GPUScene entrances | shader 读取结果，或 visible-instance indirection 与 indirect args | 后续 draw recording 与 GPU execution |

这张表只建立正向交接。每一门能证明到什么深度、还不能证明什么，会在对应小节已经讲清 owner、input、output 和 consumer 后再说明。

## 四本账：相似的数字不代表同一种身份

四本账是责任轴，不是第二条过程主线。

| 账本 | 回答的问题 | 典型数据 | 生命周期 |
|---|---|---|---|
| Scene identity ledger | 当前 Scene 驻留期内它是谁、现在位于哪里 | `PackedIndex`、`PersistentIndex` 及映射 | SceneInfo/Proxy 当前驻留期 |
| GPU allocation ledger | GPUScene 给它预留了哪些区间 | primitive key、instance offset/count、instance payload offset/stride | 当前分配成立期间 |
| Publication ledger | 哪些内容需要刷新，消费者应使用哪组入口 | dirty set、buffer/SRV、capacity、tile 参数、frame 与 dynamic range | 当前图和消费窗口 |
| Draw consumption ledger | 本帧哪个 View、哪项 draw work 要读哪些实例 | primitive/dynamic metadata、range/runs、view、output mapping | 当前 View/Pass 工作期 |

### Scene identity 不等于 packed storage

`PackedIndex` 是当前 `FScene` packed arrays 的存储位置；swap-remove 或其他压缩操作可以让它改变。`FPrimitiveSceneInfo::PersistentIndex` 所表达的 persistent primitive identity 在当前 SceneInfo/Proxy 从加入 Scene 到移除 Scene 的驻留期内稳定，但它不是 Component 的永久 ID，也不保证跨 Proxy 重建保持不变。

正文中还会遇到类型概念 `FPersistentPrimitiveIndex`，以及 shader/draw 路径携带的 primitive lookup。它们属于同一身份链的不同表示，不能当成一个可随意互换的变量。

### Primitive identity 不等于 instance slot

同一个草丛 primitive 可以保持 persistent identity 不变，却因为实例从 800 增加到 1300 而重新分配 instance range。`InstanceSceneDataOffset` 回答“当前连续区间从哪里开始”，relative instance ID 回答“区间内是哪一叶草”，可选 instance payload offset/stride 回答“扩展数据在哪里、每项多宽”。

因此：

```text
stable primitive identity
≠ stable instance offset
≠ stable payload range
≠ current View selected it
```

## 状态门一：Scene publication 到 GPUScene identity

这一门由 `FScene` 推进。它接收已经排入 Renderer 的 add、remove、transform 与 instance 变化，通过 `FScene::Update()` 批量吸收并发布，输出当前 Scene 驻留期内可查询的 SceneInfo、packed storage 关系和 persistent primitive identity 映射。下一步的 GPUScene allocation 与 upload preparation 以这份已发布状态为输入，而不是回头读取 live Component。

对于 800 叶草，publication 后可以确定：

- 当前 Scene 驻留期内的 persistent primitive identity；
- 当前 instance source 的数量和内容来源；
- 哪些 primitive-wide 或 instance-specific 状态发生了变化；
- GPUScene 更新路径可以开始建立分配与上传责任。

这份输出的边界是 CPU Renderer Scene publication：此时还不能确定 shader 能看到第 317 叶的新 transform。Scene publication 与 GPUScene producer work 是不同阶段。

## 状态门二：instance 与 instance payload 区间分配

GPUScene 的 CPU 侧 allocator 会为 instance records 分配连续范围，并按需要为可选 instance payload data 分配扩展区。这里的 payload 专指 **GPUScene instance payload data**，例如实例级 custom data 或由 flags/stride 解释的扩展记录；它不等于第 07 章 draw/Instance Culling 携带的 **culling/draw work metadata**。

### Allocation 护照

- **What**：为 primitive 的 instance 与可选 payload 预留可寻址区间。
- **Why**：shader 和 culling 需要用整数 ID/offset 定位数据，而不是 CPU 指针。
- **Owner**：GPUScene 的 CPU allocation ledger。
- **Input**：当前 primitive identity、instance count 与 payload stride/flags 合同。
- **Data**：offset、count、payload offset、stride、待 clear 范围。
- **Consumer**：upload preparation 用这些区间生成目标地址；draw/culling setup 后续携带匹配的 range。
- **Control**：Scene/GPUScene 更新根据实例数量和数据合同申请、重分配或释放。
- **Lifetime**：当前 allocation 成立期间，不等于 primitive 永久身份。
- **Boundary**：allocation 只证明 CPU 分配账成立，不证明 GPU buffer 已写入。
- **Debug**：单实例串位、扩容后随机错位，先查 range/stride/relative ID，而不是先查材质。

### Free 不是数据立即消失

释放旧 range 时，CPU allocation ownership 结束，并把该范围纳入后续 invalidation/clear 责任。旧 GPU 内容不会因为 allocator 的 CPU 操作而瞬间消失：

```text
CPU allocation ownership ended
→ old range scheduled for clear / invalidation
→ GPU work establishes invalid contents
→ later consumers use a compatible published resource window
```

这也是为什么旧 offset 不能在重分配后继续被 draw metadata 使用。

## 状态门三：dirty 到 upload set

这一门由 GPUScene upload preparation 推进。输入是已发布的 Scene/Proxy/instance source、各层 dirty state，以及当前 primitive/instance/payload allocation；输出是本次更新要使用的 CPU upload records、scatter destinations、clear ranges 和 setup prerequisites。后续 resource setup 与 clear/scatter producer 消费这组冻结后的工作描述。

Dirty 在这条正向链中的职责是圈定“哪些 GPUScene 表示需要重新编码”，让 preparation 不必每帧重建整库。它记录的是 CPU 侧变化责任；只有当它与当前源快照和目标 allocation 合并成 upload set 后，下一位 producer 才得到完整输入。

第 317 叶只改变 transform 和 custom data 时，更新可以被拆成以下责任：

1. 判断 primitive-wide、instance record、instance payload data 哪一层 dirty；
2. 从当前 Scene/Proxy/instance source 取得一致的 CPU 快照；
3. 构造 primitive、instance 或 payload upload records；
4. 为每条记录确定目标 primitive key、instance slot 或 payload offset；
5. 收集需要 clear 的旧范围；
6. 建立后续 RDG setup/prerequisite 所需的 upload set。

到这里成立的是 CPU 侧上传工作集，不是上传完成：它不能证明 RDG pass 已执行，更不能证明 GPU 已看到新值。

### 三种成本不能混写

| 成本 | 回答的问题 | 草叶案例 |
|---|---|---|
| Dirty item count | 多少 primitive/instance 需要重新编码 | 只有第 317 叶改变时可能只触及局部记录 |
| Record/write volume | 实际构造并 scatter 多少 primitive/instance/payload 数据 | custom data 改变可能同时触及 instance record 与 payload |
| Capacity change | 目标 buffer 是否需要注册更大容量或迁移 | 800→1300 可能越过当前 instance capacity |

CPU packing、GPU scatter 与 buffer resize 是三件不同的工作。减少 dirty 数不保证没有扩容；扩容也不等于 persistent primitive identity 需要改变。

### Upload set 护照

- **Source**：已发布 Scene 状态及当前 instance source。
- **Owner**：GPUScene upload preparation。
- **Output**：CPU upload records、scatter destinations、clear ranges 与 setup prerequisites。
- **Lifetime**：从准备开始到当前图 producer 消费完成。
- **Proof**：源数据与目标地址已经冻结到可建立 GPU work。
- **Not proof**：RDG pass 已执行、queue 已提交、GPU 已消费。

## 状态门四：GPUScene resources 与复合发布窗口

Upload set 还需要目标资源。GPUScene 会依据 persistent primitive high-water mark、instance allocator upper bound、payload 使用量以及当前配置，注册或扩展 primitive、instance 和 payload buffers，并建立相符的容量与寻址参数。

### 复合发布窗口怎样成立

这一门由 GPUScene 与 RDG 共同推进。它们接收 upload set 给出的容量、目标地址和布局需求，输出当前图中可被后续 pass 引用的 buffers/SRVs，以及与这些资源匹配的 capacity、high-water mark、stride、tile/SoA 和 range 参数。clear/scatter producer 向这些资源写入，Scene/View resource entrances 随后把同一组参数交给 readers。

本章把这组相互匹配的资源与参数称为“GPUScene resource window”或“复合发布窗口”。一个可消费窗口至少由以下对应关系共同成立：

- primitive、instance、instance payload buffer/SRV；
- primitive high-water mark 与 max allocated instance ID；
- capacity、stride、tile/SoA 寻址参数；
- Scene frame 状态；
- persistent 与 dynamic primitive ranges；
- Scene/View uniform 中暴露的资源入口；
- culling context 使用的 View 和 output mapping。

这是 UE 在本路径里采用的复合一致性合同，不是一个足以验证全部关系的 `GPUSceneVersion` 整数。只更新其中一个指针或 offset，不能证明整个窗口一致。

### 逻辑记录不等于物理 `struct[]`

为了教学，可以把每个 instance 想成一条逻辑记录；物理实现却可以采用 SoA、tiled 或压缩布局。shader 必须使用与 CPU packing 同一合同中的 tile size、stride、offset 与 flags 解码。状态跟踪粒度、逻辑记录粒度和 backing allocation 粒度也不必相同。

Buffer resize 可以改变底层资源入口和容量，却不要求 persistent primitive identity 改变。旧内容如何复制或迁移由具体 resize 路径控制，不应假定永远原地扩展，也不应假定所有 Scene 数据都必须重新生成。

## 状态门五：clear 与 scatter producer work

这一门由 GPUScene upload path 在 RDG 中推进。它接收 upload set、已注册的目标 resources 与待清理 ranges，输出 clear/scatter passes 以及这些 writers 到后续 readers 的图内依赖。Scene/View resource entrances、shader 和 Instance Culling 是这组 producer work 的下游 consumers。

正向数据变化是：

```text
CPU upload records + scatter destinations + clear ranges
→ RDG buffer registration / resize decisions
→ clear and scatter passes
→ dependency edges to later consumers
```

这一步建立的是图内生产关系。RDG 可以保证声明正确的 consumer 位于 producer 之后，但不能把“调用过 upload 函数”当成 GPU 已执行的证据。

完成深度必须保持为：

```text
RDG producer work established
→ RDG execution records RHI work
→ RHI/backend translate and platform command formation
→ platform queue submit
→ GPU executes clear/upload
→ dependent shader or culling consumer observes the data
```

本章只需要知道这些层级不同，不展开 RHI 内部实现。图内依赖也不是全设备 GPU-complete fence，不能据此断言任意图外复用或原生资源退休已经安全。

## PrimitiveSceneData：primitive-wide ABI

`PrimitiveSceneData` 可以理解为 GPUScene 中按 primitive lookup 访问的 primitive-wide 数据合同。它不是 `FPrimitiveSceneInfo` 的内存拷贝，而是为 shader 消费重新编码的 ABI。

- **What**：primitive 级共享状态的 GPU 表示。
- **Why**：同一 primitive 的许多 instances 与 passes 需要复用 transform 基准、bounds、flags 和其他共享数据。
- **Owner**：GPUScene 资源与当前复合窗口；源事实仍来自 Renderer Scene。
- **Key**：persistent 或经 dynamic 翻译后的 primitive lookup。
- **Lifetime**：与当前 GPUScene allocation/publication window 一致。
- **Control**：dirty/upload producer 更新；Scene/View uniform 暴露入口。
- **Boundary**：它不选择材质，不证明当前 View 可见，也不是完整 Scene 树。
- **Debug**：整片草在所有 GPUScene 消费路径中位置都旧，优先检查 primitive dirty、lookup 与 resource window。

材质、shader permutation 与 pass state 由第 07 章的 mesh pass 决策建立。Primitive ID 只帮助后续 shader 找到对象数据，不能替代材质选择。

## InstanceSceneData 与可选 instance payload

一个 instance lookup 首先进入 instance record，再反查 primitive-wide 数据和可选扩展内容：

```text
InstanceId
→ InstanceSceneData
   ├─ primitive lookup
   ├─ relative instance identity
   ├─ flags / transform data
   └─ optional instance payload location
→ PrimitiveSceneData
```

这张图表达逻辑关系，不承诺固定 C++ struct 或固定 bit layout。

### InstanceSceneData 护照

- **What**：实例级核心 lookup 与 transform 合同。
- **Why**：大量 instances 共享 primitive-wide 数据，却需要各自位置、相对编号和状态。
- **Owner**：当前 instance allocation 与 GPUScene resource window。
- **Data**：primitive lookup、relative instance ID、flags、transform 或其编码入口。
- **Conditions**：具体字段和 packing 受配置、shader permutation 与布局合同约束。
- **Boundary**：instance slot 是位置，不是对象永久身份；record 也不拥有权威 Component 状态。
- **Debug**：只有第 317 叶错误时，优先检查其 slot、relative ID、flags 与 payload，而非重建整个 primitive identity。

### Instance payload 护照

Instance payload data 是可选的 per-instance 扩展区。是否存在、怎样寻址、每项多宽，由 flags、offset 与 stride 共同控制。不是每个 instance 都有相同 payload，也不能把 payload 想成总在 PrimitiveSceneData 之后的一段固定结构。

必须避免名称混淆：

- **instance payload data**：GPUScene 中可选的实例扩展内容；
- **culling/draw work metadata**：Visible draw work、range、output mapping、indirect 参数等本帧提交元数据。

## Large World：高位基准与相对数据必须使用同一合同

远离世界原点时，直接把完整世界坐标压成单个低精度 float 矩阵会放大误差。GPUScene 的数据合同需要保存足以重建高精度位置的高位基准与相对数据；具体采用 tile-offset、high/low 或其他压缩路径，取决于当前 shader permutation 和数据布局合同。

对第 317 叶而言：

```text
primitive high-position / reference
+ instance relative transform
→ shader-side reconstructed position
```

CPU encode 与 shader decode 必须属于同一路径条件。若近处正确、远处才抖动，ID、slot 和 culling 可能都已成立；更高信号的嫌疑是 high-position/reference、relative transform 或布局参数不匹配，而不是材质本身。

## 状态门六：resource entrances 暴露当前窗口

这一门由 Scene Uniform、View state 与相关 context 推进。它们接收已经建立的 GPUScene resources、layout/frame 参数、persistent/dynamic ranges 和图内 producer 关系，输出一组绑定到当前消费窗口的 Scene/View 参数。shader 与 Instance Culling 通过这些入口取得相符的 SRV、capacity、layout 和 range，而不是各自寻找 GPUScene 内部的“最新指针”。

这能证明：

- 后续图内 consumer 获得目标 GPUScene 资源集合的参数入口；
- primitive/instance/payload 寻址参数和当前 window 可以一起传递；
- RDG 能根据 producer/consumer 声明建立执行顺序。

它不能证明：

- upload GPU work 已经在调用点完成；
- platform queue 已经 submit；
- GPU 已越过最后消费者；
- 任意 CPU readback、覆盖复用或 native retirement 已经安全。

因此，resource entrances 的正向职责是把 readers 接到同一资源窗口；它的边界才是“逻辑读取入口成立，不等于 GPU completion”。

## Dynamic primitive：更窄的发布窗口

Dynamic primitive 不占用长期 Scene persistent identity。它由具体 collector/context 在当前帧或当前 View/path 的消费窗口中收集，并取得临时 primitive、instance 与 payload ranges。

安全的状态链是：

```text
collector gathers
→ commit freezes current inputs
→ dynamic primitive range allocated
→ instance / payload ranges allocated
→ dynamic upload records and RDG work established
→ matching ranges exposed to View and culling context
→ current-window consumers use them
```

Commit 能证明 collector 输入已冻结、dynamic ID range 和 CPU allocation 已确定；它不能证明 upload pass 已执行、Scene/View entry 已刷新、Instance Culling 已看到数据或 GPU 已消费。

Dynamic 也不必然意味着“每个 View 都有独立物理 buffer”。哪些 upload data 可以共享、range 是否按 View 独立，取决于具体 collector 与渲染路径。真正的风险是：dynamic identity、instance range、GPUScene resource entrance 和 consumer View 不属于同一个复合窗口。

一个只在某个 View 中存在的临时 gizmo 可作为对照：若 persistent 草丛正常而 gizmo 消失，优先检查 collector commit、dynamic translation、View range 和 culling context matching，而不是先怀疑共享 primitive buffer 全部损坏。GPU writer delegate 等特殊 GPU 生成路径只是条件扩展，不是所有 dynamic primitive 的固定步骤。

## 状态门七：Instance Culling 与 shader 的消费合同

这一门由实际 readers 推进。shader 接收 primitive/instance lookup 与 Scene/View resource entrances，产出对当前 GPUScene 记录的解码结果；Instance Culling 接收 visible draw work、View、候选 range/runs、dynamic translation、output mapping 与同一组 GPUScene entrances，产出后续 draw 可使用的 visible-instance indirection 和 direct/indirect instance count。后续 draw recording 消费这些结果。

Instance Culling 因而需要的不只是一段 instance range：

| 输入 | 所有者 | 用途 |
|---|---|---|
| Visible draw work | 第 07 章 draw pipeline | 指定要处理的 command/work item |
| Primitive/dynamic metadata | Draw command/work setup | 解释 persistent 或 dynamic lookup |
| Instance range/runs | Draw/culling setup | 指定候选 instances |
| Culling View | 当前 View | 定义本次筛选条件 |
| GPUScene entrances | Scene/View parameters | 提供 primitive、instance、payload 数据 |
| Dynamic offsets | 当前 dynamic context | 翻译临时 ID/range |
| Output mapping | Culling work | 将结果对应到 indirect/direct draw work |

在这些输入一致时，Instance Culling 可以生产：

- packed visible instance indirection；
- direct instance count 或 indirect arguments；
- instance/data offsets；
- 可选 compaction、run 或多 View 元数据。

这套消费合同的边界是：Instance Culling 读取 GPUScene，但不拥有权威 transform，也不重新选择材质或 pass shader；这些源状态与 draw 决策分别属于上游 Scene/GPUScene 和第 07 章 draw pipeline。

### InstanceIdsBuffer 不是 Scene 副本

`InstanceIdsBuffer` 一类结果保存的是 packed indirection 及路径所需附加位。它不复制完整 transform，不拥有 primitive source state。后续 shader 解出 instance ID 或附加元数据，再回到 GPUScene 读取 instance 与 primitive data。具体 packing 由路径和 shader 合同决定，不能把教学图当成固定 bit layout。

### Indirect args 不是 draw completion

Indirect arguments 成立只能证明 GPU culling 已为后续 indirect draw 准备参数：

```text
culling output ready
≠ RHI draw recorded
≠ platform queue submitted
≠ draw executed
≠ image presented
```

如果普通 GPUScene 读取正确，而 GPU-driven instance count 为零，生产数据库通常不是首要嫌疑；应检查 visible draw work、range、View、output mapping、InstanceIdsBuffer 与 indirect args。

## 贯穿案例：800 叶草的状态账本

| 阶段 | 当前权威数据 | Owner/Controller | 成立证据 | 尚不能推断 |
|---|---|---|---|---|
| Scene published | SceneInfo、persistent identity、800 个 instance source | `FScene` | 当前 Renderer Scene 可查询 | GPUScene 已更新 |
| Allocation | instance/payload ranges | GPUScene CPU allocators | offset/count/stride 已确定 | GPU buffer 已写入 |
| Upload set | 第 317 叶的 instance/payload records | GPUScene upload preparation | dirty item、源快照与 scatter 目标成立 | RDG producer 已执行 |
| Resources | primitive/instance/payload buffers、capacity、layout | GPUScene + RDG | SRV 与寻址参数构成复合窗口 | queue 已提交 |
| Producer work | clear/scatter passes | RDG | consumer 被排序在 producer 之后 | 全设备 GPU complete |
| Entrances | Scene/View parameters、dynamic ranges | Scene/View state | consumer 指向目标窗口 | culling output 正确 |
| Culling output | visible packed IDs、indirect args | Instance Culling | 第 317 叶是否进入输出可检查 | draw 已执行 |
| GPU consumption | draw 读取 indirection 与 GPUScene | RHI/backend/GPU | 匹配的 GPU/capture evidence | 其他 queue 或 Present 完成 |

### 变体 A：只移动第 317 叶

Primitive-wide 数据可以不变，instance transform record 必须 dirty；若 custom data 也改变，还需判断它位于 instance core record 还是 instance payload data。正确的增量路径不应默认重传 800 叶全部内容。

若整片草位置正确、只有第 317 叶仍在旧位置，最后成立状态通常已越过 primitive resource window，应检查该 instance 是否进入 upload set、scatter target 是否指向当前 slot，以及 relative ID/payload stride 是否一致。

### 变体 B：从 800 叶增加到 1300 叶

实例数量改变可能触发：

```text
new instance allocation
→ old range scheduled for clear
→ draw/culling range and offset updated
→ target capacity possibly resized
→ new upload records and producer work
```

Persistent primitive identity 可以保持不变，但旧 instance offset 不再可靠。扩容后随机串位，优先检查 realloc、旧 range clear、新 offset、capacity 与 resource entrance 是否属于同一窗口。

### 变体 C：只有远离原点时抖动

若近处正确，说明 identity、slot、基本 producer 与消费链很可能成立。优先检查 primitive high-position/reference、instance relative transform、CPU packing 与 shader decode 的路径条件，而不是先怀疑 material 或 indirect args。

### 变体 D：普通读取正确，GPU-driven 数量为零

这说明 GPUScene primitive/instance 内容大概率已经可读。下一步检查 draw work 是否携带正确 primitive/dynamic metadata、instance range 与 View，InstanceIdsBuffer 是否生成正确 indirection，以及 indirect args 是否映射到目标 draw。不要把“重新分配 persistent identity”作为第一反应。

## 唯一调试模型：寻找最后成立状态

### 1. Scene publication

检查 SceneInfo 是否处于当前驻留期、persistent identity 是否有效、是否能映射到当前 packed index、Proxy 重建后是否误用旧 identity。失败时回到第 02 章。

### 2. GPU allocation ledger

检查 instance offset/count、instance payload offset/stride、实例数量变化后的 realloc，以及旧 range 是否进入 clear 责任。失败时不要先查 shader。

### 3. Upload set

检查 primitive/instance/instance payload 哪一层 dirty、CPU record 是否读取当前 Scene 快照、scatter target 是否指向当前 allocation，以及条件性 GPU writer 是否真正登记 producer。

### 4. GPUScene resource window

检查 primitive/instance/payload SRV、capacity、high-water mark、max instance ID、layout/tile 参数、frame 状态、dynamic range 与 Scene/View parameters 是否可以被同一合同解释。

### 5. Producer-before-consumer

检查 clear/upload RDG pass 是否存在、访问是否声明、consumer 是否依赖 producer。此处只能证明图内顺序，不能用作全局 GPU-complete 证据。

### 6. Shader lookup

检查 primitive lookup、dynamic translation、InstanceId、relative instance ID、flags、instance payload offset/stride，以及 Large World encode/decode 合同。

### 7. Instance Culling inputs

检查 visible draw work、range/runs、View、dynamic offsets、GPUScene entrances 与 output mapping。普通读取正确而此处失败时，生产数据库通常不是第一嫌疑。

### 8. Culling output

检查 packed visible instance indirection、indirect instance count、compaction/order 数据与目标 draw 的映射。

### 9. RHI/GPU consumption

最后才区分：

```text
RDG work established
→ RHI work recorded
→ platform command formed
→ queue submitted
→ GPU executed upload / culling / draw
```

| 症状 | 最后可能成立状态 | 优先检查 |
|---|---|---|
| 整片草所有路径都停在旧位置 | Scene publication 或 allocation | dirty、persistent mapping、upload set |
| 只有第 317 叶错误 | primitive resource window 已成立 | instance slot、relative ID、instance payload |
| 增加实例后随机串位 | 旧 allocation 曾正确 | realloc、draw range、clear、新 offset |
| 近处正常、远处抖动 | ID/slot 链成立 | high-position/reference 与 shader decode |
| Persistent 正常、dynamic 消失 | 共享 buffers 大概率成立 | collector commit、dynamic range、View entrance |
| 普通读取正常、GPU-driven 为零 | GPUScene 内容大概率成立 | culling work、range、View、indirect args |
| Capture 有 upload、draw 仍读旧值 | producer work 存在 | resource entrance、consumer lookup、ordering |
| Queue 已提交但资源仍不可复用 | submit 已成立 | 匹配最后消费者的 GPU completion evidence |

## 章节出口

学完本章，应能回答：

1. Scene publication、GPU allocation、upload set、RDG producer、resource entrance 和 GPU consume 分别到哪一级？
2. 为什么 `PackedIndex`、persistent primitive identity、instance slot 和 relative instance ID 不能互换？
3. 为什么 dirty、allocation、collector commit 或 Scene Uniform 更新都不等于 GPU 已看到新数据？
4. 为什么“版本”必须理解为一组相互匹配的 buffers、容量、布局、ranges 与 consumer entrances？
5. 为什么只有一个 instance 错、远处才抖、dynamic 消失、GPU-driven 数量为零分别指向不同责任层？

下一章将接管 draw consumption ledger：MeshBatch 如何被 pass-local processor 压成 MeshDrawCommand，当前 View 如何建立 Visible Command，Instance Culling 输出又怎样进入 RHI draw recording。第 08 章则解释这些 collector、upload、uniform 和 culling context 在 Frame Init 的哪些阶段门启动与收口。
