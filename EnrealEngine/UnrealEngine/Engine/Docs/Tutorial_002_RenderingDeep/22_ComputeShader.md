# 22 Compute Shader 与 GPU 通用计算

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `03_ThreadModel.md`、`04_RHI.md`、`05_RenderGraph.md`、`20_MaterialPipeline.md`、`21_ShaderSystem.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `22_ComputeShader_CoverageMatrix.md`

---

## 开场：一句 `Dispatch` 在 UE 里为什么不够

Unity 读者看 compute shader 时，常把整件事压缩成一个用户操作：找到 kernel，设置好 buffer/texture，然后 `Dispatch(groups)`。公开层让 shader 查找、资源绑定、状态处理、命令记录 / 提交与 GPU 执行看起来像一个整体；这些工作并不发生在同一完成时点。接口越短，出错时越需要主动拆开证据层。

这个模型搬到 UE 里太短。UE Renderer 的一次 compute pass 不是孤立的一句调用，而是一段被 shader system、RDG（RenderGraph）、RHI command list 和后端队列共同约束的工作。它要回答的问题分散在好几层：

- 这份 shader 是否编译进了正确的 shader map？
- 参数结构声明了哪些资源访问？
- dispatch 的 group 数量是否符合平台限制？
- UAV 写完之后谁来插同步屏障？
- async compute 是否真的能和 graphics 重叠？
- 命令最后怎样落到 D3D12 / Vulkan / Metal？

UE 把这些责任拆开，不是为了制造复杂度，而是为了让大型 renderer 能**分层调试**：shader 缺失、资源可见性、dispatch 覆盖范围、队列同步、后端命令——每一类问题都能单独定位，不必一上来就猜。本章的目标，就是帮你建立这套分层心智模型，让“compute 出错了”能落到具体某一层，而不是反复改 HLSL 碰运气。

### 贯穿案例：一次 view depth copy

为了让模型有抓手，本章自始至终跟踪一个很小的对象：一次 **view depth copy** compute pass。它的 compute shader 类型是 `FViewDepthCopyCS`——读取当前 view 的 depth texture，把它写进另一张 UAV texture，过程中用到 view uniform buffer。

选它是因为它够干净：不依赖 Nanite、Lumen、TSR 的任何算法细节，却完整走过 UE compute shader 的主路径。后面每讲一层，我们都把这次 depth copy 套进去看一遍。

### 主路径地图

下面这张地图是本章要沿着走的路线，也是本章的边界。它不是源码调用栈，而是一份**责任交接**的顺序——数据和控制权依次从哪一层交到下一层：

```text
C++ shader type + permutation（确定要执行的 compute 代码）
  -> FParameters / RDG resources（声明绑定数据与资源读写）
  -> 数据域 + group size         （形成 direct group count 或 indirect args）
  -> Renderer 声明 RDG pass      （把 dispatch 候选节点登记进帧图）
  -> RDG compile                 （决定保留、资源寿命、barrier 与 async pipe）
  -> 执行阶段设置 PSO + 参数并录制 RHI dispatch
  -> RHI translation 编码并封闭 platform command list
  -> platform queue submit
  -> GPU 执行 dispatch，completion fence 到达
  -> 下游 consumer 验证输出，资源在最后消费者之后退休或复用
```

---

## 本篇边界

本章只讲 compute shader 在 UE 中怎样**声明、调度、同步和执行**。

第 20 篇已经讲过材质节点图如何进入 shader map，第 21 篇已经讲过 shader type、VertexFactory 和参数绑定如何服务 mesh draw。本章复用这些概念，但不再展开材质图、VertexFactory、BasePass shader lookup 或 ShaderCompileWorker。后端的 root signature、descriptor heap、residency、command signature 二进制布局也不在边界内。本章仍会继续越过原生 `Dispatch`，讲清 command list finalize、queue submit 和 completion fence，因为这些阶段决定“命令被记录”“命令已提交”“GPU 已完成”能否被严格区分。

下一篇第 23 章会从工具侧接上：本章建立的是**概念排查路线**，第 23 章会把 RenderDoc、Unreal Insights、RDG/CVar 调试和 GPU profiler 串成更具体的定位方法。

## 本篇必须能回答

读完本章，你应该能回答：

- UE compute shader 为什么先是一个 C++ shader type，而不是一个资产 kernel 调用。
- `FParameters` 为什么既是 shader binding 数据，又是 RDG resource manifest。
- graph 参数与 lambda 的值、引用、指针捕获分别由谁保活，哪一种错误属于 RDG 声明，哪一种属于 C++ 生命周期。
- permutation、compile environment 和 HLSL `[numthreads]` 之间如何配合。
- `FComputeShaderUtils::AddPass` 声明了什么，cull root、external output、纯副作用和 `NeverCull` 怎样决定节点是否保留。
- thread group size、group count、wrapped group count 分别解决什么问题，为什么 wrapped 后 Z 仍可能超平台上限。
- direct dispatch 和 indirect dispatch 的 group count 从哪里来，间接参数 buffer 如何从创建、写入、transition、消费走到复用或退休。
- SRV/UAV resource transition 和 UAV barrier 由谁推导，`SkipBarrier` 需要哪些连续访问与跨 pipe 条件。
- `ERDGPassFlags::AsyncCompute` 改变了什么，完整资格条件是什么，为什么它不是“保证并行”的开关。
- compute PSO 的 cache entry、异步创建和 native PSO ready 为什么是不同状态。
- `FRHIComputeCommandList`、原生 command list、platform queue submit 和 completion fence 的边界在哪里。

---

## 1. 第一层：compute shader 先是一个 shader type

在 UE Renderer 里，compute shader 的入口不是“加载一个 compute asset 并按名字找 kernel”。它首先是一种 **C++ shader type**。

这个类型本身不保存当前帧的数据，也不是 GPU 资源。它是一份注册到 shader system 的**身份说明**，告诉 UE 四件事：

| 身份维度 | 它告诉 UE 什么 |
| --- | --- |
| 类别 | 它属于 global / material / mesh material 中的哪一类 |
| 参数结构体 | 运行时绑定哪些常量、SRV、UAV、uniform buffer |
| permutation | 编译哪些变体，运行时用哪个变体取 shader |
| HLSL 入口 + frequency | 对应哪个 HLSL 入口，最终以 compute frequency 编译 |

`FViewDepthCopyCS` 是典型的 **global compute shader**。这里的 “global” 不是“全局变量”，而是“它不依赖某个材质节点图，也不依赖 VertexFactory”。这类 shader 适合 depth copy、后处理工具，以及 Lumen/Nanite/TSR 里的 renderer 基础设施计算。和第 21 篇的 mesh material shader 相比，它少了一条 VertexFactory 轴；和第 20 篇的 material shader 相比，它不需要 material include。

把 `FViewDepthCopyCS` 拆成一张“身份护照”，会更容易看清 global shader declaration 到底在声明什么：

| 身份片段 | 它给 UE 的承诺 | 出错时先问 |
| --- | --- | --- |
| 继承 `FGlobalShader` 并声明 global shader 类型 | 这份 shader 走 global shader map，不乘上 material / VertexFactory 维度 | 它是否在模块加载和 shader type 初始化期被注册到了全局类型集合 |
| `SHADER_USE_PARAMETER_STRUCT` 与 `FParameters` | 运行时绑定和 RDG 资源访问都由同一份参数结构描述 | C++ 参数结构、HLSL 参数和 RDG resource manifest 是否对齐 |
| `ShouldCompilePermutation` | 当前平台和 feature level 是否需要编译这个 shader | shader missing 是不是因为平台过滤，而不是 pass 没执行 |
| `ModifyCompilationEnvironment` 写入 `THREADGROUP_SIZE` | HLSL `[numthreads]` 看到的 group size 来自 C++ 侧同一份常量 | C++ dispatch 计算和 HLSL 编译常量是否一致 |
| 实现宏绑定 HLSL 入口和 `SF_Compute` | 最终编译的是 compute frequency 的 HLSL entry | entry 名、frequency 或文件绑定错了，RDG/RHI 仍然拿不到可执行 shader |

这张表不是要你背宏名，而是把第一层的责任说清楚：global shader declaration 只生产“哪份 compute 代码可被查询”的身份合同。它不拥有当前帧的 depth texture，不创建 UAV，也不决定这次 dispatch 跑多少组。后面任何输出错误都要先排除“根本没有正确 shader identity”这一层，否则在 RDG 或 RHI 里查只会看到后果。

**这一层错了，后面的 RDG 和 RHI 都救不回来。** 比如 shader type 没注册、HLSL 入口不匹配、shader frequency 不是 compute，运行时从 shader map 里取 shader 就会失败。此时症状看起来像“pass 没跑”，但根因在第一层：UE 手里根本没有那份可执行的 compute shader。

还要把“允许编译”和“运行时已经有 shader”分开。平台与 feature level 先决定当前 permutation 是否具备编译资格；cook 或 ODSC 再决定对应 shader map 何时获得结果；运行时查询成功后，`TShaderRef` 才能交给后续 pass。这个引用只拥有 shader 代码身份，不拥有本帧参数、RDG 资源或 compute PSO。

```text
编译资格成立
  -> shader map 中该 permutation 已存在且完成
  -> 运行时查询得到有效 shader ref
  -> 后续才有资格创建或查找 compute PSO
```

因此这里的 last-valid-state 最深只能写成“目标 permutation 可取得”。它不能外推为“PSO 已 ready”“pass 未被裁剪”或“GPU 已执行”。如果 shader map 仍在编译、ODSC 请求尚未完成，或者当前 cook 没有该变体，后面的 dispatch 链根本还没有起点。

所以调试时先问这一层的四个问题：

```text
这个 C++ shader type 是否注册进 shader system?
它是否以 compute frequency 编译?
当前平台和 feature level 是否允许编译?
运行时取 shader 时用的 permutation 是否和编译时一致?
```

这一层只回答“GPU 将执行哪份 shader 代码”。它还没回答“这次 dispatch 用哪些资源”——那是下一层参数结构体的职责。

---

## 2. 第二层：参数结构体有双重身份

UE compute shader 最容易被误解的设计，是 `FParameters` 同时承担两件事：

| 站在谁的角度看 | `FParameters` 是什么 |
| --- | --- |
| shader binding | 描述 shader 执行时要绑定的常量、texture、SRV、UAV、uniform buffer |
| RDG | 描述这个 pass 在帧图中读写哪些 graph resource |

在 depth copy 案例里，参数结构体装着三类东西：一个只读 depth texture、一个可写 UAV texture、一个 view uniform buffer。对 HLSL 来说，这些是 shader 能访问的数据；对 RDG 来说，这些是 pass 的**资源访问清单**。RDG 在执行 pass 之前就能扫描这份清单，知道本 pass 读 depth、写目标 texture，并据此建立依赖、延长资源生命周期、插入 transition 和 UAV barrier。

把这三个字段当成一份小账本，而不是一个普通 C++ struct，会更接近 UE 的真实设计：

| 参数 | 对 shader 的意义 | 对 RDG 的意义 | 所有权 / 生命周期边界 |
| --- | --- | --- | --- |
| `SceneDepthTexture` | HLSL 以 texture 方式读取当前 view 的 depth | 本 pass 是这张 depth resource 的读者 | 资源由上游 pass 或图外系统生产，本 pass 只声明读取 |
| `RWDepthTexture` | HLSL 以 UAV 方式写入目标 depth copy | 本 pass 是目标 texture 的写者，后续读者需要依赖它 | `CreateUAV` 创建的是 RDG view 声明，不是立刻写入像素 |
| `View` uniform | HLSL 需要 view rect、矩阵等每视图常量 | 这是 shader binding，不是本 pass 写出的 RDG resource | view uniform 由本帧 view 所有，参数结构只持有引用 |

这份账本解释了一个很常见的误判：`RWDepthTexture` 字段被填好，只说明“图里有一个可写目标视图”。目标纹理内容还没有改变；真正写入要等这个 pass 没被裁剪、RDG 执行到 lambda、RHI 录下 dispatch、GPU 执行到对应线程之后才发生。调试 output 没变时，先把“UAV view 已经声明”和“UAV 内容已经被写入”分成两个状态。

### Lambda 捕获解决延迟执行，不替代资源声明

“lambda 捕获了资源”不等于“RDG 知道资源被使用”。lambda 是延迟执行体的数据闭包，只有在 RDG 编译和调度之后才会被调用；resource manifest 则必须在 `AddPass` 时就可以枚举。RDG 需要先看见 producer/consumer 才能决定裁剪、生命周期和 barrier，不能等进入 lambda 后再猜依赖。

把正确模型和错误模型并排放，差别一目了然：

```text
错误模型:
  lambda 里会用资源，所以 RDG 执行时自然知道

正确模型:
  参数结构体声明资源访问，RDG 在编译前就知道
  lambda 只是用已经声明好的资源去录制命令
```

### 为什么参数要由 graph allocator 持有

`GraphBuilder.AllocParameters` 也属于这条生命周期规则。RDG 的 `AddPass` 只是**记录**，真正执行要等到 graph execute 阶段。参数不能放在普通栈变量里：创建 pass 的函数一返回，栈对象就失效了；它必须由 graph allocator 持有，活到 pass 执行那一刻。

这里有两条彼此独立的正确性轴：

| 轴 | 谁负责 | 失败表现 |
| --- | --- | --- |
| RDG 声明可见性 | pass 参数 metadata 与显式 access | 资源存在但图不知道依赖，导致裁剪、错误生命周期或缺少同步 |
| C++ 捕获生命周期 | pass 作者与被捕获对象的 owner | 图依赖正确，但执行 lambda 时引用悬空、裸指针失效或读到后来被修改的外部状态 |

普通捕获并非一律错误。小型不可变值按值捕获最容易推理，compute helper 也会按值保存 shader ref、group count 和参数指针；值副本跟随 lambda 活到执行。引用捕获只在被引用对象明确活过 graph execute 且执行期间不会被意外修改时成立，函数局部栈对象通常不满足。裸指针捕获不自动保活 pointee，必须由 graph allocator、引用计数对象或更长寿命的外部 owner 给出明确保证。RDG resource ref 和 RHI resource ref 也不能只凭“指针还非空”判断安全：前者仍要出现在图参数中，后者若绕过 RDG 还需要图外同步与寿命合同。

用一个 custom pass 对照两种失败：参数结构正确声明了输出 UAV，但 lambda 又按引用捕获函数局部配置，函数返回后配置失效，这是 **C++ 生命周期错误**；反过来，某个外部 owner 确实保活了裸指针，但资源没有写进参数结构，这是 **RDG 声明错误**。前者的 last-valid-state 是“pass 已保留但 execute 数据无有效寿命保证”，后者最多是“C++ 对象仍存在”，都不能推进到安全录制。

### 漏一个、多一个会怎样

- 参数结构体**漏了一个 UAV**：RDG 可能不知道本 pass 写了目标资源，后续读取就缺少正确的依赖和屏障。
- 参数结构体**保留了 shader 实际没用的资源**：RDG 可能被误导，延长无用生命周期或建立伪依赖。

compute helper 会清理 shader 未使用的 graph resources，目的正是让 manifest 更贴近真实绑定。

所以 `FParameters` 的首要教学意义不是“传参方便”，而是**把本 pass 的资源合同提前交给 RDG**。它让 RDG 能在执行前回答：谁生产 depth，谁写 output，谁稍后读取 output；如果这份合同不完整，RDG 就没有足够信息保护资源状态。

---

## 3. 第三层：permutation 选编译版本，compile environment 决定 HLSL 看到什么

参数结构体解决“绑定什么”。permutation 解决“**编译哪几份 shader**”。

compute shader 常把线程组大小、平台能力、算法开关、质量等级写成编译期变体。UE 不把这些都做成运行时分支，而是用 permutation domain 把离散维度编码成一个 permutation id。

depth copy 的 permutation 很简单：没有额外维度，只编一份。但它仍然用到两个关键的编译钩子：

| 编译钩子 | 它决定什么 |
| --- | --- |
| `ShouldCompilePermutation` | 当前平台 / feature level 是否需要这份 shader |
| `ModifyCompilationEnvironment` | 写入 HLSL 编译时可见的 define，例如 `THREADGROUP_SIZE` |

这两个钩子和 thread group 模型直接相连。HLSL 的 `[numthreads]` 决定一个 group 内有多少线程；C++ 侧的 group size 常通过 compile environment 写进 shader，这样 HLSL 和 C++ 的 dispatch 计算用的是**同一份常量**。若 C++ 认为 group size 是 8，而 HLSL 实际用 16，那么输出覆盖范围、越界判断、dispatch 规模都会错。

更复杂的 renderer compute shader 会把 permutation 分成布尔、连续整数或稀疏整数维度。无论维度多少，调试顺序都一样：

```text
permutation domain 是否包含这个维度?
ShouldCompilePermutation 是否允许当前组合?
ModifyCompilationEnvironment 是否把所需 define 写给 HLSL?
目标 shader map 是否已经拥有并完成这个 permutation?
运行时 GetShader 是否用同一套 permutation vector?
HLSL 是否按这些 define 编译出了预期路径?
```

permutation 适合平台能力、线程组形状或算法结构这类有限离散选择，因为编译器能裁掉无关代码。连续阈值、每帧尺寸和频繁变化的质量参数更适合普通 shader 参数：把它们做成 permutation 会扩大编译、cook、缓存和首次使用覆盖面；全部改成运行时参数则会保留动态分支或多余数据路径。选择的重访条件很明确：当变体数量开始拖累 cook、PSO 预热或磁盘缓存，而运行时分支代价可接受时，应把维度移回参数；当分支代价稳定高于变体成本且组合数量可控时，permutation 才更合适。

**一个关键区分：permutation 不是 async compute。** permutation 选择编译版本；RDG pass flag 选择调度资格。一个 shader 可以有多个 thread group permutation，但仍运行在 graphics pipe 的 compute 阶段；一个 pass 可以标记为 async compute，但这并不会自动生成“异步版的 shader”。把这两件事混在一起，会让你在错的层找问题。

---

## 4. 第四层：`AddPass` 声明 dispatch 意图，不立刻发命令

shader type、参数和 permutation 都备齐之后，Renderer 才能声明一次 compute dispatch。在 depth copy 里，Renderer 大致做四件事，顺序是连贯的：

1. **取当前 permutation 的 compute shader** —— 从 view / global shader map 得到一个 `TShaderRef`。
2. **分配并填写 graph-lifetime 参数** —— 输入 depth、输出 UAV、view uniform buffer。
3. **根据输出区域计算 group count** —— 以输出像素域决定要 dispatch 多少 group。
4. **调用 compute `AddPass`** —— 把这次 compute 工作登记进 RDG。

### `AddPass` 不是 `Dispatch`

`FComputeShaderUtils::AddPass` 不会立即 dispatch。它是 compute 专用的 RDG pass 声明 helper：检查 pass flags 只能表达 compute 或 async compute 语义，对 group count 做诊断校验，清理未使用的 graph resources，然后把一个**执行 lambda** 登记给 RDG。平台上界仍由调用方负责满足，诊断校验不替代输入规模约束。

这个 lambda 的命令列表类型是 `FRHIComputeCommandList&`。这个类型不是随手选的，它表达了 pass 的**能力边界**：compute pass 可以设置 compute PSO、绑定 compute shader 参数、dispatch、做必要的 transition，但不能画 graphics draw。graphics command list 继承自 compute command list，所以 graphics pipe 上也能执行 compute；反过来不成立——纯 compute list 不拥有 draw 能力。

### `AddPass` 的产物是帧图节点，不是 GPU 命令

因此 `AddPass` 阶段产出的不是 GPU 命令，而是一个可编译的帧图节点，包含四样东西：

| 节点构成 | 作用 |
| --- | --- |
| Pass name | 调试 / profile 时可见的名称 |
| Parameters | shader binding + RDG resource manifest |
| Pass flags | `Compute` 或 `AsyncCompute` |
| Execute lambda | 图执行阶段拿到 compute command list 后录制 RHI 命令 |

这和 Unity `CommandBuffer.DispatchCompute` 的直觉不同。在 UE 里，声明先进入 RDG，RDG 后续还要裁剪、合并资源生命周期、插入 barrier、计算 async fork/join，最后才执行 lambda。**看到 `AddPass` 不代表 GPU 已经收到 dispatch，也不代表这个 pass 一定不会被裁剪。**

把 depth copy 的输出状态沿着 `AddPass` 往后推一遍，可以看到每个断点能证明什么：

```text
AddViewDepthCopyCSPass 被调用
  -> 只能证明 Renderer 声明了一个候选 compute pass

PassParameters 填了 SceneDepthTexture / RWDepthTexture / View
  -> 只能证明 RDG 能看到读写合同和绑定数据

FComputeShaderUtils::AddPass 返回
  -> 只能证明图里有一个带 compute lambda 的节点

RDG 编译后该 pass 未被裁剪，执行阶段进入 lambda
  -> 才开始设置 compute PSO、绑定参数并录制 dispatch

RHI dispatch recorded
  -> 只能证明命令进入 RHI 记录

platform queue submitted
  -> 只能证明队列接收了 payload

completion fence reached，且下游读取同一资源
  -> 才能分别证明 GPU 完成与 output 被目标 consumer 验证
```

因此，“output 没写出来”不能只在 `AddPass` 调用点下断点。先确认 pass 在 RDG 事件里确实执行，再看 group count 是否覆盖目标区域，最后看 output UAV 写入后有没有被后续 pass 或图外边界读取。否则你可能只证明了“声明发生过”，却没有证明“写入发生过”。

### 节点怎样从候选变成保留：cull root、external output 与副作用

RDG 裁剪不是按“这个 pass 看起来重要”决定，而是从可观察根反向保留生产链。一个图内输出如果被后续保留 pass 消费，它的 producer 会沿依赖链被保留；资源成为图外可见输出，或者 pass 参数包含 external output，也会建立可观察边界。若节点既没有保留中的消费者，也没有 external output，更没有声明真实副作用，编译器就可以删除它。

```text
候选 pass
  -> 输出被保留 consumer 读取：沿 producer 链反向保留
  -> 生产 cull-root / external output：作为可观察根保留
  -> 只有无法用图资源表达的真实副作用：显式 NeverCull
  -> 以上都不成立：允许裁剪
```

这里的 owner 分工必须明确：pass 作者负责把真实输入、输出与副作用声明完整；RDG 编译器拥有最终裁剪决定。`NeverCull` 不是“我正在调试，所以别删”的通用修复，它表达的是资源依赖之外仍有必须发生的副作用，例如 capture 边界或显式外部动作。它会连带保留必要的上游生产链，减少图优化空间并延长相关资源寿命；能用 producer-consumer 或 external/extracted output 表达的价值，应优先使用资源合同。

用 depth copy 做三组对照：

| 声明 | 编译结果 | 正确修复 |
| --- | --- | --- |
| 输出 copy 无任何消费者 | 该 copy pass 可被裁剪 | 如果结果确实要用，让真实下游读取同一 RDG resource |
| 输出被后处理消费或交到图外 | producer 链成为最终输出的一部分 | 保持真实资源连接，不额外加 `NeverCull` |
| pass 只触发图外副作用，没有可表达的 graph output | 仅靠资源图无法证明价值 | 在副作用真实且不可替代时使用 `NeverCull` |

因此 `AddPass` 后的 last-valid-state 是 **Declared**；RDG compile 后确认未裁剪才是 **Retained**；进入 execute lambda 才能说 CPU 开始录制。被裁 pass 不建立实际执行期资源寿命，也不可能靠后端断点“找回来”。

---

## 5. 第五层：thread group 把数据域映射成 dispatch 形状

compute dispatch 的第二个高频误区，是把 thread count、group size、group count 混成一个数。在 UE 主线里，它们是三个不同的概念：

| 层 | 含义 | 由谁决定 |
| --- | --- | --- |
| 数据域 | 这次计算覆盖多少元素，例如输出纹理宽高 | 算法 / 输出尺寸 |
| group size | 一个 thread group 内有多少线程 | HLSL `[numthreads]` |
| group count | CPU/RDG 发给 `Dispatch` 的 group 数量 | C++ 侧由数据域和 group size 算出 |

### depth copy 怎么算

depth copy 的数据域是 view rect 的像素尺寸。若 shader 每个 group 覆盖 8×8 像素，C++ 侧就用**向上取整**把像素尺寸转换成 group count。一个 1920×1080 的 view rect 会得到 240×135 个 group；如果数据域是 1919×1079，向上取整后仍然是 240×135，最后一圈线程就会落在“理想数据域”之外。

这一步有一个容易被忽略的所有权边界：C++ helper 根据数据域计算请求的 group count，调用方继续负责平台上界，RHI/后端执行给定 group；每个线程最终访问哪个像素，则由 HLSL 的 dispatch thread id 决定。一般 compute shader 要么在 HLSL 里对尾部线程做 bounds check，要么保证取整后的全部线程访问仍落在底层资源的有效范围内。贯穿案例里的 `CopyDepthCS` 没有显式 bounds check；它用 `DispatchThreadId + View.ViewRectMinAndSize.xy` 的 `xy` 分量直接索引输入和输出，所以当 view rect 尺寸不是 8 的整数倍时，最后一组线程会越过 `ViewRect.Max`。这不等于它必然越过底层 texture，但说明 **view rect 本身没有提供边界保护**：调用点必须保证扩出的尾部坐标对源和目标 texture 都有效，或由 shader 增加显式裁剪。调试时要同时核对 C++ 数据域、平台上界、这条 HLSL 索引公式和两张 texture 的实际 extent，不能把“helper 向上取整”误读成“shader 自动安全”。

这也解释了为什么 dispatch 问题常表现为几类不同症状：

```text
输出少一圈:
  数据域或 group count 可能少算了

输出边缘异常:
  尾部线程的边界处理或 view rect 假设可能不匹配

输出完全没动:
  group count 可能为 0，或 pass 没执行，或写入资源不是你后续读取的那张

只有某些 view 出错:
  view rect、目标 texture 尺寸和 HLSL 索引偏移的组合需要逐项核对
```

### 平台维度上限与 wrapped dispatch

平台会限制每个维度的最大 group 数。普通 2D/3D 工作需要逐项校验 `GroupCount.X/Y/Z`；对较大的 1D 工作，UE 提供 **wrapped group count**，以固定 stride 先把过大的 X 折到 Y，再把过大的 Y 折到 Z，shader 再把三维 group id 还原成线性 group id。

wrapped 是线性 group id 的编码方案，不是“任意大工作量都合法”的保证。helper 没有在 Z 超限后继续分层；极大目标数仍会得到超过平台 Z 上限的结果。后续 `ValidateGroupCount` 对 X/Y/Z 使用的是 `ensure` 诊断边界，不是阻止所有构建继续提交的硬性 `check`。调用方仍然拥有输入规模上界，不能把合法性责任交给 helper。

```text
目标线性 group 数
  -> X 超限时折入 Y
  -> Y 超限时再折入 Z
  -> 验证 X/Y/Z 都未超过平台上限
  -> shader 反解线性 group id，并对尾部 early out
```

若 Z 仍超限，应把工作分块成多个 dispatch / pass，或改用 GPU work queue、indirect 循环等有界消费方式。分块增加 dispatch 次数、中间状态和跨 pass 同步，但换来可证明的平台上界；wrapped 适合“大但仍有明确上限”的 1D 工作。以 64 threads/group 的 buffer 处理为例，先记录目标 group 数和折叠后的 X/Y/Z，再验证平台上限与 shader early-out。通过这一检查只证明 dispatch 参数合法，不证明 shader 索引安全、资源状态正确或 GPU 已完成。

输出缺边、少一圈、越界写、只有部分像素更新时，按这个顺序查：

```text
输出数据域是否是实际要写的大小?
HLSL [numthreads] 是否和 C++ 的 group size define 一致?
GetGroupCount 是否向上取整，而不是向下取整?
shader 是否对边界 group 做了 bounds check?
GroupCount 是否超过平台维度上限?
大 1D 工作是否需要 wrapped group count?
wrapped 后 Z 是否仍超限，是否必须分块?
```

这一层回答“这次 dispatch 覆盖多少工作”。它仍然不等于资源同步正确——写入 UAV 后谁能读到结果，要看 RDG manifest 和 barrier（第 7 层）。

---

## 6. group count 从哪来：direct 与 indirect dispatch

direct dispatch 的 group count 由 CPU 在录制前给出；indirect dispatch 的 group count 由 GPU producer 写入 args buffer，再由后续 GPU consumer 读取。来源差异是起点，但 indirect 还改变了 buffer usage、offset 校验、producer-consumer transition、寿命和完成判据。

| | group count 来源 | 典型场景 |
| --- | --- | --- |
| direct dispatch | CPU 在添加 pass 时就能算出 | depth copy：输出尺寸来自 view rect，group size 是编译时常量 |
| indirect dispatch | GPU 先前工作写进 args buffer | GPU 先 compact 一个任务列表，再按结果决定 dispatch 规模 |

depth copy 是 direct dispatch：CPU 直接把算好的 group count 交给 `AddPass`。

### indirect args 是一份跨 pass 的 GPU 工作合同

当 CPU 在录制时不知道最终工作量，indirect dispatch 让规模留在 GPU 时间线上，避免把 compact count 读回 CPU 再重新提交。代价是 args buffer 从创建到退休都必须闭环：

```text
1. 创建
   使用支持 indirect args 的 graph buffer 描述，容量覆盖目标 offset 后的参数结构。

2. 初始化
   producer 必须完整覆写 X/Y/Z，或先清零再写；旧帧残值不能成为隐式默认。

3. GPU producer 写入
   写入的是 group count，不是 thread count；CPU 只规定上限、布局和是否走这条路径。

4. RDG 建立依赖
   buffer 从 UAV/write 转成 IndirectArgs read，写者和消费者位于同一资源链。

5. consumer dispatch
   offset 满足 4-byte 对齐，offset 后有完整参数容量，平台边界检查成立，随后录制 DispatchIndirect。

6. 最后消费者完成
   只有覆盖该 consumer 的 queue completion fence 到达后，本次 args 与被间接启动的工作才达到 GPU complete。

7. 复用或退休
   图内资源由 RDG 覆盖到最后消费者；跨图复用必须有 external / extraction 与 GPU 完成合同，不能在旧 consumer 尚未完成时覆盖。
```

这条链的 owner 也分层：创建者拥有 buffer 描述与容量；producer shader 拥有本次 X/Y/Z 内容；RDG 拥有 UAV -> IndirectArgs 依赖与 transition；consumer 拥有 offset 和 indirect dispatch；外部资源 owner 负责跨图保活与复用等待。args buffer 不是普通“参数数组”，它的布局和 usage 要符合 RHI 后端要求，而且即便不作为普通 shader 参数绑定，也必须通过显式 graph access 被 RDG 看见。

用一个局部案例承载这条生命线：Pass A compact 可见任务并写 count，Pass B 把 count 转成完整 X/Y/Z args，Pass C 读取 args 间接处理任务，Pass D 消费输出。若 Pass C 不跑、跑太多或跑太少，先查初始化、producer 写入、offset/容量、实际 X/Y/Z 和 RDG transition；若准备复用 buffer，再查最后 consumer 的 completion fence。小而稳定、CPU 已知的 depth copy 使用 direct 更简单，不应为了“GPU-driven”标签额外引入 args 生成、同步和调试成本。

这里的 last-valid-state 必须精确：producer 命令已录制不等于 args 内容已生成；transition 已规划不等于 GPU 已越过它；`DispatchIndirect` 已录制不等于 queue 已提交；只有目标 payload 的 fence 完成后，才能写“该次 indirect 工作 GPU complete”。

---

## 7. 第七层：SRV/UAV transition 是 RDG 合同，不是 shader 里的注释

compute pass 常见的资源关系是：读 SRV，写 UAV，后续 pass 再读这个 UAV 的结果。现代显式 API 要求这些访问之间有正确的资源状态转换和同步。UE 不希望每个 pass 作者手写底层 transition，于是把这份责任**上移给 RDG**。

RDG 能自动插 barrier 的前提，是 pass 参数结构准确表达了资源访问。对 depth copy 来说，输入 depth 被声明为只读 texture，输出被声明为 RDG UAV。RDG 编译时看到“前面某个 pass 生产 depth，本 pass 以 shader read 使用；本 pass 写 output，后续 pass 可能读 output”，于是能建立依赖并生成 transition。

沿着 depth copy 建一个资源状态账本：

```text
上游:
  SceneDepthTexture 被生产出来，处在某种可转向 shader read 的状态

depth copy pass:
  参数结构声明 SceneDepthTexture 是只读输入
  参数结构声明 DestinationDepthTexture 的 UAV 是写入输出
  RDG 把本 pass 记成 output 的 producer

下游:
  如果后续 pass 采样 output，参数结构要把它声明成读者
  RDG 才能在写后读之间插入 transition / UAV barrier
```

这里的所有权很清楚：pass 作者拥有“声明准确”的责任；RDG 拥有“根据整张图安排状态转换”的责任；RHI/后端只执行 RDG 交给它的 transition 和 dispatch。pass lambda 里直接拿底层 RHI resource 写，或者把 output UAV 藏在参数结构之外，都等于把资源状态变化从 RDG 账本里偷走。

### UAV barrier 的特殊性

UAV 有一个特殊点：即使前后访问状态看起来一样，也可能仍需要 UAV barrier。原因是 unordered write 的可见性不能只靠“状态没变”来判断——如果两个 pass 都以 UAV 访问同一资源，后一个 pass 可能仍需要等待前一个 pass 的写入完成。UE 的 RDG 状态比较会把 UAV barrier 当成特殊情况处理，除非明确使用了允许跳过 barrier 的机制。

`SkipBarrier` 表达的是一组受约束的连续 UAV overlap 意图，不是“这张资源以后都不用同步”。RDG 还会比较资源 / subresource 状态与 no-barrier view handle；前后访问必须落在可匹配的 no-barrier 集合中。中间插入普通 UAV view、SRV read、不同 handle，或者访问离开这段连续 overlap，都会重新建立同步边界。跨 graphics / async compute pipe 时，producer-consumer 依赖、queue ownership、fork/join fence 和资源寿命仍然独立成立，skip UAV barrier 不能替代跨 pipe 同步。

作者只有在能列出整段访问闭包时才应使用它：哪些 pass 连续访问同一 UAV、彼此是否允许 unordered overlap、何处恢复普通 barrier、是否跨 pipe、最终谁读取结果。无法证明这些条件时，默认 barrier 的成本换来明确可见性，更适合正确性优先路径。

### 这层模型能解释的常见 bug

| 写法 | 后果 |
| --- | --- |
| lambda 捕获了 texture，但参数结构没声明 | RDG 不知道资源访问，无法插正确 barrier |
| 直接拿底层 RHI resource 绕过 RDG | 图编译器看不见这次读写，生命周期和状态可能错 |
| 把 UAV 写误声明成 SRV 读 | 后续依赖方向和 barrier 都可能不对 |
| 为了性能跳过 UAV barrier | 必须证明同一资源 / subresource 的连续 no-barrier handle 闭包，并保留 graph dependency 与跨 pipe 同步 |

RHI 层只提供 transition 原语；RDG 层负责根据整张图决定 barrier 放在什么位置。**对 compute shader 作者来说，最重要的工作不是手写 barrier，而是把资源访问声明准确。**

当 output 看起来“写了但下游读旧值”时，按这条资源账本拆问：

```text
写入者是否真的用 RDG UAV 声明了 output?
下游读者是否真的用 RDG SRV / texture 参数声明了同一资源?
中间是否把底层 RHI resource 绕过 RDG 直接传走?
是否使用了跳过 UAV barrier / overlap 之类优化，而读写之间仍有可见性需求?
```

这比只问“shader 有没有写 RWTexture”更可靠，因为 shader 代码只表达局部写入；跨 pass 可见性要靠 RDG 的资源状态合同来完成。

一个最小对照是同一 append buffer 的连续 UAV 写：若两次写被算法证明可以 overlap，并使用同一受约束 no-barrier view，可组成一个 overlap 区间；一旦下一步要以 SRV 读取 compact 结果，就必须结束 overlap 并建立写后读可见性。depth copy 的目标 UAV 随后被下游采样，属于普通写后读，不应套用 skip 作为默认优化。RDG 生成了 transition 只能证明同步进入命令计划；直到对应 queue fence 完成，才能证明 GPU 已越过该同步点。

---

## 8. 第八层：async compute 是队列资格，不是并行承诺

`ERDGPassFlags::AsyncCompute` 最容易被误读成“这个 pass 一定会和 graphics 并行”。它真正表达的是：这个 pass **有资格**被 RDG 放到 async compute pipe 上执行。是否真的重叠，还取决于平台支持、全局 CVar、调试模式、资源依赖、fork/join 区间和后续消费者。

在当前路径中，`IsAsyncComputeSupported` 的全局资格谓词需要同时满足：RDG async compute 配置大于 0；graph 不处于 immediate mode；当前 shader platform 没启用 render-pass merge；RHI 报告高效 async compute；RHI 支持独立 depth/stencil copy access；`GTriggerGPUProfile` 未触发。任一条件不成立，作者写下的 `AsyncCompute` flag 仍保留意图，但 graph 不会按正常 async 资格调度。调试工具还有各自独立的影响：例如 DumpGPU 会关闭 parallel RDG execute，GPU profiling 会改变 scope / event 收集。它们不应被追加成 `IsAsyncComputeSupported` 中并不存在的条件；同时，profile 结果只代表该调试形态，判断正常帧是否重叠仍要回到非 profile 复现。

RDG 要为 async compute 解决三件事：

1. **确定 fork。** async pass 读取的资源必须等 graphics 生产完成后才能开始。如果输入刚由 graphics pass 写完，RDG 要在 graphics 管线的某个位置放行 async compute。
2. **确定 join。** graphics 后续如果要读 async compute 的输出，必须在 join 点等待 async compute 完成。这个等待不是普通线性 pass 顺序能表达的，因为它跨了两条 GPU pipeline。
3. **扩展资源生命周期。** 一个资源在 graph 的线性 pass index 上看似已经没人引用，不代表 async queue 也用完了。只要它处在 fork/join 覆盖的区间内，就不能被释放或拿去做 transient alias。

因此 async compute 的正确心智模型是一条带分叉的流：

```text
compute pass 标记为 AsyncCompute
  -> RDG 判断平台和运行时是否允许
  -> 根据资源依赖计算 graphics fork / graphics join
  -> 对跨管线 transition 使用 fence
  -> 把相关资源生命周期扩展到 async 区间
  -> 执行时可能重叠，也可能因依赖太紧而基本不重叠
```

### 调试“没重叠”

调试 async 不重叠时，不要只盯 pass flag。先查：输入是否刚由 graphics 生产，输出是否马上被 graphics 消费，debug flush 是否关闭了 async/parallel，平台是否支持 async compute，RDG 是否因为资源 lifetime 或 transient aliasing 扩大了同步范围。很多情况下，flag 是对的，但依赖区间太短，根本没有可重叠的空间。

把 depth copy 想象成 async pass，更能看清这条边界。它读的 depth 通常来自 graphics 侧已经完成的深度工作，写出的 copy 如果马上又被 graphics 侧消费，RDG 就必须把 fork 放在输入安全之后，把 join 放在输出被读之前。这个区间如果很短，Profiler 里看不到明显重叠并不奇怪；它说明依赖图没有给 async 留出空间，而不是说明 `AsyncCompute` flag “失效”。

调试 async 边界时，优先把问题说成三个状态：

```text
queue eligibility:
  pass flag 与全局 async 谓词是否同时成立?

dependency interval:
  它的 graphics fork 和 graphics join 中间有没有可重叠工作?

resource lifetime:
  它读写的资源是否因为 async 区间被延长，影响 transient alias 或后续 barrier?

actual overlap:
  两条 queue 的时间戳是否真的重叠，而不只是 pass 出现在 async timeline?
```

这四个状态分别属于 pass 声明、全局资格、依赖图 / 资源生命周期和后端执行。把它们混成一句“async 没并行”，就会失去排查入口。pass 出现在 async timeline 只证明被调度到该 pipe；queue 时间戳重叠才证明实际 overlap；对应 queue fence 到达才证明完成。

graphics compute 是更简单的替代方案：它减少跨 queue fence、ownership 和寿命压力。只有当可覆盖的 graphics 工作足够长，隐藏的时延大于同步和调度成本时，async 才有收益。紧邻 graphics producer 与 consumer 的短 depth copy 可以具备资格却几乎没有重叠窗口；另一个独立的长计算若在 fork/join 之间有充分 graphics 工作，才适合验证“eligible -> scheduled -> overlapped”三层证据。

---

## 9. 第九层：执行阶段把 RDG pass 变成 RHI compute 命令

RDG 编译完成、进入执行阶段后，前面登记的 lambda 才真正被调用。compute helper 的执行可以理解成四步：

1. **诊断 group count** —— 对每个维度运行上界 `ensure`；调用方仍必须提前保证平台合法性。
2. **准备 dispatch** —— 取 `FRHIComputeShader`，设置 compute pipeline state，绑定 shader 参数。
3. **发 dispatch** —— `DispatchComputeShader` 或 `DispatchIndirectComputeShader`。
4. **收尾** —— 需要时解绑 shader UAV。

### compute PSO 也是 PSO

compute PSO 不像 graphics PSO 那样包含 render target 格式、blend、depth/stencil 等大量状态，但它仍然是 PSO。首次使用某个 compute shader 时，RHI 可能需要查缓存、创建或等待后端 compute pipeline state。调试 shader 第一次执行卡顿时，不要只看 graphics PSO——compute PSO 同样有创建和等待成本。

要把四个状态分开：有效 shader ref 只是 cache key 的核心输入；cache entry 可以先存在；native compute pipeline 可在异步任务或 precache 中创建；command list 真正设置 PSO 时必须消费 ready 的 native pipeline。cache miss 发生在关键帧时，路径可能同步创建或等待 completion event，形成 cold-run hitch；warm run 命中不能证明预热覆盖了所有平台和 permutation。

```text
shader ref 有效
  -> 查找或创建 compute PSO cache entry
  -> 异步创建 / precache 完成，native PSO ready
  -> SetComputePipelineState 被录入
  -> dispatch 被录入
```

Pipeline State Cache 拥有 cache entry 与完成事件，RHI 后端拥有 native pipeline，command list 在设置阶段消费它。同步创建的优点是状态简单，代价是 miss 直接落到当前帧；异步创建与 precache 把成本移出关键帧，但要求预先覆盖真实 shader permutation，并处理首次使用早于 ready 的等待。对 depth copy 应分别记录 cold run 与 warm run 的 PSO 状态；“cache hit”最多证明 entry 可复用，不能外推到 dispatch 已提交或 GPU 已完成。

### 参数在这一步变成后端可消费的绑定

`SetShaderParameters` 用的是第 21 篇讲过的 shader parameter binding。到 RHI 层时，参数不再是“某个 C++ 字段名”，而是 loose constants、uniform buffer、SRV/UAV、sampler、bindless handle 等后端可消费的绑定集合。对于 RDG resource，执行阶段还要把 graph resource/view 解析成真实的 RHI resource/view。

`UnsetShaderUAVs` 也不是可有可无的礼貌动作。某些 RHI 或验证路径需要在 dispatch 后清理 UAV 绑定，避免后续 shader 仍看到旧的 UAV 绑定状态。UE 把这件事放进 compute helper，减少每个 pass 作者手写时的遗漏。

---

## 10. 第十层：RHI command list 记录平台无关命令

`FRHIComputeCommandList` 位于 RHI command list 的能力层级中：

```text
FRHICommandListBase
  -> FRHIComputeCommandList        （compute PSO、参数、dispatch、transition）
      -> FRHICommandList            （再增加 draw、render pass、graphics PSO）
          -> FRHICommandListImmediate
```

这个层级表达可录制的命令能力。graphics pipe 可以录制 compute；纯 compute list 不具备 draw 能力。它不是“某条 GPU queue 已经拥有这段工作”的证明，真正的 pipe、translation 和 queue submit 还在后面。

### bypass 改变中间路径，不改变完成语义

helper 调用 `DispatchComputeShader` 时有两类 CPU 路径：

```text
bypass:
  直接把调用转给当前 RHI compute context

recorded:
  在线性 RHI command list 中保存 command node
  后续由 RHI 执行 / translation 阶段回放到 context
```

间接 dispatch 的 command node 还保存 argument buffer 与 offset。recorded 路径支持 RHI tasks、parallel translate、profiling、validation 和多后端；bypass 减少中间节点，适合缩小调试边界，但会改变线程、batching 和 race 的表现。两条路径最终都仍需把平台命令编码进 native command list，再 finalize、submit 并等待 GPU completion。因而“bypass 已调用后端”仍不等于 queue submitted。

这一层的 owner 是 RHI command list；它持有平台无关 command node 及其资源引用，直到 translation 消费。此时最深证据是 **RHI recorded**。如果断点只证明 execute lambda 进入、PSO 设置和 dispatch node 已生成，故障报告必须停在这里。

---

## 11. 第十一层：原生命令编码、queue submit 与 GPU complete

到后端时，dispatch 才被编码为具体图形 API 命令。以 D3D12 为例，直接 dispatch 先提交 pending descriptor、常量、资源表和 compute pipeline state，再调用原生 `Dispatch(groupX, groupY, groupZ)`；indirect 路径准备同类 compute state、验证 argument buffer 状态并调用 `ExecuteIndirect`。

关键边界是：原生 `Dispatch` / `ExecuteIndirect` 仍只是在 **native command list 中编码命令**，不是整条链末端。编码之后至少还有这些控制转移：

```text
RHI command node recorded
  -> translation 生成 platform command list
  -> native command list close / finalize
  -> finalized lists 汇入 submission payload
  -> platform queue 接收 command lists
  -> payload fence 被 signal
  -> GPU 消费该 payload
  -> completed fence value 到达目标值
  -> 下游 consumer / readback 验证输出语义
```

这些状态由不同 owner 推进：

| 状态 | owner / 数据 | 最深能证明什么 |
| --- | --- | --- |
| RHI recorded | RHI command list 持有 command node 与资源引用 | 平台无关命令已记录 |
| translated / finalized | translation task 生成并封闭 native command list | 原生命令已编码，可进入提交 payload |
| queue submitted | backend queue 接收 command lists，payload 关联 submission fence | 驱动队列已接收工作 |
| GPU complete | queue 的有效 completed fence 达到目标 payload value，且无 device-removed 失败 | 该 queue payload 已完成 |
| output verified | 指定下游 consumer、readback 或 capture 验证目标资源 | 本次输出在该观察条件下语义正确 |

payload 需要持有 command allocator、command list 和资源引用直到 completion；过早释放或复用会让 CPU 生命周期领先 GPU。parallel translation 与 submission thread 可以提高 CPU 吞吐，但会扩大“已记录”和“已提交”之间的并发窗口；bypass 可以缩短窗口，却不能删除 queue submit 与 fence completion 这两个语义层。

若在 capture 或后端断点看到原生 dispatch，只能写 **native command recorded**。看到 queue API 接收 command lists，才能写 **Submitted**。看到 marker entered / exited 只能说明 GPU 到达或离开 marker 边界，不能替代整个 payload 的 fence。只有 completed fence 达到目标值且没有 device-removed 失败，才能写 **GPU complete**；即便如此，shader 算错、写错资源或下游覆盖仍需 output verified 才能闭环。

### Depth copy 从声明走到验证

把主案例完整跑一次，能看到每个阶段的数据、owner 和 last-valid-state：

```text
1. shader map 提供 FViewDepthCopyCS 的目标 permutation
   last-valid-state: shader permutation present

2. graph allocator 持有参数，输入 depth、输出 UAV、view uniform 被声明
   last-valid-state: resource contract declared

3. AddPass 创建候选节点，输出被真实下游消费，因此编译后保留
   last-valid-state: pass retained

4. view rect 与 numthreads 共同生成合法 direct group count
   last-valid-state: dispatch shape validated

5. RDG 规划 depth read、目标 UAV write 与下游 write-to-read transition
   last-valid-state: synchronization planned

6. execute lambda 进入，compute PSO ready，参数绑定，RHI dispatch node 生成
   last-valid-state: RHI recorded

7. translation 把节点编码进 native list，并 close / finalize
   last-valid-state: native command recorded and finalized

8. platform queue 接收 payload 并 signal submission fence
   last-valid-state: queue submitted

9. completed fence 达到该 payload 的目标值
   last-valid-state: GPU complete

10. 下游读取同一 depth-copy resource，或 readback / capture 验证目标区域
    last-valid-state: output verified

11. 最后 consumer 对应的 GPU 工作完成后，资源才进入可退休 / 可复用边界
    last-valid-state: lifetime closed for this use
```

这条主线只承担 direct compute。indirect args、无消费者裁剪 / `NeverCull`、连续 UAV overlap、长 async 区间各自由前面的局部案例承担，避免把一个简单 depth copy 人为改造成所有机制的集合。

---

## 12. 按症状反推层级

compute shader 出问题时，最有效的排查方式不是先改 HLSL，而是把症状落回本章主线的某一层。下面这张表把“看到的现象”映射回“应该先查的层”：

| 症状 | 优先检查的层 |
| --- | --- |
| shader 找不到、运行时报缺 shader | 第 1/3 层：shader type 注册、frequency、platform gating、permutation |
| RDG validation 报资源生命周期或 pass flag 错 | 第 2/4 层：资源 manifest、lambda 捕获寿命、pass flags |
| `AddPass` 命中但 pass 消失 | 第 4 层：真实 consumer、cull root、external output、副作用与 `NeverCull` |
| 输出缺边、尺寸不对、偶发越界 | 第 5 层：数据域、group size、bounds check、wrapped Z 上限与分块 |
| 写了 UAV 后读到旧值 | 第 7 层：manifest、transition、UAV barrier、no-barrier handle、跨 pipe 同步 |
| indirect dispatch 不跑或数量异常 | 第 6 层：args 创建 / 初始化 / 写入、offset、transition、最后 consumer |
| async compute 没有重叠 | 第 8 层：完整资格谓词、fork/join、实际 queue 时间戳、ProfileGPU 形态 |
| 首次 dispatch 卡顿 | 第 9 层：compute PSO miss、precache、completion event、native PSO ready |
| 后端看到 Dispatch 但输出仍未完成 | 第 10/11 层：native list finalize、translation、queue submit、completion fence、下游验证 |

把 depth copy 从头到尾串起来看，完整的调试线就是把主路径地图倒着走一遍：

```text
FViewDepthCopyCS 是否有正确的 compute shader identity
  -> 参数是否声明了 depth 输入、UAV 输出和 view uniform
  -> permutation 与 THREADGROUP_SIZE 是否一致
  -> AddPass 是否声明为 compute pass
  -> group count 是否覆盖 view rect
  -> RDG 是否看到输入读、输出 UAV 写，并保留该 pass
  -> 执行阶段 compute PSO 是否 ready，参数是否绑定
  -> RHI 是否录制了 DispatchComputeShader
  -> 原生 command list 是否编码并 finalized
  -> platform queue 是否 submitted
  -> completion fence 是否到达
  -> 下游 consumer 是否读取同一 output 并验证内容
```

如果具体症状是“目标 depth copy texture 没有内容”，可以把路线收束为八个证据状态：

```text
1. 声明是否成立:
   CopyViewDepthCS 及其资源是否 declared?

2. 保留是否成立:
   pass 是否因真实 consumer / output 在 compile 后 retained?

3. 调度形状是否有效:
   group count 是否由正确 view rect 算出，HLSL group size define 是否一致?

4. RHI 录制是否成立:
   native PSO 是否 ready，RWDepthTexture 是否绑定，平台无关的 RHI dispatch node 是否 recorded?

5. 平台命令是否形成:
   translation 是否把 RHI node 编码进 native command list，并完成 close / finalize?

6. 提交是否成立:
   目标 queue 是否接收 payload 并 signal submission fence?

7. GPU 是否完成:
   有效 completed fence 是否达到目标值，且没有 device-removed 失败?

8. 结果是否验证:
   后续是否读取同一 RDG resource，写后读同步是否成立，目标区域内容是否符合语义?
```

这些门分别对应 **Declared、Retained、shape valid、RHI recorded、Platform commands formed、Submitted、GPU complete、Output verified**。每个断点和工具输出只能推进到有直接证据的一项；无法证明更深状态时，last-valid-state 就停在前一项。

| 完成深度 | 允许写入报告的结论 | 不得外推 |
| --- | --- | --- |
| Declared | pass / resource / command 已声明 | 未裁剪、已录制 |
| Retained | RDG compile 后未裁剪 | lambda 已执行 |
| RHI recorded | 平台无关的 RHI command node 已进入 RHI command list | 后端已翻译或形成 native command list |
| Platform commands formed | translation 已把 RHI 记录编码进 native command list，并完成本次提交所需的 close / finalize | platform queue 已接收 payload |
| Submitted | platform queue 已接收 command lists / payload fence 已 signal | GPU 已完成 |
| GPU complete | 有效 completed fence 到达目标 payload，且无 device-removed 失败 | 输出语义正确 |
| Output verified | 指定 consumer / readback / capture 验证结果 | 其他 profile、capture 或构建形态同样正确 |

每一步都问同一个问题：**当前状态由谁拥有，下一步需要什么声明才能安全交接。** compute shader 不是“一句 dispatch”，而是一串跨 shader system、RDG、RHI 和后端的合同。

---

## 小结

UE compute shader 的核心模型可以压成一句话：

> **C++ shader type 与 permutation 确定 compute 代码，`FParameters` 同时声明 shader 绑定和 RDG 资源访问，数据域与 group size 形成 direct group count 或 GPU 生成的 indirect args，`AddPass` 把工作登记为候选节点，RDG compile 决定保留、资源寿命、barrier 与 async fork/join，执行阶段在 compute PSO ready 后录制 RHI dispatch，后端再编码并 finalize platform command list、提交 queue；completion fence、下游 consumer 和最后消费者之后的退休边界，分别闭环 GPU complete、output verified 与资源复用安全。**

这也是它和 Unity `Dispatch(kernel, groups)` 最大的差异。下面这张表把两边对齐，帮你把已有的 Unity 直觉迁移过来，同时看清 UE 把哪些责任拆开了：

| 责任 | Unity 公开层 | UE 对应层 |
| --- | --- | --- |
| 找到要执行的 shader | kernel 名查找 | C++ shader type + permutation（第 1/3 层） |
| 绑定资源 | `SetTexture` / `SetBuffer` | `FParameters`，同时充当 RDG manifest（第 2 层） |
| 资源状态同步 | 公开层基本隐藏 | RDG 推导 transition / UAV barrier（第 7 层） |
| 选择 dispatch 形状 | `Dispatch(kernel, groups)` | 数据域 → group size → group count（第 5/6 层） |
| 队列 / 并行 | 较少显式暴露 | `AsyncCompute` flag + RDG fork/join（第 8 层） |
| 提交与完成 | 公开层较少暴露中间深度 | RHI recorded → native finalized → queue submitted → fence complete（第 9/10/11 层） |

Unity 把 shader 查找、资源绑定、状态同步和命令提交压成一个较短的接口；UE 则把这些责任拆到多个层级，正是为了让大型 renderer 能独立调试 shader missing、RDG lifetime、UAV visibility、async overlap、indirect args 和 RHI 后端问题。

下一篇第 23 章会从工具侧接上：本章给的是概念排查路线，第 23 章会把 RenderDoc、Unreal Insights、RDG/CVar 调试和 GPU profiler 串成更具体的定位方法。
