# 09 深度预 Pass 与 HZB：从可见 draw 输入到可消费深度证据

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: 05 RenderGraph、06 GPUScene、07 MeshDrawCommand、08 帧初始化与可见性  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）

## 开篇：一份主场景深度合同怎样建立

DepthPrepass 位于 BasePass 之前，但它不是一张独立深度纹理的唯一生产者。第 08 章先把 Scene 压缩成当前 view 的 primitive 候选、per-pass draw 输入和 GPUScene 数据窗口；本章从这些输入出发，为每类几何分配深度写入责任，再把 traditional mesh、velocity 与 Nanite 的结果汇入同一份 `SceneDepth.Target`。随后 Renderer 建立阶段性的 `PartialDepth`、shader 可读的 `SceneDepth.Resolve`，并把 resolved depth 归约为 current HZB；其中 furthest HZB 还可以提取为下一帧历史。

因此，主线不是“PrePass 开或关”，而是一条连续的生产链：当前 frame 先选择 early depth 的覆盖目标，具体 producer 再写入主场景 depth attachment，资源组织阶段发布各类可读表达，最后由 BasePass、GPU occlusion、Nanite、Instance Culling 和屏幕空间系统按自己的时机与语义消费。PrePass 负责这条链的前半段完整度，BasePass 与其他 producer 负责补齐剩余表面。

这套模型可以直接解释三类常见现象。Masked 几何必须用能保持 alpha cutout 轮廓的 depth path；需要 velocity 的 movable opaque 可以把 early depth 责任交给 velocity pass；Nanite 使用自己的 culling/raster 路径，却仍把结果交付给主场景深度。至于 `Target`、`PartialDepth`、`Resolve` 和 HZB，它们是同一生产链在不同读取时机提供的资源表达，而不是四套互不相关的深度系统。

> **Unity 迁移边栏**：如果你习惯把 Unity 的 DepthOnly 或 `_CameraDepthTexture` 理解为“一次 pass 生成一张固定深度纹理”，需要先放下这个等价关系。UE 的主场景深度由当前配置启用的 traditional、velocity、Nanite 和 BasePass producer 分阶段共同建立；attachment 已有内容、shader-readable 表达成立、HZB 已构建以及历史证据可用，是不同状态。这个类比只用于暴露旧心智模型的缺口，不表示两套 API 或时序可以逐项对应。

本章沿着下面这条唯一状态链推进：

```text
08 交付 CPU primitive 候选、pass input 与 GPUScene 数据窗口
    ↓
Renderer 用 EDepthDrawingMode 定义 BasePass 前的覆盖目标
    ↓
Traditional、Velocity、Nanite 接收各自负责的几何
    ↓
GPU raster 把结果汇入 SceneDepth.Target
    ↓
PartialDepth 发布第一阶段读取入口（或回退到主深度表达）
    ↓
Resolve 发布 shader-readable depth
    ↓
HZB builder 产生 current furthest / optional closest
    ↓
ViewState 提取 current furthest，供下一帧注册为 previous history
    ↓
BasePass、culling 与 screen-space consumers 选择所需版本和语义
    ↓
按“最后成立状态”调试
```

这条链同时回答四个问题：

1. **Owner**：谁选择对象，谁写 Target，谁建立可读表达，谁构建历史？
2. **Data**：数据如何从 pass-local draw 输入变成 Target、Resolve、HZB 和 GPU 消费结果？
3. **Control**：对象为什么走 traditional、velocity 或 Nanite 路径？消费者为什么只能在某个阶段之后读取？
4. **Lifetime**：当前帧阶段性深度、最终 current HZB 和下一帧 previous HZB 各自活多久？

## 贯穿案例：同一条街道上的四类对象

全章持续追踪同一个街道 view：

- **masked 栅栏**：静态、具有 alpha cutout，要求深度轮廓与最终可见表面一致；
- **movable 金属车**：需要速度信息；当 mode 为 `DDM_AllOpaqueNoVelocity` 且 velocity producer 接受该对象时，early depth 责任交给 velocity pass；
- **Nanite 建筑**：不走普通 DepthPass MeshDrawCommand 路径，而由 Nanite raster/culling 路径向主场景深度交付结果。
- **建筑后方的远处路牌**：它本身不是新的 depth producer 类型，而是 HZB consumer 子案例，用来观察 bounds、mip、current/previous 证据和“宁可多画、不能错剔”的保守原则。

它们最终共享一份主场景深度消费合同，但生产路径、成立条件和可用于调试的证据并不相同。

## 一、入口状态：08 交付三类可消费输入

DepthPrepass 不从 `UPrimitiveComponent` 重新开始。第 08 章已经把场景状态转成当前 view 可消费的几类输入：

| 当前产物 | Owner | 交给下一位消费者的数据 | 下一步 |
|---|---|---|---|
| CPU primitive 可见性与 relevance | visibility / relevance 任务 | 当前 view 仍需考虑的 primitive 与 pass 资格 | pass setup 组织 traditional depth 候选，或登记动态生产工作 |
| cached / dynamic DepthPass 输入 | mesh pass setup | cached command、dynamic build request 与 per-view pass 输入 | `FDepthPassMeshProcessor` 固化 traditional depth 配方 |
| GPUScene 与 scene uniform 资源窗口 | GPUScene / Renderer | 与当前 view 匹配的 primitive、instance、payload、range 与读取入口 | Instance Culling、depth shader 与 Nanite 读取对象数据 |
| Nanite view/raster 输入 | Nanite view setup | view、instance/cluster 候选与 raster 状态 | Nanite culling/raster 形成可导出的 depth result |

`PrimitiveVisibilityMap` 的职责是为同帧 CPU pass setup 提供 primitive 级粗筛结果；GPU 侧再根据 instance range、Nanite cluster 和 HZB 证据继续细分。GPUScene 则提供这两类 GPU 工作读取对象身份与 payload 的数据合同。两者分别生产“要继续组织哪些工作”和“GPU 怎样找到这些工作的对象数据”。

CPU coarse visibility 与 GPU fine culling 不是互相替代的两套方案。CPU primitive 级结果要立即服务同帧的 pass setup、relevance 和工作组织；GPU 更适合继续处理 instance range、Nanite cluster、HZB 查询以及结果仍留在 GPU 上消费的细粒度工作。前者缩小并组织问题，后者在不把海量结果读回 CPU 的前提下继续缩小真正执行的范围。

在街道案例中：

- masked 栅栏和金属车可以进入传统 DepthPass 候选，但之后仍要接受 material、velocity 和 processor 条件；
- Nanite 建筑进入 Nanite 的 view/culling 输入，不要求先形成普通 DepthPass command；
- 三者所需的 transform、primitive identity 和 view data 必须在各自 producer 读取之前处于同一有效资源窗口。

入口阶段完成后，traditional 与 Nanite producer 都拿到了可开始工作的输入。下一步由 drawing mode 和具体 producer 条件为街道对象分配写入责任。

## 二、`EDepthDrawingMode`：分配 BasePass 前的覆盖责任

`EDepthDrawingMode` 描述 ordinary early mesh depth producer 在 BasePass 前希望建立到什么程度。它不是材质上的单一开关，也不是从低到高排列的纯线性等级。

| 策略 | ordinary early producer 的覆盖责任 | 后续交接 |
|---|---|---|
| `DDM_None` | 本帧不安排 ordinary early mesh producer | Nanite、BasePass 与其他 producer 继续建立最终主深度 |
| `DDM_NonMaskedOnly` | 提前处理适合廉价 depth path 的 non-masked opaque | masked 表面留给满足轮廓合同的 later producer |
| `DDM_AllOccluders` | 提前处理具备 occluder 资格并被 pass 接受的对象 | 其余表面在后续 producer 中补齐 |
| `DDM_AllOpaque` | 以 BasePass 前覆盖 opaque 可见表面为目标 | masked、WPO、PDO 等表面使用能保持最终轮廓的 material depth path |
| `DDM_MaskedOnly` | UE5.7 Mobile early-Z 的 masked-only 分支只把 masked 表面交给 ordinary early producer | non-masked 表面沿 Mobile renderer 的其他 producer 前进 |
| `DDM_AllOpaqueNoVelocity` | 覆盖 full-prepass 目标中的非 velocity 部分 | 满足 velocity 条件的几何由 velocity producer补写 depth |

UE5.7 不是从这张表里任意挑一个 mode，而是按下面的决策链收敛：

```text
先建立 shading-path 默认值
    Deferred 默认 DDM_NonMaskedOnly；Mobile 默认 DDM_None
        ↓
应用显式项目/开发覆盖
    Deferred r.EarlyZPass=0/1/2 分别覆盖为 None/NonMasked/AllOccluders；3 保留默认行为
    Mobile early-Z=2 选择 DDM_MaskedOnly
        ↓
检查是否有功能要求 full depth prepass
    Desktop: Nanite、compute AO、DBuffer、virtual texturing、stencil LOD dithering、
    early masked、forward shading 或 selective base-pass outputs 等条件可强制 full
    Mobile: MobileUsesFullDepthPrepass 决定 full 分支
        ↓
按 depth pass 的 velocity 能力分工
    能在 depth pass 输出 velocity -> DDM_AllOpaqueNoVelocity，velocity producer 补齐对应对象
    不能在 depth pass 输出 velocity -> DDM_AllOpaque，并让 early producer 覆盖 movable opaque
```

Full prepass 是一项工程取舍：它提前支付更多 depth draw，masked/material path 甚至会提前支付材质相关成本，换取 BasePass 前更完整的深度、DBuffer 等功能前置条件和更早的遮挡机会。`AllOpaqueNoVelocity` 进一步避免同一对象在 depth 与 velocity producer 中重复承担写入，但代价是 BasePass 前的深度完整性现在依赖 velocity pass 确实接受并执行这些对象。

`ShouldRenderPrePass` 先决定当前 frame/path 是否安排 ordinary PrePass 窗口，`EDepthDrawingMode` 再规定这个窗口覆盖哪类表面，processor / producer 最后对具体对象执行资格判断。三层判断共同形成“谁在 BasePass 前写哪一部分”的责任表。

还要再加第三层判断：某个具体 mesh 是否被对应 processor 或 producer 接受。因此实际控制关系是：

```text
ShouldRenderPrePass：本帧是否存在 ordinary PrePass 窗口
    ↓
EDepthDrawingMode：该窗口追求什么覆盖目标
    ↓
processor / producer 条件：这个对象最终由谁写、是否真的写
```

策略有三层责任：

1. Renderer 根据 feature、platform、project settings 和运行时条件选择整体策略；
2. pass processor 判断具体 mesh/material 是否被当前 producer 接受；
3. traditional、velocity 或 Nanite producer 在自己的执行窗口内真正写入深度。

在街道案例中：

- `DDM_NonMaskedOnly` 下，masked 栅栏不进入 ordinary non-masked early coverage；
- `DDM_AllOpaque` 下，栅栏若进入 early depth，必须执行能保持 alpha cutout 轮廓的 material depth path；
- `DDM_AllOpaqueNoVelocity` 下，金属车不是从深度合同中消失，而是把写入责任转给满足条件的 velocity producer；
- Nanite 建筑不由这个枚举直接变成传统 DepthPass draw，它仍由 Nanite 路径交付主场景深度。

策略阶段完成后，masked 栅栏、movable 金属车与 Nanite 建筑分别有了预期 producer。下一节把这份策略变成实际的 draw、culling 与 raster 工作。

## 三、三类 producer：共享目标，不共享生产路径

### 3.1 Producer responsibility matrix

| Producer | 输入 | 主要控制条件 | 交付给主深度的产物 | 下一消费者 |
|---|---|---|---|---|
| Traditional Mesh PrePass | cached/dynamic DepthPass 候选 | drawing mode、mesh/material 接受条件、position-only eligibility | traditional raster draw 写入的 depth/stencil 内容 | first-stage readers、后续 depth producers 与 BasePass |
| Velocity producer | velocity pass 可接受的动态几何 | velocity pass 已安排、对象需要并能输出 velocity、renderer path 允许 depth write | velocity 与对应 early depth | 后续主 depth readers 与 HZB 路径 |
| Nanite producer | Nanite view、instance/cluster、raster state | Nanite enabled、有效 view/raster inputs、平台与 path 支持 | Nanite raster result 导出的主场景 depth | Partial/Resolve/HZB 与 BasePass depth contract |
| BasePass fallback | 前序 producer 尚未覆盖的表面 | BasePass depth/stencil policy 与当前深度完整度 | 最终主深度中仍需补齐的表面 | BasePass 之后的 resolved depth consumers |

### 3.2 Traditional producer：processor 决定“能否按这条路径写”

`FDepthPassMeshProcessor` 的教学价值不在函数调用顺序，而在它把候选转换成 depth draw command 所需的四步决策：

```text
候选 mesh batch
    ↓ 是否允许参与 depth pass
检查 material / mesh / pass 条件
    ↓ 选择表面一致性路径
position-only 或 material depth shader
    ↓ 固化 pass-specific 状态
形成可由当前 view 调度的 depth command
```

position-only 的收益来自省去不必要的材质像素工作，但它不是所有 opaque 的默认真理。只要最终可见表面依赖 alpha clip、复杂 WPO、PixelDepthOffset 或其他会改变覆盖/深度的材质行为，depth producer 就必须选择能够保持 BasePass 可见表面一致的路径，或者不在该 early producer 中覆盖它。

对 masked 栅栏而言，最危险的错误不是“没有用最快 shader”，而是用错误的几何轮廓写深度：如果栅栏空洞被写成实心遮挡物，后续 HZB 会把错误轮廓放大为遮挡证据。

Processor 选定路径并形成 command 后，traditional producer 沿着下面的控制链把结果交给 Target：

```text
pass-local candidate
→ FDepthPassMeshProcessor 形成 DepthPass command
→ view / instance culling 产出可执行范围
→ RDG depth producer 声明目标、依赖与 raster pass
→ ParallelMeshDrawCommandPass 组织 RHI recording
→ RHI/backend 形成平台命令
→ Platform Queue Submit 把平台命令交给设备 queue
→ GPU raster 执行 depth draw
→ SceneDepth.Target 获得该表面的深度内容
```

这里列出 `FDepthPassMeshProcessor` 与 `ParallelMeshDrawCommandPass`，是为了给“策略形成”和“命令记录”各保留一个最小 UE 锚点，而不是要求沿函数调用链阅读源码。每个箭头都产生新的权威产物：command recipe、可执行范围、RHI work、platform work 与最终 Target 内容。调试时先确认哪一种产物最后存在，完整证据边界集中放在本章末尾的 Last-valid-state 表。

### 3.3 Velocity producer：接收 movable depth 责任

`DDM_AllOpaqueNoVelocity` 的含义不是“动态物体不写 early depth”，而是 renderer 允许满足条件的 velocity pass 接管这部分写入。这个交接只有在以下条件同时成立时才可靠：

- 当前 renderer/path 确实安排 velocity pass；
- 金属车被 velocity path 接受；
- velocity 输出与 depth/stencil 状态允许它承担补写；
- pass 在需要形成 early depth 的阶段内执行。

因此金属车的正向路径是：drawing mode 把它分配给 velocity producer，velocity pass 接受它并记录相应工作，GPU 在该窗口同时交付 velocity 与 depth，后续主 depth readers 再消费补齐后的结果。若 Target 中缺少车辆深度，就沿这四个产物依次检查。

### 3.4 Nanite producer：交付同一主深度合同

Nanite 建筑不走传统 MeshDrawCommand 的 ordinary DepthPass 路径。它从 Nanite 的 instance/cluster 候选开始，经过 GPU culling 与 raster 工作，把可见结果导出到主场景 depth 合同。

“共享主深度”不等于“共享生产算法”：

- traditional producer 的最小工作单位是传统 mesh draw；
- Nanite 以 instance、cluster、candidate 和 raster result 组织工作；
- 两者可以在不同 RDG pass 中执行，但对后续主场景 depth consumer 来说，必须由资源依赖保证读取发生在所需 producer 之后。

Producer 阶段完成后，每类几何都有明确写入责任，相应 draw/culling/raster work 也已进入各自执行路径。接下来要观察这些工作怎样汇入共同的 `SceneDepth.Target`。

## 四、`SceneDepth.Target`：汇合多条写入路径

`SceneDepth.Target` 表示当前承担 depth/stencil attachment 写入与测试职责的资源表达。traditional draw、满足条件的 velocity draw、Nanite depth export，以及必要时的 BasePass depth write，都可能在各自阶段影响主场景深度。

这里不要把 Target 简化成“绝对不能采样的纹理”。更稳定的判断是：

- 它当前以什么角色被绑定；
- 目标 consumer 需要哪一种 shader-readable 表达；
- RDG 是否已经建立 producer 到 consumer 的依赖；
- 平台、sample count、memoryless 和具体 path 是否要求额外 resolve/转换。

在街道案例中，masked 栅栏、金属车与 Nanite 建筑按各自窗口到达 Target。某个 capture 时刻可能已经包含栅栏与建筑，车辆则在 velocity 窗口之后出现；等三条写入路径都完成后，主 depth attachment 才包含这个配置下 BasePass 前应有的覆盖。观察 capture 时要同时记录时间点和资源表达，才能把内容归回正确 producer。

Target 的产物是一份仍按 depth/stencil attachment 合同被写入和测试的主深度。第一阶段 consumer 接下来通过 `PartialDepth` 取得早期入口，更晚的 shader consumer 则等待 resolved 表达。

## 五、`PartialDepth`：发布第一阶段读取入口

`PartialDepth` 表达 SceneTextures 中供第一阶段消费者使用的 depth 资源窗口。它在存在独立 first-stage buffer 时指向该阶段结果；没有独立 buffer 时，接口回退到主 SceneDepth 表达：

```text
某个 view 使用 second-stage depth pass
    → primary depth pass 结束后复制出 FirstStageDepthBuffer
    → Nanite 需要时也把 depth result 发射到该 buffer
    → PartialDepth 指向并 resolve 这份第一阶段表达

FirstStageDepthBuffer 不存在
    → PartialDepth 回退到主 SceneDepth 表达
```

因此，`PartialDepth` 的核心教学意义是**冻结一个阶段边界**。消费者选择它，表示自己只需要第一阶段已经成立的深度，可以与 velocity、Nanite 或 second-stage 等后续 producer 的工作继续重叠。

这也意味着它的完整度必须结合 producer 时序解释：

- drawing mode 把 masked 表面纳入 primary depth pass 且 material depth path 被执行时，独立 `FirstStageDepthBuffer` 包含栅栏的 cutout 轮廓；
- `DDM_AllOpaqueNoVelocity` 的 velocity 补写发生在 primary depth copy 之后，因此独立 first-stage buffer 不接收这次补写，车辆进入主 `SceneDepth.Target` 的后续内容；
- 独立 first-stage buffer 存在且 Nanite 路径执行时，Nanite 会把 depth result 额外发射到该 buffer，使建筑也进入这份阶段性表达。

当独立 first-stage buffer 不存在时，fallback 让相同 consumer 接口继续获得有效 depth 表达。资源身份可以复用，阶段语义仍由当前 renderer 的 producer 时序决定。

Partial 阶段完成后，第一阶段 consumer 已获得定义明确的读取入口，后续 producer 则继续推进主 SceneDepth。需要更完整内容的 shader consumer 在下一步读取 `SceneDepth.Resolve`。

## 六、Resolve：发布 shader-readable depth

后续 shader consumer 需要的是满足其绑定和采样合同的 depth 表达。`SceneDepth.Resolve` 用来描述这条 shader-readable 边界。

教学上应把 resolve 理解为：

```text
depth attachment 的阶段性生产结果
    ↓ 资源依赖、sample/format/platform 条件处理
可被后续 shader 按约定读取的 depth 表达
```

底层可能发生独立复制、resolve、别名使用或其他平台相关处理；本章不把其中任何一种物理实现当成所有平台的固定事实。

责任边界如下：

| 责任 | Owner | 最后成立状态 |
|---|---|---|
| 写入 depth attachment | traditional / velocity / Nanite / BasePass producer | Target 在相应阶段获得内容 |
| 建立 shader-readable 表达 | renderer 的资源组织与 resolve 路径 | Resolve 被 RDG 标记为可由后续 pass 消费 |
| 选择正确版本 | 具体 consumer | 读取与自身阶段、完整度和语义匹配的资源 |

Resolve 发布后，后续 shader 可以沿 RDG 依赖读取与该阶段匹配的 depth 表达。HZB builder 就是其中一个 consumer：它读取 resolved depth，再生产层级化结果。

## 七、HZB：把二维深度变成层级证据

HZB 把 shader-readable depth 归约为多级纹理，使后续 GPU 工作可以用较低分辨率 mip 快速判断一个屏幕区域的保守深度关系。较低 mip 保留较细空间分辨率，较高 mip 用一个 texel 概括更大的屏幕区域，使 consumer 可以根据对象或 cluster 的屏幕 bounds 选择合适层级，而不必逐像素查询完整深度。

UE5.7 当前 `BuildHZB` 路径把这件事分成四步：

1. 根据 view rect 把基准尺寸向上取到 2 的幂；主场景默认 `RenderHzb` 调用以半分辨率建立 mip0，`bLevel0Unscaled` 特例则保留原尺寸，例如后文 Nanite primed HZB 使用的构建路径；
2. mip0 从 `SceneDepth.Resolve` 覆盖的 texel 做 Gather4 / 2x2 归约，把原始二维深度变成第一级区域证据；
3. 后续每一级继续从父 mip 的 2x2 区域归约，直到高 mip 用一个 texel 概括更大的屏幕范围；
4. compute 路径可以一批生成最多四级 mip，pixel fallback 则逐级生成 furthest；当 consumer 请求 closest 时，构建路径同时产生对应 closest 输出。

在常见 reversed-Z 约定下，shader 对采样区域取较小 device-Z 形成 furthest 证据，并对较大 device-Z 做向上取整后形成 closest 证据。这里的取整与归约不是为了还原线性距离，而是为了让压缩后的半精度层级仍保持 consumer 所需的保守边界。对街道路牌而言，屏幕 bounds 越大就要选择越高的 mip；这样查询次数更少，但一个 texel 概括的区域更宽，遮挡判断也会更保守。UE 用层级查找在查询成本、空间精度和误判安全之间做取舍：证据不足时多画，而不是错剔。

### 7.1 Device-Z、mip 与保守判断

深度纹理中存放的通常是 device-Z，而不是可以直接按世界距离理解的线性米数。UE 常见路径使用 reversed-Z：更近的表面可能对应更大的数值，更远的表面对应更小的数值。因此不要脱离投影约定死记“min 是近、max 是远”；先问这个 HZB 输出承诺的是 closest 还是 furthest 语义，再看对应归约操作。

HZB 能服务遮挡判断，依赖三个连续步骤：

1. 把待测试对象或 cluster 的 bounds 投影到屏幕；
2. 根据它覆盖的像素范围选择一个不会过细、也不会跨越过大区域的 mip；
3. 使用该区域的保守深度证据判断它是否能被已有遮挡物完全挡住。

所谓“保守”，意味着证据不足时应把对象留在可见集合，而不是冒险错剔。bounds 偏大、mip 过粗、history 失效或深度不确定，通常会带来多画，而不应让本来可见的路牌消失。街道中的远处路牌正是这个原则的观察点：建筑遮住它的大部分屏幕区域时，furthest 证据仍必须足够确定，才能安全把它判为被遮挡。

### 7.2 Closest 与 furthest 服务不同查询

先按 consumer 要回答的问题理解两类输出，再结合 reversed-Z 判断具体归约方向：

- **furthest HZB** 保存适合保守遮挡判断的区域边界；
- **closest HZB** 保存适合需要区域最近深度语义的边界；
- 构建路径始终围绕已请求的 consumer 语义组织；需要遮挡边界时产生 furthest，需要最近表面边界时再产生 closest；
- 只请求 furthest 的路径只建立 furthest 输出和对应依赖。

具体数值归约受 device-Z 表示影响，但 consumer 依赖的是“closest/furthest 语义是否正确”，而不是名称表面对应的数值大小。

### 7.3 Current 生产链与 previous 历史链

当前帧可以从已产生的 depth 表达构建 current furthest HZB，并在需要时构建 current closest HZB。历史主线则围绕 **previous furthest HZB**：

```text
current furthest HZB produced
    ↓ queue extraction
写入 ViewState 的帧历史
    ↓ 下一帧重新注册且有效性检查通过
previous furthest HZB available
```

这条历史主线保存的是 previous furthest HZB。没有 ViewState、首次帧或时间重置、camera cut、相机移动超过继承阈值，或 visibility 被强制 reset 时，`PrevViewInfo` 不继承旧 HZB；本帧只有 Nanite 或 Instance Culling occlusion 等 consumer 请求历史时才 extraction，否则历史入口会被置空。分辨率或投影变化是否拒绝旧资源，要由具体 consumer 的映射与有效性检查决定，不能概括成 Renderer 一律清空。连续性与请求条件满足时，下一帧 GPU consumer 才注册并读取这份历史。

最小定位锚点可以理解为：当前 view 的 furthest HZB 由 `View.HZB` 入口承载；closest 是按需请求的可选 view-side 输出；previous furthest 则来自 ViewState history 的提取、注册与有效性检查。符号只负责帮助定位，真正的判断仍是“当前还是历史、closest 还是 furthest、是否 produced、是否有效”。

### 7.4 HZB 的生命周期

| 表达 | 产生时机 | 典型用途 | 生命周期边界 |
|---|---|---|---|
| current furthest | 当前帧 depth 已产生后 | 当前帧 GPU 遮挡与后续历史提取 | 当前图内消费；提取后可成为下一帧历史 |
| current closest | 只有请求该语义的路径才产生 | 特定屏幕空间或体积 consumer | 当前图内按请求消费 |
| previous furthest | 上一帧 current furthest 在有历史 consumer 请求时成功提取并重新注册 | 当前帧早期的保守预测或遮挡证据 | 无 ViewState、首次帧/时间重置、camera cut、过大相机移动、强制 visibility reset 或具体 consumer 判无效时放弃 |

HZB 阶段完成后，GPU 图内已经存在带明确版本与 closest/furthest 语义的层级深度。GPU culling 和 screen-space consumers 可以直接读取；furthest 结果还可以被提取到 ViewState，延续为下一帧输入。

### 7.5 CPU/GPU 放置由下一位消费者决定

理解 HZB 为什么留在 GPU 上，关键不是一句“GPU 更快”，而是看结果接下来由谁、在什么时候消费。街道案例可以分成四类生产—消费关系：

| 生产者 → 消费者 | 本章实例 | 为什么放在这里 |
|---|---|---|
| CPU → CPU，同帧 | `PrimitiveVisibilityMap` → relevance / pass setup | CPU 下游立即需要 primitive 级粗筛结果，留在 CPU 可以直接继续组织 pass 输入 |
| GPU → GPU，同一 Nanite 窗口 | previous / primed HZB → main raster → internal occluder HZB → post | Nanite 用阶段内部更新的证据复测 deferred candidates；这份 internal HZB 不是稍后建立的 `View.HZB` |
| GPU → GPU，本帧后续 | `SceneDepth.Resolve` → current `View.HZB` → later screen-space consumers / history extraction | 最终 current HZB 继续保持 GPU resident；generic pass Instance Culling 的 HZB occlusion 读取的是有效 previous furthest |
| GPU → CPU，延迟或可选 | occlusion query / feedback → 后续帧 CPU 历史 | CPU 接受滞后一帧或更多的保守证据，不阻断当前帧流水线 |
| GPU → CPU，同帧强制 | 当前 GPU 可见结果 → 当前 CPU pass setup | CPU 必须等待 GPU、搬回结果再继续生产，形成硬同步点，因此主线避免这种依赖 |

Previous furthest HZB 本身走的是 GPU-resident history：本帧从 current HZB 提取资源，下一帧再注册给 GPU consumer。CPU 负责调度这份历史入口，并不读取 HZB 像素表。Readback 真正昂贵的地方也由此清楚：除了数据搬运，它还可能迫使 CPU 与 GPU 在同一帧互相等待，破坏两条时间线的重叠。

## 八、Nanite two-pass：用更新后的证据复测候选

Nanite 的 two-pass occlusion 最适合用“证据更新”理解：

UE5.7 的条件化实例先建立一条更具体的 producer 链：two-pass 只有在 `r.Nanite.Culling.TwoPass` 启用且存在可用的 previous / primed HZB 输入时才成立；没有该输入时，本次 cull/raster 不执行 two-pass 分工。Renderer 优先从 view history 取得 previous HZB。历史不可用且 `r.Nanite.PrimeHZB=1`，或设置为 `2` 强制 priming 时，Renderer 先运行额外的低细节 Nanite depth-only priming raster，再从该 RasterContext depth 构建 furthest `NanitePrimedHZB`；是否同时纳入 non-Nanite `SceneDepth` 由独立条件控制，不能把 priming 简化成对已有主深度做一次下采样。

```text
previous furthest 或本帧 primed HZB 有效
        ↓
Main 阶段
    cull/raster 明显可见的 instance / cluster
    把被当前证据遮挡或仍不确定的候选留给 deferred/post 集合
        ↓
Main raster result 与当前 SceneDepth 共同建立 Nanite.PreviousOccluderHZB
        ↓
Post 阶段
    用更新后的 occluder HZB 复测 deferred candidates
        ↓
可见 post result 与 main result 一起继续 raster / export 到主 SceneDepth
```

`Nanite.PreviousOccluderHZB` 是当前图中新建的中间资源，但它的 occluder 集合来自上一帧判定的 occluders：这些 occluders 在本帧 main 中重新 raster，然后由当前 `SceneDepth` 与 RasterContext 结果建立新的图内 HZB。它不是 `PrevViewInfo.HZB` 那张 ViewState 历史纹理。Main 的输入可以来自上一帧 history 或本帧 primed HZB；post 再用这份 current-graph internal HZB 复测前一阶段留下的 candidates。稳定责任是“先用 history/priming 组织 main，再把 previous-frame occluders 本帧重画出的证据交给 post”。

在街道案例中，main 阶段让已有足够证据的 Nanite cluster 继续工作，把被当前证据遮挡或仍不确定的 cluster 放入待复测集合。阶段中主 depth 和 HZB 证据更新后，post 再决定这些 cluster 是否继续 raster。整个闭环留在 GPU 上，不要求把 cluster 可见性回读给同帧 CPU。

## 九、消费者：按读取方式、版本与语义选入口

每个 consumer 先声明自己的读取方式，再选择 current/previous 与 closest/furthest 语义：

| Consumer | 读取入口 | 生产的下一状态 | 触发条件 |
|---|---|---|---|
| Opaque BasePass | 当前阶段的 depth/stencil access 合同 | depth-tested GBuffer work，必要时补写 depth | `BasePassDepthStencilAccess` 与当前表面覆盖情况 |
| Generic pass Instance Culling | 条件化 previous furthest HZB | visible instance buffer / indirect args | `PrevViewInfo.HZB` 有效且当前 pass 启用 instance occlusion |
| Nanite culling | 阶段可用的 previous/current furthest 证据 | main/post candidate 与 raster work | 当前阶段已有的证据版本与 Nanite 配置 |
| 屏幕空间或体积 consumer | current closest 或 furthest HZB | 对应 GPU texture/buffer 结果 | 算法请求该语义且输出已由 HZB builder 建立 |
| Current-frame instance occlusion query | current HZB 与本帧 instance candidates | 供未来帧消费的历史 mask / query state | 当前路径启用该历史生产链并建立所需输出 |
| CPU historical occlusion | CPU 侧历史 query / feedback | 下一次 CPU visibility 判断的保守输入 | 历史结果到达且 view 有效性条件满足 |

GPU visible instance buffer 和 indirect args 会继续留在 GPU，后续 draw 直接消费它们。CPU 只在调试、统计或特殊功能明确请求时才 readback，因此主渲染链不需要用同帧 GPU 结果反向阻塞 CPU pass setup。

### 9.1 条件化 Instance Culling：结果为什么继续留在 GPU

在启用相应 GPU occlusion 的路径中，数据轴不是“CPU 读取 HZB 后决定画谁”，而是：

```text
GPUScene primitive / instance identity
+ 当前 pass 的 instance range
+ 条件化 previous furthest HZB
→ GPU visibility decision
→ visible instance buffer
→ indirect args
→ 后续 draw 在 GPU 侧直接消费
```

这条链把 CPU broad phase 与 GPU fine culling 连成连续分工：CPU primitive 结果组织 pass work，GPU 再按 instance bounds 与 HZB 证据筛选；visible buffer 与 indirect args 随后直接驱动 draw。只有调试、统计或特殊功能才显式 readback。

这是条件模型：当前 pass 启用相应 GPU occlusion、平台与配置支持该路径、`View.PrevViewInfo.HZB` 有效且资源依赖成立时，generic Instance Culling 才加入 previous furthest HZB 并生成对应 indirect work；条件不成立时，它沿不依赖该 HZB 的路径继续组织实例工作。Current HZB 参与本帧更晚的 GPU consumer，也可以进入生产未来帧 instance occlusion history 的另一条 query 链，不能把它写成同一批 pass setup 的直接输入。

### 9.2 一个条件化的 screen-space consumer

某些半透明材质或屏幕空间效果会读取 resolved depth 或 closest HZB 来估计当前像素附近的最近表面。它们与 opaque BasePass 的 depth/stencil access 不是同一消费方式：前者通过 shader 参数读取一份可读资源，后者首先依赖 attachment 的测试/写入策略。若建筑和路牌的主深度正确，但某个半透明屏幕空间效果仍穿帮，应先检查它实际绑定的 depth/HZB 版本和 RDG 依赖，而不是重新怀疑 traditional PrePass 是否画过。

## 十、街道案例：沿状态链检查四类对象

| 状态门 | Masked 栅栏 | Movable 金属车 | Nanite 建筑 |
|---|---|---|---|
| 08 输入 | 进入 DepthPass 候选 | 进入 pass 输入并携带动态状态 | 进入 Nanite view/culling 输入 |
| 策略分配 | non-masked-only 时留给后续；full-prepass 时进入 material depth path | `AllOpaqueNoVelocity` 转交满足条件的对象给 velocity | 进入 Nanite view/culling/raster 路径 |
| Producer 接受 | 需要 material depth path 保持 alpha 轮廓 | velocity pass 必须实际接受并执行 | Nanite culling/raster 条件必须成立 |
| Target | 写入 cutout 后的真实轮廓 | 在 velocity 阶段补入同一主 depth | raster result 导出到主 depth 合同 |
| Partial | primary depth 已执行 material path 时，独立 first-stage buffer 包含 cutout 轮廓 | 独立 first-stage buffer 不包含稍后的 velocity 补写；fallback 到主 SceneDepth 时沿用主资源内容 | 独立 first-stage buffer 存在时，Nanite 额外向它发射 depth result |
| Resolve | 获得 shader-readable 表达 | 同上 | 同上 |
| Current HZB | 栅栏轮廓影响保守遮挡 | 车辆当前深度成为层级证据 | 为后续 cluster/occlusion 提供证据 |
| Previous history | 本帧 furthest 可供下一帧使用 | 下一帧可成为历史遮挡物 | 下一帧早期 Nanite 可使用历史证据 |
| 首要调试点 | material depth/alpha clip 是否一致 | velocity responsibility 是否真正落地 | main/post 使用的证据版本与 produced 状态 |

远处路牌先按 CPU primitive broad phase 的结果决定是否进入 pass work；进入 generic Instance Culling 候选范围后，后者把路牌 bounds 映射到有效 previous furthest HZB 的合适 mip，证据不足时继续把它写入 visible instance buffer 和 indirect args。Nanite two-pass 则按自身的 previous/primed 与 main 后中间 HZB 复测 cluster；本帧 current HZB 还可以服务更晚的 screen-space consumer，或生产未来帧 history。三条路径都使用层级深度，但证据版本与输出 owner 不同。

这个案例揭示了“同一 Target、多 producer”的核心：最终资源可以统一，生产责任却必须逐对象、逐路径追踪。调试时如果只问“PrePass 开没开”，就会丢失真正决定结果的控制关系。

## 十一、Last-valid-state evidence ladder

最可靠的调试方法不是从最终黑屏或遮挡错误反推整个 renderer，而是寻找**最后一个已成立状态**。

| 最后成立状态 | 能证明什么 | 不能证明什么 | 下一检查点 |
|---|---|---|---|
| CPU primitive 可见 | CPU broad phase 没淘汰对象 | 不证明进入 DepthPass 或 Nanite work | pass relevance / Nanite input |
| Pass-local 输入存在 | 当前 view 已建立候选 | 不证明 processor/culling 接受 | processor 或 GPU candidate 状态 |
| Traditional command 形成 | 传统 depth 路径具备可调度 command recipe | 不证明 culling 后仍有可执行范围 | instance/draw range |
| Culling 后可执行范围成立 | command 对应的 instance/draw 范围已被接受 | 不证明图中已声明 depth producer | RDG pass declaration |
| RDG work declared | depth pass、目标资源与 producer→consumer 依赖已经加入图 | 不证明 RHI 工作已记录 | RDG execute / pass recording |
| RHI recorded | depth draw、binding 与 barrier 已记录为 RHI 抽象工作 | 不证明平台命令已形成 | backend translation / encoding |
| Platform commands formed | API command buffer/list 已形成 | 不证明已提交设备 queue | Platform Queue Submit event |
| Platform Queue Submit | 平台命令和同步关系已交给 graphics/compute queue | 不证明 GPU raster 已写 Target | capture、timestamp/fence 与资源观察 |
| `SceneDepth.Target` 已有对象 | 截图时刻 attachment 含该深度 | 不证明目标 consumer 读到同阶段 Resolve/Partial/HZB | consumer resource binding 与依赖 |
| `PartialDepth` 已产生 | 第一阶段可读入口成立 | 不证明 final SceneDepth 完整 | 后续 producer / final resolve |
| `SceneDepth.Resolve` 已产生 | shader-readable depth 表达成立 | 不证明 HZB 已构建 | HZB pass 与输出请求 |
| Current HZB 已 produced | 当前 GPU consumer 可读相应层级证据 | 不证明 CPU 获得答案，也不证明历史有效 | consumer output / extraction |
| History 已 extraction | 下一帧可尝试注册 previous furthest | 不证明 camera cut 后仍有效 | previous validity |
| Consumer binding 与依赖成立 | consumer 指向预期的 Resolve/HZB 版本，且 RDG ordering 正确 | 不证明 consumer shader 已产生正确输出 | shader 参数、bounds、mip 与输出 buffer |
| GPU consumer output 已产生 | visible list / indirect args 等 GPU 决策成立 | 不证明后续 draw 已消费，也不证明最后 consumer 完成 | downstream pass 与 completion evidence |
| GPU consumed | 目标 draw/pass 已读取 consumer output 或 depth evidence | 不证明当前资源版本已越过所有合法 consumer | 覆盖最后 consumer 的 fence/等价 completion evidence |
| Final consumer completed | 排在最后合法 consumer 后的 completion evidence 已完成 | 不证明下一帧 history 仍有效 | 下一帧 request、identity 与有效性检查 |

### 症状到证据的映射

- **masked 栅栏遮挡过多**：先确认 material depth path 是否执行 alpha clip，再确认错误轮廓是否已经进入 Target/HZB。
- **金属车 early depth 缺失**：先确认 `AllOpaqueNoVelocity` 的责任转移，再确认 velocity pass 是否执行并写 depth。
- **Nanite 与传统几何遮挡关系错误**：确认两类 producer 的结果在目标 HZB build 前是否都已进入所需 depth 表达。
- **Target 截图有对象，但 consumer 看不到**：确认 consumer 绑定的是 Target、Partial、Resolve、current HZB 还是 previous HZB，并核对 RDG dependency。
- **camera cut 后大面积误遮挡**：确认 previous furthest history 是否被判无效，而不是继续复用旧证据。
- **BasePass 仍大量写 depth**：检查 early depth 完整度目标、各 producer 覆盖和 BasePass depth/stencil policy；不要只检查 PrePass 是否执行。
- **GPU culling draw 数量异常**：确认 HZB 版本、instance/cluster bounds、visible buffer 与 indirect args 的生产顺序，不要把 CPU primitive 可见性当作最终 GPU 结果。
- **RenderDoc 中 command 已出现但 Target 没有对象**：继续区分 culling 后范围、RHI recording、平台命令形成、Queue Submit 与 GPU raster；CPU 侧 command 存在不是深度写入证据。

## 十二、常见误读回收

### 误读一：PrePass 执行就代表 early depth 完整

错误。执行存在性由 renderer 安排，完整度由 drawing mode、processor 接受和多个 producer 的实际写入共同决定。

### 误读二：`DDM_None` 代表本帧没有 SceneDepth

错误。它只关闭 ordinary early mesh producer；Nanite、BasePass 或其他路径仍可能建立最终主深度。

### 误读三：DepthPass 一定使用 position-only，所以一定便宜

错误。masked、WPO、PDO 等表面必须保持与最终可见轮廓一致；必要时要使用 material depth path，或不由该 producer 覆盖。

### 误读四：Nanite 有另一套与主场景无关的最终深度

错误。Nanite 有独立的 culling/raster 中间结构，但会向主场景 depth 消费合同交付结果。

### 误读五：Resolve 必然是一次物理整纹理复制

错误。稳定概念是 shader-readable 的逻辑边界；物理实现受平台、sample count 和资源配置影响。

### 误读六：每次 HZB 都同时生成 closest 与 furthest

错误。输出由 consumer 需求决定，某些路径只构建 furthest。

### 误读七：previous closest 与 previous furthest 是对称历史

错误。通用历史主线是 previous furthest HZB；closest 不是普遍有效的 previous history。

### 误读八：BasePass 普遍采样 HZB

错误。Opaque BasePass 首先依赖 depth/stencil access 合同。HZB 是特定 occlusion、Nanite、Instance Culling 条件路径和后续屏幕空间系统的输入。

## 十三、章节出口：把可判断的深度合同交给 BasePass

到这里，第 10 章接收到一组可以逐项判断的深度状态：

1. 当前 renderer 选择了什么 early depth 完整度策略；
2. traditional、velocity 与 Nanite 各自覆盖了哪些表面；
3. 当前 BasePass 读取点面对的是哪一阶段的主 depth；
4. 对某个表面，可信深度是否已经存在，BasePass 应只测试、采用 read-only 策略，还是仍需补写；
5. `PartialDepth`、`Resolve` 和 HZB 已为哪些 consumer 建立，以及各自位于哪一个生命周期窗口。

因此，第 10 章讨论 BasePass depth contract 时，会根据当前表面是否已被可信 producer 覆盖、材质是否保持相同可见轮廓、Nanite 与平台路径以及 renderer 选择的 depth/stencil access，决定只测试、read-only 使用还是继续补写。

`BasePassDepthStencilAccess` 是这个交接处的最小 UE 定位锚点：它把前面已经成立的深度覆盖条件转成 BasePass 的 depth/stencil 访问策略，帮助定位当前 BasePass 被允许如何消费或补写这份深度合同。更深的 RHI、Platform Queue 与 GPU completion 边界统一在 Last-valid-state 表中判断。

如果只带走一句话：

> **DepthPrepass 的本质是把 pass-local 候选经由多 producer 写入主 SceneDepth，再建立 Partial、Resolve、HZB 与历史证据；调试时必须沿着“谁负责、写到哪一阶段、哪一版可读、最后哪个状态成立”向前推进。**
