# 10 BasePass 与 GBuffer

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `01_Architecture.md`、`02_SceneProxy.md`、`03_ThreadModel.md`、`04_RHI.md`、`05_RenderGraph.md`、`06_GPUScene.md`、`07_MeshDrawCommand.md`、`08_FrameInit.md`、`09_DepthPrepass.md`。  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）

## 核心问题：BasePass 把可见表面生产为标准场景纹理

对熟悉 Unity 的读者，可以先把 BasePass 放在 deferred GBuffer pass 的位置，再补上 UE 特有的责任分层：它从 Renderer 已经准备好的当前 view 工作集出发，把每个满足条件的 opaque / masked 表面转换成后续系统能稳定解释的表面记录。

在桌面 deferred 主路径中，BasePass 的核心任务不是完成最终光照，而是把当前 view 中满足条件的 opaque / masked 表面，转换成后续阶段能够稳定解释的表面记录。它接收前序阶段已经建立的可见性、深度、对象数据和 draw 配方，执行材质求值，把结果编码进当前 GBuffer layout；随后由 Renderer 把这些资源纳入 `SceneTextures` 的标准读取接口。

```text
前序状态已成立
    ↓
输入合约：BasePass 知道画谁、怎样取数、怎样使用深度
    ↓
编码合约：表面语义被组织并编码进当前 GBuffer layout
    ↓
发布合约：Renderer 建立后续 pass 的 SceneTextures 读取入口
    ↓
    Lighting 以这份已发布的表面记录作为光照输入
```

三段的产物依次是可消费的 BasePass 工作集、符合当前 layout 的表面资源，以及后续 pass 使用的 `SceneTextures` 图内读取入口。它们由不同 owner 推进；录制、提交与 GPU 消费深度统一放在第 14 节。

### 本章范围

本章以 deferred opaque / masked GBuffer 路径为主线。Forward shading 不以完整 deferred GBuffer 作为同一种交付目标；Nanite 可以改变表面编码工作的生产方式，但仍向 deferred `SceneTextures` 消费合同交付数据；特殊材质、平台、Substrate 和 GBuffer layout 变体会改变具体目标集合与 payload 组织。

本章不会展开 Lighting 的 BRDF、完整材质编译系统或 Nanite 的 binning 算法。它只回答：**BasePass 如何把已经可处理的表面，变成可被后续阶段解释的数据。**

### Unity 读者的迁移地图

如果沿用 Unity deferred renderer 的直觉，最容易把 UE 的 BasePass 简化为“组织 draw、填 per-object 常量、写一组 GBuffer attachment”。这些动作并非不存在，但 UE 把它们拆到了不同生命周期和责任层。

| 熟悉的 Unity 直觉 | UE 中更准确的对应关系 | 调试时为什么重要 |
|---|---|---|
| 每帧现场组织 Renderer draw | 静态表面的 BasePass policy 可以提前形成 cached command，动态表面再走当前帧 command 路径 | 当前 view 没画出来，不一定是本帧“没遍历到对象”，也可能是缓存命令过期或 pass policy 不再匹配 |
| per-object constant 直接跟随 draw | primitive / instance identity 与 GPUScene 资源窗口共同提供对象数据 | command 正确不代表对象数据就是当前版本；旧 transform、错 slot 与缺 draw 属于不同故障层 |
| decal 在表面之后做一次 overlay | DBuffer 路径会在 BasePass 前生产属性 patch，再由 receiver / response 条件决定是否应用 | 看见 decal pass 执行，不代表 BasePass 的最终 BaseColor、Normal 或 Roughness 已被修改 |
| attachment 写完即可被后续 shader 使用 | BasePass 先生产资源，Renderer 再建立 `SceneTextures` publication，RDG 还要维护读取依赖 | Render target 中有值，不等于目标 consumer 绑定了同一资源版本 |

这张表不是 API 一一对应，而是提醒读者：UE 的问题经常发生在**形成时机、资源版本和发布边界**，不能只盯着一次 draw call。

## 贯穿案例：同一个红色金属球

全章只追踪场景中的红色金属球，并依次改变它的条件：普通 deferred opaque；有或没有完整 PrePass 深度；接收改变底色与粗糙度的 DBuffer decal；材质改成 Clear Coat；几何改由 Nanite 路径生产表面结果。

每次变化都只问四件事：谁拥有当前责任、输入数据是什么、控制权如何转移、最后成立了哪种可调试状态。

---

## 第一段：输入合约

### 1. 前序阶段向 BasePass 交付四类可消费输入

当前帧走到 BasePass 时，Scene publication、view 建立、可见性与 relevance 判断、pass-local command 准备已经把 Game Thread 世界转换成渲染侧数据和当前 view 的工作集。BasePass 从这个明确起点消费 command、对象数据、深度与条件性 DBuffer patch。

| 输入 | 主要生产者 | BasePass 怎样消费 | 缺席或失效条件 |
|---|---|---|---|
| BasePass command | BasePass-specific mesh processor 与 command 构建路径 | 提供 shader、PSO、binding、render state 与 draw 参数 | 对象未进入该 pass、材质被拒绝、缓存失效或动态命令未形成 |
| 对象与实例数据 | Renderer Scene、GPUScene 及相关资源发布路径 | shader 通过 primitive / instance identity 读取变换与 payload | identity、range、上传或资源窗口不一致 |
| SceneDepth | DepthPrepass、Velocity、Nanite 或 BasePass 自身 | 用于 depth test，并按当前策略决定是否继续写 depth | 前序深度不完整、对象未被前序 producer 覆盖或路径要求补写 |
| DBuffer patch | 启用并执行的 before-base decal 路径 | 在材质表面语义进入 GBuffer 前修改被允许的属性 | DBuffer 未启用、无有效 decal、primitive 不接收或 material response 不允许 |

这不是四件永远同时存在的固定物品。command 与对象数据是主路径基础；已有深度和 DBuffer 受 renderer mode、材质、平台与 feature 条件控制。

前序阶段负责生产可消费输入，BasePass 负责按当前条件消费它们。BasePass command 可能来自缓存期，也可能在当前 view 的动态路径形成；GPUScene 数据属于当前可用的复合资源窗口；DBuffer patch 和 pass-local command 主要服务当前帧或当前图。它们寿命不同，但在消费窗口内必须一致。

四份输入提供四种不同的故障分流：

| 现象 | 优先检查的输入合同 | 不应先归咎于 |
|---|---|---|
| 球体完全没有 BasePass draw | command 是否形成并被当前 view 选择 | GBuffer 编码公式 |
| 球体位置或实例数据像上一帧 | GPUScene identity、range 与资源 publication window | Processor 的 blend state |
| 球体深度测试异常或表面漏写 | 当前 SceneDepth 覆盖与 depth-stencil access | Lighting 解码 |
| draw 正常但贴花不改变粗糙度 | DBuffer producer、receiver 与 response chain | Nanite binning |

因此，“BasePass 输入不对”不是一个可直接调试的结论。必须先判断缺的是 command、对象数据、深度证据，还是条件性的表面 patch。

### 2. Processor 把候选 mesh 固化为 BasePass-specific command

`FBasePassMeshProcessor` 是 BasePass 的 pass policy。它读取已编译的材质结果、mesh / vertex-factory 信息与当前 renderer 条件，把候选 mesh 转换成适合 BasePass 的 command 配方。

```text
候选 MeshBatch
    ↓ 能否进入 BasePass
材质与 pass 条件过滤
    ↓ 使用哪种表面策略
shader / light-map / shading policy 选择
    ↓ 固化哪些状态
depth-stencil、blend、raster、PSO 与 binding 组织
    ↓
BasePass FMeshDrawCommand 所需状态
```

`AddMeshBatch` 把候选送入合适的决策分支，`Process` 把已选策略固化为 `FMeshDrawCommand` 所需的 shader、fixed-function state、bindings、sort key 与 draw parameters。静态路径可以缓存这份配方，动态路径则在当前 view 形成它；随后的 view selection、culling 与 recording 继续消费这份 command。

Cached 与 Dynamic 描述 command 的形成时机和寿命，不等于 Component Mobility。BasePass 只要求到消费时，command 与材质、场景身份和资源版本一致。

Processor 的决策不能压成一次简单的“接受或拒绝”。它至少要依次回答下面几类问题：

| 决策层 | 典型判断 | 固化到最终 command 的意义 |
|---|---|---|
| Pass eligibility | material domain、blend mode、main-pass relevance、当前 renderer path 是否允许进入 BasePass | 决定候选是否属于本 pass，而不是决定它是否存在于场景 |
| Material selection | 当前 material 是否可用，是否需要走 default material fallback | 决定实际参与 shader 与 state 选择的材质身份 |
| Lighting policy | 当前表面需要哪类 light-map policy 或无 light-map 路径 | 影响 shader permutation、uniform data 与后续 binding 组合 |
| Shader policy | vertex factory、material、shading path 与 permutation 条件 | 选择此前已编译、并与当前 BasePass 条件匹配的 shader 组合，固定这条 command 实际执行哪套 GPU 程序 |
| Fixed-function state | depth/stencil、blend、raster 与 PSO recipe | 固化 BasePass 对 depth contract 和颜色目标的使用方式 |
| Draw organization | shader bindings、sort key、draw parameters 与后续 culling 交接 | 让 command 能被 view 选择、排序、合批并进入 recording |

`AddMeshBatch` 更接近“进入哪条策略分支”，`Process` 更接近“把选定策略变成 command 所需状态”。default material fallback 说明 Processor 不是只做布尔过滤：某些情况下，它还会把原始材质选择转换为更适合当前 pass 的实际表示。light-map policy 也不是 Lighting 章节才突然出现的知识；BasePass 必须在形成 command 时确定 shader 如何获得相应的静态或间接光照输入合同，即使最终 BRDF 仍由后续阶段执行。

#### Cached command 的生命周期案例

假设红色金属球原本使用稳定的 opaque 材质，其 BasePass policy 已形成 cached command。现在运行时把材质改成另一套 domain、blend 或 shader permutation 需求：

```text
旧 cached command 仍描述旧材质合同
    ↓
材质 / render state 变化使缓存失效
    ↓
相关 command 需要重建或改走当前帧动态形成路径
    ↓
当前 view 再选择与记录新的 BasePass command
```

如果修改材质后的第一次绘制缺失或短暂使用旧表现，应沿“旧缓存失效 → 新 command 形成 → 当前 view 选择新 command”检查版本交接，再由第 14 节的完成深度账本判断工作实际推进到了哪里。

### 3. Depth contract 按前序覆盖选择 compare 与 write 责任

```text
已有满足当前表面需求的可信深度
    → BasePass 主要进行 depth compare
    → 在相应路径中使用 read-only 或受限写入策略

当前表面尚未被前序 producer 覆盖
    → BasePass 仍需承担该表面的 depth write

实际选择
    → 受 Early-Z 模式、masked、WPO/PDO、Nanite、平台与 renderer path 影响
```

“已有可信深度”必须对当前像素最终可见表面成立。若 masked cutout、顶点位移或像素深度修改使 BasePass 表面与前序深度不一致，固定使用 equal/read-only 会拒绝正确像素，或让深度与 GBuffer 不再描述同一表面。

#### 红色金属球：有无完整 PrePass

- 完整 PrePass 已覆盖球体：BasePass 可以用与该路径相符的 compare 策略复用深度；
- PrePass 没有覆盖球体：BasePass command 必须允许球体补写深度；
- 球体改成 masked 或发生改变轮廓的位移：必须重新检查前序深度与 BasePass 可见像素是否一致。

调试时要把 SceneDepth 中的球追溯到具体生产阶段和表面版本，再与 command 绑定的 depth-stencil access 对照。这样可以直接判断当前表面是复用前序深度，还是由 BasePass 补写。

### 4. DBuffer 是条件输入：先生产 patch，再决定是否应用

DBuffer 的教学重点不是记住固定 A/B/C 通道表，而是理解它是一份在 BasePass 表面编码前可被应用的属性补丁。

从资源形态上看，它仍然不是一组抽象布尔值。before-base decal 会把不同类别的表面修改编码到一组 DBuffer 纹理与有效性信息中，BasePass 再把这些资源当作当前像素的条件输入。典型实现会把颜色、法线、粗糙度等补丁分布到若干目标中，但具体通道和组合受平台、配置与材质路径影响，因此应记住“属性类别与有效 mask”，而不是背诵永久 A/B/C 槽位表。

```text
DBuffer feature 与当前路径有效
    ↓
before-base decal 阶段生产 patch
    ↓
primitive 允许接收 decal
    ↓
material response 允许对应属性被修改
    ↓
BasePass 材质求值应用 patch
    ↓
修改后的表面语义进入 GBuffer encode
```

这一步必须发生在 GBuffer payload 组织与编码之前，因为 DBuffer 修改的是 BaseColor、Normal、Roughness 等**表面事实**。这样 deferred Lighting 以及其他读取这些场景纹理字段的消费者，看到的是同一份已经应用 decal 的表面合同。另一种可行架构是在 Lighting 之后叠加颜色；它适合只改变最终画面颜色的效果，却不能等价修改法线、粗糙度或供多个后续 pass 共享的材质输入。UE 选择 before-base patch，是用额外的 DBuffer 生产和带宽成本换取跨消费者一致的表面语义。

任何一环不成立，都不应被简化成“BasePass 忘了读 decal”。资源可能存在但没有有效 decal 写入；decal 可能已写入但球体关闭接收；球体可能接收 decal，但材质 response 不允许修改粗糙度；legacy DBuffer 与 Substrate 路径也可能使用不同组织方式。

#### 红色金属球：贴花变体

假设 decal 把球体局部底色改暗、粗糙度提高。BasePass 不修改 Material Instance 的持久参数；它在当前像素求值期间读取允许的 patch，把原始表面语义变成修改后的语义，再送入 GBuffer 编码。

**第一段的产物：** 当前 view 获得了可执行的 BasePass command；每个 command 都能取得一致的对象数据，并携带与前序覆盖相匹配的深度策略和可选 DBuffer 输入。编码阶段将直接消费这份工作集。

---

## 第二段：编码合约

### 5. 材质节点图先编译成 GPU 程序，再交付当前像素结果

这里先把“材质节点图”说具体。它就是材质作者在 Material Editor 中连接出的计算关系：纹理采样、常量和数学节点共同决定 BaseColor、Metallic、Roughness、Normal 等材质输出。对贯穿本章的红色金属球，可以先想成红色输入连接 BaseColor，接近 `1` 的输入连接 Metallic，另一个数值连接 Roughness。节点和连线描述的是“这些属性怎样计算”，还不是某个像素已经算出的结果。

在对应 shader 被编译时，UE 会把这些节点关系翻译成材质 HLSL 计算代码，再与 BasePass 的入口、vertex factory 和当前需要的 shader 变体组合，形成 GPU 可以执行的 shader。这个阶段产生的是**计算规则和 GPU 指令**，不是预先算好的红色、金属度或粗糙度；只要材质使用纹理、参数或随像素变化的输入，GPU 仍要在绘制时为当前像素执行这些指令。

因此，BasePass 处理红色金属球的一个像素时，实际数据链是：

```text
Material Editor 节点图
    ↓ shader 编译：把“属性怎样计算”变成可执行 GPU 程序
与当前 BasePass 条件匹配的已编译 shader
    ↓ 当前像素执行：输入几何插值、view、primitive 与材质参数
FPixelMaterialInputs
    BaseColor = 当前像素算出的红色
    Metallic  = 当前像素算出的金属度
    Roughness = 当前像素算出的粗糙度
    Normal    = 当前像素算出的法线
    ↓ getter 读取并按接口规则约束这些字段
BasePass 获得用于组织 FGBufferData 的表面语义
```

`getter` 在这里就是“小型取值函数”。例如 `GetMaterialBaseColor` 从 `FPixelMaterialInputs.BaseColor` 读取当前像素的结果，并按该接口的规则约束返回值；`GetMaterialMetallic`、`GetMaterialRoughness` 承担同类职责。getter 不接收节点图，也不负责遍历节点，它消费的是本次像素调用已经算出的材质属性。

现在才适合说明“运行时解释材质图”这个边界。这里的“解释器”只是用来比较两种执行方式的通用软件概念，不是 UE 中某个名为“材质图解释器”的类或模块。若采用解释器方案，运行时需要保留节点图，再由一个通用程序读取节点类型、沿连线逐步执行；这种方案可以让图结构在运行时更自由地变化，但也会把节点分派和图遍历成本带进执行阶段。常规 BasePass 材质路径消费的则是已经编译好的 GPU shader，让编译器提前消除图遍历、优化材质计算，并针对目标平台和 pass 形成可执行程序。

所以这里真正要区分的是**执行编译后的指令**与**运行时读取图结构再解释执行**。UE 选择前者，不表示材质属性在运行前已经全部算好；它表示运行前准备好“怎么算”，绘制时再由 GPU 为各像素算出具体结果。

落实到 UE 的数据形态，`FMaterialPixelParameters` 描述当前像素所处的几何与渲染环境。`CalcMaterialParametersEx` / `CalcMaterialParameters` 补齐这份上下文，并执行已编译进当前 shader 的材质计算，把结果填入本次 shader invocation 的 `FPixelMaterialInputs`。随后 getter 读取这些结果，BasePass 再把它们组织进 `FGBufferData`。这些对象都服务当前 shader invocation，不是持久材质资产。第 20 章会展开节点图怎样生成材质代码；本章只需要走通 BasePass 怎样消费它。

更完整的数据轴是：

| 数据形态 | 它回答的问题 | 生命周期与边界 |
|---|---|---|
| Material Editor 节点图 | 材质的 BaseColor、Metallic、Roughness、Normal 等属性应怎样计算 | 属于材质资产的计算描述；在 shader 编译时参与代码生成，不是当前像素结果 |
| 已编译的材质 shader 代码 | GPU 应执行哪些指令，才能在给定像素上下文中算出材质属性 | 属于匹配当前材质、pass 与 shader 变体的可执行程序；代码与一次执行产生的数值不同 |
| `FMaterialPixelParameters` | 当前像素来自哪里，具有什么插值、坐标、视图与 primitive 上下文 | 服务当前 shader invocation，不是持久材质资产 |
| `FPixelMaterialInputs` | 已编译的材质代码对这组上下文求值得到了哪些材质语义 | 是当前 shader invocation 的材质求值结果，不是最终 GBuffer layout |
| Material getters | BasePass 以稳定语义接口取得 BaseColor、Metallic、Specular、Roughness、Normal、AO 等值 | 读取并按接口规则约束当前结果，不负责代码生成或节点遍历 |
| `FGBufferData` | 当前 shading model 希望向 deferred lighting 交付什么逻辑 payload | 与物理 MRT 分离，仍需 layout encode |

这些阶段不能折叠成“shader 算出颜色”。几何上下文错误、材质求值错误、getter 接口结果错误、payload 组织错误和 MRT 编码错误会产生相似画面，却属于不同修复点。

### 6. `FGBufferData` 把表面语义组织成待编码逻辑记录

BasePass 先把表面属性组织成逻辑记录，再由统一编码步骤映射到当前 layout。

```text
材质语义输出
    ↓
FGBufferData：逻辑表面记录
    ↓
SetGBufferForShadingModel：按模型组织 payload
    ↓
EncodeGBufferToMRT：映射到当前物理目标集合
```

`FGBufferData` 把“表面是什么意思”与“当前平台怎样存”分开。它可以包含底色、法线、粗糙度、金属度、ShadingModel 标识和条件化 CustomData，但它本身不是固定 MRT 槽位表。

#### 红色金属像素的 payload 账本

选取红色金属球上一个可见像素，先忽略贴花与 Clear Coat。材质求值可能交付下面这组逻辑语义：

| 语义 | 示例状态 | 进入 payload 后承担的任务 |
|---|---|---|
| BaseColor | 红色表面反射语义 | 作为后续模型解释的基础颜色，而不是此刻已经算好的受光颜色 |
| Metallic | 接近金属端 | 改变后续对基础颜色与镜面反射语义的解释 |
| Specular | 当前材质定义的镜面基准 | 与 Metallic、shading model 一起约束后续解码 |
| Roughness | 中等粗糙 | 描述高光分布所需的表面参数，不在 BasePass 中执行最终 BRDF |
| Normal | 当前像素的最终表面法线 | 必须与切线空间转换、WPO/normal map 和当前 layout 编码一致 |
| AO | 当前材质输出的遮蔽语义 | 作为 payload 输入交给后续消费者，具体使用方式由对应光照合同决定 |
| ShadingModelID | 普通 deferred lit 模型 | 规定这组字段应按哪种表面合同解释 |
| CustomData | 对普通模型可能无对应有效语义 | 只有模型和路径共同定义后才可读取 |

这张账本的调试价值在于分开两个问题：如果 getter 得到的 Roughness 已经错误，问题发生在材质求值或 DBuffer 应用之前；如果 `FGBufferData` 中 Roughness 正确、Lighting 看到的却像另一字段，问题更可能位于 layout encode、目标绑定或后续解码。

`SetGBufferForShadingModel` 是 legacy material 主路径中组织 model-specific payload 的关键位置，但不是所有路径唯一且最终的写入点。Substrate 或特殊路径可能随后补充或改写数据。稳定原则是：payload 的解释必须与当前 shading path 和 layout 一致。

### 7. ShadingModelID 决定 payload 怎样被解释

ShadingModelID 是随表面记录交给后续 Lighting 的语义标签：它规定 BaseColor、Roughness 和 model-specific 数据应按哪一种表面模型解释。记录这个标签不会在 BasePass 中立即执行 BRDF；真正的模型计算属于 Lighting 阶段。

CustomData 是留给特定 ShadingModel 的条件化 payload 空间。它的含义由四项条件共同成立：当前 ShadingModel 定义了对应数据，layout 提供有效编码位置，材质路径实际写入，consumer 再按同一合同解码。因此它不能脱离 model 与 layout 被当作任意自由字段读取。

#### 红色金属球：Clear Coat 变体

把普通金属球改成 Clear Coat 后，变化不只是多写几个数：生成材质代码产生 coat 语义；ShadingModelID 改变后续解释方式；model-specific 组织步骤把 coat 强度与 coat roughness 放入当前路径允许的 CustomData 表达；Lighting 以后按 Clear Coat 合同读取这些字段。

对照修改前后，可以看到哪些状态保持、哪些状态改变：

| 项目 | 普通金属 | Clear Coat 变体 |
|---|---|---|
| 几何与 primitive identity | 不变 | 不变 |
| BaseColor / Metallic / 主层 Roughness | 仍由材质语义提供 | 可以继续存在，并描述底层表面 |
| ShadingModelID | 普通模型的解释标签 | 改成 Clear Coat 对应的解释标签 |
| CustomData | 可能没有该模型定义的有效 coat 语义 | 承担 coat 强度、coat roughness 等路径定义的数据 |
| 后续 consumer | 按普通模型读取 payload | 必须先读取新的 model id，再按 coat 合同解释同一物理存储中的数据 |

所以“CustomData 有值”不能证明 Clear Coat 正确。若 ShadingModelID 没变，consumer 会按错误合同读它；若 model id 正确但 coat getter 或 payload 组织错误，问题仍在 BasePass 编码侧；只有两者一致，后续 Lighting 才具备正确解码的前提。

Clear Coat 只是最容易观察的一个例子。在 legacy、non-Substrate 的逻辑 payload 中，不同模型会把有限的 model-specific 存储解释成不同类别的数据：

| ShadingModel 类别 | model-specific payload 需要表达什么 | 调试时先确认什么 |
|---|---|---|
| Default Lit | 使用 BaseColor、Normal、Metallic、Specular、Roughness、AO 等常规 lit 字段，不依赖专用 CustomData 分支 | model id 与常规 deferred 表面字段是否匹配 |
| Unlit | 保留 Unlit 语义标签，并交付 emissive / SceneColor 路径需要的结果；不建立供普通 lit BRDF 使用的 payload | 不要把残留 GBuffer 内容当作 Default Lit 输入；实际目标清理受 layout / permutation 约束 |
| Clear Coat | coat coverage 与 coat roughness 一类第二层表面参数 | model id、coat getters 与 CustomData 是否同时成立 |
| Subsurface / Profile / Two Sided Foliage | 透射、次表面颜色、profile 或相邻的模型专用参数；具体组合由 model 决定 | 不要把一个 subsurface 模型的字段解释套到另一个模型 |
| Cloth | cloth / fuzz 相关的模型专用参数 | cloth model id 与对应材质输出是否一起写入 |
| Hair | hair tangent、backlit 或散射解释所需的模型数据 | 几何/材质输出与 Hair 解码合同是否一致 |
| Eye | iris / cornea 相关的模型专用数据 | Eye model id、专用输入与目标 layout 是否同属当前路径 |

这张表说明的是**语义类别**，不是固定 MRT 字段表。实际字段组合要由当前 ShadingModel、legacy/Substrate 路径和 layout 一起确定；因此调试时先确认 model id，再检查该模型对应的 payload producer 与 consumer，不能先从某个物理通道倒推全局含义。

本章到此只证明表面语义被编码，不展开双层高光怎样计算。

### 8. C++ target binding 与 HLSL MRT 是同一 layout ABI 的两面

把 GBuffer 记成“某属性永远在 MRT2”无法解释 velocity、Substrate、平台和 layout 变体。真正稳定的是四维 ABI：

| ABI 维度 | 必须一致的内容 | 不一致时的结果 |
|---|---|---|
| Target count | 当前 pass 实际绑定多少颜色目标 | shader 输出与 render pass 目标数量不匹配 |
| Slot order | 每个逻辑输出对应哪个 target slot | 属性写入错误纹理，后续整帧误解码 |
| Pixel format | 每个目标的位宽、类型与编码能力 | 精度、归一化或位解释错误 |
| Conditional presence | velocity、Substrate 或其他目标何时存在 | permutation 与资源集合不一致 |

C++ 侧按当前 SceneTextures / GBuffer 配置形成 render-target bindings；HLSL 侧按相应 compile environment 和 permutation 形成输出。二者必须由同一 layout contract 派生。

UE5.7 当前经典 deferred helper 可以作为条件化实例：它从 `SceneColor.Target` 开始组织颜色目标；GBuffer A-E 只有在当前 bindings 为相应成员分配了有效 index 时才加入，Velocity 也只有在当前 layout 给出有效 index 时才加入。这个实例把四维 ABI 落到可观察状态：C++ 绑定的目标数量和顺序，必须与当前 shader permutation 形成的 `Out.MRT[n]` 完全一致。

变体会主动改变这份实例。UE5.7 的 Nanite `DispatchBasePass` 以 `GBL_ForceVelocity` 请求包含 Velocity 的 layout；目标存在不表示每个材质都写 velocity，只有 WPO 或当前路径启用 BasePass velocity 等写入条件成立时，对应材质工作才产生有效 velocity。经典 raster 路径在 default layout 后按当前 Substrate 配置追加 Substrate MRT；Nanite 只有在 Substrate 已启用且当前平台没有使用 blendable GBuffer 时，才为 adaptive GBuffer 追加 top-layer 目标。于是“经典 default layout 正确”只能证明对应经典路径，不能替 Nanite 或 Substrate 变体证明 target set、slot order 与 shader outputs 已对齐。

一个**条件化的典型 deferred 实例**可以帮助理解 ABI，但不能当作永久槽位表：某种桌面 deferred 配置可能同时绑定 SceneColor、若干 GBuffer 属性目标和可选 Velocity；HLSL 则把法线、材质属性、底色、模型标识等编码到与该配置匹配的 `Out.MRT[n]`。当 ForceVelocity、Substrate、平台格式或 layout permutation 改变时，target 数量、slot 顺序、格式和存在性都可能随之改变。

这个实例只用于建立两个判断：第一，SceneColor 在典型路径中可能是布局的一部分，不能把 BasePass 绝对化为“只写 GBuffer”；第二，看到名为 GBuffer A–E 或 Velocity 的资源时，仍要回到当前 layout contract，而不能从字母或历史槽位推断永久含义。

`EncodeGBufferToMRT` 把逻辑 `FGBufferData` 映射到当前 ABI。编码结束只说明 shader 已形成正确输出意图；它不证明 RDG pass 已执行、资源已向后续 consumer 发布或 GPU 已完成。

### 9. 经典 raster 路径的编码状态链

```text
BasePass command 选择红色金属球 draw
    ↓
当前 view 把 visible command 与 GPUScene / Instance Culling draw parameters 对齐
    ↓
BuildRenderingCommands 形成当前 BasePass raster work
    ↓
RDG BasePass producer 声明目标、依赖与 raster pass
    ↓
DispatchDraw 把已选 command 记录为 RHI draw
    ↓
shader 获得 primitive、instance 与 material 上下文
    ↓
已编译进 shader 的材质代码产生红色、金属度、粗糙度与法线
    ↓
可选 DBuffer patch 修改底色或粗糙度
    ↓
ShadingModel-specific 规则组织 FGBufferData
    ↓
EncodeGBufferToMRT 按当前 layout 写 SceneColor / GBuffer targets
```

走到 `BuildRenderingCommands` 时，Processor 已经决定“应该怎样画”，材质也已经对应到可执行 shader。这里的责任是把当前 view 真正可见的 command、GPUScene identity、instance range 和 culling 输出收敛成可执行 draw parameters。RDG raster producer 再声明这些 draw 将写哪些 BasePass targets，`DispatchDraw` 才把工作推进到 RHI recording。这样经典路径与 Nanite 支线都明确经过“工作身份成立 -> 图中 producer 成立 -> 命令记录 -> GPU 执行”的交接，只是它们使用的 command 与 dispatch 形态不同。

SceneColor 是否参与、承载什么含义，同样取决于 shading path 和 layout。不能把“BasePass 只写 GBuffer”或“总按同一种方式写 SceneColor”当成无条件事实。

### 10. Nanite 改变生产路径，不改变后续消费合同

Nanite 使用自己的 BasePass 生产链：先建立 raster / visibility 结果，再把可见区域组织成 shading command 与 bin，最终在条件满足时通过 compute shading 把表面结果写入当前 GBuffer 目标。它改变的是表面结果的生产方式，而不是把普通 BasePass draw 原样替换成一次同形态调用。

这里的 material bin identity 是 Nanite 为 GPU shading 工作建立的执行分组身份：使用兼容 shader 与 binding 状态的可见区域被归到相应 bin，以便形成 GPU-driven dispatch。它描述的是当前 shading 工作怎样归组，不是 Component 上用于指定材质资产的 material slot。

```text
有效 Nanite raster result
    ↓
可见 material section 对应有效 shading command / bin
    ↓
binning 与 indirect dispatch 输入成立
    ↓
ShadeGBufferCS 等 compute shading 工作被加入 RDG
    ↓
按当前 GBuffer layout 写入表面结果
```

这条支线需要同时满足：当前 view 与 renderer path 启用并支持 Nanite；raster result 有效；material command / bin 与 shader 状态有效；indirect dispatch 参数已产生；平台和 shading path 支持对应 compute shading；RDG 为共享目标建立正确依赖与顺序；写入目标匹配当前 SceneTextures / GBuffer layout。

“Nanite 用 compute 写 GBuffer”不是无条件口号。任何前置状态不成立，都可能让问题停在 raster result、bin、indirect args、dispatch 或资源依赖层。

#### 两个可见材质区域怎样变成两个 shading bin

假设 Nanite 版本的红色金属球有两个可见材质区域：主体仍是普通红色金属，装饰环使用 Clear Coat。raster / visibility 结果首先回答“哪些 cluster 与像素可见”，但它还没有执行两套材质求值。接下来需要把可见结果重新组织成材质工作：

```text
可见 raster result
    ↓ 标记每个可见区域对应的 Nanite shading identity
普通红色金属区域 ──→ bin A：普通模型 shader / binding 状态
Clear Coat 装饰环  ──→ bin B：Clear Coat shader / binding 状态
    ↓
每个 bin 统计并形成自己的间接 dispatch 范围
    ↓
GPU 按 bin 执行对应 compute shading
    ↓
两类像素按同一当前 layout ABI 写入 deferred 表面目标
```

这里发生了一次关键数据换形：**几何可见性结果被重新分组成材质执行工作。** material bin 不是 Component 的材质槽，也不是已经完成的 draw；它是把可见像素范围、shading command、shader/binding 状态和 indirect dispatch 连接起来的 GPU 工作身份。

调试时若 raster result 有球体而只有装饰环缺失，应优先检查该区域是否获得正确 bin identity、对应 shading command 是否有效、bin 的 indirect range 是否非空，以及 dispatch 是否使用匹配的 layout。此时直接检查经典 BasePass MDC，往往找不到 Nanite 支线真正断开的状态。

Substrate、GBufferF 或 SGGX 一类特殊组织也不能被粗暴塞回“普通固定 GBuffer 槽位”。它们可能扩展目标集合、改变 payload 组织或使用额外数据表达。本章只要求确认：特殊路径仍必须给当前 consumer contract 提供一致的资源、layout 与解释规则；具体材质与存储算法留给后续专题。

#### 红色金属球：Nanite 变体

把同一个球体换成 Nanite 几何后，材质语义仍然是红色、金属与指定粗糙度，Clear Coat 或 DBuffer 条件也仍需按当前路径处理。经典路径由 mesh draw command 驱动 raster shader；Nanite 路径由 raster result、material bin 和 indirect dispatch 驱动 compute shading。

两条路径不共享同一种 command 生产机制，却交付同一类 deferred `SceneTextures` 消费合同。具体 layout 可以采用相应变体，不能表述成所有平台都写完全相同的固定纹理集合。

**第二段最后成立的状态：** 经典 raster 或满足条件的 Nanite compute shading 已拥有生产当前表面记录所需的工作与目标关系；相关 RDG 工作执行时，GBuffer 资源会获得对应内容。仅凭 shader 编码路径，仍不能宣称后续 pass 已通过标准接口读到资源。

---

## 第三段：发布合约

### 11. BasePass 生产资源，Renderer 建立 SceneTextures publication

责任必须在这里切开：

- BasePass producer 负责让 SceneColor / GBuffer targets 获得表面内容；
- Renderer 的后续资源组织步骤负责扩展合适的 `SceneTextures.SetupMode`，并重建或更新 SceneTextures uniform buffer；
- 后续 pass 通过标准接口声明并读取所需场景纹理。

```text
BasePass targets 已纳入 RDG 生产关系
    ↓
Renderer 确认后续阶段应暴露哪些 SceneTextures 成员
    ↓
SetupMode 扩展到包含相应 GBuffer 读取合同
    ↓
SceneTextures uniform buffer 按新资源集合建立
    ↓
后续 pass 获得标准图内读取入口
```

`SetupMode` 是接口承诺暴露哪些资源的内容清单，不是 GPU 完成证据。uniform buffer 建立也不表示像素已经在物理时间上执行完毕；RDG 仍依靠资源依赖保证 consumer 在 producer 之后读取正确版本。

在 deferred 主路径中，GBuffer publication 在 BasePass 生产之后建立。Forward 或自定义路径可能没有相同资源集合，也可能在不同位置组织资源，因此不能把单一 setup bit 或调用位置写成所有 renderer path 的永久公式。

### 11.1 从图内 producer 到 GPU consumer 的完成链

SceneTextures publication 建立的是**图内资源合同**。同一批工作在执行时间线上还要继续经过以下状态，后面的状态不能由前面的状态代替：

```text
RDG work declared
    pass、目标资源与 producer→consumer 依赖进入图
        ↓ RDG compile / execute
RHI recorded
    BasePass draw、barrier 与绑定被记录为 UE RHI 工作
        ↓ Dynamic RHI / backend
Platform commands formed
    RHI 工作被编码成目标图形 API 的 command buffer / command list
        ↓ device queue handoff
Platform Queue Submit
    平台命令与同步关系交给 graphics/compute queue
        ↓ GPU executes producer
GPU BasePass resources produced
    SceneColor / GBuffer 的目标版本获得实际像素内容
        ↓ GPU executes dependent consumer
GPU consumer consumed
    Lighting 或其他实际声明读取该 SceneTextures 版本的 dependent pass 完成读取
        ↓ completion evidence covers the last consumer
Final consumer completed
    当前资源版本才允许按生命周期规则退休、覆盖或复用
```

RDG/Renderer 拥有图内声明与 publication；RHI/backend 拥有命令记录、平台命令形成和 queue handoff；GPU producer 与 consumer 拥有实际资源写入和读取。GPU capture 可以观察资源内容与 pass 执行；已经 resolve / read back 的 timestamp 可以定位 GPU 到达某个标记的时段，但不自动授权资源退休。只有 signal 排在最后 consumer 之后、且已经完成的 fence 或等价 completion evidence，才能支持当前资源版本退休、覆盖或复用。普通 immediate flush 即使推进了命令提交，也不自动等待 GPU 完成，因此不能把 CPU 返回点当作资源退休证据。

### 12. “可读”有三种不同含义

| 层级 | 成立状态 | 还不能证明什么 |
|---|---|---|
| 资源身份可绑定 | consumer 能通过标准接口引用目标资源 | 不证明 producer 已在执行顺序上先完成 |
| RDG 图内可读 | producer/consumer 依赖与资源版本正确 | 不证明 GPU 已完成整帧执行 |
| GPU Completion | 覆盖最后 consumer 的完成证据已成立 | 不自动证明语义编码或材质内容正确 |

SceneTextures publication 只说明后续阶段拥有正确的图内读取合同。它不是 Scene publication，不是 command-list control transfer，不是 Platform Queue Submit，更不是 GPU Completion。

### 13. Depth 的可读形态不应被过度物理化

SceneDepth 在不同路径中可能需要 resolve 或其他可读形态准备。应把它理解为逻辑使用边界：attachment 写入表达与 shader-readable 表达可能不同，Renderer / RDG 必须为 consumer 建立合法资源关系。

这不意味着每个平台都必然执行一次独立的整纹理物理复制。单采样、别名、memoryless 和平台实现都可能改变底层动作。稳定的调试问题是：consumer 实际读取哪个资源表达和哪个内容版本，而不是“是否看见一次 copy”。

**第三段最后成立的状态：** Renderer 已把 BasePass 生产的表面资源纳入后续 pass 的 `SceneTextures` 读取合同，并由 RDG 维护生产者到消费者的图内顺序。Lighting 获得了可读取输入，但其光源选择、阴影、BRDF 与最终颜色仍未被证明。

---

## 14. Last-valid-state：沿最后成立状态调试

当红色金属球消失、颜色错误、贴花无效或只在 Nanite 版本出错时，先找到最后一个确定成立的状态。

| 最后成立状态 | 能证明什么 | 不能证明什么 | 下一检查点 |
|---|---|---|---|
| BasePass 候选存在 | view/relevance 没有把球体完全排除 | 不证明 processor 接受材质 | BasePass filter 与材质条件 |
| Processor 产出 command | shader、PSO、binding 和 pass state 可以形成 | 不证明当前 view 执行 command | cached/dynamic 选择、culling 与 recording |
| 对象数据窗口有效 | shader identity 能找到 primitive / instance 数据 | 不证明材质语义正确 | material inputs 与资源版本 |
| Depth contract 合法 | compare/write 策略与已有深度匹配 | 不证明颜色目标正确 | render-target layout |
| DBuffer patch 已产生 | before-base decal 资源包含修改 | 不证明球体会应用它 | receiver 与 material response |
| Material getters 正确 | 生成材质代码给出预期语义 | 不证明 payload 组织正确 | ShadingModelID 与 CustomData |
| `FGBufferData` 正确 | 逻辑表面记录正确 | 不证明 MRT 物理编码正确 | layout ABI 与 encode |
| GBuffer target 有内容 | producer 在某个时刻写入资源 | 不证明后续读取同一版本 | SetupMode、uniform buffer 与 RDG dependency |
| SceneTextures publication 成立 | 后续 pass 有标准图内读取入口 | 不证明 Lighting 或 GPU completion 正确 | Lighting 输入解释与后续完成证据 |

### 典型症状如何下钻

**球体完全不出现：** 检查 command 是否存在、processor 是否接受、当前 view 是否选择，以及 depth compare 是否拒绝所有像素。不要先进入 Lighting，因为表面记录可能根本没有产生。

**底色或粗糙度错误：** 比较 material getter 输出与 DBuffer 应用前后语义，再检查 `FGBufferData` 和 layout encode。逻辑记录正确而纹理通道错误时，问题更可能位于 slot、format 或 permutation 对齐。

**Clear Coat 只表现成普通金属：** 检查 ShadingModelID、CustomData 写入条件和后续解码合同。CustomData 有数值不证明它按 Clear Coat 语义解释。

**decal 资源有值但球体不变：** 沿 DBuffer feature、decal 写入、primitive receiver、material response 和当前 shading path 的条件链检查。

**只有 Nanite 球体出错：** 先证明经典与 Nanite 共用的材质语义、GBuffer publication 和 Lighting consumer 正常，再下钻 raster result、shading command/bin、indirect args、`ShadeGBufferCS` 与 RDG ordering。

**GBuffer capture 正确但后续读取异常：** 检查 capture 对应的阶段与资源版本，再检查 `SetupMode`、SceneTextures uniform buffer 和 consumer dependency。目标纹理某一时刻有值，不证明 consumer 读取的是同一份已发布版本。

---

## 15. 三段合约回看

### 输入合约

BasePass 接收 pass-specific command、对象数据、条件化深度状态与可选 DBuffer patch。Processor 固化 BasePass draw 配方，不是最终 GPU 完成。Depth contract 必须按前序覆盖、材质和路径条件选择。

### 编码合约

生成材质代码通过 getter 交付表面语义；`FGBufferData` 组织逻辑记录；ShadingModelID 与 CustomData 规定解释方式；encode 按 target count、slot order、pixel format 和 conditional presence 四维 ABI 写入当前 layout。Nanite 可以使用不同生产支线，但交付同一类 deferred 消费合同。

### 发布合约

BasePass 生产表面资源，Renderer 扩展 SceneTextures 的暴露范围并建立标准读取入口。publication 证明图内接口和依赖可以成立，不证明 Platform Queue Submit 或 GPU Completion。

## 章节出口：把已发布表面交给 Lighting

走出本章时，红色金属球完成了从 pass command、材质语义到 GBuffer payload 的转换，并通过 SceneTextures 获得后续读取入口。

这里仍没有证明哪些光源影响它、阴影是否正确、ShadingModel 对应 BRDF 是否按预期执行、最终 SceneColor 是否正确，或 GPU 是否完成最后一次消费。

第 11 章会先为这些已发布表面补齐可由 Lighting 消费的遮挡证据；第 12 章 Lighting 再联合读取表面描述、光源数据与 shadow terms。ShadingModel、法线、底色、粗糙度和金属度如何进入最终光照，是后续章节的责任，而不是 BasePass 的隐藏工作。
