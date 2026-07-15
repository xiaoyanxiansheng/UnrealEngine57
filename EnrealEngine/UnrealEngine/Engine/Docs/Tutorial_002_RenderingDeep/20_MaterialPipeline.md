# 20 MaterialPipeline：材质节点图如何变成可用的 ShaderMap

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `04_RHI.md`、`07_MeshDrawCommand.md`、`10_BasePass.md`、`16_Nanite.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `20_MaterialPipeline_CoverageMatrix.md`

第 10 篇讲 BasePass 时，我们刻意只建立了一个最小模型：BasePass shader 会 include 一份生成出来的材质 HLSL，调用 `CalcMaterialParameters` 得到 `FPixelMaterialInputs`，再把 BaseColor、Metallic、Roughness、Normal、ShadingModel 编码进 GBuffer。那个模型能解释 BasePass **怎样消费**材质，但它对更早的生产端避而不谈：那份 HLSL 是谁、在什么时候、根据什么规则生产出来的？

这就是 Unity 读者最容易踩空的地方。在 Shader Graph 心智里，一张图大致对应一个 shader；改图就重编，改属性就传参，界限很直觉。可一旦你在 UE 里改了一个节点，可能触发一长串重编；改一个数值却什么都不重编；同一个材质还会冒出几十上百个 shader permutation，但你又从来没见过“全组合”那么夸张的数量。这些现象指向同一个被隐藏的事实：**UE 的材质节点图根本不直接生成一个完整的 pass shader。**

所以本篇要回答的核心问题是：

> **Material Editor 里的节点图，怎样变成 BasePass、DepthPass、Nanite material shading 都能取用的 shader？为什么改 static switch 会重编、改 scalar parameter 通常只更新运行时参数？为什么同一个材质有那么多 permutation，却又不会把所有理论组合都编出来？**

### 贯穿全文的那个材质

全文用同一个具体材质举例，建议你始终把它放在脑子里：

> 一个红色、偏粗糙、金属度接近 1 的 opaque 材质，挂在普通 StaticMesh 上进入 BasePass。节点图很简单：常量红色接 `BaseColor`，常量 `1.0` 接 `Metallic`，常量 `0.65` 接 `Roughness`，再放一个 static switch 控制是否采样一张纹理。

UE 处理这个材质的关键设计，一句话先放在这里，后面逐层拆开：

> **材质节点图先生成一份可缓存的材质 include（“属性怎么算”），再由各个 pass shader 用自己的入口、shader type、vertex factory 去组合编译（“在哪个 pass 怎么用”）。最终可执行的 shader 是这两者相乘的结果，不是节点图本身。**

### 先看一眼这条生产链

下面这张图是本篇要走的路线。现在不必看懂每一步，把它当作地图，我们会沿着它一节一节走：

```text
UMaterialInterface / UMaterial / MaterialInstance   （资产语义：图、默认值、覆盖）
  -> 为实际需要的编译配置准备 FMaterialResource     （FMaterial 的具体实现）
  -> FMaterial 构造 FMaterialShaderMapId             （决定编译结果能否复用）
  -> 查 inline、进程内 map、DDC 或同 id 的在途工作   （编译前先复用）
  -> 缺失或不完整时才 Translate 节点图
       -> 生成 /Engine/Generated/Material.ush  （pass shader 能 include 的材质代码）
       -> 生成 UniformExpressionSet、Substrate output 等编译输出
  -> cached layout 提供候选 compile coordinates
  -> 具体 material gates 通过且结果确需生产时，创建或复用 compile jobs
  -> job 结果回填、map finalization、material-complete、GT 安装、RT 发布
  -> FMaterialRenderProxy 提供 fallback 与当前实例的 uniform/resource 数据
  -> BasePass、Nanite raster/shading 等 consumer 查询已有 shader
  -> PSO precache/submit 与 GPU 完成属于更下游的完成层
```

把这条路线先按生产责任读一遍，后文的 cache、job 和 consumer 分支就不会混成一个“材质编译”：

| 阶段 | Owner / Input | 正向 Output | 下一 Consumer / Lifetime |
| --- | --- | --- | --- |
| 资产语义与参数分流 | UObject 资产/实例与 GT；节点图、static parameters、runtime overrides | 选定代码结构与实例值的两类变化请求 | static 分支进入 `FMaterialResource`；runtime 分支进入 render proxy；资产状态按 UObject/package 存活 |
| 渲染配置与身份 | `FMaterialResource` 以 `FMaterial` 合同接收目标 platform/feature/quality 与 static 状态 | `FMaterialShaderMapId` 和对应 cache/DDC 查询身份 | cache/translate；resource 与 map 引用独立于编辑器面板对象的瞬时访问 |
| Cache 与 Translate | material cache/translator 接收 identity、依赖和节点图 | 命中的 map，或 generated material include、`UniformExpressionSet` 等编译输出 | layout/gates/job 枚举；在途状态持续到查询、翻译或编译结果收束 |
| Jobs 与 ShaderMap | compiler manager、shader types 与 VF 接收 compile coordinates/environment | job results、finalized map、material-relative complete 状态 | GT install 与 RT publication；map 通过共享引用和延迟清理覆盖线程消费期 |
| 线程侧发布与实例数据 | `FMaterial`/render command/proxy 接收可用 map 与当前 runtime values | RT-visible shader map、当前 uniform/resource cache 与 fallback 选择 | BasePass/Nanite 等 draw consumers；旧版本保持到既有 RT/command 引用解除 |
| Pass 消费 | BasePass/Nanite consumer 接收 pass/VF coordinate、shader refs、bindings 与平台条件 | mesh command 或 Nanite raster/shading 工作所需材质输入 | PSO/RHI/GPU 下游；本章不把 shader-map 完成提升为 draw 或 GPU 完成 |

这张表也固定了两个分叉：static switch 改变代码身份，由 asset/resource/cache/compile 链推进；普通 scalar、vector 或 runtime texture override 改变实例数据，由 proxy/uniform/resource 链推进。只有改动实际影响代码结构或布局条件时，runtime 数据更新才会回到编译身份问题。

## 本篇边界

本篇讲透“材质图到 shader map”这条生产链：生产、缓存、permutation、运行时参数边界。相邻系统只在主线需要时点到，深入留给对应章节：

| 相邻概念 | 本篇只讲它在当前链路里的角色 | 深入出处 |
| --- | --- | --- |
| BasePass 写 GBuffer | BasePass 是 material shader map 的消费端，不重新翻译节点图 | 第 10 篇 |
| MeshDrawCommand / PSO | build 固化 minimal 配方；precache 可提前生产 PSO；submit 补全并查询/创建 | 第 07 / 04 篇 |
| Shader 参数声明、VertexFactory 绑定细节 | VF 参与 compile environment 和 shader map 维度，绑定系统留给后续 | 第 21 篇 |
| Nanite cull / raster / material bin | 区分 programmable raster material shader 与 BasePass compute/work-graph shading | 第 16 篇 |
| ShaderCompilerWorker、后端字节码 | 本篇只讲 job 如何产生和回填，不展开 worker 内部 | 第 21 / 22 篇 |
| Substrate BSDF 内部 | 讲 compilation output 如何改变 BasePass 参数、MRT 与 consumer 合约，不展开 BSDF 数学 | 后续专题 |

## 本篇必须能回答

读完本篇，你应该能自信地回答这几个问题。如果其中某个还答不上来，说明对应小节没读透：

- `UMaterialInterface`、`FMaterial`、`FMaterialResource`、`FMaterialRenderProxy`、`FMaterialShaderMap` 各负责什么，为什么不能只用一个“材质对象”走到底。
- 节点图怎样变成 `/Engine/Generated/Material.ush`，这份 include 又怎样被 pass shader 使用。
- identity、layout、type permutation、VF 分区和 pass lookup request 各自决定什么，为什么不能统称 permutation。
- cache 命中、job 完成、map finalized、complete for material、GT 安装与 RT publication 为什么是不同状态。
- static switch 与 scalar / vector / texture runtime parameter 为什么走不同的更新路径。
- cooked 缺失怎样按环境和材质重要性确定 fallback 或 fatal，为什么不能靠运行时编译兜底。
- traditional BasePass、Nanite programmable raster、Nanite BasePass shading 与 Substrate consumer 怎样消费材质结果。

## 一、责任链：五类对象、两条更新路径

在沿主线走之前，先把全篇会反复出现的对象摆清楚。它们最容易被 Unity 读者压缩成“一个材质对象”，而 UE 恰恰是把它们拆开，才换来了缓存复用和跨线程消费的能力。

`UMaterialInterface` 是资产侧统一入口：`UMaterial` 提供基础节点图和默认值，材质实例在它之上保存 static parameter 与普通运行时参数覆盖。它们由 UObject/GC 世界管理，主要在 Game Thread 编辑、加载和发起更新。renderer 的 draw 热路径不能把“遍历 UObject、解析实例层级、递归节点图”当作稳定输入，否则渲染线程会依赖可变资产状态和 GC 生命周期。

`FMaterial` 是这条链上容易漏掉的责任层。它不是另一份资产，而是 shader compiler 与 renderer 共同面对的渲染材质接口：它暴露材质属性，拥有异步编译的暂态，维护 Game Thread 与 Render Thread 可见的 shader-map 引用，并回答当前材质需要哪些 shader。`FMaterialResource` 继承 `FMaterial`，是 `UMaterial` 和材质实例最主要的具体实现；不能把两者当同义词，因为编译与 renderer 合约属于基类责任，平台、Feature Level、Quality Level 等具体配置语义由 resource 实现承载。

resource 也不是固定笛卡尔积。UE 只为资产、目标平台、cook/editor 路径以及实际使用的 feature/quality 组合准备所需实例；若结果不依赖质量档，Quality Level 可以表达为“不区分具体档位”。这种按需实例化避免为从不使用的配置永久复制渲染状态。代价是调试时不能仅凭资产名推断当前 resource，必须确认正在处理的 platform、feature 与 quality 语义。

`FMaterialRenderProxy` 是渲染侧的实例视图。它是 `FRenderResource`，为 Render Thread 提供当前实例参数、uniform-expression cache、sampler/resource 状态与材质 fallback。普通参数变化先改变实例覆盖，再使 proxy cache 失效或排队刷新；shader 重编若改变 uniform layout，还可能要求重建 uniform buffer。缓存 mesh command 可能保存 uniform-buffer 引用，因此“cache 已标脏”不是完成，当前 draw 绑定到新资源才是完成。

`FMaterialShaderMap` 保存某个 identity 下的编译结果；带 VF 维度的 mesh material shader 分区保存在其中的 `FMeshMaterialShaderMap`。它用引用计数和延迟清理跨越 GT/RT 消费，不能用 UObject 的 GC 时刻推断它何时对 renderer 失效。

| 对象 | 正面职责 | Owner / control | 有效期与调试问题 |
| --- | --- | --- |
| `UMaterialInterface` / `UMaterial` / instance | 图、默认值、static/runtime overrides、序列化与 cook 语义 | GT 与资产系统推进 | 面板值正确，不证明渲染快照已发布 |
| `FMaterial` | compiler/renderer 接口、shader-map 暂态与 GT/RT 引用 | GT 发起 cache，compiler manager 推进结果，RT 消费已发布结果 | 问“当前在途 id、GT map、RT map 分别是什么” |
| `FMaterialResource` | 把一个实际需要的 platform/feature/quality 配置实现成 `FMaterial` | 资产按目标配置创建，引用计数与 RT 延迟删除保护生命周期 | 问“异常平台正在用哪份 resource” |
| `FMaterialShaderMap` / `FMeshMaterialShaderMap` | 保存 identity 下按 type/permutation/VF 组织的编译结果 | 编译系统生产，map 冻结/共享，GT 安装后再发布 RT | 问“map finalized、success、complete 和 RT visible 到哪一步” |
| `FMaterialRenderProxy` | 为 draw 选择可用 material/fallback，并提供实例参数与 uniform/resource cache | GT 请求更新，RT 读取或刷新渲染资源 | 问“新参数是否已进入当前 draw 的 UB/resource binding” |

记住一个分工，后面就不会迷路：

> **资产层描述“想要什么”，`FMaterial` 定义编译与 renderer 合约，`FMaterialResource` 实现某个实际配置，shader map 保存代码结果，render proxy 把可用材质与当前实例数据交给 draw。**

而材质改动有两条主要更新路径，全篇的调试结论都落在这条岔路上：

- **代码结构路径**（static switch、材质函数、generator）：资产/`FMaterialResource` 生成新的 static 状态与 shader-map identity，cache/translator/compiler 推进新 map，GT/RT publication 后 consumer 才能使用；旧 map 保持到既有引用解除。
- **运行时数据路径**（scalar / vector / texture override）：material instance 把新值交给 `FMaterialRenderProxy`，RT 刷新匹配当前 layout 的 uniform/resource 数据，当前 draw 随后绑定新版本；shader-map identity 通常保持不变。纹理引用仍可能参与编译身份中的链接一致性哈希，但这不等于每次 MID texture override 都重编。

把贯穿案例落到这四类对象上，会更容易看出“同一个材质”其实经历了几次换手：

```text
编辑器里的红色金属材质资产
  -> UMaterialInterface：统一访问 base graph 与 instance overrides
  -> FMaterialResource：作为 FMaterial，为当前实际配置准备渲染版本
  -> FMaterialShaderMap：保存 BasePass、DepthPass、Nanite 等消费端已经编出的 shader
  -> FMaterialRenderProxy：选择可用 material/fallback，提供 Roughness、纹理和 uniform 数据
```

如果你把 Roughness 从 0.65 改成 0.2，变化应停在 runtime-data 分支：render proxy 的参数和 uniform cache 更新，shader-map identity 不变。如果切换 static switch，让材质从常量红色改为采样纹理，变化会回到 `FMaterial` 编译分支：对应 resource 构造新 identity，旧 shader map 不再代表新代码。这个微案例的调试价值在于，它先问“变化应停在哪一条责任链”，再决定查 DDC、compile job，还是 runtime uniform。

下面正式沿生产链往下走。

## 二、节点图先产出可复用的材质 include

节点图在这条生产链中的正向产物，是可被多个 pass 和多种几何输入组合使用的材质属性计算代码。红色金属球会进入 BasePass，也可能参与 DepthPass、阴影或 Nanite 路径；这些 consumer 需要共享“属性怎么算”，同时保留各自的入口、频率、VF 与输出合同。

同一个材质至少会被问出几类不同问题：

- BasePass 要它输出 GBuffer 所需的 material attributes。
- DepthPass 可能只关心 masked clip 或 position-only 能不能成立。
- Shadow pass 需要深度相关的 material behavior。
- Nanite primary BasePass 不用传统 per-draw pixel shader，而是把可见像素按 material bin 分组后用 compute 形态着色。
- 不同 vertex factory（普通 static mesh、landscape、spline mesh、Nanite VF 等）会给 shader 提供不同的输入结构。

如果材质图直接生成“一个最终 shader”，这些场景就会互相绑死：BasePass 的 render target 布局、DepthPass 的入口、Nanite compute dispatch、各种 vertex factory 的输入，都得塞进同一个文件。UE 选择把职责切开：

```text
材质图只回答：     “这个材质的属性怎么算？”
Pass shader 回答：  “在哪个 pass、用哪个入口、输出到哪里？”
VertexFactory 回答：“这类几何给 shader 提供哪些输入？”
Renderer 回答：     “本次 draw 要取哪一组 shader？”
```

所以材质图生成的是一份**材质 include**，而不是完整 pass shader。这份 include 里有 pass 能调用的材质函数和属性求值代码；BasePassPixelShader、DepthPass shader、Nanite BasePass compute shader 再各用自己的入口把它包含进去。

> **Material Editor 图的产物是“材质属性计算片段”，不是“最终 draw shader”。最终可执行 shader 来自材质 include、pass shader type、vertex factory 和 permutation 的组合。**

这也解释了开头那个现象：改一个节点之所以可能影响很多 shader，是因为你改的不是某个 pass 的像素函数，而是一份被多个 pass 共享的材质 include。只要某个 pass shader 需要它，对应组合就可能要重编。

## 三、从资产请求到可销毁的渲染资源

承接上一节：材质 include 会为不同目标配置编译，但 UObject、编译工作和 Render Thread 消费并不共享同一种生命周期。UE 因而把一次更新拆成“资产提出意图、渲染材质承接编译、线程侧发布、延迟释放”四段，而不是让一个对象被所有线程任意读写。

红色金属材质修改 static switch 后，控制流先在 Game Thread 形成新的 static parameter 状态。资产系统据此选择实际需要更新的 `FMaterialResource`，resource 以 `FMaterial` 身份构造 identity 并开始 cache。编译管理器随后拥有在途 job；它可以与别的 resource 共享相同 identity 的工作。结果准备好后先安装到 Game Thread 可见引用，再通过 render command 更新 Render Thread 引用。旧 map/resource 不能在 enqueue 之后立刻释放，因为之前记录或缓存的渲染工作仍可能引用它；引用计数与延迟清理把销毁推迟到最后一个 CPU/RT 使用者之后。

```text
GT asset/static override changed
  -> 选择实际受影响的 FMaterialResource
  -> FMaterial 建 identity，发起 cache/compile
  -> compiler manager 推进在途 map
  -> GT 安装可用 map
  -> render command 发布 RT-visible map
  -> 旧资源在引用解除与延迟清理条件满足后销毁
```

这种拆分不是 GPU 强制规定的唯一写法，而是 UE 用来隔离 UObject/GC、异步编译和 RT 消费的工程选择。另一种架构可以使用完全不可变的 compile descriptor 与 RCU handle；它能减少虚接口，却要引入版本化 handle 和更显式的 publication/retirement 管理。UE 当前的 `FMaterial`/`FMaterialResource` 模型兼容大量材质派生实现，并让 renderer 对统一接口编程。

**Worked case：预览正常、目标平台异常。** 最后有效状态若停在“目标平台 resource 已创建”，下一步就查该 resource 的 identity/cache；若 GT map 已安装而 RT 仍显示旧材质，下一步查 publication；若 RT map 已正确但参数仍旧，转到 render proxy/uniform cache。只看资产名无法区分这三种故障。

## 四、ShaderMapId 固定可复用的编译身份

有了 `FMaterialResource`，UE 接着要回答一个省钱的问题：**这份编译结果能不能复用？** 材质编译很贵，正常路径绝不会动不动重编。可只拿材质资产 GUID 当钥匙又不够——很多不改 GUID 的输入，照样会改变生成代码或所需的 shader 组合。

`FMaterialShaderMapId` 是这份编译结果的“身份证”：它把会改变生成代码、布局或所需 shader 组合的状态组织成可比较身份，让 cache、DDC 和在途工作判断能否复用结果。理解它时要区分四类信息：

| 类别 | 代表内容 | 为什么影响复用 |
| --- | --- |
| 显式身份字段 | quality、feature、usage、static parameter subsets、generator 与 Substrate 配置等 | 它们会直接改变生成代码或应有结果 |
| 基础材质与属性覆盖形成的间接状态 | base material identity，以及 domain/blend/shading 等会被 base-property override 改变的状态 | 它们未必都以同名独立字段出现，但会进入最终 identity/layout 判断 |
| 依赖集合与哈希 | 材质函数、参数集合、shader type、pipeline、VF、expression include、external code 等依赖 | 依赖代码或清单变化时，旧结果必须失效 |
| 纹理链接一致性哈希 | uniform expression 引用纹理的名称与顺序等链接信息 | 保持编译结果与资源布局的关联，不表示每个运行时纹理值都属于 identity |

这张表正好解释了 static switch 与普通参数的根本差异。**Static switch 是 identity 的输入**：它能在编译期选择分支，改它会使旧 map 不再代表新代码。**普通 scalar/vector override 通常是 runtime value**：它进入 uniform expression 或 uniform buffer。**普通 MID texture override 通常也走 runtime resource 路径**；identity 中存在纹理引用哈希，不应被扩大解释成“每换一张运行时纹理就重编”。

因此边界是：ShaderMapId 不是材质名，也不是运行时参数快照；它只承担编译结果复用身份。实例当前值是否已经进入 draw，要沿 render proxy、uniform/resource cache 与 binding 另行证明。

`ShaderMapId` 还不是完整 DDC key 的同义词。DDC key 会把该 identity 与目标平台、版本化依赖和相关编译输入编码成可持久化查询键；editor 表示与 cooked 表示也可能不同。概念上应坚持：identity 回答“这份 material compile result 能否复用”，DDC key 回答“持久缓存里到哪里取这份结果”。

反过来想，如果 identity 少算输入，改材质函数或 generator 后可能错误复用旧结果；若把普通 runtime value 全算进去，则每个实例值都会制造无意义重编和 DDC 碎片。全内容哈希更保守，但构建成本高且削弱增量复用；显式依赖更高效，却要求所有影响代码/layout 的依赖都被登记。UE 选择后者，因此调试时要验证具体依赖类别，而不是只比较材质 GUID。

**Worked case：三次改动。** 切 static switch，应观察新 identity 与新/复用后的 map；改材质函数，应观察依赖哈希使 key 失效；给 MID 换纹理，通常应看到 proxy resource/cache 更新而 identity 不变。最后有效状态分别停在 identity、dependency key 与 runtime binding 三个不同层次。

## 五、缓存与异步编译是一台状态机

有了 identity，UE 仍不会马上翻译节点图。它先寻找已经生产或正在生产的结果，来源不止“内存与 DDC”两级：

```text
cooked/serialized inline map 可用？
  -> 当前进程已有相同 identity 的 finalized map？
  -> 另一个 material 正在编相同 identity，可共享在途工作？
  -> uncooked 路径可从完整 map DDC 异步加载？
  -> job-cache 模式可复用单个 shader job 结果？
  -> 开发构建是否允许 full compile 或 ODSC request？
```

这些来源解决不同问题。inline map 让 cooked 包不依赖外部 DDC；进程内 map 与在途工作避免同一会话重复编译；完整 map DDC 跨会话复用；job cache 在更细粒度上复用编译结果；ODSC 是受支持开发环境中的按需请求，不是 shipping fallback。`BeginCacheShaders` 可以启动异步 I/O/编译，`FinishCacheShaders` 则推进或收束其结果；两者之间，material 处在明确的“请求已发出但尚未满足 consumer”阶段。

最需要纠正的是“空 map -> 编译完成 -> 完整 map”的二态想象。异步期至少有以下可区分状态：

1. **Identity 已建立**：知道要找哪份结果，但 cache 查询还未结束。
2. **Cache request pending**：inline/in-process/DDC/job-cache 中的某一路仍在推进。
3. **Compiling id 已登记**：`FMaterial` 能指出当前在途 shader-map id，避免重复提交，也可关联共享编译。
4. **Jobs 已提交或部分回填**：某些 `FShader` 已产生，另一些仍失败或等待。
5. **Finalized clone 可供有限用途**：异步期间 GT map 可能为空，也可能指向从在途 map 取得的冻结部分结果；“有指针”不等于当前 material 所需清单完整。
6. **Map compilation finalized**：结果处理结束，内容不再按在途方式增长。
7. **Compiled successfully**：编译成功标志成立；它与 finalized 是两个判断。
8. **Complete for material**：`IsComplete(Material, ...)` 遍历 cached-layout candidates，但只对 `Material->ShouldCache` 等 material gates 判定为实际需要的 coordinates 要求结果；同一 map 对不同材质需求的判断具有 material-relative 语义。
9. **GT installed**：Game Thread 可见引用和 complete bit 已更新。
10. **RT publication**：render command 把 RT-visible map、在途 id 与 complete 状态更新到位，consumer 才能依据 RT 状态查询。

这种梯度允许 editor 尽早显示部分或旧结果，同时保持 Render Thread 只消费冻结、生命周期安全的状态。若强制所有 cache/compile 都同步原子完成，模型更简单，却会把 DDC I/O 和大批 shader job 延迟直接压到 Game Thread；若允许 RT 读取正在增长的 map，则会引入数据竞争、悬空引用和不可重复查询。UE 选择异步生产加显式 publication，并以 fallback 保持编辑器可交互。

**Worked case：首次 DDC miss。** 红色金属材质的 identity 在进程内与 DDC 都未命中。最后有效状态若是“DDC request 已结束”，下一推进者是 compiler manager；若 jobs 已处理但 `CompiledSuccessfully` 为 false，查具体失败 job；若 complete for material 为 true 而画面仍是默认材质，查 GT 安装与 RT publication；若 RT 已发布正确 map，问题已经离开编译状态机，应转向 proxy、lookup 或 PSO。第二次打开项目命中 DDC，只能证明 map 数据被找到，仍需安装与 publication 才能供当前线程消费。

> **调试结论**：不要问笼统的“编译完成了吗”。要问最后成立的是 cache 查询结束、job 处理、map finalized、compiled success、complete for material、GT installed，还是 RT published。

## 六、Translate 的输出契约：生成 Material.ush 和 UniformExpressionSet

缓存没命中，终于进入编译。第一道大门是 `Translate`：把材质图翻译成一份 pass shader 可 include 的材质 HLSL，同时产出材质运行时参数所需的编译期信息。它的输出正好对应两个不同的下游需求：

```text
FSharedShaderCompilerEnvironment           （给 shader compiler 看的“世界”）
  -> 含 /Engine/Generated/Material.ush 的虚拟 include 内容
  -> 含材质编译所需的 defines、include map、目标平台等环境

FMaterialCompilationOutput                 （给运行时参数系统的“说明书”）
  -> UniformExpressionSet
  -> Substrate material compilation output
  -> 用到的 scene textures、VT stacks、runtime VT 输出、统计与其他编译期标志
```

第一类输出回答“shader compiler 之后能看到什么 HLSL”。`/Engine/Generated/Material.ush` 是**虚拟 include**，不是磁盘资产，也不是运行时解释输入；它在 shared compiler environment 的 include map 中存活，供这一批 compile job 消费。pass shader 源码仍提供入口、frequency 与输出约定，材质 include 只负责属性计算。

第二类输出回答“运行时如何喂参数以及 consumer 需要怎样的材质布局”。`UniformExpressionSet` 记录哪些表达式能在 CPU/preshader/uniform-buffer 层求值，哪些纹理和参数要从 `FMaterialRenderProxy` 取当前实例值；Substrate output 则记录 closure、每像素存储和 feature/tile 等需求。它们随 shader-map content 保存，不是 shader bytecode，却必须与相应 shader 代码和 consumer 合约保持一致。

UE5.7 里 Translate 有两条路径，但对下游契约相同：

```text
启用 new HLSL generator 且 Substrate 未启用
  -> Material IR builder -> Material IR to HLSL -> 模板解析 -> /Engine/Generated/Material.ush

否则
  -> legacy FHLSLMaterialTranslator -> 节点 property 编译与 code chunk -> 模板解析 -> /Engine/Generated/Material.ush
```

新旧路径内部表示不同，但对下游的契约一致：都要把材质属性计算暴露成 generated material include，并填好 compile environment 与 compilation output。legacy translator 自身还可利用 translation 级缓存；new path 的 IR module 只是翻译期间的局部中间状态，不会变成 runtime graph。这里 Substrate 是一条重要边界：**UE5.7 的 new generator 在 Substrate 开启时不作为主路径使用**，但 Substrate 的影响也绝不止选择 translator；第十四节会继续追到 shader-map output 与 BasePass consumer。

## 七、legacy 路径：从材质属性根输入递归生成表达式

要直观理解“节点图怎样变成 HLSL”，legacy translator 是最好的窗口。它不是先把整张图变成一个独立 AST 再统一打印文件，而更像“从每个材质属性的根输入往上游递归编译”。

对红色金属材质，translator 会分别询问 BaseColor、Metallic、Roughness、Normal 等 material property。每个 property 都从 `UMaterial` 上对应的输入开始：

```text
BaseColor property
  -> 找到 BaseColor 输入线
  -> 如果连了表达式，沿 FExpressionInput 进入上游节点
  -> 常量 / Multiply / TextureSample / ScalarParameter 等节点调用 compiler API
  -> translator 得到一个 code chunk index
  -> 最终把 chunk 组织成 PixelMaterialInputs.BaseColor 的赋值
```

这里有个关键概念：**code chunk** 是 translator 内部的一段中间表达式或局部变量。节点返回的不是最终字符串，而是“我生成的这段代码在 translator 里的索引”。等每个 property 都编完，translator 再把这些 chunk 安排进材质模板，生成 `CalcMaterialParameters` 相关代码。

这解释了两个常见现象：

- **未连接的材质属性不会凭空消失**，而是走默认表达式或默认值。
- **static switch 可以在编译期选择分支**；普通 scalar parameter 则通常变成 uniform 表达式或 uniform buffer 数据。

对读者来说，最重要的不是记住每种节点的 compiler API，而是抓住数据形态的逐级变化：

```text
节点图连接
  -> material property 的表达式树
  -> translator code chunks
  -> PixelMaterialInputs / material getter 函数
  -> /Engine/Generated/Material.ush
```

所以如果 BaseColor 生成错了，调试应该先看 generated material include 里对应 property 的 HLSL，而不是先看 BasePass 的 MRT encode。BasePass 只消费生成后的数值；节点到属性值的错误发生在这一层。

## 八、Shared 与 per-job environment 不能互相污染

Translate 生成了材质 include，但还没有可用的 GPU shader。cached layout 先提供候选 compile coordinates；对某个具体 material，只有 `Material->ShouldCache` 等 gates 通过、pipeline filter 允许且结果确需生产时，才会创建或复用对应 job。UE 把这些实际 job 的环境拆成材质共享部分与局部部分：`FSharedShaderCompilerEnvironment` 承载 generated include 和可共享的 material defines；每个实际 job 再拥有自己的 source、entry、frequency、type permutation、VF 与 pass 条件。

不要把它想成一个全局配置文件，而要想成“**这一批 material shader compile job 共同看到的编译世界**”：

- 它提供 generated material include。
- 它带目标平台、feature level、材质相关 defines 和编译参数。
- job 的局部 environment 接收 shader type 的 entry、permutation 与 pass defines。
- mesh material job 先让 VF 写入输入策略，再让 shader type 写入自己的条件。

把它当作分层编译上下文，比当作“材质对象的字段”准确得多。共享层减少重复保存大块 include；局部层避免一个 job 的 define 污染另一个 job。BasePass PS、Nanite BasePass CS 与 DepthPass 可以看到同一份材质属性代码，但它们的 source/entry/frequency/VF 条件不同。完全复制每份环境更容易理解，却增加内存与构造成本；完全共享一个可变环境更省内存，却会产生顺序依赖和交叉污染。UE 选择共享不可变大块、按 job 叠加局部条件的折中。

如果顺序反了就会出错，这恰好反证了这一层的必要性：先编 pass shader、后生成材质 include，compiler 就看不到 `CalcMaterialParameters` 这类函数；跳过 vertex factory 修改环境，shader 可能用上不存在的输入结构；把材质 include 当成最终 shader，pass 的 render target、entry point、shader frequency 就无处表达。

**Worked case。** LocalVF BasePass PS 与 NaniteVF BasePass CS 共享红色金属材质 include，但前者是 raster pixel entry，后者是 compute/work-graph shading entry；两者的 VF defines、frequency 和 pass conditions不同。若 generated BaseColor 在两条路径都错，最后有效状态在 shared material output；若只有一条错，查对应 job 的 local environment 与 lookup coordinate。

> **材质编译不是“一份 HLSL 编一次”。它先生成共享材质部分；cached layout 给出候选 coordinates，具体 material gates 通过且结果需要生产时，相关 job 才在各自局部 environment 中完成组合。**

## 九、身份、清单、编译坐标与消费请求是四类决策

UE 材质系统里确实有 permutation，但不能用这个词吞掉所有决策。更稳定的分类是：

| 类别 | 决定什么 | 例子 | 出错时表现 |
| --- | --- | --- | --- |
| identity | 一份 material compile result 能否复用 | static switch、feature/quality、generator、依赖 hash | 命中错误结果，或不必要地重编 |
| layout | 按 material parameters、platform 与 type/VF/pipeline predicates 缓存的候选清单 | type/permutation、pipeline 与 per-VF entries | candidate 存在，但具体 material 仍可能不要求或没有对应结果 |
| compile coordinate | 标识一个候选或实际 job/result 的编译坐标，本身不保证 job 存在 | shader type 内部 permutation id；mesh shader 还带 VF type | candidate 存在，但 gate、job 或结果停在不同阶段 |
| lookup request | consumer 此刻实际要哪组坐标 | BasePass 根据 policy 请求 VS/PS，Nanite shading 请求 CS/work graph | map 中有其他结果，但当前请求查不到 |

`FMaterialShaderMapLayout` 缓存的是给定 `FMaterialShaderParameters` 与 platform/flags 下，经 shader、VF、pipeline predicates 过滤后的候选清单。具体 `FMaterial` 在 compile、`IsComplete` 与 runtime missing/lookup 阶段还会用 `Material->ShouldCache` 决定哪些 candidates 真正需要；因此 `complete for material` 是相对具体材质的判断。pass policy 通常在消费时选择具体 shader type/permutation 发起 request；它不是普遍存进 shader map 的“第四层 permutation”。第 21 章会从 predicate 与 VF 角度展开这一构建过程。

这就是组合数量被压住的原因。红色金属材质理论上可乘上平台、static switch、lightmap policy、GBuffer layout、skylight、VF、frequency 与 pass type，但 UE 先用 identity 隔离复用世界，再用 cached layout 压缩候选 coordinates，最后由具体 material gates 过滤实际需求与生产工作。全组合编译实现简单，却在大型内容库中不可扩展；完全运行时 JIT 可减少 cook 量，却不适合要求无编译器、可预测帧时间的 cooked 平台。

于是一个典型排查顺序就是逐层下钻：

```text
BasePass 取不到 shader
  -> identity 是否指向正确复用世界
  -> cached layout 是否包含目标 candidate，具体 Material->ShouldCache 是否要求它
  -> 若该 coordinate 被具体 material 要求且结果缺失，job 是否创建/复用并回填成功
  -> map 是否 complete for this material 且已发布到 RT
  -> BasePass lookup request 是否与已编结果一致
```

固定 identity 做一个检查：layout 可能包含 LocalVF 的某个 BasePass PS，却不包含 NaniteVF 的对应结果；也可能两者都在 layout，但 consumer 请求了不同 permutation。DDC hit 只说明找到了某份 map，不证明当前 lookup coordinate 在里面。普通 runtime parameter override 通常也不应让 layout 改变；它影响的是 uniform/resource 数据。

## 十、Compile job 如何回填、冻结并发布 ShaderMap

进入编译枚举后，UE 遍历 cached-layout candidates；只有具体 `Material->ShouldCache` 等 material gates 通过、未被 pipeline filter 排除，而且现有 map/job cache/in-flight work 尚未满足该结果时，才创建或复用相应 single job 或 pipeline job。candidate membership 不保证 job 存在。job key 必须能区分 type、permutation、VF、platform 与相关环境；编译输出还要带回 parameter map，供 shader 创建阶段验证和固化绑定。一个 job 成功，只证明这一格结果可用，不证明整张 map 已完成。

```text
material include
  + pass shader source / entry point
  + shader type permutation id
  + shader frequency
  + vertex factory type（mesh material shader 才有）
  + compile environment
  -> FShader
```

产出的 shader 按是否带 vertex factory 维度，落进不同的存放位置：

```text
无 VertexFactory 维度的 shader
  -> FMaterialShaderMapContent

带 VertexFactory 维度的 mesh material shader
  -> FMaterialShaderMapContent
       -> FMeshMaterialShaderMap(VertexFactoryType)
            -> ShaderType + PermutationId -> FShader
```

BasePass 的传统 StaticMesh 路径用的是 mesh material shader，所以它的 `TBasePassVS` / `TBasePassPS` 会落在某个 vertex factory 对应的 `FMeshMaterialShaderMap` 里。Nanite BasePass 的 compute 形态同样是材质 shader 体系里的一个具体 shader type / frequency / VF 组合，而**不是**运行时临时把材质节点图交给 Nanite 编译。

只有当结果处理、finalization、成功与 material-relative completeness 条件分别成立后，map 才能进入相应可用层级；persistent 完整结果可保存到 DDC。下一次相同 identity 与依赖输入就可能直接复用。这里务必和 PSO 分清：`FShader` 是已编译 shader 及其参数绑定信息，**不是** graphics PSO。PSO 还要结合 vertex declaration、render-target 格式与 blend/depth/rasterizer state；它可能由 precache 提前生产，也可能在 submit 查询时触发创建或等待策略。

结果处理不是一次赋值，而是增量状态转换：

```text
job output returned
  -> 校验编译成功与 parameter bindings
  -> 按 type/permutation/VF 插入 FShader 或 pipeline result
  -> 所有在途结果处理结束，finalize content
  -> 标记 compilation finalized
  -> 汇总 compiled successfully
  -> 对当前 FMaterial 用 cached candidates + material gates 检查 complete for material
  -> persistent 完整结果才具备保存 DDC 的条件
  -> GT install
  -> enqueue RT publication
```

这里的 owner 依次变化：worker/compile manager 拥有 job 处理，shader map 拥有冻结后的 result/content，`FMaterial` 拥有 GT/RT refs，consumer 最终通过当前线程可见 map 获得 shader ref。部分 finalized clone 允许异步期发布冻结快照，但不能把它等同于最终 map；`bCompilationFinalized`、`CompiledSuccessfully` 和 `IsComplete(Material)` 也不能互相替代。

为什么不等所有 shader 组成一个巨型原子编译包？单体包发布简单，但一个 entry 失败会模糊失败坐标，也会损失 job cache、并行度与增量回填。UE 选择按 job 生产、按 map 冻结收束，因此调试时可以精确定位“LocalVF 下某个 BasePass PS job 已失败”，而不是只得到“材质编译失败”。

**Worked case。** 跟踪 LocalVF BasePass PS：job 成功并插入 `FMeshMaterialShaderMap(LocalVF)` 是第一证据；map finalized 是第二证据；complete for red-metal material 是第三证据；GT 与 RT refs 更新是第四、第五证据。任一步失败都保留不同的 last-valid-state。shader ref 有效仍不代表 graphics PSO 已可用，更不代表 draw 已 recorded、queue submitted 或 GPU consumed。

## 十一、运行时参数通过 Proxy 与 Uniform 数据路径发布

现在只改红色金属材质的一个 scalar parameter，例如把 Roughness 从 0.65 调到 0.2。这个变化由 material instance 记录当前实例值，再通过 `FMaterialRenderProxy` 进入 `FUniformExpressionSet` 的求值与 uniform/resource 更新路径：

```text
MaterialInstance / MID 设置参数值
  -> GT override 通过更新请求到达对应 FMaterialRenderProxy
  -> proxy cache serial/dirty state 变化，可能进入 deferred/async update
  -> RT 在当前 FMaterialRenderContext 中求 uniform expressions / preshaders
  -> 创建或更新匹配当前 layout 的 uniform buffer、纹理、sampler、VT stack
  -> MeshDrawCommand / 当前 draw 绑定新 UB 与 resource
  -> 既有 shader 继续执行，但读取新值
```

这条路径的输入是新实例值和现有 shader layout，产物是 RT 可见的当前 uniform/resource 版本，下一消费者是 MeshDrawCommand/当前 draw。它服务高频值变化，生命周期由 proxy cache、uniform/resource 引用与实际 draw 消费共同约束；因为代码结构没有改变，所以通常不需要建立新 shader-map identity。

这条路径的设计目的，是避免把高频值变化变成 shader compile，同时避免每 draw 重新遍历整套表达式。`FUniformExpressionSet` 说明运行时该求什么；`FMaterialRenderProxy` 说明当前实例给出什么值，并通过 `GetMaterialWithFallback` 一类 RT 合约选择可用材质。cache 可以合并一帧内多次更新，但这也意味着 GT setter 返回只证明请求已发出，不证明 RT cache 已重建或 draw 已绑定新 buffer。

更具体地看，Roughness 从 0.65 调到 0.2 时，旧 shader 仍从材质 uniform 数据读取 Roughness；变化的是数据版本。若重编使 uniform layout 改变，旧 UB 不能继续按旧布局解释，必须重建；缓存的 command 若持有旧 UB 引用，也必须经过相应更新路径。runtime texture override 则除 cache 刷新外还要验证新 texture/resource 与 sampler/VT binding 已进入当前 draw。

因此这个局部案例有三个调试检查点：

```text
参数覆盖是否进入 material instance?
  -> 更新是否到达正确 render proxy，cache serial 是否变化?
  -> preshader/uniform evaluation 是否产出新数据?
  -> UB/resource 是否按当前 layout 重建?
  -> 本次 draw 绑定的是新资源还是上一版?
```

如果第一步没发生，是编辑器或实例参数问题；如果第二步没发生，是渲染代理/缓存失效问题；如果第三步没发生，才去查 draw 绑定和帧同步。任何一步都不需要重建 shader map。

所以改参数后的调试，正好沿着“两条更新路径”分叉：

- **改 static switch 后画面没变**：优先查 static parameter set、shader map id、DDC / shader map 是否重建。
- **改 scalar / vector 后画面没变**：查 instance override、proxy cache serial、evaluation、UB 与当前 draw binding。
- **改 runtime texture 后画面没变**：除上述路径外，查 texture/sampler/VT resource binding；不要先假定 identity 应改变。
- **改材质函数或 custom code 后画面没变**：优先查 shader map id 里的依赖 hash 和 DDC key，而不是 runtime uniform。

这也正是 Unity 里 static keyword 与 material property 差异在 UE 的对应关系：static 影响**编译身份和 shader layout**；普通 runtime property 影响 **uniform data**。

## 十二、BasePass 消费已发布的 ShaderMap

走完整条生产链，现在回到一帧渲染。红色金属 StaticMesh 进入 BasePass 时，BasePass mesh processor 接收 RT-visible material、当前 pass/VF 条件与已有 shader map，产出 MeshDrawCommand 所需的 shader refs 和最小管线输入：

```text
根据 pass 条件决定需要哪些 BasePass shader type / permutation
  -> 带上当前 VertexFactoryType 去 Material.TryGetShaders
  -> 把拿到的 shader refs 放进 mesh draw command / submit 路径
```

这个消费模型成立后，边界也随之清楚：BasePass 不在当前 draw 中重新遍历或 Translate 节点图；开发环境的 missing/ODSC 分支可以请求未来结果，但不能让当前 lookup 同步得到尚未完成的新 shader。

BasePass shader type 早已注册好自己的 pass shader source、entry point 和 shader frequency。传统路径通常请求 BasePass VS/PS；某些路径会请求 compute 或特殊 pixel shader 形态。在正常 cooked 路径或当前 map 已 complete 的稳定路径中，`TryGetShaders` 按当前线程可见的 `FMaterialShaderMap` / `FMeshMaterialShaderMap` 查询 `ShaderType + PermutationId + VertexFactoryType`，其结果就是当前已有 shader。

但“稳定 lookup”不能扩大成“这个调用在所有构建下都没有生产侧副作用”。在 `WITH_ODSC` 的开发条件下，missing/force-recompile 可登记 on-demand request；在 editor 中，如果已有 compiling map 而目标 shader 缺失，调用也可能创建并提交对应的 missing compile jobs。两条分支都只推进未来状态：**当前这次 `TryGetShaders` 不会等待新 job 完成，也不会立刻把刚请求的 shader 放进返回值。** 它仍按当前 map 返回已有 shader 或报告缺失，consumer 本次使用旧结果/fallback 或放弃该请求。

把消费端拆成一个最小 worked case：

```text
红色金属 StaticMesh 进入 BasePass
  -> render proxy 先给出当前可用 material，必要时落到 fallback
  -> MeshPassProcessor 知道它是 opaque、普通 StaticMesh、当前 GBuffer layout
  -> pass policy 决定需要 BasePass VS/PS 的哪组 permutation
  -> LocalVertexFactory 作为 VF 维度参与查询
  -> TryGetShaders 在 FMeshMaterialShaderMap(LocalVF) 里找对应 shader
  -> 成功后 shader refs、vertex declaration、bindings 与 minimal pipeline state 进入 MeshDrawCommand
  -> precache 可提前生产 PSO；submit 才用 active render targets 补全并查询/创建/等待或跳过
```

这些 editor/ODSC 分支不会重新 Translate 材质图，而是基于当前 material、map 与目标 coordinate 请求缺失结果；它们也不把 runtime compile 变成 cooked fallback。普通 cooked draw 缺 `LocalVertexFactory` 分区时不能现场补救。若 shader refs 已取到但 Roughness 仍旧，问题在 uniform binding 或 GBuffer 消费，不在 lookup。

因此 BasePass 出问题时，要先判断它落在哪一层：

- **BasePass 根本没请求某个 shader**：查 pass policy、lightmap policy、GBuffer layout、debug / OIT / 128-bit RT 等条件。
- **BasePass 请求了但 `TryGetShaders` 失败**：查 cached layout 是否包含 candidate、具体 material gates 是否要求它，以及 job/result/cook 是否真实存在。
- **BasePass 拿到 shader 但像素值错**：查 generated material include、runtime uniform、DBuffer、GBuffer encode，而不是重新跑材质编译。
- **shader refs 有效但 draw 被跳过或提交处卡顿**：查 PSO precache 状态、full initializer、async compile 与 active skip/wait policy，而不是回退到 shader-map DDC。

这和第 10 篇正好首尾相接：第 10 篇从 `CalcMaterialParameters` 往后讲 GBuffer 编码；本篇从节点图往前解释它从哪里来。这里的完成边界是 material shader 被正确选择并进入 command 配方；PSO ready、RHI recorded、queue submitted 与 GPU consumed 都是更深状态。

## 十三、Nanite 有两个不同的材质 consumer

“Nanite material shader”不是一个足够精确的类别。Nanite 至少在两个阶段消费材质相关编译结果，二者解决的问题、shader 类别和 lookup 形态不同。

第一类是 **programmable raster material shader**。它服务 Nanite raster/coverage 阶段中需要材质参与的可编程行为，代表锚点是继承自 `FMaterialShader` 的 `FNaniteMaterialShader`。它不是 traditional BasePass PS，也不是下面的 NaniteVF mesh material compute shader。它的存在让 raster 阶段只承担形成可见性所需的材质行为，而不必在每个潜在片元上完成整套 BasePass shading。

第二类是 **BasePass material shading**。可见像素按 material/shading bin 组织后，`TBasePassCS` 以 `FMeshMaterialShader` 身份、带 Nanite VF lookup key，使用 compute 或 work-graph frequency 写 BasePass 输出。它复用同一 material translation 与 shader-map 生产体系，但执行入口、VF coordinate 与 dispatch 组织不同于传统 raster BasePass。

```text
Nanite programmable raster consumer
  material result -> FNaniteMaterialShader -> coverage/visibility 所需可编程行为

Nanite BasePass shading consumer
  visible pixels + shading bin
  -> TBasePassCS / work-graph form + Nanite VF lookup
  -> material parameters and BasePass outputs
```

为什么分开？Nanite 希望先确定真正可见的几何/像素，再对可见像素执行完整材质计算，从而避免把 traditional per-triangle full shading 带回 raster 阶段。传统 VS/PS BasePass 对非 Nanite mesh 仍是合适方案；在不支持相应 Nanite compute/work-graph path 的平台上，应走平台支持的 Nanite 模式或非 Nanite 路径，而不是假定所有执行形态都存在。

**Worked case A：Nanite depth/coverage 错。** 最后有效状态停在 culling output，下一步检查 programmable raster shader、material raster bin 与 coverage；不要先查 BasePass shading CS。

**Worked case B：depth 正常、材质错。** raster consumer 已成立，下一步检查 shading bin、Nanite VF 下的 `TBasePassCS`/work-graph lookup、uniform bindings 与 BasePass output。传统 StaticMesh 也错时，故障更可能回到 shared material include、identity 或 runtime parameter。

第 16 篇负责 Nanite 管线细节；本章固定边界是：**Nanite 没有绕开 material pipeline，但 programmable raster 与 BasePass material shading 是两个 consumer，不能混成一种 shader。**

## 十四、Substrate 会改变输出与 consumer 合约

Substrate 不能只被理解成“Translate 时走 legacy”。它表达 layered closure/BSDF 后，材质编译结果还必须告诉 renderer：该材质属于什么 material type、需要多少 closure、每像素需要多少整数存储、有哪些 BSDF feature，以及应进入哪种 tile 分类。这些信息位于 `FSubstrateMaterialCompilationOutput`，随 shader map 保存，并以 GT/RT 查询形式供 consumer 使用。

这份 output 会改变下游合约：BasePass compile environment 选择相应输出格式；pass 参数包含 Substrate BasePass uniform；render-target setup 可能追加 Substrate MRT；stencil/DBuffer 与后续分类也会依据材质类型和 feature 条件调整。Nanite BasePass shading 同样必须写出与当前 Substrate 路径匹配的数据。若 shader code 按一种 closure/layout 写，而 BasePass targets 按另一种布局准备，结果不是“某个颜色参数不对”，而是 producer 与 consumer 对每像素存储契约失配。

```text
Substrate graph/translation
  -> generated material code
  -> FSubstrateMaterialCompilationOutput
  -> shader-map GT/RT queries
  -> BasePass compile/output format + uniform parameters
  -> matching MRT/tile/lighting consumers
```

这是一种设计取舍。legacy fixed GBuffer 对简单 shading model 更紧凑、目标和后续消费更固定；Substrate 为 layered material 提供更强表达力，却增加存储、分类和 consumer 协调成本。项目只需要简单固定材质、且平台预算紧张时，legacy path 可能更合适；需要复杂分层 BSDF 时，额外合约成本换来表达能力。

**Worked case。** 同一个红色金属外观，legacy shading model 的最后有效状态可停在固定 GBuffer targets；改为 Substrate slab 后，依次验证 translation output、closure/uint-per-pixel 等 shader-map 状态、BasePass 参数与 targets，再看最终 buffer writes。若 new generator 未使用但上述 Substrate output 与 targets 正确，这不是失败，而是 UE5.7 的受支持路径选择。

## 十五、Cook、fallback 与完成梯度

Cook 的目标不是“把 DDC 带到运行时”，而是为目标平台收集、编译、序列化并内联或放入 shader library，使运行时 lookup 能从 packaged data 得到所需 map/binary。`FShaderType` 已注册、材质资产已加载或本机 DDC 有结果，都不能替代目标平台 cook coverage。

缺失时必须按环境与材质重要性判断，不能只说“可能 fallback，也可能 fatal”：

| 环境 / 条件 | 缺失后的确定边界 | 为什么 |
| --- | --- | --- |
| Editor/uncooked，允许正常编译 | 可提交 full compile；若已有 compiling map，`TryGetShaders` 还可能补交 missing jobs，但当前调用不返回新 shader | 开发环境有 compiler 与迭代预算，当前 draw 使用已有结果/fallback |
| 支持 WITH_ODSC 的开发路径且请求属于 ODSC | `TryGetShaders` 可登记按需请求；当前调用仍只返回现有结果 | ODSC 是异步开发通道，不重新 Translate，也不是同步 draw 编译 |
| `RequiresCookedData()` 平台误入 material compile | fatal | editor/ODSC 的请求能力不能被解释成 cooked runtime compile 兜底 |
| 包加载发现目标要求 cooked data，但 material 没有 cooked shader | fatal | 包内容不满足运行前提 |
| 默认材质或 required-complete material 缺有效 map | fatal | fallback 链本身不能再缺最终保障 |
| 普通材质无法编译且 map 无效 | 清空无效 map，draw 通过 default material fallback 维持可渲染性 | 普通内容可降级，但画面不再代表原材质 |

开发版本也可以选择更严格的 cook validation，在打包前阻止缺失进入产物；这提高确定性但降低“先跑起来再请求”的迭代便利。shipping 平台通常更看重可预测帧时间与无编译器部署，因此把工作前移到 cook。

最后，把本章所有“完成”放到一条梯度里：

```text
identity built
  -> cache lookup finished
  -> jobs submitted
  -> job results processed
  -> map finalized
  -> compiled successfully
  -> complete for material
  -> GT installed
  -> RT published
  -> proxy uniform/resource current
  -> shader refs selected
  -> minimal pipeline state built
  -> PSO available (precache or submit-time resolution)
  -> RHI commands recorded
  -> platform queue submitted
  -> GPU consumed
```

每个箭头都有不同推进者和证据。material pipeline 的核心生产责任到 RT-published complete map 与当前 proxy 数据；consumer lookup、PSO、record/submit/GPU 是下游更深完成。任何日志若只写“material complete”，调试时都应追问它指哪一层。

## 十六、把整条链变成 last-valid-state 调试路线

把前面各节倒过来用，就是一条调试路线。关键不是列出所有类名，而是为每个症状记录四件事：**最后已验证状态、下一状态、负责推进者、可观察证据**。

```text
屏幕症状
  -> GPU/RenderDoc 能否证明正确 shader、UB/resource 与 targets 已绑定
  -> RHI command 是否 recorded，PSO 是否 ready/active，draw 是否被 skip
  -> consumer lookup request 是否得到正确 type/permutation/VF result
  -> RT 是否看见 complete map 与当前 proxy cache
  -> GT map 是否 finalized/success/complete，job 是否回填到正确分区
  -> cache/identity/layout 是否与 static dependencies 一致
  -> Translate 是否产出预期 material include、uniform 与 Substrate output
  -> 资产/static override 或 runtime override 最后在哪一层仍正确
```

几个具体症状按 last-valid-state 分流：

| 症状 | 最后有效状态 | 下一状态 / 推进者 | 需要的证据 |
| --- | --- | --- | --- |
| static switch 改了但画面不变 | asset static override 正确 | identity/layout / `FMaterial` cache | 新 identity、依赖 key、目标 coordinate 与 job 记录 |
| scalar 或 texture override 不生效 | instance override 正确 | proxy cache -> UB/resource / RT update | cache serial、evaluation 结果、当前 draw binding |
| 普通 mesh 正常，Nanite depth/coverage 错 | shared material translation 正确 | programmable raster consumer / Nanite raster setup | raster shader/bin 与 coverage 输出 |
| Nanite depth 正常，shading 错 | raster result 正确 | Nanite BasePass shading lookup/dispatch | NaniteVF shader ref、shading bin、参数与 outputs |
| 仅 Substrate 路径错 | generated material code 正确 | Substrate output -> BasePass targets/consumer | closure/layout query、MRT/parameters 与 buffer write 一致 |
| cooked 普通材质显示默认材质 | package/material 已加载 | cooked map lookup/fallback policy | 目标平台 map 缺失及 default fallback 选择；不要等待 runtime compile |
| cooked default/required material 缺失 | cook coverage 是最后有效阶段 | load/validation | 缺失应作为 fatal 内容错误处理 |
| shader refs 正确但 draw 消失 | MDC minimal state 已构建 | PSO precache/submit policy | full initializer、precache result、active skip/wait 状态 |
| DDC miss 首次卡顿 | identity 正确 | async DDC/job pipeline | request pending、in-flight sharing、job queue 与 finalization 时间 |

GPU capture 很适合证明最终 shader、resource、PSO 与 draw 状态，却不能单独证明 asset identity、layout filter 或 DDC 决策；这些阶段需要 compile diagnostics、日志或 CPU debugger。反过来，GT 显示 map complete 也不能证明当前 GPU draw 已消费它。工具必须匹配状态深度。

把本篇压缩成一句话：

> **UE 把资产语义、`FMaterial` 编译合约、具体 resource、shader-map 结果与 render-proxy runtime data 分开；identity 决定复用，cached layout 提供候选 compile coordinates，具体 material gates 通过且结果确需生产时才创建或复用 job，GT/RT publication 再把可用结果交给 consumer。BasePass、Nanite raster/shading 与 Substrate 合约消费这些结果，普通 runtime override 更新 UB/resource，而 cook、PSO、record、queue 与 GPU completion 各有独立边界。**

下一篇第 21 章从这里继续往下看：这些已经编译好的 shader 如何声明参数，如何让 VertexFactory 和 shader parameter metadata 参与绑定，最终怎样接到 RHI shader binding。
