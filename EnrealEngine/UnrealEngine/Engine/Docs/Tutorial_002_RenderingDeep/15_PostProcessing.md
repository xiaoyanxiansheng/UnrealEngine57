# 15 PostProcessing：SceneColor 如何成为最终输出

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `05_RenderGraph.md`、`10_BasePass.md`、`12_Lighting.md`、`13_Atmosphere.md`、`14_Translucency.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `15_PostProcessing_CoverageMatrix.md`

## 开篇：一个让人栽跟头的“最后一步”直觉

第 14 篇结束时，场景里大部分可见内容已经写进了 `SceneColor`：不透明物体、光照、大气、水、一部分半透明都参与了这张 HDR 主场景颜色。直觉上你会觉得：剩下的就是“加点 Bloom、调个色、blit 到屏幕”，是收尾工作。

从 Unity 过来的 TA 尤其容易带着这个直觉：

> “所有 post effect 跑完，最后固定执行一次 Final Blit 到 BackBuffer 就完事了。”

在 UE 里，这个“固定最后一步”的想法会让你栽跟头。`SceneColor` 此刻其实**还不是**玩家看到的图像：它可能仍是主渲染分辨率，可能保留着 >1.0 的 HDR 能量，还没有时间抗锯齿、Bloom、ToneMapping、调试覆盖、空间放大，也还没写进当前 view family 的外部输出纹理。更关键的是——**最后写 BackBuffer 的那个 pass，是动态算出来的，不是固定的。** 它可能是 Tonemap，也可能是 FXAA、某个 Visualize、PrimaryUpscale、SecondaryUpscale 或某个插件回调。

所以本章要解决的 UE 设计问题是：

> **Renderer 不能把 `SceneColor` 当成一个固定的“最后 blit 源”，因为最终输出之前还有一组按视图、show flag、材质、插件、TSR 和 debug 状态动态变化的 pass。UE 因此把后处理组织成一条 RDG 中的可变颜色流：每个 pass 接住当前 `SceneColor`，产出新的当前颜色；只有最后一个真正启用的 pass，才被授权写入外部输出目标。**

本章只追踪这条主线，不写成后处理效果百科。

### 先约定几个反复出现的名词

- **`SceneColor`（主场景颜色）**：贯穿后处理的颜色资源。它表达的是场景参考的线性光能，但纹理里的数值通常已经乘过当前 view 的 `PreExposure`，以免极亮或极暗的场景值长期挤在浮点格式的极端范围。它在 RDG 里还有“写入目标”和“后续可读表达”的区别；后处理消费的是满足 shader 读取合同的那一份。
- **`ViewFamilyTexture` / 本章所说的 BackBuffer**：后处理最终写入的**外部**输出纹理，由 viewport、scene capture 等 view family render target 提供。它不是 swap chain 的 present 本身——present 属于更外层的 RHI 流程。
- **`TOverridePassSequence`（覆盖式 pass 序列）**：决定“谁是最后一个有效 pass、由谁拿到最终输出权”的机制。它是“动态最后一步”的实现核心。
- **场景线性语义、PreExposure 数值与显示编码**：这不是简单的“HDR / LDR”二分。ToneMapping 前的颜色在语义上仍表示场景线性光能；存储数值可能经过 `PreExposure`；色彩处理还要知道项目 working color space。ToneMapping 边界之后，颜色才根据目标设备进入 sRGB、Rec.709、PQ、scRGB 或线性 capture 等不同输出表达。

把这些名词再按“资源护照”钉一下，会更接近真实调试时的思路：

| 名词 | 它此刻装了什么 | 谁拥有或推进 | 下一步交给谁 | 调试时先问 |
| --- | --- | --- | --- | --- |
| resolved HDR `SceneColor` | 可供 shader 读取的、场景参考线性且通常已 pre-exposed 的主场景颜色 | Render Thread 上的 RDG 图 | TSR、曝光统计、Bloom、Tonemap | 语义是场景光能还是显示编码？数值对应哪个 `PreExposure`？ |
| 当前 `SceneColor` | 某个 pass 刚产出的 texture / slice 与有效 view rect | 刚建立该输出的后处理 pass | 下一个 enabled pass | 分辨率、rect、working space、pre-exposure 和输出设备状态是什么？ |
| `ViewFamilyOutput` | 指向 `ViewFamilyTexture` 的 render target 描述 | `TOverridePassSequence` 授权的最后 pass | RDG external texture | 最后 pass 是否真的拿到了 external output？ |
| `ViewFamilyTexture` | viewport / scene capture 提供的外部 render target | view family render target；RDG 只是临时注册访问 | Renderer 外层的 viewport / RHI 提交流程 | rect、load action、多 view 是否正确？ |
| TSR / TAA history | 跨帧颜色、guide、metadata 及其 rect、格式和 pre-exposure 语义 | view state；当前图用 extraction 写回 | 下一帧 temporal pass | 历史是否兼容当前 view，还是应当按 camera cut 重新开始？ |

注意这里有两个容易混淆的“拥有”。当前 `SceneColor` 由 RDG 图内的 pass 产生和接力；`ViewFamilyTexture` 的底层纹理却来自 view family 外部目标。后处理能写它，但不拥有 present，也不决定 swap chain 什么时候显示。

## 框架问题：动态的最后一步意味着什么

如果你来自 Unity SRP，可以把后处理类比成 camera color post stack——这个类比能帮你起步。但 UE 的关键差异不在“有哪些效果”，而在“谁是终点”：

- Unity 的直觉是固定 Final Blit；UE 是 `TOverridePassSequence` 在运行时算出“最后一个 enabled pass”，由它直写外部目标。
- 这条链是可插拔的：motion blur 可能关闭，Tonemap 可能被替换，FXAA/SMAA 可能启用，debug view 可能插入，空间 upscale 可能是 primary、secondary 或第三方插件，`SceneViewExtension` 还能在多个节点后追加回调。

这就带来一个贯穿全章的判断纪律：**“某个 pass 产生了正确的 `SceneColor`”不等于“画面已经输出”。** 你还得确认：这个 pass 是否被判定为最后有效 pass、它的 override output 是否指向 view family 外部目标、多 view 时 load action 是否允许保留前一个 view 的输出区域。

## 心智模型：一条不断被重写的颜色流

本章只要你跟住一件事：**“当前颜色”是一枚沿 pass 序列移动的资源句柄，不是一张永久纹理被所有阶段随意原地修改。** 一个 pass 读取上一段颜色，建立一份满足新分辨率、新颜色语义或新输出目标的结果，然后把“当前颜色”这个角色交给它。上一份资源可能继续被 visualizer 或 history 使用，也可能在最后一个消费者之后由 RDG 回收。

为了把模型钉在地上，本章用同一个画面贯穿全章：

> 摄像机横移扫过一块 HDR 霓虹招牌，招牌前有一片设置为 **After DOF** 的半透明玻璃。

这个画面会依次暴露后处理链最重要的五个问题：

- **横移** → TSR 需要在正确位置读取颜色、深度、速度和上一帧 history。
- **霓虹高亮** → Bloom 要在 tonemap 前从 HDR 颜色里提取并多级模糊。
- **HDR 颜色** → ToneMapping 要把场景线性空间压到显示输出空间，ACES 变换在这里落地。
- **After DOF 玻璃** → 第 14 篇留下的 separate translucency 不能随便合成，必须在后处理时机消费。
- **调试视图** → Visualize pass 不是旁路截图，而是可能接管最终输出的链上节点。

用一条数据流概括，本章讲的是：

```text
Resolved、pre-exposed scene-linear SceneColor
  -> DOF / AfterDOF translucency 建立 temporal 输入
  -> TSR / TAA 校验 history、重投影并建立新历史
  -> global eye adaptation / local-exposure 空间修正，并排队 average-local-exposure readback
  -> MotionBlur / BeforeBloom material
  -> Gaussian 或 FFT Bloom 形成 HDR 旁支
  -> CombineLUT 组织 working-space grading、tone curve 与 output transform
  -> Tonemap 应用曝光、Bloom、LUT，进入目标输出表达
  -> AA / AfterTonemap material / Debug / Visualize / spatial upscale
  -> 动态最后 pass 写 ViewFamilyTexture
  -> RDG execute、RHI recording、platform command formation、Queue Submit、GPU completion、present 继续推进
```

把这条颜色流按“谁生产哪一个资源版本”展开，后文的分支就有统一坐标：

| 阶段 | Producer 与新产物 | 下一 Consumer | Lifetime 与条件分支 |
| --- | --- | --- | --- |
| 后处理入口 | scene renderer 把场景写入结果推进为 shader-readable、pre-exposed HDR `SceneColor`，并把 `ViewFamilyTexture` 注册成 external output | pass sequence、TSR/曝光/Bloom 等首批消费者 | 当前颜色属于本 RDG 图；需要跨帧的输入必须另走 extraction；resolve 是否产生独立工作取决于资源形态与 MSAA |
| Pass sequence | `AddPostProcessingPasses` 维护当前颜色、原始颜色与 `ViewFamilyOutput`，`TOverridePassSequence` 把 override output 授给最后 enabled pass | 下一个 enabled pass，或 external `ViewFamilyTexture` | pass 是否存在由 view/show flag/插件/材质/debug 条件决定；非最后节点产出新的临时颜色版本 |
| Temporal | TSR/TAA 消费当前 HDR color、depth、velocity、透明层和有效 history，产出本帧当前颜色；需要未来帧的数据另行 extraction | 曝光、MotionBlur/Bloom、Tonemap，以及下一帧 temporal pass | 本帧输出活在当前图；history 由 view state 跨图持有，并受 camera cut、rect/format、view identity 与 exposure 尺度检查 |
| Bloom / exposure | 曝光与 local-exposure pass 产出本帧标量或空间修正；Gaussian/FFT Bloom 从 Tonemap 前 HDR 高亮形成旁支 | Tonemap 与相关 visualizer | Gaussian、FFT 或关闭由实际资格和强度门控决定；临时 Bloom 纹理活到 Tonemap/visualizer 最后读取，跨帧平均曝光只通过 readback/manager 路径继续 |
| LUT / Tonemap | `CombineLUT` 产出或复用与当前颜色配方匹配的 LUT；Tonemap 把 HDR 当前颜色、曝光与 Bloom 推进到目标输出合同 | 后续 AA、AfterTonemap material、debug/visualize、upscale，或 external output | LUT 可以是 view-state 持久资源、primary stereo 复用或本图临时资源；输出可能是 SDR、PQ、scRGB 或 linear capture，不以“Tonemap 后必为 LDR”为前提 |
| Debug / Visualize | 启用的链上节点读取所需阶段资源并产出新的当前颜色 | 后续节点，或在它成为最后 enabled pass 时写 external output | 输入必须匹配 pre/post-tonemap 语义、rect 与分辨率；未启用时不产生这一版本 |
| 外部交接 | 最后 enabled pass 按当前 view rect 与 load action 写 `ViewFamilyTexture` | Renderer 外层的 RDG/RHI/viewport 路径 | external texture 不由后处理临时池拥有；多 view 必须保留已写区域；此处只完成颜色资源交接，不等于 Queue Submit、GPU completion 或 Present |

表中的“当前颜色”不是固定对象名，而是最近一个 enabled producer 交出的 texture/slice/rect 与颜色合同。某个可选分支关闭时，consumer 直接接住上一份合法版本；它不会凭空得到一个同名但未生产的中间结果。

## Unity → UE 对照

| Unity 经验 | UE 对应 | 关键差异（别想当然的地方） |
| --- | --- | --- |
| 固定 Final Blit 到 BackBuffer | `TOverridePassSequence` 动态最后 pass | 终点是算出来的，可能是 Tonemap/AA/Visualize/Upscale |
| Camera color post stack | RDG 内不断重写的当前 `SceneColor` | 没有全局永久 camera RT，每段只拥有自己产出 |
| MSAA resolve 后给 post stack | `SceneColor.Target` → `.Resolve` | 写入态与可采样态分离，后处理读 Resolve |
| TAAU / temporal upscaler 在链尾 | TSR 在链**中段**（Bloom/Tonemap 前） | TSR 处理 HDR 颜色与 history，不是单纯放大 |
| Color Grading LUT + Tonemapper | `CombineLUT` + `Tonemap` | ACES 与输出设备变换编码进 LUT 分支 |
| Frame debugger overlay | 链上有序的 Visualize 节点 | visualizer 读/写当前颜色，可能成为最后输出 |
| 平台 present | RHI/viewport present（章外） | 后处理只写 `ViewFamilyTexture`，不负责 present |

这张表只建立方向感，每个差异由下文展开。

## 本篇边界

先说本章**正向负责**什么：它经营从“已解析 HDR `SceneColor`”到“写入外部 `ViewFamilyTexture`”这一整条后处理颜色流，以及 Render 尾部把跨帧 history、临时资源、外部输出各自收口的过程。这是 Part 3 追踪的 `SceneColor` 真正变成最终图像的最后一段。

明确了正向职责，再划出不展开的部分：

- TSR 内部的时间重建算法、history 重投影细节。
- Bloom 的 shader 数学与采样核细节。
- ACES 标准本身的色彩科学细节。
- swap chain present、viewport/RHI 提交流程。
- Nanite / Lumen / VSM 等系统的内部算法（仅在它们作为 visualizer 或输入时提及接口）。

源码锚点集中在 `15_PostProcessing_CoverageMatrix.md`，正文不堆密集源码引用。

## 本篇必须能回答

读完本章，你应该能不查源码地回答下面这些问题：

1. 后处理读的是哪一张颜色？为什么必须先确认 `SceneColor` 的 resolve 形态？
2. UE 没有固定 Final Blit，那么“谁写 BackBuffer”是怎么决定的？
3. After DOF 的玻璃为什么要跨到后处理才合成？它的时机由谁掌握？
4. `PreExposure`、当前 eye adaptation、local-exposure 空间修正和 average-local-exposure 跨帧标量为什么不能合并？
5. TSR 在链里的位置为什么是中段？哪些条件会让 history 失效或重建？
6. Gaussian Bloom 和 FFT Bloom 分别适合什么问题？`BM_FFT` 还要经过哪些资格、强度与 spectral-cache 身份检查？
7. working color space、cached settings、持久/临时 LUT、ACES/tone curve、output gamut 和设备编码分别在哪一步成立？
8. Debug / Visualize 是“画面外贴的 UI”吗？它会影响最终输出吗？
9. 两个 view 共用一个外部目标时，为什么后一个 view 必须保留前一个 view 的区域？
10. `AddPostProcessingPasses` 返回之后，这一帧就 present 了吗？还差哪些完成深度？

哪一问答得犹豫，对应小节就是重点。

## 1. 后处理接住的是可读的 pre-exposed scene-linear `SceneColor`

后处理的第一个问题不是 Bloom 或 Tonemap，而是：它应该读哪张颜色图？

在 UE 的 scene texture 体系里，`SceneColor` 可以同时有 target 与 resolve 两种形态。主场景 pass 写入的是渲染目标形态；当存在 MSAA 或 target/resolve 分离时，后处理要读的却必须是稳定的单采样 resolved 颜色。否则 screen-space pass、history 读取和后续 fullscreen pass 会面对“不知道应该采样哪一份颜色”的边界错误。

因此，进入后处理前，Renderer 会先确认 `SceneColor` 是否需要 resolve。需要时，它添加相应的 RDG 工作，把写入目标变成后续 shader 可读的表达；不需要时，就让可读角色直接指向已有纹理。这里的 **resolve 是逻辑可读性合同，不应一律想象成一次昂贵的全图物理复制**：是否需要独立资源、是否真的发生样本合并、底层怎样过渡，取决于纹理形态、MSAA 和平台实现。这个动作的所有者是 Render Thread 上的一帧 renderer 主流程；它不改变后处理链的概念顺序，只把输入从“刚被场景 pass 写过”推进到“后续 pass 可以按约定读取”。

此时还必须把四层经常被混成“HDR”的含义拆开：

| 层次 | 回答的问题 | 在霓虹案例中的含义 |
| --- | --- | --- |
| 场景参考线性语义 | 数值是否仍按场景光能成比例 | 招牌比墙亮十倍，颜色仍表达约十倍场景能量，而不是十倍屏幕亮度 |
| `PreExposure` 数值表达 | 这些线性值以什么倍率存进有限精度纹理 | 本帧 scene color 通常已经乘了当前 view 的 `PreExposure`，把数值搬到更稳定的范围 |
| working color space | RGB 三个分量对应什么色域原色和白点 | 调色、LUT 和色域变换必须知道“这三个数代表哪套颜色坐标” |
| shader 可读资源状态 | 哪个纹理、哪个 slice、哪个 rect 现在允许被采样 | resolve/访问状态正确后，TSR、曝光、Bloom 才有合法输入 |

这四层不是替代关系。一个纹理完全可能“语义上是场景线性、数值上已 pre-exposed、坐标上属于项目 working space、资源状态上却还不能被下一个 shader 读取”。如果只写“这是一张 HDR 图”，你无法判断错误究竟来自能量语义、曝光倍率、色域，还是资源交接。

此时有两类消费者被区分开：

| 消费者 | 需要的颜色形态 | 生命周期 |
| --- | --- | --- |
| 当前帧后处理 | resolved HDR `SceneColor` | 当前 RDG 图内不断被重写 |
| 下一帧 screen-space / history | tonemap 前的 scene color 或 depth | 通过 extraction 活到下一帧 |

这就是为什么后处理前的 resolve 和 history extraction 不能混在一起理解。Resolve 解决“当前图内谁能读”的问题；extraction 解决“当前图结束后谁还活着”的问题。霓虹招牌在这里仍表达 scene-linear 能量，但调试数值时必须同时带上 `PreExposure`，不能把纹理里的存储值直接当作未经缩放的物理亮度。后续 TSR、曝光统计和 Bloom 都围绕这份输入工作。

这个阶段还会把 view family 的外部 render target 导入 RDG，形成后处理最终可能写入的 `ViewFamilyTexture`。它的所有权不属于后处理临时纹理池，而是来自 viewport、scene capture 或其他 view family render target。RDG 只把它注册为 external texture，并在图结束时让它回到可作为渲染目标的访问状态。换句话说，本章所谓 BackBuffer，不是 swap chain present 本身，而是 Renderer 后处理阶段能写到的外部输出纹理。

调试失败时，先问三件事：当前帧有没有得到 resolved `SceneColor`；后处理输入是否仍是 HDR scene color 而不是 tonemap 后颜色；最终输出纹理是否成功作为 external texture 导入 RDG。若这三件事任何一件错了，后面的效果看起来都可能“算法错”，但根因其实是输入或输出边界错。

## 2. AddPostProcessingPasses 把当前颜色变成可交接的状态

`AddPostProcessingPasses` 是桌面 deferred 后处理主入口。它不是简单的效果列表，而是建立三种角色：

| 角色 | 它持有什么 | 为什么需要 |
| --- | --- | --- |
| 当前 `SceneColor` | 一张 `FScreenPassTexture`，随 pass 更新 | 让每个 pass 都能读上一段结果并交出下一段结果 |
| 原始 `SceneColor` | 后处理前的输入副本 | 给复杂度、GBuffer、debug visualizer 回看原始场景 |
| `ViewFamilyOutput` | 指向 external `ViewFamilyTexture` 的 render target 描述 | 让最后一个有效 pass 可以直接写最终输出 |

这里的核心不是 `SceneColor` 变量名，而是所有权语义：每个 pass 只拥有自己产出的那一段结果；下一段接管以后，上一段可以留作 debug 输入、被 history extraction 保存，或作为 RDG 临时资源在图执行时释放/复用。UE 不需要一个全局永久 camera color RT 承担所有角色。

但后处理链是动态的。Motion blur 可能关闭，Tonemap 可能被替换，FXAA/SMAA 可能启用，debug view 可能插入，空间 upscale 可能是 primary、secondary 或第三方插件。UE 用 `TOverridePassSequence` 解决“谁负责写最终输出”的问题：

```text
先声明每个有序 pass 是否启用
  -> Finalize 计算最后一个有效输出点
  -> 每个 pass 按顺序执行前询问：
       如果我是最后输出点，就把输出目标改成 ViewFamilyOutput
       如果不是，就写一张临时 RDG 纹理并交给后续 pass
```

这条规则让后处理链既可插拔，又不会丢失最终输出所有权。`SceneViewExtension` 的 after-pass 回调也接入这套机制：扩展可以在 MotionBlur、Tonemap、FXAA、SMAA 或 VisualizeDOF 后追加处理，但如果它成为最后输出点，也必须由同一套 override 机制写入 `ViewFamilyOutput`。

这一步最常见的误判，正是开篇那条直觉：把“某个 pass 产生了正确的 `SceneColor`”等同于“画面已经输出”。在 UE 里还必须确认：这个 pass 是否被 `PassSequence` 判定为最后有效 pass；它的 override output 是否指向 view family 外部目标；多 view 时 load action 是否允许保留前一个 view 的输出区域。

用同一个霓虹画面看三种链尾，输出边界会更清楚：

```text
情况 A：没有后续 AA / Visualize / Upscale
  Tonemap 接住 HDR SceneColor + Bloom + LUT
  -> 直接拿到 ViewFamilyOutput
  -> 写 external ViewFamilyTexture

情况 B：Tonemap 后还开了 FXAA / SMAA / SecondaryUpscale
  Tonemap 只写临时显示空间 SceneColor
  -> 后续 pass 接住它
  -> 最后一个 enabled pass 才写 ViewFamilyOutput

情况 C：Temporal upscaler visualizer 或某个 after-pass callback 在最后
  前面的颜色可能全都正确
  -> visualizer / callback 接管当前颜色
  -> 它成为最终输出者
```

因此调试“Tonemap 看起来没问题但窗口里不是那张图”时，先不要怀疑 Tonemap shader。先看 `PassSequence` 的最后有效节点是谁，以及这个节点有没有把自己的输出目标从临时纹理改成 `ViewFamilyOutput`。UE 的输出权是一枚会沿 pass 序列移动的令牌，不是一张固定叫 BackBuffer 的全局颜色图。

## 3. After DOF 玻璃说明半透明为什么跨到后处理

第 14 篇已经解释过，某些半透明不会立刻合成进主 `SceneColor`，而是按 pass 类型写入 separate translucency 资源。后处理接到的 `TranslucencyViewResourcesMap` 是一个只读视图：它告诉后处理“这些透明内容已经画好，但还没有按最终时机并入主颜色”。

我们的玻璃设置为 After DOF，原因不是材质想晚一点显示，而是它的视觉正确性依赖后处理时序。若它在 DOF 前合成，它可能被景深错误模糊；若它在 TSR 输入之外合成，它可能和主场景的时间重建不同步；若它在 motion blur 的错误一侧合成，它可能参与或逃过运动模糊，产生和材质意图不一致的结果。

UE 的分工是：

| 阶段 | 责任 |
| --- | --- |
| 半透明渲染阶段 | 把 AfterDOF / AfterMotionBlur 等结果写入资源表，不决定所有后处理细节 |
| 后处理阶段 | 根据 DOF、TSR、motion blur 是否启用，选择把资源交给 TSR、DOF pass，或显式合成到新的 `SceneColor` |

这就是 separate translucency 的 owner/thread 边界：资源在 Render Thread 的透明 pass 中产生，生命周期仍属于当前 RDG 图；后处理只消费它的 RDG texture 视图，不回头修改半透明 draw 的决策。它解决的是“透明内容的绘制时机”和“透明内容的合成时机”需要解耦的问题。

如果 After DOF 玻璃出现拖影、锐利度不对或没有参与正确的 Bloom/Tonemap，调试时不要先猜 shader。先沿资源时机查：它是否进入了 AfterDOF 资源；后处理是否把它交给 TSR 处理；若没有交给 TSR，是否在 DOF 后显式合成成新的 `SceneColor`；合成之后的当前颜色是否继续进入 Bloom 和 Tonemap。

## 4. TSR：把“本帧样本”变成“可继续处理的当前颜色”

TSR 容易被误解成链尾的放大器。UE5.7 的桌面 deferred 主链中，它处在 DOF / AfterDOF 透明处理之后、Motion Blur、Bloom 和显示变换之前。这个位置是有目的的工程选择：TSR 不只改变尺寸，它还要在**场景线性、pre-exposed 的颜色域**里，用本帧几何证据和跨帧历史重建更稳定的当前颜色。

如果把 TSR 放到 Tonemap 之后，它面对的是已经被曲线压缩和设备编码影响的颜色。高亮能量被压缩后，历史拒绝、反走样和重建很难再区分“真实的场景亮度变化”和“显示曲线造成的数值变化”；Bloom 也只能基于不完整的高亮能量工作。反过来，如果在 DOF / 需要参与 temporal reconstruction 的透明层之前执行，背景和玻璃会各自形成不同的时间稳定性。UE 选择当前位置，是在重建输入完整性、后续效果稳定性和管线成本之间取平衡。

### 4.1 TSR 消费的不只是一张颜色图

| 数据 | 它携带的证据 | 缺失或错误时会怎样 |
| --- | --- | --- |
| 当前 `SceneColor` | 本帧 jitter 后真正采到的场景线性颜色 | 没有本帧事实，只能错误复用历史 |
| `SceneDepth` | 表面前后关系和遮挡边界 | 前后景历史会互相污染 |
| `SceneVelocity` | 当前像素对应的表面从上一帧移动到哪里 | 横移时出现重影、拖尾或历史错位 |
| TSR history | 过去帧已经重建出的颜色与辅助信息 | 每帧退化成重新开始，稳定性下降 |
| view rect、jitter、分辨率与格式 | 当前输入、输出和历史怎样对齐 | 读错 texel、边缘拉伸或直接判历史不兼容 |
| `PreExposure` 关系 | 历史数值和本帧数值如何换到同一曝光尺度 | 曝光变化时历史会忽明忽暗 |
| PostDOF translucency | 需要和主场景共享 temporal 处理的透明层 | 玻璃与背景的稳定性、锐利度或拖影行为分裂 |

TSR 的输出也不是一个名字就能概括。面向当前帧，它交出 `FullRes` 当前颜色以及可供低分辨率消费者复用的结果；面向未来帧，它建立新的 history，并在 RDG 图结束时提取到持久 view state。**输出分辨率和 history 分辨率是两个合同**：当前颜色必须满足后续后处理的 extent/rect，history 则必须满足下一次 temporal 重建所需的尺寸、格式和元数据。它们可能有关联，但不能假定永远相同。

### 4.2 history 不是“有纹理就有效”

上一帧留下了一张 history，并不代表本帧应该使用。至少要逐项确认：

1. view 是否有可持久化的 view state，history 是否真的存在；
2. 是否发生 camera cut，或其他明确要求重新开始时间积累的状态变化；
3. 历史的格式、尺寸、有效 rect 和当前 temporal 配置是否兼容；
4. 历史表达使用的曝光尺度能否通过 pre-exposure correction 与本帧对齐；
5. 当前路径是否允许使用或恢复一份仍兼容的保留历史。

camera cut 必须让旧历史失效，因为“上一帧这个像素来自哪里”的连续运动假设已经断裂。格式或尺寸不兼容也不能硬采样，否则不是质量稍差，而是数据解释错误。`PreExposure` 变化则不同：它通常不是几何连续性断裂，因此可以在 history 读取时做倍率校正，把旧数值换到本帧的表达尺度，而不必仅仅因为亮度适应变化就丢掉所有时间信息。

某些配置可以从仍兼容的保留状态中恢复历史，而不是机械地只认紧邻上一帧；这仍然受有效性检查约束。它是提高短暂中断后稳定性的工程选择，不是 temporal reconstruction 的硬约束。更保守的实现可以一律在中断后重建，代价是更多闪烁和收敛时间；更积极的恢复策略能保留稳定性，但必须承担错误复用旧场景状态的风险。

### 4.3 横移霓虹招牌：一帧内到底换了什么状态

```text
第 N 帧开始：
  SceneColor 保存 jitter 后的新样本，并使用第 N 帧的 PreExposure 表达
  Depth / Velocity 描述霓虹、墙面和玻璃的本帧几何关系
  第 N-1 帧 history 带着自己的 rect、格式和曝光语义进入图

TSR 内部合同：
  先判断历史是否存在且兼容
  -> 把可用历史重投影到本帧位置，并校正 pre-exposure 差异
  -> 在遮挡、运动和新显露区域拒绝不可信历史
  -> 用本帧样本补足或替换

TSR 交接：
  FullRes 成为新的当前 SceneColor
  -> 低分辨率结果可供后续曝光 / Bloom 等阶段复用
  -> 新 history 被 extraction 到 view state，供未来帧重新注册
```

这里有三种不同的“完成”。TSR pass 被加入 RDG，只表示工作已声明；RDG execute 把依赖兑现为 RHI 工作，经过 Platform Queue Submit 与 GPU 消费后，当前帧输出才真正被产生；extraction 建立的跨图资源在下一帧重新注册并通过有效性检查后，history 生命周期才闭合。只看到一个 pass 名字，不能证明 history 已经稳定传到了下一帧。

### 4.4 为什么不总用 TSR

| 方案 | 优势 | 代价或局限 | 更合适的场景 |
| --- | --- | --- | --- |
| TSR | 结合历史、深度、速度和较低渲染分辨率重建高质量输出 | 依赖可靠 velocity/history，快速变化、透明和细线条需要专门处理；有跨帧状态成本 | 需要时间超分与 UE 主渲染链深度整合的实时场景 |
| TAA / TAAU | temporal 思路较直接，配置和成本目标可以不同 | 重建质量、锐化或放大能力与 TSR 不同，仍有 history 问题 | 不需要 TSR 全套重建能力或目标平台预算不同 |
| 纯空间 upscale | 没有跨帧历史，不会产生历史拖影；camera cut 天然简单 | 不能利用过去样本，亚像素细节和稳定性通常更弱 | UI、频繁不连续镜头、缺少可靠 velocity 或低成本路径 |
| 第三方 temporal upscaler | 可选择不同质量/硬件生态和专用优化 | 必须遵守 UE 的输入、输出、曝光、rect 与 history 接口；插件版本和平台支持成为新约束 | 项目已有明确平台目标和供应商能力评估 |

不存在脱离目标的“最好”。TSR 的价值来自它把时间重建放进 UE 已有的 scene color、velocity、view state 和后处理交接里；代价也正是它更依赖这些合同。调试时若 `FullRes` 没成为后续当前颜色，查输出交接；若画面每帧重新收敛，查 history extraction/有效性；若横移拖影，查 velocity、遮挡拒绝和透明层时序，而不是只调 sharpen。

## 5. 曝光：`PreExposure`、eye adaptation 与 Local Exposure 的两份数据

新手常把曝光理解成 Tonemap 里的一个乘数。UE 的主链实际上至少分成三类责任：**本帧渲染采用的 `PreExposure` 数值尺度、根据当前画面更新的全局 eye adaptation 状态、以及 local exposure。** 其中 local exposure 还必须拆成两份数据：当前 RDG 图内按屏幕位置变化的空间修正，以及经 GPU readback 反馈给后续 `PreExposure` 的 average-local-exposure 标量。它们相关，但 owner、数据形状、完成时刻和生命周期不同。

| 状态 | 数据形状 | 何时成立 | 谁消费 | 生命周期 |
| --- | --- | --- | --- | --- |
| `View.PreExposure` | 每个 view 的标量倍率 | 本帧场景绘制前，通常依据上一轮可用曝光状态确定 | 场景 shader、TSR history 对齐、后处理 | 固定服务于这一帧的 view |
| eye adaptation | 全局曝光状态/缓冲 | 后处理读取本帧亮度分布后更新 | Tonemap、Bloom 设置、下一帧 `PreExposure` 决策 | 通过 view state 跨帧 |
| local-exposure 空间修正 | 随屏幕位置变化的低频亮度、双边网格/模糊亮度与局部修正参数 | 后处理从当前颜色与曝光参数建立 | Bloom 设置、Tonemap、local-exposure 可视化 | 主要属于当前 RDG 图，只服务已经建立它的这次 view 处理 |
| average local exposure 反馈 | 每个 view 的平均局部曝光近似标量 | GPU 计算后随 exposure 数据异步 readback；CPU 只在结果可用时接收 | view-state exposure manager，后续可用帧的 `UpdatePreExposure` | 跨帧持久；延迟取决于 readback 就绪，不承诺固定一帧 |

### 5.1 为什么需要 `PreExposure`

真实场景亮度跨度可以极大：暗室里的墙和太阳反射可能相差许多数量级。浮点纹理虽然能存 HDR，却不意味着在所有数量级上都有同样有效精度。如果整条场景管线长期把数值挤到格式的极大或极小区间，量化、溢出风险和后续运算稳定性都会变差。

`PreExposure` 的作用，是在渲染本帧时先把场景线性数值整体搬到更合适的数值范围。它**不把场景线性语义变成显示颜色**，也不等于最终观众曝光；它只是改变存储/计算尺度。因为本帧开头还没有完整分析完本帧图像，所以 UE 通常使用上一轮已经可用的曝光状态来决定本帧 `PreExposure`。在启用相关路径时，这份状态不只包括最近可用的 global exposure，也会纳入 exposure manager 持有的 average local exposure 近似。固定曝光、覆盖值或没有可用 view state 的路径会采用各自的明确分支，不能把“上一帧结果”写成无条件公式。

这里的因果约束不是随意延迟：你不能先得到本帧完整亮度统计，再回头要求已经执行过的 BasePass 和 Lighting 使用它；异步 GPU readback 也不保证 CPU 恰好在下一帧开始前拿到结果。因此更准确的说法是“使用最近一次已经可用且身份匹配的曝光反馈”，而不是“固定延迟一帧”。

不做 pre-exposure 也不是数学上绝对不可能。替代方式包括使用更高精度、更大动态范围的中间格式，或让每个阶段自行处理极端数值；代价是带宽、显存和 shader 复杂度上升，而且跨 pass 更容易出现尺度不一致。UE 选择统一 view 级倍率，是用一个明确合同换取大量阶段的数值稳定和较低格式成本。

### 5.2 全局 eye adaptation 回答“整幅画面应该暴露多少”

eye adaptation 读取当前画面的亮度统计，结合测光方式、曝光补偿、适应速度和上下限，推进一个全局曝光状态。它不是瞬间把 `View.PreExposure` 改写成新值：当前帧早期场景颜色已经按既定倍率产生；新状态用于当前显示变换，并成为后续帧 pre-exposure 的依据之一。

如果关闭自动曝光或使用手动/固定曝光，系统仍需要给后续 shader 一个明确曝光合同，只是这个状态不再随图像统计自动变化。手动曝光适合需要严格镜头匹配、离线对照、产品展示或调试数值的场景；自动曝光更适合玩家从室内走向室外这类动态亮度范围，但会引入跨帧适应和画面“呼吸”的设计问题。

### 5.3 local exposure 回答“同一幅画面不同区域是否需要不同修正”

全局曝光只有一个值。若镜头同时看见暗室内部和窗外霓虹，任何单一曝光都会牺牲一侧：照顾室内，窗外容易压成大片高亮；照顾窗外，室内细节会沉入黑色。local exposure 从低频亮度结构建立空间变化的修正，让亮区和暗区可以获得不同程度的局部压缩或提升。

这不是“每个像素各自自动曝光”。若局部修正追着细纹理或噪声快速变化，会制造光晕、局部对比反转和时间闪烁。因此它通常依赖低分辨率、模糊或双边结构，并用对比、细节强度等参数限制作用。硬约束是局部修正必须保持空间和时间上的可用稳定性；具体采用何种滤波、分辨率和权重，则是质量与成本选择。

以上描述的是**当前图空间修正数据**：它回答“这幅图的这个区域怎样修正”，由当前 RDG pass 产生，随后被 BloomSetup、Tonemap 或 visualizer 消费。不要把它直接缓存到下一帧，也不要用一张上一帧的局部纹理替代当前 view 的 rect、分辨率和亮度结构。

UE 还从曝光计算中得到一份 **average local exposure**。它不是局部纹理的降分辨率副本，而是用于保护后续 pre-exposure 计算的标量近似。GPU 把它和 exposure readback 数据一起排队；CPU 侧的 view-state exposure manager 从最新已经完成的 readback 中更新持久值；后续某个可用帧调用 `UpdatePreExposure` 时，再把这份标量纳入最终倍率。于是两条链同时存在：

```text
当前空间链：SceneColor -> local-exposure 空间数据 -> 本帧 BloomSetup / Tonemap
跨帧反馈链：GPU average local exposure -> 异步 readback -> view-state exposure manager
             -> 后续可用帧 UpdatePreExposure -> 新的 View.PreExposure
```

为什么还要这份平均反馈？如果 spatial local exposure 大幅改变整体数值分布，而下一轮 pre-exposure 只参考 global exposure，场景中间值可能仍被推向不理想范围。平均标量给跨帧数值尺度一个近似补偿，同时避免把整张局部纹理跨帧带入场景 shader。代价是它只有平均意义，不能复现空间细节，并且受异步 readback 延迟影响；这正是空间修正与跨帧反馈不能合并成一个“local exposure 状态”的原因。

替代方案也很明确：只用全局曝光，画面更稳定、意图更可控，但极端同屏动态范围更难兼顾；在美术上增加补光或降低光源范围，能从内容源头解决问题，却改变场景设计；更复杂的局部 tone mapping 能保留更多区域细节，但成本和伪影控制更困难。local exposure 不是默认优于这些方案，而是在“保留同屏明暗信息”这一目标下提供额外自由度。

### 5.4 从暗巷转向霓虹：四份数据如何接力

```text
第 N-1 帧：镜头主要看暗巷
  -> view state 保存上一轮全局曝光结果

第 N 帧开始突然转向霓虹：
  -> 本帧 SceneColor 先用既定 PreExposure 产生，不能等待本帧统计
  -> TSR 用 pre-exposure correction 把旧 history 和本帧数值放到可比较尺度
  -> 当前亮度统计更新 eye adaptation，开始向“更亮场景所需状态”推进
  -> local exposure 根据霓虹和暗墙的空间分布形成当前 RDG 空间修正
  -> BloomSetup 与 Tonemap 在本帧消费这份空间合同
  -> GPU 同时形成 average-local-exposure 标量，并把它随 exposure readback 排队

后续某个 readback 已可用的帧：
  -> view-state exposure manager 接收最新可用 global/average-local exposure 反馈
  -> UpdatePreExposure 用这些持久标量推进新的 PreExposure
  -> history 每帧按曝光尺度重新对齐
  -> 适应逐步收敛，而不是一帧内回写过去的 SceneColor；也不保证严格只隔一帧
```

这条链可以解释一种常见误判：转向霓虹时出现亮度呼吸，不必立刻认定 Tonemap 曲线错误。先看本帧空间修正是否正确；再看 average-local-exposure readback 是否已经就绪并被 exposure manager 接收；最后确认后续 `UpdatePreExposure` 是否实际消费了该持久值。空间修正错误会表现为区域对比/光晕异常，跨帧反馈停住则更可能表现为后续 pre-exposure 数值尺度长期不收敛。两者不能用同一张 local-exposure 可视化图一次证明。

## 6. Bloom：在显示变换前扩散场景高亮

Bloom 的输入不是已经编码到显示设备的颜色，而是 TSR 后、Tonemap 前的场景线性当前颜色。原因不只是“传统上如此”：Bloom 想表达的是强光经过镜头/传感器形成的能量扩散。若先把高亮通过 tone curve 压扁，不同强度的霓虹可能都变成接近白色，Bloom 就失去了判断能量和控制光晕的依据。

### 6.1 Gaussian Bloom：多尺度近似宽窄光晕

```text
当前 SceneColor
  -> 可选 BloomSetup：阈值、曝光和 local exposure 相关预处理
  -> downsample chain：把高亮分布送到多个尺度
  -> 从粗到细执行模糊，并把更粗层结果作为 additive 输入
  -> 累积 Bloom texture 交给 Tonemap
```

最粗一级用低成本覆盖很宽的光晕；更高分辨率级保留招牌边缘附近更窄的亮边。每一级不是孤立生成一张最终图，而是在自己的尺度上接住更粗层贡献，再加入本层细节。这样宽光晕无需全分辨率卷积，窄光晕也不会全部被粗 mip 吞掉。

local exposure 可能参与 BloomSetup，是因为“哪些值应该产生多强 Bloom”不能只看纹理里的 pre-exposed 数字。局部曝光把亮暗区域带到不同显示关系时，Bloom 提取也需要保持一致，否则暗区被局部抬升后可能意外泛光，或真实高亮在局部压缩后失去光晕。具体是否应用受设置控制；调试时必须同时看原始场景能量、全局曝光、local exposure 和 Bloom threshold。

### 6.2 FFT Bloom：用卷积核表达更复杂、更宽的响应

FFT Bloom 适合用较大、可塑形的卷积核表达镜头响应，例如明显的非圆对称光斑或大范围散射。它把大核卷积转到频域处理，核越大时越可能比直接空间卷积有优势；但它有额外变换、工作纹理和平台成本，也不意味着小而普通的泛光一定更快或更好。

#### 设置为 `BM_FFT` 不等于本帧实际走 FFT

UE 会先判断 FFT 路径是否**有执行资格**：Bloom method 必须是 `BM_FFT`，当前 view 必须有可持久化的 `ViewState`，平台必须声明支持 FFT Bloom，并且 view 必须拿到有效的 physical kernel texture。四项任一不成立，本帧就不会建立 FFT 分支。

随后还要过强度门控：全局 Bloom intensity 必须大于零；FFT 真正可用时看 convolution intensity，FFT 不可用时则按非 FFT/Gaussian 分支重新看 Gaussian intensity。这个顺序产生三个不同结果：

```text
BM_FFT + ViewState + 平台 + kernel texture 都有效
  -> global Bloom intensity 与 convolution intensity 有效：执行 FFT Bloom
  -> 任一强度无效：本帧 Bloom 可直接关闭，不保证自动改走 Gaussian

FFT 能力条件不成立
  -> 重新按非 FFT 路径检查 global Bloom intensity 与 Gaussian intensity
       -> 都有效：走 Gaussian Bloom
       -> 否则：本帧没有 Bloom
```

因此“设置面板写着 FFT，但 capture view 没有 ViewState”不是 FFT shader 执行失败；它首先是路径资格不成立。是否还能看到 Bloom，要继续看 Gaussian intensity，而不能假定存在无条件 fallback。

#### spectral kernel 是 view-state 缓存，不是每帧 Bloom 输出

FFT 卷积需要先把 physical-space kernel 变换为 frequency-space spectral kernel。若每帧都重新做 kernel 预处理和 FFT，会把一份通常不变的镜头核反复付费。UE 因此把可复用的 spectral texture 与 kernel constants 提取到当前 view 的持久 `ViewState` 中；本帧卷积结果、SceneColor 中间纹理仍属于当前 RDG 图，二者生命周期不能混为一谈。

缓存只有在 `r.Bloom.CacheKernel` 允许、view state 已有 spectral kernel，并且下面这些**内容身份**全部相同时才能复用：

| 身份类别 | 必须保持一致 | 为什么会影响频域结果 |
| --- | --- | --- |
| physical kernel 资源 | 同一底层 kernel texture 身份 | 换一张镜头核，频谱当然不同 |
| physical kernel mip | 当前 mip count/所用 mip 关系一致 | 流送或 mip 变化会改变实际采样核 |
| frequency texture 描述 | clear value、flags、pixel format、padded frequency extent 一致 | 决定 spectral texture 的存储和变换尺寸 |
| convolution size | Bloom convolution scale 一致 | 改变核在目标图上的物理覆盖范围 |
| image size | 当前参与 FFT 的图像尺寸一致 | 影响频域工作尺寸和核重采样 |
| cache control | `r.Bloom.CacheKernel` 仍启用 | 关闭缓存时必须重新建立，不能沿用持久结果 |

任一身份改变，系统都要重新准备 physical kernel、计算 frequency-space 描述、执行 kernel FFT，并把新的 spectral texture/constants extraction 回 view state。缓存失效不代表 Bloom 关闭，只表示本次要承担 kernel 初始化成本；真正是否有 Bloom 仍由前面的路径资格和 intensity 决定。

回到霓虹招牌：美术给它配了一张横向拉丝的 convolution kernel。第一次使用时没有 spectral cache，系统要从 physical texture 的当前 mip 建立频域核；后续帧 view、图像尺寸、卷积尺寸和 kernel 身份都不变，就能复用。若动态分辨率改变 FFT image/frequency extent，kernel texture 流送改变 mip，或美术替换 physical texture，缓存身份失效并重新变换。若这一帧是没有持久 ViewState 的 capture，即使方法仍标为 `BM_FFT`，也要回到 Gaussian gate；Gaussian intensity 同样为零时，最终就是没有 Bloom。

| 路径 | 主要优势 | 主要代价 | 更适合 |
| --- | --- | --- | --- |
| 多级 Gaussian | 成本可控、易按尺度降采样、适合常规柔和 Bloom | 对复杂镜头核的表达有限，宽光晕是多尺度近似 | 大多数实时镜头和可扩展质量档 |
| FFT convolution | 能高效表达非常大的、定制形状的卷积核 | 额外频域变换与内存，配置和平台约束更多 | 明确需要特殊镜头响应且预算允许 |
| 不使用 Bloom / 美术假光晕 | 最稳定、成本最低，艺术控制直接 | 不再从实时场景高亮自然派生；遮挡和尺度关系需额外维护 | 风格化、移动端或严格性能路径 |

两条 UE 路径都只是 Bloom 的实现选择，不改变它的输出责任：产生一张 HDR 旁支纹理，等待 Tonemap 合成。Bloom 自己不拥有最终输出，也不负责设备编码。若霓虹没有光晕，证据链应从“TSR 后高亮是否仍存在”开始，再看曝光/local exposure、BloomSetup、Gaussian/FFT 分支和最终合成，而不是直接把亮度乘大。

## 7. 调色、ToneMapping 与输出编码：一次显示边界包含多份责任

ToneMapping 不是一条孤立曲线，而是把**场景参考线性颜色**转换成**特定观察设备可解释的颜色**的边界。为了不把所有问题都叫“色调映射”，先拆出连续的责任：

```text
项目 working color space 中的 scene-linear、pre-exposed SceneColor
  -> 应用全局曝光与 local exposure，使数值代表当前观察意图
  -> white balance / color grading / film look
  -> tone curve 压缩场景动态范围
  -> 从内部工作/调色空间变换到目标 output gamut
  -> 按设备合同编码为 sRGB、Rec.709、PQ、scRGB 或线性 capture
```

这里的顺序是心智模型，不要求每个责任都对应一张独立纹理或一个独立 pass。UE 会把可合并的固定变换预组合进 `ColorGradingTexture`，再由 Tonemap 逐像素读取当前颜色、曝光、Bloom 和 LUT 完成边界。

### 7.1 working space、AP1 与 output gamut 不是同一个色域名词

- **项目 working color space** 定义前面场景和材质颜色在什么原色/白点坐标中解释。改它会影响整个内容和渲染合同，不只是最后换一个显示 profile。
- **调色/色调映射中间空间** 是实现选择。默认 ACES 风格路径会把颜色送入适合该变换的中间表达，常见核心是 AP1 相关空间；它服务于调色和 tone curve，不意味着最终显示设备就是 AP1。
- **output gamut** 定义目标设备能够表达的原色范围，例如面向 Rec.709/sRGB 或更宽色域。
- **transfer/encoding** 定义数值怎样编码给设备，例如 SDR 的 sRGB/Rec.709 曲线、HDR 的 PQ，或 scRGB 的线性浮点表达。

如果把这四层都称为“颜色空间”，最常见的错误是：LUT 在一种原色定义中生成，Tonemap 却按另一种输入解释；或者正确完成了 tone curve，却用错误 transfer function 交给显示器。视觉上它们都可能表现为偏色、发灰或高亮不对，但修复位置完全不同。

### 7.2 为什么使用 `CombineLUT`

白平衡、颜色分级、film look、tone curve 和一部分输出变换对同一个 view 的大量像素是共享的。如果把这些稳定计算全部放进每个全屏像素，代价会随分辨率重复。`CombineLUT` 先把它们组合到较小的颜色查找纹理里，Tonemap 只需查表并处理不能预烘的输入。

这是一种工程选择，不是颜色变换的硬约束。替代方案是逐像素直接计算：它减少 LUT 近似和缓存管理，参数变化也能即时反映，但全屏 ALU 成本更高；另一种是更高分辨率或多级 LUT，精度更好却增加纹理成本。UE 选择组合 LUT，是因为多数游戏 view 的调色配方在一个 frame 内稳定，适合把“每个像素都一样的计算”提到低分辨率预计算。

#### 三个容易混淆的 cache 角色

| 角色 | 它是什么 | Owner / lifetime | 它不是什么 |
| --- | --- | --- | --- |
| cached settings | 一份描述“上次 LUT 内容配方”的比较快照 | Renderer 侧跨调用保存，用于判断内容是否变化 | 不是 GPU LUT 纹理，也不能被 Tonemap 采样 |
| 持久 tonemapping LUT | 可注册回当前 RDG 的外部 LUT 资源 | 由 view 暴露、通常由 view/view-state 持久状态支持，可跨帧复用 | 不是只凭名字就永远有效；内容必须匹配 cached settings |
| RDG 临时 LUT | 当前 view 不支持持久 LUT 时，在本图创建并生成的纹理 | 当前 RDG 图；由本帧 Tonemap 消费后结束 | 不能假装已经进入 view state，也不能下一帧直接复用句柄 |

`cached settings` 与“持久 LUT 资源”分开，是为了同时回答两个问题：有没有一张可复用的 GPU 纹理，以及那张纹理里的**内容身份**是不是当前配方。只有输出纹理可注册、cached settings 未变化，并且 `r.LUT.UpdateEveryFrame` 没有强制重建时，系统才直接复用；没有持久资源支持时，即使配方没变，也要在当前 RDG 创建临时 LUT。

#### LUT 身份必须覆盖整份颜色配方

“相关参数变化就失效”不够用于调试。至少要按下列类别核对：

| 身份类别 | 代表内容 | 错误复用的结果 |
| --- | --- | --- |
| view / execution identity | view key、shader platform、compute 或 raster 生成路径 | 把另一个 view 或不同执行 permutation 的结果当成本 view 配方 |
| LUT inputs | 参与混合的 LUT 资源身份与各自权重 | 调色资产已换或 blend 权重变化，画面仍停在旧 look |
| working color space | To/From XYZ、AP1/AP0 等工作空间矩阵与 sRGB 身份 | LUT 按旧原色解释输入，产生系统性偏色 |
| grading / film recipe | white balance、全局/阴影/中间调/高光 grading、film/tone 参数、gamut 相关参数 | 局部调色或 tone curve 修改不生效 |
| output contract | output device、output gamut、inverse gamma、maximum luminance | SDR/HDR 切换或目标亮度变化后仍使用旧输出映射 |

这些字段不一定各自对应一个独立 pass，但共同决定 LUT texel 里到底存了什么。缓存命中证明的是“本次内容身份与上次一致”，不是“LUT 文件名没变”。

#### primary stereo 生成与 secondary reuse

在 stereo view family 中，primary view 在需要完整 Tonemap 配方且不是 gamma-only 路径时负责生成/更新 `ColorGradingTexture`。secondary view 不会无条件再生成一次：它先尝试注册自己可见的持久 LUT；若没有，再尝试复用 primary view 暴露的 LUT。这避免双眼在相同颜色配方下重复付费。

复用边界是**内容合同兼容**，而不是“都属于同一个 view family”就自动正确。双眼通常共享 working space、grading 和输出设备，因此复用合理；若某个 secondary view 真的拥有不同的 view identity、post-process 配方、输出设备或目标亮度，就需要一份与它匹配的持久/新生成 LUT，不能盲目借 primary 结果。否则两眼可能出现颜色不一致，或某只眼稳定复用旧 HDR/SDR 配方。

调试 stereo 霓虹画面时要分两层取证：先核对 cached settings 是否覆盖当前 view key、platform/compute、LUT 权重、working-space 矩阵、grading/film 和 output contract；再确认 Tonemap 实际绑定的是本 view 持久 LUT、primary LUT，还是当前 RDG 临时 LUT。只看到 `CombineLUT` 曾经运行，不能证明 secondary 正在消费内容正确的那张纹理。

### 7.3 ToneMapping 最终要服从输出设备

| 输出合同 | 大意 | 不能误解成 |
| --- | --- | --- |
| sRGB / Rec.709 SDR | tone curve 后进入 SDR 色域和相应显示编码 | 简单 clamp 到 0..1 就结束 |
| PQ HDR | 按 HDR 显示目标组织亮度并使用 PQ 编码 | 把 SDR 结果乘亮 |
| scRGB | 在线性浮点范围表达 HDR，交给支持该合同的输出链 | 所有平台都能直接显示的“无限 HDR” |
| 线性 capture | 保留指定线性输出，供 capture/compositing 消费 | 玩家窗口的常规显示结果 |

因此，“Tonemap 后一定是 LDR”并不准确。更可靠的说法是：**Tonemap/输出变换之后，颜色已经进入目标输出合同；这个合同可能是 SDR 编码，也可能仍以 HDR 线性或 HDR 编码形式存在。** 后续 AA、visualizer、upscale 和 capture 必须知道自己收到的是哪一种。

### 7.4 replacing tonemapper 接管的是合同，不是一个滤镜槽

自定义或 replacing tonemapper 若接管这个位置，至少要明确处理：

1. 输入 scene color 的 working space 与 `PreExposure`；
2. 全局曝光和可选 local exposure；
3. Bloom 等 HDR 旁支怎样合成；
4. color grading / film look 是否保留或由谁替代；
5. tone curve 与目标 output gamut；
6. sRGB、PQ、scRGB 或 capture 的输出编码；
7. alpha、有效 rect，以及后续 pass 期待的颜色范围；
8. 若自己成为最后 enabled pass，怎样通过 `PassSequence` 写入外部目标。

只让画面“看起来差不多”不足以证明合同正确。忽略 `PreExposure` 会让自动曝光变化时亮度漂移；忘记 Bloom 会损失高亮扩散；输出编码错误会导致窗口、截图和 HDR 设备结果不一致；把显示编码颜色交给仍假设 scene-linear 的后续 pass，则会产生二次曲线或错误混合。

ToneMapping 的 owner 边界也因此很清楚：`CombineLUT` 经营 view 级颜色配方，Tonemap 经营每像素从当前 HDR 颜色到目标输出表达的转换；它们都不拥有 present。若后面还有 FXAA、SMAA、visualizer 或空间 upscale，Tonemap 只产出新的当前颜色并交棒；只有它恰好是最后 enabled pass 时，才直接写 `ViewFamilyOutput`。

## 8. Debug / Visualize 是链上节点，不是旁路截图

UE 的 debug/visualize pass 分两类，但它们都影响当前颜色流。

第一类是 debug view 专用路径。当 view family 进入 debug material / shader complexity 等模式时，Renderer 不走普通后处理主链，而走一个缩短的 debug post chain。它仍然有 `SceneColor`、`ViewFamilyOutput` 和 pass sequence，只是启用的 pass 集合不同。这类路径的意义是“整张最终画面被 debug view 取代”。

第二类是在普通后处理链中插入的 visualizer。GBuffer overview、HDR、local exposure、motion vectors、temporal upscaler、Lumen scene、Virtual Shadow Map、selection outline、editor primitive、pixel inspector 等都按 pass sequence 的顺序接入。它们不是在最终图外面贴一张 UI，而是读当前阶段的颜色、深度、GBuffer、pre-tonemap 或 post-tonemap 结果，再产出新的当前颜色。

这解释了为什么 debug pass 的插入位置很重要：

| 插入位置 | 看到的颜色语义 |
| --- | --- |
| Tonemap 前 | HDR scene color，仍适合分析曝光、Bloom 输入、GBuffer 关系 |
| Tonemap 后 | 显示空间颜色，适合分析最终视觉输出 |
| Temporal upscaler visualizer | 同时关心 TSR/TAA 输入、输出和 history |
| Editor / selection / screenshot mask | 更接近最终输出，可能成为最后写外部目标的 pass |

例如 Temporal Upscaler visualizer 不是“最后在屏幕角落画个说明”。它会拿到 TSR/TAA 的输入、输出和 history 相关数据，再把可视化结果写成新的当前颜色。GBuffer overview 也类似：它可能同时需要 pre-tonemap HDR、post-tonemap 颜色和 separate translucency 作为观察输入。只要它们在 pass sequence 中处于最后有效位置，它们就不是覆盖在最终图上的旁路 UI，而是最终图本身。

还有一组 late debug drawing 在 pass sequence 主体之后执行，例如测试图、atlas debug、ShaderPrint、FX debug、用户 scene texture debug 等。它们通常直接画在当前 `SceneColor` 上，因此当最终画面被奇怪覆盖时，不要只查 Tonemap 或 Bloom，也要查这些后处理尾部调试绘制。

Visualize 的失败模式通常是“读错阶段”：例如一个预期看 scene-linear 高亮的工具接到显示变换后颜色，或一个预期展示最终输出合同的 overlay 插在 upscale 前导致 view rect 不匹配。调试时先确认 visualizer 所处 pass、输入颜色是 scene-linear/pre-exposed 还是某种设备输出表达、是否拿到 `SceneColorBeforeTonemap` 或 `SceneColorAfterTonemap`，再查具体可视化 shader。

## 9. `ViewFamilyTexture`：最终颜色的外部交接，不是 Present

普通路径走到链末尾时，`TOverridePassSequence` 已经把最终输出权交给最后一个有效 pass。它可能是 Tonemap、AA、visualizer，也可能是 primary/secondary upscale。这个 pass 收到的 `ViewFamilyOutput` 指向 RDG 注册的 external `ViewFamilyTexture`。到这里，后处理只完成了一个明确合同：**当前 view 的最终颜色应写到 view family 指定的外部纹理区域。**

### 9.1 双视图为何必须同时管理 rect 与 load action

假设同一个 `ViewFamilyTexture` 承载左右分屏：View A 写左半边，View B 写右半边。每个 view 的最后 pass 都是“最终输出者”，但它们不能都把整张目标当作一块无需保留的空白纹理。

| 时刻 | view 覆盖范围 | 合理的目标初始动作 | 原因 |
| --- | --- | --- | --- |
| 第一个 view 覆盖完整目标 | full rect | 可以 `NoAction` | pass 会覆盖全部有效区域，不需要保留旧内容，也无需为未覆盖区域清零 |
| 第一个 view 只覆盖部分目标 | partial rect | 需要 `Clear` 未定义区域 | 否则这一帧未被任何 view 写到的区域可能保留垃圾或旧帧内容 |
| 后续 view 写同一目标 | 自己的 rect | `Load` | 必须保留前面 view 已经写好的区域，只更新自己的部分 |

这些动作不是美术参数，而是 render target 内容生命周期。`NoAction` 的含义不是“自动保留”，而是“不要求旧内容有效”；`Clear` 建立一份已知初始内容；`Load` 才明确要求保存先前写入。把三者混淆，在单 view 全屏时可能恰好看不出问题，一到 split-screen、editor multi-view 或部分 rect 输出就会出现半屏闪烁、旧帧残留或后一视图擦掉前一视图。

把双视图案例走一遍：

```text
ViewFamilyTexture 刚进入本帧
  -> View A 是首个 partial view：Clear 目标，再只写左侧 ViewRect
  -> View A 的最后 enabled pass 结束：左侧区域成立
  -> View B 是后续 view：Load 现有目标，只写右侧 ViewRect
  -> View B 的最后 enabled pass 结束：左右区域同时成立
  -> external texture 以完整 view family 结果离开后处理
```

这里的 owner 分成两层：每个 view 的 `PassSequence` 决定“哪个 pass 写自己的最终 rect”；view family 输出创建逻辑决定“这次写之前要 Clear、Load 还是不关心旧内容”。只检查最后 pass 而不检查 rect/load action，仍不足以证明多视图输出正确。

### 9.2 Render 尾部要把三类生命周期分别收口

| 资源 | 后处理后的命运 | 何时才算满足本层合同 |
| --- | --- | --- |
| `ViewFamilyTexture` | external texture，最终访问状态交还外部消费者 | RDG 执行了最后写入和必要访问过渡 |
| TSR/TAA/其他 history | extraction 到持久 view state，未来帧重新注册 | extraction 对应的 GPU 资源保持到下一次合法消费 |
| Bloom/downsample/Tonemap 临时纹理 | 只服务当前 RDG 图，可在最后消费者后回收或 alias | 图调度确认最后消费者结束，不能凭 CPU 代码作用域提前释放 |
| late debug/on-screen 输出 | 可能继续修改外部目标 | 它们若启用，也必须算进“最终写入者”证据 |

临时资源能否复用，取决于最后 GPU 消费者，而不是 C++ 变量离开作用域；history 能否跨帧，取决于 extraction 和下一帧注册，不是因为对象名字带 `History`；external texture 能否交还，取决于图执行和最终访问状态，不是因为 `AddPostProcessingPasses` 已返回。

### 9.3 “后处理函数返回”距离屏幕显示还有多深

```text
后处理在 RDG 中声明 pass 与资源依赖
  -> RDG compile / execute 决定顺序、barrier、alias 和 extraction
  -> RHI command recording 记录平台无关的图形/计算/复制工作
  -> translate / context 消费 RHI work，后端 finalize 可提交的平台命令包
  -> Platform Queue Submit 把命令包与提交顺序交给目标队列
  -> GPU 实际消费命令；匹配本次写入的 fence / completion value 到达
  -> viewport / swap chain 按平台协议 present
  -> 显示系统在自己的时序中扫描输出
```

每个箭头都是一次 producer / output / consumer 的控制权交接。构图期的 pass/event 注册只证明工作已声明；执行 trace 或 lambda/context 证据才能继续证明图执行已到达该节点。RHI recording 的产物仍是平台无关 work；backend finalize 才形成可提交的平台命令包；Platform Queue Submit 只证明目标队列接管，不能证明 GPU 已经完成；与包含 `ViewFamilyTexture` 最后一次写入的目标 queue 提交范围匹配的 fence / completion value 到达后，才足以讨论相关资源是否可安全复用；present 调用也不等于显示器已经扫描到那一帧。把这些层都叫“渲染完成”，会让卡顿、黑屏和资源生命周期问题无法分诊。

因此，如果 `ViewFamilyTexture` 的 GPU 内容正确但窗口不更新，应把调查移到 viewport、RHI、Platform Queue Submit、GPU completion 和 present；如果 RDG capture 里最后 pass 根本没写外部目标，问题仍在本章；如果只有 split-screen 的第二个 view 破坏第一视图，优先检查 load action 和 rect，而不是 Bloom 或 Tonemap。

把 Part 3 的最后一段主线收束起来：BasePass、Lighting、大气、水和半透明逐步建立 scene-linear `SceneColor`；后处理先确认它的可读表达，再让当前颜色沿动态 pass sequence 移动。TSR 建立稳定颜色和新 history；eye adaptation 与 local-exposure 空间数据服务当前显示，同时 average-local-exposure 通过异步 readback 反馈给后续 `PreExposure`；Bloom 按 Gaussian 或满足资格/缓存身份的 FFT 路径形成高亮旁支；`CombineLUT` 以 cached settings 判断持久或临时 LUT 内容，再由 Tonemap 完成 tone curve、output gamut 和设备编码；Debug/Visualize 可以继续接管颜色；最后 enabled pass 按 view rect/load action 写 `ViewFamilyTexture`。随后 RDG、RHI、平台队列、GPU 和 present 继续推进各自的完成深度。到这里，“颜色正确”“外部目标已写”“GPU 已完成”“玩家已看到”终于被分成了可验证的四件事。

## 调试主线

不要从“最后看起来是什么颜色”倒猜某个 shader，而要寻找**最后一个已经成立的状态**。下面每一级都以上一级成立为前提；在哪一级第一次失去证据，问题就落在它与上一级之间。

| 证据级 | 必须成立的状态 | 典型证据 | 若这里首次失败 |
| --- | --- | --- | --- |
| 1. 后处理输入可读 | scene-linear `SceneColor` 的 texture/slice/rect 和访问状态正确 | 输入纹理、有效 rect、resolve 关系 | 查场景颜色交接，不查 Bloom |
| 2. temporal 输入完整 | color/depth/velocity/history/透明层与 view 匹配 | camera cut、history format/size、velocity 可视化 | 查 TSR 输入与历史有效性 |
| 3. TSR 当前输出成立 | `FullRes` 已成为新的当前颜色 | TSR 输出内容和后续 pass 输入指向一致 | 查 TSR 输出交棒 |
| 4a. 当前曝光/空间修正成立 | 本帧 `PreExposure`、eye-adaptation 输出和 local-exposure 空间数据各自有效 | 曝光可视化、历史倍率、双边网格/模糊亮度与局部结构 | 区分数值尺度、全局适应与当前空间修正 |
| 4b. 跨帧平均反馈成立 | average local exposure 已经完成 GPU readback、进入 view-state exposure manager，并被后续 `UpdatePreExposure` 消费 | 最新可用 readback、manager 持久标量、后续 view 的 `PreExposure` | 不承诺固定一帧；区分 readback 未就绪、CPU 未接收与尚未消费 |
| 5a. Bloom 实际路径成立 | FFT 资格由 `BM_FFT`、ViewState、平台与 kernel texture 共同决定；随后 global/path-specific intensity 有效 | 实际 Bloom method、view state、平台能力、kernel、Gaussian/FFT intensity | FFT 资格失败后重新检查 Gaussian gate；不保证本帧仍有 Bloom |
| 5b. FFT kernel 身份成立 | spectral kernel 来自正确 physical texture/mip、frequency desc、convolution/image size，且 cache CVar 允许复用 | `r.Bloom.CacheKernel`、view-state spectral/constants 与本帧身份 | 区分冷缓存/失效重建、FFT convolution 输出和完全无 Bloom |
| 6a. LUT 内容身份成立 | cached settings 覆盖本 view 的 platform/compute、LUT 权重、working-space、grading/film 与 output contract | cache identity、持久或临时 LUT 的生成/复用理由 | 查旧配方未失效、错误 stereo reuse 或强制更新状态 |
| 6b. Tonemap consumer 成立 | Tonemap 实际绑定本 view 持久 LUT、兼容的 primary stereo LUT，或当前 RDG 临时 LUT | `ColorGradingTexture` 的真实资源身份与当前 view | 区分“LUT 内容错误”和“正确 LUT 没被该 view 消费” |
| 7. 最后 pass 已确定 | `PassSequence` 的最后 enabled pass 接受 `ViewFamilyOutput` | pass 序列和 override output | 查插件、AA、visualizer、upscale 接管 |
| 8. external target 已写 | 正确 view rect 使用正确 load action 写入 `ViewFamilyTexture` | 多 view 各区域内容 | 查 rect、Clear/Load/NoAction |
| 9. RDG 已执行 | pass、barrier、extraction、external final access 已兑现 | RDG 执行时序 | 查依赖、裁剪和资源生命周期 |
| 10. RHI work 已记录 | 平台无关图形/计算/复制命令已进入 RHI command list | RHI command 记录与封口 | 查 RDG execute 到 RHI recording；尚不能声称 native command 已形成 |
| 11. Platform command 已形成 | translate/context 已消费 RHI work，backend 已 finalize 可提交命令包 | finalized platform command package | 查 translate、context、parallel join 与 finalize；尚不能声称 Queue 已接管 |
| 12. Platform Queue Submit | 命令包已交给目标 GPU 队列 | 队列提交与 payload/fence 证据 | 尚不能声称 GPU 完成 |
| 13. GPU 已完成 | 与包含 `ViewFamilyTexture` 最后一次写入的目标 queue 提交范围匹配的 fence / completion value 已到达，外部目标内容已由 GPU 产出 | GPU fence/timestamp/capture 与对应 queue submission | 查 GPU hang、同步和资源复用 |
| 14. Present 链成立 | viewport/swap chain 接受目标图像，显示链继续推进 | present/平台帧时间线 | 查后处理之外的窗口与显示链；present 仍不等于 scanout 完成 |

霓虹画面若“录屏正确、窗口错误”，证据通常已经走过第 8 级，调查应向后移动；若 pre-tonemap visualizer 正确而 post-tonemap 变灰，问题落在第 5 到第 6 级；若单 view 正确而双 view 左半被擦除，问题落在第 7 到第 8 级。这个模型的价值，是让你用已成立状态缩小范围，而不是同时怀疑所有效果。

## 常见误读

**误读一：所有 post effect 后固定执行一次 Final Blit。**  
UE 的最终输出 pass 是 `TOverridePassSequence` 动态算出来的最后有效 pass，可能是 Tonemap、AA、Visualize 或 Upscale。

**误读二：resolve 永远是把整张颜色复制一遍。**
后处理需要的是 shader 可读合同。存在 MSAA / target-resolve 分离时必须完成相应解析；不需要独立解析时，可读角色可以沿用已有纹理，底层不必凭空复制。

**误读三：TSR 是链尾的空间放大。**  
TSR 在链中段，处理 scene-linear、pre-exposed 颜色与 history，位置在 Bloom/显示变换之前。

**误读四：`PreExposure` 就是本帧自动曝光结果。**
`PreExposure` 是本帧早期场景渲染使用的数值倍率，通常来自先前已经可用的曝光状态；当前 eye adaptation 在后处理中更新。local exposure 又分为本帧空间修正与异步跨帧 average-local-exposure 标量，后者只在 readback 就绪后的后续 `UpdatePreExposure` 中生效。

**误读五：Bloom 读的是显示编码后的颜色。**
Bloom 在显示变换前读取场景高亮，再由 Tonemap 合成。设置 `BM_FFT` 只表达意图；实际还需要 ViewState、平台、kernel texture 和强度门控。FFT 条件失败后会重新检查 Gaussian 路径，但不保证本帧一定仍有 Bloom。

**误读六：ToneMapping 后一定是 0..1 的 LDR。**
它进入的是目标输出合同；该合同可以是 SDR，也可以是 PQ、scRGB 或线性 capture。不能脱离输出设备判断数值范围。

**误读七：Visualize 是画面外贴的 UI 截图。**
Visualize 是链上颜色流的消费者兼生产者，可能成为最后写外部目标的 pass。

**误读八：`AddPostProcessingPasses` 返回就 present 了。**
它建立 RDG 工作与 external output 关系；RDG execute、RHI recording、Platform Queue Submit、GPU completion 和 present 是更深的完成层级。

## 本章小结

PostProcessing 的本质不是“给画面加效果”，而是把一份 scene-linear、通常已 pre-exposed 的当前颜色，沿一条**动态**的状态链推进成目标设备可解释的外部输出。

这条链先确认 `SceneColor` 的可读表达，由 `AddPostProcessingPasses` 建立“当前颜色 / 原始颜色 / 外部输出”三种角色，并用 `TOverridePassSequence` 决定谁拿到最终输出权。After DOF 玻璃说明绘制与合成时机为何解耦；TSR 校验 history、对齐曝光尺度并建立新历史；曝光阶段同时经营本帧 local-exposure 空间修正与异步跨帧 average-local-exposure 标量；Bloom 依据实际门控选择 Gaussian、FFT 或关闭，并由 view-state 管理可复用 spectral kernel；`CombineLUT` 用完整内容身份管理 view 持久 LUT、stereo reuse 或 RDG 临时 LUT，再由 Tonemap 处理 working space、调色、tone curve、output gamut 和设备编码；Debug/Visualize 仍可继续接管当前颜色。最后 enabled pass 按 view rect/load action 写 `ViewFamilyTexture`，RDG、RHI、平台队列、GPU 和 present 再逐层兑现更深完成性。

如果要用一句话带走本章：**UE 的后处理没有固定 Final Blit；它管理的是“当前颜色的语义、数值尺度、跨帧历史、显示合同和最终写入权”如何逐段成立。调试时要寻找最后一个已成立状态，而不是从最终观感倒猜某个 shader。**
