# 11 Shadows 阴影系统

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `01_Architecture.md`、`02_SceneProxy.md`、`03_ThreadModel.md`、`04_RHI.md`、`05_RenderGraph.md`、`06_GPUScene.md`、`07_MeshDrawCommand.md`、`08_FrameInit.md`、`09_DepthPrepass.md`、`10_BasePass.md`。  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见 `11_Shadows_CoverageMatrix.md`

## 开篇：阴影不是“画一张图”，而是为 Lighting 建立可消费的遮挡合约

BasePass 结束后，Renderer 已经拥有可由后续 RDG pass 读取的 `SceneDepth`、GBuffer 与其他 `SceneTextures`。但这些资源只描述“相机看到的表面是什么”，还没有回答直接光照必须知道的另一个问题：

> **对当前屏幕像素而言，从接收点到这盏灯的路径是否被遮挡？**

初学者最容易把答案压缩成一句话：从灯看场景，画一张 shadow map，然后在 Lighting 中比较深度。这个模型能解释常规 shadow map 的核心数学，却解释不了 UE 为什么还要区分：

- 一盏灯本帧是否应创建阴影工作；
- 一个 projected shadow 是否与某个 view 相关；
- 哪些 primitive 是 shadow caster，而不是主相机可见物；
- 静态环境与动态物体分别由谁更新；
- 深度应该写入 atlas、cubemap、preshadow cache，还是 VSM physical pool；
- 深度写完之后，谁把它投影和过滤成当前灯的 attenuation；
- RDG pass 已声明、RHI 已记录、平台队列已提交和 GPU 已完成消费分别意味着什么。

因此，本章不用“四种阴影技术并列介绍”的方式组织内容，而沿贯穿案例的一条生产—消费链前进。下一节先展开整条路线，再逐段解释 owner、产物、消费者与生命周期。CSM、VSM、MegaLights、Contact Shadow、ray traced shadow，以及 whole-scene、per-object、preshadow 等名词，都要放回这条链上理解：它们要么改变 request 的路由，要么改变资源与 producer，要么改变 Lighting 获得 attenuation 的方式。

## 贯穿案例：太阳、静态建筑、动态树与一个地面像素

全章只让一条案例承担主线：

> 一盏 Directional Light 照射一栋静态建筑、一棵随风运动的 movable tree 和地面。我们选定地面上一个被树遮挡的像素，从这盏灯提出阴影需求开始，一直跟到 Lighting 得到该像素的 shadow terms。

为了让每个阶段都能落到具体状态，先给四个对象固定职责：

| 对象 | 本章中的作用 |
|---|---|
| Directional Light | 发起大范围直接光照与阴影需求 |
| 静态建筑 | 代表低频变化、适合缓存或预计算的遮挡几何 |
| 动态树 | 代表每帧 transform 或 instance 数据可能变化的 caster |
| 地面像素 | 最终 receiver；Lighting 要判断它到太阳是否被树挡住 |

后面每增加一个 UE 名词，都要回答它让这四个对象中的哪一份数据、所有权或控制状态发生了变化。

## 阅读路线：同一份遮挡状态怎样走到 Lighting

先把主案例按生产—消费关系展开。后文每一节负责解释表中的一段；读者先看到每一段生产什么、交给谁，再在章末用完成深度表判断证据停在哪里。

| 阶段 | Owner / control | 主案例中的产物 | 下一消费者 |
|---|---|---|---|
| Light consideration | scene renderer / light visibility | 当前 view 的太阳灯候选 | per-light occlusion route |
| Occlusion route | per-light route | regular Shadowmap、VSM、MegaLights 或 RT 的责任选择；本例先走 CSM regular | 对应路线的 request producer |
| CSM request | Directional Light + view | view-depth 区间与 cascade 2 的请求，目标地面像素和树落入该区间 | frame shadow setup |
| Projected contract | frame shadow setup | `FProjectedShadowInfo` 保存 cascade 2 的 matrices、bounds、类型与帧内状态 | per-view projected visibility |
| Per-view visibility | visible-light/view data | cascade 2 对当前 view 的消费资格 | caster gather 与资源准备 |
| Caster gather | shadow setup tasks | 动态树因 shadow relevance 和空间交集进入 subject primitives | mesh selection / cache policy |
| Static/dynamic policy | scene cache + frame projected shadow | 建筑归 static cached layer，树归 movable layer | allocation 与本帧 producer |
| Resource allocation | sorted shadow resources | cascade 2 的 atlas rect 与 border；VSM 分支则建立 mapping | depth producer |
| Mesh selection | shadow pass setup | 树按 mesh/VF 能力进入 regular、VSM non-Nanite 或 Nanite producer | shadow draw command formation |
| Draw command formation | mesh pass | shader、pipeline、instance range 与 shadow view 配方 | GPUScene input 与 instance culling |
| GPUScene input | scene GPUScene + shadow view | 当前树 transform、instance data 与 view-local culling 输入 | shadow culling / raster |
| Instance culling | shadow culling context | 树实例对应的实际 draw 参数 | RDG depth producer |
| RDG depth producer | Render Graph | depth pass、目标资源和 producer→consumer 依赖 | RHI / platform command formation |
| GPU depth | depth target / pool | cascade 2 atlas 中的树深度；建筑内容可来自有效 cache | projection / filtering |
| Projection/filter | shadow projection | 地面 receiver 对应的 attenuation 或 packed mask | per-light Lighting shader |
| Lighting consumption | per-light shader | `GetShadowTerms` 所需的遮挡项 | 本帧后续合法 GPU consumer |
| Final completion | GPU queue/fence + resource system | 覆盖最后 consumer 的完成证据 | 资源退休、覆盖或复用 |

本章会统一使用八层完成深度：`RDG work declared -> RHI recorded -> Platform commands formed -> Platform Queue Submit -> GPU shadow resource produced -> Projection/filter output produced（分支适用时） -> Lighting consumed -> Final consumer completed`。这里先把它们当作主链上的不同交接点；各层的严格证据和资源退休规则集中放在第 13 节。

## 本篇边界

本章讲清阴影如何接入一帧渲染框架，重点是 producer/consumer 合约、资源生命周期和调试证据。

本章会讲：

- shadow request 怎样根据 light、view、平台和 occlusion route 成立；
- `FProjectedShadowInfo` 怎样描述 projected-shadow 家族的帧内工作；
- light visibility、projected-shadow visibility、caster relevance 为什么必须分开；
- CSM 怎样把 Directional Light 的有限采样密度分配给多个深度区间；
- whole-scene、per-object、preshadow 与 static/dynamic/precomputed 的关系；
- regular atlas、cubemap、preshadow cache 与 runtime cache mode；
- ShadowDepth 怎样准备 mesh pass、GPUScene 数据并声明深度 producer；
- projection/filtering 怎样把光空间深度变成 Lighting 可读的 attenuation；
- VSM、MegaLights、Contact Shadow 与 RT shadow 在主链上的接口分支；
- 如何使用第 13 节定义的八层完成深度，区分从 RDG 声明到最后 consumer 完成的每次控制与资源交接。

本章不展开：

- VSM page table 编码、clipmap 内部布局、完整失效算法与采样核，留给第 19 篇；
- MegaLights reservoir、时域复用和 denoise 算法，留给第 18 篇；
- Nanite cluster shadow raster 的内部算法，留给第 16 篇；
- BRDF 与最终 `SceneColor` 光照积分，留给第 12 篇。

## 1. Request 与 Route：先决定由哪套责任系统提供遮挡

### 1.1 request 是什么

Shadow request 不是一张纹理，也不是一个 draw call。它表示 Renderer 已经判断：

1. 当前 view 需要考虑这盏灯；
2. 灯允许产生相应遮挡；
3. 当前平台和 shading path 支持候选方案；
4. 应把遮挡责任交给 shadow map、ray tracing 或 MegaLights 等路径之一。

这一步的输入包括 light type、cast flags、mobility、view family、平台能力、ray tracing 条件和 MegaLights 设置。其 owner 是本帧的 scene renderer 与 light scene info；其 lifetime 是本帧的路由决策，后续 projected shadow、trace 或 sample 工作都依赖它。

UE 不能对每盏灯无条件建立 shadow view、遍历 caster、分配资源并运行 projection。这样虽然逻辑简单，却会让 CPU setup、atlas 空间、GPU raster 和屏幕 projection 成本随灯数失控，还会生产没有任何 view 消费的资源。

因此：

- **硬约束**：Lighting 只能消费由合法 producer 生成、且与当前 light/view 匹配的遮挡数据。
- **UE 工程选择**：用 `ELightOcclusionType`、排序区间、show flag 和 CVar 把路径提前路由。
- **替代方案**：全部灯固定走 shadow map，适合小型、灯数受控的 renderer；全部走 RT 可以减少 shadow-map 管线，但要求 RT scene、平台与 trace 预算；完全预计算适合不发生运行时变化的场景。

最小源码定位锚点是 `GetLightOcclusionType` 和 `FSceneRenderer::CreateDynamicShadows`。前者把当前灯交给具体遮挡责任系统，后者在 shadow-map 路由中创建后续 projected-shadow 工作。route 的产物正是下一阶段能够继续展开的 producer contract。

### 1.2 Renderer 怎样把 Cast 意图解析成实际路线

Cast flag 只是 request 的一个输入。实际路径还受以下条件影响：

- light 是否在至少一个 view 中需要考虑；
- occlusion route 是否被 MegaLights 或 ray tracing 接管；
- light mobility 与预计算阴影是否有效；
- point/spot shadow 在当前平台和项目设置中是否受支持；
- forward shading、mobile path 与 deferred path 是否允许相同资源形式；
- shadow quality、show flag 或调试开关是否禁用了动态阴影。

Forward shading 是重要的平台边界：UE5.7 的该路径会更早渲染 regular shadow maps，并明确不支持 VSM。Deferred 路径则可以在 BasePass 后利用 `SceneDepth`、GBuffer 和其他输入完成 VSM page request，再渲染 shadow depth。这里的顺序差异是 consumer 输入不同造成的，不是单纯的代码排列差异。

### 1.3 主案例：太阳灯首先得到什么

太阳灯通过 light 可见性与 cast 条件后，route 把遮挡责任交给一个具体 producer 家族。本例先采用 `Shadowmap`：route 输出太阳灯、当前 view 和 shadow-map 责任的组合，下一阶段据此建立 CSM request 与 projected contract；随后 caster gather、allocation、depth producer 和 projection 会依次消费上一阶段的产物。

如果项目启用并支持 VSM，Directional Light 的 shadow-map 路由会把 request 交给 VSM/clipmap 相关 projected work；如果走 RT，下一位 producer 则读取 RT scene 执行遮挡查询。route 的职责是选定接力者，而不是提前完成后续 producer 的工作。

---

## 2. 从 Light 到 View 再到 Primitive：三层判断怎样缩小工作集

阴影最容易漏掉的几何事实是：**主相机看不见一个物体，不代表这个物体不能把阴影投到相机看得见的地方。**

因此 UE 至少要分开三类判断。

### 2.1 Light visibility：当前 view 是否需要考虑这盏灯

这是最外层判断。scene renderer 结合灯的影响范围、show flag、距离和 view 条件，为当前 view 产生本帧 light candidate。per-light route 消费这份候选，选择遮挡责任系统，并据此创建 projected、sample 或 trace 工作。

### 2.2 Projected-shadow visibility：某个投影工作是否与当前 view 相关

一盏灯可以创建多个 projected shadows，例如多个 CSM cascade、point light cubemap、per-object shadow 和 preshadow。它们不一定都被每个 view 消费。

UE 把灯级 projected-shadow 列表与 view 级可见性分开保存：

- `FVisibleLightInfo::AllProjectedShadows` 表示这盏灯本帧已经建立的 projected-shadow 对象；
- `FVisibleLightViewInfo::ProjectedShadowVisibilityMap` 表示这些对象对某个 view 是否可见；
- view relevance 还记录 opaque、translucent、shadow relevance 等使用条件。

`InitProjectedShadowVisibility` 会结合 dependent view、stereo 关系、view relevance 和历史 shadow occlusion query 判断可见性。VSM projected shadow 会跳过某些传统历史 shadow occlusion query，因为错误沿用旧查询可能让仍需维护的缓存页被错误淘汰。

这里的硬约束是每个 consumer view 必须只使用与自己匹配的投影参数。用 bit map 和历史 query 降低 CPU/GPU 工作，则是 UE 的工程选择。替代方案是每个 view 无条件处理全部 projected shadows；它更直接，但会浪费 projection、mask 带宽和 caster setup。

### 2.3 Caster relevance：哪些 primitive 可能写入这个 shadow view

Projected shadow 对 view 可见之后，系统仍要找出可能影响该投影的 caster。这个集合不能直接复用主视图可见 primitive 列表，因为 caster 可以位于屏幕外、相机背后或被主相机遮挡，只要它的投影仍能落到可见 receiver 上。

UE 可以利用 light-primitive interaction、包围体、空间结构、frustum 与 view relevance 缩小候选。替代方案是每个 shadow view 遍历全场景；它在正确性上可行，但会增加 CPU culling、动态 mesh 收集和后续 draw setup。

### 2.4 主案例：屏幕外的树仍可能投影到屏幕内地面

假设风把树冠吹出主相机边缘，但树干投下的阴影仍延伸到屏幕内的地面像素：

1. 主视图 primitive visibility 可以把树判为不可见；
2. 太阳灯与负责该深度区间的 cascade 仍对当前 view 可见；
3. 树的 bounds 与 shadow caster 条件使它进入该 cascade 的 subject primitive 集合；
4. 后续 ShadowDepth 仍必须为树建立 shadow draw。

如果把第 1 步直接当作第 3 步，阴影会在物体离开屏幕时提前消失。反过来，如果无条件把全部场景 primitive 都塞进每个 cascade，结果通常正确，但成本不可控。

因此三层判断形成一条生产链：

```text
light visible
  -> 为当前 view 产生 light candidate

projected contract exists
  -> 保存某个投影工作的矩阵、范围、类型与资源需求

projected-shadow visibility = true
  -> 把该投影交给匹配的 consumer view

subject primitive exists
  -> 为 mesh selection 和 ShadowDepth 提供 caster 候选
```

每一层都缩小下一位 producer 的输入，同时保留屏幕外 caster 这类正确性要求。这样既不会把主视图 visibility 错当 caster visibility，也不必让每个 shadow view 无条件遍历全场景。

---

## 3. `FProjectedShadowInfo`：projected-shadow 家族的帧内合约

### 3.1 它解决什么问题

CSM cascade、spot shadow、one-pass point light shadow、whole-scene shadow、per-object shadow、preshadow，以及部分 VSM 接入，都要共享一组基本事实：

- 从哪个 shadow view 观察；
- caster 和 receiver 的空间边界是什么；
- 深度投影矩阵如何定义；
- 分辨率与 atlas 区域是多少；
- 是否已分配、是否使用缓存、属于哪种 cache mode；
- 后续 depth producer 与 projection consumer 应读取哪些状态。

UE 用 `FProjectedShadowInfo` 把这些事实组织成一个帧内工作合约。它不是 shadow texture，也不是所有阴影技术的统一对象。Contact Shadow 的 shader-local ray、MegaLights 的 sample/trace context 和纯 RT shadow 不需要被强行解释成同一个对象。

### 3.2 owner、数据、控制与生命周期

| 维度 | 含义 |
|---|---|
| Owner | Renderer 侧的本帧 shadow setup；缓存型 preshadow 可通过引用跨越单帧临时分配 |
| Data | shadow matrices、caster/receiver bounds、cascade settings、分辨率、atlas 坐标、border、render targets、cache mode、类型 flags |
| Producer | whole-scene、per-object 或 clipmap setup 路径 |
| Consumer | caster gather、shadow mesh pass setup、ShadowDepth、shadow projection |
| Control | whole-scene/per-object/preshadow/VSM、allocation 与 cache flags |
| Lifetime | 从 projected-shadow 创建开始，持续到 projection 消费结束；不能在 depth 写完时就视为无用 |

`SetupWholeSceneProjection`、`SetupPerObjectProjection` 与 `SetupClipmapProjection` 是最小定位锚点。它们说明不同 shadow 类型用不同初始化条件填充同一公共合约，但不表示每个字段对每种类型都有效。

### 3.3 为什么 UE 选择一个较大的公共对象

生产者和消费者共享同一投影参数是硬约束，否则 depth 写入与 projection 读取会使用不同空间。把公共状态集中在一个大型对象中，则是 UE 的工程选择。

这个选择的收益是：whole-scene、per-object 和多种投影能复用 visibility、allocation、mesh pass 与 projection 基础设施。代价是对象字段很多，阅读时容易误以为每个 flag 都适用于所有类型。

可替代设计包括：

- 每种 shadow type 一个小对象：边界清晰，但公共逻辑重复；
- 纯数据组件组合：更灵活，但调度和有效性组合更复杂；
- 完全不同的 producer graph：适合与 shadow map 差异极大的 RT 或 sample-driven 路径。

因此调试时应先问“这个字段对当前 projected-shadow 类型是否成立”，而不是看到对象里有字段就认为它已被初始化。

### 3.4 主案例：第二个 cascade 的合约逐步成立

假设地面像素和动态树都位于太阳的第二个 cascade：

1. CSM split initializer 给出该 cascade 的 view-depth 区间与 bounds；
2. `FProjectedShadowInfo` 保存该 shadow view 的投影、bounds 和类型状态；
3. 分配前，`X/Y` 还没有 atlas 意义；
4. caster gather 后，树进入 subject primitive；
5. allocation 后，`X/Y/Resolution/BorderSize` 才指向实际 atlas 区域；
6. depth producer 把树写入该区域；
7. projection consumer继续使用同一个投影合约，把该区域映射到地面像素。

这条链让 `FProjectedShadowInfo` 从路线选择逐步获得 caster、atlas 区域和深度内容：`bAllocated` 管资源位置，caster list 管几何输入，depth producer 管资源内容，projection 再消费同一投影合约。每个字段都服务下一位 owner，而不是用一个总开关概括全部进度。

---

## 4. CSM：把有限 shadow texel 分给不同 view-depth 区间

### 4.1 CSM 解决的是采样密度，不是“太阳有没有阴影”

Directional Light 覆盖范围极大。固定分辨率 shadow map 如果同时覆盖脚边与远山，每个 texel 对应的世界空间面积会非常大，近处阴影失去细节。

这是有限采样密度带来的硬约束。CSM 的工程选择是沿相机 view depth 把范围切成多个 cascade：近 cascade 覆盖范围小、精度高；远 cascade 覆盖范围大、精度低。它不是唯一解：

- 单张大图实现简单，但近处精度差；
- 更多 cascades 提高分配自由度，却增加 depth 与 projection 成本；
- VSM/clipmap 用更细粒度的虚拟资源管理换取复杂性；
- ray traced shadow 避开 cascade，但需要不同的平台与 trace 预算。

### 4.2 request 成立的条件

CSM 不是看到 Directional Light 就无条件创建。其 request 会受以下条件影响：

- light 是否投动态阴影；
- light mobility 与预计算阴影有效性；
- current view 允许的 cascade 数；
- dynamic shadow distance、far shadow distance 与 far cascade count；
- static lighting 是否允许，未构建或 channel overflow 是否需要 runtime fallback；
- mobile、forward 或 deferred path 的能力边界。

因此“太阳使用 CSM”是当前配置下的路径结论，不是所有 Directional Light 的硬规则。

### 4.3 四步建立一个 cascade

**第一步：确定 cascade 数量。** 近 cascade 数受 light 设置与 view 上限约束；far cascades 还有独立数量和距离限制。

**第二步：计算 split distance。** UE 使用 distribution exponent 把精度偏向近处，而不是简单线性均分。预计算光照无效时，UE5.7 会使用不同的有效 exponent，因此编辑器未构建状态与最终有效预计算状态的 split 分布可以不同。

**第三步：建立 transition 与 fade。** 相邻 cascade 的投影与采样密度不同，硬切会形成接缝。过渡区允许 attenuation 在一段深度范围内平滑交接。

**第四步：构造稳定 bounds。** UE 从 split frustum 拟合包围球，再建立 shadow projection。包围球不是数学上的唯一选择；它牺牲一部分贴合度，换取相机轻微移动或旋转时更稳定的投影，减少 shimmering。

最小定位锚点是 `GetEffectiveCascadeDistributionExponent`、`GetSplitDistance` 与 `GetShadowSplitBounds`。

### 4.4 主案例：树落入第二个 cascade

当第二个 cascade 的 split 与 bounds 覆盖树和目标地面像素时，只能证明这份空间 request 合法。下一步还必须分别证明：

- projected shadow 对当前 view 可见；
- 树具有 shadow relevance；
- 树进入 subject primitive 集合；
- atlas/VSM 资源已分配；
- 树的动态 transform 被 shadow view 使用；
- projection 对地面像素读取正确 cascade 与 fade。

### 4.5 常见症状回到哪一层

| 现象 | 优先检查 |
|---|---|
| 近处抖动 | bounds 稳定策略、split 分布与 texel 密度 |
| cascade 接缝明显 | transition/fade 与相邻 projection |
| 远处突然消失 | shadow distance、far cascade 条件与 fade |
| cascade 覆盖正确但树无影 | caster relevance、cache layer、mesh pass 与 depth write |
| depth capture 正确但屏幕阴影错 | projection matrix、atlas 区域、fade 与 filter |

---

## 5. Static、Dynamic、Precomputed 与三类 projected shadow

“static”与“dynamic”在阴影讨论中经常指向不同层级。先把它们拆开：

- **Light mobility / primitive mobility**：场景对象是否允许运行时变化；
- **Precomputed shadowing**：离线构建并在运行时读取的遮挡信息；
- **Runtime cached static depth**：运行时 shadow map 中，把低频变化 primitive 的深度跨帧复用；
- **Dynamic layer**：当前帧必须根据 movable caster 或变化状态更新的深度。

Runtime cached static depth 不等于 baked lighting。前者仍是运行时 shadow producer 的资源，只是更新频率较低；后者来自预计算管线，消费接口与失效条件不同。

### 5.1 Whole-scene shadow

Whole-scene shadow 的空间范围由灯和 view 决定，不围绕单个 receiver 建立。典型例子是 movable Directional Light 的 CSM，或 movable point/spot light 的整灯阴影。

它适合：

- 许多 caster 共同影响大范围 receiver；
- 需要统一 shadow view 和资源分配；
- 逐对象建立投影会产生过多 setup 与 projection。

代价是范围大、caster 多，若每帧完全重画，CPU/GPU 成本高。因此 UE 会进一步使用 runtime cache mode 拆分静态与动态更新责任。

### 5.2 Per-object shadow

Per-object shadow 围绕某个动态对象或对象组建立投影。经典教学方向是“动态角色向环境投影”。它只覆盖相关对象与 receiver 范围，可以在 whole-scene shadow 不合适或不可用时提供局部动态阴影。

它的收益是局部、分辨率可集中；代价是对象越多，projected shadow 数、allocation 和 projection 次数越多。因此它不是“比 whole-scene 更精确所以总是更好”，而是为少量重要动态对象换取更高的每对象管理成本。

### 5.3 Preshadow

Preshadow 是一种 per-object projected shadow，用来表达**静态环境向动态 receiver 投影**。例如静态建筑或地面遮挡一个移动角色。它与“角色向世界投影”的 per-object shadow方向相反。

Preshadow 适合缓存，因为静态环境的 caster 深度在 receiver 小范围移动时可能仍可复用。UE 会尝试从 preshadow cache 复用已有深度；只有缓存不存在、失效或不覆盖新状态时才需要重新生产。

这里的硬约束是 receiver 或 caster 变化后不能继续使用不匹配的旧遮挡。选择 preshadow cache、缓存尺寸与复用策略则是 UE 的工程选择。全部实时重画更容易保证正确性，但成本更高；全部预计算更便宜，却不能支持动态 receiver 的任意变化。

### 5.4 主案例：静态建筑与动态树如何分工

在太阳 whole-scene shadow 中：

- 静态建筑属于低频变化 caster，可以进入 static cache layer；
- 动态树每帧 transform 可能变化，必须进入 movable/dynamic layer；
- 地面像素最终看到的是 projection 阶段对这些有效深度的组合，而不是某一个 layer 单独代表完整阴影。

后续帧先用 initializer 验证建筑的 cached shadow map 与当前 light/view 是否匹配；匹配后保留 static layer，同时让动态树用当前 transform 更新 movable layer。组合阶段把两层交给 projection，projection 再读取同一代资源版本。树影停在旧位置时，这四步正好给出从 cache validity 到 receiver attenuation 的排查顺序。

最小锚点包括 `bCastStaticShadow`、`bCastDynamicShadow`、`HasStaticShadowing` 与 `bPreShadow`。这些名字帮助定位条件，但 mobility、precomputed validity 与 runtime cache 必须按层级分别解释。

---

## 6. Regular 资源模型：atlas、cubemap、preshadow cache 与 border

### 6.1 为什么多个 shadow 共用 atlas

常规二维 shadow 可以被打包到较大的 depth atlas。每个 `FProjectedShadowInfo` 在 allocation 后获得 `X/Y`、`ResolutionX/Y` 和 `BorderSize`。

共享 atlas 的收益是减少独立纹理数量、绑定切换和内存碎片，并允许统一管理多份 projected shadow。代价是必须处理 packing、溢出、区域坐标和相邻区域采样隔离。

替代方案包括：

- 每灯独立 texture：实现和调试简单，但资源与绑定成本高；
- texture array：索引方便，但尺寸、层数和同规格约束不同；
- bindless：降低绑定压力，却不会自动解决分配、缓存和生命周期问题。

### 6.2 border 为什么属于正确性条件

过滤 shadow depth 时，采样核会访问中心 texel 周围的邻域。如果有效内容紧贴 atlas 分区边缘，PCF 等过滤可能读到相邻 shadow 的深度。

因此 border 的存在不是装饰：

- **硬约束**：过滤采样不能越过当前 shadow 的合法内容并读到别的 allocation；
- **UE 工程选择**：border 的具体大小、packing 方式与 shader 坐标换算；
- **代价**：border 占用 atlas 空间，较大的 filter kernel 需要更多保护区域或更严格的 clamping。

allocation 产出一块带 border 的合法目标区域；depth producer 消费其 `X/Y/BorderSize` 写入内容，projection 再用同一组参数重建采样坐标。三者共享 allocation contract，才能避免写入与读取落到不同分区。

### 6.3 cubemap 与 one-pass point light shadow

Point light 向所有方向发光，不能用一个普通二维透视投影覆盖完整空间。常规路径可以使用 cubemap 或 one-pass point light shadow 表达多个方向。

这是几何覆盖需求造成的硬约束；具体使用六面、layered rendering 或 one-pass 组织是工程选择。Cubemap 方便方向查询，但资源和 raster 成本较高；双抛物面等替代投影减少面数，却引入扭曲和接缝处理。

### 6.4 `FSortedShadowMaps` 表达的资源类别

UE 不把所有常规 shadow 塞进同一个资源。`FSortedShadowMaps` 区分：

- 2D shadow map atlases；
- shadow map cubemaps；
- preshadow cache；
- translucency shadow atlases；
- virtual shadow map shadows；
- complete shadow map atlases 等特殊集合。

这说明“shadow atlas”只是资源类别之一。调试时必须先确认当前 projected shadow 被路由到哪种集合，再检查 allocation 和 producer。

### 6.5 生命周期

本帧临时 atlas 通常服务当前帧 depth 与 projection；cached whole-scene shadow 或 preshadow cache 可以跨帧持有 pooled render target。下一帧先用 light transform、primitive 变化、resolution、initializer、预算和淘汰策略重新判断 cache validity，再决定复用或更新。

当前资源版本由 RDG、pool/cache owner 保持到最后 GPU consumer 结束，此后才进入安全退休或覆盖窗口。引用计数、RDG 声明、queue submit 与 GPU completion 的严格证据统一放在第 13 节。

---

## 7. Runtime Cache Mode：按 layer 复用低频遮挡并补画动态内容

UE 的常规 whole-scene shadow 可以通过 `EShadowDepthCacheMode` 表达不同更新责任：

| 模式 | 主要职责 |
|---|---|
| `SDCM_StaticPrimitivesOnly` | 生产或更新静态 primitive layer |
| `SDCM_MovablePrimitivesOnly` | 生产当前帧 movable primitive layer，并与缓存静态结果配合 |
| `SDCM_CSMScrolling` | 复用可滚动的 CSM 区域，同时更新新暴露或失效部分 |
| `SDCM_Uncached` | 不依赖上述跨帧缓存，按当前 request 生产 |

### 7.1 为什么拆成 layer

静态建筑和动态树的变化频率不同。如果每帧把两者一起重画，静态部分浪费 raster 成本；如果整张图都缓存，树移动后会留下旧影。

因此：

- **硬约束**：发生变化的遮挡必须在 consumer 读取前得到更新或被判失效；
- **UE 工程选择**：把 static、movable 与 scrolling 组织为这些 cache mode；
- **替代方案**：整张重画逻辑最简单但昂贵；更细粒度 tile/page cache 节省更新，却增加 invalidation、metadata 和内存管理复杂度；VSM page cache 属于更细粒度的另一类方案。

### 7.2 owner 与 lifetime

`FCachedShadowMapData` 由 scene 侧缓存结构按 light 持有，保存 initializer、pooled shadow map、last-used 与静态 subject 记录等跨帧状态。当前帧的 `FProjectedShadowInfo` 选择 cache mode，并把缓存资源接入本帧 depth/projection。

这形成两层 owner：

- scene cache 拥有跨帧 pooled resource 与有效性记录；
- 当前 frame projected shadow 拥有本帧如何使用或补画它的控制状态。

把两者都叫“shadow owner”会掩盖生命周期差异。

### 7.3 主案例：建筑缓存、树补画、地面像素消费组合结果

第一帧或缓存失效时：

1. 静态建筑写入 static layer；
2. 动态树写入 movable layer；
3. projection 将两类有效遮挡组合为地面像素的 attenuation。

后续帧建筑未变、树发生移动时：

1. static layer 可以继续复用；
2. movable layer 必须使用树的新 transform 重画；
3. 旧 movable 内容不能被当作新位置的遮挡；
4. projection 仍必须绑定当前帧匹配的组合资源。

如果画面出现“建筑阴影正确、树影停在旧位置”，最后成立状态通常是 static cache 仍有效，而动态 layer 的 gather、GPUScene 数据、depth write 或组合失效。

---

## 8. ShadowDepth Producer：从 caster 候选走到可执行的深度工作

前七节解决了“为什么需要这份阴影、哪个 view 会消费、哪些 caster 可能参与、目标资源在哪里、哪些内容可以复用”。但这些状态仍没有把树写进任何 GPU 深度资源。

ShadowDepth producer 的职责是把上游合约兑现成一组有顺序约束的工作：

```text
subject primitives
  -> shadow-view relevance 与 mesh selection
  -> shadow mesh draw command formation
  -> shadow view 的 GPUScene / instance 输入
  -> instance culling 与 draw 参数构建
  -> RDG depth producer 声明
  -> RHI recording 与平台命令形成
  -> GPU raster / Nanite raster 写入目标资源
```

这条链中最重要的边界是：**“选择了一个 caster”“形成了 draw 配方”“记录了 draw 命令”“GPU 写出了深度”是四个不同状态。**

### 8.1 caster gather 只建立几何候选，不建立 draw

`BeginGatherShadowPrimitives` 所代表的工作不是立即画阴影，而是等待并组合一组前置事实：

- primitive 对 shadow view 的 relevance 已经可判断；
- light visibility 已经可用；
- light 与 primitive 的 interaction 已经建立；
- 可复用的 mesh draw command cache 已经进入可读状态；
- VSM 的 renderer setup 已经提供该分支需要的基础状态。

有了这些条件，系统才能并行遍历场景、空间结构或 interaction，找出可能影响 preshadow、view-dependent whole-scene shadow 等投影的 primitive。

这里存在一个硬约束：caster 集合必须在空间和类型上保守地覆盖真正可能产生遮挡的几何，否则会漏影。UE 如何把遍历拆成 packet、如何安排 task prerequisite、何时并行执行，则是工程选择。

可替代方案是同步遍历全部场景。它的控制流更简单，适合规模很小或调试构建，但会把昂贵的 primitive 筛选串行堵在渲染主线上。UE 把 gather 启动与最终收口拆开，是为了让它和前后其他 CPU 工作重叠；代价是调试时必须区分“task 已启动”“前置条件已满足”“gather 已收口”。

对主案例而言，动态树在这一阶段经历的状态变化是：

```text
scene primitive
  -> 与太阳第二个 cascade 有空间交集
  -> 满足 cast dynamic shadow 与 shadow relevance
  -> 成为该 projected shadow 的 subject primitive
```

如果在这里找不到树，后续 GPUScene、mesh command 和 depth capture 都不可能把它补回来。优先检查树的 bounds、cast flags、light-primitive interaction、shadow-view relevance 和当前 projected shadow 类型，而不是先调 bias。

### 8.2 mesh selection 决定由哪条 raster 路径负责几何

Subject primitive 仍可能包含多个 LOD、多个 vertex factory、静态 mesh batch、动态 mesh batch，以及 Nanite 与非 Nanite 表示。ShadowDepth 需要把它们送入适合当前 shadow 类型的 mesh pass。

常见的 shadow mesh pass 包括：

| 目标 | 典型 mesh pass / raster 责任 |
|---|---|
| Directional Light 的 regular CSM | `CSMShadowDepth` 与常规 shadow depth raster |
| one-pass point light shadow | `OnePassPointLightShadowDepth` 与多方向输出 |
| VSM 非 Nanite 几何 | `VSMShadowDepth` 与 VSM page 输出 |
| VSM Nanite 几何 | Nanite shadow raster 路径 |

Mesh selection 的目标不是让两条路径都画一遍，而是让每份几何由合法 producer 负责。UE5.7 会结合 mesh/vertex factory 是否支持 GPUScene、shadow 类型与 selection mask 决定 regular SM、VSM 或兼容路径。

这里应避免一个过强简化：不能把“支持 GPUScene”直接等同为“必然只走 VSM”，因为一个 primitive 的不同 mesh/LOD/vertex factory 能力可能不同，兼容分支也可能保守地允许多类选择。真正的硬约束是：最终参与 Lighting 的遮挡数据必须完整且不能因无意重复生产而被重复调制；具体 selection mask 和兼容策略是 UE 的工程选择。

在同时保留 regular fallback 与 VSM 的配置中，mesh selection 分工尤其重要：

- 若某类几何没有进入任何 producer，会漏影；
- 若同一遮挡贡献被两条不应叠加的路径重复消费，可能过暗；
- 若只看“primitive 已在 subject list”而不看最终 pass，容易误判 producer 已成立。

主案例中的树若是普通非 Nanite mesh，可能进入 regular CSM 或 VSM 的非 Nanite pass；若使用 Nanite，则 VSM/Nanite 路径承担主要深度生产。选择取决于项目路径与 mesh 能力，不应写成 CSM 与 VSM 的简单质量二选一。

### 8.3 shadow mesh draw command 是 draw 配方，不是 GPU 命令

`SetupMeshDrawCommandsForShadowDepth` 把通过 selection 的 mesh batch 转换为 shadow depth pass 可消费的 draw 配方。它会确定：

- 使用哪个 shadow depth shader permutation；
- vertex factory、material 与 raster state 如何组合；
- draw 对应哪个 shadow view；
- 哪些 instance range 交给 Instance Culling；
- pass 如何引用静态缓存命令或动态构建请求。

这一步复用了第 07 篇的 Mesh Draw Command 体系，但必须注意章节边界：第 07 篇建立的是通用命令模型，不代表本帧 shadow-specific command 已经存在。只有当前 shadow view、mesh selection、material relevance 和 instance 条件都确定后，shadow pass 才能形成自己的 draw 配方。

**Command formation** 在本章中特指“形成能够描述一次 shadow draw 的 CPU 侧配方和批次”。它回答画什么、用什么 pipeline state、从哪些 instance 数据取值。它还没有回答平台 command buffer 中是否已经出现对应 draw，也没有证明 GPU 执行。

不用这层配方的替代方案，是在记录 RHI 命令时重新遍历 mesh 并即时选择 shader。那样实现直观，却会重复高成本状态匹配，削弱缓存、排序、并行 setup 与 Instance Culling 的复用。UE 的选择是提前形成稳定配方，再在 pass 执行阶段构建实际 rendering commands。

### 8.4 为什么 shadow view 需要自己的 GPUScene 输入

动态树的 transform、instance scene data、primitive payload 和 instance range 可能已经为主相机 view 准备过，但 shadow view 仍有独立的视图矩阵、culling frustum、LOD 与 instance culling 上下文。

因此，`FinishDynamicShadowMeshPassSetup` 会在 mesh setup 收口后，为需要的 shadow depth views 接入或上传动态 primitive shader data。这里的数据关系是：

| 维度 | 含义 |
|---|---|
| Owner | Scene 的 GPUScene 管理器拥有 GPU 侧 scene buffers；shadow view 保存本次读取这些 buffers 所需的引用与 view-local context |
| Data | primitive transform、instance data、payload、instance range，以及 shadow view 的 uniform/culling 输入 |
| Producer | scene publication、dynamic primitive collection 与 GPUScene upload/update |
| Consumer | shadow instance culling、vertex/mesh shader、Nanite 或常规 shadow raster |
| Lifetime | scene buffer 可跨 pass/跨帧管理；当前 shadow view 的绑定与动态收集状态服务本帧 producer |

硬约束是 shader 和 culling 必须读取与当前 primitive 状态匹配的数据版本。UE 选择 GPUScene，是为了让许多 draw 通过 ID/offset 从共享 GPU buffers 读取实例数据，而不是为每个 draw 重复绑定大量 per-object 常量。

替代方案是每个 shadow draw 绑定独立 uniform buffer。它对少量对象简单，但对象和 instance 数量上升时，更新、绑定与命令尺寸成本会增加。GPU-driven 路径还可以把更多筛选留在 GPU；代价是对 buffer 内容、ID、offset、view context 和间接参数的调试要求更高。

主案例中，如果树的主视图位置正确而阴影仍停在旧位置，应检查：

1. 树的新 transform 是否已进入本帧 Renderer-visible scene state；
2. shadow view 是否收集了该动态 primitive；
3. GPUScene 中对应 primitive/instance 数据是否为当前版本；
4. mesh command 使用的 instance ID/range 是否指向同一份数据；
5. instance culling 后是否仍保留该 draw。

“主相机画对了”只能证明主视图消费链正确，不能替 shadow view 证明这些状态。

### 8.5 Instance Culling：把候选 command 变成实际 draw 参数

Mesh draw command 可以描述一批候选 instance，但 shadow view 的 frustum、cascade bounds、LOD 与遮挡条件仍可能把其中一部分实例剔除。Instance Culling 依据 shadow view 注册的 culling view 与 GPUScene 数据，生成实际 draw 参数或间接参数。

硬约束是被剔除的实例不能影响当前 shadow view；采用 GPU culling、批量间接参数和 deferred culling scope 是 UE 的工程选择。CPU 逐实例裁剪是可行替代，在实例少时更易检查；实例多时则增加 CPU 工作和命令生成压力。

因此 command formation 之后的数据继续这样换形：

```text
shadow mesh command exists
  -> 提供 pipeline、binding 与候选 instance range

instance culling keeps the tree
  -> 生成包含树实例的实际 draw / indirect 参数

RDG depth pass declared
  -> 把参数与 depth target 纳入 producer 依赖

GPU raster executes
  -> 把树的光空间深度写入目标资源
```

### 8.6 并行准备的收益、代价与收口点

Shadow setup 会把 gather、shadow view setup、mesh command formation 等工作拆成 task，并在需要进入 ShadowDepth 前等待它们。并行化的收益是把多 cascade、多 light 和大量 caster 的 CPU 工作分散到 worker threads，与其他 frame setup 重叠。

但并行化不会放宽数据依赖：

- relevance、light visibility、interaction 与 mesh-command cache 必须先可用；
- mesh pass setup 必须在消费其结果前收口；
- 动态 GPUScene 数据必须在 shadow culling/raster 读取前接入；
- 被并行记录的 command list 必须完成 recording 并交回后续 RHI 调度。

不用并行 task 可以减少状态数量，适合单步调试；代价是 CPU frame time 更容易被 shadow setup 拉长。调试并行 bug 时，不应只问“任务是否启动”，而要问“哪个 prerequisite 未满足、哪个结果尚未 publish 给 consumer、哪个收口点等待了错误对象”。

### 8.7 cache mode 如何改变 depth producer

ShadowDepth 并不总是清空目标并把所有 caster 重画一遍。根据第 7 节的 cache mode，它可能：

- 复制或引用已有 static cached depth；
- 只画 movable primitives layer；
- 更新 CSM scrolling 新暴露区域；
- 复用有效 preshadow cache，从而跳过该 preshadow 的深度写入；
- 在 cache 失效时回到完整或更大范围更新。

cache policy 先把内容分成“本帧生产”和“有效复用”，再让 projection 读取单一资源、复制结果或组合状态。这样 ShadowDepth 只为发生变化的责任层付费。

主案例的后续帧中，建筑 static layer 可以直接复用；树的 movable layer 用新 transform 生产并与静态内容组合。capture 若只有建筑深度，下一检查点就是 movable layer 的 gather、GPUScene、draw 参数和实际写入。

---

## 9. 从 RDG Depth Pass 到 GPU 深度资源

当 caster、mesh command、GPUScene 和 instance inputs 都准备好后，Renderer 才能把 ShadowDepth producer 加入 RDG。

### 9.1 RDG 把 ShadowDepth producer 接入执行图

`RenderShadowDepthMaps` 组织 VSM、2D atlas、cubemap、preshadow cache、translucency shadow 等资源的 producer。对每个目标，RDG 声明：

- pass 读取哪些 view、mesh command、instance culling 与缓存输入；
- pass 写哪个 depth texture、physical page pool 或特殊 shadow resource；
- 哪些 pass 必须先后执行；
- 资源需要哪些状态转换与 barrier。

RDG 选择这套声明式模型，是为了让调度器统一分析生命周期、aliasing、barrier 和并行机会。立即执行每个 pass 的替代方案更容易逐句理解，却会让资源状态和跨分支排序散落在各个 shadow 类型中。

硬约束是 producer 写入必须先于 projection/Lighting 读取，且资源状态合法。具体 pass 合并、裁剪、并行 recording 与 transient allocation 是 RDG 的工程实现选择。

### 9.2 RHI 与 platform backend 形成设备可提交命令

RDG 执行到 ShadowDepth pass 时，pass lambda 或 draw pass 会把 draw、dispatch、barrier 和 render-pass 操作记录到 RHI command list/context。这是 **RHI recording**。

RHI 命令仍是 UE 图形抽象层的命令表示。随后，动态 RHI 与平台 backend 会把它们翻译或编码为目标 API 的 command buffer / command list，这里称为 **platform command formation**。不同 backend 可以在记录期间逐步编码，也可以在命令列表执行/翻译阶段形成平台命令；本章只要求保留责任边界，不假定所有平台采用同一时刻。

两者之间的关系是：

```text
mesh draw command formation
  决定“应该画什么”

RHI recording
  把 pass 工作记录成 UE RHI 命令

platform command formation
  把 RHI 工作编码为具体图形 API 可提交的命令
```

这三层完成了一次控制形态转换：CPU draw 配方先变成 RHI 工作，再变成目标图形 API 可以接收的 command buffer/list。下一位 owner 是平台 queue；严格的证据深度留到第 13 节统一判断。

### 9.3 Platform Queue Submit 把命令交给设备执行

平台命令形成后，graphics/compute command buffers 被提交到 GPU queue。此时 CPU/backend 的 producer 已完成控制权交接，设备队列按同步与依赖关系推进到 ShadowDepth，再由 GPU raster/compute producer 写目标资源。

资源由 `ConvertToExternalAccessTexture` 一类转换入口变成 external pooled reference，并把目标 access / pipeline 登记到 external access queue；`ExternalAccessQueue.Submit(GraphBuilder)` 只把这些登记应用为 RDG external-access mode。设备队列提交则由更下层的 RHI/backend 完成。前者管理资源访问合同，不形成或提交平台命令；后者才把设备执行控制转移给 graphics/compute queue。

立即 flush 并等待 GPU fence 可以把 submit 与完成拉近，适合诊断或同步需求，但会破坏流水并行。正常帧使用依赖图、barrier 与延迟提交，是为了让 CPU 继续准备后续工作、GPU 按依赖顺序执行。

### 9.4 GPU depth production 的可观察证据

只有当 GPU 执行对应 raster/compute producer 后，目标资源中才出现可供 projection 读取的深度或 VSM page 内容。可用证据包括：

- GPU capture 中 ShadowDepth pass 的 render target / UAV 内容；
- pass 后资源查看器看到树的深度轮廓；
- timestamp/fence/query 证明 GPU 已越过 producer；
- 后续只读调试 pass 读取到预期值。

主案例中，第二个 cascade 的 atlas 区域应包含动态树从太阳方向看到的深度。若 GPU capture 或后续只读验证显示对应 draw 已执行，但 atlas 区域仍没有树，检查 actual draw 参数、viewport/scissor、atlas rect、depth state、instance data 与 raster 路径。第 13 节会把 queue submit 与资源内容证据放回统一调试表。

### 9.5 Projection 接管之前，深度资源保持有效

Shadow depth 产生后由 projection/filtering 读取，还可能服务 hair、volumetric、debug visualization 或其他合法 consumer。资源系统因此把 depth、mapping 与相关 allocation 保持到这些读取结束。

硬约束是最后 GPU consumer 完成前资源及其 mapping 必须保持有效。RDG transient lifetime、pooled render target 与 cache manager 是 UE 用来满足该约束的工程机制。跨帧 cache 还要额外满足下一帧 initializer 与 invalidation 条件。

---

## 10. Projection 与 Filtering：第二个 producer 阶段

ShadowDepth 回答的是“从灯的投影视角，哪个位置最先遇到 caster”。Lighting 需要的却是“当前屏幕像素到灯是否可见”。两者处于不同空间，不能把 atlas 深度直接当作最终 shadow term。

因此，阴影主链至少有两个 producer 阶段：

1. **Depth producer**：生成光空间或虚拟阴影空间的遮挡深度；
2. **Projection/filter producer**：结合当前 receiver，把深度查询转换为屏幕像素或 lighting sample 可消费的 attenuation/mask。

### 10.1 projection 具体生产什么

对常规 deferred shadow，projection 阶段读取：

- 当前像素的 `SceneDepth`，用于重建 receiver 位置；
- shadow projection matrices 与 cascade/face 选择；
- atlas/cubemap 中的 shadow depth；
- `X/Y/Resolution/BorderSize` 等 allocation 参数；
- depth bias、transition、fade、filter 参数；
- per-object、whole-scene、static/precomputed 等组合信息。

然后它把当前 receiver 投到 shadow space，比较 receiver depth 与 caster depth，并应用过滤、fade 和类型组合，写出当前灯的屏幕空间 attenuation。常规 deferred 路径常把结果放进 screen shadow mask / light attenuation texture，供后续 light pass 读取。

Owner 关系应这样理解：shadow depth resource 由对应 depth/cache 系统持有；projection pass 拥有本次从 depth 到 attenuation 的转换工作；输出的 screen mask 是 RDG 管理的本帧资源；Lighting 是其 consumer。

### 10.2 filtering 为什么属于查询/投影阶段

深度图保存的是离散遮挡样本。若只做一次硬比较，边缘会锯齿、cascade 交界突变，bias 误差也更明显。PCF 等过滤在 receiver 查询附近采样多个 depth texel，得到软化或稳定的可见性估计。

“receiver depth 与 caster depth 必须在一致空间中比较”是 shadow map 算法的硬约束。使用多大核、是否旋转采样、何时做 receiver-plane bias、是否先生成 screen mask，则是质量、带宽和 shader 成本之间的工程选择。

两种常见组织方式是：

- **先 projection 到 screen mask**：每盏灯先生成可复用 attenuation，Lighting shader 读取简单；代价是额外 render target 带宽与逐灯 pass；
- **在 light shader 中直接查询 shadow data**：减少中间纹理或合并 pass，但增加 light shader 复杂度，并要求相关 page table、depth、filter 参数同时绑定。

哪种更好取决于灯数、平台、缓存局部性、是否能批处理和需要组合的其他效果，而不是一个全局结论。

### 10.3 常规 screen attenuation mask 的通道语义

Deferred Lighting 中的 `LightAttenuation` 不是单一“0/1 阴影值”。不同通道可以携带 whole-scene directional shadow、subsurface shadow、per-object/light-function 等组合结果，Lighting 再把它与 precomputed shadow factors 合并成 surface、transmission 等 shadow terms。

这解释了为什么 atlas 有正确深度仍可能屏幕无影：

- projection 可能使用了错误 cascade、cubemap face 或 atlas rect；
- fade/transition 可能把结果衰减掉；
- filter/bias 可能让比较全部判为可见；
- screen mask 可能没有被写入、被清空或被其他调制覆盖；
- Lighting 可能没有绑定这张 mask，或读取了错误通道。

### 10.4 VSM packed mask bits 与直接 Lighting 读取

VSM 也需要 projection：page table 和 physical depth 不能自动变成屏幕遮挡。UE5.7 可以先把多个 local-light VSM projection 的结果编码进 packed mask bits，再由 clustered/deferred light shader按 light 索引读取。

在一组严格条件下，某些非 Directional local lights 可以省略独立 screen shadow mask：VSM one-pass projection 已经产生 packed bits；当前灯只有 VSM shadow；没有必须写入该 mask 的 light function、preview indicator、first-person self shadow 或 heterogeneous-volume shadow；相关 clustered/deferred 路径能够直接读取这些 bits。

这里的边界是：

- **packed bits 仍是 projection producer 的输出**，不是 Lighting 直接把 raw physical page 当最终 shadow term；
- Directional Light 等路径仍可把 VSM projection 合成到常规 screen mask；
- 只要还有其他效果必须在 attenuation buffer 中组合，就不能简单省略 screen mask；
- one-pass projection 是否启用是工程配置与路径条件，不是所有 VSM light 的硬规则。

所以调试 VSM 时要先确认当前灯走的是哪种消费接口：

```text
VSM physical depth
  -> per-light / directional projection
  -> screen shadow mask
  -> Lighting

或

VSM physical depth
  -> one-pass projection
  -> packed shadow mask bits
  -> clustered/deferred Lighting 直接索引
```

### 10.5 Lighting consumer 怎样绑定并形成 shadow terms

普通 deferred light 在进入 shading 前，会按该灯的 occlusion route 准备 shadow input：screen shadow mask、packed VSM bits、ray-traced mask 或无阴影默认值。Light shader 再结合 precomputed shadow factors 与 contact correction，构造 `FShadowTerms`。

`GetShadowTerms` 是最小 shader 锚点。它表达的是消费边界：

- 基础 shadow terms 从 `LightAttenuation` 与预计算因子得到；
- Contact Shadow 在允许时进一步调制 surface/transmission shadow；
- 后续 BRDF 使用这些 terms，但 BRDF 积分属于第 12 篇。

Screen mask 交给 Lighting 时，还要完成下面的绑定与覆盖链：

- 当前 light 的参数绑定了同一份 mask/bits；
- light index 与 packed bit 索引匹配；
- shadowed flags 与 route 没有让 shader 走无阴影分支；
- precomputed、dynamic、contact 等项按预期组合；
- 当前像素确实由这盏灯的 light volume/fullscreen pass 覆盖。

### 10.6 主案例：树深度如何变成地面像素的 attenuation

继续第二个 cascade：

1. `SceneDepth` 重建目标地面像素的世界位置；
2. cascade 选择确认该像素落在第二个 split，并计算 transition/fade；
3. shadow projection 把地面点变换到第二个 cascade 的 atlas 坐标；
4. filter kernel 在合法 border 内读取附近 shadow depth；
5. 树写出的 caster depth 比地面 receiver depth 更靠近太阳，因此可见性降低；
6. projection 把该结果写入这盏太阳灯的 attenuation 通道；
7. Lighting 绑定该 attenuation，`GetShadowTerms` 得到较低的 surface visibility；
8. 第 12 篇的 direct-light BRDF 才使用它调制太阳对地面像素的贡献。

如果 GPU capture 中 atlas 树轮廓正确，但地面仍被照亮，应从第 2–7 步检查，而不是继续修改 caster gather。

---

## 11. VSM 接口模型：request、pool/cache 所有权与生命周期

VSM 改变的是资源粒度和 request 方式，不改变“producer 必须在 consumer 前提供合法遮挡数据”这一硬约束。

### 11.1 为什么先提出虚拟页需求

Directional Light 的投影空间很大，local lights 也可能很多；固定为每盏灯保留完整高分辨率 shadow map，会为当前帧没有查询的区域支付内存和 raster 成本。

VSM 把逻辑地址空间切成虚拟页。屏幕像素、froxel、water/front-layer 输入或 MegaLights samples 根据未来查询位置提出 page request；系统再为需要保留或更新的虚拟页建立 physical mapping。

这是一种需求驱动的工程选择。硬约束不是“每个被渲染的页都必须对应一个精确像素请求”，而是“consumer 可能查询的区域必须有有效 mapping 或合法 fallback”。为了稳健性和低分辨率覆盖，系统可以 coarse mark、保守扩张、复用缓存页，或在某些策略下保留近期请求页。因此 VSM 可能生产比本帧逐像素最低需求更多的页面。

这种 over-request 的代价是额外 physical pages 与 raster；收益是避免漏页、支持粗层 fallback、降低 request 不完备带来的闪烁。更激进的精确 request 节省工作，但对可见性估计、buffer 容量和边界覆盖更敏感。

### 11.2 四类状态必须分开

| 状态 | 本阶段产物 | 下一消费者 |
|---|---|---|
| Virtual request flags | 需要考虑或保留的虚拟区域集合 | page allocation / cache lookup |
| Page-table mapping | 虚拟页到 physical page 的地址关系 | Nanite / non-Nanite page producer |
| Physical page content | 对应 caster 的新深度或有效缓存内容 | VSM projection |
| Projection output | screen mask 或 packed bits | clustered / standard Lighting consumer |

把这四类状态合并成“VSM ready”会掩盖几乎所有关键 bug。

### 11.3 physical pool 与 cache 的真正 owner

VSM 的长期资源所有权分成两层：

- `FVirtualShadowMapArrayCacheManager` 是 scene extension，持有或管理跨帧 physical page pool、上一帧相关 buffers、cache entry、invalidation 与提取状态；
- `FVirtualShadowMapArray` 是本帧工作接口，接收 cache manager，导入或创建 RDG 引用，维护当前帧 page request、page table、uniform 与 producer/consumer连接。

因此不能把本帧 array 说成底层 physical pool 的唯一所有者。更准确的模型是：cache manager 保有跨帧资源与历史；frame array 在当前 RDG graph 中取得合法引用并推进本帧状态。

没有这种分层，可以每帧重新创建完整 physical pool，所有权简单但内存分配和历史复用成本很高。跨帧 cache 提高复用，却要求精确 invalidation：light transform、caster 变化、instance 失效、projection 改变或资源预算压力都可能使旧页不再合法。

### 11.4 page/resource lifetime

VSM 的不同数据有不同 lifetime：

| 数据 | 典型 lifetime |
|---|---|
| 本帧 request flags、部分 page-table/RDG 工作资源 | 当前 graph/frame |
| physical page pool | 由 scene cache manager 管理，可跨帧存在 |
| cache metadata 与上一帧映射/反馈 | 跨帧，用于复用、失效与淘汰 |
| projection mask bits / screen mask | 本帧，直到最后 Lighting consumer 完成 |
| 当前 frame uniform | 本帧 consumer 绑定期间有效 |

“physical pool 跨帧存在”不表示其中每一页跨帧有效；“cache hit”也不表示当前 light/view/mapping 一定一致。必须同时验证 cache entry、page metadata、invalidation 与当前 projection identity。

### 11.5 VSM producer 的接口顺序

在 deferred 路径中，接口级顺序可以概括为：

```text
frame VSM enabled
  -> frame array 接入 cache manager 与历史资源
  -> BasePass 后收集 pixel/froxel/water/front-layer 等 page requests
  -> MegaLights sample 若使用 VSM，再合入 sample-driven requests
  -> BuildPageAllocations 建立/复用 mappings
  -> Nanite 与非 Nanite producer 写需要更新的 physical pages
  -> VSM projection 生成 screen mask 或 packed bits
  -> Lighting consumer 读取
  -> frame data 按规则提取回 cache manager
```

Forward shading 是明确边界：UE5.7 forward path 不支持 VSM，regular shadow maps 会采用更早的渲染位置。`ShadowMapsRenderEarly` 也不能在 VSM 启用时当作同等可用的提前渲染开关，因为 VSM page demand 依赖 BasePass 后输入。

### 11.6 主案例的 VSM 分支

如果太阳改走 VSM：

1. 地面像素及其周边查询区域为太阳 clipmap 提出 page demand；
2. coarse marking 或保守范围还可能请求额外低分辨率页；
3. cache manager 提供可复用 physical pool 与历史状态；
4. 当前 frame array 建立或复用 virtual-to-physical mapping；
5. 动态树使相关页需要更新，不能只信任旧 static content；
6. Nanite 或非 Nanite producer把树深度写入对应 physical pages；
7. VSM projection 针对地面像素查询 page table 与 physical depth；
8. 太阳 Directional Light 通常把结果合成 screen attenuation，再由 Lighting 消费。

在这条链里，request bit、mapping、page 内容、projection 和 Lighting binding 必须逐层验证。第 19 篇会展开 clipmap、page-table 编码、cache invalidation 和采样算法；本章到“接口与生命周期能够被正确生产和消费”为止。

---

## 12. 三条边界分支：MegaLights、Contact Shadow 与 Ray-Traced Shadow

这三条路径都改变阴影主链中的某些责任，但不应被误解为三套与核心链完全无关的系统。

### 12.1 MegaLights：把 local-light 查询单位改成 sample

大量 local lights 如果逐灯建立 projected shadow、逐灯 projection、逐灯 light pass，成本会随灯数快速增长。MegaLights 的核心路由变化是把工作单位从“每盏灯完整处理”改为“为当前 view 选择并处理 light samples”。

接口级 owner/data/producer/consumer 是：

| 维度 | MegaLights 分支 |
|---|---|
| Owner | view/frame 的 MegaLights context |
| Data | 被接管的 local-light range、samples、trace inputs/results、resolve 输入 |
| Producer | sample generation；VSM/screen-space/RT trace；resolve/denoise |
| Consumer | MegaLights lighting resolve，而不是普通逐灯 shadow projection consumer |
| Control | light type、allow flag、shadow method、平台与全局设置 |
| Lifetime | 当前帧；时域历史的算法细节留给第 18 篇 |

若 MegaLights 选择 VSM shadow method，samples 必须在 VSM allocation/raster 前提出页需求，否则 sample 将查询没有生产的页。这是 producer-consumer 顺序的硬约束。UE 如何随机选择 sample、如何 reservoir/temporal/denoise，是后续篇的工程算法。

局部案例：一批 point/spot lights 被排序交给 MegaLights 后，普通 light loop 不应再次把它们当作相同责任的逐灯 consumer。若 sample 已生成但随机漏影，依次检查 sample-driven request、page mapping、physical write、trace 与 resolve；不要先假设某盏灯丢失了一张传统 shadow map。

Directional Light 通常不是 MegaLights 用来解决“大量 local lights”问题的目标。太阳主案例继续由 CSM/VSM/RT 主线承担，避免把 MegaLights 误解为所有 light type 的统一替代路径。

### 12.2 Contact Shadow：screen-space 近接触修正

Contact Shadow 解决的是 shadow map bias 或 texel 密度难以覆盖的短距离接触漏光。它用当前屏幕可见的 `SceneDepth`、GBuffer/Substrate 信息和光方向做短射线，并调制当前灯的 shadow terms。

它的硬边界来自屏幕空间：屏幕外、被别的表面覆盖或没有写入当前 depth/GBuffer 的遮挡信息不可被可靠查询。因此它适合作为近接触补偿，不应替代远距离或屏幕外 caster 的 shadow map/RT 遮挡。

UE5.7 中至少要区分两种组织：

- 普通 deferred light shader 内联执行，并直接修改当前 `FShadowTerms`；
- standalone/mobile 等路径先生产 screen-space contact mask，再由后续 consumer 使用。

所以“Contact Shadow 不产生任何资源”只适用于 inline 分支，不能作为全局规则。

灯光参数中的 contact length 还存在一个调试陷阱：deferred-light uniform 可以用长度的符号编码 world-space 或 screen-space 模式，shader 再解码为独立的 `ContactShadowLengthInWS` 与绝对长度。看到负长度时，先确认它是否是模式编码，而不是直接判断功能被禁用。standalone 路径也可以使用显式布尔参数，不能把负号编码推广成所有实现的唯一接口。

桌腿案例中，shadow map 已提供大尺度遮挡，Contact Shadow 只沿光方向检查短距离屏幕信息。若桌腿移出屏幕，屏幕空间证据消失是方法边界，不是 ShadowDepth 漏画。

### 12.3 Ray-traced shadow：把 producer 换成 RT scene 查询

当平台支持 ray tracing、view family 启用相关能力、light type 与 light flag 允许时，occlusion route 可以选择 ray-traced shadow。此时 producer 不再依赖为该灯创建常规 shadow atlas，而是查询已经建立并可供当前帧使用的 ray tracing scene，生产 shadow mask/trace result，再交给 Lighting。

其接口级模型是：

| 维度 | RT shadow 分支 |
|---|---|
| Owner | Renderer 的 ray tracing shadow pass 与相关 view resources |
| Data | ray tracing scene、light parameters、view/receiver、sample与denoise设置 |
| Producer | ray generation/inline trace 与可选 denoise |
| Consumer | 当前 light 的 deferred/lighting path |
| Control | 平台能力、项目设置、view family、light `CastRayTracedShadow` 路由 |
| Lifetime | 本帧 mask/result，直到 Lighting 与最后 GPU consumer 完成 |

RT 的替代收益是直接查询几何级加速结构，避免 regular atlas/cascade 的某些分辨率问题；代价是 RT scene 构建/更新、trace、噪声与 denoise 预算。它不是“开启后一定更正确”的全局结论，也不应在本章展开 BVH、sample 或 denoiser 算法。

Raytraced route 把当前 light/view 交给 RT scene 查询；trace 生成 mask，可选 denoise 处理后由 Lighting 绑定。无阴影时沿 `RT scene → light trace work → mask/denoise → Lighting binding` 检查，不需要在只属于 projected-shadow 家族的 `FProjectedShadowInfo` 列表中寻找对象。

### 12.4 按责任接口比较，而不是按“先进程度”排序

| 路由 | 适用问题 | 主要 producer 输入 | 交给 Lighting 的接口 | 关键边界与代价 |
|---|---|---|---|---|
| Regular CSM / shadow map | 少量关键 directional/local lights；兼容与可预测的 raster 路径 | light/view projection、caster mesh、GPUScene/instance data、atlas/cubemap | screen attenuation mask | cascade/atlas 分辨率、逐灯 setup 与 projection 成本 |
| VSM | 大范围高细节、Nanite 友好、稀疏查询 | page requests、cache/mapping、caster mesh、physical pool | screen mask 或 packed VSM bits | page budget、invalidation、mapping 与 producer复杂度 |
| MegaLights VSM/RT | 大量 local lights 的 sample-driven 查询 | sorted light range、view/depth/GBuffer、samples、VSM或RT输入 | MegaLights trace/resolve lighting | sample、trace、resolve 与时域算法成本；不走普通逐灯消费模型 |
| Ray-traced shadow | 平台和预算允许的几何级查询 | RT scene、light/view、trace与denoise参数 | ray-traced mask/result | RT scene 更新、trace、噪声与 denoise 预算 |
| Contact Shadow | 近距离接触漏光补偿 | 当前屏幕 depth/GBuffer、light contact参数 | inline shadow-term 修正或 standalone mask | 受屏幕空间信息限制，不能承担远距离/屏幕外遮挡 |

“少量关键灯适合 regular map”只是常见预算经验，不是技术门限；VSM 也不是 CSM 的单向质量升级。实际选择取决于平台、shading path、light type、mobility、几何表示、灯数、缓存行为、兼容性和目标质量。

---

## 13. 调试与资源退休：沿唯一完成深度继续查

前十二节已经建立了完整生产链。现在才把“完成”拆成严格证据：调试先找最后仍成立的状态，再检查它与下一状态之间的 producer、data、control 和 lifetime；资源系统则用同一条链决定何时允许退休、覆盖或复用。

### 13.1 八层完成深度

| 深度 | 已经发生什么 | 下一项需要的证据 |
|---|---|---|
| RDG work declared | pass、资源读写和依赖已经加入 Render Graph | RDG 执行并形成 RHI work |
| RHI recorded | draw/dispatch/barrier 已记录为 RHI 抽象工作 | backend 形成平台命令 |
| Platform commands formed | RHI 工作已编码成图形 API command buffer/list | 提交到设备 queue |
| Platform Queue Submit | command buffer/list 已提交到 graphics/compute queue | GPU 越过目标 producer |
| GPU shadow resource produced | capture、timestamp、fence 或只读验证已观察到 shadow depth / VSM page 内容 | projection/filter 读取匹配版本 |
| Projection/filter output produced | 适用分支已把 shadow resource 转成 attenuation、screen mask 或 packed bits | Lighting 绑定并读取匹配输出 |
| Lighting consumed | 当前灯 shader 已读取 attenuation、VSM mask 或其他 shadow terms | 覆盖所有后续合法 consumer 的完成证据 |
| Final consumer completed | 最后 GPU consumer 已结束 | 资源可按生命周期规则退休、覆盖或复用 |

`bShadowDepthRenderCompleted` 只检查 Renderer 的图内编排已经越过 ShadowDepth 组织点。资源转换入口负责产生 external pooled reference 并登记 access / pipeline，`ExternalAccessQueue.Submit(GraphBuilder)` 只把登记应用为 RDG external-access mode。它们都不替代平台 queue submit 或 GPU fence。UE 采用 RDG 和延迟命令提交，是为了统一管理 barrier、并行 recording 与资源复用；每步都 flush 等待虽然更容易逐句观察，却会破坏 CPU/GPU 流水和跨 pass 调度。

### 13.2 Last-Valid-State 表

| 最后成立状态 | 能证明 | 不能证明 | 下一检查点与可用证据 |
|---|---|---|---|
| light 通过 view 判断 | 当前 view 需要考虑这盏灯 | 需要 shadow map/RT/MegaLights | cast flags、occlusion route、light sorting |
| route 已选择 | 遮挡责任系统明确 | producer work 已创建 | projected shadow、RT pass 或 MegaLights context |
| projected shadow 存在 | 投影合约已建立 | 当前 view 会消费 | per-view visibility/relevance map |
| projected visibility 为真 | 至少一个 view 需要该投影 | caster 存在 | subject primitive gather、interaction、bounds |
| subject primitive 包含树 | caster 候选成立 | mesh 被 pass 接受 | mesh selection、material/VF/pass relevance |
| cache entry / allocation 成立 | 有可复用资源或合法目标区域 | 当前内容有效 | initializer、invalidation、layer责任、atlas rect |
| shadow mesh command 成立 | draw 配方存在 | instance culling 后有 draw | culling view、GPUScene ID/range、indirect args |
| shadow view GPUScene 数据正确 | shader可读取当前 transform/instance | draw 已记录或执行 | pass command build、culling output |
| RDG depth pass 已声明 | producer/consumer 图关系成立 | RHI/platform/GPU 进度 | RDG execution、RHI recording |
| RHI recording 完成 | RHI 抽象工作已收口 | 不证明平台命令已形成 | backend translation / encoding |
| Platform commands formed | API command buffer/list 已形成 | 不证明已提交设备 queue | Platform Queue Submit event |
| Platform Queue Submit | GPU 已接收工作 | GPU depth 已产生 | timestamp/fence/capture 中的 pass执行 |
| GPU depth 正确 | caster 深度已物理产生 | projection 正确 | receiver reconstruction、matrix、rect、filter |
| VSM request/mapping 正确 | 虚拟需求与物理地址关系成立 | physical page 当前有效 | cache invalidation、page write、metadata |
| screen mask 正确 | 常规 projection attenuation 成立 | 当前 light 绑定正确 | light parameters、mask channel、shader permutation |
| packed VSM bits 正确 | one-pass projection 输出成立 | light index/cluster consumer 匹配 | packed index、clustered light data、VSM ID |
| RT shadow mask 正确 | trace/denoise producer成立 | deferred light 使用它 | occlusion route、mask binding |
| Contact Shadow 参数正确 | 当前灯允许 contact query | ray 命中有效 caster | inline/standalone route、screen depth、length模式 |
| `GetShadowTerms` 输入正确 | 当前像素遮挡项可形成 | 最终 BRDF 与 SceneColor 正确 | 第 12 篇 Lighting integration |
| Lighting 已消费 | 当前灯已读取阴影证据 | 资源可立即复用 | 所有后续 GPU consumer 与 fence/lifetime |
| Last GPU consumer completed | 当前资源版本可按规则退休/覆盖 | 下一帧 cache 仍有效 | 下一帧 invalidation、budget、identity |

### 13.3 三种典型坏结果

**结果 A：树在主画面运动，树影停住。**

最后成立状态若是建筑 static cache 正确，优先检查树是否重新进入 movable layer、shadow view GPUScene 是否拿到新 transform、instance culling/draw 是否仍引用旧 range，以及动态 layer 是否实际写入。不要先清空整个 static cache；那只会掩盖动态责任链的问题并增加成本。

**结果 B：atlas 中树轮廓正确，地面仍亮。**

Depth producer 已成立。继续检查 cascade 选择、projection matrix、atlas rect/border、depth convention、bias/filter、screen attenuation 输出与 Lighting binding。此时反复修改 caster bounds 通常不会解决问题。

**结果 C：地面阴影正确一帧，下一帧随机缺块。**

Regular cache 路径检查 cache invalidation、resource reuse 与 dynamic layer 更新；VSM 路径检查 request、mapping、physical page cache age/invalidation、projection 所用 page table 与 pool 是否同代。若只在高负载出现，还要检查 page/atlas 预算与资源生命周期，而不是把它直接归因于采样噪声。

### 13.4 一条可执行的排查顺序

```text
1. 先认 route
   Shadowmap / VSM / MegaLights / Raytraced / Contact branch

2. 再认 producer contract
   projected shadow、sample context、RT mask pass 或 contact parameters

3. 对 shadow-map 家族按顺序检查
   per-view visibility
   -> caster gather
   -> cache/allocation
   -> mesh selection
   -> GPUScene/instance culling
   -> depth resource
   -> projection/filter
   -> Lighting binding

4. 对 VSM 在资源链中插入
   request
   -> mapping/cache validity
   -> physical page write
   -> screen mask / packed bits

5. 对每条路径标明实际经过的 completion depth
   RDG declared
   -> RHI recorded
   -> platform commands formed
   -> Platform Queue Submit
   -> GPU shadow resource produced
   -> projection/filter output produced（分支适用时）
   -> Lighting consumed
   -> Final consumer completed
```

这条八层链是规范词汇，不要求每个分支虚构并不存在的中间资源。inline Contact Shadow 在 Lighting shader 内完成屏幕查询与 shadow-term 修正，因此不单独产生 shadow resource 和 projection/filter output；standalone Contact mask 才按实际 producer、mask output 与 consumer 经过对应层级。其他分支也只记录自身真实存在的阶段。

### 13.5 资源退休为什么必须等最后 consumer

假设 atlas 在 ShadowDepth 后立刻被重新用作其他 transient texture，即使 projection pass 已声明但尚未在 GPU 执行，projection 也会读到被覆盖的数据。这不是 shadow 算法问题，而是 lifetime 违反了 producer-consumer 硬约束。

RDG 通过资源依赖、barrier、pass lifetime 与 transient allocator 避免这类错误；跨帧 pooled cache 和 VSM pool 还需要 fence/队列顺序与 cache ownership 共同保证底层资源没有被过早销毁或覆盖。

“最后 consumer”取决于实际分支：regular mask 可能被 deferred light、hair 或其他 pass 读取；VSM pool 可能被 projection、debug 或后续合法查询读取；RT mask 可能还经过 denoise。固定的 CPU 函数返回点不能替代真实 GPU 依赖终点；而 final consumer completed 也只结束当前资源版本的使用，不自动保证下一帧 cache 继续有效。

---

## 结语：阴影把遮挡证据交给 Lighting

本章的核心不是记住多少 shadow 类型，而是建立一条不会越级的生产—消费模型：

```text
light/view 提出需求并选择 route
  -> projected contract 或替代分支建立责任
  -> caster / sample / RT scene 输入准备
  -> cache 与资源分配确定写入位置和复用范围
  -> depth / trace producer 生成遮挡数据
  -> projection/filter producer 生成 attenuation 或 mask
  -> Lighting 绑定并形成 shadow terms
  -> 最后 GPU consumer 完成后资源才可安全退休或复用
```

CSM 是 regular shadow-map 路径中的采样密度分配方案；VSM 把资源管理细化为 demand、mapping、physical page 与 cache；MegaLights 把大量 local-light 查询改成 sample-driven；Contact Shadow 补屏幕空间近接触细节；ray-traced shadow 把 producer 换成 RT scene 查询。它们改变的是主链中的 route、producer、资源或 consumer 接口，而不是取消 producer-consumer 合约。

到这里，地面像素已经获得可由 Lighting 使用的 shadow terms。本章停在这个接口：下一篇将从 light gathering、light volume/clustered 路径与 deferred shading 出发，解释这些 shadow terms 怎样与 BRDF、材质和其他光照项共同形成最终直接光照。
