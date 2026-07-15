# 21 ShaderSystem：VertexFactory 与 Shader 绑定

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `04_RHI.md`、`05_RenderGraph.md`、`06_GPUScene.md`、`07_MeshDrawCommand.md`、`10_BasePass.md`、`20_MaterialPipeline.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `21_ShaderSystem_CoverageMatrix.md`

如果你来自 Unity，关于“一个材质怎么变成屏幕上的像素”，脑子里多半是一条比较平的链路：引擎导入 Shader asset，扫描里面的 SubShader、Pass 和 keyword，生成一批 variant；画一个物体时，把它的 vertex layout、材质属性凑齐，调一次 `DrawRenderer` 就完事。这条直觉里，shader 是“被文件里的声明发现的”，参数是“画的时候按名字塞进去的”，顶点格式只是“数据怎么摆”。

这套心智模型搬到 UE，会在几个地方同时崩。最典型的就是：**同一个材质，在普通 StaticMesh 上画得好好的，换到 Landscape、骨骼网格、Instanced Mesh 或某个自定义网格上，就突然“没有 shader”或者画错。** Unity 的直觉解释不了这件事——明明是同一个材质、同样的 pass。

UE 的正向模型是一条逐层生产、逐层消费的状态链。C++ 注册先产出可枚举的 shader type；layout 与 material/VF/shader 条件再从类型全集里选出应存在的编译坐标；编译系统把这些坐标变成 shader-map 结果；参数 metadata 与编译输出共同固化 binding records；mesh processor 把 shader refs、VF 输入和 bindings 收敛进 `FMeshDrawCommand`；submit 最后补齐 PSO 上下文并记录 RHI 工作。

```text
shader type declaration / registration
  -> permutation + VF + material eligibility
  -> compile jobs -> shader-map result -> GT install -> RT publication
  -> parameter metadata + compiled allocations -> binding records
  -> MeshDrawCommand build / dispatch-side consumption
  -> RHI recording -> platform queue submit -> GPU consumption
```

这张图先规定每一层的责任。registry 由模块启动期生产、进程级使用，失败时根本枚举不到 type；eligibility 由 layout caller 与 material/VF/shader predicates 推进，输出“当前坐标应被要求”，失败时不会产生相应 job；compile manager 与 shader map 消费这些坐标，输出可由 GT 安装并发布到 RT 的 map/result，失败时 lookup 没有可用 entry；parameter metadata 与 compiler allocation 在 shader 创建阶段形成 records，records 随 shader 结果存在，错配时在 bind/validation 或 runtime write 暴露；mesh processor 消费已发布结果与 VF/owner 数据，输出可缓存或逐帧使用的 draw recipe；RHI command list 消费 recipe，在当次渲染工作内记录状态和 draw，之后才由更深层的提交与 GPU 执行继续推进。后文每一节都只展开这条链上的一段。

本篇要回答的核心问题就是：**一次真实的 mesh draw，怎样从已完成 RT publication、对当前材质 `complete for material` 的 shader map 里取出正确结果，配上 VertexFactory、参数绑定和 pipeline state，最终记录成可提交的 RHI 工作。** 第 20 篇已经解释 identity、layout、编译结果与 GT/RT publication；本篇把 shader map 当作已发布输入，不重复节点图翻译。

本章的终点也要先限定：我们会走到 `FMeshDrawCommand` 建好，并在 submit 时解析出可用 PSO、vertex streams 与 shader parameters，记录相应 RHI commands。状态链的正向终点依次是 **RHI recorded -> command list transferred -> platform queue submitted -> GPU consumed**；每个终点都有自己的推进者和证据，后一个尚未发生时，前一个仍然是合法的最后有效状态。

## 贯穿全篇的例子

整章只跟一个对象：一个普通 StaticMesh，材质是红色、金属度高、粗糙度偏高的 opaque 材质，顶点工厂是 `FLocalVertexFactory`，进入 BasePass 时需要 `TBasePassVS` 与 `TBasePassPS`。我们跟踪它从 type registry、compile coordinate、map result、VF input、parameter records 到 MDC/PSO 的状态变化；每一站都问 owner、data、control 与 lifetime。

读完本篇，你应该能回答下面这些问题——它们也是后面每一节的引子：

- 为什么 UE 需要 C++ 注册 shader 类型，而不是扫描 `.usf` 文件。
- Global、Material、MeshMaterial 三类 shader 的边界，到底是按什么划的。
- 为什么 `TBasePassVS` / `TBasePassPS` 属于 mesh material shader，而不是普通 material shader。
- 变体爆炸怎样由 permutation domain、VF/shader/material gates 与 layout caller 共同管住。
- 编译结果存在哪、运行时按什么 key 取出来，为什么换个顶点工厂就可能取不到。
- shader type、compile job、`FShader`、shader map、`TShaderRef` 与 RHI shader 各在哪一层。
- shader 参数怎样经过 metadata/layout、compiled parameter map、binding records 与 runtime write 四步。
- VertexFactory type、instance data、stream component、vertex declaration、input stream 与 VF shader resource binding 为什么不能混称一种 binding。
- 一次 BasePass draw 的参数，到底来自几个不同的“所有者”。

## 本篇边界

本篇只讲 **ShaderSystem 如何把 C++ shader 类型、材质编译结果、VertexFactory 和参数绑定，收敛成一次可提交的 mesh draw**。相邻系统只在这条主线里说明它当前扮演什么角色，深入留给各自的专篇：

| 概念 | 本篇怎么处理 | 深入出处 |
| --- | --- | --- |
| 材质节点图如何生成 HLSL | 直接用“第 20 篇已生成好的 material shader map” | 第 20 篇 |
| MeshDrawCommand 的排序、合批、缓存生命周期 | 讲本章输入如何进入 command，不重讲全套排序系统 | 第 07 篇 |
| RDG 资源生命周期、barrier、transient alias | 只讲 RDG 参数是 shader 参数的一类来源 | 第 05 篇 |
| RHI 后端 root signature、descriptor heap | 讲到 RHI shader、PSO 与 `FRHIBatchedShaderParameters` 的抽象边界 | 第 04 篇 |
| BasePass 的 GBuffer 编码和 BRDF | 只把 BasePass 当作 shader system 的消费端 | 第 10 / 12 篇 |
| Compute dispatch 模型 | 只在结尾说明它复用注册和参数机制、但没有 VF 这条轴 | 第 22 篇 |

这条主线本身就是调试路线。遇到 shader missing、参数错位、某种 mesh 画错或 PSO active skip，先判断最后有效状态：type 是否注册、binary 是否存在、map 是否可见、lookup 是否成功、VF declaration/stream/resource 哪类绑定正确、parameter records 是否匹配、PSO 是否 ready、RHI 是否 recorded。末尾会把它们合成 last-valid-state 表。

## 1. 第一步：为什么 UE 要先“注册” shader，而不是先找 HLSL 文件

要画 `TBasePassVS`，UE 首先得知道世界上存在这么个 shader。这正是 Unity 直觉踩空的第一脚：在 Unity 里，引擎从 Shader asset 入手，扫到 pass 和 keyword 就生成 variant；shader 是被文件发现的。UE 不是这样起步。

UE 的设计取舍是：**让 C++ 代码主动声明 shader 的存在，HLSL 文件只当源码材料。** 一个 shader class 通过声明宏和实现宏，把自己注册成一个“shader 类型”。这个注册出来的运行时身份，可以理解成“shader class 的身份证”——它记着这类 shader 的名字、源文件、入口函数、shader frequency（顶点还是像素等）、有多少变体、参数长什么样、怎么构造、用什么钩子过滤编译，以及它属于 Global、Material 还是 MeshMaterial 哪一类。注意此时还没有任何 GPU shader，也没有任何具体材质值；UE 拿到的只是一份“将来如何为这个 shader class 枚举和构造编译任务”的说明书。

为什么要把这件事做得这么重？因为 UE 必须在**真正编译和缓存之前**，就掌握完整的类型集合：

- 编译结果要分层存放，得先知道要枚举哪些 shader 类型、哪些变体；
- DDC（编译缓存）的 key 要包含 shader 类型、源码 hash、变体和依赖，少一样缓存就可能命中错的二进制；
- 编译任务要知道入口函数、frequency、参数布局；
- 运行时查询要按类别判断它走哪条查找路径；
- 而且这套类型集合一旦定下来就不能被晚到的模块偷偷改动。

一个 `.usf` 文件提供不了这些。它没有 C++ 构造函数、没有过滤钩子、没有参数结构描述，也不知道自己该挂到哪份编译结果上。所以“声明一个可查询的 shader 类型”这件事，只能由 C++ 承担。

注册的生命周期不是“静态收集后一次初始化”两步，而是三个责任不同的阶段：

```text
static collection
  -> 静态 FShaderTypeRegistration 只保存 lazy accessor
  -> 此时复杂 FShaderType 尚未由该 accessor 构造/注册

FShaderTypeRegistration::CommitAll
  -> 调用每个 lazy accessor
  -> 构造实际 FShaderType，加入 name/type/sorted registries
  -> 清空 pending registrations，并关闭 late registration 窗口

InitializeShaderTypes
  -> 基于已固定的 shader/VF/pipeline type 集合
  -> 建立 source、uniform-buffer、VF 与 pipeline 等派生初始化状态
  -> 这是注册后的派生初始化，不是第二次注册
```

这里有一个对插件和模块开发非常实际的约束：shader 类型所在模块必须在 `CommitAll` 关闭注册窗口前加载。注册对象在类型已经初始化后才出现会触发 late-load 断言，并提示使用 `PostConfigInit` 一类足够早的模块阶段。类型集合影响 serialized layout、DDC 与枚举结果，运行中随意追加会使旧 map 无法解释新集合；UE 因而选择启动期固定 registry。显式 manifest/codegen registry 也能解决静态初始化顺序，代价是增加构建步骤；真正支持 hot registration 则需要 map/layout 版本化与重新初始化，UE 当前路径不提供这种能力。

回到金属 StaticMesh：静态收集只证明 pending registration 存在；`CommitAll` 后，`TBasePassVS/PS` 的实际 type 才能按名查询；`InitializeShaderTypes` 后，编译系统依赖的派生 source/UB 状态才 ready。三个 last-valid-state 对应三种排查：没有 registration instance 查模块/宏；Commit 后 type 不可查查构造注册；type 可查但 source metadata 未 ready 查派生初始化。

> **调试心智**：如果一个 shader 类型看起来“根本不存在”，先查类型注册和模块加载，别先查 HLSL。HLSL 报语法错，至少说明编译器看到了源码；而类型没注册时，对应的编译任务可能压根没被枚举出来——你查 HLSL 是查不到的。

## 2. Type、job、FShader、shader ref 与 RHI shader

注册成功只得到**类级说明书**，不是某个平台上的 GPU binary。为避免后文把“type exists”和“shader ready”混为一谈，先固定这条生命周期：

```text
FShaderType
  -> layout/caller 枚举某个 platform/material/VF/permutation coordinate
  -> compile job
  -> compiler output + parameter map + shader code
  -> FShader instance 插入某个 shader-map content
  -> shader-map resource/code 管理底层可创建的 RHI shader
  -> TShaderRef 把 FShader content 与所属 map/resource 关联
  -> GetRHIShader... 取得特定 frequency 的后端 shader resource
```

`FShaderType` 的 lifetime 近似模块/进程级，它描述 source、entry、frequency、permutation count、构造与过滤策略。`FShader` 则是某个具体编译坐标的结果对象，带 type/VF/permutation 语义、parameter binding 信息与 resource index，并由 map content 索引。一个 `TBasePassPS` type 可以产生多个 platform、material、permutation 与 VF 结果，不能因为 type 已注册就断言某格 binary 存在。

shader map 同时保存冻结 content 与 resource/code。`TShaderRef` 不只是一个缺少上下文的 shader 指针：它把 shader content 与所属 map 关联，因而能经 map resource 取得对应 RHI shader；但 ref 本身不应被理解成独立 lifetime owner，material/map/command 上下文仍必须保证资源在消费期间有效。RHI shader 是后端可绑定的阶段资源；即使它存在，graphics PSO 仍需与其他 stage、vertex declaration、render targets 和 fixed-function state 组合。

UE 选择“type metadata、编译结果、map owner、RHI resource”分层，是因为一个类型会产生大量可共享结果，而相同结果又会被多个 draw 引用。固定 shader 集的小引擎可以把 type 与 binary 合成一个 asset，模型更短；材质/VF/permutation 矩阵下，这会把复用与 cook key 隐藏在 asset 变体内部。

**Worked case。** 对红色材质的 LocalVF BasePass，完整生产链是：已初始化的 `TBasePassPS` type 先被 cached layout 选入候选；具体 material 再决定该坐标是本次编译要求；`BeginCompileShader` 一类入口建立带 platform/material/VF/permutation key 的 job；结果处理把 compiler output 加入 map resource code，用它构造 `FShader`，再插入 LocalVF 对应的 `FMeshMaterialShaderMap`；完整 map 先安装到 GT，再由 render command 发布给 RT；BasePass lookup 最终取得关联 map/resource 的 shader ref。type 近似进程级，job 只活到编译结果处理，`FShader` 与 code 受 shader map 管理，RT 消费期则由 map/ref-count 边界保护。

这条正向链也给出了失败症状：候选或 material gate 未要求该坐标时没有 job；job 失败时没有可插入结果；结果已进 map 但尚未 RT publication 时 renderer 仍看旧 map/fallback；RT lookup 已取得 ref 后，下一消费者才是 MDC/PSO 路径。这样，“type exists”“binary exists”“RT-visible”“shader ref selected”分别有了可验证的完成点。

## 3. 三类 shader 的边界，划在“它依赖什么”

类型注册回答了“有哪些 shader 存在”，紧接着的问题是：这个 shader 属于哪一类？UE 把 shader 分成 Global、Material、MeshMaterial 三类，但这不是为了归档好看。**分类的依据是依赖关系，而依赖关系直接决定了它的编译结果存在哪、运行时怎么查、画的时候能拿到什么。**

按“依赖什么”从轻到重排：

- **Global shader** 不依赖材质或具体 VF。结果按 shader platform 进入 global shader map，适合全屏、compute 工具、copy、debug、LUT 与后处理工作。
- **Material shader** 依赖材质翻译结果，但不依赖顶点输入策略。结果进入 `FMaterialShaderMapContent` 的非 VF 分区。
- **Mesh material shader** 同时依赖材质与 `FVertexFactoryType`。结果仍属于 material shader-map content，只是按 VF type 分进 `FMeshMaterialShaderMap`；这个分区不由某个 VF **实例**拥有。BasePass、DepthPass、Velocity 等 mesh shader 通常属于这一类。

这里有个最容易踩的坑：**类别不是看实现宏的名字猜出来的，而是声明时显式指定的。** BasePass 这两个 shader 的实现宏名字里带着 “Material” 字样，但它们在声明时指定的类别是 MeshMaterial。所以它们走 mesh material shader 这条路：编译和查询都要带上一个额外的维度——顶点工厂类型。

为什么这条边界这么要紧？因为它直接决定了运行时能不能拿到 shader：

```text
Global shader
  -> 不看材质，不看 VF

Material shader
  -> 看材质编译结果
  -> 不按 VF 再分层

Mesh material shader
  -> 看材质编译结果
  -> 还要看顶点工厂类型
  -> 查询时必须落到“某个材质 + 某个 VF”的那一格
```

类别选错，代价不是“名字不好看”这么轻。一个本该依赖 VF 的 shader 如果被做成 global shader，它就进不了材质的编译结果，也拿不到 mesh draw 给的顶点工厂绑定；反过来，一个本不依赖材质的工具 shader 如果被做成 material shader，它会平白无故地乘上材质这个维度，凭空增加编译量和缓存负担。

三类 map owner 的 lookup key 也不同：global 路径主要是 platform + type/permutation；material 路径增加当前 material identity；mesh material 路径再增加 VF type。分类的目的正是只把真实依赖维度乘进 key。统一用一个 shader asset + keywords 可以缩短 API，但会把依赖边界藏在约定里，编译冗余与 missing 诊断更难归属。

把 `TBasePassVS` / `TBasePassPS` 做成一个概念护照，它们在本章里承担的职责是：

```text
它们是什么：
  BasePass 用来画 mesh 的 mesh material shader 类型。

它们为什么需要 material：
  像素阶段要 include 第 20 篇生成的材质代码，才能算出 BaseColor、Metallic、Roughness 等属性。

它们为什么需要 VertexFactory：
  顶点阶段要知道 StaticMesh 的位置、法线、切线、UV、primitive id 怎样进入 shader。

它们被谁消费：
  BasePass mesh processor 查询到 shader refs，再交给 MeshDrawCommand 固化。

它们坏了先查什么：
  shader 类型是否注册、类别是否是 MeshMaterial、当前 VF 分区是否编出目标 permutation。
```

有了这张护照，后面再看到 “BasePass 取不到 shader”，就不应该笼统理解成“材质没编译”。更精确的问题是：这两个 mesh material shader 类型，在当前材质的 `FMeshMaterialShaderMap(FLocalVertexFactory)` 里，有没有对应变体。

> **Unity 对照**：在 Unity 里你习惯从 ShaderLab pass 或 SRP pass 的角度看 shader。UE 更强调“这个 shader 的依赖边界在哪”——依赖材质，就进材质编译结果；还依赖顶点输入策略，就再多吃一个顶点工厂维度。把“pass 视角”换成“依赖边界视角”，后面很多现象才解释得通。

## 4. ShaderMap owner、lookup 与 GT/RT visibility

类型注册告诉 UE 有哪些 shader，分类告诉 UE 它们依赖什么。但画的时候真正要问的是另一件事：**编译好的二进制存在哪，怎么按 key 取出来？**

UE 用分层 shader map 回答。map 同时提供 frozen content 的索引和底层 shader resource/code 的 lifetime 边界；它不是 VF instance 的附属数组。不同类别结果落在不同 owner：

- global shader 的结果放在全局 shader map；
- 材质相关的结果放在材质的编译结果里（也就是第 20 篇生成的那份 `FMaterialShaderMap`）；
- mesh material shader 的结果，还要再按顶点工厂类型，分进 `FMeshMaterialShaderMap` 这一层。

对金属 StaticMesh 来说，第 20 篇的编译已经产出了一份 `FMaterialShaderMap`，它代表“这个材质在当前平台、feature level、quality、静态开关、源码依赖和变体标志下的整个编译世界”。但 BasePass 想取 `TBasePassVS` / `TBasePassPS` 时，光有这份还不够——因为它们是 mesh material shader，查询必须再带上 `FLocalVertexFactory` 这个顶点工厂类型，才能落到正确的那一格。

运行时查询的 key，可以用这个近似模型理解：

```text
材质编译结果
  + shader 类型
  + 变体编号（permutation id）
  + 顶点工厂类型（仅 mesh material shader 需要）
  + 平台 / 变体标志 / shader map 身份（已隐含在这份结果里）
  -> 取出一个 shader 引用
```

这里还藏着一条**线程与状态边界**。`FMaterial` 同时跟踪在途 compiling id、GT installed map 与 RT-visible map；map 采用引用计数和延迟清理保护跨线程 lifetime。GT complete 后到 RT publication 前存在合法时间窗，Render Thread 仍只能使用旧 map/fallback。查询必须读取当前线程允许的引用，不能从另一侧偷看并把未发布状态当作 renderer 已可用。

所以 BasePass 消费编译结果时，“材质有 shader 了”并不等于“能画”。它要同时满足一串条件：

```text
当前线程能看到的 shader map 已发布
  -> 这份 shader map 对当前材质 `complete for material`
  -> 请求的 shader 类型和变体确实在里面
  -> 如果是 mesh material shader，当前顶点工厂类型下也有
  -> TShaderRef 所需 map/resource lifetime 仍有效
```

这就解释了开头那个让 Unity 直觉崩掉的现象：**同一个材质，在普通 StaticMesh 上能画，在 Landscape、骨骼网格、Instanced Mesh 或某个自定义 VF 上缺 shader。** 因为材质的编译结果不是只存“一份最终 pass”；mesh material shader 的可用性是落到具体顶点工厂组合上的——某个 VF 那一格没编出来，那个 VF 就没 shader。

把这个现象具体化：红色金属材质先挂在普通 StaticMesh 上，BasePass 查询的是“当前材质 shader map + `TBasePassPS` + 当前 permutation + `FLocalVertexFactory`”。这格存在，所以能画。你把同一个材质换到 Landscape 上，材质本身没有变，但查询 key 的最后一项变成 Landscape 对应的 VF 类型；如果那一格没有通过过滤、没有进入 layout，或编译结果还不完整，BasePass 就会取不到 shader。这里变化的不是材质颜色，不是 pass 名字，而是**顶点工厂轴上的坐标**。

所以“同一材质换 mesh 后坏了”的最小状态账本是：

```text
material shader map 身份：基本不变
shader type / permutation：通常仍是 BasePass 那组
vertex factory type：从 LocalVF 换成另一种 VF
lookup result：换到新的 FMeshMaterialShaderMap 分区
debug question：新的 VF 分区里是否存在目标 shader?
```

**Publication worked case。** compile manager 已使 GT map complete，但 render command 尚未执行时，GT 工具查询可以看到新结果，BasePass RT lookup 仍可能使用旧 map/fallback。最后有效状态是“GT installed”，下一推进者是 render command，证据是 RT-visible map/complete bit 更新；不要在此阶段重查 HLSL。

## 5. Compile decision：坐标、caller 与三类 gate

现在进入编译阶段。引擎知道有 `TBasePassPS`，也知道它是 mesh material shader，但同一个 shader 类型仍然会有一大堆变体：不同 light map policy、不同 GBuffer 布局、有没有天光、平台能力、材质静态开关、顶点工厂能力，都会影响最终生成哪个二进制。变体数量很容易爆炸，UE 必须有办法管住它。

UE 把这件事拆成两个互相配合的概念：

**变体坐标（permutation domain）** 是 C++ 这一侧的“变体坐标系”。它把若干个维度编码成一个变体编号，并能把每个维度的取值写进编译条件里。它回答的是：“这个 shader 类型一共有哪些离散的变体。”

**编译条件（compile environment）** 是真正发给 shader 编译器的那一套东西——define、include、编译器开关、虚拟 include 内容等等。它回答的是:“HLSL 实际看到哪些条件、据此生成什么代码。”

一个是“坐标”，一个是“写给 HLSL 的条件”，两者分工清楚。但变体太多，不能全编。mesh material shader 中最显眼的是两道 predicate：

- 顶点工厂这边问一句：这个 VF 该不该为当前材质、平台、shader 类型，缓存这个组合？（VF 侧的 `ShouldCache`）
- shader 类型这边再问一句：这个 shader 变体该不该为这个组合编译？（shader 侧的 `ShouldCompilePermutation`）

这两道 predicate 仍不是完整决定，但还要先区分“cached layout 候选”与“某个具体 `FMaterial` 真正要求的结果”。cached layout 由 `FMaterialShaderParameters` 描述材质类别/能力，再结合 shader type、VF、pipeline、platform/flags predicates 构造；它可被参数相同的材质复用，因此此时不会调用某个具体实例的 `Material->ShouldCache` 来决定候选 membership。

```text
FMaterialShaderParameters + platform/flags
  -> shader type / pipeline predicates
  -> mesh path 再应用 VF predicate + mesh shader predicate
  -> 构造可复用的 cached layout 候选清单

具体 FMaterial 使用该候选清单
  -> compile-job 枚举时应用 Material->ShouldCache
  -> IsComplete 时只要求该 material 实际需要的 entries
  -> runtime missing/lookup 检查再次应用 material + VF + shader gates
  -> 通过当前 gate 只表示“此阶段应生成/应要求”
  -> 再检查 job、map result、cooked binary 是否真实存在
```

这种拆分解释了两个看似矛盾的状态：cached layout 可以有某 entry，但当前 material 的 `ShouldCache` 使它不成为 completeness 要求；反过来，所有相关 gates 都通过，也只证明应该提交或要求该 coordinate，不证明 job 已创建、编译成功、binary 已序列化或 shipping cook 已覆盖。mesh material compile job 写条件时，VF 先修改 local environment，shader type 再写 permutation/pass 条件；shared material environment 来自第 20 章。

如果这里出了问题，症状往往不像“少了个 define”那么直观，而是更靠后才暴露：

- 编译结果布局里根本没有某个 `(shader 类型, 变体编号, 顶点工厂类型)` 组合；
- 查询时取不到 shader；
- shader 编译成功了，但运行时绑定的 VF 参数和 HLSL 期待的对不上；
- 只在某个平台、或某种 mesh 上才缺 shader。

分布式 predicates 便于 material、VF 与 shader 各自扩展，但调试必须记录每道决策；中央规则表更易审计，却会形成大型耦合点。UE 选择前者，因此排查时应固定一个 `(type, permutation id, VF type)`：先查 cached layout candidate membership，再查具体 material 的 `ShouldCache`，最后查 job/result 与 cook coverage。任何 gate 通过都不能单独证明最终 shader 存在。

## 6. FVertexFactoryType：编译策略与能力注册

上一节里 VF 已参与 gate 和 compile environment。这里先只讲**类型层**：`FVertexFactoryType` 是全局注册的 input-policy metadata，描述 shader source、capability flags、compile hooks、`ShouldCache` 与按 shader frequency 创建 VF parameter binder 的工厂。它的 lifetime 近似 registry 级，和某个 mesh 拥有的 `FVertexFactory` instance 完全不同。

类型 flags 只声明“这种 VF 路径支持什么”，不证明某个 instance 已有有效 buffer、declaration 或 uniform。把 type existence 当成 stream readiness，会让 compile 问题与 render-resource 初始化问题混在一起。UE 把 mesh input convention 变成可枚举 compile axis，是为了让 Local、Skin、Landscape、Spline 等策略复用 pass/material shader，并让 cache key 看见输入策略差异。固定 vertex schema 或全面 manual fetch 可以减少 VF type 数量，适合统一 GPU-driven mesh database；UE 仍需兼容大量传统和特殊输入路径，因此保留可扩展 VF registry。

**Worked case。** 红色 StaticMesh 使用 `FLocalVertexFactoryType`：先验证 type 已注册、flags 与 gates 允许 BasePass；再验证具体 LocalVF instance 已 initialized。前者通过而后者失败时，shader map 可能完整，但 draw 仍没有可用 input data。

## 7. FDataType 与 FVertexStreamComponent：先描述数据来源

`FLocalVertexFactory::FDataType` 是 instance 保存的 CPU-side 输入描述。它不直接是 RHI declaration，也不是本次 draw 的 stream binding；它把 position、tangent、color、texcoord、lightmap 等语义关联到 `FVertexStreamComponent`，并可保存 manual-fetch 所需 SRV。

每个 stream component 说明“哪个 vertex buffer、byte offset、stride、element type 与 usage 提供这个属性”。mesh render resource 生产 buffer 与这些描述，`SetData` 把描述交给 LocalVF instance。先有这层 descriptor，VF 才能从多个 buffer 组合统一 declaration/stream list，也能支持 color override、position-only 或 manual fetch 分支。

```text
StaticMesh render resources
  -> Position buffer + offset/stride/type
  -> Tangent buffer + offset/stride/type
  -> UV/Color/Lightmap buffers and optional SRVs
  -> FLocalVertexFactory::FDataType
  -> LocalVF instance owns the input description
```

若所有 asset 都强制单一 interleaved buffer，可省掉部分 descriptor 复杂度，但会限制导入格式、stream specialization 与 override。UE 选择描述层来兼容不同 mesh resource layouts。

**Worked case。** Position/Tangent/UV component 都指向已初始化资源，说明 data description 成立；若 UV component 的 offset/stride 错，后续 declaration 和 stream 都可能“存在”却读错数据。最后有效状态是 buffer resource，下一步检查 component descriptor，而不是 material uniform。

## 8. InitRHI：FVertexStream、FVertexElement 与 declaration

VF render resource 初始化时，component descriptor 被转换成两个相邻但不同的产物：

- `FVertexStream` 表示一个 stream slot 背后使用哪个 vertex buffer、stream offset、stride 与 usage。只有这四项都相同才复用同一 stream slot。
- `FVertexElement` 使用 component offset 描述 attribute 在该 stream 内的位置，并记录 attribute index 与 element type；component offset 不是用于 stream-slot 去重的 stream offset。

elements 共同创建 RHI vertex declaration；LocalVF 还可按条件准备 default、position-only、position+normal 等 declaration。declaration 是**解释规则**，stream list 是**buffer 来源**。把 buffer pointer 烘进 declaration 会阻止布局复用，也会迫使每 draw 重建；把二者拆开后，同一 declaration 可配不同 mesh buffer。纯 manual vertex fetch 路径可以绕开部分传统 declaration，但会把职责转移到 SRV、索引和 shader 代码，且受平台/VF capability 约束。

```text
FVertexStreamComponent
  -> AccessStreamComponent
     -> FVertexStream: buffer/stream-offset/stride/usage -> unique slot
     -> FVertexElement: attribute/type/slot/component-offset
  -> InitDeclaration
     -> RHI vertex declaration variants
```

**Worked case。** Position 与 UV 即使来自同一个 buffer、stride 和 usage，只要 `StreamOffset` 不同，就必须得到不同 stream slot；复用它们会让 stream source 的基址错误。slot 正确后，UV 的 component offset 或 element type 仍可能使 attribute 解释错误。stream offset、component offset 与 declaration element 必须分开验证，不能统称“VF 绑定错”。

## 9. Build 与 submit：FVertexInputStream 和 element override

到 command build 时，`GetStreams()` 才把 VF 的 stream descriptors 变成本次 command 保存的具体 `FVertexInputStream`，即 RHI buffer 与 offset 等 draw-time 输入。per-element binding 还可以覆盖或追加 color、primitive-id stream 等来源。MDC 同时保存 vertex declaration 到 bound shader state；submit 时状态缓存比较后再调用 stream-source binding，把具体 buffer 放入对应 slot。

```text
VF declaration variants + VF stream descriptors
  -> MeshBatch element chooses declaration and overrides
  -> BuildMDC stores vertex declaration + FVertexInputStream list
  -> submit applies SetStreamSource-equivalent bindings
```

这条延迟允许大部分 VF state 被缓存，同时给单个 mesh element 保留 override。若每个 element 复制整套 VF，内存与构建成本更高；bindless vertex pulling 可减少 fixed stream state，但会改变 shader input、resource table 与 PSO 模型。

**类别护栏：** vertex declaration 定义 attribute 如何解释；vertex input streams 提供本次 draw 的 buffers；VF shader parameters/uniform/SRV 属于 shader resource binding。三者在 MDC 汇合，但任何一个都不能替代另外两个。调试要分别说 declaration mismatch、stream-source mismatch、primitive-id override mismatch 或 manual-fetch SRV mismatch。

## 10. 参数链第一步：metadata 与 RHI uniform-buffer layout

到这里，shader 已能查询，但仍需要一份稳定的 C++/HLSL/RHI 数据合约。参数宏生成 `FShaderParametersMetadata`：成员名、类型、C++ byte offset、数组/嵌套形状、resource category 与 use case。对于 uniform-buffer 结构，这些信息进一步初始化 RHI uniform-buffer layout，包括 size/alignment、constant/resource 成员清单与 layout hash/signature。

metadata 是**声明布局**，不是某个编译结果的“活跃参数表”。它解决 C++ 结构与 HLSL 声明如何共享稳定 schema；layout hash 让运行时发现“shader 按旧结构编译、CPU 却按新结构填数据”的危险错配。手写 offset 对极小 shader 可行，但容易静默漂移；外部 IDL/codegen 能提供更强类型安全，却增加独立 schema 与 build pipeline。UE 选择 C++ metadata 同时驱动 HLSL 声明、RHI layout 与 validation。

第 20 章的 `UniformExpressionSet` 也不是这套 metadata。前者是 material graph runtime expression/resource 的说明书，后者是 C++ shader parameter struct 的布局描述。材质 uniform 最终可以作为某个 binding 进入 draw，但两种“说明书”的 owner、生产时机和用途不同。

**Worked case。** 给 View/Pass 参数结构增加一个改变布局的成员后，metadata/layout hash 改变；若 shader binary 未按新布局重编或 binding 仍使用旧 signature，validation 应阻止把新 CPU bytes 按旧 HLSL contract 解释。最后有效状态是 metadata ready，下一步是 compiled parameter map 与 binding 固化。

## 11. 参数链第二步：compiled parameter map 与 binding records

shader 编译输出的 `FShaderParameterMap` 只描述 HLSL 最终保留下来的 allocation：名称对应哪个 constant buffer、base index、size 或 resource slot。它会排除被编译器优化掉的参数，因此“C++ struct 中有成员”不保证这个 shader instance 有 binding。

shader 创建/初始化阶段，binding context 按名称把 compiled allocations 与 metadata 对齐，并固化为紧凑 records。record 不再只保存字符串，而是记录 C++ byte offset、shader buffer/base index、size 与 resource category；不同列表可表示 loose constants、texture/sampler/SRV/UAV、uniform-buffer refs、RDG resources 与 bindless 参数。绑定完整性验证也在这里发现 HLSL/C++ category 不匹配。

```text
C++ metadata/layout             compiled FShaderParameterMap
  member name/type/offset   +     surviving HLSL allocations
                     -> bind/validate once
                     -> FShader parameter binding records
```

名字仍然重要，但主要用于 compile/bind 阶段；draw 热路径遍历 records。固定 root/descriptor layout 可以进一步减少灵活性与绑定成本，适合严格 shader 集；UE 的通用 shader system选择结构化 metadata + per-shader records，以支持优化掉成员与多种 resource category。

**Worked case。** 一个 loose constant、一个 texture/sampler、一个 View uniform buffer 与一个 RDG SRV 各落入不同 record 类别。若 HLSL 优化掉某成员，正确状态是“parameter map 无 allocation，因此不生成 record”，不是“draw 时丢了值”。

## 12. 参数链第三步：runtime write 到 FRHIBatchedShaderParameters

runtime 先填好具体参数结构，再用 `SetShaderParameters` 一类路径校验 layout hash，并按 records 执行：constants 从 C++ offset 复制，texture/SRV/UAV/sampler 与 uniform buffer 被提取，RDG resource 先标记使用并解析成当次 graph 对应的 RHI resource，最终写进短生命周期的 `FRHIBatchedShaderParameters`。RHI 消费的是批量 constants、resources、uniform buffers 与 bindless handles，不理解 Material Editor 的 `Roughness` 名称。

RDG parameter 有双重职责：它既向 RDG 声明依赖/barrier，又可能提供 shader binding。前者属于图生命周期，后者才进入 parameter records；跨 graph 保存 RDG resource 引用会越过 lifetime 边界，即使 shader slot 本身正确也不安全。

```text
C++ values filled
  -> layout hash validated
  -> records copy/extract constants and resources
  -> RDG references resolved for this graph
  -> FRHIBatchedShaderParameters populated
  -> RHI parameter-setting command recorded
```

每一层都是不同完成点。C++ value 已填写不证明 record 存在；record 存在不证明 batched parameters 已含正确资源；batched parameters 已填不证明 RHI command 已记录。bindless table 或 root constants 可减少传统 slots/calls，但仍需要某种 schema 与 mapping 将 owner 数据变成 shader contract。

**Worked case。** 红色材质的 Roughness 由 material uniform 路径产生；若值已更新而 shader 仍读旧值，依次验证 material UB record、batched UB/resource、RHI record。UV 错则回到 vertex declaration/stream；不要把两者都叫“参数绑定错”。

## 13. 多 owner 参数在 draw 边界汇合

第 10 至 12 节讲清了参数结构怎样变成 RHI write，但还没回答数据由谁生产。一次 BasePass mesh draw 的参数不是来自一个 Material 对象，而是来自多个 owner，各有自己的 publication 与 lifetime。把它们归位，是定位“值错了”最有效的第一步。

对金属 StaticMesh 的这次 BasePass draw，参数可以按这几层归位：

| 参数层 | Owner / producer | Publication 与 lifetime | 对本例的意义 |
| --- | --- | --- | --- |
| Material | material instance、render proxy、uniform expressions | proxy cache/UB 更新后供 RT draw 使用，layout 变化可能重建 | 红色、Metallic、Roughness、纹理 |
| View / Scene | 当前 view/scene rendering | 通常按 view/frame 建立并在相应渲染工作内有效 | view 矩阵、相机、scene uniforms |
| Primitive | scene proxy、primitive scene data / GPUScene | scene update 发布；由 primitive id 或 uniform 路径读取 | transform、bounds、custom data |
| VertexFactory | mesh render resource 与 VF instance | resource init 后有效；stream/UB/SRV 随 mesh/VF lifetime | LocalVF uniform、streams、manual-fetch SRV |
| Pass / RDG | 当前 mesh pass / RDG pass | graph/pass 范围内声明和解析，不得跨 graph 保存 transient ref | pass textures、buffers 与 render targets |

这种分层按变化频率、owner 与 lifetime 独立更新，在 MDC 处汇合 references/records。若把所有数据放进每 draw mega-structure，任一 owner 变化都会迫使整份 state 重建；mega-buffer 可以减少 binds，却仍需地址、版本、residency 与 publication 管理，并未消除 owner 差异。

Primitive 这一层还有一条关键分叉：当 GPUScene 可用、且 VF 提供 primitive id stream 时，shader 可以拿着 primitive id 去 GPUScene 里读逐物体数据；否则就直接绑定 primitive uniform buffer。这条分叉解释了为什么同一个 material shader，在不同平台、不同 VF、不同 mesh 路径下，逐物体数据的来源会不一样。

**Worked case。** 球的位置正确说明至少某条 primitive transform 路径成立；法线正确而 Roughness 旧，最后有效状态在 VF/primitive，下一步查 material proxy/UB records；UV 只在一个 mesh 错，查 declaration/stream/manual-fetch SRV；pass texture 错则回到 RDG declaration/resolution。先定位 owner，比按参数名全局搜索有效。

## 14. Shader refs 如何进入 MeshDrawCommand

现在把前面所有的零件，收束到一次真实的 BasePass draw 上。

金属 StaticMesh 进入 BasePass mesh processor 时，processor 已掌握当前 mesh、material、VF、light map policy、blend mode、pass 条件与 feature level。它不会重新翻译材质图或扫描 HLSL；它先选择 lookup request 并取得 `TBasePassVS/PS`，随后把 input 与 binding 状态固化进 command。

这一步可以这样看（注意这是概念上的流程，不是要你去背函数调用）：

```text
进入 BasePass mesh processor
  -> 根据 pass 条件选 light map policy / shader 变体
  -> 组织出需要的 BasePass shader 类型
  -> 用“当前材质 + 这些 shader 类型 + 当前顶点工厂类型”去查询
  -> 拿到带 map/resource lifetime 的 VS / PS shader refs
  -> 选择 vertex declaration，收集 FVertexInputStream
  -> 按 frequency 固化 shader binding records/values
  -> 建立 minimal pipeline state 与 cached pipeline id
  -> finalize FMeshDrawCommand
```

查询这一步是整章前置工作的兑现点。它按当前线程使用已发布 map；mesh material shader 再进入对应 `FMeshMaterialShaderMap` 分区。lookup 失败说明请求坐标在当前可见结果中不可用，应沿前置状态排查：

- shader 类型没注册（第 1 节）；
- shader 类别不是 mesh material，没进 per-VF 那一层（第 3 节）；
- coordinate 不在 cached layout candidates，或具体 material 的 `ShouldCache` 不要求/未生成该结果（第 5 节）；
- 当前线程能看到的 shader map 未发布或不 complete（第 4 节）；
- declaration、input streams 或 VF resource parameters 不匹配（第 7 至 9 节）；
- metadata、parameter map、records 或 runtime write 断链（第 10 至 12 节）；
- cook / DDC / 按需编译的状态和运行时要求对不上。

shader 取到之后，`BuildMeshDrawCommands` 把 refs、declaration、input streams、per-frequency bindings、draw arguments 与 minimal pipeline state 压成 `FMeshDrawCommand`，并经 draw-list context 得到可缓存的 pipeline id。这里的 producer 是 mesh pass processor，具体输出是可复用或逐帧使用的 draw recipe，下一消费者是 submit；它的 lifetime 取决于静态缓存或当帧动态 command，并要求所引用的 shader map、VF resource 与绑定资源覆盖消费期。MDC built 的直接证据是 recipe/bindings 已稳定；若 build 成功而 draw 消失，排查点应前移到 submit 的 full initializer、PSO policy 与 recording，而不是重新猜测材质节点图。

对静态 mesh，这条 command 可能在 primitive 注册或更新时就缓存下来，每帧只创建一个“可见 command”指向它；对动态 mesh、或不满足缓存条件的静态 mesh，则每帧在 pass setup 里现场生成一条一次性 command。但无论静态还是动态，shader 绑定的模型都是同一套：pass shader 先写 material / view / pass 级绑定，mesh material shader 再写 primitive / VF element 级绑定。

静态 command 可缓存复用，动态 command 可按帧构建；两者都必须保证 shader map/resource、VF resource 与绑定引用在消费期间有效。shader preload 尚未完成、map 被替换或 UB layout 改变时，不能只凭旧 command 结构存在就断言所有资源 ready。

这一步结束时，读者应该能把同一条 draw 拆成四个可检查产物：

| 产物 | 由谁准备 | 出错时先查 |
| --- | --- | --- |
| VS / PS shader refs | material shader map + VF 分区查询 | type 注册、permutation、VF 过滤、线程侧 shader map |
| declaration / input streams / VF resources | VF type + instance + mesh element | element layout、stream source、manual fetch、primitive id、VF uniform 分开检查 |
| shader bindings / batched parameters | Material、View、Primitive、VF、Pass 多个 owner 汇合 | 参数 metadata、uniform buffer、resource / bindless 绑定 |
| minimal PSO recipe 和 draw 参数 | MeshDrawCommand / draw-list context | cached pipeline id、declaration、shader stages、fixed state |

如果这四个产物都能说清楚，build 就不再是黑盒：它把可复用 draw contract 固化，但尚缺 active render targets、PSO resolution 与 command recording。

## 15. PSO precache、submit 与完成深度

graphics PSO 需要 shaders、vertex declaration、blend/depth/rasterizer state 与 render-target/depth formats。build 时能确定其中大部分 minimal state，但 active render targets 常到 submit 才完整，因此 UE 同时保留“提前 precache”与“submit 补全解析”两条时间路径。

submit 的正向职责是把“可重放的 draw recipe”推进成“已记录的 RHI draw”。`SubmitDrawBegin` 读取 MDC 的 minimal state，应用当前 render targets 得到 full initializer，解析/设置 graphics PSO，并设置 vertex streams 与 shader bindings；它返回 true 后，`SubmitDrawEnd` 才记录 direct 或 indirect draw。由此产出的只是 command-list 上的 RHI 命令；RHI/平台后端随后形成平台命令并把工作交给图形队列，GPU 再消费该 draw。MDC 可跨帧缓存或当帧存在，submit/RHI commands 属于相应 command-list 工作，platform submission 与 GPU completion 则由更深的队列同步证据界定。

```text
MDC minimal pipeline state built
  -> optional PSO precache request/result
  -> submit applies active render targets, forms full initializer
  -> inspect precache/cache state
     -> Active + skip policy
        -> current draw skipped / SubmitDrawBegin returns false
        -> 本次不记录 PSO set、streams、parameters 或 draw
     -> cache hit，或取得/创建 PSO cache entry
        -> async 未完成时把 completion event 加入 command-list dispatch prerequisite
        -> SetGraphicsPipelineState、streams、parameters 与 draw recorded
        -> command-list dispatch / translation 在消费 native PSO 前等待 prerequisite
        -> platform PSO ready for backend consumption
        -> command list transferred / platform queue submitted
        -> GPU consumes draw
```

precache 可在 draw 前生产常见 PSO，降低 submit hitch；submit 仍需验证 full initializer。若 precache result 仍为 `Active` 且启用 skip policy，当前 draw 在 `SubmitDrawBegin` 返回 false 后终止，不会落到本次 PSO state/stream/parameter/draw recording。非 skip 路径取得或创建 cache entry；同步结果可以直接被后端消费，异步 miss 则把 completion event 加入 command-list dispatch prerequisite，同时继续记录 PSO state、streams、bindings 与 draw。真正的等待边界在 command-list dispatch / translation 消费 native PSO 之前，而不是一律阻塞 `SubmitDrawBegin`。全离线 PSO library 可提高 shipping 确定性，但需要完整覆盖收集并处理平台驱动变体；运行时同步创建实现简单，却把 miss 延迟暴露为帧卡顿；异步创建允许 CPU recording 继续，却把等待转移到更深的 dispatch prerequisite。

于是可以沿正向证据逐层确认：cached pipeline id 能恢复 minimal state；full initializer 已带当前 render targets；PSO cache/create 已给出可引用的 cache entry；异步未完成时 command list 已持有 completion prerequisite；`SubmitDrawBegin` 已记录 PSO、streams 与 bindings；`SubmitDrawEnd` 已记录 draw；dispatch / translation 越过 prerequisite 后，native PSO 才对后端消费 ready；平台提交证据表明 command list 已进入 queue；fence、capture 或平台完成信号再证明 GPU 已消费。前一项是后一项的输入，不是后一项的替代证据。

**Worked case。** shader refs、declaration 与 streams 都正确，但物体偶尔消失。若 full initializer 后得到 `Active + skip`，last-valid-state 就是 precache active，当前 draw 已被明确跳过，不应继续寻找本次 PSO-set/stream/parameter recording；若非 skip 路径已经 recorded 但 async prerequisite 未完成，下一步查 dispatch 是否等待并越过该 prerequisite；若 prerequisite 已完成且平台命令中没有 draw，再查 translation/recording 交接；若 draw 已 submitted，才转向 GPU execution/state。

## 16. Cooked binary、runtime lookup 与 ODSC

type registry 在 cooked runtime 仍然重要：反序列化和 lookup 需要知道 type 与布局。但 **type registered 不等于某 platform/material/VF/permutation binary 已 cook**。cook 必须枚举目标平台所需 global/material maps，把 shader code 序列化或放入 library，并覆盖 material usage 与 VF coordinate。

| 环境 | 缺失 coordinate 的处理边界 |
| --- | --- |
| uncooked editor，允许 full compile | caller 可提交 normal compile；期间使用旧结果或 fallback |
| 支持 WITH_ODSC 的开发路径 | 可发 on-demand request；当前 lookup 不会在 draw 内同步生成 binary |
| `RequiresCookedData()` 平台 | material runtime compile 是 fatal 路径，必须依赖 packaged shader data |
| cooked 普通材质 map 无效 | 可落到 default material fallback，画面不代表原材质 |
| default/required-complete material 缺失 | fatal，因为最终 fallback/必需内容本身不可缺 |

runtime JIT 对带 compiler、允许 cache/hitch 的 PC 开发环境可能合适，但不适合作为所有 cooked 平台的通用兜底。ODSC 也只属于特定开发构建条件。调试 custom VF 时，editor ODSC 请求成功不能证明 shipping cook 覆盖；必须验证目标 platform 的 layout/cook 中确有该 VF/permutation binary。

**Worked case。** 自定义 VF 的 BasePass shader 在 uncooked editor 可由 full compile 产生，在 ODSC 环境可按需请求，而在 cooked shipping 缺失时不能现场补编。最后有效状态若是 type registered，下一步查 layout/cook coverage；若 serialized map 有 entry，下一步查 runtime load/publication；若 shader ref 已取到，再进入 MDC/PSO。

## 17. 按 last-valid-state 定位失败

ShaderSystem 的问题不能只分“编译错/运行错”。对每个症状记录最后已验证状态、下一状态、推进者与证据：

| 症状 | 最后有效状态 | 下一状态 / owner | 证据与分流 |
| --- | --- | --- | --- |
| shader type 不可查 | static registration instance 存在 | CommitAll/type registry | type list/name lookup；若注册也不存在查模块加载 |
| type 可查但 binary missing | InitializeShaderTypes 已完成 | cached layout -> material gate -> compile/cook | 固定 type/permutation/VF，先查 candidate membership，再查 `Material->ShouldCache`、job/result 与 cooked binary |
| LocalVF 正常，另一 VF missing | material map identity/RT publication 正确 | per-VF map partition | 对比目标 VF entry、job/result 与 cook coverage |
| 顶点 attribute 解释错 | buffers 与 components 已初始化 | vertex elements/declaration | attribute index/type/stream/stride；与 stream source 分开 |
| declaration 正确但 mesh 数据错 | bound shader state 正确 | FVertexInputStream / element override | buffer/offset、color/primitive-id override、manual-fetch SRV |
| C++ parameter struct 已填但 shader 无值 | metadata/layout 正确 | parameter map / binding records | 参数是否 optimized out、名称/category 是否匹配、record 是否存在 |
| record 存在但资源仍错 | binding records 正确 | batched write / RDG resolution | `FRHIBatchedShaderParameters` 内容、RDG lifetime、RHI command recording |
| MDC built 但物体消失 | minimal pipeline state 正确 | full initializer / PSO policy | `Active + skip` 表示当前 draw 已 return false；非 skip async miss 则附加 dispatch prerequisite 后继续 recording |
| draw recorded、async PSO 未完成 | `SubmitDrawBegin/End` 已记录状态与 draw | dispatch prerequisite / backend consumption | 检查 completion event 是否在 translation/dispatch 消费 native PSO 前完成；不要把 recorded 当作 PSO 已 ready |
| prerequisite 完成但 capture 无 draw | native PSO 已可供 backend 消费 | translation / platform command formation | 检查 recorded direct/indirect draw 是否形成平台命令；此时尚未进入 queue/GPU 结论 |
| draw 已 recorded 但无 GPU 结果 | command-list draw 已存在 | platform queue submit / GPU consume | 分别查 command-list transfer、queue/fence 与 GPU capture，不能用 recorded 代替完成信号 |
| cooked custom VF 缺 shader | type registered | cooked map/binary coverage | target platform serialized entry；不要用 editor ODSC 结果代替 |

GPU capture 擅长证明最终 declaration、streams、shader resources、PSO 与 draw 是否 recorded/consumed，却不能单独证明 registry、layout gate 或 cook 决策；后者要用 CPU debugger、compile diagnostics 与 cook records。反过来，type list、layout 或 `FShader` 存在也不能证明 RHI/GPU 已走到对应深度。

## 18. 小结：ShaderSystem 是多轴收敛，不是一个 pass asset

把整章主线复述一遍：

1. shader 注册分为 static collection、`CommitAll` 构造注册、`InitializeShaderTypes` 派生初始化；late type 不能在固定 registry 后悄悄加入。
2. `FShaderType` 描述类级编译策略；job 生产 `FShader`；map content/resource 持有结果；`TShaderRef` 关联 map lifetime；RHI shader 是后端阶段资源。
3. Global、Material、MeshMaterial 按真实依赖分 map owner/key；`FMeshMaterialShaderMap` 是 material map 内的 VF type 分区，不是 VF instance 的 map。
4. permutation domain 给坐标，environment 给 HLSL 条件；cached layout candidates 由 material parameters 与 type/VF/pipeline/platform predicates 构造，具体 compile/completeness/lookup 再应用 `Material->ShouldCache`。任何 gate 通过都不证明 job、binary 或 cook entry 存在。
5. VF runtime chain 是 data/component -> stream/element -> declaration -> input stream；vertex declaration、stream source 与 VF shader resource binding 是三类机制。
6. parameter chain 是 metadata/RHI layout -> compiled parameter map -> binding records -> runtime batched write；Material `UniformExpressionSet` 不等于 C++ shader metadata。
7. BasePass lookup 得到 shader refs 后，MDC 固化 declaration、streams、bindings 与 minimal pipeline state；submit 形成 full initializer 后，`Active + skip` 终止当前 draw，非 skip 路径则取得 cache entry，并在 async 未完成时附加 dispatch prerequisite 后继续记录状态与 draw。
8. 完整状态链是 type registered -> coordinate eligible -> job/result inserted -> map GT installed/RT visible -> shader ref selected -> MDC built -> full initializer/cache entry + optional prerequisite -> RHI state/draw recorded -> dispatch/translation waits for prerequisite -> native PSO ready for backend consumption -> queue submitted -> GPU consumed；每层都有 producer、具体产物、下一消费者、lifetime 与失败症状。

对 Unity 渲染读者，最该记住的一句是：**UE 的 mesh shader path 是注册类型、cooked/compiled result、VF 输入链、参数 records 与 pipeline state 在明确 lifetime/completion 边界上的收敛，而不是一个 pass asset 加若干 runtime keywords。** 遇到 shader missing 或 draw 错误时，先找 last-valid-state，再检查下一 owner。

第 22 篇会把视角切到 Compute Shader。它会复用本篇的 shader 类型注册、变体坐标和参数描述机制，但**没有** VertexFactory / mesh material shader / MeshDrawCommand 这一整条轴——所以 dispatch 的组织方式会更直接，也正好可以拿来和本篇对比，看清“mesh 这条轴”到底多出了什么。
