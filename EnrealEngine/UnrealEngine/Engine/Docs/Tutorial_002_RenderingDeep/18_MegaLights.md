# 18 MegaLights：固定预算的随机多光源直接光照

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `05_RenderGraph.md`、`06_GPUScene.md`、`08_FrameInit.md`、`10_BasePass.md`、`11_Shadows.md`、`12_Lighting.md`、`17_Lumen.md`。本篇把第 12 篇的 classic deferred lighting 路由，以及第 11 篇建立的阴影证据所有权和 VSM 接口模型当作前置事实；第 19 篇将在下一章继续展开 VSM 内部的页表、物理池与缓存机制。  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见 `18_MegaLights_CoverageMatrix.md`

## 开篇：大量光源的核心成本来自贵操作逐灯重复

BasePass 结束后，屏幕上的每个可见像素已经有了深度、法线、材质和 GBuffer payload。第 12 篇讲过 classic deferred lighting 的做法：把灯排序，能批处理的走 clustered，不能批处理的走 standard deferred，最后逐灯或逐 cell 把贡献累加回 `SceneColor`。

这个模型在几十盏灯时仍然可控，但当场景设计开始把“灯具”当作美术语言使用时，问题会很快变质。一个工厂、夜市、演唱会舞台或科幻走廊里，可能同时有几十到上百个会移动、会投影、带 IES 或 area shape 的局部光源。此时成本不只是 light list 变长，而是每盏灯都可能触发三类昂贵工作：

- 它要被某个 lighting shader 评估一次 BRDF；
- 如果投影，它还要准备或查询一份遮挡证据；
- 如果它是 area light，阴影和高光还需要足够的采样才能稳定。

把这些工作按“每盏灯一次”重复，成本会随光源数线性增长。Unity SRP 读者很容易先想到 clustered light grid：让每个像素只遍历当前 cell 里的灯。UE 也有这条路径，但 clustered 只能缩短“当前像素可能受哪些灯影响”的列表，不能把“每盏投影灯都要付一次贵操作”的事实抹掉。一个 cell 里仍然可能有很多高贡献灯；如果每盏都要完整 shadow + BRDF，light grid 只是把全场景线性成本变成局部线性成本。

MegaLights 的核心回答是：

> **不要让昂贵阶段服务“每盏灯”。先从很多候选灯里抽出固定数量的 light samples，再让 shadow、shading、denoise 服务这些 samples。**

这里的“固定预算”不是说整帧总成本是常数。MegaLights 先把工作拆成两个 domain：**candidate domain** 是当前位置从 light grid cell 能枚举到的候选灯集合；**sample domain** 是下采样后的输入位置与每位置固定槽位组成的工作域。候选枚举仍然会随 cell 密度、active tile 数和屏幕分辨率变化；受明确上界约束的是 selected-sample 后端：

```text
下采样后的采样域 × 每个采样点的 sample 槽位数
```

也就是说，场景可以有很多候选灯，但每个下采样像素只留下固定几个样本槽位。后面的 shadow trace、VSM 查询和 selected-sample shading 围绕这些槽位运行，而不是围绕所有候选灯运行。resolve 与 denoise 仍会随输入分辨率、active tiles、历史有效性和配置改变工作量，所以更准确的结论是：**MegaLights 给每个 sample-domain 位置的槽位数以及这些槽位驱动的贵后端建立上界，而不是让灯数与总成本完全脱钩。** UE 再用随机重要性采样、历史引导、时域累积和空间滤波，把稀疏样本稳定成可显示的直接光贡献。

整篇文章你只需要盯住一小组对象，它们就是 MegaLights 的全部心智模型：

| 对象 | 一句话角色 | 它从什么变成什么 |
| --- | --- | --- |
| sorted light 尾段 | 当前帧被 MegaLights 接管的那批灯 | 从“classic deferred 逐灯”变成“MegaLights 候选灯” |
| `LightSamples` | 每个下采样位置固定几个样本槽位 | 从空槽位变成“选中了哪盏灯 + 权重 + 可见性” |
| `LightSampleRays` | 样本对应的遮挡查询状态 | 从“未完成的 trace”变成“带可见性的样本” |
| visible light hash | 本帧哪盏灯对哪块区域可见 | 当前帧反馈，影响下一帧抽样分布 |
| lighting history | 已经累积的直接光画面 | 跨帧稳定 denoise 结果 |

后面每一节都是在追踪这组对象之间的状态转移：灯先被路由进尾段，再被压成槽位，槽位拿到可见性，可见性 resolve 成光照，光照 denoise 后写回 `SceneColor`，同时留下两套 history 喂给下一帧。

## 贯穿案例：一间灯具很多的工厂

本篇只跟一个场景走：

```text
一间室内工厂:
  40 盏可移动 point / spot / rect local lights
  多数允许 MegaLights，且可能投动态阴影
  一个角色在灯下移动，会遮挡一部分光源
  BasePass 已写好 SceneDepth / GBuffer
  classic deferred lighting 仍负责非 MegaLights 灯
```

我们关心的问题始终是同一个：**角色身边某个像素，如何在不逐灯完整循环 40 次的前提下，得到这些灯的直接光照和阴影？**

全章主线是一条数据流：

```text
sorted lights 中的一批候选灯
  -> MegaLights 接管尾段所有权
  -> 每个 view 建固定大小的 sample buffer
  -> 从 light grid 候选中随机选出固定槽位
  -> 为 selected samples 生成 shadow evidence
  -> resolve 成 diffuse/specular lighting
  -> temporal + spatial denoise
  -> 写回 SceneColor，并把 history 留给下一帧采样
```

## 本篇边界

本篇要讲透 MegaLights 自己的框架模型：它为什么存在、怎样把大量局部光源变成固定预算样本、这些样本如何跨过 shadow / VSM / RT / deferred lighting 边界，最后如何回到 `SceneColor`。

相邻系统只讲它们在这条主线中的角色：

| 概念 | 本篇讲到 | 深入出处 |
| --- | --- | --- |
| Classic deferred lighting | 只讲 sorted light range 的排除关系，以及为什么 MegaLights 灯不再被 classic `RenderLights` 逐灯处理 | 第 12 篇 |
| Shadow / Contact Shadow / 常规 ShadowDepth | 只讲 MegaLights 不按逐灯 shadow map 组织，而按 sample 组织遮挡证据 | 第 11 篇 |
| VSM | 讲 MegaLights sample 如何提前登记 VSM 页，以及 VSM trace 如何写回样本可见性 | 第 19 篇 |
| Lumen | 只讲它与 MegaLights 的 ownership 边界：Lumen 是间接光 / 反射查询系统，MegaLights 是直接光随机采样系统 | 第 17 篇 |
| 硬件 RT / 软件 SDF / screen trace 的底层实现 | 只讲它们作为 MegaLights shadow evidence 后端的角色 | Lumen / Ray Tracing 相关章节 |
| Volume / Hair 扩展 | 讲它们共享哪些固定槽位思想，以及 input domain、shadow backend、resolve/filter、output/history owner 的差异 | 体积、半透明、HairStrands 专题 |

读完本篇，你应该能回答：

- 为什么一盏灯被 MegaLights 接管后，classic deferred light loop 不应再处理它？
- “常数开销”到底固定了什么，哪些成本仍然可能随候选灯数量增长？
- `LightSamples` 和 `LightSampleRays` 在流程里从空槽位变成了什么？
- 随机采样如何偏向高贡献灯，同时避免一盏极亮灯霸占所有槽位？
- VSM 模式和 RT 模式改变的是哪一段，为什么它们不改变 MegaLights 的固定样本模型？
- resolve / denoise / visible light history 各自解决什么问题？
- 出现漏光、噪声、灯没生效或 VSM 页缺失时，应按什么顺序倒查？

## 1. MegaLights 接管独立的 direct-light ownership range

工厂里的 40 盏灯首先仍然来自第 12 篇讲过的同一套 light gathering 和 sorting。关键区别发生在排序阶段：UE 不等到 shader 里才判断“这盏灯是不是 MegaLights”，而是在 Renderer 的灯光路由表里提前把它们排成一个尾段。

这个尾段的概念很重要。`FSortedLightSetSceneInfo` 可以看作当前帧直接光照的路由表：前面是 simple、clustered、standard、unbatched 等 classic deferred 区间，最后才是 MegaLights 区间。`MegaLightsLightStart` 就是分界线。classic `RenderLights` 以这个分界为上限，MegaLights 自己消费分界之后的灯。

这一点对 Unity SRP 读者是个需要重新校准的习惯。Unity 里“这盏灯走哪条路径”常常是在 shader 或 C# pass 内临时分支判断的；UE 则把这个决定提前到 Renderer 的灯光排序阶段，让灯落进不同的区间。决定一次、排好序，后面的消费者各自只处理自己那段，不需要在每盏灯上重复判断“它是不是 MegaLights”。

这不是纯性能优化，而是所有权约束：

```text
一盏灯由 classic deferred 拥有:
  -> clustered 或 standard deferred 负责读 GBuffer、算 BRDF、写 SceneColor

一盏灯由 MegaLights 拥有:
  -> classic deferred 不再累加它
  -> MegaLights 先抽样，再 trace / resolve / denoise，最后统一写 SceneColor
```

如果这条边界不成立，同一盏灯就可能被算两次：一次在 classic deferred 里逐灯或 clustered 累加，一次在 MegaLights denoise 后统一累加。画面会变亮，调试也会混乱，因为你无法判断哪个系统拥有最终贡献。

### 一盏灯要进入 MegaLights，要连续通过五层资格门

“项目里打开了 MegaLights”只表达了一项请求，不等于当前 view 已经能运行，更不等于某一盏灯已经归 MegaLights。UE 把资格拆开，是为了让 shader permutation、项目默认、设备能力、单个 view 与单灯策略可以独立控制。若把这些层压成一个总开关，最常见的结果就是 UI 已打开却没有任何 sample pass，或者同一盏灯的 owner 被误判。

| 顺序 | owner / 判定层 | 必须成立的条件 | 失败后的有效输出与回落 |
| --- | --- | --- | --- |
| 1 | 编译与 shader platform | 非 Mobile；SM6、Wave Ops 与平台 ray-tracing 能力满足，相关 permutation 才会存在 | 没有 MegaLights shader 路径；后续不应查 sample buffer |
| 2 | 项目与设备策略 | `r.MegaLights.EnableForProject` 只为 `FinalPostProcessSettings.bMegaLights` 提供项目默认值，PPV 可以覆盖这个默认；`r.MegaLights.Allowed` 是 device profile / scalability 可施加的 veto | 默认值或 PPV 最终关闭时不请求 MegaLights；`Allowed=0` 时即使 PPV 打开也被否决 |
| 3 | 当前 view / owner request | `IsRequested` 读取 PPV 等来源合并后的 `FinalPostProcessSettings.bMegaLights`，并检查 `Allowed`、Lighting 与 MegaLights show flags；`IsEnabled` 还要求可用 HWRT，或项目显式允许且场景具备可用 SWRT / global distance-field 表示 | request 或 required tracing data 不成立时，该 view 不会把灯交给 MegaLights；灯应停在分界前，由 classic deferred 等其他 owner 接管 |
| 4 | per-light routing | light type 受支持、光源代理允许 MegaLights、shadow method 能解析成 RT 或 VSM；directional 由独立开关控制且默认关闭 | 该灯留给其他 owner；这是本帧分类，不是组件永久身份 |
| 5 | owner output | 通过前述条件的灯进入 sorted-light 的 `MegaLightsLightStart` 尾段；classic `RenderLights` 在分界前停止 | 灯仍位于分界前时由 classic deferred 等其他路径拥有，不能继续按 MegaLights 灯调试 |

这五层的输出不是同一种数据。第一层决定代码是否存在；第二层给出可被 PPV 覆盖的项目默认并允许设备策略否决；第三层用 `IsRequested + required tracing data` 决定当前 view 能否参与 owner 分类；第四层分类单灯；第五层发布最终 owner。最终 owner 输出只有两类：

```text
灯位于 MegaLightsLightStart 之前:
  -> classic deferred 等既有路径拥有它

灯位于 MegaLightsLightStart 之后:
  -> MegaLights 拥有它
  -> classic RenderLights 必须在分界前停止
```

尾段发布后还有一个更晚的**工作门**：`GenerateMegaLightsSamples` 入口会检查 `DirectLighting`。它不参与 `IsRequested` 或单灯 owner 分类，所以不能用“sample pass 没出现”反推 tracing 失败；灯既然已经位于尾段，required tracing data 已由 owner 输出证明成立。此时应检查 `DirectLighting`、deferred-lighting 调用条件以及 `GenerateMegaLightsSamples` 是否实际进入调度。

回到工厂案例：即使项目默认 `r.MegaLights.EnableForProject=0`，PPV 显式打开 MegaLights 后，合并得到的 `FinalPostProcessSettings.bMegaLights` 仍可为 true；只要 `Allowed` veto、Lighting / MegaLights show flags 与 tracing 数据资格都通过，这个 view 仍可能把合格灯交给 MegaLights。如果一盏 rect light 仍位于分界之前，最后有效状态是 **classic / other owner**：可能是 request、required tracing 或 per-light 条件未通过，不要继续查 MegaLights sample。如果它已位于尾段而 `GenerateMegaLightsSamples` 没有出现，最后有效状态已经是 **MegaLights owner 已发布**，下一步只查 `DirectLighting`、调用与调度；只有 sample 工作实际生成后，才进入槽位、shadow method 和 evidence 后端。

## 2. 固定预算建立在 sample domain 的固定槽位上

所有权切开后，MegaLights 先为当前 view 建立固定大小的采样域，再让 trace 与 shading 消费其中的 selected slots。这个采样域由 `FMegaLightsViewContext`（当前帧当前 view 的 MegaLights 工作台）管理。它把这一帧 MegaLights 需要的所有资源攥在一起：下采样后的 depth / normal、样本纹理、tile lists、上一帧 history 输入、resolve 输出和 denoise history。

这里有一个生命周期的关键区分，初学时容易混：

- **工作台本身只活一帧。** `FMegaLightsViewContext` 的生命周期只覆盖当前 RDG 图，帧末就消失。
- **history 跨帧存活。** 真正要留给下一帧的是 view state 里的 history；每帧进入 RDG 时它们被注册成 external resource，帧末再 extraction 回 view state。

这个“帧内临时 + 跨帧持久”的双层结构，和第 05 篇 RDG 的 external / extract 语义完全一致——你可以把它当作 RDG import/extract 模型的一个具体实例来读。

### 屏幕先被降采样，再乘上固定槽位数

默认设置下，GBuffer 输入会使用 2x2 half resolution。每个下采样像素保留固定的 sample 槽位数，默认是 4。于是 1920x1080 的 view 会变成大约 960x540 的下采样采样域；每个下采样位置有 4 个槽位。

可以把资源形态理解成这样：

```text
DownsampledSceneDepth:
  下采样位置对应的深度

DownsampledSceneWorldNormal:
  下采样位置对应的法线

LightSamples:
  每个槽位保存“选中了哪盏灯、权重、当前可见性”等压缩状态

LightSampleRays:
  每个槽位保存“这条样本射线怎样追踪、是否已完成、area light UV”等压缩状态

TileAllocator / TileData / IndirectArgs:
  告诉后续 pass 哪些 tile 需要被处理
```

先把几个新词放稳，后面读采样流程才不会把它们混成一团：

| 名字 | 它是什么 | 谁拥有 / 什么时候存在 | 调试时先问 |
| --- | --- | --- | --- |
| light candidate | 当前下采样点所在 light grid cell 里、可能影响这个点的一盏 MegaLights 灯 | 来自本帧 light sorting / forward light data；采样 shader 只枚举它，不拥有它 | 这盏灯是否在 MegaLights 尾段、是否在当前 cell、是否被 lighting channel 或低权重跳过？ |
| light sample | `LightSamples` 里的一个固定槽位，记录“最后选中了哪盏灯、估计权重、当前是否可见、是否受 history guiding 影响” | `FMegaLightsViewContext` 为当前 view 创建；帧内 RDG 资源，后续 trace / resolve 共同读写 | 槽位是否非空？选中的 `LocalLightIndex` 和权重是否合理？ |
| sample ray | `LightSampleRays` 里和 sample 对应的遮挡查询状态，记录 ray 进度、area light UV、是否完成 | 采样阶段写初始状态；VSM / screen / RT / software trace 后端推进它 | 样本是已经完成，还是还在等待 shadow evidence？area light UV 是否被 history 引导到错误区域？ |
| reservoir / sampler | shader 内的一份临时抽样账本，保存候选、随机量和累计权重，用来把任意候选压进固定槽位 | 只在 `GenerateLightSamplesCS` 当前像素的计算中存在；finalize 后就变成 `LightSamples` / `LightSampleRays` | 不要在后续 pass 里找 reservoir；后续能查的是它写出的 sample buffer。 |

这里有一个必须讲清的边界：**candidate domain 和 sample domain 是两种不同的工作域。** `GenerateLightSamplesCS` 仍要读取当前位置所在的 light grid cell，并遍历 candidate domain 中的 MegaLights 灯。工厂某个角落如果有 15 盏灯都影响同一个 cell，候选阶段就会看见这 15 盏。sample domain 则由下采样位置数乘每位置槽位数构成；默认 2x2 模式下，1920x1080 view 约有 960x540 个 sample positions，每位置 4 个 slots。无论 candidate domain 有 5、15 还是 50 盏灯，这个位置交给 selected-sample 后端的槽位上限仍是 4。

因此成本应按“是否受 slot 上界约束”分，而不应写成“弱线性层 / 固定层”后把所有屏幕工作都误认为常数：

```text
仍随场景与屏幕工作量变化:
  light grid cell 中候选灯枚举
  重要性权重计算
  reservoir 替换决策
  active tile 数、sample-domain 位置数
  hash 竞争、dispatch 与 history 有效性

受每位置 slot 上界约束:
  selected samples 的 shadow evidence 数量
  selected samples 的 BRDF shading 输入数量
  sample buffer 与 ray buffer 的每位置容量
```

resolve、visible hash、temporal / spatial denoise 虽然不再逐候选灯运行，但仍受分辨率、active tiles、配置和 history 状态影响。调性能时，若 light-loop iteration 随 cell 密度暴涨而 slots 始终是 4，最后有效状态在 candidate enumeration；若候选稳定但 trace / shade 工作量跟着 slot 配置变化，才是 sample-domain 预算问题。提高 slots 或关闭 downsample 能降低方差，却会同时增加 trace、shading、带宽和过滤成本；候选极少、强调确定性逐灯调试时，classic 路径反而更直接。

## 3. 重要性采样与 reservoir 把候选灯压入固定槽位

采样阶段先用一次较便宜的贡献估计给候选灯分配概率，再用加权蓄水池把任意数量候选压进固定槽位。它既不是不看贡献地“随机挑几盏灯”，也不会先为每盏灯完成最终遮挡与光照、再选结果最高的几盏；前者会让质量完全受偶然性支配，后者则无法约束贵后端成本。

对一个下采样像素，流程可以这样读：

```text
当前下采样像素
  -> 读取 GBuffer 材质状态
  -> 用 screen position + depth 找到 light grid cell
  -> 枚举 cell 里的 MegaLights 候选灯
  -> 对每盏灯估计 target PDF 权重
  -> 用 AddLightSample 维护固定数量槽位
  -> finalize 后写 LightSamples / LightSampleRays
```

### Worked case：15 盏候选灯怎样变成 4 个槽位

回到工厂案例，假设角色身边某个下采样点落在一个很挤的 light grid cell 里：这个 cell 里有 15 盏 MegaLights 候选灯，而当前配置只给这个下采样点 4 个 sample 槽位。

第一步，候选灯仍然会被逐个看见。采样 shader 会读取这个像素的 GBuffer 材质、法线、roughness、lighting channel 和 view 信息，再把每盏候选灯临时转换成“它对这个点大概值不值得抽”的权重。这里还没有最终阴影，也没有把 BRDF 结果写进 `SceneColor`；它只是为了决定抽样概率，尽量把槽位给当前像素更可能看得见、更可能有贡献的灯。

第二步，4 个槽位不是“按权重排前四名”的数组，而是一个正在被 reservoir 维护的固定预算。第 1 盏候选灯进来时，槽位大多还是空的；第 8 盏灯进来时，它可能因为权重较高替换掉某些槽位；第 15 盏灯进来时，它仍然有机会进入槽位，但机会取决于它相对累计权重的比例。这样做的调试含义是：你不能只问“这盏灯是不是第 5 名所以没进来”，而要问“它的候选权重、history 降权、随机序列和累计权重共同给了它多大进入槽位的概率”。

第三步，finalize 把 shader invocation 内的临时 sampler 落成真正的帧内数据。下面是**语义图**，用于说明状态变化，不是 C++ / HLSL 结构体逐字段布局：

```text
FLightSampler 临时状态:
  PackedSamples[]       每个槽位当前选中的 candidate
  LightIndexRandom[]    每个槽位被重标定的随机阈值
  WeightSum             当前 invocation 已接收候选权重的全局累计和

finalize 后的 LightSamples:
  LocalLightIndex       选中的灯
  Weight                estimator weight，不是 WeightSum 本身
  bVisible / bGuidedAsVisible

finalize 后的 LightSampleRays:
  area light UV、RayDistance、bCompleted 与输入域标记
```

对某个被选中的 candidate，先把它在 sampler 中保存的 candidate weight 记作 \(w_s\)，把所有候选累计和记作 \(W\)。GBuffer 主线 finalize 的核心重加权是：

```text
LightSample.Weight = W / w_s
```

所以 `WeightSum` 是抽样账本的全局累计量，selected candidate 自己的 weight 是分母，final `LightSample.Weight` 是用于估计总体贡献的反概率权重。三者不能混叫“reservoir 权重”。上式是没有 area-light guiding 修正时的基线；若 history guiding 把 UV 从均匀分布扭到上一帧更可能可见的象限，UE 会先按所选象限权重与四象限总和修正 selected weight，再让最终除法使用这个修正后的分母。这是在补偿被改变的 area-sample 分布，不是再次累加 `WeightSum`。

如果选中的灯不需要投影，ray 可以一开始就视为完成；如果它投影，`LightSampleRays` 会把这个槽位标成“还要 trace”。如果同一盏非 area light 连续占到相邻槽位，后续还会尽量跳过重复 ray，避免为同一条遮挡查询付两次成本。到这里，reservoir 已经消失，后面的 pass 不再关心它怎样替换；后面只读写 `LightSamples` 和 `LightSampleRays`。

所以这个 worked case 对调试很有用：**候选没进槽位**，先查 light grid、权重、history guiding 和 sample budget；**槽位里有灯但阴影错**，再查 `LightSampleRays` 和 trace 后端；**trace 完了但画面没贡献**，才进入 resolve / denoise。

### 权重代表“值得抽它”的概率代理

`GetLocalLightTargetPDF` 会用当前像素的材质、法线、粗糙度、相机方向和候选灯数据，做一次 MegaLights 版 lighting 估计。它关注的是这盏灯对当前点可能有多大贡献，而不是先做最终遮挡和完整滤波。权重会考虑亮度、pre-exposure、IES profile 等因素，然后做对数压缩。

对数压缩的设计目的很实际：如果完全按线性亮度抽样，一盏极亮灯会长期占满所有槽位，其他虽然暗但可见的灯几乎没有机会出现；如果完全均匀抽样，高贡献灯又会被浪费掉。对数权重让亮灯更容易被抽中，但不会以线性 HDR 亮度霸占预算。

### 加权蓄水池把任意候选压进固定槽位

`FLightSampler` 对每个下采样位置维护一份状态：当前槽位里已有的 packed candidates、每个槽位自己的随机阈值、以及所有已见候选共享的 `WeightSum`。每来一盏候选灯，sampler 先用旧累计和与新权重得到阈值比例，再增加 `WeightSum`；每个槽位独立重标定随机阈值，因此同一候选可能替换零个、一个或多个槽位。这仍然不是排序，也不保证四个槽位互不重复。

关键点是：槽位随机量不是每次重新抽，而是被重标定。这样一趟线性枚举就能维护多个固定槽位，不需要每个槽位单独再扫一遍候选列表。最终 finalize 用 `WeightSum / selected candidate weight` 得到 estimator weight，使后续 shading 能把“被抽到的少数样本”还原成对总体光照的估计。

这一节里最重要的数据状态变化是：

```text
空槽位
  -> 候选灯以权重概率进入槽位
  -> finalize 后变成带估计权重的 LightSample
  -> 如果灯投影，LightSampleRay 标记为未完成，等待 shadow evidence
  -> 如果灯不投影，样本可直接进入 shading
```

### 历史引导让随机性不是每帧从零开始

仅靠当前帧随机采样会有很高方差。MegaLights 还会读取上一帧的 visible light hash：如果某盏灯在上一帧对这个区域不可见，它在下一帧的抽样概率会被降低；如果 area light 上一帧只有某些 quadrant 可见，新一帧的 area light UV 也会被推向这些更可能可见的位置。

这不是当前帧遮挡真值，也不是 denoise；它只改变下一次抽样分布。camera cut、transform reset、没有有效 view state 或 history 被判无效时，当前帧必须能从当前候选重新建立。可以把 MegaLights 的 history 分成两套：

```text
visible light hash history:
  影响下一帧“抽哪盏灯、抽 area light 的哪块区域”

lighting history:
  影响 resolve 后的画面如何 temporal accumulate 和 spatial filter
```

混淆这两套 history 会导致调试方向错误。某些灯长期抽不到，先看 visible light hash / guiding；画面已经有样本但闪烁或糊，才看 lighting history / denoise。

## 4. 阴影后端按 selected sample 完成可见性证据

采样结束后，`LightSamples` 已经决定了“这个下采样位置要向哪些灯请求光照”。但其中有些样本还不知道自己是否被遮挡。MegaLights 的 shadow 阶段要做的事，就是把这些 selected samples 变成带可见性的样本。

这一步和第 11 篇的常规 shadow map 心智模型不同。普通 shadow map 通常先为一盏灯生产遮挡资源，再让 lighting 采样；MegaLights 则先选出本帧真正要查询的样本，再让 shadow 后端服务这些样本。

### VSM 模式：先标样本需要的页，再让 VSM 渲染

如果一盏 MegaLights 灯使用 VSM shadow method，采样阶段之后必须在 ShadowDepth 渲染前执行 sample-driven page marking。这里先沿用第 11 篇建立的 VSM 接口模型：VSM 不会先画完整阴影图，它要先知道本帧哪些虚拟页会被查询；第 19 篇再展开这份需求如何经过页表、物理池与缓存兑现。

MegaLights 的 VSM 路径可以拆成两个不同动作：

```text
Lighting 前、ShadowDepth 前:
  Compact 需要 VSM 的 sample traces
  MarkVSMPages
  -> 向 VSM 提交这些 selected samples 需要的 page requests

ShadowDepth 后、RenderMegaLights 内:
  Compact 仍未完成的 sample traces
  VirtualShadowMapTraceLightSamples
  -> 读取 VSM page table / physical pages
  -> 把可见性写回 LightSamples / LightSampleRays
```

把一个 VSM sample 单独拿出来看，它其实经历了两次不同的状态变化：

```text
mark 阶段之前:
  LightSample 已经有 LocalLightIndex 和 Weight
  LightSampleRay.bCompleted = false
  可见性还只是“等待回答”

mark 阶段:
  用 LocalLightIndex 找到这盏灯的 VSM id
  用 sample 对应的屏幕位置和 downsampled depth 还原接收点
  向 VSM 写 page request
  不回答这个 sample 是否被遮挡

trace 阶段:
  用同一个 sample 再去读 VSM page table / physical page
  根据 VSM trace 结果更新 LightSample.bVisible
  把 LightSampleRay.bCompleted 置为完成
```

这就是“sample-driven VSM”的教学重点：MegaLights 提供的是接收点需求，VSM 提供的是页表和物理页兑现。两者之间的握手点不是一张 per-light shadow mask，而是一批 selected samples。调试时，如果 `LightSampleRay` 还没完成，先看 compact trace list 和对应后端；如果它已经完成但 `bVisible` 错，再看 VSM page table / physical page / trace 参数；如果 sample 根本没生成，就不要从 VSM 开始查。

第一步不是 shadow lookup，而是需求登记；第二步才是遮挡查询。这个顺序如果反了，VSM 可能根本没有渲染这些样本需要的页，表现就是随机漏影或某些样本永远查不到有效页。

因此，MegaLights 与 VSM 的边界应这样理解：

- VSM 仍然拥有 page table、physical page pool、cache、Nanite / non-Nanite shadow rendering；
- MegaLights 只提供“我的 selected samples 会查询哪些页”；
- VSM trace 的结果写回 MegaLights 的 sample buffer，而不是写成 classic deferred 的 per-light shadow mask。

### Shadow evidence 是一条按 completed 状态推进的有序状态机

VSM、screen、HWRT 与 SWRT 不是四个任意并列、同时回答同一问题的插件。`RayTraceLightSamples` 按固定顺序反复 compact **仍未完成**的样本，让便宜或专用证据先回答，昂贵 world representation 只处理余下工作：

```text
selected sample: bCompleted = false
  |
  | 1. 若有 VSM sampling input
  v
VSM trace
  命中该 shadow method 的样本 -> 写 bVisible，置 bCompleted
  其他未完成样本 -> 留在后续 compact 输入
  |
  | 2. 若当前 input 支持且 screen trace 开启
  v
screen trace
  找到遮挡，或射线已到达灯 -> 完成样本
  证据不足 -> 推进 RayDistance，保持 bCompleted=false
  |
  | 3. 若 world-space trace 开启
  v
world trace
  有可用 HWRT -> 查询 ray tracing scene
  否则仅在显式允许且 global SDF 可用时走 SWRT
  |
  | 4. HWRT material mode = RetraceAHS 时
  v
material retrace
  对上一轮留下的材质不确定项再次 compact 并求值
```

每次 compact list 都是 owner 交接凭证：输入是上一步 `bCompleted=false` 的 rays，输出是下一后端真正要处理的间接工作列表。screen trace 未命中不能直接解释为“可见”；若它只是把 `RayDistance` 推进后仍保持未完成，world trace 才是下一个 owner。VSM 则只完成选择了 VSM shadow method 的样本，并依赖更早的 page marking / allocation / raster 已经兑现页证据。

这条顺序存在是为了避免后端重复工作：VSM 已回答的样本不应再做 world trace，screen 已给出充分证据的样本也不应进入昂贵世界查询。反过来，若把 screen miss 当作可见或把 SWRT 写成 HWRT 的无条件等价替代，就会在屏幕外遮挡、材质 alpha 与 representation 覆盖不足时得到错误可见性。

回到工厂：sample 已生成但下一次 compact 列表为空，可能表示 VSM 或 screen 已经把它完成，不是“trace 没跑”；screen 后 `RayDistance` 前进且 `bCompleted=false`，最后有效状态就在 screen evidence，下一 owner 是 world trace；world trace 后结果仍错，才检查 HWRT / SWRT representation 与 material retrace，而不是回到 candidate selection。

## 5. Resolve：把稀疏样本变成一张光照估计图

shadow evidence 写回后，样本已经有了灯、权重、可见性和 ray metadata。下一步不是直接写 `SceneColor`，而是先 resolve 成下采样 / 全分辨率相关的 diffuse、specular 和 confidence 信号。

`ShadeLightSamplesCS` 的职责是读取每个像素对应的 sample 槽位，把可见样本转成 lighting contribution。这里有两个设计点值得单独讲清。

第一，shader 会把同一个 `LocalLightIndex` 的多个样本合并。一个灯可能因为权重大而占据多个槽位；如果每个槽位都单独做一次 BRDF，就会浪费。MegaLights 会按 unique light 聚合 sample weight，然后对这盏灯做一次 lighting 评估。于是 BRDF 求值次数更接近“被抽到的不同灯数”，而不是槽位数。

第二，resolve 会输出 `ShadingConfidence`。它不是最终颜色，而是告诉 denoiser：当前像素的估计是否可靠。样本少、权重极端、历史刚失效的区域 confidence 往往低，需要更强的 temporal / spatial 稳定；confidence 高的区域可以少滤一点，避免过度模糊。

Resolve 之后，MegaLights 同时生成 visible light hash。它记录本帧哪些灯对哪些区域可见，以及 area light 哪些区域更可能可见。这份数据不是给当前帧画面看的，而是抽取到 view state，给下一帧采样阶段做 guiding。

因此 resolve 阶段有三种输出：

```text
ResolvedDiffuse / ResolvedSpecular:
  当前帧稀疏样本估计出的直接光

ShadingConfidence:
  当前估计的可靠程度

VisibleLightHash / VisibleLightMaskHash:
  下一帧采样分布的反馈
```

## 6. Denoise：随机估计必须跨时空稳定后才能写回 SceneColor

每个下采样位置只有固定几个样本，单帧必然有噪声。MegaLights 不能把 resolve 结果直接当作最终画面，而要先通过 temporal + spatial denoise 稳定。

时域阶段读取上一帧的 lighting history、depth / normal history、reprojection vector 和当前帧 confidence。它要回答的是：上一帧这个位置的 MegaLights 光照是否还能复用？如果相机切换、前一帧 transform reset、深度 / 法线不匹配，history 就不能强行累积，否则会 ghost。

空间阶段再根据深度、confidence、moments 和采样半径，在当前帧邻域内过滤。过滤后的结果通过 UAV 直接累加进 output color target。opaque 路径下，这个 output color target 就是主 `SceneColor`。

这个写回方式和 classic deferred 的逐灯 additive 很不一样：

```text
Classic deferred:
  每盏灯或每个 clustered pass 直接 additive 写 SceneColor

MegaLights:
  多盏灯先形成随机样本估计
  resolve 成一组 lighting textures
  temporal + spatial denoise
  最后一次性把整批 MegaLights 贡献写回 SceneColor
```

把工厂里的角色移动一步，就能看出 temporal reuse 的边界。上一帧角色没有遮住某盏 rect light，本帧角色肩膀挡住了它。MegaLights 此时有两套反馈要分开处理：

```text
visible light hash:
  记录上一帧某些区域哪些灯或 area light 象限可见
  下一帧采样时影响“还要不要把槽位给这盏灯 / 这块 area UV”
  它不直接给当前帧上色

lighting history:
  记录 resolve + denoise 后的 diffuse / specular / moments / frame count
  当前帧 temporal pass 用 reprojection、depth / normal 和 confidence 判断能否复用
  它解决的是画面稳定，不决定候选灯是否进入槽位

spatial denoise 输出:
  在当前帧邻域里根据 depth / confidence / moments 过滤
  最终通过 output color target 累加回 SceneColor
```

因此，一个移动角色带来的错误要按症状分流：如果被遮住的灯下一帧仍然频繁被抽到，先查 visible light hash / mask hash 和 history guiding；如果灯已经抽到且 resolve 有值，但角色边缘拖影，查 lighting history 的 reprojection、depth / normal 拒绝和 confidence；如果 resolve 图正常但画面仍没有 MegaLights 贡献，查 denoise 空间阶段是否写入了正确的 `SceneColor` target。

这条边界对 RenderDoc / RDG 调试很重要。你不应该期待每盏 MegaLights 灯都有一个独立的 lighting draw 写入 `SceneColor`；最终写入发生在 denoise 的空间阶段。如果 sample 和 resolve 都正常但画面没有贡献，下一步应查 denoiser 输出和 output color target，而不是回到 classic `RenderLight`。

“已经完成”必须说清证据深度。对 opaque GBuffer 主线，至少有四层不同完成状态：

| 完成深度 | 已经成立 | 还不能据此声称 |
| --- | --- | --- |
| RDG declared | sample、trace、resolve、denoise pass 与资源依赖已加入图 | GPU 已经写出结果 |
| GPU wrote resolve / denoise textures | 相应 pass 已执行并写出帧内纹理 | 主 `SceneColor` 已经收到贡献 |
| SceneColor UAV consumed | opaque spatial denoise 的 additive 输出已写入主颜色目标 | 下一帧 history 已可导入 |
| history extracted / next-frame imported | 资源被排队保留，并在下一帧重新注册为 external input | CPU 同步读到了纹理，或 GPU 已空闲 |

帧末，MegaLights 会把 lighting history、moments、accumulated frame count、visible light hash 等资源 extraction 回 `FMegaLightsViewState`。Queue extraction 建立的是跨帧保留合约，不是 CPU readback。下一帧 `FMegaLightsViewContext` 再把它们注册为 RDG external 资源。这就是 MegaLights 的跨帧闭环：

```text
本帧 sample / trace / resolve / denoise
  -> 写 SceneColor
  -> extraction history
  -> 下一帧 import history
  -> 影响采样分布和 denoise 稳定性
```

## 7. 和 Deferred Lighting、Shadow、VSM、Lumen 的边界

MegaLights 很容易被误解成“另一种 deferred lighting shader”或“Lumen 的直接光部分”。这两种说法都会把 ownership 搞错。

### 与 classic deferred lighting 的边界

第 12 篇的 classic deferred 仍然存在。它负责未被 MegaLights 接管的灯：clustered-supported 灯、standard deferred 灯、unbatched 灯等。MegaLights 只接管 sorted lights 中被标记的尾段，并用自己的 sample buffer、trace、resolve、denoise 写回 `SceneColor`。

所以直接光照调试要先问：

```text
这盏灯属于 classic deferred 还是 MegaLights?
```

属于 classic deferred，就按第 12 篇从 sorted range、light grid、RenderLight、BRDF 查；属于 MegaLights，就从 sample buffer、trace、resolve、denoise 查。

### 与 Shadow / VSM 的边界

第 11 篇讲的 ShadowDepth、CSM、Contact Shadow 和第 19 篇讲的 VSM 仍然拥有各自资源。MegaLights 不把自己变成一张传统 shadow atlas；它只让 shadow 后端服务 selected samples。

VSM 模式下，MegaLights 提供 sample-driven page requests，VSM 负责 page allocation、physical page 渲染和 page table 采样；RT 模式下，MegaLights 使用 ray tracing scene 或 software representation 为样本写可见性。两种路径最后都回到 sample buffer。

### 与 Lumen 的边界

Lumen 是间接光 / 反射的分层查询系统：Surface Cache、Screen Probe、Radiance Cache 和 reflections 各自有自己的 owner 和 history。MegaLights 是直接光随机采样系统：它从 direct local light 候选中抽样、追阴影、resolve 直接光并写 `SceneColor`。

它们可能共享某些底层概念，例如 RDG、HZB、screen trace、hardware RT 或 view state history，但共享基础设施不等于共享所有权。Lumen 的 Screen Probe 不会替 MegaLights 选择直接光样本；MegaLights 的 visible light hash 也不是 Lumen Surface Cache feedback。

这个边界能避免一类常见误判：如果很多局部灯的直接光噪声高，不要先去调 Lumen Surface Cache；如果红墙 GI 逐帧收敛慢，也不要先查 MegaLights sample budget。

### Volume 与 Hair 共享预算思想，但不是“只换采样域”

Opaque、Volume 和 Hair 都采用“有限槽位表示大量候选灯”的上层思想，但它们的输入形状、shadow evidence、resolve/filter、输出和 history owner 并不相同。把它们抽象成同一算法只换坐标，会承诺实际上不存在的 Volume screen/VSM trace、Hair VSM marking 与 Hair spatial reconstruction。

| 输入 | sample domain / 临时数据 | shadow evidence | resolve / filter | 最终输出与 history |
| --- | --- | --- | --- | --- |
| Opaque GBuffer | 下采样 2D 屏幕位置 × samples per pixel；`LightSamples` / `LightSampleRays` | VSM -> screen -> HWRT/SWRT 的有序状态机，具体分支受 shadow method 与运行资格控制 | opaque resolve；temporal + spatial 均可用 | spatial additive 写主 `SceneColor`；view state 持 lighting/hash history |
| Volume | 3D froxel / voxel × samples per voxel；独立 volume sample 与 ray 数据 | 专用 world-space HWRT 或 SWRT；不进入 2D VSM trace、screen trace 主线；Translucency Volume 的 software world path仍未实现 | 独立 sampling、resolve、hash 与 volume filter | 写 volume 自己的 3D lighting 数据并持相应历史，不直接复用 opaque `SceneColor` 写回合约 |
| Hair | 独立 `FMegaLightsViewContext`；hair visibility / sub-pixel 输入与独立 sample buffer | 可走适用的 trace 路径；Hair 的 MegaLights VSM page marking 尚未实现，不能把 Hair+VSM 当完整组合 | 独立 resolve；`SupportsSpatialFilter(HairStrands)` 为 false | 写 Hair 的 `SampleLightingTexture`；持独立 history，不以 opaque spatial pass 收束 |

这些差异来自输入与消费者合约，而不只是实现重复。Volume 需要在 3D 网格中重建世界位置并把结果交给体积积分；Hair 需要服务亚像素可见性和独立 lighting target。若强行共用 opaque 的 2D page marking 与 spatial filter，位置重建、遮挡表示和输出 ownership 都会错。

两个局部案例可以快速划清边界：

- opaque sample 已经标出 VSM 页，而 Hair sample 没有相应 page request，最后有效状态是 **Hair VSM marking 未实现**，不是整个 VSM pool 故障；此组合应改用当前可支持的 shadow method 或接受功能限制。
- Volume sample 已生成，但它不出现在 2D VSM / screen compact list 中，这是预期；下一 owner 是 volume world-space trace。若项目没有可用 HWRT，而 SWRT 又未显式允许或 representation 不可用，最后有效状态停在 Volume tracing qualification。

## 8. 调试主线：按 ownership -> budget -> sample -> trace -> resolve -> history 查

当 MegaLights 画面不对，最重要的是别跳级。按下面顺序查，可以避免在错误系统里消耗时间。

### 8.1 先确认 ownership

```text
编译平台是否为非 Mobile、SM6，并支持 Wave Ops 与 ray-tracing 能力?
EnableForProject 给出的项目默认是否被 PPV 覆盖，最终 FinalPostProcessSettings.bMegaLights 是什么?
Allowed veto、Lighting 与 MegaLights show flags 是否让 IsRequested 成立?
是否有 hardware RT，或显式允许 software tracing 且 global distance field 可用?
这盏灯是否允许 MegaLights?
Directional light 是否被默认排除?
Shadow method 解析成 RT 还是 VSM?
Sorted lights 中它是否落在 MegaLightsLightStart 之后?
Classic RenderLights 是否跳过了这段尾部?

尾段已经形成后，DirectLighting 是否允许 GenerateMegaLightsSamples 工作?
deferred-lighting 调用条件是否成立，GenerateMegaLightsSamples 是否进入调度?
```

如果 ownership 不成立，后面 sample / trace / denoise 都不会有这盏灯。最后有效状态若是 shader/platform gate，去查 permutation 与设备能力；request 或 required tracing data 不成立时，灯应留在分界前由其他 owner 接管。灯一旦已在尾段，tracing 资格就不再是待证条件；此时没有 sample pass，应查 `DirectLighting`、deferred-lighting 调用与 `GenerateMegaLightsSamples` 调度。

### 8.2 再确认预算和采样域

```text
DownsampleMode 是否导致采样域过粗?
NumSamplesPerPixel 是否过低?
Tile classification 是否跳过了本应处理的 tile?
LightSamples / LightSampleRays 是否创建在预期尺寸?
```

噪声高时，不要只看 trace。先确认你给了多少槽位，以及这些槽位覆盖的是哪个下采样域。

### 8.3 检查候选枚举和 sample 选择

```text
目标像素所在 light grid cell 中有多少 MegaLights 候选?
候选灯是否通过 lighting channel?
权重是否被 MinSampleWeight 跳过?
某盏灯是否因上一帧不可见被 history guiding 降权?
area light visible mask 是否把 UV 推向错误区域?
```

某些灯很少出现，通常是候选、权重或 history guiding 的问题；不是 shadow 后端的问题。

### 8.4 检查 shadow evidence

VSM 模式：

```text
sample-driven MarkVSMPages 是否发生在 ShadowDepth 前?
是否 compact 到非空 trace list?
VSM page requests 是否被 allocation 成 physical pages?
ShadowDepth 是否渲染这些 pages?
VirtualShadowMapTraceLightSamples 是否把可见性写回 sample?
```

RT / screen / software 模式：

```text
VSM 已完成的样本是否在下一次 compact 中被排除?
screen trace 是否启用，最大距离是否合理，miss 后 bCompleted 是否仍为 false?
screen 未完成样本的 RayDistance 是否交给 world trace?
hardware RT 是否可用，inline / RayGen 路径是否符合平台?
没有 HWRT 时，software RT 是否被显式允许且 global distance field 存在?
RetraceAHS 模式是否生成 material-retrace compact list?
```

### 8.5 检查 resolve、denoise 和 history

```text
ShadeLightSamples 是否写了 resolved diffuse / specular?
ShadingConfidence 是否异常低?
VisibleLightHash 是否生成并 extraction?
Temporal history 是否因 camera cut / prev transform reset 失效?
Spatial filter 是否过强导致糊，或过弱导致噪声残留?
Denoise 空间阶段是否写入正确的 SceneColor target?
```

把这些步骤连起来，就是本篇的完整调试线：

```text
这盏灯归谁?
  -> 预算域是否存在?
  -> 候选是否进入采样?
  -> 样本是否得到遮挡证据?
  -> 样本是否 resolve 成 lighting?
  -> denoise 是否写回 SceneColor?
  -> history 是否正确反馈到下一帧?
```

用“最后有效状态”收束时，按下面的 owner 边界停住，不要跨层猜：

| 已确认的最后状态 | 下一 owner / 检查点 | 不应先做 |
| --- | --- | --- |
| 当前 view 的 MegaLights gate 为 false | 编译、项目/设备、view、tracing representation | 查 reservoir 或 denoiser |
| 灯已在 MegaLights 尾段，但 sample pass 未生成 | `DirectLighting`、deferred-lighting 调用条件、`GenerateMegaLightsSamples` 调度 | 回查 tracing representation |
| 灯已在 MegaLights 尾段，candidate 不存在 | light grid cell、lighting channel、per-light 数据 | 查 shadow backend |
| candidate 有权重但未进入 4 slots | sampler 阈值、`WeightSum`、history guiding、slot budget | 查 VSM page |
| slot 已写，ray 未完成 | VSM -> screen -> world 的 compact/completed 状态机 | 查 resolve |
| ray 已完成，resolved lighting 异常 | visibility、estimator weight、unique-light merge | 调 history rejection |
| resolve 正常，denoise texture 异常 | temporal rejection、spatial filter与 input support | 回查 candidate |
| denoise texture 正常，主画面无贡献 | opaque `SceneColor` UAV 输出；Hair 则查自己的 target | 宣称 history 已坏 |
| 当前帧正确，下一帧闪烁 | extraction、next-frame import、visible 与 lighting 两套 history | 把 QueueExtraction 当 CPU 完成 |

## 9. 收束：MegaLights 把直接光照改写成一个随机估计问题

现在可以把本章压成一句话：

> **MegaLights 不试图让每盏灯都更便宜，而是改变直接光照的工作单位：先把很多候选灯压缩成固定数量的随机样本，再让 shadow、shading、denoise 服务这些样本。**

这条设计让 UE 能把大量局部光源从 classic deferred 的逐灯成本里移出来。它不是完全不受灯数影响：candidate enumeration 仍依赖 light grid 密度，屏幕域工作仍依赖分辨率、active tiles 与 history 状态。它真正固定的是每个 sample-domain 位置的槽位数，以及这些 selected slots 驱动的贵后端上界；再通过重要性权重、加权蓄水池、visible light history、temporal / spatial denoise 控制方差。

完整主线如下：

```text
GatherAndSortLights
  -> MegaLights 拿走 sorted light 尾段

GenerateMegaLightsSamples 入口
  -> DirectLighting 与 deferred-lighting 调用 / 调度条件通过后才生成 sample 工作

FMegaLightsViewContext::Setup
  -> 建下采样采样域和固定槽位 LightSamples / LightSampleRays

GenerateLightSamplesCS
  -> 从 light grid 候选中按贡献概率抽样
  -> 固定槽位写入 sample buffer

MarkVSMPages / RayTraceLightSamples
  -> 按 VSM -> screen -> HWRT/SWRT 顺序 compact 未完成样本
  -> RetraceAHS 条件下继续 material retrace

ShadeLightSamplesCS / VisibleLightHash
  -> 稀疏样本 resolve 成 diffuse/specular/confidence
  -> 本帧可见灯反馈给下一帧采样

DenoiseLighting
  -> opaque 路径 temporal + spatial 稳定后 additive 写 SceneColor
  -> Hair 写独立 SampleLightingTexture，Volume 写独立 3D 输出
  -> history extraction / next-frame import 形成跨帧闭环
```

下一篇第 19 篇会把 MegaLights 的 VSM 部分作为“sample-driven page request”的一个来源，继续展开页表、物理池与缓存；回看第 12 篇 Lighting 时，则应把 MegaLights 视为 classic deferred light routing 的一个排除尾段。这样 ownership 不会混，调试路径也不会乱。
