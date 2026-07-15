# 12 Lighting 与延迟直接光照

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `01_Architecture.md`、`02_SceneProxy.md`、`03_ThreadModel.md`、`04_RHI.md`、`05_RenderGraph.md`、`06_GPUScene.md`、`07_MeshDrawCommand.md`、`08_FrameInit.md`、`09_DepthPrepass.md`、`10_BasePass.md`、`11_Shadows.md`。本篇把 10 篇输出的 GBuffer 和 11 篇准备的阴影证据当作输入合约。  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见 `12_Lighting_CoverageMatrix.md`

## 开篇：已发布表面记录进入延迟直接光照链

当前 view 先通过 deferred-lighting path gate，才把已发布的深度、GBuffer / Substrate 表面记录、阴影证据和现有 `SceneColor` 交给本章的直接光 consumer。这个入口判断把“项目采用 deferred 架构”和“本 view 实际执行 deferred direct-light resolve”分成两个可验证状态。

本章主线成立时，当前 view 已经拥有可供延迟光照解释的屏幕表面记录：`SceneDepth` 给出可见表面的深度，GBuffer 或 Substrate 材质容器给出表面语义，`SceneColor` 保存此前阶段已经写入的颜色基线。Renderer 随后让显式灯光读取这些记录，计算直接光贡献，并以 additive 方式修改当前 `SceneColor` 资源版本。

UE5.7 在桌面 deferred 主路径中，用一个外层控制门决定是否进入这段工作。这个门同时要求：

- Lighting show flag 开启；
- feature level 至少为 SM5；
- Deferred Lighting show flag 开启；
- 当前路径实际使用 GBuffer；
- 当前 view family 不是 ray-traced overlay。

这个门的最小定位锚点是 `bRenderDeferredLighting`。它的教学意义不是让你背一个布尔表达式，而是提醒你：**调试任何 `RenderLights` 问题前，先证明当前 view 进入了这条路径。**

Forward Shading 是另一种工程选择。它会在表面着色阶段读取可用的 forward light data，让 opaque 表面不必等待完整 GBuffer resolve。这样更适合某些 MSAA、带宽或材质需求，但会把灯光循环和材质求值放到更紧密的执行关系中。Path Tracing 和 ray-traced overlay 也有自己的光照消费模型。本章不把这些路径解释为“缺少 Lighting”，而是把它们视为**不同的表面记录与光照消费合同**。

因此，本章只沿一条正向状态链展开：

```text
Path gate 成立
  -> 接收表面、灯光、阴影和现有 SceneColor 输入
  -> 收集当前 view 可见灯，排序并建立唯一消费区间
  -> 把灯打包为 view-local GPU records
  -> 建立 cell -> light index 的 Light Grid
  -> clustered / simple-standard / standard / unbatched consumer 取得消费权
  -> 绑定当前 consumer 所需的 shadow / light-function 证据
  -> 全屏 clustered pass 或逐灯光体积 pass 限定 GPU 工作
  -> 按 legacy ShadingModelID 或 Substrate closure 解释表面
  -> 生成 direct-light contribution，累加到当前 SceneColor 版本
  -> 后续 indirect / environment / atmosphere 系统继续消费
  -> 最后按 RDG、RHI、queue、GPU completion 区分完成深度
```

如果这条链中任意一步断开，后一步即使“看起来存在”，也不能证明灯已经正确照亮像素。全章会用同一个案例，把每一步的数据、所有者、消费者和最后有效证据连起来。

## 本篇边界

本章教授 opaque deferred direct lighting 的框架责任，不展开相邻算法内部。

| 相邻系统 | 本章需要的接口 | 本章停止的位置 |
| --- | --- | --- |
| BasePass | 已发布的 `SceneDepth`、GBuffer 或 Substrate 表面合同，以及现有 `SceneColor` | 不重新讲材质图求值和 GBuffer 编码 |
| Shadows / VSM / Contact Shadow | 当前灯可读取的遮挡证据及其所有者 | 不展开 shadow raster、VSM 页表与缓存算法 |
| Lumen / AO | 间接光和 AO 对 `SceneColor` 的合成责任 | 不展开 trace、surface cache 或时域算法 |
| MegaLights | 从 classic consumer 中取得灯的唯一消费权，并输出自己的 direct contribution | 不展开采样、trace、resolve 与 denoise |
| Atmosphere / Fog / Cloud | 消费已经包含 opaque lighting 的 `SceneColor`、深度和必要光照输入 | 不展开 LUT、froxel 或 ray march |
| Translucency / Volumetric | 可复用部分 view-local light data | 不把 opaque GBuffer resolve 等同于半透明光照 |

## 贯穿案例：同一盏灯的三种配置

场景保持不变：第 10 篇的红色粗糙金属球放在灰色地面上。主灯是一盏照向球体的 local spotlight。我们不再让多个孤立案例各讲一段，而是只改变同一盏灯的配置。

| 变体 | 配置 | 预期消费者 |
| --- | --- | --- |
| A：批处理快路 | 无阴影、无独立 light function，clustered 条件成立 | clustered deferred |
| B：逐灯普通路 | 关闭 clustered，或当前 light grid 未注入灯 | simple-standard 或 standard deferred |
| C：逐灯准备路 | 灯已不兼容 clustered，并需要独立 shadow 或非 atlas light function 输入 | unbatched 后再 `RenderLight` |

另有一个局部对照：矩形灯同时带 IES 与 source texture。它只用于解释一个容易误判的能力边界：**clustered shader 可以使用 IES，但当前实现不采样 rect source texture；source texture 本身不会自动把灯重新路由到 standard。**

---

## 1. 输入账本：Lighting 接手的是屏幕表面合同

当 path gate 成立时，Lighting 接手的核心对象已经不是 mesh，也不是 `UPrimitiveComponent`。mesh 的可见表面已经被前序阶段转换为屏幕空间记录。Lighting 的工作是把“表面记录”和“灯光记录”在像素处结合。

这项拆分首先是正确性合同：表面 producer 和光照 consumer 必须同意每个字段的语义。它也是 UE 的工程选择：材质图只在适合的生产阶段求值一次，多个后续 lighting consumer 复用结果。若每盏灯重新执行材质图，材质成本会随灯数放大，材质系统和光照系统也会重新耦合。

### 1.1 四类输入以不同 owner 与生命周期汇合

| 输入层 | 典型数据 | Producer / Owner | Lighting consumer | 有效期与边界 |
| --- | --- | --- | --- | --- |
| 表面层 | `SceneDepth`、GBuffer、Substrate material data | BasePass、Nanite material shading、Renderer 的 SceneTextures publication | clustered 或逐灯 deferred shader | 当前 SceneTextures 资源版本 |
| 持久灯层 | light type、transform、radius、color、IES、function、channel 等 scene state | Renderer scene 中的 light scene info | 当帧 gather 阶段 | 跨帧存在，直到 scene publication 更新或对象销毁 |
| 遮挡层 | shadow atlas、VSM page/pool、projection mask、ray-traced mask、Contact Shadow 参数 | 各自 shadow producer 或当前 light shader | 当前 direct-light consumer | 依证据类型不同，不能合并成“一张 shadow texture” |
| 颜色层 | BasePass 和前序已写入的 `SceneColor` 版本 | 前序 RDG passes | direct、indirect、environment、atmosphere 等后续 passes | RDG 资源版本链，直到最后 GPU consumer 完成 |

这里必须区分“持久灯”与“当帧灯表”。`FScene` 中的 light scene info 是 Renderer 对场景灯的持久表示；它可以跨帧存在。后面生成的 sorted set、view-local packed records 和 cell index list 才是当前 frame 或当前 view 的临时数据。

### 1.2 Direct、indirect、environment 和 atmosphere 各自负责什么

`SceneColor` 是共享的颜色资源版本链，不是某一个系统的私有最终图。不同贡献应按能量来源和 producer 分开理解：

- **Direct lighting**：来自显式灯的当前表面直达贡献。本章的 classic `RenderLights`、clustered deferred、MegaLights 都属于 direct-light producer，但它们必须彼此排除，避免同一盏灯重复累加。
- **Indirect lighting / AO**：来自间接传播或环境遮蔽。它可以在 direct lights 前准备或部分合成，也可以为了异步重叠在 direct lights 后完成特定 composite；前后两次接口调用不表示同一贡献被重复计算。
- **Environment lighting**：skylight 和 reflections 等环境项在自己的 pass 中解释表面并继续修改 `SceneColor`。
- **Atmosphere / fog / cloud**：后续章节消费已经包含 opaque lighting 的颜色与深度，再加入天空、空气透视、雾和云。

把所有颜色都称为“Lighting 结果”会失去调试价值。若 classic direct pass 后球体亮度正确，而最终画面过亮，应继续检查 MegaLights、间接光、skylight、reflections 或后续合成，而不是回头修改点光 BRDF。

### 1.3 贯穿案例的输入状态

在三个变体中，红色金属球的目标像素都先具有相同表面状态：

```text
SceneDepth: 当前可见球面深度
Normal: 当前球面法线
BaseColor: 红色
Metallic: 高
Roughness: 中高
Material interpretation: legacy DefaultLit，或对应 Substrate closure
SceneColor: 前序阶段留下的颜色基线
```

此时灯还没有被证明进入当前 frame 的任何 consumer。GBuffer 正确只能证明表面输入成立，不能证明灯存在、阴影正确或 direct contribution 已经写入。

最小定位锚点：`SceneTextures` 表示后续 pass 读取场景纹理的标准接口；它证明资源被纳入图内消费合同，不证明 GPU 已完成生产。

---

## 2. Gather 与 Sort：从持久 scene light 到当帧路由表

一盏灯存在于 Renderer scene，只说明场景数据库知道它。当前 view 是否需要它，要经过当帧筛选。

Gather 阶段先检查 view-independent 条件，再确认至少一个 view 接受这盏灯。通过后，Renderer 才为它建立当帧排序记录。这样做避免每个 consumer 各自扫描全部 scene lights，也避免多个 consumer 对“这盏灯是否可见”得出不同结论。

若不集中 gather，clustered、standard、shadow 和 MegaLights 都可能重复做视图筛选。判断漂移会造成三类错误：同一盏灯重复累加、所有 consumer 都跳过它、不同 view 得到不一致的灯集合。

最小定位锚点是 `FSceneRenderer::GatherAndSortLights`。它把持久 scene state 转换为 frame-local routing state；承接这份当帧排序数组、连续消费边界和相关能力摘要的容器是 `FSortedLightSetSceneInfo`。这个容器不拥有持久灯身份，它只描述当前帧各类 consumer 应从哪里开始、在哪里停止。

### 2.1 SortKey 编码当前路由所需的消费特征

通过筛选的灯会得到排序记录和 packed `SortKey`。排序键的目标是让具有相似消费方式的灯形成连续区间，从而用一次扫描找到边界。

这里不能把 `SortKey` 理解为“所有灯光差异的完整描述”。它只编码当前路由和排序所需的特征，例如 light type、simple-light 身份、shadow、light function、lighting channel、clustered support 和 MegaLights ownership。source texture 采样、shader permutation、某些 per-view 条件仍由后续 consumer 决定。

连续区间是 UE 的工程选择，不是延迟光照的理论硬约束。其他实现可以使用多个独立 bucket、stable partition、tagged work queue，甚至 GPU 分类。UE 当前选择 packed key 加连续数组，是为了减少临时分配、保持线性访问，并让 consumer 只持有起止索引。代价是排序字段和边界规则必须同步维护。

### 2.2 四个区间边界为每盏灯分配唯一消费权

```text
SortedLights
  [0, SimpleLightsEnd)
      simple lights

  [SimpleLightsEnd, ClusteredSupportedEnd)
      clustered-compatible non-simple local lights

  [ClusteredSupportedEnd, UnbatchedLightStart)
      classic standard lights，可直接逐灯画

  [UnbatchedLightStart, MegaLightsLightStart)
      需要逐灯准备特殊输入的 lights

  [MegaLightsLightStart, end)
      由 MegaLights 接管的 lights
```

这些边界不是五份复制数组，而是同一排序数组上的 ownership ranges。正确性硬约束是：**同一盏灯的 direct contribution 只能由预期 consumer 生成一次。** 具体使用哪些边界字段，是 UE 的数据组织选择。

`UnbatchedLightStart` 的条件尤其容易被说错。shadow、非 atlas light function 或 lighting channel 并不会无条件把灯推入 unbatched。当前扫描只有在该灯已经标记为 clustered-unsupported、又未被 MegaLights 接管时，才用这些特殊输入需求推进 unbatched 边界。

带阴影的灯也不必一概排除 clustered。若当前 VSM one-pass projection 等条件使它仍满足 clustered 支持规则，它可以继续留在 clustered-compatible range。分类应服从实际 support flag，而不是“有阴影必逐灯”的口号。

### 2.3 三个案例变体在路由表中的变化

| 状态 | 变体 A | 变体 B | 变体 C |
| --- | --- | --- | --- |
| persistent light | 同一盏 spotlight 存在于 Renderer scene | 相同 | 相同 |
| current-view gather | 通过 view-independent 与 view 条件 | 相同 | 相同 |
| sort capability | clustered-compatible | 仍可记录为 clustered-compatible；只是运行时 consumer 不接管 | 标记为 clustered-unsupported，并携带特殊输入需求 |
| ownership range | clustered-supported | clustered-supported，但 standard fallback 起点不推进 | unbatched range |

变体 B 展示一个重要区别：**灯的能力分类和本帧实际 consumer 选择不是同一个状态。** 灯可以“支持 clustered”，但因为 clustered 开关关闭或 grid 未注入，最终由 standard fallback 消费。

### 2.4 Rect + IES + source texture 对照

IES 可以进入 clustered 所需的 packed data。rect source texture 当前不能被 clustered shader采样，但 source texture 并不是 clustered-support sort 条件。因此：

```text
rect light 仍被 clustered 接管
  -> IES 可以生效
  -> source texture 不在该 shader 路径中采样
  -> standard 不会自动补画同一盏灯
```

如果项目必须保留 rect source texture 形状，应选择能实际使用该能力的渲染配置，而不是假设运行时会自动 fallback。能力缺口与路由 fallback 是两个独立问题。

### 2.5 最后有效证据

灯进入 `SortedLights`，只能证明当前 view family 接受了它。区间和 sort bits 正确，才能进一步证明 consumer ownership 的意图。它们仍不能证明 GPU buffer 中有正确参数，也不能证明任何 pass 已执行。

---

## 3. View-local light records：先建立灯的数据记录

排序表解决“谁处理这盏灯”，但 GPU shader 还需要可随机读取的灯参数。UE 因此把当前 view 需要的灯打包成 view-local records。

这一层与 Light Grid 必须分开：

- light record 保存一盏灯的参数；
- grid 保存某个 cell 应引用哪些 light indices。

多个 cell 可以引用同一盏灯。如果每个 cell 都复制完整位置、颜色、半径、IES、rect 和 VSM 数据，内存与更新成本会随覆盖 cell 数放大。UE 选择“一份灯记录 + 多个轻量 index 引用”，以减少重复并让多个 consumer 使用一致参数。

### 3.1 四层 identity 分别定位场景、路由、GPU record 与 cell 引用

| 层级 | 身份含义 | 生命周期 | 能回答的问题 |
| --- | --- | --- | --- |
| persistent light identity | Renderer scene 中是哪盏灯 | 跨帧 scene lifetime | 场景是否拥有这盏灯 |
| sorted-light position | 当帧路由数组中的位置 | 当前 frame | 哪个 consumer range 拥有它 |
| view-local packed index | 当前 view GPU records 中的索引 | 当前 view / frame | shader 从哪里读取灯参数 |
| cell-list index entry | 某个 cell 对 packed light 的引用 | 当前 view / frame | 这个 cell 是否把灯列为候选 |

“LightSceneId”“sorted index”“buffer index”看起来都是整数，但它们属于不同命名空间。把一个层级的值当成另一个层级使用，会出现最难排查的错误：资源都存在、数字也在范围内，但 shader 读到的是另一盏灯。

### 3.2 `ForwardLightBuffer` 与 `LightViewData`

`PrepareForwardLightData` 与相关打包步骤为当前 view 建立 shader 可读的灯数据。`PackLightData` 是最小定位锚点。

这份基础设施可以在 BasePass 之前开始准备，因为它并不读取 GBuffer，也不生成最终直接光；它只把后续多个 consumer 都会使用的灯参数变成 GPU 可读形态。这样可让 opaque forward、translucency、volumetric 和 deferred 等路径复用准备结果，并给 CPU/GPU 工作留下重叠空间。反过来，看到 `PrepareForwardLightData` 已发生，只能证明 light infrastructure 被建立，不能证明当前 view 会通过 deferred gate，更不能证明 `RenderLights` 已执行。

可以把输出理解为两类记录：

- `ForwardLightBuffer`：灯的紧凑 GPU payload，例如 view-relative position、颜色、半径、方向、spot/rect shape 参数和 packed flags；
- `LightViewData`：按当前 view 组织的辅助数据，例如 translated position、IES/rect atlas 信息、VSM id 或稳定索引关联。

不同 consumer 和 buffer mode 不保证以完全相同方式读取每个字段。Forward opaque、clustered deferred、translucency、volumetric 和 MegaLights 辅助路径共享的是“view-local light infrastructure”这一层设计，不是“所有 shader 必须读取完全同一结构”的全局事实。

### 3.3 备选设计与代价

- **每个 tile 复制 AoS 灯记录**：局部访问直接，但大灯覆盖很多 tile 时重复严重。
- **每灯独立 bindless object**：绑定灵活，但索引、生命周期和平台能力更复杂。
- **CPU 每 draw 填 per-light uniform**：适合逐灯 pass，却不适合一个 clustered pass 内枚举大量灯。

UE 的 packed buffer 选择服务于“GPU 在一个 pass 中按 index 读取多盏灯”。当灯数很少、全部逐灯绘制时，简单 per-light uniform 可能更直接；当大量像素需要枚举不同候选灯时，集中 buffer 更合适。

### 3.4 贯穿案例状态

三个变体都会产生当前 view 可读的 spotlight 参数，因为 light infrastructure 还服务其他路径。变体 A 的 packed index 将被 grid cell 引用；变体 B 即使不由 clustered 消费，standard 路径仍可从自己的 per-light 参数合同取得等价信息；变体 C 还需要在 draw 前补充不能仅靠 packed record 表达的特殊输入。

最后有效证据：buffer capture 中存在该灯，只能证明 shader 有机会取得参数；它不能证明目标像素所在 cell 引用了该 index。

---

## 4. Light Grid：再建立 cell 到灯索引的空间关系

如果每个像素枚举当前 view 的所有 local lights，像素成本会随灯数线性增长。Light Grid 的工作是做 broad-phase：把“所有 local lights”缩小成“当前 3D cell 的候选 lights”。

它不是最终可见性答案。灯进入 cell 只说明其 bounds 与 cell 有潜在重叠；lighting channel、精确半径、shadow、light function、材质和 BRDF 仍在 consumer 中继续判断。

### 4.1 3D cluster 用 XY tile 与 Z slice 收紧候选

纯 2D tile 构建简单，但一个 tile 可能同时覆盖近处小物体与远处背景。只按 XY 分类会把深度上完全无关的灯都列进同一候选表。UE 把屏幕 XY tile 与 view-space Z slice 组合成 3D cell，使近处和远处像素能得到不同灯列表。

默认配置中，XY cell 常以 64 像素为尺度，Z 方向常使用 32 个非线性 slice。非线性切分让靠近相机的深度分辨率更细，远处更粗。这些数值是工程配置，不是 clustered lighting 的理论常量；分辨率越细，候选更准确，但 cell 数、构建成本和 header 存储也更高。

### 4.2 两层数据结构

| Grid 数据 | 内容 | Producer | Consumer |
| --- | --- | --- | --- |
| cell header | 该 cell 的候选数量、列表起点和标志 | light-grid compute pass | clustered / forward 等 shaders |
| light index list | 指向 view-local packed records 的 indices | light-grid compute pass | shader 通过 header 枚举 |

`NumCulledLightsGrid` 与 `CulledLightDataGrid` 是最小定位锚点。前者更接近 header，后者更接近 index payload。

### 4.3 建表流程

```text
CPU / Render Thread
  -> 根据 view rect、depth range 和配置确定 GridSize
  -> 准备 local-light bounds 与 view-local records
  -> 声明 grid buffers 和 compute work

GPU compute
  -> 为 cell 建立 view-space bounds
  -> 从当前 workload 的候选集筛选灯
  -> 可选使用有效 HZB 收紧或跳过 cell
  -> 对 sphere / cone / rect bounds 做 broad-phase overlap
  -> 写 cell header
  -> 写 packed-light indices
```

“从当前 workload 的候选集筛选”很重要。不同配置可以让每个 cell面对完整候选集，也可以先用 parent grid、indirection 或 two-level grid 缩小工作量。不能把所有模式都描述成“每个 cell 扫描全部场景灯”。

HZB culling 也是条件能力：只有相应配置开启且当前 HZB 有效时，grid injection 才能利用它。关闭后，Light Grid 仍可通过几何 bounds 建立。linked-list、fixed-list、two-level grid、async compute 与 rect bounds refinement 都属于实现分支。

### 4.4 容量与索引位宽

固定长度 cell list 必须面对“一个 cell 里灯太多”的上限。超过最大容量时，候选可能被截断或需要其他模式处理。索引位宽也限制可表达的灯数。

这是性能结构常见的取舍：固定列表地址简单、访问连续，但容量有限；linked list 更灵活，却增加分配、指针追踪和缓存成本；two-level grid 可减少密集区域扫描，但增加额外构建和 indirection。

当场景只含少量灯时，每灯 light volume 可能比构建复杂 grid 更直接。大量 local lights、且多个像素需要不同候选集合时，3D clustered grid 才能显著降低枚举成本。

### 4.5 像素如何反查 cell

```text
PixelPos + SceneDepth
  -> 计算 XY tile
  -> 用非线性 Z 参数计算 depth slice
  -> 得到 cell coordinate / cell index
  -> 读取 cell header
  -> 遍历 light index list
  -> 用 index 读取 view-local light record
```

XY 计算可以使用预先记录的 pixel-size shift，Z slice 使用与建表一致的深度映射。producer 与 consumer 必须共享同一 grid 参数；否则灯被写入一个 cell，像素却反查另一个 cell。

### 4.6 贯穿案例：变体 A 的 cell 轨迹

| 阶段 | 具体状态变化 | 最后有效证据 |
| --- | --- | --- |
| packed record | spotlight 的位置、方向、颜色、半径和 cone 参数进入当前 view buffer | buffer 中参数正确 |
| injection | cone / bounds 与若干 cells 相交，packed index 写入其列表 | 目标区域 cell header/list 含该 index |
| pixel lookup | 红色金属像素用屏幕坐标和深度得到 cell | pixel cell 与灯注入 cell 一致 |
| enumeration | clustered shader 解包该 index | shader 读到预期灯参数 |

若灯完全消失，先查 packed record 与 cell list；若只在屏幕部分区域消失，优先查 bounds、Z slice、HZB 条件与 pixel-to-cell 参数。此时 BRDF 还不是第一嫌疑。

---

## 5. Consumer ownership：谁在这一帧生成这盏灯的 direct contribution

排序、packed records 和 grid 都是准备阶段。真正生成 direct radiance 的 consumer 必须取得明确消费权。

| Consumer | 取得的工作 | 适用原因 | 主要代价 |
| --- | --- | --- | --- |
| clustered deferred | 一次全屏或材质 tile pass，在每个像素枚举 cell lights | 大量兼容 local lights 可减少逐灯 draw 和状态切换 | 受平台、grid 和功能支持边界限制 |
| simple-standard | simple lights 的专门廉价 fallback | clustered 未接管时仍要正确渲染 simple lights | 仍产生逐灯或专用批次成本 |
| standard deferred | 连续 range 中逐灯 `RenderLight` | 功能完整、控制直接 | draw、PSO 切换和 GBuffer 重读随灯数增长 |
| unbatched | 每灯先准备 shadow/function 等输入，再 `RenderLight` | 特殊输入拥有独立生命周期窗口 | 每灯准备和资源切换最重 |
| MegaLights | 接管 tail range 中的灯 | 另一套 direct-light sample/resolve 模型 | 算法与资源成本属于后续专题 |

### 5.1 Clustered deferred 的成立条件

UE5.7 当前实现要求：

- project setting 允许编译和运行 clustered deferred；
- runtime 开关开启；
- scene feature level 至少为 SM6；
- 平台支持 Virtual Shadow Maps；
- 当前 light grid 实际注入了灯。

前四项由 `ShouldUseClusteredDeferredShading` 概括，最后一项由 `AreLightsInLightGrid` 表示。SM6 和 VSM 平台支持是当前 UE 实现条件，不是 clustered lighting 理论上的硬约束。

条件成立时，standard deferred 的起点推进到 `ClusteredSupportedEnd`。这次 control transfer 表示 clustered 已经认领 simple 与 supported local lights，standard 不再重复画它们。

条件不成立时，灯的 sort capability 不必改变；Renderer 只是不推进 standard 起点，并由 fallback consumer 处理。这就是变体 B。

### 5.2 Clustered 批处理兼容灯，Standard 保留独立输入能力

一个 clustered pass 适合读取共享结构和 atlas 化资源。需要独立 per-light texture、特殊投影、方向光全屏语义、contact/first-person 特殊处理或其他不兼容输入时，强行批处理会扩大 shader permutation、分支和资源绑定复杂度。

可替代方案包括 tiled deferred、compute deferred、每灯 volume 和 GPU light BVH。它们分别在带宽、overdraw、wave divergence、MSAA、构建成本和复杂材质支持上取舍。UE 当前 clustered path 不是“永远最快”，而是在满足支持条件时减少大量 local-light raster passes。

### 5.3 Standard 与 simple-standard

若 clustered 没有取得消费权，simple lights 由专门 fallback 处理，普通灯从 `SimpleLightsEnd` 之后进入 classic loop。每盏灯执行一次 `RenderLight`，使用自己的 light parameters 和覆盖几何。

这条路径的优势是能力直接：每盏灯可以选择自己的 shader permutation、uniform 和 shape。代价是 draw 数、状态切换和 GBuffer 读取随灯数增加。少量功能复杂的灯适合这条路径；大量简单本地灯更适合批处理。

### 5.4 Unbatched 是“输入准备窗口”

变体 C 的 spotlight 已经 clustered-unsupported，并带有独立 shadow 或不能进入 light-function atlas 的 function。它进入 unbatched range 后，Renderer 为当前灯建立一个局部时间窗口：

```text
取得当前灯 ownership
  -> 选择遮挡证据类型
  -> 必要时生成或绑定 screen-space mask
  -> 必要时生成非 atlas light-function attenuation
  -> 绑定当前 view / current light 的参数
  -> 执行这盏灯的 RenderLight
  -> 共享临时资源随后可被下一盏灯重用
```

这解释了 unbatched 的生命周期价值。若所有特殊灯永久保留独立 mask，内存会迅速增长；若所有灯共用同一临时 mask，又必须保证“生产当前灯 mask -> 当前灯 draw 消费 -> 下一灯覆盖”的顺序。

lighting channel 可以参与是否需要这个窗口的判断，但 channel texture 不是每盏灯单独生产的一张 mask。不要把“当前灯使用 lighting channels”和“当前灯拥有独立 attenuation texture”混成同一资源身份。

最后有效证据：灯位于正确 range，只证明它应由该 consumer 处理；还需证明 pass 被创建、当前灯输入已绑定、目标像素获得执行机会。

---

## 6. Shadow evidence：先确定证据类型，再绑定当前 consumer

“有阴影”不是一种统一资源。Lighting 需要的是与当前灯、当前 view 和当前 consumer 匹配的遮挡证据。

| 证据 | Producer / Owner | Lighting 读取形态 | 生命周期 |
| --- | --- | --- | --- |
| regular shadow map / atlas | shadow renderer | 深度 atlas 与投影参数，或由其生成的 screen mask | atlas 可跨多个灯/投影组织；mask 常为 frame-local |
| VSM pages | Virtual Shadow Map 系统 | page table、physical pool 与 projection 所需数据 | 部分页可 cache，当前 frame 仍需正确 page request 与映射 |
| VSM projection mask bits | Lighting 前段或 `RenderLights` 内相关 projection pass | clustered / standard shader 可读取的 packed mask bits | 当前 frame / view 的 projection 结果 |
| ray-traced shadow mask | ray tracing shadow path | 当前灯的 screen-space attenuation | 当前 frame / light |
| Contact Shadow | 当前 deferred light shader | 基于 SceneDepth 的即时 screen-space shadow term 修正 | 不生成 shadow map；当前像素调用期间有效 |

第 11 篇结束时，可以认为 shadow systems 已建立其生产责任和可供后续使用的资源基础，但不能说“所有阴影已经变成一张静态 mask”。例如 VSM physical pages 写入后，当前 Lighting consumer 仍需要正确 projection identity；Contact Shadow 则根本没有预生成 shadow map。

### 6.1 变体 C 的证据链

假设 spotlight 使用 regular shadow，需要当前灯的 screen mask：

```text
shadow depth / atlas 已有匹配投影
  -> 当前 unbatched light 选择 regular-mask occlusion path
  -> projection pass 为当前 view 生成或填充 attenuation mask
  -> RenderLight 绑定同一资源版本
  -> pixel shader 将 mask 转成 shadow terms
```

如果改为 VSM，producer、资源 identity 和 projection mask 都会变化；如果只启用 Contact Shadow，shader 会在 direct-light evaluation 中使用 SceneDepth 做即时修正，而不是等待一张新的 shadow texture。

### 6.2 证据的调试边界

- shadow atlas 有深度，只证明 shadow producer 写过内容；不能证明当前灯引用正确投影。
- projection mask 有值，只证明 mask pass 产生了结果；不能证明当前 `RenderLight` 绑定同一 view / light identity。
- clustered pass 收到 VSM mask bits，只证明资源可读；还要检查该灯的 packed VSM id 与 cell enumeration。
- Contact Shadow 参数存在，只证明 shader具备执行条件；还要检查 ray length、SceneDepth 与当前像素分支。

最小定位锚点：`GetLightOcclusionType` 表示当前灯选择哪类遮挡消费方式；`RenderVirtualShadowMapProjectionMaskBits` 表示 VSM projection evidence 的生产位置；`GetLightAttenuationFromShadow` 表示 pixel shader 把证据转换为 attenuation 的消费边界。

---

## 7. GPU 覆盖：全屏 clustered 与逐灯光体积

确定 consumer 和输入后，还要让 GPU 只在有意义的像素上运行昂贵的 GBuffer 与 BRDF 工作。

### 7.1 Clustered：屏幕 pass 内按 cell 限制灯枚举

Clustered deferred 可以覆盖整个 view 或特定 Substrate tile 类型，但每个像素只枚举自己的 cell list。它减少的是“每像素候选灯数”和“逐灯 raster pass 数”。

其像素级概念流程是：

```text
读取表面记录
  -> 反查 cell
  -> 枚举 supported light indices
  -> 检查 channel、support flags 与 shadow inputs
  -> 对每盏候选灯生成 direct contribution
  -> 累加本 pass 输出
```

grid 中供 clustered 枚举的顺序也服从前面的能力分类。当前 shader 在发现不再支持 clustered 的记录时可以停止继续处理该段；这是一道防止越过 ownership 边界的保险，而不是用来替代正确排序。若 `ClusteredSupportedEnd` 或 packed support flag 错误，保险可能让后续候选整段消失。

变体 A 在这里不需要画 spotlight cone mesh；cone 参数用于 light evaluation 和 grid bounds，consumer 在像素循环中取得灯。

### 7.2 Standard：光体积只圈候选像素

逐灯 deferred 使用覆盖几何减少 pixel shader invocation：

| 灯类型 | 覆盖几何 | 作用 |
| --- | --- | --- |
| Directional | 全屏矩形 | 整个 view 都是候选 |
| Point | sphere | 圈出 radius 影响范围 |
| Spot | cone | 圈出聚光影响范围 |
| Rect | sphere bounds | 先做保守范围裁剪，真实 rect evaluation 在 shader 中完成 |

覆盖几何不负责真实 BRDF，也不代表光源的最终辐射形状。它只是保守地给像素“执行机会”。如果范围更松，结果仍可正确但浪费 shader；如果范围过紧，会发生漏光。

### 7.3 Depth、Depth Bounds 与 Stencil

光体积 draw 配置 raster、depth 与可选 stencil 状态：

- 相机在 light volume 内时，使用适合内部观察的 cull 与 always depth compare，避免包围体表面错误遮掉内部像素；
- 相机在 volume 外时，使用更严格的 depth compare 与 culling；
- point / rect 使用 sphere，spot 使用 cone；
- directional 使用全屏覆盖，并不需要 local-light depth bounds；
- Substrate 可使用材质 tile 对应的 stencil 状态，只处理该 tile 类型。

Depth Bounds Test 只有在平台支持且 `r.AllowDepthBoundsTest` 允许时启用。它根据 light bounds 计算 near/far depth，让固定功能阶段跳过深度范围外的像素。关闭时，光照仍可依靠普通 raster depth test 正确执行，只是失去这项额外裁剪。

这里没有必要把 standard light volume 解释为显式读取本章的 HZB。Light Grid 的 HZB culling 与逐灯 raster 的 depth/depth-bounds 是两套不同的候选裁剪机制。

替代方案包括 scissor rect、独立 stencil prepass、compute tiles 或完全 clustered resolve。独立 stencil prepass 可以减少复杂 shading，但会增加一次几何 pass；UE classic deferred 主线并不要求每盏 local light 固定执行“两遍 stencil + lighting”。

### 7.4 变体 B 的覆盖证据

spotlight 退到 standard 后会绘制 cone。若球体不亮：

```text
cone bounds 覆盖目标像素
  -> camera-inside / outside 判断正确
  -> raster cull 与 depth compare 允许 fragment
  -> 可选 depth bounds 未错误排除
  -> Substrate tile stencil 与当前材质匹配
  -> pixel shader 才获得执行机会
```

看到 cone draw 存在，只证明命令被记录或 GPU capture 中出现了 draw。只有目标像素通过 raster/depth/stencil，才能继续检查 shadow 和 BRDF。

---

## 8. 表面解释：legacy ShadingModelID 与 Substrate 是两种合同

GPU 到达 lighting shader 后，当前像素、当前灯和当前遮挡证据终于汇合。BasePass 负责生产表面语义；Lighting 负责解释，不重新执行材质节点图。

这是硬约束：producer 与 consumer 必须共享同一语义合同。具体用 `ShadingModelID` switch、closure container、material ID indirection 或 forward material evaluation，则是渲染器设计选择。

### 8.1 Legacy GBuffer 路径

legacy deferred 路径从 `FGBufferData` 读取 normal、roughness、metallic、AO、custom data 和 `ShadingModelID`。当前灯被转换为 deferred light data，shadow evidence 被转换为 shadow terms，然后 `IntegrateBxDF` 按 shading model 选择对应 BxDF。

```text
screen position + SceneDepth
  -> 重建表面位置与观察方向
  -> 解码 FGBufferData
  -> 检查 lighting channel / shading-model 条件
  -> 初始化当前 light data
  -> 应用 IES、rect shape、light function 与 shadow terms
  -> IntegrateBxDF(ShadingModelID)
  -> 得到 direct diffuse / specular radiance
  -> 乘当前 view 所需的 pre-exposure
```

红色粗糙金属球使用 legacy DefaultLit 时，低粗糙区域会形成更集中的镜面响应，高粗糙会扩展高光；高 metallic 让 base color 更多参与金属镜面颜色。灯的贡献仍受几何项、衰减、阴影和能量项共同限制。

若 legacy `ShadingModelID` 是没有对应 case 的值，当前 fallback 返回零光照。这是 legacy switch 的事实，不是 UE5.7 所有材质路径的全局规则。它也解释了为什么 GBuffer layout 或 ID 解码错误会表现为“灯和阴影都存在，但材质完全不响应”。

### 8.2 Substrate 路径

Substrate 不把完整材质解释压缩成同一个 legacy `ShadingModelID` switch。它读取 Substrate 的 material/closure 数据，并通过专用 deferred-lighting integration 处理一个或多个 closures。tile classification 与 stencil 可以把不同复杂度材料分流到相应 pass。

因此，调试 Substrate 表面时应检查：

- 当前像素的 Substrate material data 是否由 producer 正确写入；
- tile classification 是否把像素交给正确 consumer；
- deferred lighting permutation 是否按同一数据布局读取；
- closure evaluation 是否收到正确 light 与 shadow terms。

不能因为 legacy capture 中 `ShadingModelID` 正确，就推断 Substrate closure 数据正确；也不能把 legacy 未知 ID 的零光照行为套到 Substrate。

### 8.3 Lighting 复用已发布表面，避免按灯重复材质求值

重跑材质图可以避免存储部分 GBuffer payload，却会让计算成本随灯数重复，并要求 Lighting 保留完整材质执行上下文。Forward rendering 在某些场景正是选择“材质与光照同阶段求值”，以换取不同的带宽、MSAA 和透明度能力。Deferred 则选择先存表面、后复用表面，以支持大量灯和多种屏幕空间 consumer。

这不是一种方案普遍优于另一种方案，而是计算、带宽、材质灵活性、灯数和平台约束之间的取舍。

最小定位锚点：`DeferredLightPixelMain` 表示 classic per-light pixel入口；`IntegrateBxDF` 表示 legacy shading-model dispatch；`SubstrateDeferredLighting` 表示 Substrate 专用解释边界。

---

## 9. SceneColor 责任链：direct contribution 累加到当前颜色版本

Lighting shader 得到 radiance 后，通过 additive blend 或等价的图内写入方式修改当前 `SceneColor` 资源版本。语义上可以写成：

```text
SceneColor_next = SceneColor_current + DirectContribution_this_consumer
```

这个式子描述颜色所有权，不描述 GPU 完成时间。

### 9.1 Classic direct 与 MegaLights 通过 ownership range 排他消费

`RenderLights` 处理 clustered、simple-standard、standard 和 unbatched ranges。MegaLights 处理它接管的 tail range。两者都可以产生 direct-light contribution，但同一盏灯必须只由一个 owner 处理。

若 ownership 错误：

- classic 与 MegaLights 都处理：同一盏灯重复变亮；
- 两者都跳过：整盏灯消失；
- sort identity 与 MegaLights context 不一致：只在某些 view 或配置中出现跳变。

### 9.2 间接光接口围绕 direct 阶段安排异步准备与合成

Renderer 可以在 direct lights 前处理一部分 diffuse indirect / AO 状态，也可以在 direct lights 后完成特定 composite，以便异步工作与 raster lighting 重叠。前后位置承担不同条件和同步目的，不能简化为“间接光被算了两遍”。

本章只保留接口判断：当 direct pass 后颜色正确，而后续 indirect composite 后错误，应转向相应 GI/AO producer，而不是把责任归给 `RenderLight`。

### 9.3 Environment 与 Atmosphere 接力

在 direct 与间接阶段之后，deferred reflections 与 sky lighting继续解释 opaque surface 并修改 `SceneColor`。再往后，第 13 篇的 Sky Atmosphere、Height Fog、Local Fog Volume 和 Volumetric Cloud 消费已经包含 opaque lighting 的颜色与深度。

atmosphere/cloud 对灯颜色或透过率的交叉主要出现在满足条件的 directional-light 路径，不应写成所有 local lights 都统一执行的一步。

本章出口是：**当前 SceneColor 资源版本已经声明并在执行时获得相应 opaque direct-light contribution，随后交给 indirect、environment、atmosphere 与后处理链。** 它不是“最终画面完成”。

---

## 10. 完成深度：从 RDG 声明到 GPU 真正完成

渲染代码中最危险的模糊词是“已经执行”或“已经写入”。同一个 pass 至少有五个不同深度：

| 深度 | 发生了什么 | 能证明什么 | 不能证明什么 |
| --- | --- | --- | --- |
| RDG declared | `GraphBuilder.AddPass` 建立 pass、参数和资源依赖 | 图中存在这项工作及生产消费关系 | pass lambda 已运行、GPU 有命令 |
| RDG pass executed / RHI recorded | RDG 执行 pass lambda，向 RHI command list 记录 draw/dispatch/barrier | CPU 已形成对应 RHI 工作 | 平台队列已提交、GPU 已消费 |
| platform commands formed | RHI backend 将抽象命令转换为平台命令 | 后端命令可进入提交阶段 | GPU 已开始或完成 |
| Platform Queue Submit | 平台命令交给 GPU queue | GPU 获得可调度工作 | 最后 consumer 已完成 |
| GPU consumed / completed | GPU 执行到目标 pass，或覆盖最后 consumer 的 fence/capture 证明完成 | 对应结果已在 GPU 时间线上出现；资源可在覆盖最后 consumer 后退休 | 上游 CPU 分类语义必然正确 |

`FRDGBuilder::Execute` 和 `FRDGBuilder::ExecutePass` 是定位 RDG 从图声明进入 pass 执行的最小锚点。随后还要区分 RHI recording、platform command formation 与 Platform Queue Submit；GPU completion 则需要覆盖最后相关 consumer 的 GPU 证据。普通 render-command fence 主要证明 CPU 线程管线推进到指定深度，不能无条件替代 GPU fence。

### 10.1 RDG 用依赖与 barrier 连接 pass，保留 CPU/GPU overlap

如果每个 pass 后都等待 GPU，完成语义会很直观，但 CPU/GPU 并行和跨 pass overlap 会被破坏。RDG 选择用资源依赖、barrier 和生命周期分析表达顺序，让 CPU 可以继续构图与录制，让 GPU 按依赖执行。

替代的 immediate-mode 提交更容易按调用顺序理解，却难以进行同等级别的 transient resource 复用和图优化。显式 per-pass fence 最容易观察，却会把吞吐换成同步。

### 10.2 最后 GPU consumer 决定 SceneColor 的安全复用点

不能因为 direct-light pass 已声明或 Platform Queue Submit 已成立，就退休其输入输出资源。安全复用需要覆盖最后 GPU consumer：例如 atmosphere、translucency 或 post-processing 仍可能读取同一颜色链。资源生命周期属于整张图和后续队列执行，不属于 `RenderLights` 函数返回时刻。

### 10.3 贯穿案例的完成证据

- CPU 上看到 spotlight 位于正确 range：证明分类意图。
- RDG event 中看到 clustered pass：证明图声明包含该工作。
- RHI capture 中看到 draw/dispatch：证明命令已形成或被捕获到相应深度。
- GPU capture 中查看 pass 后 `SceneColor`：证明该 capture 内 GPU 已执行并产生颜色。
- 覆盖后续最后 consumer 的 GPU fence：才能证明相关资源可安全退休。

任何单一证据都不能同时证明 CPU sort、buffer identity、shadow ownership、BRDF 语义和整帧完成。

---

## 11. 把三种配置串成一条 worked case

下面不再列函数调用，而是记录同一盏 spotlight 在每个阶段的状态变化。

### 11.1 共同起点

| 阶段 | 状态 | Owner | 下一消费者 | 最后有效证据 |
| --- | --- | --- | --- | --- |
| path gate | 当前 view 满足 deferred direct-lighting 条件 | view-family pipeline state | deferred lighting graph | `bRenderDeferredLighting` 成立 |
| surface input | 红色金属球像素已有 depth、normal、roughness、metallic 和材质解释数据 | SceneTextures | lighting shader | capture 中表面记录正确 |
| persistent light | Renderer scene 拥有 spotlight 的持久 state | `FScene` light representation | gather stage | scene publication 后灯存在 |
| current-view gather | 灯通过 view-independent 与至少一个 view 的筛选 | frame-local gather result | sorter | 灯进入 sorted set |

### 11.2 变体 A：clustered 批处理

| 阶段 | 数据 / 控制变化 | Owner 转移 | 失败时先查 |
| --- | --- | --- | --- |
| sort | 灯位于 clustered-supported range | sorter -> clustered candidate | support bits 与 range boundaries |
| packed records | spotlight 参数获得 view-local index | current view light resources | position、radius、cone、color |
| grid | 目标 cell 列表写入该 index | grid compute -> shader-readable buffers | header、offset、index、容量 |
| ownership | clustered 条件与 grid 条件成立，standard 起点推进 | clustered pass | project/runtime/SM6/VSM/grid gate |
| shadow | 本变体无独立 shadow mask | packed light / default attenuation | 是否错误读取旧 mask |
| coverage | 屏幕 pass 到达球体像素，像素反查 cell | clustered raster pass | tile/stencil、pixel cell |
| interpretation | legacy DefaultLit 或 Substrate closure 读取灯参数 | lighting shader | layout、channel、support branch |
| output | direct radiance additive 写当前 SceneColor 版本 | clustered pass -> SceneColor chain | blend target 与 pass 后 capture |

### 11.3 变体 B：standard fallback

| 阶段 | 数据 / 控制变化 | Owner 转移 | 失败时先查 |
| --- | --- | --- | --- |
| sort | 能力上仍可 clustered | sorter | 不要错误修改 sort 结论 |
| runtime gate | clustered 关闭或 grid 没有注入灯 | clustered 不接管 | `ShouldUse...` 与 `AreLights...` |
| ownership | standard 起点不推进，fallback 取得灯 | classic loop | 是否被错误跳过或重复处理 |
| coverage | spotlight cone draw 给目标像素执行机会 | current light draw | bounds、inside/outside、depth/stencil |
| interpretation | per-light parameters 与表面合同进入同一 BRDF 语义 | deferred light pixel shader | shadow、channel、BxDF |
| output | 同样的 direct-light 语义写 SceneColor，执行组织不同 | standard pass | 与 clustered 结果差异的来源 |

如果变体 A 与 B 结果不同，不能先假设“clustered BRDF 不同”。两条路应共享同类表面和灯光语义。优先比较 packed parameters、source-texture 能力、cell membership、shadow evidence、permutation 和覆盖方式。

### 11.4 变体 C：unbatched 特殊输入

| 阶段 | 数据 / 控制变化 | Owner 转移 | 失败时先查 |
| --- | --- | --- | --- |
| sort | clustered-unsupported 且需要独立输入 | sorter -> unbatched range | 条件是否同时成立 |
| shadow/function prepare | 当前灯获得匹配 mask 或 attenuation | shadow/function producer | resource identity 与 current light |
| binding | `RenderLight` 绑定当前资源版本 | unbatched loop -> draw | 是否复用了上一盏灯内容 |
| coverage | cone 限定候选像素 | current draw | depth bounds 与 stencil 条件 |
| interpretation | shadow terms / function 调制进入 BRDF | pixel shader | mask、atlas、permutation |
| output | 仅这一盏灯的 direct contribution 被累加一次 | current pass | 是否被其他 consumer 重复处理 |

### 11.5 Rect light 对照

把 spotlight 临时换成带 IES 与 source texture 的 rect light：

- 若 clustered 接管，IES 可参与 light color，source texture 不会被当前 clustered shader采样；
- 若选择 standard 能力路径，可使用对应 source-texture permutation；
- clustered 不采样 source texture，不会自动触发 standard 再画一次；
- 如果同时保留两个 consumer，结果会重复变亮，而不是“自动补齐功能”。

这个对照说明：**路由正确性与功能完整性要分别验证。** 灯由唯一 consumer 处理可以是路由正确，但所选 consumer 仍可能不支持某项视觉能力。

---

## 12. Last-valid-state：由外向内定位断点

调试时不要从最终像素公式倒推所有可能性。按最后一个已经成立的状态逐层推进：

| 层级 | 最后有效证据 | 能证明 | 还不能证明 | 下一检查 |
| --- | --- | --- | --- | --- |
| 0 | deferred path gate 成立 | 本章路径可进入 | 任何灯已被处理 | surface/light inputs |
| 1 | SceneDepth / material data 正确 | 表面 producer可信 | 灯、shadow、consumer | sorted set |
| 2 | 灯进入 sorted set | 当前 view接受灯 | range 与 GPU data 正确 | sort bits / boundaries |
| 3 | range 正确 | ownership 意图正确 | pass 实际接管 | view-local records |
| 4 | packed records 正确 | shader可取得灯参数 | 目标 cell 引用灯 | grid |
| 5 | cell header/list 含 index | broad-phase 候选正确 | channel、shadow、BRDF | consumer ownership |
| 6 | 正确 consumer 取得灯 | 不应由其他 classic consumer 再处理 | 输入绑定和像素覆盖 | shadow/function inputs |
| 7 | 当前灯证据正确 | attenuation 可被读取 | raster 与材质解释 | coverage |
| 8 | pass 覆盖目标像素 | shader 有执行机会 | depth/stencil/branch | decoded inputs |
| 9 | shader 读到预期 surface/light data | 解码入口正确 | BxDF 与输出 | BRDF result |
| 10 | legacy BxDF 或 Substrate closure 输出合理 | direct radiance 合理 | blend target 与后续颜色 | SceneColor |
| 11 | GPU capture 中 pass 后 SceneColor 正确 | 本次 direct contribution 已出现 | indirect/environment/atmosphere | 后续 consumers |
| 12 | 后续 pass 读取同一颜色链 | RDG 图内顺序正确 | Platform Queue Submit / GPU completion | platform submission evidence |
| 13 | queue submit 已发生 | 平台工作交给 GPU | 最后 consumer完成 | GPU completion |
| 14 | 覆盖最后 consumer 的 GPU 完成证据 | 资源可安全退休 | 颜色语义必然正确 | 回到更早数据层 |

### 常见症状与首查层级

**整盏灯完全消失**

```text
path gate
  -> current-view gather
  -> sort range
  -> consumer ownership
```

**只有屏幕部分区域漏灯**

```text
packed bounds
  -> grid cell / Z slice / HZB 条件
  -> light volume coverage
  -> depth / stencil
```

**灯有亮度但 rect 图案不对**

```text
实际 consumer
  -> clustered source-texture 能力边界
  -> standard permutation 是否被明确选择
```

**阴影内容像另一盏灯**

```text
occlusion type
  -> projection / mask identity
  -> current view / light binding
  -> 临时 mask 是否过早复用
```

**GPU capture 正确但 CPU 逻辑仍可疑**

GPU capture 证明被捕获 pass 的 GPU 结果，不自动证明 sorted index、persistent identity 或 ownership 设计没有偶然对上。回到最早能解释异常配置变化的 CPU state。

---

## 13. 收尾：Lighting 是一条所有权与证据链

本章最重要的模型不是某个 shader 名，而是下面这条状态转换：

```text
deferred path gate
  -> published surface inputs + persistent scene lights
  -> frame-local gather / sort / ownership ranges
  -> view-local packed light records
  -> cell-local light index lists
  -> one direct-light consumer
  -> current-light shadow / function evidence
  -> clustered screen coverage 或 standard light volume coverage
  -> legacy ShadingModelID 或 Substrate closure interpretation
  -> direct contribution 写入当前 SceneColor 资源版本
  -> indirect / environment / atmosphere 继续消费
  -> RDG declaration、RHI recording、Platform Queue Submit、GPU completion 分层取证
```

其中有三类硬约束：表面 producer 与 Lighting consumer 必须共享语义；同一盏灯的 direct contribution 不能被重复消费；资源必须活到最后 GPU consumer 完成。

其余大量结构是 UE 为性能、并行、内存和调试做出的工程选择：packed sort key、连续区间、view-local buffer、3D Light Grid、clustered pass、逐灯光体积、共享临时 mask 和 RDG 调度。替代方案并非不能工作，只是在不同灯数、材质、MSAA、平台能力和功能需求下交换成本。

下一篇从本章的颜色出口继续：它接收已经包含 opaque lighting 的 `SceneColor` 与 `SceneDepth`，再解释大气、雾和云如何生产中间资源并合成到后续颜色链。
