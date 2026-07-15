# 24 Optimization

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `03_ThreadModel.md`、`05_RenderGraph.md`、`06_GPUScene.md`、`07_MeshDrawCommand.md`、`08_FrameInit.md`、`09_DepthPrepass.md`、`10_BasePass.md`、`12_Lighting.md`、`15_PostProcessing.md`、`16_Nanite.md`、`17_Lumen.md`、`19_VirtualShadowMaps.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `24_Optimization_CoverageMatrix.md`

玩家转头看向城市路口，帧时间从 16 ms 跳到 28 ms。此时最危险的问题不是“不知道哪个 CVar 能提速”，而是**还没有定义自己在测什么，却已经开始改系统**。

一次可交付的优化必须形成同一条闭环：

```text
测量合同、稳定 workload 与可复现 baseline
  -> 真实时间线与 critical path
  -> 选择足够深、扰动可控的观测工具
  -> 把昂贵区间归还给成本 owner 和输入数据
  -> 写出可证伪假设与 last-valid-state
  -> 做单变量实验
  -> 收集 CPU / GPU / Frame / quality 的同合同证据
  -> 对照预先冻结的门槛，接受或拒绝假设与候选方案
  -> 审计 memory / residency / history / cache / build 的资源与生命周期后果
  -> 恢复实验基线
  -> 自动化复测并作产品决策
```

这里每个箭头都是一个证据边界。跳过任意一层，结论都会从“已证明”退化成“相关性”：`ProfileGPU` 看到 VSM 贵，不等于 foliage 是根因；隐藏 foliage 后帧变快，不等于删除 foliage 是产品方案；某个 GPU event 下降，也不等于延迟、帧节奏和目标平台都改善了。

本章仍用路口 spike 贯穿全部步骤，但不把它写成一份优化配方。它的教学目标是让你能回答：**当前证据最后证明到了哪个状态，下一项实验如何推进证据，而最终改动为什么值得进入产品。**

## 1. 先建立测量合同：没有合同，数字不能比较

性能测量不是“在同一张地图里跑两次”这么简单。它的输入是一组会改变执行路径、缓存状态、像素量、帧同步和硬件频率的条件。负责冻结这些条件的人是测试设计者；工具只负责记录，不会替你保证 A/B 两组样本可比。

### 1.1 先决定优化的是 latency、throughput 还是 frame pacing

这三个目标相关，但不等价。

- **Latency** 是一次输入从采样到对应图像真正显示所经历的时间。它跨越 Game、Render、RHI、GPU queue、swapchain 和显示阶段。减少帧内工作通常有帮助，但更深的 CPU/GPU 流水也可能提高吞吐而增加输入排队深度。
- **Throughput** 是单位时间完成多少帧或多少工作。让 Game、Render、RHI 与 GPU 重叠，往往能提高吞吐；但如果输入需要多等一帧流水，低延迟目标未必改善。
- **Frame pacing** 是帧是否按稳定节奏交付。平均 16.6 ms 可以由许多 10 ms 帧和少数 40 ms 帧组成，平均 FPS 合格，操作感和动画仍会明显抖动。

UE 允许线程和多帧工作重叠，是因为串行等待会浪费 CPU 核心和 GPU。`r.OneFrameThreadLag`、`r.GTSyncType` 以及平台的 swapchain 同步策略会改变允许的重叠和同步深度。这是吞吐与延迟的工程取舍，不是某个值永远更好：交互敏感的竞技场景可能愿意牺牲部分吞吐换低延迟；离线展示或 GPU 饱和的内容可能更重视稳定吞吐。

因此，测量合同至少要写出以下预算，不能只写“目标 60 FPS”：

| 目标 | 合同需要记录什么 | 只看平均帧时间会漏掉什么 |
| --- | --- | --- |
| Frame/throughput | 目标帧预算、Game/Render/RHI/GPU 分预算、允许的动态分辨率范围 | 流水深度、局部 event 降低后新的 critical path |
| Latency | 输入到显示目标、同步模式、VSync、帧率上限、窗口/全屏模式 | CPU/GPU 排队和 Present 等待 |
| Pacing | median、关键 percentile、hitch 阈值、hitch rate、连续超预算帧 | 周期 spike、冷启动、编译和 streaming 抖动 |
| Memory | CPU 内存、VRAM、streaming pool、瞬态峰值和安全余量 | eviction、降质、跨帧 residency 抖动 |
| Product | 画质误差、正确性、构建时间、包体、预热时间和平台范围 | 用运行时收益交换了不可接受的生产成本 |

预算的 owner 是项目而不是引擎默认值。16.67 ms、33.3 ms、hitch 阈值和显存余量都必须按目标设备、显示模式和产品体验定义。

### 1.2 冻结 workload，而不是只冻结相机

**Workload** 是可重复执行的一段产品行为。它不仅包含场景和相机，还包含会改变渲染输入及跨帧状态的全部条件。

一次正式 A/B 至少记录：

1. 目标平台和具体 SKU、OS/驱动、RHI、feature level、device profile、功耗模式与温度状态。
2. packaged 或 Editor、Development/Test/Shipping 等 build 配置，以及是否附加调试器、验证层或额外 trace。
3. 输出分辨率、实际内部渲染分辨率、primary/secondary screen percentage、upscaler、动态分辨率、VSync 和帧率上限。
4. 固定 replay、输入脚本或 flythrough，明确起止帧、相机、光源、天气、时间轴、随机种子和 AI 状态。
5. warm-up 时长和正式采样窗口。warm-up 必须长到 shader/PSO、streaming、temporal history 和硬件频率进入合同所要求的状态。
6. 冷态与热态定义。首次进入、重启后首次运行、稳定驻留后的重复运行必须分开报告，不能混入同一分布。
7. run 数量、异常帧处理规则和 artifact 命名。不能在看完结果后才决定丢掉哪一帧。

只固定相机仍可能测到不同结果：第一次运行正在读纹理和 Nanite page，第二次已经驻留；一次运行触发 PSO 创建，另一次命中缓存；动态分辨率让两次 GPU event 实际处理的像素数不同；VSync 让 Present 等待吞掉了本可观察的收益。

### 1.3 先给问题分型，避免把不同生命周期混成一个平均值

在打开深层工具前，先给症状贴上**测量类型**，不是贴根因标签：

- **Steady-state**：稳定镜头或稳定 traversal 中持续超预算。
- **Periodic hitch**：按固定或近似固定周期出现的尖峰，常与批处理、回收、更新周期或节奏有关。
- **Cold traversal**：第一次进入区域时出现，热态重复后显著减弱。
- **Streaming/residency**：IO、解压、上传、pool 压力或 eviction 与画面移动相关。
- **PSO/shader**：首次遇到材质/管线组合，或编译结果在 Game Thread 最终接入时出现尖峰。
- **Present/pacing**：渲染 work 不足以解释墙钟帧时间，等待与显示节奏相关。

不同类型的 lifetime 不同。steady-state 假设关心每帧输入；cold traversal 假设关心请求到驻留的跨帧状态；PSO hitch 关心“是否已准备好”这一离散状态。把它们求一个平均数，会同时抹掉频率和原因。

### 1.4 控制统计噪声和观测污染

性能数字本身是样本，不是真值。后台进程、驱动调度、温度降频、GC、资产加载、shader 编译、动态分辨率、VSync、capture 和 trace 写盘都能改变样本。

实践中至少报告：样本数、median、与体验合同对应的 percentile、hitch count/rate、最坏稳定窗口，以及 A/B 差值相对重复运行噪声带的大小。平均值可作补充，不能独自承担判定。若 A/B 差值落在 run-to-run 波动内，正确结论是“当前实验没有分辨力”，不是“有 0.2 ms 收益”。

工具也会改变被测系统。启用更多 trace channel 会增加事件、缓冲和 IO；`ProfileGPU` 增加 timestamp/event 收集并只捕获代表帧；full PSO validation、memory trace 和详细日志也有成本。测量合同要记录这些开关，并用较轻的非捕获基线复核最终收益。

## 2. 真实时间线：依赖地图不等于墙钟顺序

UE 的 deferred renderer 可以画成 `Visibility -> GPUScene -> PrePass -> BasePass -> Shadows -> Lighting -> Post`。这张图说明**谁生产谁需要的数据**，是依赖/责任地图；它不是“这些块在墙钟上严格首尾相接”的时间线。

真实执行同时存在：

- Game Thread 生产世界与组件变化；
- Render Thread 和 worker task 发布 Scene 变化、构建 view、visibility、mesh pass 与 RDG；
- RHI Thread/backend 把抽象命令转换、记录并提交给平台队列；
- graphics、async compute、copy queue 在依赖允许时并发；
- IO、解压、资产创建与 streaming 跨多帧推进；
- Present/swapchain 把完成的图像交给显示链路。

### 2.1 三帧 overlap 与 critical path

下面是概念图，不代表所有平台都使用相同线程或固定一帧延迟：

```text
时间 -------------------------------------------------------------->

Game:       [G0 active] [G1 active] [G2 active]
Render:          [R0 tasks/build] [R1 tasks/build] [R2 tasks/build]
RHI/backend:          [Q0 record/submit] [Q1 record/submit]
GPU graphics:             [F0 graphics------] [F1 graphics------]
GPU async:                   [F0 async---]       [F1 async---]
Copy/stream:             [uploads]      [uploads]
Present:                                  [flip F0]        [flip F1]
```

**Critical path** 是决定本次产品完成点的最长依赖链，不是屏幕上最大的一个条形。对 throughput，它通常通向连续帧完成率；对 latency，它从输入采样一路通向对应 frame flip；对 pacing，它还必须解释 Present cadence 和超预算帧分布。

UE 允许重叠，是为了隐藏不同处理器和线程的空洞。如果强制所有阶段逐帧串行，因果更直观，但 CPU 与 GPU 会频繁互等，吞吐下降。相反，更深的流水提高资源利用率，却增加排队、状态 lifetime 和调试难度。优化某个 stage 后，critical path 可能迁移到原先被遮住的 stage，这就是“目标 event 降了而 Frame 不降”的正常解释之一。

### 2.2 正确读取 `stat unit`

基础 `stat Unit` 是第一层坐标，不是穷尽性模型；它汇总 Frame、Game、Draw(Render)、RHIT、GPU，并可显示 input latency。非 Shipping 构建还提供三种独立诊断入口：`stat Unit` 配合 `stat Raw` 查看未平滑的 raw 值，`stat UnitMax` 查看 max，`stat UnitCriticalPath` 查看 critical-path 口径。`stat Raw`、`stat UnitMax`、`stat UnitCriticalPath` 都不属于 Shipping 工具合同；默认 Unit 显示还会对多个值做平滑，因此短 spike 可能被削弱。

必须纠正一个常见误读：**标准 Game 值不是“Game Thread 等 Render/GPU 的总墙钟时间”**。UE5.7 的普通 Game 时间会扣除已跟踪的 Game Thread waits；critical-path 口径会按另一套等待归属计算。于是：

- 普通 Game 高，先解释为 Game Thread active work 或未被该等待口径排除的工作，不要笼统说“它只是在等 GPU”。
- `stat UnitCriticalPath` 可帮助判断等待如何参与关键链，但仍不能替代 Insights 里的 work/wait 和同步点。
- Render 时间的定义排除 idle；它高时要区分实际 setup/build work 与 join point 前未完成的 task。
- RHIT 高只说明 RHI 时间线压力大。command translation、backend recording、driver/PSO、submit、Present 或 GPU 反压都是候选，单个数值不能选择其中一个。
- GPU 值来自 GPU profiler 计时体系。是否能够剔除 CPU bubbles 受 RHI capability 和平台实现影响；不能把所有平台上的 GPU frame 数都理解为纯 shader busy time。
- Frame 是墙钟交付节奏。VSync、cap、fixed/benchmark 路径、Present 和其他主循环工作会让它不等于四条工作线的简单最大值。

所以“Game/Render/RHI/GPU 四条线”只能作为 steady-state rendering 的第一层分类。Present、IO/streaming、async queue、编译、GC、内存压力和显示链路没有因此消失。

### 2.3 Present 是完成链的一部分

Present 的职责是把已渲染 back buffer 按平台约定交给 swapchain 或 custom present。它可能涉及提交等待、`SyncInterval`、VSync、back-buffer 推进，以及外部 compositor 或 XR 路径。D3D12 的 Present scope 是一个可用入口，但其他 RHI 必须看各自 backend 的等价 owner，不能把 D3D12 名字当跨平台合同。

如果 Render/GPU work 很短而 Frame 仍长，或帧时间呈现刷新周期量化，必须检查 Present 与 pacing。降低 shader 成本但仍被同一 VSync 边界限制，吞吐显示可能不变；它仍可能增加余量或降低功耗，但报告不能写成 FPS 提升。

UE 的 input latency 计时入口提供从输入采样到对应 frame flip 的周期性引擎信号。它的完成深度停在 frame flip，不覆盖、也不能单独证明 compositor 排队、scanout 和面板响应；真正的 input-to-photon 需要结合平台 telemetry 与外部测量。

## 3. 工具是证人：按观测深度逐层收敛

一个工具只有在你知道“它的数据由谁产生、完成到哪一层、能证明什么、不能证明什么、采集成本是什么”时才有意义。

| 工具/信号 | owner 与观测深度 | 能证明什么 | 不能证明什么 | 主要扰动或条件 |
| --- | --- | --- | --- | --- |
| 基础 `stat Unit`；非 Shipping 的 `stat Raw` / `stat UnitMax` / `stat UnitCriticalPath` 诊断变体 | Engine 汇总的线程、GPU、Frame 和 latency 计时 | 大类时间线，以及非 Shipping 下的 raw/max/critical-path 候选 | pass、内容根因、全部等待来源 | 三个诊断变体均不属于 Shipping 工具合同；平滑和口径不同；GPU bubble 依平台 capability |
| CSV Profiler | 以 GT/RT 帧边界组织 category、custom stat 和 metadata | 可自动化的多帧趋势、A/B、run matrix | 深层调用栈与单帧 shader 细节 | category 数、后台处理和文件 IO |
| FPSChart | Frame histogram、percentile、hitch、动态分辨率汇总 | pacing 与体验分布 | 调用栈根因 | hitch/idle 配置必须归档 |
| Unreal Insights | CPU thread/task/wait、LoadTime/IO、可用时的 GPU queue 轨道 | work、wait、task join、IO 和 queue 的时序关系 | 未启用 channel 的事件和内容因果 | trace channel、named event、buffer、写盘成本 |
| `stat gpu` | GPU profiler 的持续统计视图 | 多帧 GPU category 趋势 | CPU setup 与内容根因 | timestamp/stat 插桩和 build/capability |
| `ProfileGPU` | 单个代表帧的 GPU event tree、busy/wait/idle 与 draw/dispatch 信息 | 该捕获帧中 queue/event 的成本归属 | spike 频率、统计显著性、未捕获帧 | 单帧与额外 event/timestamp/UI/log 成本 |
| RDG event/breadcrumb | RDG/RHI 对 pass 责任和资源依赖的命名 | event 属于哪个逻辑阶段、依赖在哪里 | pass 内 shader 根因和跨帧状态 | 编译条件、命名和 breadcrumb 不保证所有 build 一致 |
| Streaming/PSO/memory counters | 各 manager/cache/allocator 的状态 | wanted/resident/pending、hitch、pool、峰值 | 资源是否值得保留或画质是否正确 | stats/validation/trace 的 build 和开销差异 |
| GPU capture | API command、resource、pipeline、shader 与 counter 的深层状态 | pass 内绑定、资源和硬件行为 | 多 run 频率与产品统计 | 捕获会显著改变节奏；平台支持不同 |

正确顺序通常是：

```text
CSV/FPSChart 找“多久发生一次、分布怎样”
  -> Insights 找“哪条 work/wait/IO/queue 链形成尖峰”
  -> ProfileGPU/stat gpu 找“代表帧哪个 GPU owner/event 贵”
  -> subsystem counters 找“它消费的输入和跨帧状态是否变化”
  -> GPU capture 找“pass 内 shader/resource/pipeline 为什么贵”
```

不是每次都要走到最深。Visibility task 数已经随 primitive 输入成比例增长时，可能不需要 GPU capture；反之，BasePass 只随某材质变化而上升，capture 和硬件 counters 才能区分 ALU、纹理、带宽或 overdraw。选择最浅但足以区分剩余假设的工具，能减少观测污染。

CPU scope 也不能混为一种机制。Trace CPU event、named event 与 cycle stat 有不同 sink、channel、build 条件和成本。在 Insights 看见相似名称，不代表它们具有相同采集语义；报告应记录启用了什么，而不是笼统写“所有 scope 都进入 Insights”。

**ProfileGPU 最小实验卡**：`r.ProfileGPU.Sort=0`、`r.ProfileGPU.Root="*"`、`r.ProfileGPU.ThresholdPercent=0`、`r.ProfileGPU.ShowLeafEvents=1`、`r.ProfileGPU.ShowStats=1` 是运行时可改的输出控制，`ProfileGPU` 本身的可用性和事件完整度仍受 build 与 RHI profiling capability 影响。一次只改过滤、排序或显示项，可以证明“同一捕获数据怎样被组织和筛选”，不能证明渲染 work 已改变；实验结束恢复上述默认值，并用未过滤输出确认没有隐藏目标 event。

## 4. 从 event 回到成本 owner：先看数据状态，再看功能名字

下面的 renderer 地图用于导航责任与依赖：

```text
Scene publication / view setup
  -> view visibility and relevance
  -> mesh pass setup / Instance Culling / GPUScene consumption
  -> depth, HZB and Nanite visibility
  -> BasePass and GBuffer
  -> shadow demand, allocation and caster raster
  -> Lumen scene/trace/cache work and deferred lighting
  -> translucency
  -> mutable post-process chain
  -> back buffer and Present
```

它不是严格串行时间线，也不覆盖 IO、编译和跨帧 residency。每个节点的调试问题都应写成：输入数据是什么，谁拥有它，何时发布或消费，能活多久，哪个证据说明输入真的变了。

### 4.1 Scene change 与 view visibility 是两条独立状态链

原先最容易产生错误因果的地方，是把 Visibility 画成 GPUScene upload 的普遍上游。正确模型是两条链在后续消费处汇合：

```text
Game/component change
  -> render-side Scene change / dirty primitive or instance data
  -> GPUScene incremental upload -----------------------+
                                                        |
View + bounds + show flags                              v
  -> visibility/relevance -> pass and instance inputs -> draw/shadow/Nanite consumers
```

Scene change 链回答“持久场景数据本帧变了什么”。它的 owner 从 Game Thread 组件变化跨到 Render Thread 的 Scene 镜像，再到 GPUScene update/upload；数据可跨帧驻留，直到被后续变化覆盖。primitive 当前 view 不可见，也可能因为变换或实例数据变更而需要上传。

View visibility 链回答“这个 view 本帧允许哪些 primitive 和 mesh 进入哪些 pass”。它消费 bounds、view、relevance 和 show flags，输出 visibility bitset、relevance 结果、dynamic lists 与 pass 输入；这些结果通常属于当前 view/frame。

把两条链合成一条会造成两个错误：看到 visibility 高就推断 GPUScene upload 必高；看到 GPUScene update 高就去删当前画面的可见对象。调试时应分别量化 dirty 数和可见/pass 输入数，再看它们是否在同一 workload 中共同上升。

### 4.2 Visibility：它控制当前 view 的后续输入，不控制所有场景变化

Visibility 存在，是因为让每个 pass 和每个 shader重新遍历全部 Scene 会重复做空间、relevance 和 show-flag 判断。UE 先由 view visibility tasks 消费 primitive bounds、view frustum 和场景索引，再生产当前 view 的资格集合，后续 mesh pass 复用这些结果。

它的主要状态链是：

```text
Scene primitives + bounds + view
  -> frustum/spatial candidates
  -> relevance and dynamic gathering
  -> per-pass command/instance inputs
  -> task join before dependent consumers
```

若没有这层共享筛选，后续 pass 会重复遍历更多对象；若 bounds 过大、always-visible 对象过多或 HLOD 没有减少 proxy 输入，前端判断和后续 pass 都会膨胀。替代方式不是“永远更激进地 cull”：更紧 bounds 可提高剔除效率，但错误 bounds 会让对象消失；更强 HLOD/合并可减少 proxy，却增加构建、streaming、过渡和内容维护成本。

调试时，frustum/relevance/dynamic gather/wait 是不同 owner。`WaitForVisibilityTasks` 高表示依赖点在等任务完成，不自动说明等待函数本身做了大量工作；要沿 task 看最后未完成的 producer。worked case 是把路口一组 foliage bounds 可视化：如果 bounds 异常跨过整个街区，修正后 candidate、pass input 与 GPU event 同向下降，才能证明 visibility 输入参与了根因。

对 Unity SRP 读者，renderer list 可以作为“view 筛选结果进入后续绘制”的起始坐标；但 UE visibility 还生产 relevance bitset、dynamic lists 和逐 pass 输入，不能把两边的数据结构、任务 owner 或 lifetime 当成机制等价。

### 4.3 MeshDrawCommand、dynamic instancing 与 primitive identity

MeshDrawCommand 是一个 pass 可提交 draw state 的表示。UE 把 material/shader、pipeline、vertex/index geometry、bindings 等准备结果与场景对象身份分开，是为了缓存共享状态、排序并减少每帧重复组装。

Cached MeshDrawCommand 的 lifetime 可跨帧，直到 material、vertex factory、pass 条件或相关 render state 使它失效；dynamic path 则更常在每帧 setup 中重新产生。缓存降低 CPU setup，但需要更严格的可缓存条件和失效管理。完全动态构建更灵活，却把更多 work 放回每帧 Render Thread。

**Dynamic instancing 匹配的是可共享 draw state，不要求不同实例的 primitive data 数值相同。** UE5.7 会比较 pipeline、stencil、shader bindings、vertex streams、primitive-ID stream 的布局位置、index/geometry 和 draw 参数等兼容条件。合并后，primitive-ID stream/实例输入仍为每个实例保留身份，shader 再通过 ID 读取各自的 GPUScene 数据。

因此，正确优化问题是：哪些本应共享的状态被 material/VF/shader binding/geometry 差异打散？不能为了“让 primitive data 相同”删除每对象变换、光照或身份数据。只有当 per-primitive 差异错误地泄漏进共享 shader binding 或 draw state，才会破坏 bucket 兼容性。

State bucket 的作用是把兼容命令聚在一起；primitive ID 的作用是让共享 draw 仍能找到各实例数据。若把二者混成一个概念，就会误以为合批要求对象数据相等。路口案例里，应记录 bucket/command/instance 数，而不是只看 draw call 一个总数。

**MDC 最小实验卡**：`r.MeshDrawCommands.ParallelPassSetup=1`、`r.MeshDrawCommands.UseCachedCommands=1`、`r.MeshDrawCommands.DynamicInstancing=1` 均为默认开启、Render Thread safe 的运行时控制；前者还要求 threaded rendering，cached path 受 vertex factory 支持限制，dynamic instancing 还要求 GPUScene 和兼容 draw state。每次只将其中一项改为 `0`，可证明当前 workload 是否依赖并行 setup、cached command 或动态合批，不能证明 command 增长的内容根因，也不能外推到不满足条件的平台。采样后恢复 `1`，等待 command/cache 状态稳定，再确认恢复基线回到原噪声带。

### 4.4 Instance Culling：先支付筛选成本，再减少后续工作

Instance Culling 消费 command、instance、view，以及启用路径所需的 HZB/occlusion 信息，生产 compacted instance IDs 和 indirect draw 参数。它把“哪些实例真正进入 draw”推迟到更接近 GPU 的位置，适合大量实例和多 view，但 culling/build pass 自己也有成本。

如果实例很少、遮挡弱或数据准备昂贵，CPU 直接提交或较简单路径可能更便宜；如果实例巨大且大部分不可见，GPU-driven culling 更可能获益。判断不能只看 culling event：要同时比较输入 instance、输出可见 instance、indirect command、节省的 raster/shading 与新增的 build/cull 成本。遮挡错误、HZB 不匹配或错误 lifetime 会表现为消失、闪烁或 stale visibility，而不仅是性能变化。

**Instance Culling 最小实验卡**：`r.CullInstances=1` 默认开启；`r.InstanceCulling.OcclusionCull=0` 默认关闭并带 `Preview` 标记；两者都是 Render Thread safe 的运行时控制。前者的 `1 -> 0` A/B 可判断当前路径是否从实例剔除获得净收益；后者只在支持其依赖的 build/profile 中用于预览遮挡分支，不能作为产品默认或正确性证明。分别恢复为 `1` 和 `0`，重跑可见性 reference 与稳定窗口。

### 4.5 GPUScene：优化的是 dirty 增量和消费方式

GPUScene 保存 renderer 在 GPU 侧反复消费的 primitive/instance 场景数据。它存在，是为了避免每个 pass 为每个对象重复绑定大块数据，并为 Instance Culling、BasePass、shadow 和 Nanite 等路径提供稳定索引。

持久数据的 owner 是 renderer 的 Scene/GPUScene；Game/Render 变化把 primitive 或 instance 标为 dirty，update 阶段整理 CPU 侧变化，upload pass 把增量发布到 GPU buffer，后续 pass 通过 primitive/instance ID 消费。数据可跨帧驻留，dirty 状态只活到相应更新被吸收。

所以 `GPUSceneUpdate` 高时先问：本帧 dirty primitive/instance 有多少，数据量多大，是否有 per-view dynamic primitives，多 view 是否重复放大。它们都不是产品优化答案。

**GPUScene 最小实验卡**：`r.GPUScene.UploadEveryFrame=0`、`r.GPUScene.ParallelUpdate=2048`、`r.GPUScene.InstanceDataTileSizeLog2=-1` 是默认值，并带 Render Thread safe 的运行时标记。前者改为 `1` 会故意全量上传，只能判断线上形态是否接近“增量失效”；第二项的 `0` 与阈值 sweep 只能判断 CPU parallel-for 交换；第三项改为正值会选择 tiled layout 并尝试 reserved resources，平台不支持时回退普通 buffer，因此只能验证 layout/resource capability 影响。每项独立测试，恢复 `0/2048/-1` 后等待 buffer 重建和 warm-up，再复测基线。

还要分开 shadow invalidation。WPO、移动 caster 或光源变化可能使 shadow cache 失效，但“shadow 需要重画”和“GPUScene primitive data 需要上传”不是同一个状态位，也不保证同量变化。应分别观察 Scene dirty、GPUScene upload、VSM invalidation/page/raster。

### 4.6 BasePass/GBuffer：像素、primitive、材质和带宽是四个轴

BasePass 把材质/几何结果写入后续 Lighting、Lumen、Post 等系统消费的目标。它的成本可能来自：进入 pass 的 primitive/command、覆盖与 overdraw 像素、shader/permutation、GBuffer/SceneColor/velocity/depth 的写入带宽。

降低分辨率只是一项证据。若成本随内部像素数下降，说明像素覆盖、shader 或带宽相关 work 更可疑，但不能单独区分 overdraw、ALU、纹理、RT 带宽和固定成本。更有分辨力的实验组合是：固定输出并 sweep 实际 internal resolution；改变视角遮挡；替换目标材质；比较 command/primitive 数；最后用 GPU capture/counters 检查 pass 内行为。

EarlyZ 是典型交换：PrePass 先支付 depth work，期望减少 BasePass 等后续 overdraw。它可能改善高 overdraw 场景，也可能在几何重、遮挡弱或 masked 路径下增加总成本。`r.EarlyZPass` 在 UE5.7 明确不能作为运行时即时开关；相关 masked 选项还可能要求重启和 shader 重编。正式实验必须用独立启动配置，分别 warm cache，比较 PrePass、BasePass、HZB、velocity、VSM、总 GPU 和画质，而不是控制台里改值后立刻读一帧。

**BasePass 最小实验卡**：`r.BasePassWriteDepthEvenWithFullPrepass=0` 默认允许 full prepass 条件下的 readonly BasePass；设为 `1` 强制 BasePass 写 depth，适合检查 prepass 与 BasePass 内容不匹配。它是运行时可设的默认 CVar，但改变后要等 Scene depth-access 状态更新；它能证明 depth-write gate 对 PrePass/BasePass 交换的影响，不能单独证明 overdraw 或材质根因。恢复 `0`；若实验同时改变了启动期 `r.EarlyZPass`，必须恢复原启动配置并分别重启、预热。

对 Unity deferred 读者，GBuffer contract 是可用的功能坐标；但 UE 的写入位置和内容还受 EarlyZ、velocity、DBuffer、Nanite 与 debug path 改变，不能把一个 Unity RenderPass 与 UE BasePass 做一对一机制映射。

### 4.7 Lighting：同样的 light 数不代表同样的输入

Lighting 消费可见光源、GBuffer、light grid/list、阴影与其他 feature 输入。UE 先分类和排序光源，是为了让支持的光源走批量/clustered 路径，把特殊 shadow、light function、hair 或其他 feature 留给不同路径。若所有光源都走一条通用 shader，控制流和绑定更简单，但会为大量不需要的 feature 支付成本。

调试时区分 CPU 的 gather/sort/list build 与 GPU 的 light volume/clustered/deferred consume。两个镜头 light 数相同，屏幕覆盖、tile 重叠、shadow 状态和 feature 分类不同，成本可以完全不同。应记录 visible light、受影响 tile/cluster、特殊路径数量和屏幕覆盖，而不是只写“减少 light”。

### 4.8 VSM：demand、allocation/residency、raster、sampling 四段不能混

VSM 把高分辨率阴影空间切成虚拟 page，是为了只为需要的区域分配和更新 physical pages，并跨帧复用稳定内容。它用更复杂的 page table、pool、cache 与 invalidation 换取稀疏分配；传统固定 shadow map 在小场景、简单光源或平台约束下可能更直接，但会付固定分辨率和覆盖浪费。

状态链是：

```text
visible receivers request virtual pages
  -> allocator maps/reuses physical pages
  -> Nanite/non-Nanite casters rasterize pages that need content
  -> lighting samples page table and physical pool
  -> cache remains valid until eviction or invalidation
```

`VirtualShadowMapMarkPages` 高看 receiver/page demand；allocation 高看 request、reuse、pool pressure 与初始化；non-Nanite raster 高看传统 caster command/instance、masked coverage；sampling 高看 light、屏幕覆盖、filter/ray 设置。physical pool 是有预算的 residency owner，压力过大可能 eviction 或 missing/stale shadow。降低 page demand、扩大 pool、减少 invalidation、简化 caster、改变滤波质量是不同方案：它们分别交换画质、显存、更新成本和平台能力，不能只比较一个 event。

### 4.9 Nanite：渲染成本与 streaming 状态分开

Nanite 的 steady-state owner 可先按 instance、cluster traversal、covered pixels/raster、material/shading 四类分流。这比“模型三角形多”更有用，因为同一几何资产会随 view、遮挡、屏幕覆盖、material bins 和 pass 目标改变成本。

对 Unity 读者，LODGroup 只能帮助理解“距离和屏幕尺寸会改变几何工作量”；Nanite 还包含 renderer 管理的虚拟化几何、cluster culling、streaming/residency 与 material work，不能把它当成 LODGroup 的自动等价替代。

但 page request、IO、pool 和 visible resident data 属于另一条跨帧 streaming 链。第一次转向路口时 Nanite event 与 IO 同时升高，不代表 raster 算法就是根因。对照首次 traversal、稳定驻留和重启后的重复 traversal：若热态 raster 仍高，回到 instance/cluster/pixel/material；若只冷态尖峰，追 request -> IO -> async update -> resident -> consumer。

### 4.10 Lumen：功能 owner 之外还要看 cache validity

Lumen 至少分为 Surface Cache/scene update、Screen Probe Gather、Radiance Cache、Reflections。每条功能都有 producer、consumer 和跨帧 cache：场景变化会使表示失效，probe/cache 更新会在后续帧被采样，异步计算可能与 graphics 重叠。

关闭某条 tracing path 可以证明该路径参与成本，却不能证明关闭它是方案。替代方案可能是降低更新量、调整 probe/trace 预算、改善 scene representation、改变反射路径或按平台分级。它们分别交换漏光、噪声、ghosting、响应速度、显存和硬件能力。调试时把 event 与 cache 更新量、失效条件、internal resolution 和 async overlap 对齐。

### 4.11 PostProcess：mutable chain、resolution domain 与 history

PostProcess 不是固定 pass，而是 SceneColor 经过按 view/config 组织的可变链。TSR、motion blur、bloom、tonemap、post material、debug/view extension 和 secondary upscale 可能在不同 resolution domain 工作，并持有不同 lifetime 的 temporal history。

优化某个 pass 时必须记录输入/输出分辨率、链上前后关系、history 是否被重置、最终 HDR/tonemap 合同。减少 history 或改变 upscale 次序可能省 GPU/显存，却引入 ghosting、闪烁或质量变化；Editor/debug 链也不能外推到 shipping。这里的 owner 是实际启用的子 pass，不是笼统的 `PostProcessing` 父事件。

## 5. 跨帧状态：很多 hitch 根本不是“这一帧 pass 太贵”

### 5.1 Memory budget 与 residency 状态机

内存要按 owner 分开：CPU heap/asset、GPU local memory、streaming pool、VSM/Nanite 等专用 pool、render target/transient allocation。总量之外还要看峰值、碎片、预算余量和平台可用性。

**Residency** 表示资源内容是否已处于目标消费者可访问的内存位置。一个通用状态机是：

```text
not requested
  -> wanted/requested
  -> IO/decompress/create in flight
  -> uploaded/resident and safe to consume
  -> retained or evicted under policy/budget pressure
```

每次转换都有 owner：streaming manager 决定 wanted，IO/async loading 推进数据，RHI/upload 形成 GPU 资源，renderer consumer 只能在完成条件满足后读取，budget policy 决定保留或 eviction。没有 residency 管理，项目要么把所有内容常驻并迅速耗尽内存，要么在需要时同步加载并制造严重 hitch。

更大 pool 可减少 eviction，却占用其他系统预算；更激进 streaming 降低常驻内存，却增加 IO、pop 和瞬时上传；更低资源质量可减少二者，但改变画质。正确问题是“目标平台预算下，哪种状态转换导致体验失败”，而不是“内存越低越好”。

### 5.2 Texture 与 Nanite streaming

Texture streaming 应对齐 `WantedMips`、`ResidentMips`、pending、pool over-budget、IO 与画质 pop。Wanted 大于 Resident 说明需求尚未满足，但原因可能在 IO、带宽、创建/上传、pool 压力或优先级；一个 counter 不能独自选择原因。

Nanite streaming 对齐 page request、IO 吞吐、async update、streaming pool 与 visible resident data，再与 Nanite cull/raster event 对齐。两者 owner 和数据结构不同，不能用 texture pool 解释全部 Nanite residency。

Insights 的 IO/LoadTime 轨道用于看 request 和异步加载时序，subsystem CSV/stats 用于看状态量。冷/热 replay 是关键反事实：热态消失说明 residency/cache 是强候选；热态仍在则回到 steady-state workload。

### 5.3 PSO runtime hitch 与 shader compile/finalization hitch

PSO 描述一组 GPU pipeline state。若需要的 graphics/compute PSO 在 draw/dispatch 前尚未准备好，运行时创建可能形成一次性或偶发 hitch。PSO precache validation 的 hit、miss、too-late、untracked 用于判断准备是否及时；runtime hitch count/time 与 driver-cache health 信号用于对齐尖峰。

Shader compile 是另一条链：job 提交给异步 worker，结果进入 pending，Game Thread 的 `ProcessAsyncResults` 以 time slice 接入结果，某些条件还会 block 或 finish-all。于是“后台编译完成”不等于“结果已经无成本地进入运行状态”。Editor 中的实时编译行为也不能直接代表 cooked Shipping。

解决 runtime hitch 的替代方案包括更完整 precache、启动预热、扩大 PSO/shader 覆盖或减少 permutation。它们会增加 cook/build、DDC、包体、启动时间、内存和维护成本。动态创建降低预先成本，却把风险留在交互时刻。产品决策必须在目标平台和真实 cooked build 上比较这些交换。

### 5.4 Static resolution、dynamic resolution、TSR 与显存

输出分辨率是显示合同；内部渲染分辨率决定许多 pass 的像素 work；primary/secondary screen percentage 和 upscaler 决定链中何处改变 resolution domain。报告只写“1440p”而不写实际 internal resolution，无法解释 GPU 差异。

Dynamic resolution 由预算、历史、headroom、min/max 和改变周期控制内部 fraction。它的设计目的是用空间质量换取更稳定的 GPU pacing，并用 history 避免每帧对噪声过度反应。反应太快会产生分辨率波动和 temporal instability；太慢则无法及时吸收 spike。

启用动态分辨率时，固定 `r.ScreenPercentage` 可能不再拥有最终控制权。最大 fraction 还会影响 renderer 为最坏分辨率预分配的资源规模，所以“当前平均 fraction 较低”不保证显存按平均值缩小。评估必须同时看实际 fraction 历史、GPU budget、TSR/history 质量、VRAM 峰值和 pacing。

## 6. 假设、last-valid-state 与单变量实验

### 6.1 假设必须可证伪

好的假设包含 owner、数据变化、条件和预期证据：

```text
在固定路口 replay 的热态窗口中，
non-Nanite VSM raster 变高，
是因为进入该 shadow pass 的某组传统 foliage caster instances 增加，
而不是 page allocation、lighting sampling、streaming 或 Present。

若只移除该 caster group 的 shadow 输入，
对应 command/instance 和 raster event 应下降；
allocation、lighting、CPU setup、streaming 与 Present 不应等量反弹。
```

“VSM 太贵”“foliage 太多”都不是可用假设，因为没有指出哪段状态链、什么数据、由谁消费、怎样被反证。

### 6.2 用 last-valid-state 阻止证据跳步

每轮调查都维护下面的记录：

```text
Observation: 哪个统计分布或 hitch 触发调查？
Last valid state: 证据最后确认到哪个 owner、数据状态和完成深度？
First invalid/unknown state: 从哪里开始只剩相关性或猜测？
Next discriminating test: 哪个单变量实验能区分剩余假设？
Invalidation conditions: 哪些平台、cache、resolution、build、streaming 条件会使证据失效？
Artifact: CSV、trace、ProfileGPU、capture、配置和 revision 在哪里？
```

例如，`ProfileGPU` 只显示 `RenderVirtualShadowMaps(Non-Nanite)` 高时，last-valid-state 是“该捕获帧的 GPU non-Nanite VSM raster work 高”。它还没有证明 foliage，也没有证明 page residency 正常。只有 counters 和隐藏实验继续推进证据，才能更新 last-valid-state。

### 6.3 CVar 是实验控制，不是产品答案

每个 CVar 实验先记录：source/default、实际生效值、读取 owner/thread、runtime 是否可变、是否被 scalability/device profile/命令行覆盖、是否需要重启、shader 重编、render-state recreate 或 cache 重建，以及恢复方法。

| CVar 类型 | 合法实验方式 | 主要风险 |
| --- | --- | --- |
| Runtime 可变且 owner 明确 | 同一进程 A/B，仍需稳定窗口和恢复 | 改变 task/queue 形态，短期 cache 未稳定 |
| Render-thread-safe/异步生效 | 等待状态发布和 GPU 消费后采样 | 改值瞬间不代表新状态已完成 |
| Startup/read-only | 两个独立启动配置，分别 warm-up | 不能控制台即时 A/B |
| Shader/PSO 相关 | 独立构建或完整重编/预热 | cold cache、build、DDC、包体变化 |
| Platform/device-profile override | 在每个目标 profile 上验证最终值 | 本地控制台值可能被平台配置覆盖 |

`r.EarlyZPass` 属于启动期实验，不应与普通即时 CVar 放在同一套控制台 A/B 中。`r.GPUScene.UploadEveryFrame` 是故意破坏增量上传的 debug 开关。`r.MeshDrawCommands.DynamicInstancing` 可以证明当前 workload 是否依赖动态合批，但最终方案应修复意外打散的共享状态或内容组织，而不是把默认开关写成“优化发现”。

### 6.4 单变量的含义是只改变一条因果边

只隐藏 caster、只换目标材质、只固定 internal resolution、只切一个启动配置，都是单变量实验。它不要求系统里只有一个数字变化，而是要求**主动控制只改变一个原因**；由它引起的 command、page、event、Frame 等连锁变化正是要观察的结果。

实验后必须回到同一合同：同一 replay、窗口、cache 状态、build、平台和工具集合。A/B 顺序可交错运行以减少温度和时间漂移。实验配置必须恢复并重新采一轮基线；若恢复后不能回到原分布，说明存在 cache、streaming、编译或环境漂移，当前 A/B 不能直接用于产品结论。

## 7. 路口 spike：把证据推进到产品决策

下面的数字是**教学用的完整记录示例，不冒充某个真实项目的实测结果**。它演示一份 decision-complete 记录应怎样连接状态；在项目里必须用目标硬件生成自己的 CSV、trace 和 capture。

### 7.1 合同与 run matrix

在查看结果前，示例先冻结两个 profile 的合同。两者使用相同内容 revision 和同一逻辑 replay，但只在各自 profile 内做 A/B，不把不同分辨率和 RHI 的绝对值互相比较。

| 合同项 | D3D12 主 SKU | Capability Profile B |
| --- | --- | --- |
| Build / RHI / capability | packaged Development；D3D12；VSM 与对应 physical pool 路径启用 | packaged Development；Vulkan；使用另一 shadow capability，不存在同一 VSM physical pool 指标 |
| 分辨率与同步 | 2560x1440；固定内部 fraction；dynamic resolution、VSync、cap 关闭 | 1920x1080；固定内部 fraction；dynamic resolution、VSync、cap 关闭 |
| Workload | 同一 replay：frame 1200 转向路口，采样到 frame 3000；相机、光源、天气、随机种子固定 | 同一逻辑 replay 和 frame 窗口；平台专属 shader/streaming 状态独立预热 |
| Cold / warm | cold 为进程重启并清空本案例可控缓存后的首次 traversal；warm 为资源驻留稳定后的下一次 traversal | 同一定义；两种状态分别统计，禁止混样 |
| Runs / 噪声带 | baseline/改后各 7 个 cold runs 和 7 个 warm runs；warm median 噪声带 ±0.35 ms，cold p95 ±1.2 ms | baseline/改后各 7 个 cold runs 和 7 个 warm runs；warm median 噪声带 ±0.30 ms，cold p95 ±1.0 ms |
| Warm 性能预算 | Frame median <= 16.67 ms，p95 <= 18 ms；`>33.3 ms` hitch rate < 0.1% | Frame median <= 20 ms，p95 <= 22 ms；`>33.3 ms` hitch rate < 0.2% |
| Cold / streaming 预算 | Frame p95 <= 42 ms；IO peak <= 900 MB/s；texture/Nanite pending 在 frame 1380 前清空；无 residency fault 或 pool over-budget | Frame p95 <= 30 ms；IO peak <= 650 MB/s；pending 在 frame 1400 前清空；无 residency fault 或 streaming pool over-budget |
| Latency / Present | input-latency signal <= 50 ms；Present p95 <= 1.0 ms | input-latency signal <= 55 ms；Present p95 <= 1.0 ms |
| 质量与内存门 | 24/24 shadow reference 与 transition replay 全通过；VRAM peak <= 8.0 GB；VSM pool peak <= 85% | 同一 24/24 reference 与 transition replay 全通过；VRAM peak <= 5.5 GB；VSM pool 为 N/A |

正式采样前，两边都在不进入目标路口、也不请求该区域资产的固定预备视图执行 600 帧 warm-up，只用于稳定硬件频率，因此 cold run 仍是路口资产的首次 traversal；warm run 则要求目标区域 streaming pending 已清空后再重复。任何 A/B 差值必须超过对应噪声带，质量与内存门是硬门，不允许用性能收益抵消。

先分开冷/热态。示例结果：

| 样本 | Frame median / p95 | GPU median | Draw / RHIT | Present | 关键伴随信号 |
| --- | --- | --- | --- | --- | --- |
| 主 SKU 冷态首次 traversal | 35.4 / 48.2 ms | 27.1 ms | 12.0 / 6.1 ms | 0.5 ms | IO peak 980 MB/s；pending 到 frame 1420；无 residency fault |
| 主 SKU 热态基线 | 27.8 / 29.6 ms | 25.0 ms | 10.9 / 5.2 ms | 0.4 ms | streaming 稳定，spike 可重复 |
| Profile B 冷态首次 traversal | 29.1 / 34.0 ms | 19.0 ms | 9.2 / 4.5 ms | 0.6 ms | IO peak 720 MB/s；pending 到 frame 1450；无 residency fault |
| Profile B 热态基线 | 21.0 / 23.2 ms | 18.5 ms | 8.6 / 4.1 ms | 0.5 ms | streaming 稳定，spike 可重复 |

冷态额外尖峰进入 streaming 分支，不能混进本次 steady-state VSM 假设。热态 `stat unit` 只把调查推进到“GPU 是当前 throughput critical path，Render setup 也值得监控”。普通 Game 数值没有被解释成等待下游。

### 7.2 从统计到 owner，不从 event 直接跳内容

多帧 `stat gpu` 与代表帧 `ProfileGPU` 显示 non-Nanite VSM raster 在转向后持续增加，而 page marking/allocation 和 Lighting sampling 增幅较小。此时记录：

```text
Last valid state:
  热态路口窗口 GPU-bound；
  VSM non-Nanite raster 是主要增量 owner。

Unknown:
  是 caster command/instance 增加、masked coverage、cache invalidation，
  还是捕获扰动或其他 queue overlap 改变。
```

接着对齐 VSM page/pool/invalidation、shadow command/instance 与 Insights：physical pool 没有 over-budget，热态 allocation 稳定；Render Thread shadow setup 有增长；转向后进入 non-Nanite shadow pass 的目标 foliage instance 明显增加。现在 last-valid-state 才能推进到“该 caster group 是强候选输入”，仍不能写“foliage 太多”。

### 7.3 单变量反事实

诊断实验只关闭目标 foliage group 的 shadow casting，保留可见几何、相机、光源、分辨率和其他系统。示例 A/B 结果：

| 指标 | 热态基线 | 诊断实验 | 解释 |
| --- | ---: | ---: | --- |
| 目标 shadow command/instance | 100% | 31% | 主动改变的输入确实进入该 pass |
| Non-Nanite VSM raster median | 8.7 ms | 2.8 ms | 目标 event 与输入同向下降 |
| VSM allocation | 1.1 ms | 0.9 ms | allocation 不是主要收益 owner |
| Lighting sampling | 2.3 ms | 2.2 ms | 未把成本等量转移到 sampling |
| GPU / Frame median | 25.0 / 27.8 ms | 15.2 / 16.5 ms | critical path 显著缩短 |
| Draw / RHIT / Present | 无显著反弹 | 无显著反弹 | 没转移到 CPU submit 或 Present |

该实验把 last-valid-state 推进到：“目标 foliage shadow input 是此热态 VSM raster spike 的主要因果输入。”它仍不授权直接删除阴影，因为诊断实验破坏了产品画质。

### 7.4 比较替代方案，而不是把诊断开关提交进项目

至少比较三类方案：

- **内容方案**：为远距离 foliage 使用更便宜的 shadow caster/LOD、收紧不合理 bounds、减少 masked shadow coverage、对不重要实例限制投影范围。适合根因集中在特定内容族，代价是内容制作、LOD transition 和阴影细节。
- **系统方案**：减少不必要 invalidation、改善 cache reuse、调整 page/pool 策略。适合问题来自跨帧失效或 residency，而不是本例已证实的稳定 caster raster；扩大 pool 还会增加显存占用。
- **质量方案**：减少 page demand、filter/ray 质量或影响范围。适合平台预算不足且可接受质量交换，代价是阴影稳定性、细节或噪声。

示例产品决策选择“目标 foliage 族的 shadow LOD/caster policy”，owner 为 Technical Art 与 Rendering；只对通过 reference 的距离和平台 profile 生效，并保留 rollback。它比全局降低 VSM 质量范围更窄，也比扩大 pool 更符合已证明的 raster 根因。若另一个平台的阴影路径、reserved resource、async 或内存条件不同，这个决策必须重新验证，不能因为 D3D12 通过就外推。

### 7.5 改后验证与 baseline 恢复

产品改动必须重新跑冷/热态和平台矩阵，而不是复用诊断实验数字。下表仍是教学示例数据，不是本项目实测；它展示所选 shadow LOD/caster policy 怎样接受或拒绝 rollout。

| 验收项 | D3D12 主 SKU baseline | D3D12 主 SKU 改后 | Capability Profile B baseline | Profile B 改后 | 判定 |
| --- | ---: | ---: | ---: | ---: | --- |
| Warm Frame median / p95 | 27.8 / 29.6 ms | 16.4 / 17.6 ms | 21.0 / 23.2 ms | 19.0 / 21.5 ms | 主 SKU 达到 16.67/18 ms 合同；B 达到其 20/22 ms 性能合同 |
| `>33.3 ms` hitch rate | 1.8% | 0.0% | 0.2% | 0.1% | 性能门通过 |
| Cold Frame median / p95 | 35.4 / 48.2 ms | 30.8 / 39.6 ms | 29.1 / 34.0 ms | 25.7 / 28.9 ms | 两边都达到 7.1 预先冻结的 cold p95 预算 |
| Cold IO peak / pending 清空帧 | 980 MB/s / 1420 | 820 MB/s / 1340 | 720 MB/s / 1450 | 610 MB/s / 1360 | 两边都达到预先冻结的 IO 与完成帧门槛 |
| Cold streaming / residency | pending 上升；0 fault；pool 未 over-budget | pending 如期清空；0 fault；pool 未 over-budget | pending 上升；0 fault；pool 未 over-budget | pending 如期清空；0 fault；pool 未 over-budget | 无 eviction、residency fault 或 streaming residency pop；cold 数据生命周期门通过 |
| GPU / Frame median | 25.0 / 27.8 ms | 15.1 / 16.4 ms | 18.5 / 21.0 ms | 16.6 / 19.0 ms | 主 SKU 仍由 GPU 领跑但未超预算；无新 CPU critical path |
| Input-latency signal / Present p95 | 47.2 / 0.7 ms | 46.8 / 0.7 ms | 52.0 / 0.8 ms | 51.0 / 0.8 ms | frame-flip 深度信号与 Present 无回归；不声称 input-to-photon |
| VRAM peak / VSM pool peak | 7.4 GB / 82% | 7.3 GB / 69% | 5.2 GB / N/A | 5.1 GB / N/A | 主 SKU 内存门通过；B 使用不同 shadow capability，VSM pool 不适用 |
| Shadow reference / transition replay | 24/24 + transition 通过 | 24/24 + transition 通过 | reference 通过 | 远距离切换可见跳变 | 主 SKU `PASS`；B 质量门 `FAIL` |
| Runtime PSO / shader-finalization hitch | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 无新编译类 critical path |
| Rollout | baseline | 启用该 policy | baseline | 回滚 baseline policy | 主 SKU `PASS`；Profile B `FAIL` 并回滚；其 VSM 项为 `N/A` |

教学示例的结论是：D3D12 主 SKU 的 warm、cold、IO/streaming/residency、latency、内存与质量合同全部达成，GPU 仍是最长线但已低于预算，Draw/RHIT/Present 没成为新 critical path；Profile B 的 warm、cold、IO/streaming/residency、latency 和内存合同达成，却因预先冻结的质量门失败而不获准 rollout。所有诊断 CVar 和“隐藏 caster”开关已恢复，恢复运行回到原基线噪声带；产品改动只保留在主 SKU profile。

示例 artifact bundle 记录为 `OPT24_RoadJunction_ShadowLOD_v1`，包含 cold/warm CSV、IO/streaming trace、ProfileGPU、reference、配置和 revision。rollback 条件是任一目标 profile 出现 reference 失败、warm/cold p95 超预算、IO/pending/residency 门失败、内存 pool 越过合同上限或新 PSO/shader hitch；触发后恢复 baseline policy，而不是继续保留局部 GPU 收益。

这一步保留了原案例最有价值的判断：**目标 event 降低但瓶颈转移，不算 frame 级完成。** 现在检查范围扩展到了 latency/pacing、Present、streaming/residency、compile、memory、quality、build 和 platform。

## 8. 自动化基线：把一次调查变成持续产品约束

手工捕获适合深入一个代表帧，不能承担长期回归。自动化 owner 应把 workload、metadata、统计和 artifact 绑定到 revision。

一个最小自动化流程是：

```text
固定命令行/config/device profile
  -> 启动并记录 build、RHI、分辨率、dynamic-res、VSync/cap
  -> warm-up 或执行明确的 cold run
  -> replay 指定窗口
  -> CsvProfile + FPSChart + subsystem counters
  -> 多 run 汇总 median/percentile/hitch rate/noise band
  -> 与基线和绝对预算比较
  -> 超阈值时保存 trace/profile/capture 深挖
```

Metadata 至少包含 revision、平台/SKU、build、RHI、device profile、driver、输出和内部 resolution、upscaler、动态分辨率范围、同步模式、cache 状态、workload 版本与采集开关。否则几周后的 CSV 无法证明两次运行可比。

回归门槛应同时包含绝对预算和相对变化。只看相对百分比会让本已超预算的基线继续恶化；只看绝对值又可能漏掉尚未越线的明显退化。阈值要大于测得噪声带，并为 steady-state、hitch、cold traversal、memory 分别定义。

## 9. 产品决策矩阵：性能收益必须通过其他系统的门

最终交付项不是“某 CVar 从 1 改成 0”，而是一项有 owner、适用范围、平台条件、验证证据和 rollback 的产品改动。

| 决策维度 | 必须回答的问题 | 常见替代方案与交换 |
| --- | --- | --- |
| Frame/latency/pacing | 目标预算真的改善，还是只移动 event/等待？ | 更深流水提高吞吐；更浅同步降低 latency；dynamic res 稳 pacing |
| Quality/correctness | reference、运动、边界场景和 temporal artifact 是否通过？ | 降质量、改算法、改内容各自影响不同 |
| Memory/residency/IO | pool、峰值、eviction、cold traversal 是否回归？ | 常驻更多减少 hitch但占内存；流式更多反之 |
| PSO/shader/build | runtime hitch 是否换成可接受的 build/prewarm 成本？ | precache/permutation/启动预热交换 cook、包体和启动时间 |
| Platform | capability、RHI、feature level、device profile、功耗是否覆盖？ | 全局方案简单；profile override 更精确但维护成本高 |
| Ownership | 谁维护内容规则、系统设置、自动化阈值和 rollback？ | 窄范围改动更可控；全局开关更易部署但风险面更大 |

“另一种方案何时更好”必须写出目标和条件。例如，大 pool 不是普遍优于更积极 streaming；在显存充足且 traversal hitch 是主要风险时它可能更好，在低内存设备上则可能引发更严重 eviction 或 OOM。较低 internal resolution 不是普遍优于材质优化；在像素受限且 upscaler 质量可接受时更有效，在几何/CPU/PSO hitch 上则可能几乎无效。

## 10. Completion checklist：什么情况下才能说优化完成

一次章节案例或产品优化只有同时满足以下条件才算闭环：

- workload、目标、平台、build、RHI、分辨率、pacing、dynamic-res 和冷/热态已冻结。
- 基线与改后都有多个 run；样本数、median、关键 percentile、hitch count/rate 和噪声带已记录。
- 问题类型已区分：steady-state、periodic、cold traversal、streaming、compile/PSO、Present/pacing 没被混成一个平均值。
- last-valid-state 到产品根因之间没有从 event 直接跳到内容或配置结论。
- 目标 Frame、latency 和 pacing 预算达到，而不只是局部 event 下降。
- Game、Render、RHI、GPU graphics/async/copy、Present 和 streaming 的新 critical path 已检查。
- memory、residency、pool、IO 和瞬态峰值没有不可接受回归。
- PSO runtime creation、precache miss/too-late、shader compile/finalization 没制造新 hitch。
- 画质与正确性 reference 通过；temporal noise、ghosting、missing/stale data、culling error、LOD/HLOD transition 已检查。
- 目标平台/device profile/RHI capability matrix 通过；单个平台结论没有未经验证外推。
- shader permutation、PSO cache、cook/build、DDC、包体和启动预热交换已记录。
- 实验 CVar 与诊断内容改动已恢复；恢复基线能复现原分布。
- 交付项有明确 owner、适用范围、平台条件、回滚触发器和维护成本。
- CSV、trace、ProfileGPU、capture、配置、revision 已归档，自动回归可重复执行。

## 11. 主线回放

回到开头的路口 spike。第一步不是猜 VSM、Lumen 或 draw call，而是冻结产品目标与 workload；第二步把症状放进 CPU、RHI、GPU queue、Present 和 streaming 的真实时间线，找当前完成目标的 critical path；第三步按统计、时序、event、counter、capture 的观测深度收敛；第四步把昂贵 event 还原成 owner、输入数据和 lifetime；第五步用 last-valid-state 写出可证伪假设；第六步只改变一条因果边；第七步检查新 critical path 和质量、内存、构建、平台副作用；第八步恢复基线并自动复测；最后才由有 owner 的产品改动结束调查。

整条闭环可以压缩成六个问题，但每个问题都必须有证据：

```text
我是否在比较同一个 workload 和同一种 cache/lifetime 状态？
哪个产品完成目标超预算，critical path 真正在哪里？
当前工具证明到了哪个 owner 和完成深度？
哪个输入数据变化可以解释它，last-valid-state 到哪里？
哪个单变量实验能证伪剩余假设，并且恢复后仍可复现？
最终改动是否通过 frame、latency、pacing、质量、内存、构建和平台门禁？
```

能回答这六问，才是可维护的优化；否则当前动作仍只是一次调参。
