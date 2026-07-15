# 14 Translucency：半透明如何在一帧末段接回 SceneColor

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `05_RenderGraph.md`、`07_MeshDrawCommand.md`、`08_FrameInit.md`、`10_BasePass.md`、`12_Lighting.md`、`13_Atmosphere.md`。本章把 opaque、lighting、atmosphere、water、hair 前段的结果当作半透明阶段的**输入合约**来处理。  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）

## 开篇：半透明把既有 SceneColor 推进到分支化合成

半透明接手时，opaque、lighting 与 atmosphere 已经共同发布了可供后续阶段读取的 `SceneDepth` 和当前 `SceneColor`。Renderer 接下来要把每个透明材质的长期意图解析成当前 view 的 pass 路由，在各自排序域内形成 draw work，再让直接目标、separate 资源、distortion 或 post consumer 把结果接回颜色链。Unity 常见的“切到 Transparent 队列、按距离排序、打开 blend”只覆盖这条链中的一个局部步骤。

到第 14 章时，UE 已经走完了最适合 deferred 管线的部分：opaque 表面写好了深度和 GBuffer，延迟光照把结果累积进 `SceneColor`，大气、雾、云、水和部分 hair 阶段也已经按顺序改写过这张主场景颜色。半透明这时才进场。它要回答的核心问题，不是“怎么混一个 alpha”，而是：

> **一个依赖既有深度与背景颜色、却不参加 opaque GBuffer 延迟光照累积的透明物体，怎样在 UE 的一帧调度中获得 pass、顺序、光照输入和明确 consumer，最后安全地改变 `SceneColor`？**

这一句里藏着的并不是一个“透明算法”，而是一条状态链：**材质先声明想在何时、以何种方式参与透明；Renderer 再结合当前 view、项目设置和其他效果，解析出本帧真正的调度；随后才轮到排序、光照、输出资源和合成。** 如果把“材质意图”和“最终调度”当成同一件事，就会误以为勾选一个选项必然对应一个固定 pass，也会在 Auto Before DOF、distortion、underwater 或平台降级发生时找错责任人。

UE 没有用一个透明 pass 同时解决全部问题，而是把不同约束拆给不同机制。下表是本章的地图：

| 难题 | UE 的拆法 | 拆分带来的必要边界 |
| --- | --- | --- |
| 材质意图如何变成本帧调度 | 材质提供 pass、blend、lighting 等意图；Renderer 结合 view 和功能条件解析最终 pass mask | 材质不知道当前 view 是否允许 AfterDOF、是否因 distortion 分离 Standard、是否处于 underwater 路径 |
| 物体之间谁先画 | 每个透明 `EMeshPass` 各自建立 visible draw command 队列并排序 | Standard、AfterDOF、AfterMotionBlur、holdout、modulate 的**合成时机**不同，不能混进同一个排序域 |
| 同一像素里多层透明怎么正确合成 | 可选 OIT（顺序无关透明）：sorted triangles 或 sorted pixels | per-object 距离排序处理不了相交三角形和同像素多层片元 |
| 透明物体从哪里拿光照 | Translucency Lighting Volume、Lumen front-layer、ray tracing 等旁路输入 | 透明不写 GBuffer，走不了普通 deferred 的逐像素光照累积 |
| 需要背景颜色的材质怎么建立明确依赖 | 按需创建 `SceneColorCopy`，或把颜色写入 separate 资源后再合并 | UE 选择显式快照与独立目标来获得跨 RHI、跨平台可预测的读写顺序；少数硬件的 framebuffer fetch 或 subpass input 不能当作通用前提 |
| 特定透明结果何时进入最终画面 | Standard 可直接合成；AfterDOF / AfterMotionBlur 交给后处理 | DOF、motion blur、TSR 等后处理会改变透明该接入的位置 |

## 贯穿案例：一块染色玻璃和一片烟雾

为了让这些机制不变成彼此孤立的词条，本章用两个具体物体贯穿始终：

- **一块带折射的染色玻璃**：它代表“标准透明 + distortion（折射）”，并在 FrontLayer 的 view、material 与 pass gate 同时成立时提供第一层几何证据。它会同时触碰队列路由、排序、透明光照、separate 资源和折射合并——几乎覆盖本章每一节。
- **一片 AfterDOF 烟雾**：它代表“separate translucency 资源先保存、后处理阶段再合成”。它演示的是“透明着色完成 ≠ 已经进画面”这条最反直觉的边界。

读完整章，你应该能对任意一个透明物体回答这五个问题：它落在哪个队列里，谁拥有它产生的资源，输出写到哪里，下一步由谁消费，出错时该沿哪条资源线倒查。

## 本篇边界

本章属于 Part 3“一帧流程”，只讲半透明在这一帧里的接入点、输入输出、资源流向和调度位置。透明材质编译、Lumen 算法、完整 ray tracing pipeline、后处理链内部算法，都只讲到“能解释当前接入点为止”。

| 概念 | 本章讲到 | 不在本章展开 |
| --- | --- | --- |
| 材质系统 | 材质如何声明透明意图，以及 Renderer 怎样解析最终 pass | Material Editor 到 shader permutation 的完整编译链 |
| MeshDrawCommand | 透明 pass 如何使用 draw command 队列和排序 key | draw command 缓存、PSO 构建、instance culling 的完整实现 |
| Lumen / RT | FrontLayer 和 ray traced translucency 如何把结果接回透明合成 | Lumen surface cache、硬件 RT traversal、denoiser 全算法 |
| PostProcessing | AfterDOF / AfterMotionBlur 资源被交给后处理 | TSR、DOF、MotionBlur、Tonemap 的完整链路（第 15 章） |
| OIT | sorted triangles / sorted pixels 在透明阶段解决什么顺序问题 | 每个 shader 分支和样本排序算法的百科式展开 |

## 本篇必须能回答

- 半透明进场时拿到的是怎样一组输入？它的输出为什么不总是立刻写回 `SceneColor`？
- 一个透明材质凭什么进了 Standard 而不是 AfterDOF？谁来做这个路由？
- `FTranslucencyPassResourcesMap` 是什么？为什么“着色完成”和“合成进画面”要分开？
- 默认透明排序解决了什么、解决不了什么？OIT 的两条路径分别补哪一层？
- 透明不写 GBuffer，那它的光照、折射、Lumen/RT 反射分别从哪条旁路接回主颜色？

---

## 先把本章几个新词放稳

半透明这一段容易读散，是因为同一个物体会同时穿过“队列、资源表、排序、光照旁路、合成节点”几层语言。先把这些名字当成资源护照看：每个名字都回答“它装了什么、谁拥有、下一步交给谁、调试时问什么”。

| 名字 | 它装了什么 / 携带什么 | 谁拥有或推进 | 下一步交给谁 | 调试时先问 |
| --- | --- | --- | --- | --- |
| 透明 `EMeshPass` 队列 | 当前 view 里属于某个合成时机的透明 draw commands | Renderer 的可见性和 mesh pass setup | 对应 pass 的绘制与排序 | 物体进了正确的 pass mask 吗？ |
| 排序域 | 同一个 pass 内可相互比较的 visible draw commands | MeshDrawCommand pass setup | RHI 记录或 OIT 旁路 | 两个透明体是不是其实在不同 pass？ |
| `FTranslucencyPassResourcesMap` | 每个 view/pass 的 color、modulate、depth 等 RDG 资源 | Renderer/RDG 当前帧图 | distortion、upscale、post processing | shader 画完后资源有没有被后续消费？ |
| OIT sample data | 同像素透明片元的颜色、透过率、深度样本 | OIT pass 的 GPU UAV 和 compose | pass 目标颜色或 ResourceMap | 是 object 顺序错，还是像素样本没 compose？ |
| TLV | per-view 两级体积光照纹理 | Renderer 的透明光照阶段 | 选择体积光照模式的透明 draw | 全黑是没注入、没过滤，还是 lighting mode 根本不采？ |
| Distortion 资源 | 折射偏移、stencil 区域、扭曲后的背景 | distortion mesh pass + fullscreen apply/merge | `SceneColor` 或 Standard 资源合并 | 有偏移、有背景、有 Standard 颜色吗？ |
| FrontLayer | 透明第一层的 normal/depth | Lumen/Renderer front-layer pass | Lumen front-layer reflections / RT translucency | 最终颜色错之前，第一层证据存在吗？ |
| RT translucent 输出 | final radiance 与 background visibility | Lumen/RT translucent 路径 | `SceneTextures.Color` 或 ResourceMap[Standard] | 当前帧允许直接改色，还是必须等 merge？ |

本章后面所有机制都沿这张表展开。读半透明时不要只问“它有没有画”，而要问：“当前结果停在哪个护照上，下一位消费者是谁？”

## 1. 输入合约：半透明进场时手里有什么

透明阶段不是 BasePass 的延长线。它进场时，主场景已经被前面所有阶段改写过，于是它拿到的是一组**已经稳定**的输入。把这组输入想成一份合约，比想成“一堆纹理”更有用——因为合约规定了哪些能读、哪些不能直接写、谁负责维护：

| 输入 | 它是什么 | 透明阶段怎么用它 |
| --- | --- | --- |
| `SceneDepth` | opaque / water 等阶段留下的深度 | 透明做 depth test 的依据 |
| `SceneColor` | 已经光照、大气合成过的主颜色 | 某些透明 pass 的直接合成目标，也是按需背景快照的来源 |
| `SceneColorCopy` | 条件成立时创建的背景颜色快照 | 当前 view 的实际 consumer 集合需要时才创建：普通透明由可见 pass relevance 汇总，满足条件的 Single Layer Water 也可触发；underwater view 会跳过这份 copy |
| `View` / `FScene` | 可见性、排序、材质 relevance、per-view 参数 | 决定哪些透明物体可见、怎么排序 |
| TLV / Lumen 数据 | 透明专用的低频或 front-layer 光照输入 | lighting mode 或专用透明路径按需采样（见第 7、9 节） |

这份合约的责任划分很清晰。CPU 侧由 Renderer 的 render thread 和它调度出的并行任务维护；GPU 侧资源的生命周期和依赖由 RDG 描述。Game Thread 只在更早的阶段更新组件、材质和 proxy 状态，它不会在这里直接改透明队列。

**为什么要专门设一份合约，而不是让透明 shader 自由读取正在写的颜色？** 因为玻璃、热浪和某些混合模式需要“看见”已经存在的背景。如果输入和输出都指向同一张颜色附件，结果会依赖具体 RHI 是否支持 framebuffer fetch、subpass input 或其他受限的同附件读取语义，还会依赖 pass 组织方式。那不是所有 UE 平台都能采用的稳定合约。UE 的通用选择是把“读哪一版背景”和“把结果写到哪里”显式分开：Renderer 先汇总当前 view 的**实际 consumer 集合**，集合非空时才创建 `SceneColorCopy`；输出则直接 blend 到当前目标，或写进 `FTranslucencyPassResourcesMap` 等后续节点消费。

这个 consumer gate 不只看“有没有普通透明材质”。普通透明由各可见 pass 的 material relevance 汇总是否使用 Scene Color；支持 SM5 material nodes 的对应平台上，可见 Single Layer Water material 也会让同一 copy 路径成立。反过来，调用这条 copy 路径时 underwater view 会被跳过，因为水下透明走自己的目标与依赖。没有 copy 时，透明 uniform 可以落到安全的 fallback；因此看到黑色背景采样时，第一步应先判断 consumer 类型与 gate，而不是直接断言拷贝 pass 丢失。

这不是说硬件在物理上永远不能同附件读写，而是说 **UE 不把某个平台特有的隐式可见性当成默认跨平台语义**。显式拷贝会增加一次纹理读写和显存带宽，所以没有消费者时不应创建；framebuffer fetch 或 subpass input 可能省掉拷贝，却限制平台、shader 写法、MSAA 或 render-pass 组织。若项目只面向明确支持这些能力的固定平台，专用路径可能更合算；通用 Renderer 则优先选择可移植、可调试的依赖。

把这份合约展开成一帧里的正向主线，本章只沿下面一条状态链推进。可选旁路的具体位置会受渲染路径影响，因此这张图表达的是责任转移，不是所有平台共享的一张绝对 pass 时间表：

```text
材质声明：composition intent + blend mode + shading model + translucency lighting mode
  -> Renderer 解析本帧 schedule 和 pass mask
  -> 每个 pass 建立 visible commands，并做 per-object sort / 可选 OIT
  -> 透明着色先按 shading model 判定是否受光；lit transparent 再按 lighting mode 读取 TLV 或 forward lights
  -> 输出分叉：直接写当前颜色目标，或写 separate color/modulate/depth 资源
  -> 可选 distortion / FrontLayer / ray-traced translucency 旁路按各自合约交接
  -> Standard 类结果在透明 block 内合成，延迟类结果交给 PostProcessing 消费
```

回到我们的两个物体：本章玻璃声明 Standard 颜色与 distortion 意图；当 distortion 侧要求保留 Standard 结果、当前 pass 是 Standard 且 `AllowStandardTranslucencySeparated()` 成立时，颜色改写 separate 资源，等 distortion 合并时一起写回 `SceneColor`。烟雾的 AfterDOF 意图若通过 view-family gate 且未被 Auto Before DOF 移走，在第 14 章末尾**不会**立刻进主颜色，而是把颜色和调制资源交给第 15 章的后处理链。

这条合约在调试时非常省力：如果透明体完全不显示，先别看 shader 数学，先确认它**进没进对的 mesh pass**、**该 pass 在当前 view 有没有被渲染**、**输出是直接写 `SceneColor` 还是落进了 ResourceMap**。如果是背景采样错误，先分清 consumer 是普通透明、Single Layer Water 还是 underwater view，再查 `SceneColorCopy` gate、copy 版本与绑定，而不是一律怀疑透明材质或 GBuffer。

## 2. 从材质意图到 Renderer 的本帧调度

第 1 节说透明输出可能落在不同地方。这里必须先纠正一个容易造成整章误判的说法：**材质声明的是意图，Renderer 决定的是当前 view 的最终调度。** 材质可以表达“希望在 DOF 前或后合成”“使用何种 blend”“是否需要 distortion”“采用哪种透明光照模式”；它却不知道本帧是否启用 DOF、项目是否允许 AfterDOF、相机是否触发 Auto Before DOF、Standard 是否因 distortion 而需要分离，也不知道当前 view 是否处在水下专用路径。

最终路由由 Renderer 在本帧 view 条件下解析。可以把输入和控制量分成三层：

| 层次 | 提供什么 | 为什么不能单独决定最终 pass |
| --- | --- | --- |
| 材质与 primitive | composition intent、blend/modulate、distortion、shading model、lighting mode、排序优先级等 | 只描述内容需求，不掌握本帧 view 和平台能力 |
| View family 与项目策略 | 是否允许 AfterDOF、是否启用 Auto Before DOF、是否允许 Standard 分离、OIT 与 RT 能力 | 能改变或降级材质意图，但不产生具体 visible draw |
| Renderer 当前帧状态 | 可见性、水下路径、distortion 是否实际需要、pass mask、目标资源 | 把前两层解析成当前 view 真正执行的队列和资源分叉 |

正向过程如下：

```text
Material / proxy 声明透明意图
  -> Renderer 读取当前 view 与项目条件
  -> 解析 composition pass：Standard / AfterDOF / AfterMotionBlur / ...
  -> 解析 blend 资源：普通 color、modulate、holdout 等
  -> 必要时附加 Distortion 或 FrontLayer 旁路
  -> 为当前 view 设置 EMeshPass mask
  -> 每个 pass 独立收集、筛选和排序 visible draw commands
```

几个会改变直觉结果的条件必须单独讲清：

- **AfterDOF 不是绝对承诺。** 当 `AllowTranslucencyAfterDOF` 对当前配置不成立时，Renderer 不再维持正常的 Standard/AfterDOF 主分段，而会采用聚合路径完成透明绘制。材质仍保存原始意图，但这一帧的执行计划已经降级。
- **Auto Before DOF 是距离相关的调度修正。** 某些原本可在 AfterDOF 的对象，如果沿 view forward 的距离大于自动边界，也就是落到边界之后的 DOF 背景侧，会被移动到 Standard，使它参加 DOF；边界以内、非背景侧的对象仍留在 AfterDOF。这样可以避免背景侧透明保持清晰却压在模糊背景上。代价是相机或对象跨越边界时，合成时机变化可能带来视觉跳变。
- **Standard 是否写 separate 资源由三个 gate 共同决定。** distortion 侧先声明后续 merge 需要保留 Standard 结果；当前正在处理的必须是 Standard pass；view family 的 `AllowStandardTranslucencySeparated()` 还必须成立。三者同时满足才写 separate 目标，否则 Standard 沿当前直接目标合成。
- **Underwater 会改变目标资源和组合顺序。** 水下路径可以绕过常规的 direct/separate/composite 分段，把透明接入水下颜色缓冲的专用路线；因此不能把桌面 deferred 的“Standard 后接 distortion、AfterDOF 留给后处理”机械套过去。
- **Blend 与 modulate 会影响输出附件。** 普通颜色与“乘到背景上的透过/调制”不是同一种合成量，因此可能进入 color 与 modulate 两类资源，后续消费者也必须同时读取。

`AllTranslucency` 尤其容易被误教。在 mesh pass 调试里可看到相应的 `TranslucencyAll` 锚点，但它不是和 Standard、AfterDOF 并列、由美术在材质上正常选择的“万能合成类别”，而是一个**聚合或回退桶**：当 staged translucency 被关闭，或调用环境不采用主 Renderer 的常规分段时，用它一次覆盖应绘制的透明集合。把它当普通材质类别会产生两个坏处：一是误以为对象可以同时自由选择 All 与其他 pass；二是调试时看见 All 就去改材质，忽略真正改变调度的是全局策略或调用上下文。

这条调度链的 O/D/C/L 可以这样记：

- **Owner**：材质和 primitive 拥有长期意图；Renderer 拥有当前 view 的最终 schedule；mesh pass 系统拥有当前帧 visible command 列表。
- **Data**：输入是 proxy、material relevance、view flags 和功能条件，输出是 pass mask、排序域以及目标资源选择。
- **Control**：Renderer 决定何时分段、何时聚合、何时附加旁路；worker 可以并行准备 pass，但不回写 Game Thread 对象。
- **Lifetime**：材质意图可跨帧存在；最终 pass mask、visible commands 和资源分叉只对当前 view、当前帧有效。

回到两个物体。染色玻璃分别声明 shading model、surface lighting mode、普通透明和折射意图。Renderer 在确认当前帧需要 distortion 后，让它进入 Standard 颜色路线，并额外进入 Distortion。FrontLayer 旁路还要求 view 启用 Lumen front-layer reflections，primitive 可进 main pass，material domain 可进入 mesh pass，材质声明 front-layer 写入且没有选择 AfterMotionBlur；这些 gate 全部成立才附加只写 normal/depth 的命令。AfterDOF 烟雾只是先声明“希望 DOF 后合成”，Renderer 还要检查 AfterDOF 是否允许、对象是否落在自动边界之后的 DOF 背景侧、当前是否处于特殊水下路径，最后才建立 AfterDOF 或被移动后的 Standard visible commands。

**Unity 桥接**：Unity 的 `RenderQueue` 常让人把“材质队列值”直接等同“最后 draw 时机”。UE 更像是材质先提供 routing hints，Renderer 再把这些 hints 与当前 frame graph 条件求交集。代价是调度更复杂；收益是 DOF、motion blur、distortion、RT 和特殊 view 路径都能得到明确的资源边界。

这一节的失败模式应沿“意图是否正确 → 条件是否允许 → 最终 pass 是否成立”检查：

| 症状 | 优先检查 |
| --- | --- |
| 透明体完全没画 | material relevance 是否声明透明、最终 pass mask 是否包含目标 `EMeshPass`、processor 是否接受该 mesh/material |
| AfterDOF 物体进了 Standard | AfterDOF 是否允许、对象是否位于 Auto Before DOF 边界之后的 DOF 背景侧、是否发生特殊路径降级 |
| 看见 `TranslucencyAll` | 先查 staged translucency 与调用上下文，不要先把它解释成材质选择 |
| Distortion 有颜色无折射 | Standard 颜色路线成立，但附加 Distortion pass 可能没有成立 |
| FrontLayer 反射缺失 | front-layer 资格、view 条件和独立 pass 是否成立，而不只是普通透明颜色是否可见 |

## 3. ResourceMap：保存等待指定 consumer 的透明中间结果

第 2 节只建立了本帧调度。接下来 Renderer 才决定每类结果是直接写当前颜色目标，还是先保存在独立资源中。承接后者的 `FTranslucencyPassResourcesMap` **不是透明材质的全局缓存**，而是当前帧、当前 view、按透明 pass 分类的 RDG 资源索引。它让生产者和消费者用“这是 Standard、AfterDOF 还是 AfterMotionBlur 的 color/modulate/depth”交流，而不必假设这些结果已经处在 `SceneColor` 中。

```text
Renderer 得到最终 translucency schedule
  -> 对每个 pass 选择直接目标或 separate 目标
  -> 需要 separate 时，在 ResourceMap 建立该 view/pass 的资源条目
  -> 透明 draw 写 color，必要时写 modulate，并使用匹配的 depth
  -> Standard 条目可由 distortion merge 在透明 block 内消费
  -> AfterDOF / AfterMotionBlur 条目交给后处理节点消费
```

**Owner。** Renderer 拥有资源表的组织和交接，RDG 拥有纹理的图内生命周期与依赖，透明 draw 只是 producer，distortion、upscale 和 PostProcessing 是不同 consumer。Game Thread 的材质对象不拥有这些纹理。

**Data。** color 表示透明自身累积的辐射或颜色贡献；modulate 表示背景还应保留或相乘多少，不能把它误当第二张普通颜色；depth 是与 separate 分辨率和遮挡测试相匹配的深度输入或关联资源。不同 pass 不保证三者全部存在，消费者必须按条目实际能力读取。

**Control。** Standard 通常可以直接合成，但有三类控制条件会让结果先进入离屏资源：distortion 要求 Standard 暂存；AfterDOF / AfterMotionBlur 要求延迟到后处理；分辨率 scale 小于 1 时，即使没有前两类条件，主透明 pass 也要先在低分辨率目标绘制，再 upscale/composite。水下或其他特殊路径还能改变这套分叉。

**Lifetime。** ResourceMap 只覆盖本帧从透明 producer 到最后 consumer 的区间。条目被放进图里，不代表 GPU 已经写完纹理；最后 consumer 声明并执行之后，这些临时资源才能结束图内生命。若底层分配要复用，还必须等待覆盖最后 GPU 使用者的完成证据。

### 按完成深度追踪透明结果

透明调试最容易犯的错误，是用“提交了”同时指 CPU 有命令、RHI 已记录、GPU 已执行和颜色已合成。下面这些状态深度不同，不能互相替代：

| 最后成立状态 | 它真正证明了什么 | 仍然不能证明什么 |
| --- | --- | --- |
| Draw command 已形成 | 材质、PSO 和几何可被表达为一条候选命令 | 当前 view 一定可见、一定会执行 |
| Visible command 已筛选并排序 | 当前 pass 计划绘制它，顺序也已确定 | RHI 已记录或 GPU 已看到 |
| RHI 命令已记录 | 平台无关的 RHI command 已进入记录流 | 后端已形成平台命令、已送入硬件队列或目标已有像素 |
| Platform commands formed | 后端已把 RHI 记录翻译为平台原生命令并形成可提交的 command list | Platform Queue Submit 已发生或 GPU 已消费命令 |
| Platform Queue Submit 已发生 | 命令已交给 GPU 队列 | GPU producer 已结束，更不代表后续合成结束 |
| GPU producer 已写目标 | separate color/modulate 或直接目标已有本 pass 结果 | 结果已进入当前 `SceneColor` |
| ResourceMap 对 consumer 有效 | 图构建时已建立正确资源身份和 producer→consumer 依赖 | 单看 CPU 资源表不能证明 GPU 执行完成 |
| 透明 block 内 composite 完成 | Standard/distortion 等结果已成为新的当前 `SceneColor` | AfterDOF/AfterMotionBlur 已被后处理消费 |
| PostProcessing consumer 完成 | 延迟透明已在约定节点进入后处理颜色链 | 整帧 GPU 已完成、底层内存可立即复用 |
| 覆盖最后 consumer 的 GPU completion | 本次使用区间结束，可按同步规则复用资源 | 下一帧视觉一定正确；历史和路由仍可能有逻辑错误 |

这张表包含两条交错时间线。ResourceMap 条目通常在 RDG **构图**时就建立，GPU producer 写入发生在图**执行**时；因此“表里有条目”是依赖图证据，不是像素证据。调试烟雾时，先确认 producer 目标是否真的有数据，再确认后处理是否拿到同一条目，最后确认 composite 输出；不要从 CPU 看见一个非空纹理引用就跳到“烟雾已经画进屏幕”。

## 4. Separate Translucency：用额外资源换取独立分辨率和延迟合成

Separate translucency 不是一种材质外观，而是一种**资源与时机策略**：透明先写到与当前 `SceneColor` 分开的目标，再由明确的 consumer 合成。至少有三类独立触发原因，不能压成“distortion 或 post”二选一：

| 触发类别 | 为什么需要离屏 | 最后 consumer | 资源何时结束有效区间 |
| --- | --- | --- | --- |
| **Post 延迟**：AfterDOF / AfterMotionBlur | 结果必须等到对应后处理节点后再进入颜色链 | DOF 后或 Motion Blur 后的 post consumer | 对应 post composite 成为最后 GPU consumer 后 |
| **Distortion 暂存**：允许分离的 Standard 且本帧需要 distortion merge | 玻璃颜色要与扭曲背景在同一 merge 中组合 | distortion merge | merge 读取 color/modulate 并写回当前 `SceneColor` 后 |
| **Downsample 离屏**：透明 scale 小于 1，且主 pass 没被 distortion 继续扣留 | 主透明 pass 需要先在低分辨率 color/depth 上绘制 | 透明 block 内的 upscale + immediate composition | upscale/composite 写回当前 `SceneColor` 后；不会继续活到 post |

三种原因可以共享 ResourceMap 和 color/depth 形态，却有不同终点。尤其是第三类：没有 distortion、也不是 AfterDOF，Standard 仍可能因为 downsample 创建 separate 目标，但它会在同一个透明 block 内立即 upscale/composite；如果同一 Standard 又被 distortion 要求暂存，则由 distortion merge 的更晚 consumer 接手，而不是提前写回。

### 分辨率为什么可以不同

透明粒子和烟雾常有大面积 overdraw，完整分辨率会让同一像素重复执行很多次昂贵 shader。UE 可以让 separate 目标使用较低分辨率：

```text
主 SceneColor / SceneDepth
  -> 计算 separate extent 与 scale
  -> 为该尺度准备 color / 可选 modulate
  -> 准备与该尺度匹配的 depth 表达
  -> 在低分辨率目标上绘制透明
  -> 必要时 upscale
  -> 在指定节点 composite 到当前 SceneColor
```

低分辨率节省的是像素着色、blend 和带宽，尤其适合柔软烟雾、火焰和远处粒子；代价是边缘分辨率下降，细线透明会变粗或闪烁，透明与 opaque 深度交界处可能出现 halo，折射和锐利玻璃通常更难接受。提高分辨率会改善边缘与细节，却增加目标尺寸、overdraw 成本和合成带宽。固定平台、固定内容下也可以选择始终全分辨率；当透明覆盖很小或以锐利界面为主时，这往往比低分辨率再 upscale 更好。

### 为什么还要一份匹配尺度的深度

低分辨率透明仍要知道前方 opaque 表面在哪里，否则烟雾会穿过墙面，玻璃边缘也无法正确 depth test。直接用全分辨率深度与低分辨率颜色逐 texel 对齐会引入采样定义问题，因此 Renderer 会为 separate 尺度组织可共享或下采样的 depth 表达。这里的关键不是“透明写出了一张完整新深度”，而是**颜色目标和用于遮挡判断的深度必须处在兼容坐标与采样合约中**。

深度下采样本身是近似：取最近深度更能防止漏画，却可能过度遮挡细小缝隙；取平均会制造不存在的表面；更复杂的深度感知 upscale 质量更高，也更贵。UE 的选择偏向实时稳定性，不可能在低分辨率下保留每个 full-resolution 样本。若项目非常在意透明与几何边缘，优先考虑全分辨率、减少 overdraw 或为关键效果采用专门路径，而不是无限复杂化通用 upscale。

### 立即合成与后处理延迟消费

| Separate 用途 | Producer 输出 | Consumer | “真正进画面”的时刻 |
| --- | --- | --- | --- |
| Downsampled Standard | 低分辨率 Standard color + 匹配尺度 depth | 透明 block 内 upscale/composition | immediate composite 写回当前 `SceneColor` |
| Standard 因 distortion 分离 | Standard color + 可选 modulate | distortion merge | 扭曲背景与 Standard 一起写回当前 `SceneColor` |
| AfterDOF | AfterDOF color + 可选 modulate | DOF 之后的后处理节点 | consumer composite 结束 |
| AfterMotionBlur | 更晚的透明资源 | Motion Blur 之后的后处理节点 | 对应 consumer composite 结束 |

不这样拆会怎样？如果 AfterDOF 烟雾提前写入主颜色，DOF 会把它当场景内容一起模糊，失去“保持清晰地盖在景深结果上”的意图；如果所有透明都强制延迟，Standard 会多一次目标写入、读取和合成，增加内存与带宽，还可能让本应参与某些后续效果的透明错过节点。因此“全部直接写”和“全部 separate”都不是更好的统一答案，Renderer 必须按效果需求选择。

ResourceMap 的教学价值就在这里：它保存的不是“已经完成的最终透明”，而是**带着 pass 语义等待指定 consumer 的中间结果**。Downsampled Standard 的条目只活到透明 block 内 immediate composition；distortion Standard 活到 merge；AfterDOF 烟雾则活到 post consumer。只看 color/modulate 有数据只能证明 producer 成立，不能推断是哪一位 consumer、何时进入当前颜色。

## 5. Per-object 排序：为 pass 内 alpha blend 建立从后往前的顺序

调度先决定“哪些对象在同一个 pass”，排序再决定“这个 pass 内谁先影响目标颜色”。普通 alpha blend 依赖顺序，不是 UE 的任意规定。忽略预乘与特殊 blend mode，单层合成可写成：

```text
C_out = C_source * alpha + C_background * (1 - alpha)
```

第二层透明再写入时，它看到的 background 已经包含第一层结果。交换两层的前后次序，通常会得到不同颜色；所以普通透明要尽量**先画远层，再让近层覆盖远层**。如果没有排序，两个半透明对象可能因命令缓存、并行准备或可见集变化而交换顺序，重叠区域就会跳色。

UE 默认采用 draw command 级的 per-object / per-mesh 近似，因为它成本低，能覆盖大量不相交或层次清楚的透明。排序由“可稳定保存的人工控制量”和“每帧每 view 才知道的距离”共同组成：

```text
相对稳定的 sort key 部分
  -> TranslucencySortPriority
  -> TranslucencySortDistanceOffset
  -> 稳定 draw 标识

当前帧、当前 view 的部分
  -> 按 view sort policy 计算距离或轴向投影
  -> 应用 distance offset
  -> 在当前 pass 的 visible commands 内建立顺序
```

`TranslucencySortPriority` 是强制艺术控制，不是更精确的物理深度。它适合修复长期稳定的层级关系，例如角色护盾必须始终在某类特效之前；如果拿它修复会相互穿插的几何，换一个视角就可能反过来。Distance Offset 只是移动排序代表点，也不会改变真实三角形深度。两者的好处是便宜、可控，代价是把“看起来正确”写成内容规则，需要美术持续维护。

距离必须每帧每 view 重算，因为分屏、stereo 双眼和相机运动都会改变前后关系。mesh draw command 系统拥有排序数据；并行 pass setup 可以读取冻结的 view 参数并产生当前 pass 的 visible command 顺序，但不会修改 Game Thread 对象。这个阶段完成只说明**绘制计划的相对顺序成立**，还不是 RHI 记录、Platform Queue Submit 或 GPU 颜色产出。

同一视图里两块 Standard 玻璃会在 Standard 域比较距离。一片世界位置夹在两者之间的 AfterDOF 烟雾却不会插入这个数组，因为不同 pass 的结果在不同时间合成。此时“烟雾明明更近却没有按距离压住玻璃”可能不是排序 bug，而是 composition intent 本来就要求它在 DOF 后整体叠加。

per-object 的硬边界也很清楚：一个对象只能选一个代表距离，它不能为相交三角形、同一 mesh 内折返表面、同一像素上的任意多层片元建立精确顺序。把 mesh 切得更碎能改善代表点误差，却增加 draw、剔除和内容管理成本；强制人工 priority 可解决固定镜头，却不适合自由相机；真正需要像素层级时才进入下一节的 OIT。

当当前 view 通过 sorted-pixel OIT gate，且当前命令属于 pass bit 覆盖的 regular 或 separate translucency 域时，mesh draw setup 启用 inverse sorting；样本随后写入 OIT storage，并由 compose 恢复像素内顺序。这里改变的是**draw 处理/记录顺序**；Platform Queue Submit 是更深一层的平台完成状态，两者不能混为一谈。

## 6. OIT：三角形重排与像素样本排序解决不同层级

第 5 节留下两类 per-object 排序解决不了的错误：同一个 mesh 内部的三角形顺序错、同一像素上多层片元的合成错。UE 把相关能力放在 OIT（Order Independent Transparency）功能组里，但两条路径解决的层级并不相同：

| 路径 | 它修正什么 | 输入 | 输出 | 生命周期 |
| --- | --- | --- | --- | --- |
| Sorted triangles | 同一个 mesh **内部**三角形的顺序 | 支持该能力的 mesh batch / vertex factory、当前 view | 重排后的 index buffer 或等价索引数据 | 透明绘制前准备，本帧 draw 消费 |
| Sorted pixels | 同一**像素**上多层透明片元的合成 | 片元颜色、透过率、深度 | 有界 sample storage + compose 后颜色 | 当前透明 pass 创建和写入，pass 内 compose 消费 |

**Sorted triangles** 只对明确支持这项能力的 mesh batch 与 vertex factory 生效。它在 draw 前准备更适合当前 view 的索引消费顺序，仍走普通 raster 和 blend。它不能修复不支持动态重排的几何，也不能决定两个不同对象之间的顺序。它适合“一个复杂透明 mesh 自己的面片互相打架”，代价是每 view 准备或更新索引数据。

**Sorted pixels** 的门槛更高：项目与平台要支持 OIT 和 ROV 语义，运行时功能要开启，当前 view 不能走不兼容的 MSAA 路径，当前透明 pass 还必须被 pass mask 覆盖。满足条件后，shader 把片元样本写进受控存储，compose 再读取样本、恢复像素内顺序，输出到直接目标或 ResourceMap。

```text
per-object sort 决定 draw 粗顺序
  -> 可选 sorted triangles：修正受支持 mesh 的内部索引顺序
  -> 可选 sorted pixels：片元写入 OIT sample storage
  -> OIT compose：按像素读取并组合有界样本
  -> 输出到 SceneColor 或对应 separate 资源
```

“有界”是不能省略的条件。每像素存储和总节点预算都不可能无限大；当毛发片、烟雾和玻璃叠层超过预算时，实现只能丢弃、近似或合并超额样本。提高预算可降低溢出概率，却会增加显存、UAV 写入和 compose 成本。项目应按典型 overdraw 配置，并用真正的极端镜头验证，而不是把“开了 OIT”当成无限层精确保证。

把它落到交叉玻璃碎片：per-object sort 先决定两个碎片 mesh 的粗顺序；若每块碎片自身面片折返，sorted triangles 修正其内部索引；若碎片和烟雾在同一像素留下多层样本，sorted pixels 才在 GPU 上保存和 compose。CPU/Renderer 负责选择路径与组织资源，GPU 负责写样本和合成；OIT compose 有输出仍不等于颜色已经进入最终画面，输出若落在 ResourceMap，还要继续等待对应 consumer。

### 还有更好的 OIT 方式吗

“更好”取决于内容、平台和预算，没有一种方案在质量、内存和兼容性上同时占优：

| 方案 | 优点 | 主要代价 | 更适合什么时候 |
| --- | --- | --- | --- |
| Weighted blended OIT | 不保存完整层列表，成本与内存较可控 | 是加权近似，厚重颜色、高 alpha 和强遮挡容易偏色 | 大量柔和粒子、烟雾，可接受近似时 |
| Depth peeling | 按深度逐层剥离，给定层数内关系直观 | 每多一层就增加几何与像素工作 | 层数少、范围小、质量优先时 |
| Per-pixel linked list | 可保存可变片元列表再排序 | 原子操作、节点池、溢出管理和内存压力大 | 固定高端平台、层次复杂且预算充足时 |
| UE sorted-pixel storage | 与现有 pass、ResourceMap 和 compose 合约集成 | 受 ROV、MSAA、pass mask、样本容量和平台限制 | 关键交叉透明暴露明显伪影，目标平台支持时 |

因此移动平台常更适合 per-object + 内容拆分 + 少量 priority；大面积低 alpha 烟雾可能适合 weighted 近似；少量关键玻璃在高端平台上才值得精确样本排序。功能名字不是选择依据，可见错误和预算才是。

失败模式也要按层级拆：

| 症状 | 可能层级 |
| --- | --- |
| 同一个 mesh 内局部面片顺序错 | sorted triangles 未启用、mesh/VF 不支持、重排索引未被 draw 消费 |
| 多个对象重叠处跳色 | per-object policy、priority 或 distance offset 不符合预期 |
| 开了 sorted pixels 仍穿插 | 平台/ROV 条件不成立、MSAA 不兼容、pass mask 未覆盖、sample storage 溢出 |
| OIT 后颜色没进画面 | compose 输出目标错误，或 ResourceMap 后续交接未完成 |

## 7. Shading Model 与 Lighting Mode：先判是否受光，再讨论 TLV

透明通常不写供延迟光照读取的完整 GBuffer，也可能在同一像素出现多层表面，所以 opaque 那套“BasePass 保存材质属性，Lighting 后来统一逐像素读取”不能原样复用。但这不等于所有透明都必须采样 TLV。这里有**两条相邻但不同的控制轴**，必须按顺序判断。

第一轴是 **Shading Model**：

| Shading Model 判断 | 控制结果 | 调试含义 |
| --- | --- | --- |
| **Unlit** | 材质绕过场景光照，由 emissive/color 等自身输出决定可见结果 | 不应去查 TLV 或 forward light；应查材质输出、blend 和曝光 |
| **Lit translucent** | 材质需要 Renderer 提供透明光照输入 | 继续进入第二轴 `ETranslucencyLightingMode` |

第二轴才是 **Translucency Lighting Mode**。它只回答 lit transparent 应采用哪种透明光照近似，不负责把材质从 Unlit 改成 Lit：

| Lit transparency mode | 主要输入模型 | 优点 | 代价与适用边界 |
| --- | --- | --- | --- |
| Volumetric 类模式 | 从体积化的低频光照输入获取非方向、方向贡献；也有把部分计算移到 vertex 的变体 | 适合烟、雾片、粒子云，少量体积采样可服务大量片元 | 细节受体素/顶点分辨率限制，不适合锐利高光 |
| Surface TranslucencyVolume | 表面透明在着色时读取 TLV 风格的体积光照 | 比逐光源 forward 便宜，能让普通表面透明响应场景光 | 光照低频，玻璃高光和局部小灯可能不够准确 |
| Surface ForwardShading | 透明表面逐像素使用 forward light data | 法线、高光和局部灯响应更精确 | 每像素/每光源成本更高，受 forward 光照能力和平台配置约束 |

这两轴解释了为什么“玻璃采 TLV”不能写成固定事实。只靠自发光表现的能量罩先在 Shading Model 轴选择 Unlit，此时 Translucency Lighting Mode 不负责让它受光；Lit 磨砂玻璃才可能在第二轴选择 surface volume；需要锐利 specular 和精确局部灯的 Lit 玻璃更适合 Surface ForwardShading；Lit 烟雾则常适合 volumetric directional 一类模式。Lumen front-layer 与 ray-traced translucency 是更专门的反射/透射旁路，也不能简单等同某个 TLV mode。

### TLV 到底存什么，为什么是两级 cascade

Translucency Lighting Volume（TLV）是 per-view 的低频 3D 光照场。它不是“透明颜色体积”，也不是透明对象列表；它把光源对空间区域的可采样贡献压缩进体素，让 Shading Model 为 Lit 且选择 TLV 类 lighting mode 的透明 shader 用**世界位置**查询附近光照。

单个固定体积会陷入两难：覆盖很远时每个体素巨大，近景阴影和灯光变化会糊；只覆盖近处时远景粒子会采不到。UE 用 inner/outer 两级 cascade 折中：inner 覆盖相机附近、空间分辨率较高，outer 覆盖更远范围、每体素更粗。透明 shader 按世界位置映射进级联，在交界处使用实现规定的选择或过渡。

```text
View setup
  -> 为 inner / outer cascade 建立 world-space bounds 与坐标变换
  -> 分配或复用 Ambient / Directional 3D 纹理

Lighting preparation
  -> 清理当前写入目标
  -> 注入 lights、ambient 与允许的低频贡献
  -> 可选过滤，并按 view state 使用历史稳定结果

Translucency draw
  -> Lit shading model + lighting mode 共同决定是否绑定 TLV
  -> shader 用世界位置选择 cascade 并采样
  -> 采样结果参与当前透明片元着色
```

**Owner/Data/Control/Lifetime。** Renderer 拥有 TLV 的创建、清理、注入和过滤；view 参数拥有世界空间边界与坐标变换；view state 可拥有跨帧 history；透明 draw 只读当前可采样结果。控制顺序是先判断 Shading Model 是否为 Lit，再由 lighting mode、平台体纹理能力和质量开关决定是否读取 TLV。当前帧写入目标由 RDG 管理，历史资源则跨帧存在，不能把二者混成“透明材质自己的缓存”。

Stereo view 不会默认给左右眼各分配一整套 TLV。Secondary view 通过 `PrimaryViewIndex` 映射到 primary view 的 texture pair，共享同一组 inner/outer Ambient 与 Directional 3D 纹理，从而避免为每只眼维护独立的 3D 资源，并减少按独立 pair 进行的清理。注入与过滤仍可能按 view 调度并写向共享 pair，不能从资源共享推断 secondary view 没有对应 GPU 工作。代价是 view-to-texture-pair 映射与两眼空间参数必须一致：若只有一眼受光异常，先查 secondary→primary 映射、级联 bounds 与世界位置变换，不要假设每只眼睛拥有独立 TLV。

### 分辨率、范围和历史为什么互相牵制

提高 TLV 分辨率会改善小灯、阴影边缘和局部变化，但 3D 纹理成本按三个维度增长，清理、注入、过滤和采样压力都会上升。扩大 cascade 范围能覆盖更远粒子，却让同样数量体素代表更大的世界空间，近处细节变差。增加历史权重能稳定低分辨率闪烁，却会在相机、光源或粒子快速运动时留下拖影。

因此调参应先问症状属于哪一轴：远处烟雾突然无光可能是 coverage；近处小灯照不出轮廓可能是 resolution 或 lighting mode；移动后残留亮斑可能是 history。只把分辨率不断调高，不会修复选错 lighting mode，也不会让 TLV 变成逐像素镜面方案。

### 烟雾与玻璃走不同光照路线

Shading Model 为 Lit 的 AfterDOF 烟雾若再选择 volumetric directional 模式，Renderer 先把低频灯光注入 inner/outer TLV；烟雾 draw 用每个片元对应的世界位置采样，再把着色结果写到 AfterDOF separate color。这里最后成立的状态依次是：材质确实为 Lit、lighting mode 选择 TLV 路线、TLV 有光照数据、烟雾 target 有颜色、ResourceMap 交接成立、后处理完成 composite。

染色玻璃若选择 Surface ForwardShading，则应优先检查 forward light data、允许影响透明的灯光和材质法线，而不是先查 TLV。若改用 Surface TranslucencyVolume，着色成本通常更可控且结果更平滑，却要接受低频高光和阴影。对少量关键英雄玻璃，Lumen/RT 路线可能提供更高质量；对大量背景窗户，TLV 或简化 Unlit 近似通常更可控。这就是“哪种方式更好”必须附带对象数量、屏幕占比、平台和视觉目标的原因。

调试透明全黑时，第一步不是检查“TLV 是否坏了”，而是先确认 Shading Model。Unlit 应检查材质自身输出；只有 Lit transparent 才继续确认 Translucency Lighting Mode。TLV 路线沿 texture-pair mapping → cascade allocation → light injection → filter/history → uniform binding → world-position sampling 倒查；Surface ForwardShading 则检查 forward light data 与逐像素灯光。只有两条控制轴先判对，后面的资源证据才有意义。

## 8. Distortion：折射先改背景，再和透明颜色合并

玻璃折射是最容易被误解的一节。直觉会说：透明 shader 直接采当前 `SceneColor`，按法线偏移一下 UV，再 blend 回去不就行了？这会把输入版本、同附件读取能力和 draw 间可见性绑到具体 RHI 语义上，不能作为 UE 通用路径的稳定合约。UE 把 distortion 拆成三段，正是为了显式固定依赖：**记录偏移、应用偏移、合并回主颜色**。

它由一条独立的 distortion mesh pass 和随后的全屏 pass 拥有。材质输入是“translucent blend 且声明了 distorted”的对象；资源输入是当前背景颜色、场景深度、stencil 标记，以及可能已经写进 ResourceMap 的 Standard 透明结果；最终输出回到主 `SceneColor`。三段的数据流如下：

```text
Distortion mesh pass
  -> 只收集 distorted translucent 材质
  -> 把屏幕空间偏移输出到 DistortionTexture
  -> 用 stencil 标记需要 apply 的区域

Apply
  -> 读取背景 SceneColor 和 DistortionTexture
  -> 只在 stencil 区域生成被扭曲后的背景颜色

Merge
  -> 读取扭曲后的背景
  -> 如果 Standard translucency 被 separate，读取 ResourceMap[Standard]
  -> 把折射背景、透明颜色和调制信息一起合并回 SceneColor
```

把染色玻璃按状态拆开：Standard 路径先算出玻璃自身颜色和透过调制，但因为这一帧需要 distortion，它们先停在 `ResourceMap[Standard]`；Distortion pass 另画一次只输出屏幕空间偏移和 stencil，不承担玻璃颜色；Apply 用偏移去读稳定背景，得到一张“被玻璃折射过的背景”；Merge 才同时读取扭曲背景和 Standard 资源，生成最终写回 `SceneColor` 的结果。这样读者能把三个常见断点分清：颜色资源存在、偏移资源存在、合并结果存在，分别代表三段不同完成状态。

这里的关键生命周期边界是：**当一帧需要 distortion 时，Standard translucent 可能被迫渲染到 separate 资源，而不是马上写 `SceneColor`。** 只有这样，merge 才有机会把“玻璃自身颜色”和“被折射的背景”放进同一次合成。如果没有这个边界，Standard 颜色先写回主颜色、distortion 再去扭曲背景，玻璃和背景的前后关系就乱了——这也回收了第 3 节那句“开 distortion 后 Standard 玻璃为什么会消失”的悬念：它不是消失了，是被分流进了 ResourceMap 等 merge。

**Unity 桥接**：可以把它粗略类比成 GrabPass 或 `_CameraOpaqueTexture` 加折射偏移。但 UE 的差异在于它还要和 separate translucency、stencil、RDG 资源和后处理时机协作，不是一次性 grab 就完事。

调试折射时按三段拆：玻璃颜色在、折射不在，优先查 Distortion pass 有没有 draw、stencil 有没有标记、apply 有没有产出扭曲背景；折射在、透明颜色丢了，则查 Standard 有没有进 ResourceMap，以及 merge 有没有读到 Standard 的 color/modulate。

## 9. FrontLayer Translucency：发布透明第一层的几何证据

第 7 节的 TLV 管不了锐利镜面证据，第 8 节的 distortion 只记录屏幕空间折射偏移。当玻璃要参与 Lumen 或光线追踪反射时，如果系统只看主深度，就可能把玻璃**背后**的 opaque 表面当成最近表面。FrontLayer Translucency 为此提供一份很窄的几何证据。

FrontLayer 的正向角色是**捕获最前一层透明表面的 normal 和 depth**，让 Lumen front-layer reflections、ray traced translucency 等路径先定位透明表面本身。Lumen/Renderer 的 front-layer GBuffer pass 拥有这次生产：view 必须允许 Lumen front-layer reflections；primitive 必须能进 main pass；material domain、front-layer 写入标志和非 AfterMotionBlur 条件都要通过。输出只服务当前帧的后续 Lumen/RT consumer；这些 consumer 再把几何证据转成反射或透明光照，FrontLayer producer 本身不写主 `SceneColor`：

```text
FrontLayer 条件成立
  -> 清 front-layer normal / depth
  -> 绘制 LumenFrontLayerTranslucencyGBuffer mesh pass
  -> 得到透明第一层的 normal + depth
  -> Lumen front-layer reflections 读它，生成透明反射输入
  -> ray traced translucency 读它，做分类、trace 和 denoise
  -> 后续透明 base pass 或 RT composite 再影响 SceneColor
```

继续看那块玻璃：如果没有 FrontLayer，Lumen 或 RT 看到的最近表面很可能是玻璃后的墙，反射就会按墙的深度和法线组织；有了 FrontLayer，当前帧先多出一份“玻璃第一层几何证据”。这份证据的输出不是颜色，而是 normal/depth。它的下一位消费者才会把它转成反射或 RT 透明光照，再由后续透明合成影响 `SceneColor`。因此 FrontLayer 的调试问题不是“它有没有 blend 出颜色”，而是“第一层 normal/depth 是否被生产，并被 Lumen/RT 读到了”。

一句话概括它的价值：FrontLayer 用一张很窄的 GBuffer 旁路，告诉后续系统“透明第一层在这里，它的法线朝这个方向”，从而把反射/折射贴到正确的透明几何上。

它的优势也正是限制：只保存**最前一层**，数据量和接入成本可控，但第二层玻璃、玻璃内部液体、层叠薄膜等更深结构不会在这份缓冲中完整存在。它也没有透明颜色、透过率和任意多层排序信息，不能替代 OIT、distortion 或最终透明 blend。若项目必须理解多层真实透射，完整 ray tracing 或专门的多层表示更合适，代价是 trace、材质求值、降噪与平台预算显著上升。

为什么不把所有透明都写成完整 GBuffer？那会重新引入“同一像素有多少层、每层存多少材质属性、后续 lighting 怎样遍历”的问题，内存和带宽会随层数迅速增长，也失去 deferred GBuffer 固定每像素布局的优势。FrontLayer 是 UE 针对“后续系统至少需要知道最近透明表面”做的窄契约，不是完整多层透明延迟渲染。

它的失败模式通常不是 blend state 错，而是**输入资格错**：材质没启用写 front-layer transparency、被 AfterMotionBlur 等条件排除、FrontLayer pass 根本没 draw——任何一项都会让 Lumen 透明反射缺失。因为它不直接写 `SceneColor`，所以用最终颜色调试它时，要沿“front-layer normal/depth → Lumen/RT → transparent lighting 参数或 RT composite”这条链路倒查，而不是盯着最终画面找。

## 10. Ray traced translucency：RT 结果也要选对 SceneColor 接入点

光线追踪透明在本章只讲“怎么接回主颜色”，不展开 RT 算法本身。它在透明 block 里有两类语义：**Lumen 硬件 RT 透明路径**和 **legacy ray tracing translucency**。两者都可能算出透明颜色，但接回 `SceneColor` 的方式不同。

**Lumen RT translucency** 依赖第 9 节的 front-layer 数据。它逐 view 生成两样东西：final radiance（透明体自身的光照结果）和 background visibility（背景透过透明体的比例）。算完之后，它要根据当前帧是否需要 distortion、Standard 是否 separate，决定接入点：

```text
Lumen RT translucency
  -> 需要 FrontLayer normal/depth
  -> trace / resolve / denoise
  -> 得到 FinalRadiance + BackgroundVisibility
  -> 若这一帧可以直接改 SceneColor：
       SceneTextures.Color = FinalRadiance，并重建 scene texture uniform
     否则（需要留给 distortion merge）：
       ResourceMap[Standard].ColorTexture          = FinalRadiance
       ResourceMap[Standard].ColorModulateTexture  = BackgroundVisibility
       交给 distortion merge 消费
```

**legacy ray tracing translucency** 更像一条独立的 RT 绘制路径：解析当前场景颜色，发射 primary rays，再把输出贴回 `SceneColor`。它仍位于透明 block 内，但不是通过普通 mesh pass 队列完成透明 raster。

把 RT 透明当成一次“接回主颜色的二选一”会更稳。若当前帧没有 distortion 这类后续 merge 约束，Lumen RT translucency 可以把 final radiance 变成新的 `SceneTextures.Color`，并让后续 scene texture uniform 看见更新后的颜色；若 Standard 透明必须 separate，RT 结果就要拆成 color 与 background visibility，写进 `ResourceMap[Standard]` 等 merge。两条路的视觉目标一样，都是让玻璃影响最终颜色；差别是当前帧的资源所有权是否允许它现在直接改主颜色。

这里的设计理由和第 8 节的 distortion 完全同源：**RT 透明不是“算出颜色就结束”，它必须尊重当前帧是否还需要 distortion、Standard separate、scene texture uniform 更新和后续后处理。** 玻璃如果同时用了 RT translucency 和 distortion，RT radiance 就不能贸然直接覆盖主颜色——否则 distortion merge 会失去正确输入，又回到第 8 节那个“前后关系被搅乱”的老问题。

调试 RT 透明时按这个顺序：先确认当前 view 选的是 Lumen RT 路径还是 legacy fallback；再看 front-layer 数据是否有效；最后看输出是直接改了 `SceneTextures.Color`，还是写进了 Standard ResourceMap 等 distortion merge。不要只在最终 `SceneColor` 里找答案——中间结果可能是**故意**停在 ResourceMap 的。

---

## 11. Worked Case A：带折射染色玻璃怎样改变资源状态

设定一块占屏幕较大的染色玻璃：材质希望在 Standard 时机合成，使用普通 alpha/transmittance 语义，开启 distortion；为了得到锐利局部灯高光，选择 Surface ForwardShading；项目还允许 Lumen front-layer reflection。下面每一步都让一个新状态成立。

### 11.1 Renderer 把材质意图解析为本帧执行计划

材质和 primitive 先提供 Standard、distortion、surface forward lighting、sort priority 等意图。Renderer 再逐项通过本案例的 gate：Standard 命令被目标 pass 接受；distortion 在本帧启用；distortion 侧要求 Standard 保留到 merge 且 view family 允许 Standard separated；FrontLayer 的 view、main-pass、domain、material flag 与非 AfterMotionBlur 条件成立。最终 schedule 形成三条相关但不同的路线：Standard color、Distortion offset、FrontLayer normal/depth。

如果这一阶段失败，GPU 上还没有玻璃像素可查。最直接证据是当前 view 的 pass mask 和 visible command：颜色、折射和 front-layer 可以分别缺失，不能因为其中一条存在就假设另外两条也存在。

### 11.2 排序只在 Standard 域内发生

玻璃与其他 Standard 透明按当前 view 距离、priority 和 offset 排序。若玻璃自身三角形交叉且 mesh/VF 支持，可加入 sorted triangles；若同一像素多层交叉、平台与 pass 条件允许，可使用 sorted pixels。AfterDOF 烟雾不参加这个排序域。

最后成立状态从“有候选 command”推进到“有 visible/sorted command”。此时仍不能说玻璃颜色已生成。

### 11.3 光照路线由 Surface ForwardShading 决定

玻璃的 Shading Model 为 Lit，并选择 Surface ForwardShading；shader 因此读取 forward light data 做逐像素表面光照，再结合材质颜色、法线、opacity/transmittance 形成玻璃自身贡献。它不应被写成“固定采样 TLV”；只有 Lit 材质把 Translucency Lighting Mode 改成 Surface TranslucencyVolume 时，调试主线才切换到 TLV。若高光缺失而玻璃底色存在，应先检查 Shading Model、lighting mode 与 forward 灯光输入，而不是 distortion。

### 11.4 Distortion 迫使 Standard 结果暂存

因为后面还要把背景扭曲，Renderer 不能让 Standard 玻璃先把最终关系焊死在 `SceneColor` 中。Standard producer 把玻璃 color 和需要的 background modulation 写入 `ResourceMap[Standard]`；独立 Distortion pass 写偏移与 stencil；Apply 读取稳定背景并产生扭曲后的背景；Merge 同时读取扭曲背景和 Standard color/modulate，才生成新的当前 `SceneColor`。

```text
Glass material intent
  -> Renderer schedule: Standard + Distortion + optional FrontLayer
  -> Standard visible/sorted commands
  -> Surface ForwardShading 读取 forward light data
  -> GPU 写 Standard color/modulate separate targets
  -> ResourceMap[Standard] 建立给 merge 的有效交接
  -> Distortion offset + stencil
  -> Apply 得到扭曲背景
  -> Merge(扭曲背景, glass color, modulation)
  -> 当前 SceneColor 第一次真正包含完整染色折射玻璃
```

FrontLayer 只在旁边生成第一层 normal/depth，给 Lumen/RT 消费；它存在并不证明玻璃颜色或折射成立。反过来，玻璃颜色与折射都正确，也不能证明 front-layer reflection 有效。这个案例的关键不是“同一物体画了三遍”，而是同一 primitive 为三个消费者提供三种不同语义的数据。

## 12. Worked Case B：AfterDOF 烟雾为何要跨到后处理

再设定一片柔软烟雾：材质的 Shading Model 为 Lit，声明 AfterDOF，并选择 volumetric directional lighting；当前 view 允许 AfterDOF，烟雾位于自动边界以内、没有落到 DOF 背景侧，因此没有被 Auto Before DOF 移到 Standard；项目为这类 separate translucency 采用较低分辨率。

### 12.1 Renderer 先确认 AfterDOF 真的成立

Renderer 把材质意图与 view 条件求交，建立 AfterDOF pass mask。当 `AllowTranslucencyAfterDOF()` 为 false 时，本帧不执行 Standard/AfterDOF/AfterMotionBlur 分段，而改画 `TPT_AllTranslucency` 聚合桶；当 Auto Before DOF 边界有效且烟雾沿 view forward 的距离大于边界时，它从 AfterDOF 移到 Standard 并参加 DOF。只有最终 pass 确认后，才能解释烟雾应不应该被 DOF 处理。

### 12.2 TLV 为烟雾提供低频方向光

Lighting preparation 已经为 inner/outer TLV 注入并过滤光照。烟雾 shader 按世界位置选择 cascade，得到低频方向光，再计算当前片元颜色与透过率。若烟雾全黑，证据顺序是：Shading Model 是否为 Lit → lighting mode 是否要求 TLV → stereo texture-pair 映射是否正确 → 采样位置是否落入 coverage → volume 是否有注入 → uniform 是否绑定当前 view；不要先跳到 PostProcessing。

### 12.3 低分辨率 separate 目标保存中间结果

Renderer 为 AfterDOF 准备较小的 color、可选 modulate 和匹配尺度的 depth 合约。GPU producer 写完后，烟雾只存在于 separate target；ResourceMap 把这个条目交给后续 consumer。若此时查看主 `SceneColor`，看不到烟雾是正确状态，不是“透明 pass 没工作”。

低分辨率带来的柔化对烟雾通常可接受，但与墙面相交的边缘可能出现 halo。若边缘伪影不可接受，可以提高 separate 分辨率、减少效果 overdraw、改进内容边界，或让关键效果采用全分辨率；没有一种选择同时免费获得锐利边缘和低带宽。

### 12.4 PostProcessing 消费才完成视觉交付

后处理先完成 DOF，再取得 AfterDOF 条目；若资源是低分辨率，先按约定 upscale，然后用 color/modulate 合成进当前颜色。到这里“烟雾在 DOF 后保持清晰地覆盖已景深背景”才成立。底层资源要复用，还需等待覆盖这个 consumer 的 GPU completion。

```text
Smoke material intent: AfterDOF + volumetric directional
  -> Renderer 确认 AfterDOF 未被禁用，且烟雾未落到自动边界之后的 DOF 背景侧
  -> AfterDOF visible commands 独立排序
  -> TLV world-position sampling 产生烟雾受光结果
  -> GPU 写 low-resolution color / optional modulate
  -> ResourceMap[AfterDOF] 交给后处理
  -> 按当前 separate upscale 合约重建（如需要）
  -> DOF 后 consumer composite
  -> 当前后处理颜色第一次包含烟雾
```

玻璃与烟雾展示了三类 separate 中的两类：玻璃的 Standard 资源由 distortion merge 消费；烟雾的 AfterDOF 资源跨到后处理延迟消费。第三类是没有 distortion 也不是 post 延迟、只因 scale 小于 1 而离屏的 downsampled Standard，它会在透明 block 内 upscale 并立即 composite。相同的数据结构并不意味着相同的生命周期终点。

## 13. 调试主线：寻找“最后成立状态”

不要从最终坏像素直接跳进最深 shader。先沿本章主线寻找最后一个有证据成立的状态，再检查下一条边：

```text
Material intent
  -> Renderer final schedule
  -> visible / sorted commands
  -> lighting input bound
  -> GPU producer target
  -> ResourceMap handoff（若 separate）
  -> distortion / OIT / FrontLayer / RT 专用 consumer（若需要）
  -> transparency-block composite
  -> post consumer（AfterDOF / AfterMotionBlur）
  -> final GPU completion for reuse
```

| 现象 | 最后成立状态应该查到哪里 | 下一条最可疑的边 |
| --- | --- | --- |
| 对象完全消失 | 材质意图、最终 pass mask、visible command | Renderer 条件解析或 mesh pass 资格 |
| 对象可见但前后跳变 | 当前 pass 的 visible commands 已排序 | priority/distance、mesh 内三角形或像素样本层级 |
| 透明全黑但轮廓存在 | producer draw 已执行 | 先分 Shading Model；Lit 再查 lighting mode 对应的 TLV/forward/RT，Unlit 查自身输出 |
| 背景采样旧或错误 | 先分普通透明、Single Layer Water、underwater consumer，再判断 `SceneColorCopy` gate | copy 版本、绑定和 producer→consumer 依赖 |
| 无 distortion 的低分辨率 Standard 有 target、主画面没有 | downsample producer 已成立 | 透明 block 内 upscale/immediate composition 是否执行，临时资源是否过早失效 |
| 开 distortion 后玻璃消失 | Standard separate target 是否有颜色 | ResourceMap[Standard] 到 distortion merge |
| 折射有、玻璃颜色无 | Distortion offset/apply 已成立 | merge 是否读取 color 与 modulate |
| AfterDOF target 有烟雾，主画面没有 | ResourceMap[AfterDOF] 有有效 producer | upscale 或后处理 consumer |
| Lumen 透明反射缺失 | FrontLayer pass 是否产出第一层 normal/depth | Lumen/RT 是否消费同一 view 的证据 |
| OIT 偶发破层 | OIT 功能与 pass 条件已成立 | sample storage 溢出、unsupported mesh/VF 或 compose 输出 |

这里的“证据”可以来自可见命令统计、GPU capture 中的目标内容、RDG 事件和资源绑定、pass 前后颜色对比。它们回答的是不同深度的问题：CPU pass mask 不能证明 GPU 像素，GPU separate target 不能证明 composite，最终画面也不能反推中间资源一定正确。只有把证据与状态深度对应，调试才不会在错误层级反复猜。

## 14. 本章出口：交给 PostProcessing 的不是一种统一透明结果

本章交给第 15 章的输出有两类：一类已经由 Standard、distortion 或允许直接写色的 RT 路径进入当前 `SceneColor`；另一类仍在 `FTranslucencyPassResourcesMap` 中，等待 AfterDOF 或 AfterMotionBlur 的指定 consumer。第 15 章从这个边界继续讲 DOF、TSR、Motion Blur、Bloom、Tonemap 和最终输出如何改写颜色。

离开本章前，应能对任意透明对象回答：材质声明了什么意图，Renderer 最终怎样调度，它在哪个排序域，Shading Model 是否受光、Lit transparent 的 lighting mode 读取哪类光照，GPU 写直接目标还是哪一类 separate 资源，谁是最后 consumer，以及你手里的证据只证明到了哪一层。不能回答其中任何一项，就还不能用一个“透明已经完成”概括它的状态。
