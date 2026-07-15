# 23 调试工具与方法：从症状回溯到 UE 渲染层级

> **源码版本**: UE5.7  
> **前置阅读**: `03_ThreadModel.md`、`04_RHI.md`、`05_RenderGraph.md`、`08_FrameInit.md`、`10_BasePass.md`、`15_PostProcessing.md`、`21_ShaderSystem.md`、`22_ComputeShader.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `23_Debugging_CoverageMatrix.md`

---

## 开篇：错的不是工具，是层级判断

渲染调试最容易走偏的第一步，是先问“我该打开 RenderDoc、Insights、GPU Visualizer，还是先改一个 CVar”。这个问法把工具当成入口，却跳过了一个更前置的判断：**这个故障到底属于哪一层？**

工具只能证明它能看见的那一层。RenderDoc 看 capture 边界内记录的 native command 和资源快照，Insights 看 CPU 线程和任务，GPU Visualizer 看 profile 形态收集到的 scope 树。如果层级判断错了，抓到再漂亮的一帧也只是把错误放大——你会盯着一张正确的中间纹理，却怎么也想不通屏幕为什么还是黑的。

所以本篇的核心问题不是工具清单，而是一条回溯方法：

> **当画面黑了、某张中间纹理不对、GPU hang、DebugView 异常时，如何把症状回溯到 UE 的渲染层级，再选择能证明这一层的工具？**

### 贯穿案例

全章用同一个具体场景：你在 Lighting 之后、后处理之前加了一个 RDG pass，把一张自定义中间纹理合成回 `SceneColor`（`SceneColor` 是 UE 用来累积当前画面颜色的渲染目标）。这是 TA 在做自定义效果时最常见的改动。结果可能有四种表现：

- 屏幕黑了，最终输出没有到 `ViewFamilyTexture`（`ViewFamilyTexture` 是一个视图族最终对外输出的图像）。
- RenderDoc 里中间纹理是全 0、NaN、上一帧旧内容，或者根本找不到。
- `AddPass` 明明执行到了 C++，GPU capture 里却没有对应 pass。
- 开启 capture、`ProfileGPU` 或某个 RDG debug CVar 后，问题消失或位置改变。

这四种表现看起来都像“工具要怎么用”，其实都在问同一件事：**这个症状现在归哪个层级拥有？** 整篇文章就是教你先回答这个问题，再让工具上场。

### 一张地图：按症状选择最高信息量入口

层级是一张路由图，不是一条所有故障都必须从上到下执行的 checklist。入口由症状决定；进入某个节点后，再沿 producer-consumer 或 GT -> RT -> RHI -> GPU 的控制链向上游、下游扩展。

```text
黑屏 / 最终颜色错误
  -> 输出层 -> Pass 层 -> 资源层 -> 时间线 / 特性分支

旧值 / NaN / 未初始化区域
  -> 资源层 -> producer / consumer -> barrier / lifetime -> Pass 层

AddPass 命中但 pass 消失
  -> Pass 层 -> culling root / output / side effect -> 资源依赖

GPU hang / device removed
  -> 先保全 crash evidence -> CPU wait / queue submit / breadcrumb / fence completion

DebugView shader missing
  -> 特性分支 -> shader / permutation -> ODSC / shader map completion

CVar 设置无效
  -> CVar 生效链 -> set-by / thread shadow / sink / consumer -> 目标帧
```

黑屏默认适合从最终输出向上游回溯；GPU hang 若先做普通 capture，可能丢失更高价值的 crash dump 或 breadcrumb；CVar 无效若先查 SceneColor，也绕开了真正 owner。工具仍不是入口，**症状路由到的状态 owner** 才是入口。每条路线的产物都应是“最后确认状态 + 下一条可证伪假设”，而不是“已经走完第 N 层”。

## 本篇边界

本篇只讲**调试路径**，不讲工具操作手册。RenderDoc/PIX 的按钮、Insights 前端面板、具体平台 GPU crash dump 解析、Nanite/Lumen/VSM/TSR 算法细节、性能优化策略，都不在本篇展开（性能归属第 24 篇）。

本篇要讲透的，是路由图各节点能用什么证据证明，以及 Capture 与 CVar 这两条横向链怎样改变观察条件：

| 层级 | 本篇要建立的判断 | 典型证据 |
| --- | --- | --- |
| 输出层 | 最终图像是在哪个阶段丢的，是否写到了 view family 的外部输出 | 后处理最后有效 pass、`SceneColor`、`ViewFamilyTexture` |
| Pass 层 | 负责生产结果的 RDG / renderer pass 是否存在、是否执行、是否被裁剪 | RDG event、GPU scope、pass culling |
| 资源层 | 写者和读者是不是同一张资源，生命周期、alias、barrier 是否正确 | RDG 参数结构、clobber、lifetime、transition、RenderDoc resource |
| 时间线层 | 改动从 GT 到 RT、RHI、GPU 走到哪一步，工具是否改变了执行形态 | Insights trace、render command、RHI submit、ProfileGPU / flush |
| 特性 / Shader 层 | DebugView、Visualize、permutation、show flag 是否把管线改成另一条路径 | DebugView pass、Visualize path、shader map / permutation |
| Capture 边界 | 哪段 RHI / native command 被捕获，捕获是否改变提交形态 | next-frame / immediate / RDG-local capture |
| CVar 生效链 | 设置是否被接受、传播并被目标 consumer 使用 | flags、`LastSetBy`、RT shadow、sink/cache、目标帧 |

## 本篇必须能回答

读完本篇，你应该能独立回答这几个问题。它们就是后面各节的落点：

- 为什么黑屏常从输出层开始，而 GPU hang、shader missing、CVar 无效应走高信息量短路入口？
- RDG debug 能证明什么，不能证明什么？
- RenderDoc 为什么适合证明 GPU 命令和资源内容，却不能证明 GT 当前的变量值？
- Insights、GPU Visualizer、breadcrumb 和 queue fence 分别看哪种完成深度，为什么 profile 本身会改变调试条件？
- CVar 为什么不能只记录“我设了 X”，而要记录 flags、`LastSetBy`、线程 shadow、sink/cache、consumer 与 rebuild/restart 条件？
- DebugView / Visualize 为什么不是最后叠一张 UI，而是可能替换或改写整条渲染管线？
- 一次可归因实验为什么必须包含复现基线、单变量改动、基线恢复、last-valid-state 和 completion depth？

---

## 1. 先把“错误画面”改写成层级问题

为什么第一步不是开工具，而是路由？因为同一个可见症状可由不同 owner 产生，而不同症状又携带不同的信息量。黑屏只告诉你最终输出错误，适合从颜色流向上游收缩；device removed 已经直接指向 GPU / driver 完成链，此时应先保全 crash evidence；shader missing 已经指向 shader availability；CVar 无效首先是 ConsoleManager 到 consumer 的传播问题。

| 症状签名 | 首选入口 | 先证明 | 再扩展 |
| --- | --- | --- | --- |
| 黑屏、最终颜色错误 | 输出层 | 最后谁写 `ViewFamilyTexture`，当前 `SceneColor` 是否进入该链 | Pass -> resource -> timeline / feature |
| 中间纹理旧值、NaN、未覆盖区域 | 资源层 | writer / reader 是否同一资源，首次有效 producer 在哪里 | barrier / lifetime -> pass retention |
| `AddPass` 命中但 capture 无 pass | Pass 层 | Declared 是否变成 Retained / Recorded | culling root、output、side effect、event 可见条件 |
| GPU hang、TDR、device removed | crash / timeline | CPU 是否在等、queue 是否 submitted、marker 与 fence 到哪一步 | breadcrumb / DRED / vendor dump / device reason |
| DebugView shader missing | feature / shader | 是否进入分支、目标 permutation 是否 available | ODSC / cook、fallback、普通路径对照 |
| CVar 设置后无效 | CVar 生效链 | set 是否被接受、`LastSetBy`、consumer 实际读值 | thread shadow、sink/cache、restart / rebuild |

“从外往里、从结果往原因走”仍是黑屏的有效默认策略，但它不是 GPU hang 或 CVar 无效的硬规则。路由之后要沿真实状态链扩展，而不是机械地进入下一层。

### Unity 经验在这里要换一套心智

这套顺序和 Unity SRP 经验差异很大，值得专门说清，否则你会带着错误直觉去抓帧。

> **Unity 直觉**：使用 Frame Debugger 和 `CommandBuffer` 时，读者常按记录顺序追问“这一步有没有画”，并把记录、提交与完成压成一条较短的心智链。
>
> **UE 的不同**：RDG 先声明整张帧图，再编译、裁剪、分配资源、插 barrier、最后才执行。`AddPass` 出现在 C++ 里，只说明它成为了一个**候选节点**，不说明节点已保留、命令已录制、queue 已提交或 GPU 已完成。

所以 Unity 那种“记录即执行”的顺序直觉，在 UE 里必须被“帧图编译器”的心智模型替换。这一点会贯穿后面的 Pass 层和资源层。

### 落到贯穿案例

贯穿案例里的第一个动作，不是打开 RenderDoc，而是把症状写成一串可证伪的问题：

```text
这张错误图最终应该由谁输出?
负责输出的 pass 是否进入最终执行计划?
该 pass 声明的输入和输出是否被 RDG 看见?
写出的资源是否被后续 pass 消费到最终输出?
我观察到的帧，是改动已经进入 RT/RHI/GPU 之后的帧吗?
```

只有这些问题落到了具体层级，工具才有意义。进入输出层之前，先把这种问题写成一条可复用的证据链。

### 证据链写法：工具输出必须翻译成引擎状态

03 篇的调试标准是“数据现在在哪里、由谁拥有、下一步交给谁”。本篇也一样，只是对象从 render command 换成了**一帧错误画面的证据**。工具输出不能原样当结论，必须先翻译成 UE 的状态变化。

以贯穿案例的黑屏为例，一份有用的记录应该长这样：

```text
现象:
  最终画面黑，但自定义合成 pass 的 C++ 断点命中了。

期望状态:
  自定义纹理被写回当前 SceneColor，
  后处理最后有效 pass 继续把当前颜色写到 ViewFamilyTexture。

证据 A - GPU scope:
  ProfileGPU / GPU Visualizer 里有没有 CompositeCustomTextureIntoSceneColor?
  有 -> 当前 profile 形态观察到该 scope；继续记录 timestamp / marker 的完成深度并查资源。
  没有 -> 先检查 event 可见条件，再查 RDG 声明、裁剪和输出链。

证据 B - RenderDoc resource:
  合成 pass 后写出的 texture 是否就是后续后处理读取的 SceneColor?
  是 -> 资源身份暂时成立，下一步查后续覆盖或输出层。
  否 -> 这是资源层断链，下一步修正读写同一资源或 external/extracted 输出。

证据 C - RDG validation / clobber:
  是否报告未声明访问、生命周期或写后读问题?
  有 -> RDG 没有理解真实依赖，先修参数结构和 access。
  无 -> 继续用 capture 比对 writer / reader，而不是断言资源一定正确。

证据 D - Insights:
  GT 的设置、render command、RT 构图是否发生在被抓这一帧之前?
  否 -> 这是时间线问题，下一步查命令投递和帧边界。

证据 E - CVar 对照:
  分别单独开启 ProfileGPU、RDG flush 或禁用 parallel，症状是否改变?
  改变 -> 恢复 baseline 后复验；它只是 async / parallel / transient / submit 的形态线索。
```

这个例子里，`ProfileGPU`、RenderDoc、RDG validation、Insights 和 CVar 都没有单独给出“根因”。它们各自只把症状推进到一个更具体的引擎状态：Pass 是否存在、资源是否同一张、依赖是否被 RDG 看见、改动是否进入目标帧、观测是否改变执行形态。调试的下一步来自这些状态，而不是来自工具名本身。

有了这种写法，下面再从最外层——输出层——开始。

## 2. 输出层：先确认颜色流是否走到终点

**这一层要回答的问题**：最终图像有没有真的被写出去？

第 15 篇已经建立过一条关键合约：UE 后处理没有固定的 Final Blit。`SceneColor` 沿一条**动态的 pass 序列**被反复重写，只有最后一个有效 pass 才拿到写入 `ViewFamilyTexture` 的权利。换句话说，“谁最后写 `ViewFamilyTexture`，谁才拥有最终画面”——这不是某个固定函数，而是一条会随设置变化的动态链。

所以黑屏的第一问不是“我的 shader 算错了吗”，而是颜色流本身是否走到了终点：

```text
当前 SceneColor 在哪一段颜色流里?
它有没有被 resolve 成后处理可读的输入?
后处理 pass sequence 的最后有效 pass 是谁?
最后有效 pass 的输出是不是 external ViewFamilyTexture?
DebugView / Visualize 是否改走了另一条最终输出路径?
```

**如果输出层断了会怎样**：你在 RenderDoc 里可能看到你的合成 pass，也看到中间纹理完全正确，但屏幕仍然黑。因为你的结果没有进入后处理的当前颜色流，或者被后面的 Visualize / debug path 替换了。这正是“资源对了画面还黑”的典型成因。

**这一层用什么证据**：通常不是某个 shader 断点，而是后处理链路里的 pass 名、当前颜色资源、最终 external output。RenderDoc 可以沿 capture 中的 resource writer / reader 追踪外部输出，GPU Visualizer 只能辅助定位 scope 组织；两者都不能替你判断后处理 pass sequence 的语义——语义要回到第 15 篇的输出合约。

确认了“应该写出的东西确实流向了终点”之后，下一问自然是：负责生产它的那个 pass，真的执行了吗？

## 3. Pass 层：先证明“声明是否变成了执行”

**这一层要回答的问题**：你的 pass 真的进入执行计划了吗？

要理解 Pass 层，先要正面理解 RDG debug 的角色：它不是用来看 GPU 纹理内容的，而是把那个看不见的“帧图编译器”重新变成可观察对象。它主要帮你证明四件事：

- pass 有没有清晰事件名，能不能在 RDG event、GPU scope、capture 中被识别；
- pass 参数结构是否声明了它读写的 RDG 资源；
- pass 是否被 RDG 裁剪掉了；
- 调试开关是否切断了 parallel、async、transient alias、lifetime 等优化，让错误靠近声明点暴露。

下面把这四件事拆开讲。

### Pass 名是有条件的关联键

清晰的 RDG pass 名不是全局唯一 ID，而是在事件确实被生成、转发并由后端工具保留时使用的人工关联键。GPU profiler、capture marker 与 breadcrumb 各自拥有事件生成和过滤条件；build 配置、profile 支持、breadcrumbs、`r.RDG.Events`、当前 dump / profile 状态以及后端 marker 能力都会决定某一列是否可见。

贯穿案例里的 `CompositeCustomTextureIntoSceneColor` 可以帮助关联，但结论要联合更多身份：目标 frame、queue、event 父子树、pass / resource identity、draw 或 dispatch 参数。字符串相同不能证明是同一份资源或同一轮执行，某个工具缺少名字也不能单独证明 pass 未执行。

| 观察位置 | 名字可见的前提 | 看到名字最多证明 |
| --- | --- | --- |
| CPU RDG graph / dump | graph 记录了该 event | pass 被声明或保留到对应 graph 观察阶段 |
| GPU Visualizer / ProfileGPU | profile/event 收集启用且平台支持时间戳 / scope | profiler 观察到该 scope；完成深度取决于时间戳是否闭合 |
| RenderDoc / PIX event | capture 边界包含命令且后端保留 marker | 捕获文件里记录了相应 marker / command |
| GPU breadcrumb | breadcrumb 启用、marker 被编码并由 GPU 更新 in/out | GPU 到达 entered 或 exited 边界 |

因此 pass 名含糊会降低关联效率，但命名清晰也不能替代 queue、resource 与 completion evidence。报告某一列不可见时，要先记录该工具的启用条件，而不是把“没有名字”直接翻译成“没有执行”。

### RDG 只相信参数结构，不相信 lambda 捕获

第 05 篇的规则在调试里最关键：**RDG 从参数结构和显式 access 中推导资源依赖，不从 lambda 捕获里猜读写。**

贯穿案例中最常见的错误就是：lambda 里拿了一个 RDG texture 或 UAV，但参数结构没有声明它是输入或输出。结果是 RDG 看不到真实依赖，于是可能裁掉 pass、漏掉 barrier、缩短资源生命周期，或者把 transient alias 安排到错误位置。

因此 Pass 层排查的第一条路线是逐项检查声明：

```text
pass 有参数结构吗?
输入资源是否声明为 SRV / Texture read?
输出资源是否声明为 UAV / RTV / explicit access?
输出是否被后续 pass 读取，或是否通向 external / extracted / final output?
这个 pass 是否只有副作用，需要 NeverCull?
```

### “AddPass 了却消失”通常是裁剪，不是 bug

为什么 `AddPass` 执行了，capture 里却没有它？因为 RDG 会从外部可观察输出**反推**必要工作。纯图内临时结果如果没有被下游读取，也没有被 extract 到图外，它对最终结果就没有贡献，可以被裁掉。纯副作用 pass 如果没告诉 RDG 自己不可裁剪，也会消失。

所以“C++ 执行到 `AddPass`，GPU capture 里没有这个 pass”不是矛盾，它可能正说明 RDG 编译器判断它没有可观察贡献。

对贯穿案例来说：如果你的合成 pass 写了一张临时纹理，但后处理仍然读旧 `SceneColor`，这个 pass 就没有进入最终输出链。**正确的修复不是强行用工具抓它，而是让资源依赖真实地通向后续消费者或 external output。** 只有确实是调试、capture、marker 这类纯副作用 pass，才应该用 `NeverCull` 显式表达副作用边界。

### RDG CVar 是受控实验，不是普通运行状态

RDG event、culling、validation 与 clobber 回答四个不同问题。把它们统称为“RDG debug 已证明”会越过各自证据上限：

| 机制 | 能证明 | 不能证明 | 启用条件 | 侵入性 |
| --- | --- | --- | --- | --- |
| RDG event / dump | graph 中存在对应声明、依赖或保留节点 | marker 一定进入所有工具、GPU 已完成、资源内容正确 | event 收集、build/profile 与后端支持满足对应工具条件 | 低到中；事件与 dump 仍会增加记录成本 |
| culling | compile 后哪些节点因 consumer、cull root、external output 或副作用被保留 | lambda 已执行、命令已提交 | 正常 graph compile 与准确资源 / 副作用声明 | 属于正常优化；强行改变裁剪会改变待执行图 |
| validation | 参数、access、生命周期和结构合同是否触发已覆盖检查 | shader 算法正确、GPU 内容正确、所有 race 都不存在 | RDG debug 支持构建且 validation 开启 | 中；增加检查并可能改变时序 |
| clobber | 用已知值放大未初始化、旧内容或未覆盖区域的症状 | 依赖链一定正确、clobber 色本身就是根因 | validation 开启、clobber 值非零、当前 auxiliary pass 允许 clobber | 高；插入额外 clear producer，改变 graph、barrier、lifetime 与时序 |

clobber 的 last-valid-state 只能收缩到“真实 producer 没有在该观察区域 / 时点覆盖已知值”的候选范围。它不能单独区分 producer 被裁、dispatch 覆盖不足、写错资源或后续读错资源。若 clobber pass 本身改变了 race 或 alias，症状还会移动。

因此实验必须逐个启用：先在无额外工具的基线复现；只开 validation 并记录；恢复基线；只开 clobber 并记录；再恢复基线。flush、immediate、transition log、profile、capture 同样一次只改变一个变量，因为它们可能关闭或收缩 parallel execute、async compute、submission thread 等路径。部分 RDG debug 能力只存在于非 Shipping / 非 Test 的调试支持构建，Shipping 复现不能假设同一开关可用。

确认了 pass 至少达到 Retained / Recorded 中有证据的一项后，下一问是：它读写的资源，是不是你以为的那一张？

## 4. 资源层：RenderDoc 看内容，RDG 解释生命周期

**这一层要回答的问题**：写者和读者，是同一张资源吗？

到这一层，RenderDoc / PIX 这类 GPU capture 才真正派上用场。它的优势是看 GPU 实际收到的命令、event、draw/dispatch、resource 内容和资源状态，适合回答这些“事实层面”的问题：

```text
这个 pass 前，输入纹理内容是什么?
这个 pass 后，输出纹理是否被写?
后续消费者读的是同一张资源，还是另一张 resolve / extracted / external 资源?
资源状态是否从写入态转成后续读取态?
最终写 ViewFamilyTexture 的 pass 读到的是哪一张颜色?
```

**但 RenderDoc 不知道你的设计意图。** 它能显示 GPU 读了 A，却不会告诉你你本来想读 B。这个判断要靠 RDG 的资源层模型来补——RenderDoc 给“发生了什么”，RDG 给“为什么会这样”：

- 图内临时资源只在当前 RDG 图内有意义，生命周期可被压缩；
- external 资源跨过图边界，通常也是裁剪根；
- extracted 资源执行后要交回图外，它的生产链不能被裁；
- transient alias 可能让两个逻辑资源复用同一段物理内存；
- barrier 来自参数结构中声明的访问，不来自 lambda 内部的任意代码。

贯穿案例里，错误通常发生在三种**资源身份混淆**上：

```text
你以为写的是当前 SceneColor，
实际写的是一张图内临时 texture，后处理没有读它。

你以为读的是 BasePass / Lighting 后的颜色，
实际读的是 resolve 前 target，或读到了旧 external 资源。

你以为 history 还活着，
实际没有 extraction，图执行后资源生命周期已经结束。
```

所以这一层的标准动作是两类证据配合：RenderDoc 证明“GPU 读写了哪张资源”；RDG graph / debug 解释“为什么这张资源会在这个时刻存在、被裁、被 alias 或被转换”。单用任何一个都不够。

## 5. Capture 边界：你抓到的是哪条时间线

上一层用了 capture，但有一个前提常被忽略：**GPU capture 不是一台无条件的全局摄像机。** 在 UE 里，capture 也有线程边界和 RDG 边界。抓错了边界，你会以为“没抓到”，其实是抓到了另一段时间。

可以把 capture 分成三种语义：

| Capture 入口 | 语义 | 调试时的含义 |
| --- | --- | --- |
| 捕获下一整帧 | main thread 请求后续 frame | 适合观察完整主渲染，但请求时点与实际 begin/end 不是同一状态 |
| 包住 immediate RHI command list | render thread 在明确 RHI 边界开始 / 结束 | 适合已有局部 RHI 范围，调用者负责线程与边界 |
| 包住 RDG builder | 在 graph 中插入 begin/end capture pass | 边界更贴近目标 RDG 图，begin/end 作为副作用用 `NeverCull` 保留 |

第三种对本篇最重要，而且它正好印证了第 3 节讲过的裁剪规则：begin/end capture 本身是**副作用**，不能靠资源输出证明自己的价值，所以它必须以**不可裁剪的 RDG pass** 形式插入图中。也就是说——纯副作用必须显式表达，否则 RDG 没有理由保留它。

Deferred renderer 还提供程序化的下一帧入口，把 capture 作为 RDG scope 放在 deferred renderer 图周围，因而边界更贴近 BasePass、Lighting、PostProcessing 这张目标图。它并不保证线程、submission 或同步形态与无 capture 基线一致；“更容易包住目标图”和“更少扰动原始复现”是两件事。

用 capture 时要记住两点，它们正好引出下一层：

1. Capture 看到的是 RHI/GPU 边界，**不是 GT 当前变量**。你刚在 GT 上改了 CVar，不代表它已经进入被 capture 的那一帧。
2. Capture 本身可能改变运行时形态。某些后端在捕获工具存在时会调整 submission thread、capture options 或同步行为。若“不开 capture 出错，开 capture 正常”，这不是工具没用，而是说明问题可能位于 capture 改掉的并行或提交路径上。

capture 文件中看到原生 command，最深只证明该 command 被记录在捕获边界。除非工具提供可靠的 replay / completion 证据，否则不能据此断言原始运行已经 GPU complete。真正的 GPU hang 或 device removed 还可能来不及完成普通 frame capture 保存；此时应优先保全 breadcrumb、DRED / vendor crash dump、device removed reason 与 queue fence，而不是把 capture 设为必经步骤。

这两点都指向同一个更深的层级：时间线。

## 6. 时间线层：Insights 看 CPU/线程，GPU Visualizer 看 GPU scope

**这一层要回答的问题**：你看到的那一帧，真的包含了你的改动吗？

第 03 篇已经讲过，UE 渲染不是一条从 C++ 到 GPU 的直线。GT、RT、RHI、GPU 是四条不同的时间线。调试时最常见的误判，就是把“GT 设置了值”当成“GPU 已经使用了值”。这一层有两类工具，分别盯两条不同的时间线。

### Insights 回答“CPU 工作走到了哪里”

Unreal Insights 和 trace 看的是 CPU 侧的事件、任务、线程和等待，适合回答：

```text
GT 是否发出了对应 render command?
RT 是否执行到构图或 pass setup?
RHI 提交链是否卡住?
哪些 TaskGraph / worker / render command 在等待?
某个 CVar sink 或命令是否发生在目标帧之前?
```

Insights 不会告诉你某张 GPU 纹理内容是否正确——那是 capture 的事。它证明的是 CPU 侧的时间线关系。贯穿案例里，如果 pass 根本没有进入 RT 构图，RenderDoc 再怎么抓也找不到；这时应先用 trace/Insights 判断命令是否到达 RT，而不是反复换 capture 入口。

### GPU Visualizer 回答“GPU scope 如何组织”

GPU Visualizer、`ProfileGPU`、`stat gpu` 这类工具看的是 GPU 的 event/scope 树和耗时，适合回答：

```text
某个 renderer 阶段是否出现在 GPU scope 树里?
BasePass、Lighting、PostProcessing、DebugView 分支是否按预期出现?
某个 RDG event 是否被收集成 GPU 可见 scope?
有完整 timestamp 的 scope 在 profile 形态下耗时多少?
```

它们不适合直接证明资源内容（那要用 capture），也不证明 CPU 等待（那要用 Insights）。GPU hang 时，“最后可见 scope”更不能直接叫“最后完成”：scope 名可能只被 CPU 记录，marker 可能被 GPU entered 但未 exited，完整 timestamp 也只覆盖该 scope，不替代整个 queue payload 的 completion fence。

### 把 GPU stat 翻译成 Pass / Draw 问题

`stat gpu` 或 GPU Visualizer 里的一行时间，不是“某个系统错了”的结论，而是 GPU 执行树上的一个坐标。调试时先把它翻译成三类问题：

```text
这个 scope 存在吗?
  存在：说明 profiler / capture 在当前形态观察到了对应 scope。
  不存在：先回 Pass 层，查 feature gating、RDG 裁剪、event 名或 capture 边界。

这个 scope 里是 draw 还是 dispatch?
  draw：下一步通常看 RenderDoc 的 pipeline state、bound resource、draw inputs。
  dispatch：下一步通常看 UAV/SRV、thread group 输入、barrier 和输出资源。

这个 scope 的完成深度是什么?
  CPU recorded：只生成了 scope / marker 记录。
  marker entered：GPU 到达入口，但未证明退出。
  marker exited / timestamp closed：GPU 离开该 marker 边界。
  fence completed：覆盖该 payload 的 queue completion 得到证明。
```

贯穿案例里，如果 `PostProcessing` scope 存在但你的合成 pass 不在里面，问题更像 Pass 层或 Debug/Visualize 分支；如果合成 pass 在 scope 里，但 RenderDoc 显示后续 draw 仍读旧 `SceneColor`，问题已经下沉到资源身份；如果 pass 只有在 `ProfileGPU` 开启时才出现或才消失，问题又回到时间线和执行形态。这样翻译后，GPU stat 才能把你带到下一步，而不是停在“这一段看起来有问题”。

更要注意的是，**`ProfileGPU` 不是纯观察者**。触发 GPU profile 会使 RDG async compute 不具资格，并限制 parallel RHI translate，因此 profile 形态和正常形态具有不同的调度条件。调试时至少要做两份记录：

```text
正常复现形态:
  不开 profile / capture / debug flush 时的症状。

观测形态:
  开启 ProfileGPU 或 GPU Visualizer 后的 scope 树。
```

如果问题只在正常复现形态出现、在 profile 形态消失，优先怀疑 async compute、parallel execute、RHI submission、UAV overlap、资源 lifetime 这类被观测工具改变的边界。这条经验和第 5 节 capture 的第二点是同一个道理：**观测本身会改变被观测的执行形态。**

### 工具互补矩阵：每个工具只拥有一段证据

| 工具 / 机制 | 能证明 | 不能证明 | 是否改变形态 |
| --- | --- | --- | --- |
| Insights / CPU trace | thread、task、render command、CPU wait 与 RHI 提交侧活动 | GPU 资源内容、GPU marker 是否完成 | trace 有成本，但通常不提供 GPU 内容 |
| GPU Visualizer / ProfileGPU | 当前 profile 形态的 GPU scope 树与可用 timestamp | 普通形态完全相同、resource bit pattern、payload fence | 是；禁用 async 资格并限制 parallel translate |
| RenderDoc / PIX | capture 边界内的 native command、pipeline state 与 resource snapshot | GT 当前变量、原始 hang 一定已完成 / 保存 | 是；可能改变 submission 与同步 |
| breadcrumb / DRED / vendor dump | crash / hang 附近 marker in/out、device / driver 诊断 | shader 输出语义正确、未启用 marker 时的完整历史 | 需要预先启用，对 marker 粒度敏感 |
| queue completion fence | 指定 queue payload 是否完成 | 资源内容是否正确、下游是否读对资源 | 正常同步原语；主动 wait / flush 会改变时序 |
| CVar / DebugView | 受控改变某一条件，隔离 feature 或 consumer | 单独定因、普通路径同样正确 | 是；它们就是实验输入或替代路径 |

工具选择应从缺少哪一层证据出发。例如已经有 `queue submitted` 但没有 fence progression，继续反复看 pass 名不会增加完成证据；已经有 fence complete 但画面错误，应转向 resource identity 与 output verified。

排除完时间线之后，如果症状还在，问题很可能根本不在你以为的那条管线上——而是 DebugView / Visualize 把管线换成了另一条。

## 7. 特性层：DebugView / Visualize 是管线节点，不是屏幕贴图

**这一层要回答的问题**：你看到的，是普通渲染路径，还是另一条被替换的路径？

DebugView、VisualizeBuffer、VisualizeNanite、VisualizeLumen 这些名字，很容易让人以为它们是“最后叠一张调试 UI”。在 UE 里，这通常是错的。

> **Unity 直觉**：debug overlay 大多是在最终画面上叠一层，不影响底下的真实渲染。
>
> **UE 的不同**：这些 visualizer 往往是渲染管线的一部分，甚至会**替换**普通路径，让你看到的根本不是普通渲染的结果。

DebugView 的典型行为，就是替换而非叠加：

```text
ViewFamily 进入 debug view mode
  -> BasePass 判断 UseDebugViewPS
  -> 普通 BasePass 并行 / 材质路径可能被替换或收缩
  -> DebugView 专用 pass 写入调试颜色
  -> 后处理走 debug post-processing 分支或 visualizer 分支
```

这个特性既非常有用，也非常危险。

**有用之处**在于它能帮你区分层级，缩小问题范围：

- 普通 BasePass 黑，但某个 DebugView 能看到 primitive：说明 visibility / mesh input 可能还在，问题更偏材质、GBuffer 或 lighting。
- DebugView 里也看不到 primitive：问题更早，回到第 08 篇的 visibility / mesh pass / GPUScene 输入链。
- VisualizeBuffer 能看到 GBuffer 某项错误：说明错误已经发生在 BasePass 发布合约之前。

**危险之处**在于它会改变路径。它可能关闭某些并行 BasePass 形态，使用 visualize permutation，走专门的 DebugView shader，甚至让普通后处理链变成 debug chain。因此“DebugView 正常”不等于普通渲染路径正常——它只证明另一条管线里的部分 visibility / inputs 和 debug consumer 可用。

VisualizeBuffer 也一样：它是后处理/visualize 路径上的一个**消费者**，会以某种显示方式读 GBuffer、SceneColor、Velocity、Depth 等中间图。它显示的结果已经经过 visualize shader 的解释，不一定等于原资源的原始 bit pattern。要查原始资源，仍然回到 GPU capture。

如果这一层的表现是“某个 DebugView 干脆不工作”，问题就再往下沉一层，进入 Shader / permutation。

## 8. Shader / permutation 层：调试视图异常时别只看 HLSL

**这一层要回答的问题**：当前模式要求的那份 shader，到底存不存在？

当问题表现为“某个 DebugView 不工作”“BasePass 回退默认材质”“某个 Visualize 模式下 shader missing”，调试层级已经进入 ShaderSystem。

第 21 篇的规则要直接拿来用：BasePass shader 是否存在，不只取决于材质，还取决于 shader type、permutation id、VertexFactoryType、当前线程可见的 shader map、ODSC / cook 状态。

Debug/Visualize 会额外引入一类风险：它们常常要求**特殊 permutation**。比如 BasePass shader 中的 visualize 维度、DebugView 专用 shader，或 show flag 触发的后处理 visualizer。如果 shader map 里没有对应组合，症状看起来像渲染 bug，实际是“当前材质 / VF / 平台 / cook 没有这份 shader”。

这里有一个必须明确的 UE5.7 条件：BasePass 的 `FVisualizeDim` 为 visualize 取值时只在 `IsODSCOnly` 标志成立的编译请求中通过检查。它不是普通 cook permutation 会无条件常驻所有 shader map 的承诺。因而“进入 DebugView 分支”和“ODSC 请求已完成、目标 shader map 已可用”是两个状态；请求发出后仍可能处于编译中，fallback / 默认材质也会形成另一条输出路径。

排查顺序是从“是否进入分支”一路问到“shader 是否真的编译出来了”：

```text
当前模式是否真的进入 DebugView / Visualize 分支?
它要求哪个 shader type、permutation、VertexFactoryType?
ShouldCompilePermutation 是否允许该组合?
visualize permutation 的 ODSC-only 请求是否成立并已完成?
当前 shader map 是否 complete?
TryGetShaders 是失败、ODSC 请求，还是 fallback 到默认材质?
```

把状态链写完整应是：show flag / mode 选中分支 -> 请求 ODSC-only visualize permutation -> shader map 可用 -> debug pass retained / recorded -> output 被 debug consumer 使用。若只确认第一个节点，last-valid-state 就是“进入 debug branch”；若 DebugView 已显示 primitive，也只能证明 debug 路径的部分输入和输出可用，不能证明普通 BasePass、lighting 或 post chain 已完成。

本篇不重新展开材质编译系统（那是第 20/21 篇）。关键是把“debug view 异常”放到正确层级：它可能不是 GPU capture 层的问题，也不是 RDG pass 裁剪问题，而是 shader map / permutation 生命周期问题。

输出、Pass、资源、时间线和特性 / shader 已经形成可互相跳转的路由节点。还剩一条横向贯穿这些节点的控制链——CVar，它本身就是最容易被误用的调试输入。

## 9. CVar 调试：记录“生效路径”，不要只记录“设置值”

CVar 横跨所有层级，也是调试里最容易被误用的工具。问题在于：很多人记录 CVar 时只写“我设了 `r.X=1`”，却没有证明设置被接受、传播、派生并被目标 consumer 使用。

准确模型是一条六段状态链：

```text
1. 声明与 flags
   CVar 是否存在；默认值是什么；ReadOnly / Cheat / RenderThreadSafe 等 flags 是什么。

2. set-by 接受
   新值来自哪个 set-by；优先级是否不低于当前来源；LastSetBy 是否真的改变。

3. 线程传播
   main-thread value 是否已按 render command 顺序传播到 render-thread shadow。

4. sink / 派生 cache
   change 只把 sink 标为待执行；CallAllConsoleVariableSinks 发生后派生全局才更新。

5. consumer 读取
   consumer 是每次动态读取、读 RT shadow、读 sink cache，还是启动期 / 构造期 read-once。

6. 运行时与构建约束
   平台、RHI、profile、flush、bypass、cook / shader key 是否允许目标路径。
```

### Flags 和 set-by 决定“设置是否被接受”

`ReadOnly` 禁止用户从 console 改值，但 C++ 或 ini 仍可设置；`Cheat` 在禁用 cheat CVar 的最终构建中对用户隐藏并不可改；`RenderThreadSafe` 维护一份按 render command 顺序更新的 render-thread shadow，它不是“所有线程随时读都安全”的承诺。

set-by flags 从 Constructor 到 Console 按弱到强排序。较低优先级的写入会被拒绝，即使你执行了命令；因此每次实验都要记录当前值与 `LastSetBy`。例如 device profile、command line 或 code 已以更高来源设置时，较弱 ini 写入不能覆盖它。只有 set 被接受，主值才进入下一段传播。

### Change、sink ran 与 consumer used 是三个状态

CVar 改变会把 sinks 标为待调用，但不等于 sink 已运行。只有引擎到达 `CallAllConsoleVariableSinks` 的调用点，派生全局或 cache 才更新；render-thread shadow 也要等排队的 render command 到达。于是“console 打印新值”可以与“RT 仍读旧 shadow”或“consumer 仍读旧派生 cache”同时成立。

consumer 决定值的有效期。动态读取可以在后续调用看到新值；RT shadow 要等线程传播；sink-cached 值要等 sink；startup / constructor read-once consumer 在当前进程不会重新读取。UE 没有一个通用开关能替代 consumer 审计，也没有一个对所有 CVar 都正确的“restart required”答案。

| consumer 类型 | 新值生效条件 | 恢复 / 重建要求 |
| --- | --- | --- |
| 每次动态读取 | set 被接受且读取发生在设置之后 | 通常无需重启，仍要恢复原 set-by |
| render-thread shadow | 对应传播命令已到 RT | 等待正确帧边界，不用 flush 冒充正常形态 |
| sink / 派生 cache | sink 已调用并更新派生值 | 恢复后再次运行 sink 并验证基线 |
| startup / read-once | 重新进入读取点 | 通常需要重启进程；以具体 consumer 为准 |
| shader-key / cook 依赖 | 目标 shader permutation 被重编译并进入可用 shader map | 需要 shader rebuild / cook；单纯重启不保证生成缺失变体 |
| render state / resource 构建依赖 | 相关状态或资源被显式重建 | 可能需要 recreate / reload；不能只看 CVar 对象值 |

贯穿案例中，若打开 async CVar 后路径仍在 graphics pipe，应依次记录：声明 flags 与默认值、设置来源和 `LastSetBy`、RT shadow 是否更新、async sink 是否已刷新、RDG consumer 实际读到的派生值、最后是否被 profile / platform 条件压低。若同时打开 flush、profile 和 capture，任何结果都无法归因。

“我一开某 CVar，bug 就消失”只是定位线索：它说明该变量改变了声明、lifetime、alias、barrier、parallel / async 或 submit 中的一类条件。要把线索变成结论，必须恢复原值与原 set-by，重现基线，再只改这一个变量复验。同一个 CVar 在正常复现、ProfileGPU、capture、flush、Shipping/Test 构建中的意义不同，报告必须同时写设置来源、consumer 和观测条件。

---

## 10. 复现矩阵与 last-valid-state：让实验可以归因

工具只负责观察；实验者负责固定环境、一次只改一个变量并恢复基线。PSO warmup、streaming、shader compile、跨帧 history、随机种子、transient allocation 都会污染下一轮，所以“关闭开关”不自动等于回到原状态。

### 先冻结复现矩阵

| 字段 | 最低记录内容 | 为什么需要 |
| --- | --- | --- |
| build / branch | 配置、版本、改动集 | debug 能力、优化与 shader 内容可能不同 |
| platform / RHI / GPU / driver | 目标硬件与后端 | queue、marker、crash evidence 和资源状态支持不同 |
| map / camera / view | 场景、位置、show flags、分辨率 | 固定输出与 workload |
| frame / trigger | 第几帧、触发动作、最后正常帧 | 区分 GT / RT / GPU 延迟与 history |
| warmup / streaming / shader state | 预热帧数、streaming 完成、shader / PSO 是否 ready | 避免 cold miss 与异步加载污染 |
| CVar history | 值、`LastSetBy`、设置时点、consumer | 证明实验输入真的生效 |
| observation state | profile / capture / validation / clobber / flush 是否开启 | 标记执行形态是否被改变 |
| random / temporal state | 随机种子、history 清理方式 | 保持跨帧算法可比较 |

### 每轮只改变一个变量

```text
1. 在无额外工具的目标形态重现 baseline。
2. 记录 baseline evidence 与 last-valid-state。
3. 只改变一个 CVar、工具或代码条件。
4. 使用同一复现矩阵记录结果。
5. 恢复该变量的原值、原 LastSetBy 与必要的缓存 / history。
6. 再次重现 baseline。
7. 只有 baseline 能恢复，实验差异才可用于定因；否则只记为线索。
```

快速探索阶段可以同时开多个工具寻找方向，但提交结论必须回到单变量复验。validation + clobber + flush + profile + capture 一起开启，会同时改变 graph、资源初值、async、parallel translation 与 submission，无法知道是哪一项改变症状。

### 所有路线共用的完成深度

| completion depth | 允许证明的最深状态 | 不得外推 |
| --- | --- | --- |
| Declared | pass / resource / command 已声明 | 未裁剪、已录制 |
| Retained | RDG compile 后未裁剪 | execute lambda 已进入 |
| RHI recorded | 平台无关的 RHI command 已进入 RHI command list | 后端已翻译或形成 native command list |
| Platform commands formed | 后端已把 RHI 记录翻译并编码为可提交的 native command list | platform queue 已接收 payload |
| Submitted | queue 已接收 command lists，payload fence 已 signal | GPU 已完成 |
| Marker entered / exited | GPU 到达 / 离开该 marker 边界 | 整个 payload、后续资源或其他 queue 已完成 |
| GPU complete | 无 device-removed 失败，且有效 completed fence 达到目标 payload value | 输出内容与业务语义正确 |
| Output verified | 指定 consumer / readback / capture 在当前条件验证结果 | 其他 profile、capture、build 形态同样正确 |

last-valid-state 是本轮证据能到达的最深一项；completion depth 是这项在上表中的类别。无法证明下一项时必须停住，不能用“应该已经”“最后看到”补空白。

### 可复用记录模板

```text
症状签名:
复现矩阵:
假设层 / state owner:
baseline evidence:
单一改动:
改动后的 evidence:
恢复动作与 LastSetBy:
恢复后 baseline 是否再次出现:
last-valid-state:
completion depth:
当前工具能证明 / 不能证明:
下一条可证伪假设:
```

## 11. 六条常见回溯路线

前面已经把工具分到了各个证据层。下面六条路线从各自最高信息量入口开始；每完成一个步骤，都要更新 last-valid-state 与 completion depth。任何 CVar 或工具实验都按“baseline -> 单一改动 -> 恢复 -> baseline 再现”执行。

### 路线一：黑屏

```text
1. 输出层:
   后处理最后有效 pass 是谁，是否写 ViewFamilyTexture?

2. Pass 层:
   BasePass / Lighting / PostProcessing / 你的合成 pass 是否都出现在 GPU scope 或 capture 中?

3. 资源层:
   你的 pass 写的是当前 SceneColor，还是临时 texture?
   后续消费者读的是不是同一张资源?

4. 时间线层:
   CVar、show flag、代码改动是否进入了被抓的那一帧?

5. 特性层:
   DebugView / Visualize 是否替换了普通后处理输出?

输出:
   最后确认的 writer / consumer、last-valid-state、completion depth、下一条假设。
```

### 路线二：资源未初始化、NaN、旧内容

```text
1. 固定 writer / reader resource identity 与首次异常区域。
2. 只开 validation；记录后恢复 baseline。
3. 只开 clobber；它只证明已知值是否仍未被真实 producer 覆盖，记录后恢复 baseline。
4. 另起一轮只改变 transient / lifetime 条件，判断是否由 alias 或过早复用触发。
5. 在 capture 中确认写者和读者是否同一资源，并查声明与 barrier。
6. 输出最深证据：RHI recorded / Platform commands formed / Submitted / GPU complete / Output verified 中哪一项成立。
```

### 路线三：Pass 消失

```text
1. pass 是否有清晰 event name?
2. 参数结构是否包含图内资源输入/输出?
3. 输出是否通向 external、extracted 或最终 ViewFamilyTexture?
4. 如果是纯副作用，是否需要 NeverCull?
5. 是否误以为 lambda 捕获会建立 RDG 依赖?
6. last-valid-state 停在 Declared 还是 Retained；event 缺失是否先排除了可见条件?
```

### 路线四：GPU hang

```text
1. 先分类症状:
   CPU deadlock / RT-RHI wait、queue starvation、long-running work、GPU hang、device removed、OOM / driver reset。

2. 保全原始证据:
   build、RHI、queue、目标 submitted payload 的 completion fence N、该 queue 的 completed fence M、breadcrumb in/out、device removed reason、crash dump。

3. 用 Insights 查 CPU:
   RT / RHI 是否在等待，submission thread 是否前进；CPU wait 不等于 GPU hang。

4. 查提交深度:
   command 只到 RHI recorded，还是已形成 platform commands，或 payload 已 Submitted；未提交的工作不能用 GPU marker 解释。

5. 查 GPU 边界:
   marker entered 只证明进入；marker exited 只证明离开该 marker；两者都不替代 payload fence。

6. 判定 GPU complete:
   completed value 有效且不是 UINT64_MAX、无 device-removed 失败，并且 M >= N，才证明目标 payload complete。

7. 条件分支:
   可稳定复现且不触发 device loss 时，才尝试局部 capture；真正 device removed 优先 DRED / vendor dump / breadcrumbs。

8. 单变量复验:
   flush、关闭 async、关闭 overlap、改变 submission thread 分别单独实验并恢复 baseline。
```

最后进入 breadcrumb 不是最后完成，最后退出 marker 也不保证整个 queue payload 完成。若 `M < N`，last-valid-state 最多是 Submitted 或某个 marker 边界；若 device removed reason 已失败，不能把 `UINT64_MAX` 当作正常完成值。普通 capture 无法在崩溃后保存时，不构成证据缺失的异常，也不能成为必经步骤。

### 路线五：DebugView / Visualize 异常

```text
1. 当前 show flag 是否真的进入对应 DebugView / Visualize path?
2. 它是替换 BasePass，还是后处理链上的 visualizer?
3. 输入资源来自 SceneTextures 的哪个阶段，HDR/LDR 语义是否匹配?
4. `FVisualizeDim` 的 ODSC-only 请求是否完成，目标 shader map 是否 available?
5. 若 DebugView 能看到物体而普通路径错，回到材质/GBuffer/Lighting。
   若 DebugView 也看不到物体，回到 visibility / mesh pass / GPUScene。
6. 记录 last-valid-state 是 branch selected、shader available、pass Recorded 还是 output verified。
```

### 路线六：CVar 设置后无效或改变症状

```text
1. 当前构建是否包含该 CVar，flags 与默认值是什么?
2. 设置是否被 set-by priority 接受，当前值和 LastSetBy 是什么?
3. render-thread shadow 是否已按命令顺序传播?
4. sink 是否运行，派生 cache 是否更新?
5. consumer 是动态读取、RT shadow、sink cache 还是 startup / read-once?
6. 目标路径是否被 platform / bypass / flush / profile / capture 条件退化?
7. 该值是否要求 restart、shader rebuild / cook，或显式重建 render state / resource?
8. 恢复原值与原 LastSetBy，确认 baseline 再次出现，再填写 last-valid-state。
```

---

## 收尾：一张路由图，一套完成判据

```text
黑屏 / 最终颜色错
  -> output writer -> pass retained -> resource identity -> timeline / feature

旧值 / NaN
  -> producer coverage -> resource identity -> barrier / lifetime -> output consumer

pass 消失
  -> Declared -> culling root / external output / side effect -> Retained

GPU hang / device removed
  -> CPU wait -> RHI recorded -> Platform commands formed -> Submitted -> marker in/out -> completed fence / device reason

DebugView shader missing
  -> branch selected -> ODSC-only request -> shader available -> debug output consumed

CVar 无效
  -> flags -> set-by accepted -> RT shadow -> sink/cache -> consumer -> rebuild / restart
```

工具挂在这些状态旁边，各自只能提供局部证据：RDG event / culling / validation / clobber 观察图合同；RenderDoc / PIX 观察 capture 边界内的 command 与 resource；Insights 观察 CPU thread、task 与 wait；GPU Visualizer / ProfileGPU 观察 profile 形态的 scope；breadcrumb / DRED / vendor dump 观察 crash 边界；queue fence 证明 payload completion；CVar 与 DebugView 是会改变形态的实验输入。

一句话收束：**按症状选择最高信息量入口，用单变量实验推进证据，并把结论停在可证明的 last-valid-state；RHI recorded、Platform commands formed、Submitted、marker entered / exited、GPU complete 和 Output verified 不能互换。**

这也是第 24 篇的交接点。本篇问“为什么画错”，所以围绕证据闭环和层级定位；下一篇问“为什么慢”，会复用 Insights、GPU Visualizer、ProfileGPU 这些同样的工具，但提问从正确性转向成本归属。工具相同，问题不同。
