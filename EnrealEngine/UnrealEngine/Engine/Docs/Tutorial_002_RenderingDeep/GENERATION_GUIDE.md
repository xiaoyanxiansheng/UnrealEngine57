# 文档生成指南

本文件定义了 Tutorial_002_RenderingDeep 系列文档的生成规范。
每次生成新文档时，必须阅读本文件并严格遵循。

---

## 基本信息

- **源码目录**: `D:\Unreal\EnrealEngine\UnrealEngine`
- **文档目录**: `D:\Unreal\EnrealEngine\UnrealEngine\Engine\Docs\Tutorial_002_RenderingDeep`
- **大纲文件**: 同目录下 `OUTLINE.md`
- **源码索引**: 同目录下 `SOURCE_INDEX.md`（共享知识库，跨频道沉淀）
- **版本**: UE5.7（固定，不会升级，源码不会变动）
- **语言**: 中文
- **读者**: Unity 技术美术，有渲染基础，C++ 不熟

---

## 双模型完成门禁

从 05 之后，任何新章节都必须经过三步，才能进入完成状态：

1. **Codex / GPT-5.5 生产**：负责源码调研、边界表、技术主线、正文初稿和 CoverageMatrix 事实校准；正文不承载重复源码锚点块，Gate 1 不沉淀 SOURCE_INDEX。
2. **Claude Opus 4.8 教学优化**：只负责教学表达，包括段落节奏、承接、类比、读者疑问预判和重复压缩；不拥有技术事实裁判权。
3. **Codex / GPT-5.5 最终事实回归**：在 Claude 优化后重新核对高风险事实，确认没有因表达优化而改变线程归属、生命周期顺序、函数名、调试路标或章节边界；只有 Gate 3 接受后才把必要事实沉淀到 SOURCE_INDEX。

**未经过 Claude 教学优化和 Codex 最终事实回归的文章，不能在 `OUTLINE.md` 标记为完成。**

Claude 优化时必须输出同目录旁路报告：

```text
<章节名>_TeachingEditReport.md
```

Codex 最终验收时优先读取该报告。若报告缺失、过于笼统，或无法可靠隔离 Claude 改动，不能走 surgical fact-diff；必须升级为全量复核所有 A 类事实和高风险事实。全量复核通过前不能标记完成。

Codex 在 Gate 1 生产时必须落盘覆盖矩阵旁路报告，供 Gate 3 审计和对照：

```text
<章节名>_CoverageMatrix.md
```

该文件是章节级文件（非 `OUTLINE.md` / `SOURCE_INDEX.md` 这类共享文件），Gate 1 可写；审查时必须从 `OUTLINE.md` 核心问题和最终正文重建 / 核对，不得复用过期草稿矩阵。Codex 最终事实回归时必须读取该矩阵作为输入；矩阵缺失、陈旧或与最终正文矛盾时，以重建矩阵为准，并在验收前更新或拒绝该章节。

日常使用只需要记三个短句：

```text
生产 <编号或文件名>
优化 <编号或文件名>
终审 <编号或文件名>
```

含义分别是：Codex 生产初稿、Claude 教学优化、Codex 最终事实回归。文件名可由 `OUTLINE.md` 推断；如果推断不了，再要求用户补全文件名。

---

## 最高原则

**讲清楚，讲明白。**

这不是参考手册，不是知识点罗列，不是报账。这是一本让读者跟着走完就能理解系统的教程。

源码服务于事实校准和调试定位，不是教学正文的骨架。每篇必须先建立读者要解决的问题、UE 的设计想法、主流程或算法骨架；源码验证记录只放在 CoverageMatrix、TeachingEditReport 或 SOURCE_INDEX 中。正文可以保留已经解释过的 UE 符号作为概念名或调试路标，但不能用文件路径、行号、函数清单或验证记录承载教学。不能一开头就列函数、类、字段，让读者靠源码名字反推概念。

如果一段文字删掉之后不影响读者理解主线流程，那它就不应该存在。
如果一个知识点只是"存在"但不参与当前的叙事，就不要提它。
如果读者看完一个小节不知道"接下来会发生什么"，那这个小节的结尾写得有问题。

**质量不是信息量本身。** 高质量文章必须同时满足三件事：事实可靠、边界清楚、叙事顺畅。信息很多但读者不知道哪些是主线，是失败；讲得很顺但没有事实校准，也是失败；源码细节都对但挤占正文主线或越界提前讲后续篇内容，仍然是失败。

---

## 铁律

1. **所有技术结论必须基于源码验证** — 禁止凭记忆或猜测。验证记录使用稳定锚点：`文件路径` + `函数/类型/宏名`，行号作为辅助定位；这些记录默认进入 CoverageMatrix / SOURCE_INDEX / TeachingEditReport，不进入正文重复锚点块。
2. **禁止编造** — 不确定的内容标注 `[待验证]`，不可伪造函数名、行号、参数。
3. **禁止源码先行** — 正文不能以函数调用栈、结构体字段表、类关系表或“源码锚点”块作为教学入口。必须先讲目的、想法、流程/算法；必要函数名只能作为概念名或调试路标出现。
4. **不讲基础渲染理论** — 读者已掌握延迟渲染、GBuffer、光照模型等。直接讲 UE 的实现。
5. **不以字数为目标** — 篇幅服从教学边界。该长就长，该短就短，禁止为了达到行数而提前灌入后续篇内容。
6. **只讲透本篇边界内的层** — 不允许"点到为止"，但也不允许越界挖深。每个属于本篇边界的知识点都必须回答"是什么/为什么/有没有其他方式"三个问题。唯一例外：后续有专门章节深入的，可以简短介绍并注明出处。
7. **先分层级再写细节** — 事实分三类处理：
   - A 类：主线关键事实。必须现场源码核对，并在 CoverageMatrix 记录验证状态；正文只写会推进理解的概念名、状态变化和必要调试路标。
   - B 类：辅助解释事实。可引用 `SOURCE_INDEX.md`，但如果涉及具体函数逻辑或行号，仍要重新打开源码核对。
   - C 类：背景类比或经验判断。必须明确是解释性类比，不能伪装成源码事实。
8. **符号锚点优先于行号** — 行号会随着本地改动、分支、格式化漂移。CoverageMatrix / SOURCE_INDEX 优先记录 `文件路径 + 函数/类型/宏名`，必要时再补行号。正文需要调试路标时优先写符号名和它验证的状态转换，不写密集路径列表。
9. **CVar 与运行时行为分开写** — 写 CVar 时必须区分"源码默认值""枚举/模式映射""运行时校验后的退化结果""实际生效上限"。例如线程、并行度、调试工具、平台分支相关 CVar，不能把默认值、平台覆盖、worker 数限制、Bypass/验证逻辑混成一句经验判断。

## 深度要求

**核心原则：可以学得慢，但不能学得粗。**

每个首次出现的知识点，都必须用以下结构理清：

1. **是什么** — 具体包含什么数据、做什么事、在什么时机被创建/调用
2. **为什么** — 为什么需要它？它解决了什么具体问题？如果没有它会怎样？
3. **怎么做** — 先用自然语言讲清流程、数据形态或算法步骤；源码只用于旁路校准和少量调试路标
4. **会怎样** — 如果少了这一步、顺序反了、直接访问、跳过队列或绕开抽象，会出现什么问题？
5. **还有其他方式吗** — UE 为什么选择这种方案而不是其他方案？其他方案（包括 Unity 的做法）有什么不足？

这些问题必须全部回答清楚，读者才算真正理解了这个知识点。不要把"怎么做"写成源码调用栈；调用栈只是证明和定位，真正的"怎么做"应该先是流程、算法、数据变化和所有权变化。

**唯一的例外**：如果这个知识点在后续文档有专门的深入章节，当前文档可以简短介绍（2-5 句话说明它是什么、在当前流程中扮演什么角色），然后明确告诉读者"具体机制在第 XX 篇展开"。但"简短介绍"不等于"不解释"——读者必须至少理解它在当前上下文中的角色和必要性。

除此之外，**本篇边界内涉及到的每一个知识点，一个都不能放过。** 边界外的知识点只讲它在当前流程里的角色，并明确指向后续篇；不要在当前篇把它展开成第二条主线。

---

**每个属于本篇边界的机制都要让读者建立完整的心智模型。**

"心智模型"意味着读者看完后能回答：
- 这个东西的输入是什么、输出是什么？
- 中间经历了哪几步？每步做了什么？
- 为什么要分这几步？如果少一步会怎样？
- 数据在这个过程中是什么形态？从什么变成了什么？

### 什么叫"没讲透"

> Proxy 创建后通过 ENQUEUE_RENDER_COMMAND 发送到 Render Thread。

读者看完这句话知道"有个发送过程"，但不知道：
- 发送的是指针还是拷贝？
- Render Thread 什么时候执行这个命令？下一次 tick？立即？
- 如果 Game Thread 在发送后立刻修改了 Component 数据，Proxy 会受影响吗？
- 发送失败了怎么办？有队列满的情况吗？

这些问题如果读者自然会产生，就必须回答。不需要每个都用一大段，有些一句话就能澄清——但不能跳过。

### 什么叫"讲透了"

> Proxy 是在 Game Thread 上 new 出来的，但它的所有权立即转移给了 Render Thread。`ENQUEUE_RENDER_COMMAND` 把一个持有 Proxy 指针的 lambda 包装成渲染线程命令，走 TaskGraph 或 RenderCommandPipe 投递到 Render Thread 上下文执行。Render Thread 在处理渲染线程任务时按序执行这些命令。从 `ENQUEUE` 那一刻起，Game Thread 承诺不再访问这个 Proxy——这不是靠锁保证的，是靠代码契约：所有后续对渲染数据的修改都通过 `MarkRenderStateDirty()` + 新的 ENQUEUE 命令来完成，而不是直接写 Proxy。
>
> 如果 Game Thread 修改了 Transform，它不会去改 Proxy 的 Transform 字段。它会调用 `UpdateComponentToWorld()`，然后通过 `ENQUEUE_RENDER_COMMAND(UpdateTransform)` 发一条新命令，Render Thread 收到后调用 `Proxy->SetTransform_RenderThread(NewTransform)` 更新。这样两个线程永远不会同时写同一块内存。

后者让读者真正理解了所有权语义和更新机制。

### 篇幅预期

- 每篇文档先确定**教学边界**，再自然形成篇幅。
- 主流程文档通常会很长，但 2000 行不是硬指标。04_RHI 这种边界较窄的专题，即使篇幅明显短于全景型章节，也可能是正确深度。
- 篇幅不足时，先问："是否漏了本篇本职内容？"而不是"能不能把后续篇内容提前塞进来？"
- 篇幅过长时，先问："这些深入段落是否仍在推进主线？"如果不是，删掉、移到后续篇，或放进附录/源码索引。
- **宁可短而边界清楚，也不要长而主线发散。**

---

### 核心要求：每篇文档必须有一条主线

主线是什么？是一条**连续的流**——数据流、执行流、或者决策流。读者跟着这条流走完，自然就理解了系统。

好的主线示例：
- "一个 StaticMesh 从放入场景到被 GPU 绘制，数据经历了什么"
- "从 FEngineLoop::Tick 开始，一帧的渲染是怎么一步步发起的"
- "一条光线在 Lumen 中从发射到最终贡献到像素颜色，经历了哪些阶段"
- "一个 DrawCall 是怎么从 FMeshBatch 变成 GPU 执行的命令的"

坏的组织方式（禁止）：
- "Layer 5 有这些类，Layer 4 有这些类，Layer 3 有这些类..."
- "核心类一览表 → API 列表 → 常量列表"
- "概念 → 实现 → 设计决策 → 概念 → 实现 → 设计决策..."

### 写前边界审查

动笔前必须先写一份内部边界表。边界表不一定进入正文，但必须指导写作：

| 项 | 要回答的问题 |
|----|--------------|
| 本篇核心问题 | 读者读完这一篇，最重要要解决哪一个问题？ |
| 教学目的 | 为什么 Unity TA 读者需要理解这个 UE 机制？它解决哪类具体困惑或调试/开发问题？ |
| 主线类型 | 这是执行流、数据流、资源生命周期、调度决策流，还是算法管线？ |
| 贯穿案例 | 用哪个具体对象/命令/资源贯穿全文？例如一个 StaticMesh、一条 DrawCall、一张 RDG Texture |
| 先导流程/算法 | 在看源码之前，读者应该先掌握哪几步流程、哪条数据变化或哪套算法思想？ |
| 事实校准计划 | 哪些 A 类事实必须源码复核？验证记录放入 CoverageMatrix / SOURCE_INDEX / TeachingEditReport 的哪一面？正文只保留哪些必要调试路标？ |
| 本篇必须讲透 | 哪些概念属于本篇本职，不讲透读者就无法理解主线？ |
| 本篇只点到 | 哪些概念会出现，但深入归属于后续篇？要标明后续篇编号 |
| 不讲内容 | 哪些看似相关但对本篇主线无帮助，必须压住不讲？ |
| 高风险事实 | 哪些结论容易因 UE5.7 新接口、平台差异、CVar 默认值而出错，必须源码复核？ |

写作前如果边界表无法填清楚，不要开始正文。边界不清时写出来的文章一定会变成"什么都讲一点"。

边界表还有一个用处：写完后逐项回查。如果正文出现了"本篇只点到"或"不讲内容"里的大段展开，要删掉或移走。

### 怎么写每个小节

**用"因此/然后/但是"连接，不用"另外/此外/还有"。**

每个小节的结构应该是：
1. 上一步的结果到了这里（承接）
2. 这里需要做什么、为什么（动机，1-2句）
3. 它是怎么做的（流程、数据形态、所有权变化；必要时点名调试路标）
4. 做完之后数据/控制流去了哪里（引出下一步）

示例（好）：

> `FScene::AddPrimitive` 被调用后，Component 的渲染数据需要跨线程传递给 Render Thread。但 Game Thread 和 Render Thread 并行运行，直接传指针会有竞争。UE 的做法是调用 `CreateSceneProxy()` 创建一个只读副本——`FPrimitiveSceneProxy`——然后通过 `ENQUEUE_RENDER_COMMAND` 把这个副本的所有权移交给 Render Thread。从这一刻起，Game Thread 不再触碰这个 Proxy，Render Thread 全权管理它的生命周期。
>
> 接下来 Render Thread 收到这个 Proxy 后，需要把它注册到场景的渲染数据结构中...

示例（坏）：

> ### FPrimitiveSceneProxy
> #### 概念
> FPrimitiveSceneProxy 是渲染线程的数据镜像。
> #### 实现
> 定义在 PrimitiveSceneProxy.h:295。
> #### 设计决策
> 使用 Proxy 模式避免跨线程竞争。

### 密度控制

一篇深度教程不是把所有相关事实都放进去，而是把必要事实按正确顺序放进去。写作时遵守这些密度规则：

- 一个小节只解决一个推进点。小节标题如果能拆成 "A 与 B 与 C"，说明它太宽。
- 连续三段都没有出现主线对象（例如这篇跟踪的命令、资源、Pass、Primitive），说明已经偏离主线。
- "深入：" 小节必须服务当前篇主线。它不能只是作者知道得更多。
- 表格只用于比较少量关键差异；如果表格超过 8 行，优先改成叙事或移到索引。
- 代码片段只贴关键转折点。贴代码前先写一句"为什么必须看这段"，贴完后立刻解释"这段改变了什么状态/所有权/控制流"。
- 比喻只能帮助读者进入机制，不能替代机制本身。比如"RDG 像编译器"之后，必须落到具体的依赖图、生命周期、屏障、调度。

### 架构图的位置

架构图、类关系图、层次图**放在文末总结**，或者放在读者已经跟着主线走完之后。它是对已有理解的归纳，不是学习的起点。

唯一的例外：如果一个简短的示意图能帮助读者理解接下来要跟着走的路（"地图"），可以在开头放一个精简版，但必须简短（5-10 行 ASCII），并明确告诉读者"接下来我们沿着这条路走"。

### 设计决策怎么写

不要单独成段。融入叙述中，用一两句话点明：

> UE 没有让 Renderer 直接读 Component 数据，因为那样必须加锁。加锁意味着 Game Thread 一修改 Transform，Render Thread 就得等——这对 60fps 来说不可接受。所以它创建了只读副本（Proxy），两个线程各管各的。

这就够了。读者理解了动机，知道了 tradeoff。不需要"替代方案 A/B/C 对比表"。

### C++ 知识怎么融入

遇到读者可能不熟的 C++ 用法时，**在当前上下文中用 2-5 行解释清楚**，然后继续主线。

> 这里用了 `ENQUEUE_RENDER_COMMAND`，它本质是把一个 lambda（匿名函数）打包发送到 Render Thread 执行。lambda 的方括号 `[this]` 表示"捕获当前对象的指针"，这样 Render Thread 执行时能访问到发送方的数据。需要注意的是：如果捕获的是指针，你必须确保 Render Thread 执行时那个对象还活着。

不需要单独的 "C++ 知识点" 章节。

### Unity 对比怎么融入

在讲到 UE 的某个设计时，如果 Unity 有对应概念，顺带一句建立映射：

> Unity SRP 公开层常在每帧用 `CullingResults` / `RendererList` 组织绘制；UE 静态 mesh 路径则把可缓存的 pass 决策预编译成 MeshDrawCommand，注册或更新时沉淀到 Renderer 侧场景里。这是两种引擎暴露给用户的工作模型差异之一。

不需要单独的对比章节。不需要表格。

---

## 文档骨架（参考，不是死板模板）

```markdown
# [编号] [主题名称]

> **源码版本**: UE5.7
> **前置阅读**: [前序文档编号]
> **当前状态**: [Gate 状态]

---

[开场：1-3 段，建立动机。从一个具体场景/问题出发，让读者知道"为什么我需要理解这个"。不要在这里列函数调用栈。]

[先导模型：在不贴源码的前提下，讲清本篇要解决的目的、UE 的设计想法、主流程/算法骨架，以及读者接下来应该带着什么问题去看源码。]

[可选：如果流程复杂，放一个精简路线图（5-10 行），告诉读者"我们要走这条路"。]

---

[主体：沿着主线展开。每个小节是流程中的一步。]
[每步都要深入到读者能建立心智模型——不是"知道有这么个东西"，而是"理解它怎么工作"。]
[小节之间有明确的因果/时序衔接。每个小节先说明这里要解决什么问题、为什么需要这一步、数据/控制流会怎么变，再说明读者如何用这个模型调试。]
[正文只使用必要 UE 符号作为概念名或调试路标；文件路径、行号、密集函数列表和验证记录放入 CoverageMatrix / SOURCE_INDEX / TeachingEditReport。C++ 解释、Unity 对比自然融入叙述中。]
[如果一步涉及复杂机制（如 RDG 的编译过程、命令列表的录制回放），就在这一步内部充分展开，不要留给"后续文档"——除非后续文档确实有专门对应的篇幅。]

---

[收尾：]
[1. 回顾：用一段话把整条主线复述一遍（现在读者已经理解了，这是巩固）]
[2. 可选：放一张完整的架构图/流程图（此时读者能看懂了）]
[3. 引出：这篇讲完之后，下一篇从哪里接上]
```

**关键区别**：没有固定的"概念/实现/设计决策"三段式。每个小节的内部结构由叙事需要决定。有时候需要先看代码再解释为什么，有时候需要先讲为什么再看代码。

---

## 源码验证记录规范

### 记录位置

- **CoverageMatrix**：Gate 1 必写。记录本章 A 类事实的验证状态、文件 + 符号锚点、未解决风险和覆盖状态。
- **TeachingEditReport**：Claude 优化后必写。记录教学结构变化、事实是否改动、Fact Questions For Codex 和建议沉淀项。
- **SOURCE_INDEX.md**：只在 Gate 3 通过后更新。只沉淀跨章节有复用价值的事实和调试入口，不记录正文为了证明自己而列出的临时清单。
- **正文**：不写文件路径、行号、密集函数清单或验证记录。正文只允许使用已经解释过的 UE 符号名作为概念名或调试路标；每个路标必须说明它对应哪个状态转换或 debug 问题。

### 路径格式

验证记录中的路径相对于 `Engine/Source/Runtime/`（与 `SOURCE_INDEX.md` 的约定保持一致）：
```text
Renderer/Private/DeferredShadingRenderer.cpp::FDeferredShadingSceneRenderer::Render
```

模块不在 `Runtime/` 下时（如 `Developer/`、`Editor/`），写出从 `Engine/Source/` 起的完整相对路径并特别标注。

### 代码片段

- 默认不在正文引用代码片段。
- 只有当一小段代码能显著澄清已经解释过的概念、且自然语言无法等价表达时，才可以引用。
- 单个片段建议不超过 30 行（如果需要更长，用注释分段并穿插解释）
- 用自然语言 + 代码配合，而非纯代码块
- 代码片段应该嵌入在叙述段落之间，不是堆在一起，不得形成固定“源码锚点”块
- 允许对同一个函数分段引用——先贴前半段解释，再贴后半段解释，比一口气贴 80 行然后统一解释要好

好的用法：

> 当 Viewport 触发渲染时，它通过 `ENQUEUE_RENDER_COMMAND` 把工作发给 Render Thread：
>
> ```cpp
> // Runtime/Engine/Private/UnrealClient.cpp:1686
> ENQUEUE_RENDER_COMMAND(BeginDrawingCommand)(
>     [Viewport](FRHICommandListImmediate& RHICmdList) {
>         Viewport->BeginRenderFrame(RHICmdList);
>     }
> );
> ```
>
> 这个 lambda 会在 Render Thread 的下一次 tick 中被执行。注意它按值捕获了 `Viewport` 指针——因为 Viewport 的生命周期由 Engine 管理，在帧结束前不会被销毁，所以这是安全的。

坏的用法：

> ### 2.3.3 EnqueueBeginRenderFrame 实现
> ```cpp
> void FViewport::EnqueueBeginRenderFrame(const bool bShouldPresent)
> {
>     FViewport* Viewport = this;
>     ENQUEUE_RENDER_COMMAND(BeginDrawingCommand)(
>         [Viewport](FRHICommandListImmediate& RHICmdList)
>         {
>             Viewport->BeginRenderFrame(RHICmdList);
>         }
>     );
> }
> ```

（后者只是贴代码，没有融入叙事。）

---

## SOURCE_INDEX.md 使用规则

`SOURCE_INDEX.md` 是跨频道的共享知识库，存储已验证的源码事实。

### 信任策略

- 源码版本固定，但 `SOURCE_INDEX.md` 是人工维护的索引，不是最终事实来源。
- 可以把其中的路径、行号、类层级当作**检索入口**，不能把它当作免验证依据。
- 对正文中的 A 类主线关键事实，必须重新打开源码核对，即使 `SOURCE_INDEX.md` 已经记录过。

### 什么时候仍需验证

- INDEX 中没有的信息
- 需要阅读函数实现的**具体逻辑**（INDEX 只记位置）
- 涉及 UE5.7 新系统、CVar 默认值、平台条件分支、线程模式、异步任务语义的内容
- 行号和函数所属文件影响可信度的内容

### 行号使用规则

- 行号是定位辅助，不是论据本身。论据来自源码逻辑。
- 优先写 `文件 + 符号名`，例如 `RenderCore/Public/RenderingThread.h` 中的 `FRenderCommandDispatcher::Enqueue`。
- 如果写行号，必须在生成当次用 `rg` / `Select-String` / 打开文件验证。
- 不要大面积写精确行号范围。长函数可以写"在 `FDeferredShadingSceneRenderer::Render` 中，PrePass 位于 BasePass 之前"，必要时再补已验证范围。
- 如果源码和 `SOURCE_INDEX.md` 行号不一致，以源码为准，并更新索引。

### 追加规则

Gate 1 生成时只在 CoverageMatrix 或 handoff 中记录候选锚点；不要更新共享 `SOURCE_INDEX.md`，也不要把候选锚点写成正文重复块。只有 Gate 3 最终事实回归通过后，才把已重新验证、跨章节有复用价值的新发现追加到 `SOURCE_INDEX.md`。

---

## 生成流程

### 开始前

1. 读取 `OUTLINE.md` 确认当前文档的主题和核心问题
2. 读取本文件确认写作规范
3. 读取 `SOURCE_INDEX.md` 获取已有知识
4. **写边界表**：本篇必须讲透 / 只点到 / 不讲内容 / 高风险事实
5. **写教学入口**：先回答读者为什么要学这一篇、UE 的设计想法是什么、看源码前必须先理解哪条流程或算法
6. **确定主线**：这篇文档要带读者走一条什么样的路？
7. **确定贯穿案例**：选一个对象、命令、Pass 或资源贯穿全文，避免中途变成百科式罗列
8. **确认事实校准计划**：哪些 A 类事实需要源码复核，验证记录写入 CoverageMatrix 哪一行；正文是否只保留必要调试路标
9. **确认前序文档已覆盖的内容**：检查 OUTLINE 中的"核心问题"描述，里面标注了"01 已讲 X"等信息。已在前序文档中充分解释过的概念，本篇不需要重新引入——直接使用即可，就像读者已经理解了一样。

### 跨文档衔接规则

**不重复已讲透的内容**：如果一个概念在前序文档中已经用"是什么/为什么/有没有其他方式"的深度讲过，后续文档直接引用即可（"如 01 篇所述，FScene 使用 SoA 布局..."），不需要重新展开。

**但要衔接**：后续文档可以用 1-2 句话唤醒读者的记忆（"回忆一下，MeshDrawCommand 是在 Primitive 注册时预缓存的——本篇我们深入看这个缓存过程的内部实现"），然后直接进入新内容。

**首次出现 vs 深入展开的区分**：
- 01 篇对很多系统做了"首次引入 + 足够理解全局架构的深度解释"
- 后续专题篇对同一系统做"实现细节 + 算法 + 边界条件 + 调试路标"
- 两者的区别是：01 讲"它做什么、为什么需要、数据从哪来到哪去"；专题篇讲"它内部怎么做、用了什么算法、有哪些特殊情况处理、性能特征是什么"

**篇幅服从边界**：即使不重复前序内容，专题文档仍要把本篇边界内的实现细节讲透；但不再用 2000+ 行作为硬标准。如果发现篇幅较短，先检查是否漏了本职内容；如果没有漏，就接受较短篇幅。如果发现篇幅很长，先检查是否越界展开了后续篇内容。

### 生成中

1. 查 SOURCE_INDEX，补充调研新需要的源码
2. 对 A 类主线事实现场源码核对，不直接照搬索引
3. **先确定主线的骨架**（流程有几步，每步之间怎么衔接）
4. **先写流程/算法，再校准事实**：正文先完成模型解释；源码复核结果进入 CoverageMatrix，正文只保留必要概念符号和调试路标
5. 沿着骨架展开撰写，确保每段都在推进主线
6. 每写完一个大节，检查它是否回答了"这一节的目的是什么、为什么需要它、它怎么做、如果不这样会怎样、下一步去哪里"
7. 写完后通读一遍，删掉所有"不影响理解主线"的内容
8. 把边界外但有价值的材料沉淀到 `SOURCE_INDEX.md` 或后续篇备注，不塞进正文

### Gate 1 生成后（Codex 初稿 / 交给 Claude 前）

1. 做事实审校：逐个检查 CoverageMatrix 中的验证记录、函数名、行号、CVar、平台条件
2. 做边界审校：正文是否越界展开了后续篇内容，是否漏了本篇本职内容
3. 做叙事审校：每个小节是否能自然接到下一节，是否有"另外/此外/值得一提"式旁支
4. 做压缩审校：删掉重复解释、标签式定义、无主线贡献的深入段
5. 重建并落盘 `<章节名>_CoverageMatrix.md`，确认每个核心问题都是 `deep`
6. 输出 Claude handoff，明确后续需要教学优化和 Teaching Edit Report
7. 不更新 `OUTLINE.md` 状态、不追加 `SOURCE_INDEX.md`、不改章节完成状态，除非用户明确要求做与完成状态无关的维护

### Gate 3 验收后（Codex 最终事实回归通过后）

1. 更新 `OUTLINE.md` 状态
2. 更新章节头状态
3. 追加终审中新验证的事实和锚点到 `SOURCE_INDEX.md`
4. 如需调整大纲，修改 `OUTLINE.md`
5. 检查 `OUTLINE.md`、章节头、`SOURCE_INDEX.md`、更新日志和 CoverageMatrix 是否一致

### 质量审校顺序

生成后的审校必须按这个顺序做，不能只看错别字：

1. **主线审校**：用 5-10 句话复述全文流程。如果复述不出来，说明结构失败。
2. **教学顺序审校**：检查每章开头和每个大节是否先讲目的/想法/流程/算法，再给出调试意义。凡是先列函数、类、字段、源码路径或锚点块再解释目的的结构，必须重写。
3. **边界审校**：对照写前边界表，删掉越界内容，补足本职内容。
4. **事实审校**：打开源码核对所有 A 类事实。尤其核对函数所属文件、调用时机、线程语义、CVar 默认值、平台条件。
5. **读者疑问审校**：每个断言后问"读者会不会自然追问为什么/如果不这样会怎样/数据现在在哪"。会追问就补一句。
6. **压缩审校**：删掉重复段、空泛评价、只展示作者知道得多但不推进主线的内容。

---

## 质量检查清单

- [ ] 写前边界表已完成：必须讲透 / 只点到 / 不讲内容 / 高风险事实
- [ ] 开头先建立了目的、问题、UE 设计想法和主流程/算法，没有直接进入函数调用栈或结构体字段
- [ ] 每个大节都先回答 what/why/how/what-if，再给出必要调试意义；源码验证记录已移到 CoverageMatrix / SOURCE_INDEX / TeachingEditReport
- [ ] 本篇有一个贯穿案例或贯穿对象，不是多个概念轮流登场
- [ ] 文档有一条清晰的主线（数据流/执行流/决策流）
- [ ] 读者能从头读到尾不需要跳跃——每个小节自然衔接下一个
- [ ] 没有"知识点罗列"段落（如果出现了表格列举 10+ 个类/函数，考虑是否应该删减到只保留主线相关的）
- [ ] 没有"源码导览"段落（函数 A 调函数 B、结构 C 有字段 D），没有重复“源码锚点”块
- [ ] A 类主线事实已经重新打开源码核对，不只是引用 SOURCE_INDEX
- [ ] 每个 A 类技术声明在 CoverageMatrix 有源码验证记录（文件 + 符号名，必要时补行号）
- [ ] 没有编造的函数名、行号、参数、CVar 默认值、平台条件
- [ ] 行号不是唯一锚点；正文不依赖路径/行号清单来承担解释
- [ ] C++ 解释融入上下文，没有独立章节
- [ ] Unity 对比自然融入，没有独立章节
- [ ] 设计决策融入叙述，没有独立的"设计决策"段落
- [ ] 架构图/总结图放在文末或读者已理解主线之后
- [ ] 删掉了所有"另外/此外/值得一提"引导的旁支内容（或移到脚注）
- [ ] 通读时没有"这段跟我在走的主线有什么关系？"的感觉
- [ ] 本篇边界内的机制都讲到了"读者能回答 what/how/why"的程度
- [ ] 本篇边界内的机制都讲到了"如果不这样会怎样"的程度
- [ ] 边界外概念只讲当前角色，并标明后续篇，不越界展开
- [ ] 没有"蜻蜓点水"的段落——如果属于本篇，就讲清楚；如果不属于本篇，就只说明角色并指向后续
- [ ] **每个本篇首次深入的概念都展示了：具体数据内容、创建时序、和前后步骤的关系、读者可能的疑问及回答**
- [ ] **没有"贴标签式"的定义**——"X 是 Y"后面必须跟着"具体来说它包含/做了..."
- [ ] 篇幅由边界决定，没有为了行数注水，也没有漏掉本篇本职内容
- [ ] 完成主线审校、边界审校、事实审校、读者疑问审校、压缩审校

---

## 正面教材：引入一个新概念时应该做到什么程度

下面这个示例展示了"引入 FPrimitiveSceneProxy 和 FPrimitiveSceneInfo 这两个概念"时，正确的写法应该覆盖哪些内容。这不是唯一的写法，但它展示了**需要达到的深度标准**。

生成任何文档时，每当引入一个新的类/系统/机制，都要用类似的深度来处理——不一定用完全相同的结构，但必须覆盖同等层次的信息。

---

### 示例：如何讲清楚 Proxy 和 SceneInfo

> #### Proxy 具体持有什么
>
> `FStaticMeshSceneProxy` 的构造函数（`Engine/Private/StaticMeshSceneProxy.cpp`）通过 `FStaticMeshSceneProxyDesc` 从 Component 身上摘取了：
>
> - **Mesh 资源引用**（`FStaticMeshRenderData*` / RenderData 相关引用）——指向已经准备好的静态网格渲染数据。注意这里不是拷贝几何数据，而是引用渲染资源
> - **材质引用**（`TArray<UMaterialInterface*>`）——每个 LOD 的每个 Section 对应一个材质指针
> - **LOD 信息**（屏幕尺寸阈值、每个 LOD 的 Section 数量）
> - **渲染标志**（是否投射阴影、是否接收贴花、是否参与光照构建...）
> - **编辑器/调试状态**（选中、hover、hit proxy、调试显示等）
>
> Transform 和 Bounds 不要笼统写成“构造函数拷贝完”。UE5.7 的注册命令会在 Render Thread 上调用 `FPrimitiveSceneProxy::SetTransform()`，把 `LocalToWorld`、世界 Bounds、本地 Bounds、ActorPosition 写入 Proxy。也就是说，内容打包和位置标签在流程上是两步。
>
> 一个自然的问题：如果 Game Thread 修改了 Transform，Proxy 里的旧 Transform 怎么办？答案是：Game Thread **不会直接修改 Proxy**。它通过 `UActorComponent::MarkRenderTransformDirty()` 标记脏位，帧末批量走 `SendAllEndOfFrameUpdatesInternal()` / `FScene::UpdatePrimitiveTransforms()`，Render Thread 把变化排进 `PrimitiveUpdates`，最终由 `FScene::Update()` 统一提交。两个线程永远不会同时写同一个字段。
>
> #### SceneInfo 在 Proxy 基础上多出了什么
>
> `FPrimitiveSceneInfo` 不要写成“Render Thread 收到 Proxy 后创建”。UE5.7 中它是在 `FScene::BatchAddPrimitivesInternal()` 里、随 `CreateSceneProxy()` 之后一起 `new` 出来的，然后指针被打进 `AddPrimitiveCommand`。Render Thread 后续调用 `AddPrimitiveSceneInfo_RenderThread()` 时，只是把它排入 `PrimitiveUpdates`。它不是 Proxy 的子类，而是**包裹** Proxy 的容器——它内部持有一个 `FPrimitiveSceneProxy* Proxy` 指针。
>
> SceneInfo 比 Proxy 多出的东西，全部是 Renderer 自己的管理状态：
>
> - `FOctreeElementId2 OctreeId`——这个 Primitive 在场景八叉树中的位置索引，用于空间查询（视锥剔除）。Engine 模块不知道八叉树的存在，这是 Renderer 的实现细节
> - `TArray<FCachedMeshDrawCommandInfo> StaticMeshCommandInfos`——为这个 Primitive 预生成的 DrawCommand 缓存。每个 MeshPass（BasePass、DepthPass、ShadowDepthPass...）各有一组缓存
> - `int32 PackedIndex`——这个 Primitive 在 `FScene::Primitives` 数组中的紧凑索引。用于 GPU Scene 上传时快速定位数据
> - `TArray<FNaniteRasterBin> NaniteRasterBins[ENaniteMeshPass::Num]`——如果这是 Nanite 网格，它所属的光栅化 Bin 分类信息
>
> 为什么这些不放在 Proxy 里？因为 Proxy 定义在 Engine 模块——如果 Proxy 持有 `FOctreeElementId2`，那 Engine 模块就必须 include 八叉树的头文件，就产生了对 Renderer 的依赖。Engine 要保持对 Renderer 无知，才能支持多个渲染器实现（Deferred vs Mobile）共存。
>
> #### 创建时序
>
> 1. Game Thread: `UPrimitiveComponent::CreateRenderState_Concurrent()` → `FScene::AddPrimitive(Component)`
> 2. Game Thread: `BatchAddPrimitivesInternal()` → `CreateSceneProxy()` → `new FPrimitiveSceneInfo(Primitive, Scene)` → ENQUEUE 到 Render Thread
> 3. Render Thread: 执行 lambda → `Proxy->SetTransform()` → `Proxy->CreateRenderThreadResources()` → `AddPrimitiveSceneInfo_RenderThread(SceneInfo)` 排入 `PrimitiveUpdates`
> 4. Render Thread: `FScene::Update()` 统一 drain `PrimitiveUpdates`，SceneInfo 被加入 `FScene::Primitives` 数组 → 分配 PackedIndex → 插入八叉树 → 触发 DrawCommand 缓存生成
>
> 所以数据流是：**Component → Proxy（跨线程拷贝）→ SceneInfo（Renderer 包裹）→ 注册到 FScene 各种索引结构**
>
> #### 读者此刻应该能回答的问题
>
> - "如果我要加一个新的渲染属性（比如自定义的风力影响系数），我应该放在哪里？" → 放在 Proxy 里（因为它是 Engine→Renderer 的数据传递桥梁）。如果这个属性需要影响 DrawCommand 的缓存键，还需要在 MeshPassProcessor 中读取它
> - "移除时到底谁负责释放什么？" → Component 先 `ReleaseSceneProxy()` 断开 GT 侧引用；Render Thread 先 `DestroyRenderThreadResources()` 并把删除排进 `PrimitiveUpdates`；`FScene::Update()` 先撤销 octree / dense arrays / static mesh cache 等注册关系，再在末尾 setup task 中 `delete Proxy` 和 `delete SceneInfo`，HitProxy 引用单独走 `BeginCleanup()` 延迟清理
> - "为什么不直接让 SceneInfo 继承 Proxy？" → 因为 Proxy 定义在 Engine 模块，SceneInfo 定义在 Renderer 模块。继承意味着 Renderer 的类定义依赖 Engine 的类定义（这是允许的），但如果反过来 Engine 要知道 SceneInfo 的存在就不行了——现在 SceneInfo 包裹 Proxy，依赖方向是单向的

---

### 这个示例的核心教训

1. **具体数据 > 抽象标签**："拷贝了 Transform、Bounds、材质引用" 比 "拷贝渲染需要的数据" 有用 100 倍
2. **预判读者疑问并当场回答**：每当写完一个断言，问自己"读者会不会问'那如果...怎么办？'"——如果会，立刻回答
3. **展示创建时序**：谁先谁后、在哪个线程、触发条件是什么——这些是建立心智模型的关键
4. **说明"为什么不另一种做法"**：为什么不放在 Proxy 里？为什么不用继承？这些设计选择的理由能帮读者把理解从"记住是什么"升级为"理解为什么"
5. **给一个自检问题**：写完一个机制后，列出读者此刻应该能回答的具体问题。如果你自己发现回答不了，说明你没讲够

### 什么时候可以不展开到这个程度

- 这个概念在后续文档有专门的篇章深入（比如 01 里提到 RDG 但不展开，因为 05 是 RDG 专题）——此时**明确告知读者**"这里只需要知道 X，具体机制在第 05 篇"
- 这个概念对当前主线不构成理解障碍（比如提到"NaN 检查"不需要解释什么是 NaN）

除此之外，**每个首次出现的概念都要按上面的深度标准处理**。

---

## 边界判例：01_Architecture 终审

01 篇是全景入口，职责是让读者走通一帧主路径和模块边界，而不是提前讲完后续专题。终审时已经把以下内容压缩成"当前角色 + 后续出处"：

| 内容 | 归属 | 01 的处理 |
|------|------|----------|
| GBuffer 通道格式、ShadingModelID 细表 | 10 BasePass | 只保留 BasePass 写 GBuffer、Lighting 读 GBuffer |
| RDG Transient aliasing 算法、fence/barrier 细节 | 05 RenderGraph | 只保留 Execute 阶段会做生命周期分析和显存复用 |
| GPUScene Buffer layout、PrimitiveID 查找、增量上传策略 | 06 GPUScene | 只保留可见性后、PrePass 前上传动态场景数据 |
| MeshDrawCommand 排序键、PSO 缓存键、动态/静态差异 | 07 MeshDrawCommand | 只保留 FScene 缓存、FSceneRenderer 每帧筛选提交 |
| Nanite/VSM/Lumen/MegaLights 条件分支细节 | Part 3/4 对应专题 | 只保留它们是 Render() 阶段启停分支 |
| BackBuffer/Present、帧同步 | 15 PostProcessing / 03 ThreadModel | 只保留主线末尾位置和后续出处 |

通用教训：补充小节必须回答"如果删掉它，主线是否断裂"。不断裂的内容不要写成长节，只保留 2-5 句话说明当前角色，并明确交给后续篇。

2026-06-13 并行重构 02-05 的额外教训：多篇章节并行重构时，worker 只写各自章节文件；`OUTLINE.md`、`SOURCE_INDEX.md`、`GENERATION_GUIDE.md` 这类公共文件必须由主线程在所有章节回收并审查后最后统一写入。状态应按最低完成门槛处理：只要章节被重构回 Gate 1 稿，即使旧版曾终审完成，也必须降回“Codex 重构完成 / 待 Claude 教学优化”，直到 Claude 新报告和 Codex 最终事实回归重新通过。

2026-06-13 并行重构 11-15 的额外教训：Part 3 后半段章节会在同一帧顺序上互相引用输入/输出，worker 可以只读已落盘的相邻章节来修正前置假设，但仍只能写自己的章节。主线程回收后必须先清理并行时遗留的相邻章节缺席假设和缺失 handoff，再统一写 `OUTLINE.md`、`SOURCE_INDEX.md`、`GENERATION_GUIDE.md`。

2026-06-13 并行终审 01-05 的额外教训：最终事实回归通过后，公共维护文件也要作为验收对象检查一遍。至少确认 `OUTLINE.md` 状态、章节头 `当前状态`、`SOURCE_INDEX.md` 新增锚点和更新日志四者一致；如果共享索引里出现乱码、旧状态或只存在于 Teaching Edit Report 但未沉淀的 proposed additions，不能直接标完成。
2026-06-25 并行终审 01-05 的额外教训：Claude 报告刷新后，CoverageMatrix 也必须从 Gate 1 / Claude handoff 口径同步改为 Gate 3 回归口径，否则即使正文和源码事实已通过，`OUTLINE.md`、章节头、CoverageMatrix、`SOURCE_INDEX.md` 仍会互相冲突。并行代理只能给只读结论；主线程必须最后统一写章节头、CoverageMatrix 和共享维护文件，且公共文件应只沉淀已重新验证、跨章节有复用价值的事实，不把每个代理的临时抽查清单膨胀进共享索引。

2026-06-13 并行重构 16-20 的额外教训：Part 4/5 专题章节彼此不是同一帧顺序的连续阶段，但会共享 Nanite、Lumen、VSM、MegaLights、Material/Shader 等公共源锚点。worker 仍只能写自己的章节；主线程回收后必须统一检查章节头状态、`OUTLINE.md` 状态、`SOURCE_INDEX.md` 章节编号和更新日志，避免某些章节写成 Gate 1 初稿、某些写成重构完成而造成维护状态分裂。公共索引只沉淀经 worker 覆盖矩阵和主线程抽查都确认的 anchors，不把跨专题的未展开机制提前标成完成事实。

2026-06-13 并行重构 21-24 的额外教训：ShaderSystem、ComputeShader、Debugging、Optimization 会互相引用前置章节状态和公共工具锚点，worker 可能在相邻章节尚未落盘时写下临时假设。主线程回收时除了检查 coverage / handoff / verification records，还必须清理这类“尚未落盘”“后续会写”的过期假设，再最后统一写 `OUTLINE.md`、`SOURCE_INDEX.md`、`GENERATION_GUIDE.md`。公共索引只记录 Gate 1 已解释清楚的机制入口，优化和调试篇引用其他专题时只沉淀定位入口，不把算法细节重复写成新的完成事实。

2026-06-24 并行重构 06-15 的额外教训：当一次覆盖 Part 2 和 Part 3 多篇章节时，应优先按“每篇一个 worker”拆分，worker 只写自己的章节正文、CoverageMatrix 和本地 TeachingEditReport 降级说明；主线程等待全部章节回收后再统一写 `OUTLINE.md`、`SOURCE_INDEX.md`、`GENERATION_GUIDE.md`。如果旧版 11-15 曾经终审完成，新稿回到 Gate 1 后也必须把表格状态和 `SOURCE_INDEX.md` 对应章节标题降为“历史终审索引 / 当前新稿 Gate 1 待回归”，新核对锚点只留在各章 CoverageMatrix，不能提前沉淀为共享终审事实。

---

## 反面教材特征（如果你写出了这些，停下来重写）

1. **表格轰炸** — 连续出现 3 个以上的表格，列举类名/函数名/常量
2. **三段式循环** — "概念 → 实现 → 设计决策" 反复出现
3. **断裂的叙事** — 小节之间没有因果衔接，像是独立的百科词条
4. **代码墙** — 连续 50+ 行代码没有穿插解释
5. **前置架构图** — 文章开头就是一个大型 ASCII 架构图，读者还不知道为什么需要理解它
6. **源码导览开场** — 一上来就是 `FEngineLoop::Tick -> FViewport::Draw` 或某个结构体字段表，读者还不知道为什么要看这些源码
7. **"还有"/"此外"** — 如果你频繁用这些词，说明你在罗列而非叙述
8. **蜻蜓点水** — 提到了一个机制但只用一两句话带过，读者看完仍然不理解它。要么展开讲，要么不提
9. **导览式写作** — 每个系统平均分配 100 行，全部只讲到"是什么"层面。这是参考手册，不是教程
10. **贴标签式解释** — "X 是 A 模块的 B，Y 是 C 模块的 D"——只给了标签和归属，没有展示具体内容、创建时序、数据形态
