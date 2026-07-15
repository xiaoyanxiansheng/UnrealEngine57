# 16 Nanite 虚拟几何体

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `02_SceneProxy.md`、`06_GPUScene.md`、`09_DepthPrepass.md`、`10_BasePass.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见 `16_Nanite_CoverageMatrix.md`

## 开篇：Nanite 把几何选择变成局部、GPU 驱动的 compatible cut

Nanite 在资源构建阶段先把高模组织成可兼容替换的 cluster groups、parents 与 streamable pages；运行时再由 GPU 针对当前 view，从已驻留层级中选择一张满足 projected error 的 compatible cut。它改变的是两件事：**几何可见性的决策粒度，以及这个决策发生的位置。**

如果只把它总结为“UE 的自动 LOD 系统”，只能描述近处细、远处粗的结果，却解释不了局部混合精度、缺页粗化、GPU 剔除和 visibility-first shading 为什么能属于同一条管线。

- 常见的 authored LOD（包括典型 Unity `LODGroup` 用法）以“整个 Renderer/对象”为切换单位：CPU 依据距离或屏幕占比，从美术预先准备的 mesh 档位中选一档。GPU-driven object LOD 也可以减少 CPU 决策，但粒度仍通常是整对象/整 meshlet 集合。
- Nanite 的决策粒度是“一小组可兼容替换的 clusters”，主要决策位置在 GPU：网格先在资源构建阶段形成 group/parent-child replacement DAG，再打包成可遍历 hierarchy；运行时 GPU 根据 projected error 为每个局部选择 compatible cut。

这就引出第二个常被误读的词——“像素级 LOD”。它**不是**“每个像素生成一块独立网格”，也**不是**“每个三角形都带一档自己的 LOD”。更准确的说法是：**LOD 误差用屏幕像素尺度来衡量，最终选择以 group-compatible cluster cut 表达。** 一块几何是否继续细化、当前层是否已够细、细节页缺失时在哪个 streaming leaf 停下，都是 GPU 遍历 packed hierarchy 时结合当前 view 决定的。

为了让这套机制有抓手，本章用**一面 Nanite 岩壁**贯穿全程。它是一个不透明的 `StaticMesh`（静态网格）。玩家从远处走近：岩壁先用“保底”数据画出一个可见的粗略版本，再随着 GPU 不断“喊话要更细的数据”，由 CPU 侧的流送系统逐步加载更精细的部分；而在每一帧内部，这面岩壁会先被 GPU 选出当前可见的簇，写入深度与可见性信息，最后按材质分组、用计算着色器写进我们已经熟悉的 BasePass GBuffer。

下面这张路线图就是岩壁要走完的整条路。先不用看懂每个词，把它当作一张地图，后面每一节都会停在其中一站：

```text
原始 StaticMesh 几何
  -> 离线构建：leaf clusters -> groups -> parent simplification -> Cluster DAG
  -> 运行时资源：packed hierarchy / root pages / streamable pages
  -> 渲染线程注册：拿到 GPU 可寻址的资源 id 与层级偏移
  -> Scene proxy + GPUScene primitive 参数
  -> Packed view（打包视图）：投影、view rect、HZB rect、像素尺度 LOD 预算
  -> GPU 剔除：instance -> hierarchy node -> compatible cluster cut + residency fallback
  -> 光栅化：raster bin、硬件/软件两条路径、写 VisBuffer / depth evidence
  -> 导出：SceneDepth / ShadingMask
  -> 着色分组：把可见像素按材质 shading bin 归类
  -> BasePass compute 着色：交付后续光照共享的 GBuffer 输出合同
  -> 流送反馈：request/readback/version -> merge 初始 priority -> parent priority 传播
                 -> budget 内最终 selection/LRU -> slot -> data/install/transcode/scatter -> 后续消费
```

这条链跨越五种寿命：cook/build owner 生产可序列化的替换关系与页；跨帧 streaming manager 维护版本化资源 identity、全局页池和请求；scene proxy/GPUScene 在 primitive 驻留期发布资源引用；当前 view/RDG 生产 cut、raster、visibility 与 shading 工作；GBuffer、Shadow、Lumen 等下游只消费与各自 pass 匹配的结果。每次交接都以前一阶段的具体 output 为输入，不能用“Nanite 资源存在”跳过 residency、view、raster 或 material consumer 条件。

## 本篇边界

本篇只讲透一条主线：**一份 StaticMesh 的 Nanite 资源，怎样被组织成可流送的分层簇结构；怎样被 GPU 按屏幕像素尺度选出当前要画的簇；怎样经过硬件/软件光栅、可见性缓冲（VisBuffer）和着色分组，写入 BasePass GBuffer；又怎样把“缺哪些细节页”的请求反馈给流送系统。**

岩壁会途经不少相邻系统，但本篇只讲它们在这条主线上扮演的角色，深入留给各自的专章：

| 相邻概念 | 本篇只讲到 | 深入出处 |
| --- | --- | --- |
| Component / Proxy / SceneInfo | Nanite 网格如何分流成 Nanite scene proxy，并把资源 id 写进 primitive 参数 | 第 02 篇 |
| GPUScene | shader 通过 primitive / instance 数据拿到 Nanite 资源 id 与层级偏移 | 第 06 篇 |
| SceneDepth / HZB | Nanite 用 HZB 做遮挡复测，并把深度导出到主场景深度 | 第 09 篇 |
| BasePass / GBuffer | Nanite 的 compute 着色写入同一套 BasePass render target | 第 10 篇 |
| Lumen / VSM | 它们是 Nanite 剔除/光栅结果的消费者，本篇不展开其内部算法 | 第 17 / 19 篇 |
| 材质 / Shader 编译 | 本篇只讲 Nanite 以 compute 形态请求 BasePass 材质 shader | 第 20 / 21 篇 |

读完本篇，你应该能回答这些问题——它们也是后面各节的提纲：

1. “像素级 LOD”里的像素尺度究竟来自哪里，为什么它和传统对象级 LOD 不是一回事。
2. leaf cluster、cluster group、parent simplification 与 external-edge locking 为什么共同防止裂缝。
3. 逻辑 Cluster DAG、packed GPU hierarchy 与 page/residency 为什么是三套结构。
4. Nanite 资源怎样从 StaticMesh 一步步进入流送系统、scene proxy 和 GPUScene。
5. compatible cut 如何同时受 parent/child error、group compatibility 与 residency 约束。
6. GPU 管线的五个阶段分别消费什么、产出什么。
7. raster bin 与 shading bin 为什么不是同一类分组，HW/SW 为什么不是 LOD 精度。
8. VisBuffer、SceneDepth、ShadingMask、着色分组怎样一环扣一环交付 BasePass GBuffer。
9. GPU/显式/prefetch request 为什么要先汇总初始 priority、递归传播给 parents，再在 pending budget 内最终选页和更新 LRU，之后才获得 slot 并进入安装链。
10. 出现缺页、LOD 抖动、材质错位、遮挡闪烁时，该怎样找最后成立状态。

## 1. 离线构建：先制造“可被局部替换”的几何层级

Nanite 运行时能在同一面岩壁上同时选择不同精度，并不是 GPU 临时把原始 mesh 现场减面。真正昂贵的拓扑分析和简化发生在资源构建阶段；运行时 GPU 只在预先建立好的替代关系中选择一个适合当前 view 的 **cut（切面）**。

先把 `cluster` 定义钉牢：它是一小组三角形，规模受实现预算约束，能够作为剔除、LOD 选择和光栅工作单元。cluster 不是任意切一刀就结束。构建器要尽量让同一簇内的三角形在拓扑和空间上相邻，减少边界数量；边界越多，后续独立处理时越难保持连续，也会增加层级和元数据成本。

### 1.1 从原始三角形到 leaf clusters

```text
原始三角形与顶点属性
  -> 建立共享边邻接关系
  -> 同时考虑空间 locality，避免把相距很远但偶然相关的三角形塞进同一工作单元
  -> 图划分生成初始 leaf clusters
  -> 每个 leaf cluster 记录 bounds、材质范围、边界和几何误差相关数据
```

**邻接是几何连续性的证据，locality 是执行与存储局部性的目标。** 只按空间格子切分，可能把拓扑上紧密连接的表面切出大量边界；只按拓扑连通性，又可能产生空间跨度很大、包围盒极松的簇，剔除效果很差。把两者一起用于图划分，是 UE 的工程选择：它不保证数学上全局最优，但能在构建成本、边界质量、包围体紧致度和运行时并行粒度之间取得可用平衡。

替代方案包括固定网格切块、按材质 section 切分或让美术手工定义微型 LOD 块。它们实现更简单，也可能适合规则地形或专用资产；但面对任意高模时，要么产生大量裂边和松散 bounds，要么把资产准备成本重新推给内容作者。Nanite 选择自动图划分，是为了让通用静态网格也能形成可并行的局部单元。

### 1.2 cluster group：为什么不能让每个簇各自生成父级

得到一层 clusters 后，构建器继续根据 cluster 之间的外部边邻接，把若干相邻 clusters 组成 **cluster group**。group 的教学含义不是“又一种容器”，而是：**这一组子 clusters 将作为一个兼容替换区域，共同产生更粗的父层 clusters。**

构建过程可以写成：

```text
一组相邻 child clusters
  -> 合并它们携带的几何
  -> 锁定 group 对外连接的 external edges
  -> 在 group 内部做 simplification
  -> 把简化结果重新切成一组 parent clusters
  -> 记录 child group 与 parent clusters 的替代关系和累计 LOD error
  -> 对 parent 层重复 grouping / simplification，直到形成粗层根数据
```

为什么要**按 group 共同简化**？设想岩壁相邻的 child A 与 child B 各自单独减面。A 可能移动共享边上的顶点，B 却保留原位置；两边生成的父级不再严丝合缝，混合精度时就会裂开。只要运行时允许 A 变粗而 B 仍细，这种不一致边界就会直接暴露。

group 共同简化解决的是内部一致性，而 **external-edge locking（外部边界锁定）** 解决 group 与邻居的接口一致性。构建器允许 group 内部大量改变拓扑，但约束对外连接边的位置，避免本 group 的父级破坏邻接 group 仍然依赖的接口。没有这层约束，层级越往上简化，裂缝越可能在不同 LOD cut 的交界处出现。

锁边不是免费午餐。它减少简化自由度，可能让同样三角形预算下的误差更高；group 太小，锁定边占比高、简化效率差；group 太大，构建成本、parent 工作集和局部选择粒度又会上升。因此“边界必须兼容”接近正确性的硬约束，而 group 大小、图划分权重和简化预算是可以权衡的工程选择。

### 1.3 parent simplification 与累计误差

parent clusters 表示“用更少三角形替换这一组 child clusters 后的结果”。每次简化都会引入新的几何误差；如果只记录当前一级的局部误差，经过多层替换后会低估最终结果相对原始高模的偏差。Nanite 因此需要能够表达**沿替代链累计的 LOD error**，让运行时比较的是当前候选相对于更精细来源的保守误差，而不是只看最后一次减面看起来改了多少。

误差值本身仍在对象空间；“它现在是否看得出来”由运行时 view 把它投影成屏幕误差。这个分工很重要：构建阶段负责提供可比较、层级单调可用的误差关系，运行时负责加入相机、分辨率和质量预算。若在构建时烘死某个观看距离，同一资源就无法自然适配 split-screen、动态分辨率、阴影视图或不同平台质量档。

### 1.4 三种结构必须分开：DAG、packed hierarchy 与 pages

“Nanite 有一棵簇树”只够做第一印象，调试时远远不够。至少要区分三套互相关联但职责不同的结构：

| 结构 | 它表达什么 | 谁主要使用 | 不解决什么 |
| --- | --- | --- | --- |
| **逻辑 Cluster DAG / group replacement** | 哪组 children 可由哪些 parents 兼容替代，误差怎样沿层级传播 | 离线构建与运行时 LOD 语义 | 不规定 GPU buffer 怎样紧凑排布，也不等于 IO 页 |
| **packed GPU hierarchy** | 适合 GPU 并行遍历的节点、bounds、误差、child/cluster 范围和页键 | 每帧 culling shader | 不代表所有引用数据当前已驻留 |
| **page / residency storage** | 哪些压缩 cluster 数据作为 root pages 常驻，哪些 streamable pages 可按需装入全局池 | 流送系统、GPU 页表/全局 buffers | 不直接决定当前 view 应选哪个 LOD cut |

逻辑上称为 DAG，是因为重点是“group 与 parent/child 的替代关系”，而不是让读者假定每个 cluster 都只属于一条简单对象树路径。运行时为了高效查询，会把所需关系打包成适合 GPU 的层级节点；存储系统又按页组织驻留和 IO。三者可以互相引用，但不能互换名词。

例如“层级节点存在”只证明 GPU 知道可以往哪里遍历，不证明目标 cluster 页已经驻留；“页已驻留”只证明数据可读，不证明当前 view 的 projected error 要求选择它；“逻辑 child 比 parent 更细”也不意味着二者在磁盘上相邻。把三套结构分开，缺页、LOD 和 hierarchy corruption 才能分别定位。

### 1.5 root pages 与 streamable pages：让缺细节退化为“较粗”，而不是“消失”

**root pages** 提供资源可画的保底集合，随资源注册保持可用；**streamable pages** 保存可按需进入全局 GPU 页池的更细数据。玩家走近岩壁时，view 可能要求更细 cut，但目标页尚未驻留。此时合理行为是停在仍可用的 streaming leaf / 较粗兼容 cut，并写出请求，而不是越过缺页继续访问无效内存，更不是让整面岩壁消失。

把岩壁放回三套结构：

```text
逻辑 DAG：近处区域应该用 child group 替换 parent group
  -> packed hierarchy：GPU 遍历到对应节点，发现 projected error 仍过大
  -> residency：child 所在 streamable page 尚未驻留
  -> 本帧：保留已驻留的较粗 compatible cut，同时输出页请求
  -> 后续尚未执行的 traversal：只有 transcode/scatter 已由 GPU 完成，才允许选择更细 cut
```

这也说明 full residency 不是唯一可行设计。把所有 Nanite 数据常驻显存，运行时最简单、没有 IO 延迟，但大型世界的内存成本不可接受；完全依赖 CPU 距离预测可以提前加载，却难覆盖遮挡、多视图和快速镜头变化；显式 prefetch 对过场和传送点很有效，但需要内容或玩法知道未来。Nanite 的反馈流送选择“GPU 告诉 CPU 实际缺什么”，换来更精确的需求证据，也接受至少数帧的回读/IO 延迟。后文第 6 节会把这个闭环补全。

## 2. 注册：资源必须先变成 GPU 能寻址的整数

资源数据加载进内存后，离“能画”还差一步：GPU 不能用 C++ 指针访问这份数据。它需要的是 buffer 加整数索引。所以 Nanite 要把岩壁这份资源注册进渲染线程上的全局 Nanite 流送系统，换回两个 GPU 可寻址的整数：

| 运行时索引 | 作用 |
| --- | --- |
| `RuntimeResourceID` | 标识这份 Nanite 资源，并附带保底页槽位的版本信息，用来过滤掉延迟到达的过期流送请求 |
| `HierarchyOffset` | 标识这份资源的层级数据，在全局层级 buffer 中的起始位置 |

这一步由**渲染线程**拥有：资源初始化会把工作投递到渲染线程，再由全局流送系统分配层级空间、保底页空间和运行时 id。`RuntimeResourceID` 之所以要带版本号，是因为 GPU 的缺页请求会跨帧回读——如果某个保底页槽位被新资源复用了，旧资源遗留的请求绝不能误命中新资源。版本号就是用来识别并丢弃这种“迟到的过期请求”的。

拿到整数之后，岩壁就回到普通 UE 场景对象的生命周期里。`UStaticMeshComponent` 在创建 scene proxy 时，会先判断当前平台、资源和材质是否都支持 Nanite：支持就创建 Nanite scene proxy，否则回落到传统静态网格 proxy。无论走哪条，这个 proxy 仍然遵守第 02 篇讲过的所有权规则——Game Thread 摘出快照，Render Thread 接管，SceneInfo 登记进 `FScene`。

最后一步把三者接到一起：primitive 的 shader 参数会带上 Nanite 资源信息。也就是说，GPU 侧从头到尾看不到 `UStaticMeshComponent`，也看不到任何 C++ 资源指针；它看到的只是几个整数：

```text
Primitive / instance 数据
  -> Nanite 资源 id
  -> Nanite 层级偏移
  -> transform / 包围盒 / 标志位
  -> 全局 Nanite buffers
```

这条链解释了一个重要的调试直觉：当“某个 Nanite 对象完全不显示”时，应该先顺着这条链查，而不是一头扎进 LOD 公式。依次确认——资源是否带有有效的 Nanite 数据、是否真的创建了 Nanite proxy、渲染线程是否已经确认了运行时 id 与层级偏移、primitive 参数是否把这两个整数写给了 GPU。只要其中一环断了，后面所有阶段都拿不到数据。

这里也顺手稳定一下 **instance culling** 这个词。它不是“簇级 LOD 的另一种名字”，也不是重新遍历 UObject。它发生在每帧 GPU 工作的入口处，拿到的是已经进入 Renderer / GPUScene 的 primitive 与 instance 数据，然后回答一个更早的问题：**这个实例有没有资格进入当前视图的 Nanite 工作队列？** 通过以后，后面的层级遍历才会去问“这个实例里的哪些节点和簇要画到多细”。

继续用岩壁案例：如果场景里有十段同资源的岩壁实例，平台能力、show flag、资源有效性等前置条件先决定 Nanite 路径是否会被启用；进入 GPU 工作后，instance culling 再根据当前 view 和 instance 数据，把视锥外或实例数据无效等候选挡在外面。剩下的实例才携带各自的 transform、bounds、资源 id 和层级偏移进入层级剔除。也就是说，**instance culling 改变的是进入 Nanite 管线的实例集合；cluster culling 改变的是某个实例内部的可见簇集合。** 调试“对象完全不显示”时看前者；调试“对象显示但局部精度/遮挡不对”时才继续看后者。

## 3. Packed view：把对象空间误差翻译成屏幕误差

资源注册完成后，Nanite 每帧不直接问“相机离岩壁多少米”，而是问：**候选几何相对于更精细版本的误差，投到这个 view 上还占多少像素？** 同样一厘米误差，在近景 4K 主视图里可能明显，在远景阴影视图里可能完全不可见。

每个 Nanite view 会被打包成 GPU 可读的 **packed view**。它不仅有 view/projection 矩阵，还包括 view rect、裁剪与 HZB 相关矩形、LOD 换算尺度等。概念上：

```text
对象空间累计 LOD error
  -> 通过 bounds / 深度 / 投影换算成屏幕尺度
  -> 再与每像素或每边质量预算比较
  -> 得到“当前 view 是否需要更细”的判断
```

动态分辨率、TSR 输入尺寸、split-screen rect、投影方式、Nanite 质量缩放和 mesh 级偏置都可能改变这个换算。于是“同一个资源有固定 LOD 距离”在 Nanite 里并不成立。view 是屏幕尺度的 owner，cluster 层级只提供对象空间 bounds 和误差。

这样设计的原因，是距离本身不能表达视觉误差。窄 FOV 下远处物体仍可能占很多像素；超宽视角下同样距离可能很小；不同分辨率对可见误差的容忍也不同。传统距离阈值更简单、CPU 成本低，适合对象数量和 LOD 档位有限的项目；屏幕误差更通用，但要求每个 view 正确提供尺度并在 GPU 上执行大量判断。

调试 LOD 抖动时，先核对 packed view 的 rect、投影、光栅尺寸和 LOD scale，再看 cluster 数据。若两个 view 使用了错误的共享尺度，同一岩壁会在左右眼、分屏或主视图/阴影视图间选择不一致精度；这不是离线简化突然坏了，而是“对象误差到像素误差”的翻译错了。

## 4. Compatible cut：选出的不是零散 clusters，而是一张无裂缝替代切面

GPU 遍历 packed hierarchy 时，会先用 node 级 bounds 和误差快速判断整片子树是否可能相关，再在更细粒度上判断 cluster。目标不是“把所有误差小于阈值的簇都加入”，而是建立一组彼此兼容、覆盖当前可见几何且不过度重叠的 clusters，也就是当前 view 的 **compatible cut**。

### 4.1 parent/child 误差共同限定替代边界

可以用下面的逻辑理解一个候选 cluster：

```text
当前 cluster 的 projected error 已经小到可接受
  AND 它所替代/被替代关系的 parent error 说明不应停在更粗一级
  -> 当前 cluster 才处于本帧 cut 的边界上
```

只检查“当前 cluster 足够细”会同时接受 parent 和 child，造成重复覆盖；只检查“parent 太粗”又可能一路细化到底，浪费几何预算。parent/child 误差边界共同表达“比我粗的不够、而我已经够”的区间。实际 shader 会利用 node 与 cluster 中打包的误差和 bounds 批量完成这些判断，但读者应记住的是区间语义，而不是某个比较符号。

### 4.2 group compatibility 防止局部替换撕开边界

离线构建时，一组 child clusters 共同生成一组 parent clusters；运行时也必须尊重这个 group replacement 关系。不能随意拿半个 parent group 和一部分不兼容 children 拼成画面，否则即使每个 cluster 单独误差都达标，它们也未必共享一致边界。

compatible cut 的“compatible”正是这个意思：允许同一对象不同区域处于不同层级，但交界必须落在构建阶段已经保证兼容的 group 边界上。它不是要求整面岩壁统一 LOD，而是把局部自由限制在不会产生孔洞、重叠和裂缝的替代组合内。

### 4.3 residency 是 cut 的第二个门槛

几何误差说“应该更细”，不代表更细数据现在可读。若遍历抵达一个 streaming leaf，发现目标子页未驻留，GPU 必须停在已驻留的兼容粗层并输出页请求。只有完整 streaming 安装链在 GPU 上完成后，尚未执行的后续 traversal 才能越过这里；已经结束的本次 traversal 不会被回写。

```text
质量门槛：当前 view 是否需要 child cut？
  -> 否：保留 parent/当前 cluster
  -> 是：检查 child page residency
       -> 已驻留：继续下钻，选择更细 compatible cut
       -> 未驻留：本帧使用 streaming-leaf fallback，并请求页面
```

这是正确性与可用性的折中。强行访问未驻留数据是不合法的；等待数据再绘制会让岩壁出现空洞；保留较粗 cut 能保证连续可见，但允许短时间精度不足。Nanite 选择“粗而完整”作为缺页降级目标。

### 4.4 LOD、遮挡和光栅路径是三类独立判断

必须纠正一个看似合理的说法：**轮廓边缘或遮挡交界并不会因为“它是轮廓/遮挡边界”就直接要求更细 LOD。** Nanite 的 LOD 主判据是投影误差；轮廓处若几何误差更容易在屏幕上暴露，可能间接需要更细，但那仍由 bounds/error/view scale 表达。遮挡判断只回答“当前证据下是否可见”，不改 LOD 预算。

同样，硬件还是软件光栅是在 cut 已选出之后决定执行车道。三者应按顺序理解：

```text
LOD selection：若可见，这里需要哪组 compatible clusters？
Occlusion/frustum culling：这些候选在当前 view 是否需要画？
Raster path selection：通过的 clusters 用 HW 还是 SW 更合适？
```

把三类判断混在一起，会导致错误调试：LOD 太粗时去查 HZB，遮挡缺块时去调像素误差，或看到 SW raster 就以为系统自动降级了模型精度。它们可以共享 packed view 和 bounds，却回答不同问题。

## 5. GPU 管线的五个阶段

到这里，岩壁已经被选出了可见簇。但从“选中”到“画进 GBuffer”，中间还有完整的一段路。Nanite 每帧既不是一个普通 draw call，也不是传统 BasePass 的某个前置优化——它是一条**完整的 GPU 驱动几何管线**。为了调试时不迷路，可以把它压成五个阶段，每个阶段都有明确的输入、输出和典型失败模式。

### 阶段 1：primitive / instance 筛选

输入来自 Renderer 已发布的 primitive/instance 数据、GPUScene、Instance Culling 和当前 packed view。这一层只回答：**哪些岩壁实例有资格进入本 view 的 Nanite 工作队列？** 它不会重新遍历 UObject，也不选择实例内部的 LOD。

先做对象/实例级筛选，是因为场景里大量实例可能整体在视锥外、被 show flag 排除、资源无效或不属于当前 pass。若跳过这一层，让每个实例都展开 hierarchy，再在 cluster 级发现整对象无关，会浪费节点队列、GPU 带宽和原子计数。CPU 对象级剔除也能完成一部分工作，并且在小场景更简单；Nanite 把大量实例判断留在 GPU，是为了减少 CPU draw 管理和让同一份 GPUScene 数据服务多视图/多 pass。

输出是初始 GPU 工作队列，携带 instance transform、bounds、资源 id、hierarchy offset 与必要 flags。这里首次失败时，岩壁通常**完全不进入**后续 hierarchy：应检查 proxy 分流、资源注册、GPUScene primitive/instance 数据和当前 pass 条件，而不是材质或 cluster error。

### 阶段 2：层级 + 簇剔除

这是 compatible cut 真正落地的阶段。GPU 不会把资源里的所有 clusters 平铺扫描，而是分级缩小集合：

```text
primitive / instance 候选
  -> hierarchy node：用较粗 bounds/error 一次排除或接受整片子树
  -> cluster：对剩余局部做 projected error、frustum/clip、residency 等判断
  -> visible clusters / streaming requests / post-pass candidates
```

若每帧对每个实例的所有 clusters 做平面扫描，Nanite 最想支持的“海量几何”本身就会把 culling 成本淹没。层级节点的价值是让一次测试代表许多 descendants；代价是要维护 bounds、误差和队列，并承受层级不紧致时的过度遍历。对三角形很少、对象级 LOD 已足够的普通 mesh，扁平 CPU draw 或简单 GPU cluster list 反而可能更便宜，这也是 Nanite 不是所有资产都必然受益的原因。

通过 node 级测试后，cluster 级才建立当前 compatible cut，并同时检查 streamable page 是否驻留。通过的 clusters 写入可见集合；缺页候选产生反馈；遮挡证据不足但不能安全删除的候选进入后续复测集合。**被 node 剔除、被 cluster LOD 排除、因缺页退回粗层、因遮挡待复测**是四种不同状态，不能都叫“cull 掉了”。

Nanite 的 **main/post 两阶段遮挡**也发生在这里。难点是时序：当前帧更完整的深度尚未由 Nanite 自己产生，但早期剔除又希望利用已有 HZB。条件允许且存在可用历史 HZB 时，main cull 用它做保守判断；main raster 产生新深度证据后，系统建立更新后的 HZB，再让 post cull 复测先前不能安全下结论的候选。没有可用 previous HZB 或相关路径关闭时，不能假定这套 two-pass 一定存在。

把 two-pass 写成状态换手，会更接近实际调试：

```text
可用 previous HZB
  -> main cull：确定可见、确定遮挡或保留不确定候选
  -> main raster：可见 clusters 写出新的 VisBuffer / 深度证据
  -> 更新用于复测的 HZB
  -> post cull：只复测 main 阶段保留的不确定候选
  -> post raster：补画复测后仍可见的 clusters
```

这套设计以额外队列、一次 HZB 更新和 post 工作换取更少误删、更少无效光栅。替代方案是只用 previous HZB，成本更低但快速运动和新显露区域的证据更旧；完全不做遮挡最保守，却会光栅更多隐藏 clusters；更激进地一次性删除“不确定”候选成本低，却会产生可见孔洞。UE 选择 main/post，是在遮挡收益与时序正确性之间折中。遮挡闪烁时应查候选是否进入 post、两个 rect 是否一致、新 HZB 是否建立和 post 是否补画，而不是先调 LOD scale。

### 阶段 3：raster 分组 + 硬件/软件光栅

阶段 2 只产生了要光栅的 visible clusters。阶段 3 先按几何执行条件组织 **raster bin**，再在硬件和软件光栅之间分配工作。raster bin 关心双面、masked/可编程光栅、位移、spline/skinning 等会改变**如何生成覆盖**的状态；它不表示最终材质应该如何计算 GBuffer。

硬件路径使用平台图形管线能力；软件路径使用 compute 处理对硬件固定光栅效率不友好的微三角。选择通常参考 cluster 投影后的边尺度/覆盖特征，并受平台、pass 类型、可编程光栅能力、async compute 和强制设置约束。直观上，大三角更容易让固定功能硬件高效工作；大量只覆盖极少像素的微三角会让传统三角形 setup 成本占比过高，软件路径可以采用更适合 Nanite 数据的并行方式。但这是成本启发式与能力约束，不是“尺寸超过一个固定值就永远走 HW”的跨平台定律。

调度不只有一种：

| 调度 | 含义 | 优势 | 代价/适用条件 |
| --- | --- | --- | --- |
| Hardware-only | culling 强制把可执行工作交给 HW 路径 | 实现和同步简单，适合不支持/不需要 SW 的路径 | 微三角可能低效，无法利用 compute 专用优势 |
| Hardware then Software | 先完成 HW，再运行 SW | 阶段关系清楚，资源冲突较易控制 | 两条路径串行，重叠机会少 |
| Overlap | HW 图形工作与 SW compute 尽量并发 | 可以利用不同队列/执行资源，提高吞吐 | 依赖平台 async 能力、资源同步和负载平衡；错误配置可能反而增加等待 |

纯 compute 光栅可以统一执行模型、擅长微三角，却会放弃硬件光栅对大三角、深度/覆盖的成熟吞吐；纯硬件路径简单且兼容工具，但可能被海量微三角 setup 限制。Nanite 选择混合路径，是把不同 triangle regime 交给更合适的执行资源；在不支持有效 async、场景主要是大三角或平台驱动约束强时，硬件-only 可能是更好的选择。

岩壁上两个都被 compatible cut 选中的 clusters：左侧大块岩面投影三角较大，可进入 HW；右侧风化碎片形成密集微三角，可进入 SW。它们的**几何精度已经确定**，现在只是在分执行车道：

```text
可见簇集合不变
  -> raster bin 记录覆盖生成所需的管线状态
  -> HW / SW 各自产生间接参数
  -> 两条路径都写回同一种 VisBuffer / depth 可见性合约
```

这里必须和后面的 **shading bin** 分开：

| bin | 分组对象 | 回答的问题 | 典型输入 |
| --- | --- | --- | --- |
| raster bin | clusters / raster work | 怎样产生可见性覆盖，用哪种 raster pipeline/HW-SW 路径 | 几何与可编程光栅状态 |
| shading bin | 已可见 pixels | 哪一组 BasePass material compute shader 处理这些像素 | material section、ShadingMask、shading command |

同一个材质可能因几何光栅状态进入不同 raster bins；不同几何只要最终材质着色命令相同，也可能汇入同一 shading bin。看到 SW/HW 差异不要解释成材质或 LOD 差异；看到材质错位也不要先改 raster threshold。

### 阶段 4：导出深度 / ShadingMask，并为着色分组做准备

主视图光栅的核心产物是可见性身份。`VisBuffer64` 不是 SceneColor，也不是完整 GBuffer；它让后续阶段能够回答“这个屏幕样本最终由哪个 Nanite 几何命中占有”，并据此恢复 triangle/cluster/instance 与材质关联。深度、可见 cluster 列表和其他元数据与它一起构成光栅结果，但不要把所有信息都想象成塞在同一个 64-bit 数值里。

为什么先写 identity、以后才跑材质？因为微三角场景里，同一像素可能经历大量 overdraw 候选，最终只有最前面的命中需要完整 BasePass material。若在每个 triangle raster 时立刻执行昂贵材质，大量被后续深度覆盖的 shading 工作会浪费。VisBuffer 把“谁赢了深度/覆盖”先定下来，随后只给最终可见 pixels 分派材质工作。

这不是免费优化。系统要付出一张 identity buffer、从 identity 恢复属性的逻辑、像素分类与间接参数、compute shading 管理，以及在没有传统 pixel quad 自然上下文时处理纹理导数/邻域信息的复杂度。若场景几何简单、overdraw 很低、材质路径依赖传统图形管线特性，立即在 raster pixel shader 中着色可能更直接。Nanite 选择 visibility-first，是因为它的目标工作负载正是“几何单元极多、真正可见像素有限”。

接下来 Nanite 要把自己的可见性结果发布到主场景共享合约：导出 `SceneDepth`、可选速度/模板等，并创建 `ShadingMask`。`ShadingMask` 告诉后面的材质阶段“这个屏幕像素属于哪个 shading bin”。深度导出可根据平台和配置走 compute 或光栅路径；实现分支可以不同，但主视图输出责任一致：

```text
VisBuffer + raster results
  -> SceneDepth / 模板 / 可选 velocity 等主场景几何合约
  -> ShadingMask：每个可见 Nanite 像素的材质执行类别
```

这条交接给出强分诊证据：岩壁 `SceneDepth` 正确，说明注册、compatible cut 和光栅至少有一条有效链；若 GBuffer 材质仍错，调查应后移到 section/material 注册、ShadingMask、shading bin 和 compute dispatch。反过来，VisBuffer 有命中但 SceneDepth 为空，则错误落在导出合同，不应直接怪材质图。

### 阶段 5：材质着色，写入 BasePass GBuffer

Nanite 不按“每个 mesh section 立刻发一个 classic BasePass draw”的方式执行主视图材质。它把持久注册、每帧命令建立、像素分类和最终 dispatch 分开：

```text
材质 section / Nanite material pipeline 注册
  -> 获得可供场景与 proxy 引用的 shading bin / pipeline 身份
  -> 每帧为当前 BasePass 配置构建 shading commands
  -> 扫描 ShadingMask，把最终可见 pixels 分类到各 shading bins
  -> 生成 shading-bin metadata、pixel data 与 indirect args
  -> 对有工作量的命令 dispatch BasePass material compute shader
  -> 写 SceneColor / GBuffer / 相关 BasePass outputs
```

持久注册回答“这个材质路径存在且叫什么”；每帧 command build 回答“当前 view、BasePass permutation 和 render targets 下，哪些命令可执行”；ShadeBinning 回答“本帧屏幕上哪些 pixels 属于哪个命令”；indirect args 让没有可见像素的 bin 不必按满屏 dispatch。把四层都叫“材质分组”会掩盖不同失败点。

把岩壁拆成两个材质槽位来看，这一步就不再抽象：

| 状态 | 石头 slot | 苔藓 slot | 读者要抓住的变化 |
| --- | --- | --- | --- |
| 光栅之后 | VisBuffer 里有石头三角命中 | VisBuffer 里有苔藓三角命中 | 几何阶段只证明最终可见 identity |
| 深度导出 | 写入同一张 SceneDepth | 写入同一张 SceneDepth | 深度合约已经发布给主场景 |
| ShadingMask | 像素标成石头对应的 shading bin | 像素标成苔藓对应的 shading bin | 材质归属从三角形/section 变成屏幕像素上的 bin id |
| ShadeBinning | 收集石头像素并计数，建立 bin data/indirect args | 收集苔藓像素并计数，建立 bin data/indirect args | 可见像素被整理成 compute shader 能消费的工作队列 |
| BasePass compute | 写石头材质的 GBuffer payload | 写苔藓材质的 GBuffer payload | 终点仍是同一组 BasePass targets |

如果把这一步口语化叫“材质解析”，它解析的不是运行时重新编译材质节点图，而是 **VisBuffer / ShadingMask 里的几何命中，应该路由给哪组已经准备好的 BasePass material compute shader**。若 shading command 没建立，pixel identity 正确也没有执行者；若 ShadingMask 映射错，正确 shader 会处理错误像素；若 indirect args 为零，命令存在却不会获得实际工作量。

这一步背后最重要的设计取舍是：**几何可见性与材质执行解耦。** 几何阶段可以处理成千上万个小 clusters，却不因此生成同等数量的材质 draws；材质阶段面向最终可见 pixels 与 shading bins 工作。工作量更接近“看得见多少像素、涉及多少实际材质命令”，而不是“资源里有多少微三角”。代价则是上一节列出的 VisBuffer、分类、属性恢复、导数和 compute pipeline 复杂度。

最准确的正向模型是：**Nanite 绕过了经典 BasePass 的逐 mesh draw 前半段，但通过 Nanite compute material shading 交付同一类 BasePass GBuffer 输出合同。** 它绑定当前 BasePass 目标并携带 view、scene textures、DBuffer、Nanite 可见性与 shading-bin 数据，最终写入后续 deferred lighting 认识的 SceneColor/GBuffer 资源。后续光照不需要为“这个像素来自 classic mesh draw 还是 Nanite compute shading”另建一套材质语义。

这句话必须保留条件：相同的是主场景消费者所依赖的输出合同，不是说两条实现拥有完全相同的执行顺序、shader stage、所有材质能力或平台支持。遇到 Nanite 材质限制时，不能用“最终都是 GBuffer”推导出前半段所有功能天然等价。

### 与 Depth / BasePass / VSM / Lumen 的边界

把阶段 4、5 和相邻章节对齐，可以避免把 Nanite 写成“到处接管一切”的万能管线：

- 对 **DepthPrepass / HZB** 来说，Nanite 是主场景深度的生产者，也是 HZB 遮挡证据的消费者。它在 PrePass 窗口导出 `SceneDepth`，但 HZB 的完整构建、resolve 时机和后续消费者属于第 09 篇。
- 对 **BasePass** 来说，Nanite 只是把前半段从传统 draw 改成 VisBuffer + ShadingMask + compute shading。GBuffer 的槽位、payload 语义、SceneTextures 发布合约仍属于第 10 篇。
- 对 **VSM** 来说，Nanite 走的是 shadow / virtual shadow map 视图，把深度写进 VSM 的物理页池。它复用 Nanite 的剔除和光栅能力，但目标不是主视图的 `SceneDepth` 或 BasePass GBuffer。
- 对 **Lumen Card Capture** 来说，Nanite 可以进入 `LumenCardCapture` 这个独立 mesh pass，把材质结果写进 Lumen 的 card capture atlas。它同样不是主视图 BasePass 的另一套颜色输出。

因此调试时先问当前 Nanite 工作属于哪个消费者：主视图 depth / BasePass、VSM shadow depth，还是 Lumen card capture。消费者不同，packed view、输出目标、ShadingMask 或 shading command 的语义也不同；不能把主视图里成立的证据直接搬去解释 VSM 或 Lumen。

## 6. 流送反馈：LOD 选择与页驻留形成闭环

第 4 节的 streaming-leaf fallback 只保证“缺页时当前 traversal 仍能画”。要让更细 cut 真正可选，还要跨过 GPU request、readback、CPU 选页、页槽位预留、数据请求、ready-page bookkeeping、GPU transcode、scatter fixup 和 GPU completion 多个异步边界。**request 已读回、slot 已预留、数据已 ready、CPU install 已登记、transcode 已执行、scatter 已执行、GPU 已完成、后续 traversal 已消费**是不同状态，不能统称为“页面装好了”。

### 6.1 GPU 请求只是需求证据

hierarchy/cluster culling 发现“当前 compatible cut 需要更细页，但该页不驻留”时，在允许反馈的 pass 中写出请求。请求必须能够定位运行时资源和页范围，并携带足够优先级信息。GPU 不直接打开资源文件，也不等待 IO；它继续使用较粗 fallback 完成本帧。

这个异步设计避免 GPU 因磁盘/解压等待而停住，但天然引入延迟。请求写出后，CPU 要等 readback 可锁定，数据要经过后续安装图，GPU 还要完成 transcode 与引用更新。已经执行完的那次 culling 不会回头重跑；只有图时序上尚未执行的后续 traversal 才可能看到新状态，实践中也经常落到后续帧。把“先粗后细”完全消灭，需要预知未来需求或让全部数据常驻，而不能靠把一个反馈 CVar 调得更激进解决因果延迟。

### 6.2 delayed readback 必须经过版本过滤

GPU request 经 readback 到达 CPU 时，原来的资源槽位可能已经注销并被另一个资源复用。`RuntimeResourceID` 因此不仅表示槽位，还携带版本语义。CPU 处理请求时先验证版本；过期 request 必须丢弃，否则旧岩壁的延迟请求可能把新资源的无关页装进同一槽位，造成难以复现的数据破坏。

这是异步句柄系统的通用设计：整数槽位解决 GPU 可寻址，版本解决槽位复用后的身份歧义。替代方案是不复用 id，最终会耗尽或扩大索引空间；每次等所有 GPU readback 完成后再释放槽位，正确但会拖慢资源生命周期。版本化 id 用较小元数据换取异步安全。

### 6.3 优先级选择不是“请求什么就立刻全装”

有效 request 会展开成候选页面，但每次更新的 pending 数量、IO、解压、上传和页池都有预算。流送系统先合并 GPU、显式调用与 prefetch 三类来源，对重复页面累计初始 priority，并区分已经 registered 的已知页面与第一次请求的新页面；随后递归加入 parent dependencies，把 child 需求的 priority 继续传播给依赖页。只有这份依赖闭合后的候选集合，才进入最终 priority selection。

最终 selection 受 `MaxSelectedPages` 和本次更新剩余 pending budget 约束，并在选择过程中更新 registered-page LRU 热度。优先级的目的不是保证绝对公平，而是在预算内先改善最影响当前画面的缺页；parent priority 传播则防止 child 入选而依赖仍被排在预算之外。若完全按到达顺序处理，远处大量低价值请求可能阻塞镜头中心的近景；若永远只服务最高优先级，低优先级区域可能饥饿。具体排序和预算是工程选择，硬约束是：**依赖集合闭合与 priority 传播必须早于 budget selection。**

### 6.4 单页请求的真实顺序：先占可复用 slot，再请求数据

一个 child page 不是可随意独立塞进 GPU pool 的字节块。其 cluster 解码可能依赖 parent pages，hierarchy 引用也需要 fixup。因此系统先把请求集合和依赖关系整理好，再决定有限 GPU page slots 分给谁。对同一个新 page，因果顺序是：

```text
GPU request readback
  -> RuntimeResourceID / version filter
  -> 合并 GPU / explicit / prefetch requests，对重复页面累计初始 priority
  -> 递归加入 parent dependencies，并向依赖页传播 priority
  -> 在 MaxSelectedPages / per-update pending budget 内做最终 priority selection
       同时更新已 registered page 的 LRU 热度，形成 SelectedPages
  -> 从本轮未引用、且 RefCount == 0 的 LRU 候选中预留/复用 GPU page slot
       -> 没有可用 slot：本轮停止继续选择，不为该 page 发起数据请求
       -> 复用旧 slot：解除必要的旧 registered 关联，保留明确的替换关系
  -> 把新 page 登记为 pending / registered，并记录选定 GPUPageIndex
  -> issue IO / DDC / 已驻留 memory data request
  -> 数据完成后进入 ready-page CPU install bookkeeping：
       resident virtual mapping、dependency list、upload staging、fixup 更新计划
  -> RDG independent transcode：先完成不依赖 parent 解码结果的工作
  -> 按 parent dependencies 拓扑排序执行 dependent transcode
  -> flush cluster scatter 与 hierarchy scatter fixup
  -> RDG execute / RHI recording / Platform Queue Submit / GPU completion
  -> 图中尚未执行的后续 traversal 才能把新页视为可消费 resident data
```

**slot reservation 在数据请求前。** 这是页池预算与 IO 预算保持一致的关键：若没有可复用 slot，提前读取数据只会占用 IO 和 staging，最后仍无处安装。反过来，这也意味着 IO 尚在等待时，slot/registered bookkeeping 已可能发生变化；看到 pool churn 不能推断 IO 已完成。

**ready-page CPU install 不等于 GPU 可访问。** CPU 此时可以更新 resident mapping、准备上传数据、整理依赖并生成 fixup 计划，但 cluster page data 仍要进入 RDG transcode。`independent transcode` 与按 parent 顺序执行的 `dependent transcode` 又是两层：前者准备每页可独立解码的内容，后者必须等本批依赖先成立。

**transcode 完成也不等于 hierarchy 引用已经发布。** cluster/hierarchy scatter flush 负责把准备好的更新散写到全局结构；这些 pass 被加入 RDG 仍只代表工作已声明。经过 RHI 记录、Platform Queue Submit 和 GPU completion 后，写入才对满足图顺序的后续消费者成立。已经跑过的 traversal 不会因为 CPU mapping 或后来的 scatter 自动重算。

这里的 GPU completion 指覆盖这些 producer writes 的 GPU 工作已经按依赖完成，使后续 traversal 能看见结果；它不要求 CPU 为整帧做同步等待。若 traversal 与安装工作处于同一图中，正确的 RDG 依赖和 GPU 执行顺序可以建立可见性；若 traversal 已先执行，则只能等待再后面的消费机会。

### 6.5 有限页池意味着 residency 也是竞争结果

streaming pool 不是无限显存。新 page 在发起数据请求前就必须从 LRU 中找到本轮未引用且 `RefCount == 0` 的候选 slot；复用时会改变旧注册关系，并在 ready install 阶段完成旧 resident 内容的卸载/替换 bookkeeping。若找不到候选，本轮选页会提前停止。若工作集长期大于 pool，玩家来回转头会不断预留、驱逐和重载，表现为局部细节反复变粗、IO 持续繁忙或请求永远追不上。

这时增大 pool 可能有效，但会占用更多显存并挤压其他渲染资源；降低 Nanite 质量或减少资产细节能缩小工作集，却牺牲几何质量；显式 prefetch 可以在传送、过场和已知路径前提前装页，但需要上层知道未来；CPU 相机预测能掩盖 readback 延迟，却可能加载最终不可见的数据；full residency 消除运行时 streaming 抖动，但只适合数据规模可控的平台/场景。

### 6.6 岩壁迟迟不细化：沿闭环找最后成立状态

```text
packed view 的 projected-error 预算是否真的要求 child cut
  -> hierarchy 是否抵达正确 streaming leaf
  -> request 是否写出并进入 readback
  -> RuntimeResourceID/version 是否仍有效
  -> GPU / explicit / prefetch requests 是否合并并累计初始 priority
  -> parent dependencies 是否递归进入候选，priority 是否传播到依赖页
  -> MaxSelectedPages / pending budget 内的最终 selection 是否选中它，并更新 registered-page LRU
  -> 是否找到本轮未引用且 RefCount == 0 的 LRU slot
  -> 旧 registered 关联是否正确解除，新 page 是否登记 pending/registered
  -> IO / DDC / memory data 是否完成并进入 ready 集合
  -> CPU install 是否只完成 resident mapping、upload staging 与 fixup 计划
  -> RDG independent transcode 是否建立
  -> parent-dependent transcode 是否按拓扑顺序完成
  -> cluster/hierarchy scatter 是否 flush
  -> Platform Queue Submit 后 GPU 是否完成这些写入
  -> 尚未执行的后续 traversal 是否真正读到新 resident page
  -> pool 压力是否马上把它再次驱逐
```

因此 popping 不一定是 error 阈值错。它可能是反馈延迟的正常可见结果，也可能停在 request/version、priority/budget、依赖、slot reservation、数据请求、CPU install staging、GPU transcode、scatter、GPU completion 或后续消费任一层。看到 CPU resident mapping 已更新时，不能跳过 transcode/scatter 直接宣布 GPU page 可读；只有整条闭环都成立，才值得继续怀疑 compatible-cut 选择。

## 7. 所有权与生命周期：每类数据该活在哪一层

Nanite 横跨 Engine、Renderer、RDG、GPU 和流送系统这么多层。调试时最容易犯的错，往往不是算错某个公式，而是**把一个数据放错了层**——比如想把一个只活一帧的 buffer 缓存到下一帧，或者想让 shader 去读一个还活在 Game Thread 的对象。下面这张表把各类数据归位：

| 数据层 | 生命周期 | 典型内容 | 所有者 |
| --- | --- | --- | --- |
| 构建期逻辑数据 | 只在构建/派生数据生成阶段 | adjacency、cluster groups、parent/child replacement DAG、simplification error | Nanite 资源构建器 |
| 烘焙 / 序列化资源 | 跟随 mesh 资源 | packed hierarchy、root/streamable page 描述、压缩 cluster 数据、page dependencies | 资源系统 / Engine |
| 渲染线程全局 Nanite 状态 | 跨帧持久 | 运行时资源 id、全局 hierarchy/cluster buffers、物理页池、驻留与 fixup 状态 | Nanite 流送系统 |
| 场景 primitive 状态 | primitive 注册期间 | Nanite scene proxy、GPUScene 参数、资源 id/offset、材质 raster/shading pipeline 注册 | Renderer / `FScene` |
| 每帧 RDG 状态 | 仅当前图调度与 GPU 执行窗口 | packed views、node/cluster queues、compatible cut、raster/shading 数据，以及 streaming transcode/scatter passes | RDG / Nanite renderer / streaming uploader |
| 跨帧 CPU 流送状态 | 跨帧且异步 | request readback/version、GPU/显式/prefetch 合并与初始 priority、parent priority 传播、budget 内 final selection/LRU、slot、pending/registered、数据请求与 ready staging | Nanite 流送系统 |

这张表能直接回答“我新加的 debug 数据该放哪”：parent/child 替代关系的构建诊断属于构建期；每份资源不变的 packed hierarchy/page 属性属于烘焙资源；每个 primitive 的开关或材质注册状态属于 proxy/Scene；本帧 cut、raster、shading 以及 transcode/scatter 是 RDG/GPU 工作；跨帧 request、slot、pending、IO 与 ready bookkeeping 由流送系统持有。把当前帧 `VisBuffer` 指针缓存到下一帧会悬空；把 CPU resident mapping 当作 GPU 数据已经完成会越过 transcode/scatter；把运行时 pool 槽位烘进资产则会把可复用地址误当永久身份。

## 8. Unity 桥接：别把 Nanite 当成 LODGroup

回到开篇那个误解。Unity 的 `LODGroup` 经验能帮你理解“近细远粗”这个**目标**，但解释不了 Nanite 的**实现**。把两条管线并排放，差异一目了然：

```text
Unity LODGroup：
  CPU 按对象的距离 / 屏幕占比，选择一整套 mesh LOD
  renderer list 提交选中的那套 mesh
  材质 pixel shader 随 draw call 运行

UE Nanite：
  StaticMesh 构建出 group-compatible Cluster DAG，再打包成 hierarchy/pages
  GPU 按 view 像素误差选 compatible cut，并用 HZB 独立判断遮挡
  raster bins 把可见 clusters 分到 HW/SW，产生 VisBuffer / depth
  shading bins 把最终 pixels 分给材质命令
  compute 材质着色按 shading bin 写进同一套 GBuffer
  GPU 缺页请求反过来驱动页流送
```

这几个差异会实实在在地改变你的调试习惯：

- **对象在、但局部太粗**：不一定是 mesh LOD 没切换（Nanite 根本没有“整对象切档”这一步），更可能是页驻留没跟上，或层级遍历没往下钻。
- **深度正常、但材质错**：别只去查材质节点，还要查 ShadingMask、着色 bin 数据和 BasePass 的 compute dispatch（对应第 5 阶段）。
- **某些簇走硬件、某些走软件**：这不代表它们精度不同，只是光栅执行路径不同（对应第 4 节的澄清）。
- **光照看不出差别**：光照不需要为 Nanite 做特殊分支去读另一套颜色——它读的就是 BasePass 发布后的 GBuffer。

传统 `LODGroup` / authored LOD 并没有因此失去价值。对象和三角形数量很少时，Nanite 的 hierarchy、VisBuffer、binning 与 streaming 元数据可能没有足够收益；需要美术严格控制每一档轮廓/拓扑或平台不走 Nanite 路径时，显式 LOD 更可预测；已知镜头的过场也可以用预加载和手工 LOD 换取确定性。更好的方式取决于目标：Nanite 优化的是大规模细粒度几何的自动选择与可见像素材质执行，不是无条件替代所有 mesh 表达。

## 9. 调试主线

### 9.1 岩壁全过程状态表

下面不是函数调用顺序，而是岩壁从资产到 GBuffer、再到下一次更细 residency 的**状态成立链**：

| 阶段 | 岩壁此刻的状态 | Owner / 推进者 | 关键数据变化 | 没成立时的表象 |
| --- | --- | --- | --- | --- |
| 离线切簇 | 原始三角形成为局部 leaf clusters | 资源构建器 | adjacency/locality → cluster bounds/edges | 构建失败或 cluster 分区质量差 |
| 建立替代层级 | child groups 共同简化成 parents | 资源构建器 | external edges 锁定，累计 LOD error 建立 | 混合精度裂缝、误差关系异常 |
| 打包资源 | 逻辑 DAG 变成 packed hierarchy + pages | 资源构建器/资源系统 | root/streamable pages 与 dependencies 序列化 | hierarchy/page 引用无效 |
| 运行时注册 | 资源获得版本化 id 和 hierarchy offset | Render Thread 流送系统 | 资源身份进入全局 buffers/页池 | 对象完全进不了 Nanite |
| Scene 发布 | proxy/GPUScene 能指向 Nanite 资源 | Renderer/Scene | primitive/instance 参数携带 id、offset、transform | instance 阶段找不到资源 |
| view 建立 | 当前屏幕质量目标可由 GPU 读取 | Render Thread/RDG | packed view 带 rect、projection、LOD/HZB 信息 | 分屏/分辨率变化时 LOD 异常 |
| 分级剔除 | instance → node → cluster 缩小候选 | GPU culling | compatible cut、post candidates、requests | 整体缺失、局部孔洞或过度工作 |
| residency 门控 | 细页可用，或退回 streaming-leaf fallback | GPU culling | resident cut 与缺页 request 分开 | 显示但长期过粗 |
| raster binning | visible clusters 获得覆盖生成管线 | GPU | raster metadata/indirect args，HW/SW 分流 | cut 正确但无 VisBuffer 命中 |
| 可见性光栅 | 最终可见 identity 与深度候选建立 | HW graphics / SW compute | VisBuffer、visible clusters、depth evidence | 光栅缺块、HW/SW 路径差异 |
| 主场景导出 | Nanite 几何发布到 SceneDepth/ShadingMask | Nanite composition | depth/velocity/模板与材质分类入口 | VisBuffer 有，SceneDepth 或材质入口无 |
| shading command | 当前 BasePass 的材质执行者成立 | Render Thread + RDG | section/pipeline → shading command | 部分材质没有可执行命令 |
| ShadeBinning | 可见 pixels 获得 bin data/indirect args | GPU | ShadingMask → 像素工作队列 | 深度对但某材质不着色 |
| BasePass compute | Nanite pixels 写入共享 GBuffer 合同 | GPU compute | material inputs → SceneColor/GBuffer outputs | 后续光照读到空/错材质 |
| request 接收 | 缺页需求经 readback 进入 CPU 并通过资源版本过滤 | GPU readback/流送系统 | request → 有效 RuntimeResourceID/page key | 过期请求被丢弃或一直 fallback |
| 请求汇总 | GPU、显式与 prefetch 请求合并，重复页面累计初始 priority | 流送系统 | 多来源 requests → registered/new candidates | 某来源未进入集合或 priority 未累积 |
| parent 传播 | 递归加入 parent dependencies，并把需求 priority 传播给依赖页 | 流送系统 | child candidates → dependency-closed candidate set | child 很热但 parent 仍未获得选择机会 |
| 最终选择/LRU | 在 `MaxSelectedPages` 与 pending budget 内按最终 priority 形成 SelectedPages，并更新 registered-page LRU | 流送系统 | dependency-closed candidates → SelectedPages/LRU heat | 候选有效但本轮预算未选中 |
| slot 预留 | 数据请求前获得可复用 GPUPageIndex | 流送系统/LRU | 仅使用本轮未引用且 `RefCount == 0` 的候选；必要时解除旧注册 | 无空闲 slot，因而没有后续数据请求 |
| pending/数据请求 | 新 page 先登记 pending/registered，再 issue IO/DDC/memory request | 流送系统/资源系统 | selected key + GPUPageIndex + data request | slot 已占但数据仍未 ready |
| CPU ready install | CPU 建立 resident mapping、dependency list、upload staging 与 fixup 计划 | 流送系统 | ready bytes → uploader/fixup bookkeeping | CPU 显示 resident，但 GPU 数据仍不可访问 |
| GPU transcode | independent pass 先执行，dependent passes 按 parent 拓扑推进 | RDG/streaming uploader/GPU | staging bytes → cluster page data | CPU install 正常但 page data 未解码完成 |
| scatter 发布 | cluster 与 hierarchy 更新散写进全局结构 | RDG/GPU | fixup plan → global cluster/hierarchy references | page data 有了但 traversal 仍找不到正确引用 |
| GPU 完成 | RDG/RHI 工作经过 Platform Queue Submit 并由 GPU 完成 | RHI/平台队列/GPU | transcode/scatter 写入达到完成深度 | 只看到 RDG event 或 submit，尚不能证明可读 |
| traversal 消费 | 图中尚未执行的后续 culling 读取新 resident page | Nanite culling/GPU | 新页进入 compatible cut；仍受后续 LRU 压力 | 安装链完整但当前 traversal 已经跑过 |

这个表还说明 owner 为什么要分层：资源构建器不能决定某一帧 view 的 cut；GPU culling 不能执行磁盘 IO；流送系统不能回头修改已经完成的本帧 cut；material compute 也不该重新决定 geometry residency。每层只推进自己拥有的状态，通过明确数据把结果交给下一层。

### 9.2 最后成立状态证据梯

调试时从上往下找**最后一个有证据成立的状态**，不要同时改 LOD、HZB、材质和 pool：

| 级别 | 要证明什么 | 若成立，下一步查什么 |
| --- | --- | --- |
| 1 | 资产含有效 packed hierarchy、root pages 与 cluster 数据 | 运行时注册 |
| 2 | 版本化 resource id、hierarchy offset 与 root residency 有效 | proxy/GPUScene 参数 |
| 3 | 当前 instance 进入 Nanite 初始工作队列 | hierarchy node traversal |
| 4 | node culling 抵达预期局部，packed view/rect/scale 正确 | compatible cluster cut |
| 5 | parent/child error 形成兼容 cut；缺页时有 resident fallback | 遮挡与 raster bin |
| 6 | HZB main/post 分支在当前条件下有效，不确定候选被保守处理 | visible clusters 与 raster args |
| 7 | raster bin metadata/indirect args 非空，HW/SW 选择符合平台和投影特征 | VisBuffer/depth 命中 |
| 8 | VisBuffer 中有岩壁的最终可见 identity | SceneDepth/ShadingMask 导出 |
| 9 | `SceneDepth` 正确且 `ShadingMask` 指向预期材质类别 | shading command/build |
| 10 | 石头/苔藓各自的 shading command 存在且可见 | ShadeBinning data/args |
| 11 | 可见 pixels 已进入正确 shading bin，indirect args 有工作量 | BasePass compute outputs |
| 12 | SceneColor/GBuffer payload 正确 | 后续 Lighting 合同，问题已离开 Nanite 主线 |
| 13 | 缺页 request 已写出、readback 并通过 version filter | GPU/显式/prefetch merge 与初始 priority |
| 14 | 多来源请求已合并，重复页面 priority 已累计 | parent dependencies 与 priority 传播 |
| 15 | parent 集合闭合并获得传播后的 priority | `MaxSelectedPages` / pending budget 内 final selection 与 registered-page LRU 更新 |
| 16 | SelectedPages 已形成且目标 page 本轮入选 | LRU slot reservation |
| 17 | 找到本轮未引用且 `RefCount == 0` 的 slot，旧注册正确解除，新 page 已 pending/registered | IO/DDC/memory request；注意 slot 在 IO 前已占用 |
| 18 | 数据请求完成并进入 ready 集合 | CPU install bookkeeping |
| 19 | resident mapping、dependency list、upload staging 与 fixup 计划成立 | 仍不能声称 GPU page 可访问；继续查 RDG transcode |
| 20 | independent transcode 与按 parent 拓扑执行的 dependent transcode 完成 | cluster/hierarchy scatter |
| 21 | cluster 与 hierarchy scatter fixup 已执行 | Platform Queue Submit 与 GPU completion |
| 22 | 覆盖 transcode/scatter 的 GPU 工作已完成 | 尚未执行的后续 traversal 是否消费 |
| 23 | 后续 culling 已能选择新细页且未立刻被 pool 驱逐 | 若仍粗，再回查 error/cut，而非 streaming |

四类常见现象因此能快速分诊：**完全不显示**通常止于 1-4；**遮挡闪烁/缺块**重点看 5-8 中的 HZB rect 与 post 候选；**有深度但材质错**说明至少到 9，应查 10-12；**长期过粗**若 5 已产生 fallback，则沿 13-23 区分 request merge、parent priority、final selection/LRU、slot、data ready、CPU install、GPU transcode、scatter、completion 与消费。只有证据显示 streaming 已完整闭环，才回到 projected error/compatible cut。

## 10. 收束：一句话主线

把全章压成一句话：

> **UE 先把 StaticMesh 构建为 group-compatible 的 Cluster DAG：相邻 child clusters 共同简化成锁定外边界的 parents，再打包成 GPU hierarchy 与 root/streamable pages；运行时资源获得版本化 id 并通过 proxy/GPUScene 进入场景，每个 packed view 让 GPU 按 projected error、group compatibility 与 residency 选出 cut，再分级剔除、用 raster bins 分配 HW/SW 光栅产生 VisBuffer 和 SceneDepth/ShadingMask，随后把最终可见 pixels 按 shading bins 路由到 BasePass material compute，交付共享 GBuffer 合同；缺页流送则先 readback/version-filter GPU requests，与 explicit/prefetch requests 合并并累计初始 priority，再递归加入 parents 并传播 priority，之后才在 `MaxSelectedPages`/pending budget 内完成 final selection 与 registered-page LRU 更新，接着执行 IO 前 slot reservation、pending/data request、CPU install staging、independent/dependent GPU transcode、cluster/hierarchy scatter、Platform Queue Submit 与 GPU completion，最终供尚未执行的后续 traversal 消费。**

这句话也正好是本章和前后篇的接口：第 02 / 06 / 09 / 10 篇已经给好了对象桥、GPUScene、深度和 GBuffer 合约；本章解释 Nanite 如何在这些既有合约之间，插入一条 GPU 驱动的几何管线；而后面的 Lumen、VSM 和调试章节，会把 Nanite 的输出当作它们自己的输入，继续往下展开。
