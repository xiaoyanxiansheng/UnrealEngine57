# 19 Virtual Shadow Maps

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `05_RenderGraph.md`、`07_MeshDrawCommand.md`、`09_DepthPrepass.md`、`11_Shadows.md`、`12_Lighting.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `19_VirtualShadowMaps_CoverageMatrix.md`

Virtual Shadow Maps（VSM）先为需要高质量动态阴影的光源建立**可分页的虚拟阴影地址空间**，再由本帧接收者需求决定哪些虚拟页需要物理映射与 caster depth。方向光把这个地址空间组织成相机相关的 clipmap，局部光使用 full VSM 或 single-page VSM；shader 因而可以按高分辨率坐标寻址，而 GPU 只分配、渲染和采样当前真正需要的页。

这套机制把**可寻址分辨率**与**已分配物理存储**拆开。本篇沿着一条正向数据流回答它如何成立：**light / view 提出需求 → VSM id 与 clipmap / 虚拟地址定位 → 接收者标页 → 缓存复用与失效判定 → 物理页分配和页表映射 → Nanite / non-Nanite 写入 → projection / sampling → Lighting 或 MegaLights 消费，并在帧尾保留下一帧可复用的证据。**

第 11 篇已经把 VSM 放进一帧阴影阶段的时序里：它在 BasePass 之后标页、在 shadow depth 阶段分配并渲染物理页、最后被 lighting 采样。本篇不再重走那条一帧主线，而是钻进 VSM 自己的内部，把"虚拟承诺 → 物理兑现"这条数据流拆开。

## VSM 先把寻址分辨率与物理存储解耦

这个设计针对的是固定 shadow map atlas（存深度的图集纹理）的**分辨率分配**约束，而不是滤波问题：

- 一盏灯如果用固定大小的 atlas，近处要么浪费分辨率、要么不够细；远处和大范围又总是不够用。
- Directional light（方向光，模拟太阳）的 CSM（级联阴影）能把相机视锥切开缓解这件事，但 cascade 数量有限，切线位置、远近精度和动态物体更新会互相牵制。
- Local light（点光 / 聚光等局部光源）如果每盏直接分一张大贴图，显存和 draw call 成本会很快失控——场景里几百盏小灯时尤其致命。

这三种困境的共同根源是同一句话：**在固定 atlas 模型里，纹理分辨率和物理存储是一一绑定的。** 你想要 16k 的清晰度，就必须真的分配并渲染一张 16k 的纹理，不管这张纹理里有多少像素当前帧根本不会被任何人看到。

UE 的 VSM 把问题改写成另一句话：

> **每盏需要高质量阴影的灯，都可以看起来拥有一张很大的虚拟阴影图；但 GPU 只给当前帧真正会被采样的虚拟页分配物理页，并只渲染这些页。**

"虚拟"两个字是关键：分辨率是一个**寻址承诺**，不是一块**已分配的存储**。

### Unity 桥：最近的类比是 Virtual Texture，但反馈来源不同

如果你来自 Unity，这条思路最近的桥是 Virtual Texture（虚拟纹理）：寻址空间很大，物理驻留的内容只是被请求的那一小块。但要小心，两者的**反馈来源**不一样：

- 材质虚拟纹理常见路径是"屏幕采样产生 feedback，再**异步、跨帧**流送纹理页"——这一帧请求，可能下一帧甚至几帧后才驻留。
- VSM 的反馈是"屏幕像素、体积样本、hair / water / MegaLights 样本**直接在 GPU 上标记本帧需要哪些阴影页**"，然后**同一帧内**把这些页分配、清理、渲染完，再交给 lighting 采样。

所以 VSM 没有跨帧的流送延迟——demand（需求）和兑现发生在一帧之内。记住这一点，后面整条主线（标页 → 分配 → 渲染 → 采样 → 缓存）才说得通。

### 先过运行资格门：虚拟承诺不是所有平台都会建立

进入 16k 虚拟地址、page table 和 physical pool 之前，Renderer 先判断当前 shader platform 与项目是否真的能运行 VSM。UE 5.7 的运行门可以读成一条正向状态链：

```text
平台支持 Nanite 所需能力
  -> 项目 / device profile 允许 Nanite，并满足 VSM 所需原子能力
  -> r.Shadow.Virtual.Enable 请求 VSM
  -> UseVirtualShadowMaps = true
  -> 当前帧有需要 VSM 的 lights / views
  -> 建立本帧 current id、request / page-table RDG references 与相关 passes
```

`DoesPlatformSupportVirtualShadowMaps` 以平台 Nanite 支持为基础；运行时 `UseVirtualShadowMaps` 还要求 `r.Shadow.Virtual.Enable` 与 `DoesRuntimeSupportNanite(..., check atomics=true, check project setting=true)` 同时成立。该项目开关的源码默认值为 0，实际工程可由项目设置写入配置。把这层门独立出来，是因为 VSM 的 GPU 页表、原子更新与 Nanite 平台能力属于资源主线的前置条件，不是 allocation 阶段可以补救的细节。

这里建立的是**本帧 references 与工作**，不是决定跨帧 physical pool 是否存在。持久 pool 和 cached pages 由 cache manager 的存储生命周期决定；它们可以已经存在，只是当前帧尚未为这些 lights / views 发布可消费的 request、page-table references 与 passes。

资格门关闭时，最后有效状态就在项目 / 平台选择：后续本帧 VSM id、page request、page table references 与相关 passes 不应建立，但 cache manager 仍可能持有此前创建的 persistent pool。此时项目应使用当前平台可用的传统 shadow map、CSM 或烘焙方案。阴影规模小、要求更简单且确定的资源预算时，这些替代方案也可能比引入虚拟页管理更合适。

## 贯穿案例：一个黄昏室外场景

为了不停留在函数名层面，全章跟着同一个室外黄昏场景走：

> **贯穿案例**：一盏 **Directional Light（太阳）** 照亮整片地形与远山；场景里一个 **正在奔跑的 Nanite 角色**，脚下需要清晰的近距离阴影；脚下地面是 **普通 non-Nanite mesh**；远处一盏 **聚光灯（spot light）** 挂在塔上，离相机很远、屏幕占比很小。BasePass 已写好 GBuffer，Lighting 即将开始。问题始终是同一个：当 lighting 要点亮“角色脚下那个像素”时，它如何拿到“这个像素被太阳挡住了吗”的答案，而 GPU 又凭什么没有为整片地形渲染一张 16k 的太阳阴影图。

这三个对象会落到三种不同的 VSM 形态和生命周期上，正好覆盖本篇要讲的全部分支：

| 贯穿对象 | VSM 形态 | 页的生命周期 | 在哪节展开 |
| --- | --- | --- | --- |
| 太阳 | clipmap 多层 | 相机微动时平移复用 | 第 3 / 8 节 |
| 奔跑的角色 | 它在太阳 clipmap 里占的页 | 角色变化通过 primitive invalidation 使受影响的 cached pages 失效；directional coarse demand 由 clipmap coarse-level marking 决定 | 第 8 节 |
| 普通地面 | non-Nanite caster 写入被分配页 | 受 non-Nanite VSM 资格与 culling 控制 | 第 6 / 9 节 |
| 远处塔上聚光灯 | single-page VSM | 远小光源，几乎不重画 | 第 2 / 3 节 |

同一个场景，三种页生命周期并存。下面这张精简路线图就是我们要走的路（细节后面逐段展开）：

```text
light / view request       角色脚下的可见接收像素需要太阳阴影
  -> VSM address          当前帧 VSM id 与 directional clipmap 把它定位到虚拟页
  -> receiver marking    接收者证据把该页写成 page request
  -> cache validation    检查上一帧物理页能否复用；失效页进入重建路径
  -> allocation/mapping  复用或分配 physical page，并发布 page table 映射
  -> shadow rendering    Nanite / non-Nanite caster 把深度写进被分配页
  -> projection/sampling 通过 page table 把虚拟 UV 翻译成物理 texel 并采样
  -> consumers           Deferred Lighting 或 MegaLights 的 VSM 路径消费阴影结果
  -> frame-end extraction 保留 pool、metadata 与 previous-frame evidence 供下一帧复用
```

这条路线里，`FVirtualShadowMapArray` 持有本帧 id、request、page-table RDG references 并组织 passes，`FVirtualShadowMapArrayCacheManager` 持有跨帧 physical pool、metadata、cache identity 与 previous-frame evidence；GPU passes 写入本帧映射和深度，Lighting / MegaLights 只消费已发布的本帧结果。帧尾 extraction 延长的是可复用底层状态，不是把本帧 RDG reference 直接变成跨帧对象。

## 本篇边界

本篇核心问题来自 `OUTLINE.md`：**虚拟页表如何解决阴影分辨率问题？** 围绕这条主线，相邻系统只讲"它在 VSM 这条线上扮演什么角色"，深入归属如下。

| 问题 | 本篇讲到 | 延后到 |
| --- | --- | --- |
| 虚拟地址空间 / 页表 / 物理页池 / page flags 是什么 | 完整展开：常量、page table 条目编码、物理池所有权 | — |
| Directional clipmap 与 local light VSM 如何变成 VSM id | clipmap 层级与稳定性、local full / single-page、id 分配顺序 | — |
| Page marking 如何把屏幕 / froxel / hair / water / MegaLights 样本变成 page request | 完整展开 demand 生成 | MegaLights 自身采样算法见第 18 篇 |
| Page allocation 如何复用缓存、分配新物理页、生成层级 flags | 完整展开 allocation 管线 | — |
| Nanite 与 non-Nanite 如何只渲染被请求的物理页 | 两条路径的输出目标与 culling | Nanite cluster culling / raster 完整算法见第 16 篇 |
| Lighting 如何从虚拟地址翻译到物理页、缺页时降级 | 完整展开采样链 | Deferred BRDF / clustered shading 见第 12 篇 |
| Cache manager 如何处理跨帧复用与失效 | 完整展开缓存条件 | — |
| 调试如何沿数据流逐层定位 | 完整展开五层排查路径 | — |
| Shader 参数系统与 uniform buffer 绑定机制 | 只用其结果 | 绑定机制见第 21 篇 |

## 本篇必须能回答

读完本篇，你应该能回答：

- 一张"16k 虚拟阴影图"到底是什么意思？它和"分配一张 16k 物理纹理"差在哪里？
- page table 一个条目里到底存了什么，使得后续渲染和采样都不必知道物理纹理的真实排布？
- 太阳的 clipmap 怎样用重叠的 nested levels 覆盖世界？“环”为什么只能类比各层主要贡献的覆盖带，而不是物理存储形态？
- "只渲染需要的页"这件事，是在哪一步、由谁决定的？page marking、allocation、render 三步各自负责什么？
- lighting 采样时，什么条件下能退到更粗层，完全没有任何 valid mapping 时又会发生什么？
- 角色奔跑时它脚下的旧阴影为什么会失效、而身后的地形阴影能复用？谁来保证不留 stale shadow（残影）？
- 阴影出问题时，为什么应该先倒查 page demand 而不是先调 bias？

## 1. VSM 把"高分辨率阴影"拆成虚拟地址和物理存储

回到上面那句根本困境：固定 shadow map 把**分辨率**和**存储**焊死在一起。VSM 做的第一件事就是把这两者切开——shader 侧仍然以一张很大的虚拟阴影图来寻址，但这张虚拟图被切成等大的"页"，**只有被请求的页才映射到一个物理页**。

要把这件事讲具体，先要回答两个量化问题：虚拟空间能寻址多大？一页又有多大？一组 shader / C++ 共享的编译期常量共同划定了虚拟坐标系的“格子”：

| 常量 | 值 | 含义 |
| --- | --- | --- |
| `VSM_LOG2_PAGE_SIZE` | 7 | 每页 `1<<7 = 128 × 128` texel |
| `VSM_LOG2_LEVEL0_DIM_PAGES_XY` | 7 | level 0 有 `128 × 128` 页 |
| `VSM_VIRTUAL_MAX_RESOLUTION_XY` | 16384 | `128 页 × 128 texel`，即虚拟坐标系上限 |
| `VSM_MAX_MIP_LEVELS` | 8 | `7 + 1` 层 mip |
| `VSM_MAX_SINGLE_PAGE_SHADOW_MAPS` | 8192 | 远 / 小光源单页槽位数 |

读这张表只需抓住一个反直觉的点：`16384` 不是"每盏灯分配一张 16k 物理纹理"，而是"page table（页表）能**表达**一个 16k 的虚拟坐标系"。回到贯穿案例——我们那盏太阳**确实拥有**一个 16k 的虚拟寻址范围，但它当前帧真正占用的物理深度 texel，只有角色脚下、远山轮廓等少数被请求的页。真正的深度存在 `PhysicalPagePool`（物理页池）里，它是一个 `Texture2DArray<uint>`（无符号整数的二维纹理数组，存的是打包后的深度），由跨帧的缓存管理器持有，每帧导入 RDG（RenderGraph，UE 的帧级资源/依赖图，见第 05 篇）。

### 两个所有权不同的对象：本帧的组织者 vs 跨帧的持有者

这里就出现了一个对理解 VSM 至关重要的分工。VSM 既要复用上一帧渲染过的静态阴影（跨帧），又要遵守 RDG handle / reference 在本帧图内有效的生命周期模型。UE 的解法是把**资源引用生命周期**与**底层存储生命周期**拆给两个 owner：

| 对象 | 生命周期 | 它持有什么 | 一句话职责 |
| --- | --- | --- | --- |
| `FVirtualShadowMapArray` | 当前 render / RDG graph | `PageRequestFlagsRDG`、`PageTableRDG`、`PageFlagsRDG`、`PhysicalPageListsRDG` 以及导入后的 pool 引用 | 本帧组织 pass、依赖、barrier 与 shader bindings；它持有的是当前图可用的 references |
| `FVirtualShadowMapArrayCacheManager` | scene 跨帧 | external physical pool、physical metadata、可复用的 previous-frame evidence 与 per-light cache identity | 管底层 allocation / texture 存活、缓存身份、导入与 extraction 决策 |

为什么必须这样切？如果完全不保留底层存储，静态页每帧都要重新分配和重画；如果把本帧 RDG reference 当成跨帧对象直接保存，资源依赖、barrier 与有效期会变得不可信。分离后，cache manager 持有可跨帧的 allocation / texture 与 metadata，本帧 array 把它们注册成当前图 references，再组织 GPU pass 使用。帧末 extraction 只是为后续帧保留资源并发布 previous-frame evidence，不等于 CPU 同步完成，也不等于 GPU 已空闲。

把生命周期画成两条线更容易分辨：

```text
reference lifetime:
  external storage -> RegisterExternalTexture/Buffer -> 本帧 RDG refs -> QueueExtraction

storage / cache lifetime:
  cache identity + physical pool + metadata
  -> 可跨帧保留
  -> pool resize、配置变化或 bAllowPersistentData=false 时释放/全失效
```

`ExtractFrameData(bAllowPersistentData=false)` 会丢 previous buffers，并释放 physical / HZB pool；本帧没有 allocated shadow data 时，也不能声称完成了整套 extraction。反过来，某个持久 pool 仍存在而本帧 page table 尚未建立时，也不能说“现在一个物理页都没有”：存储存在不等于当前帧 virtual mapping 已发布。

后续所有资源都挂在这两个对象上。而把这层数据模型暴露给 shader 的接口是 `FVirtualShadowMapUniformParameters`；它把后续每一节需要的资源入口绑定给消费者：

| 字段 | 作用 |
| --- | --- |
| `PageTable` | 虚拟页 → 物理页地址的映射（第 2 节展开） |
| `PageFlags` | 页状态的层级结构，告诉渲染 / culling 哪些页被请求、哪些 mip / clipmap 层有数据 |
| `PageReceiverMasks` | 可选的接收者 mask，把"这一页里哪些 8×8 子块真需要阴影"再压细 |
| `PhysicalPagePool` | 真正存放阴影深度的物理页池 |
| `ProjectionData` | 每个 VSM id 的光源投影、clipmap level、bias、flags、light id |
| `PerViewData` | 每个 view 裁剪后的 VSM light grid 和 directional VSM id 列表 |

读到这里只需建立一个心智模型：**一个很大的虚拟坐标系 + 一个很小的物理页池 + 一张把前者翻译成后者的页表。** 下一节就把这张页表的"最小合约"拆开看。

### 先把"页"这个词放稳

本章后面会反复说 virtual page、physical page、page table、page flags、cache page。它们不是同一个东西的不同叫法，而是同一条状态线上的不同位置：

| 名字 | 它是什么 | 谁推进它 | 调试时回答什么问题 |
| --- | --- | --- | --- |
| virtual page | 16k 虚拟阴影图中的一个地址格子 | page marking 根据接收者需求选中 | "这个像素会不会需要这一块阴影？" |
| physical page | 物理页池里的一个真实 128×128 存储块 | allocation 从 available / cached 列表中分配或复用 | "这块需求有没有真实存储可写？" |
| page table entry | virtual page 到 physical page 的 32-bit 翻译合约 | allocation 写入，lighting 和 shadow rendering 读取 | "当前虚拟地址该去哪个物理 texel？缺细节时往哪层退？" |
| page flags / rect bounds | 已分配页的层级状态和矩形范围 | allocation 生成，culling / 渲染路径消费 | "哪些 mip / clipmap 层有有效页，哪些区域需要提交 caster？" |
| cached page metadata | 上一帧保留下来的物理页身份和失效状态 | cache manager 跨帧持有，下一帧 `UpdatePhysicalPages` 消费 | "这页能不能复用，还是必须重画？" |

把贯穿案例里的"角色脚下像素"放进去看：BasePass 后它只对应一个 **virtual page request**，还没有物理深度；allocation 之后，它对应 page table 中一个 valid entry，并拿到某个 physical page；Nanite / non-Nanite 渲染之后，这个 physical page 才真正有 caster depth；lighting 采样时再从虚拟 UV 查到这个物理 texel。任何一步断掉，症状都不同：没 request 是缺页，没 allocation 是空映射，没渲染是空深度，lighting 查错是投影或 id 错，cache 没失效则是残影。

## 2. 页表条目是“虚拟页到物理页”的最小合约

承上：既然渲染和采样都不直接碰物理纹理，那它们靠什么找到深度？答案是 page table。它不是一个高层 C++ map，而是一张 **shader 直接读写的纹理**——每个条目就是一个 32-bit 整数。理解“细页缺失时何时能退粗页、何时只能返回 invalid”，关键全在这 32 个 bit 怎么分。条目的位布局如下：

```text
page table entry (32 bit):
  [0:9]   PhysicalAddress.x        (page table 解出的物理页坐标，低 10 位)
  [10:19] PhysicalAddress.y
  [20:25] LODOffset                (当前层没页时，往粗几层去取)
  [26:29] 当前未用
  [30]    bThisLODValidForRendering
  [31]    bAnyLODValid
```

这几个 bit 解释的是**有更粗 valid mapping 时的条件式 fallback**：

- `bThisLODValidForRendering`（bit 30）：这个虚拟页在**本层级**有物理页，可以被 shadow rendering 写入。
- `bAnyLODValid`（bit 31）：这个虚拟位置至少在**某个层级**可用。
- `LODOffset`（bit 20:25）：如果当前层没有页，往更粗的 mip / 更远的 clipmap level 跳几层就能取到。`LODOffset == 0` 就表示"正好就是本层"，源码用 `bThisLODValid = bAnyLODValid && LODOffset == 0` 表达这个等价关系。

换句话说，“缺细节页 -> 退到粗层”的能力是**编码进 page table 条目本身**的，不是采样时临时猜出来的；但它要求 `bAnyLODValid=true`。若当前 virtual position 在所有层都没有 mapping，translation result 的 `bValid=false`，后续 projection / filtering 决定视觉结果，VSM 本身不保证仍有阴影。

翻译动作本身很直接。把虚拟 texel 地址右移 `VSM_LOG2_PAGE_SIZE`（即 7）得到虚拟页坐标，查表得到物理页地址，再把页内 offset 加回去（对应 `VirtualToPhysicalTexelBase`）：

```text
VirtualTexelAddress
  -> VPageX/Y       = VirtualTexelAddress >> 7
  -> entry          = PageTable[ VSM id + level + (VPageX,VPageY) ]
  -> PhysicalTexel  = entry.PhysicalAddress * 128 + (VirtualTexelAddress & 127)
```

这就是 VSM 的核心合约：后续渲染和采样**都不需要知道"这张阴影图到底是不是 16k 物理纹理"**，它们只要遵守 page table 翻译。写入方在渲染时确认当前页能写（看 bit 30）；采样方找到最佳可用页（必要时跟着 `LODOffset` 退层）。

这也是本章最重要的 worked case。假设角色脚底的太阳阴影细节页没有映射，但同一片地面在更粗的 clipmap 层有 valid mapping：allocation 会把当前细页的 entry 写成“本层不能直接渲染，但某个更粗层有效”，也就是 `bAnyLODValid = true`、`bThisLODValidForRendering = false`、`LODOffset > 0`。lighting 顺着 `LODOffset` 去粗层读深度。若粗层也没有 mapping，则 `bAnyLODValid=false`，这里没有“自动再退一层”的保证。调试时要区分：`bThisLODValidForRendering` 回答本层能否被 caster 写入；`bAnyLODValid + LODOffset` 回答 lighting 是否还有可采样 fallback；两者都无效时，最后有效状态应回到 request / allocation，而不是继续调 filter。

### single-page：把海量小光源塞进同一个地址体系

合约还要回答贯穿案例里那盏远处聚光灯：它屏幕占比那么小，难道也要占一整个 16k 的 page table 区域吗？不需要。UE 把 single-page shadow map 塞进同一个地址体系——single-page VSM 直接落到 page table 的第 0 个 page 区域（源码以"level offset 永远落在 0th page"判定 single-page）；full VSM 则跳过这块 reserved range，从 `VSM_MAX_SINGLE_PAGE_SHADOW_MAPS`（8192）之后开始排布。

这里容易误解成"所有 single-page 小灯共享同一个物理页"。不是。single-page 的意思是：这盏灯在页表的 reserved 区域里只需要一个 page table 入口，且它的页内坐标按自己的 VSM id 落到那块入口；后续 allocation 仍然会给被请求的 single-page 分配或复用具体 physical page。也就是说，single-page 省掉的是完整 16k page table 区域和多 mip / 多页管理成本，不是把所有小灯塞进同一张深度页。

于是塔上那盏很远很小的聚光灯只占一个 single-page 入口，根本不需要完整的 clipmap / full 区域。这正是 VSM 处理"海量远处小光源"的关键。下一节就讲：一盏灯到底凭什么被分到 clipmap、full 还是 single-page。

## 3. Directional light 用 clipmap，local light 用 full / single-page VSM

虚拟页表回答了"如何表达一张大阴影图"，但还有一个问题没回答：**每盏灯应该被表达成哪些 VSM id？** Directional 和 local 的空间问题不同——太阳要覆盖整个世界的远近，局部光只照亮一小片——所以 UE 分两套入口处理。

### 太阳：clipmap 是重叠的世界空间 nested levels

Directional light 的 VSM 是 clipmap。UE 为每个主 view 创建一个 `FVirtualShadowMapClipmap`，再创建一个 legacy `FProjectedShadowInfo`（传统投影阴影信息结构）作为接入点。注意这里的 "legacy" 不是说"用旧算法渲染 VSM"，而是 UE 复用 `FProjectedShadowInfo` 这条**已经能接入 shadow mesh pass、Nanite view 和 non-Nanite culling 的管线**，避免为 VSM 另造一套接入。

clipmap 的配置来自一组 `r.Shadow.Virtual.Clipmap.*` CVar。源码默认的 `FirstLevel` 是 **6**，`LastLevel` 是 **22**，每层半径由 `GetLevelRadius(AbsoluteLevel) = pow(2, AbsoluteLevel + 1)` 得到，即 `2^(Level+1)`。

理解 clipmap 的关键在于它和 CSM 的区别。CSM 是把**相机视锥**切成几段 cascade；VSM clipmap 则以 view origin 为中心建立多层**彼此重叠、逐级扩大覆盖范围的 nested levels**：

```text
                 远山（覆盖大、分辨率低）
        ┌─────────────────────────────┐  level 大
        │      ┌───────────────┐       │
        │      │   ┌───────┐   │       │
        │      │   │ 相机  │   │       │  level 小（角色脚下，分辨率高）
        │      │   └───────┘   │       │
        │      └───────────────┘       │
        └─────────────────────────────┘
   每往外一层，覆盖半径翻一倍（2^(Level+1)）
```

近处 level 小、分辨率高（对应角色脚下）；远处 level 大、覆盖范围大（对应远山）。每多一个 level，最大覆盖半径翻一倍。“环”可以用来类比某层相对相邻层主要贡献的有效覆盖带，却不能理解成物理上只存一个环形纹理；每一层仍是重叠的虚拟地址层级，并通过 page table 按需映射。

那相机走动时太阳阴影为什么不抖？clipmap 的稳定性来自两个动作：

1. **snapping（吸附）**：在 light view 空间中，按 level radius 把 view center 吸附到网格上。相机的微小移动不会让 shadow projection 每帧连续漂移——它只在跨过一个网格格子时跳一下，而不是连续滑动。
2. **平移复用**：`UpdateClipmapLevel` 比较新的 page-space location、Z guardband（深度护栏带）、WPO disable threshold（世界位置偏移关闭阈值）。如果缓存仍然覆盖新范围，就写入 `NextData.PageAddressOffset`，表示"上一帧物理页可以平移复用"；否则清掉 valid 标志，让该层重建。

### 局部光：full 还是 single-page

Local light 的 VSM 由 `AddLocalLightShadow` 建立。它先根据 view、光源半径、投影尺度和 resolution bias 估算一个保守的 `MinMipLevel`（最小 mip 层级），再判断这盏灯能否作为 distant / single-page light——塔上那盏远处聚光灯就在这里被判为 single-page。Spot light 通常是一张图；point light（点光）的 one-pass shadow 会有 6 个 cube face。缓存是否失效由 `UpdateLocal` 用 light projection key、origin、radius、distant 状态、receiver mask 状态判断。

这里再把地址差异说实一点。Directional clipmap 的页地址是"一盏太阳灯的某个 clipmap level + 该 level 内的 page address"；local light 的页地址是"某个 local VSM id + mip level + 页地址"，point light 还要先选 cube face，相当于同一盏点光拥有 6 个相邻的 VSM entry。它们最后都查同一张 page table，但页表偏移的来源不同：clipmap 用 view-centered level 序列，local 用单灯投影和 mip。调试时看到 page request 写错区域，要先判断这是 clipmap level 选错，还是 local face / mip / id 选错，不能只说"VSM 页表错了"。

### VSM id 是本帧地址索引，不是跨帧灯身份

三类灯的 VSM id 在每帧按当前 directional、local 与仍保留缓存的 unreferenced 集合重新分配，顺序固定为：

```text
directional 先  →  local 后  →  本帧未引用但仍保留缓存的 light 最后
```

这个顺序不是审美选择，它服务于 receiver mask 分配并让当前活跃集合保持紧凑。更重要的是：**数值 VSM id 只对当前 `FVirtualShadowMapArray` / 当前 render 有意义。** 跨帧保持的是 per-light cache entry 的 identity、physical metadata 与 previous/current id 关系，不是某个固定整数 id。

cache entry 每次拿到新 id 时，会把旧值保存为 previous id，再记录 current id。随后 GPU remap 读取上一帧 physical metadata 和 `NextVirtualShadowMapData`：用 previous id 找到对应 cache identity，把旧 page address 加上 clipmap 的 `PageAddressOffset`，验证地址仍合法后，将 metadata 改写为本帧 current id / address。local light 的 offset 通常为 0；clipmap 可用非零 offset 平移复用。

```text
frame N:
  塔灯 cache identity = TowerSpot
  current VSM id = 27

frame N+1:
  当前活跃灯集合重新排序
  TowerSpot current VSM id = 31，previous id = 27
  GPU remap: (id 27, old page address) -> (id 31, current page address)
  physical page 可继续复用
```

每帧重分配 id 的收益是当前数组只需表达本帧相关集合，并可把 unreferenced cache entries 放到尾部；remap 则保住昂贵的 physical pages。若把数值 id 当持久身份，frame N+1 的 27 可能属于另一盏灯或根本无效，结果会是整灯阴影错位。此时最后有效状态是 physical depth 本身正确，下一检查点应是 previous/current id、cache key 与 address remap，不应先调 bias。

如果这里顺序或 remap 数据错了，后续 page marking 会把请求写到错误的本帧 VSM id 上，或缓存页被挂到错误 identity。lighting 最终查到另一盏灯或空页，表现为整片阴影错位或跨帧跳变。

## 4. Page marking 把"这个像素要阴影"写成 page request

到这里，本帧的虚拟空间、projection data 与 id 已建立，但**本帧 demand 到 mapping 的关系尚未形成**。跨帧 physical pool 与 cached pages 可能已经存在；缺少的是当前接收者对当前 virtual addresses 的 page requests，以及随后发布给本帧消费者的 page table。VSM 的核心成本控制就在这里：它不先画所有 shadow pages，而是在渲染前根据接收者标记“本帧要哪些页”。

`BeginMarkPages` 先做两件准备，再标页：

1. 把每个 VSM id 的投影数据上传，并初始化一个 per-page 的 dispatcher。这个 dispatcher 让后续 compute pass 能按"每个 VSM / 每个 page table 区域"调度，而不是把所有 VSM 粗暴塞进一个巨大 dispatch。
2. 创建并清理本帧的关键 request 资源：

| 资源 | 用途 |
| --- | --- |
| `PageRequestFlagsRDG` | page marking 写入的"本帧需要这些虚拟页" |
| `PageReceiverMasksRDG` | 可选的接收者 mask |
| `PageTableRDG` | allocation 之后填入虚拟→物理映射 |
| `PageFlagsRDG` | allocation 之后生成的层级页状态 |
| `DirtyPageFlagsRDG` | 渲染后用于 HZB / dirty tracking |
| `PhysicalPageListsRDG` | LRU / available / empty / requested 四组物理页列表 |
| `UncachedPageRectBoundsRDG` / `AllocatedPageRectBoundsRDG` | 每个 VSM / mip 层的请求与已分配页矩形 |

这一步的所有权也要说清：page marking 只拥有**需求生成权**，不拥有物理存储，也不负责判断 caster 是否真的会写深度。它的输入是本帧已经稳定的接收者证据：screen pixel、froxel、hair、water、front layer 或 MegaLights selected samples。它的输出是 `PageRequestFlagsRDG` 和可选 receiver mask。换句话说，marking 回答"谁会问阴影"，allocation 才回答"给谁物理页"，render 才回答"哪些 caster 写进去"。如果把这三句混成一个 pass，调试时就会在错误层里找问题。

接着 `BeginMarkPages` 对每个 view 裁剪 light grid，只保留带 VSM 的 local lights，并单独记录 directional VSM id。这里有一个 MegaLights 的交接细节：它会根据 MegaLights 是否在自己标 VSM 页，决定是否把 MegaLights 管理的灯从常规 marking light grid 中排除。原因很直接——MegaLights 如果自己按 sample 标记 VSM 页，常规 light grid 再标一次会扩大 page demand，破坏"只服务被抽中样本"的成本模型（MegaLights 采样算法见第 18 篇）。

真正的页标记有两类，**顺序有硬约束**。

**第一类是 coarse pages（粗页），它总是先跑，而且不能和后续 pass overlap。** 当 coarse request 不包含 non-Nanite geometry 时，后面的 pixel pages 需要能覆盖相应写入；这里选择避免原子写，因此必须靠顺序保证覆盖关系。coarse pages 为远处、体积、大气等路径准备全域低频阴影证据。它与 detail request、receiver mask、dynamic invalidation 是不同粒度的数据：coarse/detail 决定页需求语义，receiver mask 再细化页内接收区域，invalidation 则描述上一帧缓存证据能否复用。

Directional coarse demand 由 clipmap 自身配置的 first / last coarse level 与对应 coarse-level 标记决定。下面两个控制讨论的是 **local VSM** 的 coarse 行为，不能拿来解释太阳的 directional clipmap：

| 控制 | 默认 | 作用与代价 |
| --- | --- | --- |
| `r.Shadow.Virtual.MarkCoarsePagesLocal` | 2 | 0 关闭；1 始终标最后 mip；2 仍标 coarse pages，但抑制非-detail coarse page 因 moving geometry、WPO、animation 产生的动态失效 |
| `r.Shadow.Virtual.NonNanite.IncludeInCoarsePages` | 1 | 允许普通 mesh 写入 coarse pages；关闭可显著降低大 coarse page 的 non-Nanite 成本，但相应普通 mesh 阴影贡献会缺失 |

对塔上聚光灯这类 local VSM，mode 2 是明确的性能选择：它保留 local 低频覆盖，但允许 local non-detail coarse shadow 比 detail shadow 更旧。需要 local 体积 / 远距动态阴影准确时，应接受更多 invalidation 与重画成本；普通 mesh coarse raster 过贵且项目能接受远距缺失时，才适合关闭 non-Nanite include。

**第二类是像素 / froxel / hair / water / front-layer 的精细请求。** 核心动作是把世界位置翻译成一个虚拟页地址，再在 page request 里标记它：

```text
TranslatedWorldPosition + light projection
  -> shadow UV / clipmap level / local mip
  -> virtual texel address
  -> virtual page address
  -> OutPageRequestFlags[page table offset] = VSM_FLAG_ALLOCATED | VSM_FLAG_DETAIL_GEOMETRY
  -> (可选) 更新 OutPageReceiverMasks
```

两类灯的选层方式不同：Directional light 走 `MarkPageDirectional`，用 `CalcAbsoluteClipmapLevel` 加上 per-light resolution bias 和 global bias 选 clipmap level，再在该 level 的 page table 里标记。角色脚下那个像素就是在这里，被算出落在太阳 clipmap 的某个近层、某个虚拟页，然后写下一条 request。Local light 走 `MarkPageLocal`：point / rect light 先选 cube face，再按像素 footprint 选 mip。Receiver mask 开启时，shader 不只标"整页需要"，还会把页内 8×8 receiver mask 的子块写进 `OutPageReceiverMasks`，进一步缩小后续要渲染的范围。

这一步回答了分辨率问题的一半：**分辨率不是按整盏灯预先决定，而是按“当前帧哪些接收者会采样这盏灯的哪个虚拟位置”决定。** 没有 page marking，当前 16k 虚拟地址承诺就没有本帧 demand；这不代表持久 pool 不存在。错误的 page marking 会让 allocation 后面分错页，表现为“该有阴影的地方没有当前 mapping”，或“物理页浪费在不可见区域”。

回到角色脚下像素，这一步的状态变化只有这么多：

```text
GBuffer 中的世界位置 + 太阳 clipmap projection
  -> 选中一个 absolute clipmap level
  -> 算出 virtual texel / virtual page
  -> 在 PageRequestFlags 写 VSM_FLAG_ALLOCATED | VSM_FLAG_DETAIL_GEOMETRY
  -> 如果 receiver mask 开启，再写页内 8×8 子块
```

注意最后一行：receiver mask 不改变“有没有这页”这个大问题，它只把“页里哪些接收者真的需要阴影”进一步压细。它服务的是后续渲染和 culling 的工作量，不是替代 page table 映射。

回到太阳案例，角色脚下的 detail request 由 directional 选层写入，远处 volume 的低频 demand 则看太阳 clipmap 的 first / last coarse level 是否覆盖对应范围。角色变化后的旧影要沿 primitive invalidation 与 clipmap cache 复用条件排查；若普通地面只在远处 coarse 结果中缺影，还要检查 directional coarse-level marking 与 non-Nanite include，而不是先查 detail pixel marking。

## 5. Page allocation 把请求页映射到可写物理页

Page marking 只说明"需要哪些虚拟页"；此时当前 request 尚无已确认的 valid virtual-to-physical mapping。persistent pool 与 cached physical pages 可能早已存在，allocation 要做的是复用或分配其中的页，把本帧 demand 兑现成可写映射：`BuildPageAllocations` 把 `PageRequestFlagsRDG` 转换成可渲染的 `PageTableRDG` 和 `PageFlagsRDG`，发生在 shadow depth 阶段。

这条 allocation 管线由一串 compute pass 组成，**顺序固定**（与源码中的 `RDG_EVENT_NAME` 一一对应，可作为调试锚点）：

```text
UpdatePhysicalPages           复用上一帧 metadata，传播失效
  -> PackAvailablePages       整理可分配列表
  -> AppendPhysicalPageList   把 empty 页并入 available
  -> AllocateNewPageMappings  为新请求从 available 取物理页，写 PageTable
  -> GenerateHierarchicalPageFlags  生成层级 flags 与 page rect bounds
  -> PropagateMappedMips      用更粗 valid mapping 回填更细未映射 entry
  -> SelectPagesToInitialize  挑出需要清的页
  -> InitializePhysicalMemoryIndirect  只清这些页
  -> 更新 uniform 的 PageTable / PageFlags / ReceiverMasks / RectBounds
```

理解这条管线的关键，是先看清它操作的对象：物理页被分成四个列表，整条 allocation 全程就是在这四个列表之间**搬运物理页**。

| 列表 | 含义 |
| --- | --- |
| `PHYSICAL_PAGE_LIST_LRU` | 上一帧的 LRU 顺序输入 |
| `PHYSICAL_PAGE_LIST_AVAILABLE` | 本帧可分配的物理页 |
| `PHYSICAL_PAGE_LIST_EMPTY` | 本帧变空或失效的临时页 |
| `PHYSICAL_PAGE_LIST_REQUESTED` | 本帧被请求或继续使用的页 |

把 allocation 当成状态机看会更稳：

| 输入状态 | allocation 做什么 | 输出状态 | 失败时的画面/调试信号 |
| --- | --- | --- | --- |
| request flag 有位，但上一帧没有可复用页 | 从 available / empty 里拿 physical page，写 page table | 本页 `bThisLODValidForRendering`，需要被清理后渲染 | page pool overflow 或 page table invalid，表现为缺阴影块 |
| request flag 有位，上一帧 physical page 仍有效 | 复用 metadata，更新 requested / LRU 状态 | page table 指向旧 physical page，可继续使用或重画 | cache key / invalidation 错会出现 stale shadow |
| 细节页没映射，但粗层有映射 | 把粗层 physical address 与 `LODOffset` 回填到细层 entry | lighting 可 O(1) fallback 到粗页 | 没有 `PropagateMappedMips` 会出现硬缺口或闪烁 |
| shader 判定 physical page 的 static / dynamic slice 需要初始化或重建 | 加入 initialize list，按 slice 清理 | physical page 可安全写 depth | 漏清会读旧深度；过度清理会破坏 cache 收益 |

这一表也说明了谁在推进所有权：本帧 `FVirtualShadowMapArray` 组织 RDG pass 和 transient list；跨帧 cache manager 提供上一帧 page table / metadata；GPU compute pass 真正写 page table、page flags 和 physical page lists。CPU 侧不会逐页手工填 C++ map，它只搭好 pass 和资源合约。

下面按"复用 → 分配 → 稳定性 → 清理"四个动机把上面的 pass 串起来。

**复用（第一步）。** `UpdatePhysicalPages` 是复用缓存的入口。如果上一帧 cache data 可用且 GPU mask 有效，它把上一帧的 physical page metadata 映射到当前帧的 VSM id / page address。对 clipmap，用上一节写下的 `NextData.PageAddressOffset` 把 page address 平移到新的 snapped 位置（地形阴影靠这一步复用）；对 local light，offset 通常为 0。它还把上一帧 `PageRequestFlags` 里的 invalidation flags（失效标记）传播到 physical page metadata——角色奔跑造成的失效就是在这里生效（详见第 8 节）。

**分配（核心一步）。** `PackAvailablePages` 和 `AppendPhysicalPageList` 准备好可分配列表后，`AllocateNewPageMappings` 才真正遍历本帧 request，把尚未映射的虚拟页从 available list 中 pop 出物理页，写入 `PageTable` 和 `PageFlags`，并更新 physical page metadata。如果物理页池不够，request 可以得不到服务；这不是正常 fallback，当前帧会出现 missing-shadow artifacts，并由 cache manager 报告 page-pool overflow。

**稳定性（关键一步）。** `GenerateHierarchicalPageFlags` 和 `PropagateMappedMips` 是采样稳定性的来源，也是第 2 节那个 `LODOffset` 的"幕后写手"。前者根据 metadata、page flags、receiver masks 生成层级 page flags 和 page rect bounds，让 culling 能快速知道某个 mip / clipmap 层有没有有效页。后者从当前未映射的细层 entry 出发寻找更粗的 valid mapping，再把该粗层的 physical address 与 `LODOffset` 回填到细层 entry；directional 路径编码的是跨 clipmap level 的 offset，local 路径编码的是跨 mip 的 offset。这样 lighting 查询细层时无需逐层搜索，读一个 entry 就能 O(1) 定位条件式 coarse fallback。**没有这一步，lighting 遇到当前细节页缺失时只能返回无效，阴影会闪烁或出现硬缺口。**

**初始化（最后一步）。** `SelectPagesToInitialize` 读取 physical metadata，跳过未分配、unreferenced 或 fully cached pages；对至少有一个 uncached slice 的页，按 static / dynamic initialized 与 dirty flags 决定哪些 slice 要进入 indirect clear。它选择的是**本帧 shader 判定需要初始化或重建的 physical pages**，不只是“刚拿到的新页”。这能避免清整池并保留缓存；若漏掉应清 slice，会读到旧 depth，若把所有 requested pages 都清掉，又会失去静态 cache 和带宽收益。

allocation 的完成也有深度：CPU 建好 RDG pass 只表示工作已声明；GPU 执行 allocation 并写出 valid page-table entry / metadata 后，raster 才能安全消费；只有 request flag 不能叫“已分配”。entry valid 但 `LODOffset>0` 表示当前细层不可渲染、lighting 只能条件式读粗页；entry 仍 invalid 则最后有效状态在 available list、pool capacity 或 allocation pass。

到这里，VSM 才从“需求列表”变成“可写的物理页集合”。下一步是把 caster depth 真正写进这些页；在那之前，valid mapping 也不等于已有有效阴影深度。

## 6. 渲染阶段只把 caster 写进被分配的物理页

Allocation 之后，shadow rendering 面对的不再是传统 atlas 的一块连续 viewport，而是**一组已经映射的 virtual pages**。`RenderVirtualShadowMaps` 可由 Nanite 与 non-Nanite 两条 geometry submission 路径写入同一个 `PhysicalPagePoolRDG`；共同输出使 lighting 不需要知道 caster 类型，但两条路径的资格与 culling owner 不同。

这一步不要再让 Nanite 或 mesh pass 去"决定哪些页需要阴影"。页需求已经由第 4 节的 receiver / sample marking 决定，物理页也已经由第 5 节 allocation 兑现。渲染阶段的职责是消费这些结果：拿到 compact 后的 shadow views、page table、rect bounds 和 receiver mask，把 caster 的深度写到对应 physical page。也就是说，Nanite / non-Nanite 在这里是**写入者**，不是 demand owner。

**Nanite 路径**用一个 shadows pipeline 的 depth-only raster context，输出目标就是 `PhysicalPagePoolRDG`。每个 render pass 先经过一次 view compact（把没有页需求的 VSM views 剔掉），再把几何画进物理页池。VSM HZB（层级 Z 缓冲）可用时，Nanite 会用上一轮 HZB / page table 信息做 occlusion；本帧渲染后若开启 HZB，会更新 HZB 为后续帧和 non-Nanite culling 准备层级深度。

**Non-Nanite 路径先过独立资格门。** `r.Shadow.Virtual.NonNaniteVSM` 默认 1，是 read-only/config、修改后需要重启的支持开关；运行时 `UseNonNaniteVirtualShadowMaps` 还要求总体 `UseVirtualShadowMaps` 已成立。门关闭时，普通 mesh 不会进入这条 VSM writer 路径；这时普通地面缺影的最后有效状态就在 feature qualification，不应先误诊 instance culling。

资格成立后，non-Nanite 路径遍历本帧排好的 VSM shadows，为每个 VSM 建立 render views 并接入 mesh command pass 的 instance culling。它“知道自己在写 VSM”的证据，是传给 shadow depth shader 的几个关键 uniform 字段：

```text
bRenderToVirtualShadowMap = true
VirtualSmPageTable        = PageTableRDG
AllocatedPageRectBounds   = AllocatedPageRectBoundsRDG
UncachedPageRectBounds    = UncachedPageRectBoundsRDG
OutDepthBufferArray       = PhysicalPagePoolRDG UAV
```

这说明 non-Nanite **不是**画到一个传统 depth render target 再拷贝进 VSM。它在 shadow depth pass 中根据 page table 和 view data，把深度**直接写到物理页池的对应页**。VSM HZB 存在时，non-Nanite culling 还会绑定页级 HZB（`HZBPageTable` / `HZBPageFlags` / `HZBPageRectBounds` / `HZBTextureArray`），用它裁掉不需要提交的 instance。

所以"只渲染需要的页"其实有两个层次：

1. **没有 page request 的虚拟区域不会分配物理页，因此渲染 pass 没有可写目标**——不存在的页不会被画。这是最根本的省。
2. 有 page request 的区域还会通过 page rect bounds、receiver masks、Nanite / instance culling 进一步缩小 caster 工作量。

这两层划分也直接指导调试：如果 page table 正确但 caster 没写进去，通常不是 lighting 问题，而是这一层的 view / culling / mesh pass 没把该 caster 送到对应物理页。调试时要先看物理页是否存在，再看该 caster 是否进入了 Nanite / non-Nanite 的 VSM render path。

一个具体分诊例子：角色是 Nanite mesh，地面是普通 mesh。若角色投影正常、地面投影缺失，先确认 non-Nanite gate，再查 render views、instance culling、visible-instance overflow 和 `OutDepthBufferArray` 写入；若反过来地面正常、角色缺失，优先查 Nanite compact views、shadows pipeline 和 Nanite culling。两条路径的输出同为 physical pool，因此 lighting 看到的只是“这页有没有正确深度”，不会告诉你是哪条 writer 漏了。

这一阶段的 completion 是 physical depth 已由相应 raster path 实际写入 pool，并且后续 projection / lighting 通过 RDG 依赖在写入之后消费。page table valid 只证明有地址；CPU 看见 render pass 也只证明工作已组织。只有 depth 写入完成，lighting 才有可采样遮挡证据。

## 7. Lighting 采样时仍以"虚拟阴影图"思考

物理页写好后，VSM 最后要服务 lighting。在 deferred light 渲染里，每盏灯拿到的是 VSM 的 uniform buffer；local light 在 one-pass projection mask 可用时还会拿到 `VirtualShadowMapId` 和对应的 mask bits。directional / local 在需要 screen shadow mask 的路径上，由 `ApplyVirtualShadowMapProjectionForLight` 执行 VSM projection。

这里的所有权已经再次换手：lighting 不拥有 page request，也不拥有 physical page allocation，它只读取 `VirtualShadowMapUniformParameters`。这份 uniform 把第 1 节列出的 `PageTable`、`PhysicalPagePool`、`ProjectionData`、`PageFlags` 等资源打包给 shader。lighting 的工作是把当前世界点变成虚拟阴影空间的查询，再按 page table 找 physical texel。它不应该反过来"补画缺页"；缺页只能在 marking / allocation / render 的下一次机会里解决。

shader 侧的采样可以归纳成“选层 -> 条件式退层 -> 翻译 -> 读深度”四步：

| 函数 | 做什么 |
| --- | --- |
| `SampleVirtualShadowMapDirectional` | 按 world position 到 clipmap origin 的距离算 absolute clipmap level，加 resolution bias，**选层** |
| `SampleVirtualShadowMapClipmap` | 读 page table；当前层缺页但 `LODOffset` 指向更粗层时，计算 clipmap relative transform，**退层** |
| `SampleVirtualShadowMapLocal` | point / rect light 先选 cube face，再按 receiver footprint 和 `MinMipLevel` 选 local mip |
| `ShadowVirtualToPhysicalUV` / `VirtualToPhysicalTexelBase` | 把 virtual UV / texel **翻译**到物理页池 |
| `SampleVirtualShadowMapPhysicalDepth` | 最终 `PhysicalPagePool.Load(...)` **读深度** |

这就是虚拟页表解决分辨率问题的另一半：**lighting shader 的接口仍然像在采一张大阴影图，但它每次采样都会经过 page table。** 角色脚下那个像素采太阳阴影时，shader 拿到的是虚拟 UV，page table 尝试把它翻译到物理页池里的真实 texel：

- 细节页存在时读细节页；
- 细节页不存在但粗页存在时（靠第 5 节 `PropagateMappedMips` 写好的 `LODOffset`）读粗页；
- 所有层都没有 valid mapping 时返回 invalid，交给后续 projection / filtering 决定视觉结果；VSM 不保证仍然产生阴影。

所以“细页缺失”有两个完全不同的 last-valid-state：`bAnyLODValid=true` 且 `LODOffset>0` 时，证据仍在粗页；`bAnyLODValid=false` 时，证据停在 request / allocation，不能用“自动退粗页”结束诊断。VSM 既不需要为太阳预渲染完整 16k 图，也不需要 lighting 知道物理页池怎么排布，但它不会凭空补出未映射的遮挡。

**Unity 桥**可以这样落地：如果你在 SRP 里自己实现一个 shadow atlas，lighting 通常拿的是 atlas UV 和矩阵，atlas 分配在 CPU 侧一次确定。UE VSM 的 lighting 拿的是 `VirtualShadowMapUniformParameters`，矩阵只能把世界点带到**虚拟空间**，真正的 atlas UV 不是预先固定的，而是在 shader 里**查 page table 当场得到**的。这是"CPU 侧静态分配"和"GPU 侧按页查表"的根本差别。

### One-pass projection：一次聚合多个 local VSM 的屏幕遮挡结果

physical depth ready 之后，VSM 还需要把“某个像素对某盏灯的阴影结果”交给 deferred lighting。传统做法是逐灯生成 screen shadow mask，通用但会重复遍历屏幕。UE 5.7 默认开启 `r.Shadow.Virtual.OnePassProjection=1`，对 **local VSM** 使用一次 projection pass 遍历裁剪后的 light grid，把多盏 local lights 的结果写成 packed shadow-mask bits。

```text
pruned local-light grid + page table + physical depth
  -> one-pass projection 遍历当前像素的 local VSM lights
  -> 为每盏灯写 packed mask bits
  -> deferred lighting 用 VirtualShadowMapId 找到对应 bit
  -> 满足 elide 条件时跳过传统 per-light screen shadow mask
```

这个 packed 输出是本帧 projection / lighting 之间的临时合约，不是 page table，也不跨帧。默认 `r.Shadow.Virtual.OnePassProjection.MaxLightsPerPixel=16`；实现将其硬限制到 32。当前编码按每盏灯 4 bits 控制 transient VRAM：降低上限节省带宽与显存，但更容易让像素内后续 local lights 超出编码；提高到 32 会扩大临时 mask。

若 light-grid 中 local VSM 数超过上限，projection 会多检查一次并记录 OPP max-lights overflow。overflow 不是安全降级承诺，超出范围的灯可能出现 shadow artifacts。可视化 VSM 时 one-pass projection 会关闭；关闭开关、条件不满足、需要传统 mask 合成时，local light 回到 per-light projection。Directional light 不进入 local light grid，也不使用 local OPP，仍走自己的 screen shadow mask projection。

OPP 的设计收益是把多个 local lights 的 projection 聚合，减少逐灯屏幕 pass 与中间带宽；代价是 packed 上限与适用条件。逐灯 projection 在灯少、需要单灯稳定检查、directional 或特殊合成条件下更直接。两条路径最终都只负责发布给 lighting 的遮挡结果，不改变前面的 page demand、allocation 或 caster depth。

回到案例：page-table entry 与 physical depth 都 valid，但某像素第 17 盏 local light 缺影，最后有效状态在 projection 输入，下一步查 OPP max-lights、overflow 与 packed bits；若太阳阴影缺失，则不要查 local OPP，而应查 directional projection。completion 也要分三层：physical depth ready、projection bits / screen mask ready、deferred lighting consumed；前一层成立不保证后一层已经发布。

## 8. Cache manager 让物理页跨帧复用，但只在安全时复用

到这里主线已经走完一帧。但还有一个成本没解决：如果 VSM 每帧都重新渲染所有请求页，仍然很贵——尤其太阳照着的大片**不动的地形**，明明上一帧画过、这一帧一模一样，重画就是浪费。

UE 的缓存策略是：物理页池和 metadata 跨帧保留；page table / page flags / rect bounds 等上一帧数据在 RDG 执行后被 extraction（抽取成 external 资源），下一帧重新导入用于复用和失效判断。这件事由第 1 节那个跨帧持有者 `FVirtualShadowMapArrayCacheManager` 负责。

缓存要解决的是一个很具体的 ownership 问题：RDG 资源默认只活一帧，但物理页池里的深度有些值得活过多帧。于是 frame N 结束时，cache manager 把可复用的页表、flags、rect bounds、physical page lists 和 request flags 抽成 external 资源；frame N+1 开始时，本帧的 `FVirtualShadowMapArray` 再把它们作为上一帧证据导入。这样 RDG 仍然拥有本帧资源依赖，cache manager 只拥有跨帧可复用状态。

先看一个反直觉但重要的边界：**物理池 resize 不是无损操作。** `SetPhysicalPoolSize` 拥有物理池，当请求尺寸、array size、最大物理页数或 create flags 改变时，它重新创建池并 `Invalidate`——清空所有 cache entries、上一帧 buffers 和 physical page metadata。也就是说，任何依赖缓存页的阴影都会在 resize 后重新建立。

每帧结束时，`ExtractFrameData` 分两类抽取：

1. 无论是否启用物理页缓存，只要本帧有 VSM shadow data，就 extraction page table / page flags / rect bounds / Nanite performance feedback / throttle buffer——因为 HZB 和下一帧分析仍可能需要它们。
2. 如果 cache enabled，再 extraction projection data / physical page lists / page request flags，并保存去掉 RDG 临时引用后的 `PrevUniformParameters`。

### 三种复用，正好对应贯穿案例的三个对象

VSM cache 不是"只要有上一帧纹理就复用"，而是一个**条件复用系统**。复用的判定分三条线，回到贯穿案例正好对应三种情况：

| 贯穿对象 | 复用路径 | 判定条件 | 复用方式 |
| --- | --- | --- | --- |
| 不动的地形（太阳 clipmap） | `UpdateClipmapLevel` | 之前渲染过、新 Z range 兼容、view center 仍在 Z guardband 内、WPO threshold 未变 | 不移动物理页，只写 `NextData.PageAddressOffset` 平移复用 |
| 塔上聚光灯（local light） | `UpdateLocal` | `PreShadowTranslation`、`WorldToLight`、receiver mask、distant / cached 状态都没变 | 保持 cache identity；每帧 current id 可变化，再由 previous/current id remap 复用物理页 |
| 奔跑的角色（太阳 clipmap 的 primitive 失效） | `ProcessInvalidations` | skeletal / deformable / revealed primitive 等被收集为 invalidation | 使太阳 clipmap 中受影响的 cached pages 失效；后续是否产生 coarse demand 由 directional coarse-level marking 决定 |

三条线的细节：

**Clipmap 平移复用（不动的地形）**通过后**不移动物理页**，只写 offset；下一帧 `UpdatePhysicalPages` 用这个 offset 把上一帧 metadata 的 page address 映射到当前 clipmap。相机轻微移动时，太阳阴影因此不必整张重画。

**Local light 复用（塔上聚光灯）**比较投影、位置、receiver mask 和 distant 状态。条件不变时，per-light cache entry 仍代表同一 cache identity；本帧 allocator 仍会给它分配 current id，再由 remap 将旧 physical metadata 接到新 id。移动光源、从 distant 切回 regular、receiver mask 开关变化会使 cache entry 失效，丢掉 previous/current id 可复用关系，而不是承诺某个数值 id 永久保持。另有一个只属于 local VSM 的策略：若动态物体影响它的 non-detail coarse pages，默认 `r.Shadow.Virtual.MarkCoarsePagesLocal=2` 可以抑制这类动态失效并保留较旧结果；这是 local 性能取舍，不参与太阳 clipmap 的失效判定。

**Primitive 失效（奔跑的角色）**另走一条路径。`ProcessInvalidations` 在有上一帧 cache data 时，收集 skeletal / deformable / revealed primitive 等 invalidation，把对应页写入上一帧 `PageRequestFlags` 的 invalidation bits；下一帧 `UpdatePhysicalPages` 再把这些 flags 传播到 physical metadata。太阳 clipmap 中受影响且仍有当前 demand 的页因此重新进入 allocation / render，未受影响的地形页则继续按 clipmap cache 条件复用；directional coarse levels 是否产生 demand，由 clipmap 自身的 coarse-level marking 决定。

把奔跑角色的 worked case 拆成状态变化：

```text
frame N:
  角色骨骼或 transform 变化
  -> cache manager 收集它的 primitive / instance range
  -> ProcessInvalidations 用上一帧 page table 找到受影响 cached pages
  -> 在上一帧 PageRequestFlags / metadata 上写 invalidation

frame N+1:
  UpdatePhysicalPages 读到 invalidation
  -> 相关 physical page 不再当作稳定缓存
  -> 仍被当前接收者请求的页重新进入 allocation / render
  -> detail page 写入角色新姿态，未受影响的地形页继续复用
  -> directional coarse level 是否有 demand 由太阳 clipmap 的 coarse-level marking 决定
```

这段时序解释了为什么 stale shadow 常常不是"这一帧没画角色"，而是"上一帧的 cached page 没被正确宣布失效"。调试时要看 invalidation 是否从 primitive / instance range 传播到 page request flags 和 physical metadata，而不是只看当前帧的 shadow draw 是否存在。

所以，错误地绕过其中任何条件，最常见结果就是 stale shadow（残影）：角色跑走了，旧姿势的阴影还留在 cached page 里。这也是为什么下一节排查 stale shadow 要先查 invalidation，而不是先调滤波。

### Pool pressure：先降需求，overflow 仍会直接缺影

cache 解决“能否复用”，pool pressure 解决“本帧物理容量是否足够”。默认 `r.Shadow.Virtual.MaxPhysicalPages=2048`。GPU page-management pass 会把 free-page 数和当前 global resolution LOD bias 作为 status feedback 交回跨帧管理逻辑；当分配率超过 `r.Shadow.Virtual.DynamicRes.MaxPagePoolLoadFactor=0.85`，系统逐步提高 global bias，默认最大 `r.Shadow.Virtual.DynamicRes.MaxResolutionLodBias=2.0`，让后续页需求偏向更粗分辨率。

```text
GPU 本帧统计 free pages / current bias
  -> status feedback 跨 CPU/GPU 帧返回
  -> cache manager 比较 85% load threshold
  -> 逐步调整 GlobalResolutionLodBias，最大 2.0
  -> 后续 marking / allocation 以更粗需求降低压力
```

这是预防性闭环，不是同一帧的救援。feedback 到 CPU 决策再影响后续帧有时间深度；已经 overflow 的当前帧 request 不会因 bias 更新而立刻补回。free pages 变成负数时，未服务分配会明确产生 missing-shadow artifacts，并报告 page-pool overflow。

三种常见调节对应不同代价：增大 pool 减少未服务请求但增加显存；提高 resolution bias 或允许 dynamic resolution 减少页数但损失细节；关闭某些 coarse / non-Nanite demand 可省成本但会失去相应覆盖。它们都不是无损方案。案例中 free pages 降到约 15% 以下后，global bias 应逐步上升；若仍 overflow，最后有效状态在 request / pool allocation，不能把缺影解释为正常粗 LOD fallback。

## 9. 调试时按数据流倒查，而不是先调 bias

VSM 出问题时，最容易先去调 shadow bias 或 filter 参数。但很多问题根本不是 filter，而是资格、id/remap、page demand、allocation、render、projection 或 cache 中某个 owner 没有产出。排查应沿最后有效状态倒查：

```text
platform/runtime gate
  -> per-frame id + cache remap
  -> page request
  -> allocation / initialization
  -> Nanite / non-Nanite depth
  -> virtual translation + projection / OPP
  -> deferred lighting
  -> extraction / next-frame cache
```

每层都问同一个问题：输入有效而输出无效时，哪一个 owner 应该推进下一状态？

### 9.0 VSM 主线是否有资格存在

先确认 `UseVirtualShadowMaps=true`。若项目开关、平台 Nanite 能力、原子支持或运行时 Nanite 项目设置不成立，后续 VSM resources / passes 不应出现。此时 last-valid-state 是项目 / 平台 shadow 选择，不是 page table。

### 9.1 这个像素有没有提出 page request

先确认 `BeginMarkVirtualShadowMapPages` 是否运行、当前输入类型是否参与 marking。GBuffer / water / front-layer / hair / froxel 分支都在 `BeginMarkPages` 里组织，shader 侧入口是 `FGeneratePageFlagsFromPixelsCS`、`FGeneratePageFlagsFromFroxelsCS`、`MarkPageDirectional`、`MarkPageLocal`。如果 MegaLights 正在单独标页，要看 MegaLights 的 sample-based marking，不要只盯常规 pruned light grid。

- 可见表面局部没有阴影页：看 `PageRequestFlagsRDG` 是否被该像素 / froxel 标记。
- 只有体积或远处云阴影缺失：看 coarse pages 是否被关闭、first / last coarse level 是否覆盖该范围。
- MegaLights 阴影缺页：看 sample-based marking 是否执行，而不是只看 `FGeneratePageFlagsFromPixelsCS`。

### 9.2 请求有没有变成 page table 映射

接着看 allocation。若 `PageRequestFlags` 有请求但 `PageTable` 没有 valid entry，查 previous/current id remap、available / empty / requested lists、pool capacity 与 allocation 输出。若 entry valid 但读到旧 depth，查 `SelectPagesToInitialize` 的 static / dynamic initialized / dirty 状态，而不是假定“新页才需要清”。page-pool overflow 表示未服务请求并会产生 missing shadow；dynamic-resolution feedback 只能影响后续帧，不能修复当前 overflow。

### 9.3 物理页有没有被渲染

如果 page table 有映射但阴影内容空，转到渲染路径。Nanite 看 `RenderVirtualShadowMapsNanite`、view compact、Nanite draw 和是否有有效 render pass。Non-Nanite 看 `RenderVirtualShadowMapsNonNanite`、mesh command pass、`bRenderToVirtualShadowMap`、`VirtualSmPageTable` 和 `OutDepthBufferArray`。

- Nanite 物体有阴影、普通 mesh 没阴影：先看 non-Nanite VSM gate，再看 culling、visible instances overflow。
- 普通 mesh 有阴影、Nanite 没阴影：看 Nanite VSM render pass 是否生成、compact views 是否为空。
- 某些动态物体没写入 cached page：看 receiver mask、uncached / dynamic page flags、primitive invalidation。

### 9.4 Lighting 是否拿到了正确 VSM 输入

最后查 virtual translation、projection 与 lighting。translation 的 `bValid=false` 表示所有 LOD 都没有 mapping；只有 `bAnyLODValid=true` 才能按 `LODOffset` 退粗。physical depth valid 而某像素第 17 盏 local light 缺影时，查 OPP max-lights、overflow 与 packed bits；directional 缺影则查自己的 screen shadow mask projection，不查 local OPP。projection bits / screen mask 正常而最终无影，才进入 deferred-light consumer。mapping、depth 与 projection 都有效后，再看 bias、SMRT、normal bias、light source radius 等采样参数。

### 9.5 Cache 是否留下了 stale page

Stale shadow 先看 cache invalidation，而不是先调滤波。锚点：

- `UpdateClipmap` / `UpdateLocal`：light movement、receiver mask、distant 状态是否让 cache identity 失效。
- `UpdateClipmapLevel`：clipmap Z guardband、WPO threshold、page address offset 是否复用成功。
- previous/current id + address remap：cache identity 是否被正确接到本帧 VSM id。
- `ProcessInvalidations`：moving / deformable / revealed primitive 是否把对应 instance range 加入 invalidation；太阳 clipmap 还要结合 directional coarse-level marking 与 `UpdateClipmapLevel` 复用条件判断。
- Local VSM non-detail coarse：`r.Shadow.Virtual.MarkCoarsePagesLocal=2` 是否有意抑制动态失效；这个开关不控制 directional clipmap coarse levels。
- `SetPhysicalPoolSize`：pool resize 是否已导致全 cache drop。

把这些检查压成 last-valid-state 表：

| 已验证的最后状态 | 下一 owner / 检查点 | 不应先做 |
| --- | --- | --- |
| VSM runtime gate 为 false | 项目、平台、Nanite / atomics 支持 | 查 page table |
| current id 已分配，跨帧整灯错位 | cache identity、previous/current id、address remap | 调 bias |
| request flag 有效，entry invalid | allocation lists、pool、overflow | 查 filter |
| entry valid，depth 旧或空 | initialization、Nanite/non-Nanite gate、raster/culling | 查 OPP |
| physical depth 有效，local mask 缺失 | virtual translation、OPP limit/overflow、packed bits | 重做 caster culling |
| projection bits / screen mask 有效 | deferred lighting consumer | 查 marking |
| 当前帧正确、下一帧 directional clipmap stale | extraction/import、primitive invalidation、directional coarse-level marking、clipmap cache 条件 | 增大 filter |
| 当前帧正确、下一帧 local VSM non-detail coarse stale | extraction/import、local coarse mode 与失效策略 | 增大 filter |

同时区分四种 completion depth：CPU / RDG 已声明工作；GPU 已写 request、table、depth 或 mask；deferred lighting 已消费 mask；frame extraction 已排队并在后续帧导入。`QueueExtraction` 不表示 CPU 已同步，也不表示 GPU 已空闲。

可视化工具上：VSM 可视化入口来自 `EngineShowFlags.VisualizeVirtualShadowMap`；`r.Shadow.Virtual.ShowClipmapStats` 打印 clipmap 统计；`DebugDrawDistantVirtualSMLights` 可看 distant local lights 的缓存状态。RenderDoc 里按 RDG event 过滤 `VirtualShadowMapMarkPages`、`BuildPageAllocation`、`RenderVirtualShadowMaps(Nanite)`、`RenderVirtualShadowMaps(Non-Nanite)`，比只看最终 deferred light pass 更快定位——因为这几个 event 正好就是上面五层的产物。

## 10. 主线回放

现在可以把 VSM 压回一条数据流。资格门先决定本章资源主线是否存在。Renderer 每帧为 directional、local 与 unreferenced cache entries 重分配 VSM id，并通过 cache identity 与 previous/current id remap 保留可复用 physical pages。BasePass 后，page marking 用当前接收者与 coarse/detail demand 写 `PageRequestFlags`。Shadow depth 阶段，GPU allocation 复用或分配物理页，生成 page table / flags / rect bounds，并选择 shader 判定需要初始化或重建的 slices。Nanite 与通过独立 gate 的 non-Nanite writer 把 caster depth 写入共同 pool。Lighting 查 page table：有更粗 valid mapping 时按 `LODOffset` fallback，完全无 mapping 时返回 invalid。local lights 可由 OPP 聚合成 packed mask bits，directional 走自己的 projection；deferred lighting 再消费这些结果。帧尾 extraction 为后续帧保留 previous evidence，但不等于 CPU/GPU 同步完成。

所以，回到开篇那个误解：虚拟页表不是“让阴影图无限大”，而是把高分辨率变成**按需兑现且受容量约束的虚拟承诺**。接收者 demand 决定 virtual pages，allocation 在有限 pool 中兑现，cache 尝试跨帧复用，dynamic resolution 在接近容量时降低后续需求；真正 overflow 仍会缺影。太阳从未拥有一张 16k 物理纹理，它只让当前 demand 对应的地址映射到有限 physical pages，并在条件成立时复用或退到更粗 valid pages。

VSM 在本系列里只是 Part 4 的一站：它如何被 MegaLights 的 sample 驱动去标页、又如何被 Nanite 的 cluster raster 写入物理页，分别在第 18 篇和第 16 篇展开；它最终如何与 deferred BRDF 合成进 SceneColor，则属于第 12 篇。
