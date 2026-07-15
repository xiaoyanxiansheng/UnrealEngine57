# 17 Lumen：Surface Cache、RT 与 Screen Probe 如何协作

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `05_RenderGraph.md`、`06_GPUScene.md`、`08_FrameInit.md`、`10_BasePass.md`、`11_Shadows.md`、`12_Lighting.md`、`15_PostProcessing.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）
> **验证记录**: 见同目录 `17_Lumen_CoverageMatrix.md`

## 开篇：Lumen 解决的不是“怎么打一条光线”，而是“怎么负担得起足够多的光线”

第一次接触 Lumen，最容易形成两个相反但同样不准确的印象。

一种印象是：Lumen 就是更复杂的屏幕空间 GI。既然画面已经有 SceneColor、Depth 和 GBuffer，那么把屏幕上的颜色沿深度传播一下，似乎就能得到间接光。这个思路成本低，但它不知道屏幕外有什么，也不知道被前景挡住的表面是什么。相机一转，原来可见的信息消失，GI 就会失去依据。

另一种印象是：只要打开硬件光线追踪，Lumen 就会为每个像素发很多光线，命中真实三角形后重新执行完整材质、遍历灯光、追阴影，再计算多次反弹。这个思路能得到更直接的几何证据，但实时帧预算通常承担不起如此多的命中点着色；场景中的加速结构还要随实例变化而更新，动态场景越大，维护成本越明显。

Lumen 的核心设计不是在这两个极端里二选一，而是把一次完整的间接光查询拆成几个可以分别降成本的问题：

1. **世界如何被表示，才能让光线快速找到“可能命中了哪里”？**
2. **命中以后，如何快速知道这块表面此刻向外提供多少 radiance，而不是每次重新计算完整材质和光照？**
3. **当前视图真的需要多少查询？能否让一组 probe 共享结果，而不是每个像素独立承担全部射线？**
4. **哪些结果值得跨帧保存？保存多久？什么时候必须拒绝旧结果？**
5. **当一种表示缺少信息时，下一种表示怎样只接手仍未解决的查询？**

因此，更准确的第一印象是：

> **Lumen 是一个由多种场景表示、分层射线查询、跨帧缓存和当前视图积分共同组成的实时光照系统。它不是“不计算”，而是避免在每个像素、每条射线、每次命中上重复最昂贵的计算。**

### 先稳定五个新词

在进入流程前，先把本章反复使用的五个词放稳。它们不是同一层概念。

**Representation（表示）**，是系统为了某种查询而保存的世界近似。三角形、Mesh SDF、Global SDF、Surface Cache card、上一帧屏幕历史和 Radiance Cache probe 都是表示，但它们保留的信息不同。表示不是“真实世界本身”；选择一种表示，就是选择要保留什么、愿意丢掉什么，以及更新它要付出多少成本。

**Query（查询）**，是当前消费者向这些表示提出的问题。例如：“这条 ray 命中了什么？”“命中点的间接光是多少？”“这个像素周围半球的 irradiance 是多少？”同一条查询可以先问屏幕历史，失败后再问 HWRT 或 SDF。

**Cache（缓存）**，是为了避免重复昂贵工作而保存的可复用结果。Surface Cache 保存贴在表面上的材质与 lighting；Radiance Cache 保存世界空间 probe 的低频 radiance。两者都叫 cache，但缓存键、owner、失效条件和消费者完全不同。

**History（历史）**，是跨帧复用的状态总称。它不等于一张“上一帧颜色”。本章至少会遇到场景级 Surface Cache、GPU→CPU feedback readback、probe-domain history、像素级 indirect history、Radiance Cache state 和 reflection denoiser history。把它们混成一个 history，会直接破坏调试判断。

**Owner（所有者）**，不是“哪个函数最后写过它”，而是谁负责保证这份数据在自己的生命周期内有效、决定何时更新或拒绝它，并把它发布给消费者。RDG pass 可以在本帧写一张 atlas，但跨帧 owner 仍可能是 `FLumenSceneData` 或 `FViewState`。

### 贯穿案例：红墙如何把白色地面染红

本章始终跟随同一个场景：

> 一面红色漫反射墙被太阳照亮。墙本身不发光，但它会把接收到的光以偏红的漫反射 radiance 重新送入环境。墙旁边是一块白色粗糙地面和一面镜子。我们要解释：白色地面上的红色溢色、粗糙地面的低频高光，以及镜子里清晰的红墙，分别如何得到。

为了让各种边界真正出现，案例会经历四个变化：

- 相机先正对红墙，红墙在屏幕内；随后猛转头，让红墙离开屏幕。
- 相机从远处走近，Surface Cache 需要从粗 page 逐渐提高到更细 page。
- 中途发生 camera cut，用来观察哪些 history 必须丢弃、哪些 scene cache 仍然可以存在。
- 同一份红墙 radiance 同时影响粗糙地面和镜子，用来区分 probe rough specular 与专用 Lumen Reflections。

### 全章唯一主线

后面所有机制都挂在这条状态链上：

```text
世界对象进入 Lumen 可查询表示
  → Surface Cache 建立 card / page / material atlas
  → Surface Cache lighting 把“表面是什么”变成“表面提供多少 radiance”
  → 当前 view 放置 Screen Probe
  → 每条 probe ray 依次查询屏幕、HWRT 或 SDF、远距离 fallback
  → Radiance Cache 为低频和远距离查询提供可复用结果
  → probe radiance 被过滤并积分回像素
  → diffuse / rough specular 与专用 reflections 在 composite 边界进入 SceneColor
  → 各类 cache、history 和 feedback 按各自条件进入后续帧
```

这是一条**数据有效性主线**，不是源码调用栈。调试时要问的也不是“某函数跑没跑”，而是“最后一个已经成立的状态是什么，下一份数据为什么没有成立”。

## 本篇边界

本篇负责回答：**Surface Cache、分层 tracing、Screen Probe、Radiance Cache 和 Reflections 如何协作，最终形成 Lumen diffuse indirect 与 reflection 输入。**

- 第 12 篇负责 classic direct lighting 的灯光路由和 GBuffer 着色。本篇只把 direct light、shadow 或 VSM 结果视为 Surface Cache lighting 的输入，不重复逐灯 deferred lighting。
- 第 16 篇负责 Nanite 的 cluster、culling、raster 和 material bin。本篇只讲 Nanite 如何把 card capture 结果交给 Lumen，不展开 Nanite 内部算法。
- 第 18 篇负责 MegaLights 的固定预算多光源直接光采样。MegaLights 与 Lumen 可以共享 HZB、SDF 或 HWRT 基础设施，但不共享采样 owner。
- 第 19 篇负责 VSM 的虚拟页需求、分配、渲染和缓存。本篇不教授 VSM page table，只说明 VSM 可以成为 Surface Cache direct lighting 的阴影证据。
- 本篇会解释 Lumen radiosity 在 Surface Cache 中承担什么职责，但不推导完整 SH、BRDF 或 radiosity 数学公式。

## 本篇必须能回答

读完后，应该能够回答：

- 为什么 Lumen 同时需要屏幕历史、Surface Cache、SDF、HWRT 和 Radiance Cache，而不是只保留一种“最准确”的表示？
- Surface Cache 的 Card、Page、Page Table 和 Atlas 分别解决什么问题？缺少其中任意一层会付出什么成本？
- `FLumenSceneData`、`FLumenSceneFrameTemporaries` 和 `FViewState` 各自拥有什么，生命周期如何交接？
- Card Capture 为什么使用独立视图？classic mesh 与 Nanite 的 capture 路径为什么不能混成一句“最低 LOD 重画”？
- Surface Cache material atlas 与 lighting atlas 为什么分开？direct 和 radiosity 为什么还需要独立预算？
- feedback 为什么不是固定 N→N+1？为什么 last-used buffer 也不等于 GPU→CPU readback？
- Screen Probe 为什么采用均匀网格加有限自适应，而不是每像素追踪或完全自适应？
- 一条 ray 如何以“未解决集合”的形式从 screen trace 流向 HWRT/SDF，再流向远距离 fallback？
- Radiance Cache 的 clipmap、indirection 和 trace budget 各自解决什么问题？
- `LightIsMoving`、gather 输出槽位 0–2 和上层 reflection 槽位 3 是什么关系？
- 粗糙地面的 specular 与镜子里的清晰反射为什么不能共享同一种采样与 history？
- camera cut 后，哪些 history 应被拒绝，哪些 scene cache 仍可继续存在？
- 如何用 last-valid-state 证据梯定位 Lumen 问题，而不是随机开关 CVar？

## 1. 先选“世界的表示”，再谈 ray 命中

### 1.1 为什么没有一种表示可以包办全部查询

实时渲染希望同时得到四件事：

1. 对细小几何和遮挡关系足够准确；
2. 屏幕外也能查询；
3. 动态变化能及时更新；
4. 每帧查询和维护成本足够低。

这四个目标互相拉扯。真实三角形和命中点完整着色最直接，但加速结构、材质和灯光求值昂贵；屏幕历史极便宜且细节丰富，但没有屏幕外信息；全世界高精度体素可以覆盖屏幕外，却会带来巨大的内存和更新成本；低频 probe 很稳定，但无法表达镜面反射所需的方向高频。

UE 的选择是让不同表示承担不同问题，再用明确的 fallback 连接它们。下面先看“每种表示保留了什么”，而不是先记它们的函数名。

### 1.2 Representation 信息保留表

| 表示 | 保留的信息 | 主动舍弃或近似的信息 | 更新与存储代价 | 更适合解决的问题 |
| --- | --- | --- | --- | --- |
| 上一帧屏幕历史 / HZB | 当前视图曾看见的细节、深度和屏幕 radiance | 屏幕外、遮挡后、刚暴露区域；还可能带时间滞后 | 低，已有屏幕资源可复用 | 最先尝试的近屏幕细节命中 |
| Surface Cache Card/Page | 从有限方向参数化后的表面材质与缓存 lighting | 精确三角形形状、所有观察方向、无限分辨率和即时更新 | 需要 card coverage、atlas 内存、capture 与 lighting 预算 | ray 命中后的低成本表面 lighting 查询 |
| Mesh SDF | 单个 mesh 周围的有符号距离结构 | 精确三角形边缘、完整材质和极薄细节 | 需要生成、存储和更新 distance field | 软件 detail trace 的几何命中 |
| Global SDF | 世界空间 clipmap 中合并后的粗距离场 | 每个 mesh 的独立细节与高频几何 | 覆盖范围大但分辨率较粗 | 软件路径的远距离或剩余 miss |
| Heightfield | Landscape 一类高度场的专用几何结构 | 非高度场、悬垂和任意封闭几何 | 对地形高效，但适用对象有限 | 软件 detail trace 中的 Landscape |
| HWRT acceleration structure | 可供硬件遍历的实例和几何命中证据 | 命中本身不自动等于完整材质与 lighting；动态结构仍需维护 | 几何命中质量高，场景更新和 hit lighting 可能昂贵 | 精确世界命中、镜面和高质量 tracing |
| Radiance Cache | 世界空间 probe 的低频 radiance/irradiance | 精确表面边界、镜面方向高频和逐三角形命中 | 需要 clipmap、probe atlas 与受预算更新 | 远距离、rough、miss 和多个消费者共享的低频结果 |

这张表里最重要的边界是：

- **HWRT、Mesh SDF、Global SDF 和 heightfield 主要回答“可能命中了哪里”。**
- **Surface Cache 主要回答“命中表面已经缓存了什么材质与 lighting”。**
- **Radiance Cache 主要回答“这个世界空间邻域有没有可插值的低频 radiance”。**
- **屏幕历史同时提供便宜命中与已有 radiance，但覆盖范围受当前/历史视图限制。**

如果把这些都叫“RT backend”，读者会误以为打开 HWRT 后 Surface Cache 和 Radiance Cache 就失去作用。实际情况是：HWRT 可以替换几何命中方式，但命中后仍可以采 Surface Cache；Radiance Cache 仍可以被远距离、rough reflection 或其他消费者使用。

### 1.3 为什么 UE 选择分层表示

这是一个工程选择，不是数学上唯一可能的实现。

**纯屏幕空间方案**会更便宜，也不需要维护离屏场景表示。对固定镜头、狭小画面或允许明显离屏缺失的项目，它可能是更合适的成本选择；代价是相机转动、遮挡变化和屏幕边缘会暴露根本性信息缺口。

**纯 HWRT + 完整 hit lighting**更接近“命中真实几何后当场算光”。在离线渲染、低像素数、高端硬件，或镜面质量优先且场景规模受控时，它可能更合适；代价是动态实例更新、每命中材质、灯光和阴影计算都会放大 GPU 成本。UE5.7 的硬件 RT 选项本身就提醒：实例数量超过约十万时，场景更新可能产生显著成本。这不是一个超过阈值就必然失败的硬边界，而是必须纳入预算评估的风险。

**高精度世界体素或稠密 probe grid**可以统一查询形式，但内存会随空间体积而不是可见需求增长；动态场景还要持续重建。它们在小范围、规则空间或固定体积中可能更适合，但不适合作为任意开放世界的无条件默认。

Lumen 当前主路径选择多表示，是为了把精确度和成本分配给真正需要的查询：屏幕已有的信息先复用；剩余查询才进入世界表示；命中后的昂贵 lighting 尽量从缓存取得；低频远距离结果再由 Radiance Cache 共享。

### 1.4 这条路径什么时候根本不会成立

在开始调试“GI 为什么错”之前，必须先确认 Lumen 对这个 view 有资格运行。大体上需要：

- 当前平台和项目配置支持 Lumen；
- view 属于可维护状态的正常场景视图，而不是 planar reflection 或 reflection capture 这类被排除的次级视图；
- view 有可持久化的 state；
- 当前视图选择了 Lumen GI，并且相关 show flag / scalability 没有关闭它；
- 当前不是 path tracing 或 ray tracing debug 这类互斥模式；
- HWRT 或软件 distance field tracing 至少有一种可用。

这组条件属于**路径资格**，不是 Surface Cache coverage。若资格不成立，后面检查 page、probe 或 history 都没有意义。last-valid-state 的第一层永远是“这个 view 是否真的选择并允许 Lumen”。

## 2. O/D/C/L：先分清谁拥有、装什么、谁推进、活多久

Lumen 难读的一个重要原因，是同一条光照链同时跨过 scene、view、frame graph、GPU buffer 和 CPU readback。只说“这是 Lumen 数据”远远不够。这里用四个问题稳定所有权：

- **O — Owner**：谁负责保证状态有效？
- **D — Data**：里面实际装什么？
- **C — Control**：谁决定它何时更新、失效或发布？
- **L — Lifetime**：它活在一帧、一个 view，还是多个 frame？

### 2.1 `FLumenSceneData`：场景表示的持久 owner，但不一定全场只有一份

`FLumenSceneData` 保存 Lumen Scene 的持久部分：primitive groups、mesh cards、cards、page table、Surface Cache allocator、持久 pooled buffers、材质 atlas、lighting atlas、radiosity 状态、feedback/readback 以及待处理的场景增删改。

把它简单称为“scene owner”有助于建立第一印象，但必须加上作用域：UE 可以使用默认的 Lumen Scene Data，也可以为特定 view 或 GPU 选择单独的数据。渲染时不是无条件访问一个全局单例，而是根据当前 view、共享 view key 和 GPU mask 找到对应的 Lumen Scene Data。

为什么需要这种复杂度？因为 scene capture、多 view 或多 GPU 场景可能需要不同的可见集合、缓存状态或 GPU 资源。如果强制所有 view 共用唯一缓存，可能减少内存，却会引入错误共享、跨 GPU 同步和次级视图污染；如果每个 view 永远独占完整缓存，又会显著增加内存和重复维护。默认共享加按需 view/GPU-specific，是两者之间的工程折中。

### 2.2 `FLumenSceneFrameTemporaries`：当前 RDG 图里的发布面

跨帧 pooled resource 不能直接假装成当前 RDG graph 的内部资源。`FLumenSceneFrameTemporaries` 保存的是本帧已经注册到 RDG 的 texture、buffer、SRV/UAV，以及本帧 final gather 和 reflections 可共享的输出。

它的 owner 是当前 renderer/frame graph，生命周期主要是这一帧。它不负责让 Surface Cache 活到下一帧；它负责让本帧 pass 在 RDG 依赖、barrier 和 async compute 规则下安全访问这些资源。

如果让 `FLumenSceneData` 直接绕过 RDG 把所有持久资源交给 pass，RDG 就无法完整理解读写依赖；如果反过来让 RDG 临时对象承担跨帧所有权，graph 结束后历史又无处存放。UE 因此把“跨帧拥有”和“本帧发布”拆开。

### 2.3 `FViewState`：当前相机观察历史的持久 owner

Screen Probe 的布局与时间历史、完整分辨率的 diffuse/rough history、Radiance Cache state、opaque/translucent/water reflection history，都与一个可持续观察世界的 view 关联。这些状态由 `FViewState` 下的 Lumen view state 持有。

为什么不是 `FLumenSceneData`？因为两个相机可以观察同一 scene，却拥有不同屏幕位置、遮挡、运动矢量、probe 分布和时间重投影。把这些 history 放进 scene cache，会让不同 view 互相污染。

为什么不是纯 frame resource？因为 temporal accumulation 和 Radiance Cache reuse 都需要跨帧。没有 `FViewState`，每帧都要冷启动，噪声、闪烁和重复 trace 成本都会上升。

### 2.4 Unity/SRP 有限迁移桥：相似的是问题分层，不是对象等价

如果你来自 Unity SRP，可以用三组**有限类比**帮助定位 UE 概念，但应先保留上面已经建立的 UE owner 边界：

- `FLumenSceneData` 中的 Surface Cache 很像由 renderer 长期维护的一组 persistent `RenderTexture` / `ComputeBuffer`：它跨帧保存世界侧缓存。相似点是“资源不能每帧从零创建”，不同点是 UE 的数据还绑定 renderer scene、card/page allocator、view/GPU-specific 选择和 pending scene operations，不是几张纹理就能替代。
- `FViewState` 可以帮助联想到 Unity 中按 camera 保存的 temporal history 或 per-camera screen sampling 状态。相似点是两个 camera 不能无条件共享重投影历史；不同点是 UE view state 由 renderer 生命周期管理，并同时承载多类 Lumen history，不是相机脚本上的任意字典。
- HWRT、Mesh SDF、screen trace 等每帧路径选择，可以联想到 SRP 中根据平台能力、camera 配置和 quality setting 选择 RT 或 compute backend。相似点是“消费者 contract 不变、实现后端可切换”；不同点是 UE 决策还受 show flags、当前 view、项目 tracing data 和各 Lumen feature 条件共同约束，不是一个永久 backend toggle。

最危险的迁移方式，是把这些状态都塞进某个 `MonoBehaviour` 缓存，并认为它等价于 UE ownership。`MonoBehaviour` 属于游戏对象/组件心智，而这里的资源要跨 Game/Render 边界、服务多个 view 或 GPU、参与 RDG import/extraction，并在 renderer scene 更新时保持一致。脚本可以修改影响渲染的配置，却不能替代 `FLumenSceneData`、`FViewState` 和 RDG 各自承担的所有权合约。

### 2.5 CPU readback：跨越 GPU producer 与 CPU consumer 的异步邮箱

Surface Cache 缺页 feedback 由具备反馈写权限的 GPU 路径产生，但 page allocation 和 capture list 的一部分决策在 CPU setup 中推进。UE5.7 中并不是所有 Lumen tracing 都有这项写权限：Screen Probe tracing 明确关闭 CPU page feedback；启用 Surface Cache feedback 的 Lumen Reflections，或正在运行且显式开启反馈的 Lumen visualization，才会为这条 readback 链生产数据。Readback ring 因而不是普通 history texture，而是一组有“本次是否接受 submission、GPU copy 是否 ready、CPU 是否已经消费”状态的异步邮箱。

它不保证下一帧一定可读，也不保证每帧都能提交新副本。这个生命周期将在第 7 节单独展开。

### 2.6 O/D/C/L 总表

| 系统 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| Lumen Scene / Surface Cache | 当前 view 选中的 `FLumenSceneData` | card/page/page table、材质与 lighting atlas、scene pending state | scene updates、Surface Cache requests、capture/lighting budgets | 跨帧，可能 default 或 view/GPU-specific |
| 当前帧 Lumen 资源 | `FLumenSceneFrameTemporaries` / RDG | 本帧注册后的 texture/buffer/SRV/UAV、共享输出 | RDG pass 依赖与 renderer 调度 | 当前 graph |
| Screen Probe histories | `FViewState` | probe history、full-resolution indirect history | final gather、history validity tests | per-view 跨帧 |
| Radiance Cache | `FViewState` 中的 `FRadianceCacheState` | clipmaps、indirection、probe atlases、allocator state | mark、reuse、allocation、trace budget | per-view 条件式跨帧 |
| Reflections | 当前帧输出 + `FReflectionTemporalState` | reflection rays/result、specular+second moment、frame count | reflection method、trace/resolve/denoise | 当前帧输出 + per-view history |
| Surface Cache feedback | `FLumenSceneData` 中的反馈对象 | raw feedback allocator/buffer、accepted submission 的 hash/compaction 输出、readback ring；ready 后才形成 CPU requests | reflection/visualization sampling writer、ring capacity guard、hash/compaction、GPU readiness、CPU consumption | 非固定延迟的跨帧 producer-consumer |

## 3. Surface Cache：把任意表面改写成可分页查询的二维数据

### 3.1 Card 为什么存在

红墙在真实场景中是一个三维 mesh。要让任意 ray 命中后都重新执行完整材质和 lighting，成本会随 ray 数量增长。Surface Cache 选择另一种方式：从有限方向观察表面，把可见部分投影到二维 card 上，再把 card 放入统一 atlas。

`FLumenCard` 可以理解为“从一个指定方向看到的一块表面参数域”。它包含 card 的局部/世界包围信息、方向、可见性、dilation 模式、期望与实际分辨率层级，以及各层 page allocation。

为什么是 card，而不是直接使用 mesh 原有 UV？

- 原有 UV 的语义属于材质贴图，不保证为动态 lighting cache 提供合适的唯一展开、密度或方向覆盖。
- Card 可以由 renderer 统一生成和管理，不要求作者为每个动态对象准备 lightmap UV。
- Card 将复杂 mesh 转成规则二维采样域，天然适合 page table、atlas、滤波和 GPU 采样。

代价也很明确：有限方向投影不能完美代表所有表面。薄片、复杂凹面、遮挡内部或与真实光追几何不一致的区域，可能出现 coverage 不足、投影误差或漏光。Surface Cache visualization 中的粉色/黄色覆盖问题，本质就是“这块世界表面没有得到足够好的 card representation”。

可能的替代方案包括：

- **静态 lightmap UV**：静态场景可得到稳定高质量预计算，但不适合动态材质、动态几何和全动态 lighting。
- **世界体素**：不依赖表面 UV，适合低频和体积查询，但高精度边界需要大量内存。
- **完整 hit lighting**：命中处质量更直接，但材质、灯光和阴影成本随 ray 数上升。

Lumen 选择 card，不是因为其他方案不能实现，而是因为 card 在动态场景、统一 GPU 查询和有限实时预算之间提供了可管理的表面缓存域。

### 3.2 为什么 Card 还要切成 Page

如果每张 card 都永久分配最高分辨率矩形，远处红墙和看不见的表面也会占据大量 atlas。Surface Cache 因而把 card 的虚拟分辨率继续切成 page，只让当前需要的层级驻留。

UE5.7 的物理 page 是 128×128 texel，但一个常规虚拟 page 的有效尺寸是 127×127。少掉的部分来自 page 周围的半 texel border，用于稳定双线性采样和边界处理。只记“128 page”而不理解有效虚拟区域，会无法解释 dilation、border 和相邻页滤波为什么存在。

分辨率层级从 8 texel 到 2048 texel。这里的 level 不是材质纹理 mip，而是“这张 card 在 Surface Cache 中希望以多细的空间密度表达”。距离、需求、预算和 atlas 空间共同决定期望层级是否真的能驻留。

### 3.3 Page Table 为什么不能省

Page Table 把“card 的虚拟 page”翻译成“物理 atlas 中当前可采样的矩形”。`FLumenPageTableEntry` 同时携带：

- 当前物理 page 或 sub-allocation 的位置；
- 该 page 覆盖的 card UV；
- 所属 card 与分辨率层级；
- 高分辨率页未驻留时可使用的粗层级采样信息。

这个间接层解决了三个冲突：

1. shader 希望用稳定虚拟坐标查询，不应关心物理 atlas 每帧怎样搬迁；
2. allocator 希望自由复用、驱逐和重分配物理空间；
3. 高分辨率页尚未准备好时，系统希望继续使用已有效的较粗 page，而不是立即暴露空洞。

要注意适用条件：只有 card 已建立有效分配层级时，高分辨率缺页才能沿 hierarchy 退回粗 page。Card 尚未覆盖、最低层也未有效 capture，或数据被清空时，不能宣称“永远有内容可读”。Fallback 降低了升级过程中的空洞风险，但不是凭空创造有效 representation。

如果没有 page table，shader 必须直接持有物理 atlas 坐标。每次驱逐、扩容或重排都会迫使大量消费者更新地址，物理存储与逻辑身份被绑定，流送和调试都会更困难。

### 3.4 Sub-allocation 为什么值得增加复杂度

小 card 可能只需要 8×8、8×16 或 32×32 一类区域。若每个小区域都独占 128×128 物理 page，内部碎片会迅速消耗 atlas。Surface Cache allocator 因此按尺寸建立 bin，把多个小矩形放进同一物理 page。

这是一种典型的内存换控制复杂度：

- 好处是提高 atlas 利用率，让更多小 card 保持驻留；
- 代价是 allocator、page table 和 copy/compression 路径必须理解 sub-allocation；
- 对卡片数量少、尺寸统一且内存宽裕的专用系统，固定整页会更简单；
- 对通用 UE 场景，大量不同大小 card 让 sub-allocation 更值得。

### 3.5 Atlas 为什么分成“表面是什么”和“表面提供多少光”

Surface Cache 最终暴露两类数据。

**材质/几何层**回答“红墙是什么”：albedo、opacity、normal、emissive、depth。它们主要由 Card Capture 更新。材质改变、mesh 变化、card 失效或 page 重新分配，会影响这些层。

**lighting 层**回答“红墙此刻向外提供多少 radiance”：direct lighting、indirect/radiosity、final lighting，以及配套累积或降噪状态。灯光、阴影、全局光照变化和 lighting update budget 会影响这些层。

为什么不把它们合成一张最终纹理后就不再区分？因为失效条件不同。红墙材质从红改蓝时，材质 representation 先失效；太阳方向改变时，几何与 albedo 仍可用，但 lighting 失效。拆开以后，系统可以只重做真正过期的阶段。

不拆开的坏处是：任何材质或灯光变化都迫使整个 cache 重新 capture 和重算；或者系统错误复用旧 lighting，让“蓝墙仍把地面染红”。

### 3.6 Worked case：相机走近红墙时，一张 page 如何升级

相机离红墙很远时，某张 card 只有较粗的 locked/allocated 层级。Page table 指向一块有效的粗分辨率区域，地面 probe 可以得到不精细但连续的红墙 lighting。

相机走近后，基于相机距离和 scene 维护产生的请求可以提高 card 的驻留层级；另外，只有启用了 Surface Cache feedback 且实际运行的 Lumen Reflections，或显式启用反馈且实际运行的 Lumen visualization，才能把实际采样中发现的高分辨率需求经 readback 转换成 CPU request。纯 Screen Probe tracing 的角色止于读取当前有效 page，并在高层 page 不可用时接受合法的粗层 fallback；它既不写 CPU page feedback，也不写 page last-used buffers。CPU request 处理随后尝试为高层 page 分配物理空间；若 atlas 压力大，可能驱逐长时间未使用的高层 page，或保持更粗分配。成功后，新 page 先进入本帧 capture 列表，并在临时 capture atlas 获得区域。

新 page 尚未 capture、lighting 尚未恢复时，旧粗 page 仍是可用 fallback。等材质 capture、旧 lighting 重采样和新 page table 数据都准备好后，GPU 采样才转向更细区域。

这里真正变化的不是“红墙突然有了另一张纹理”，而是：

```text
虚拟 page 的期望分辨率提高
  → allocator 给出新的物理位置
  → capture/lighting 让该位置变得有效
  → page table 发布新的可读映射
```

调试时分别检查：card 是否存在、最低层是否有效、目标高层是否申请、物理空间是否分配、capture 是否完成、lighting 是否有效、page table 是否发布。只看到“高分辨率页没出现”还不足以判断是哪一步失败。

## 4. 持久 Surface Cache 如何进入一帧 RDG

### 4.1 跨帧 owner 与本帧 owner 为什么必须分开

Surface Cache 要跨帧保存，而 RDG 每帧重新建立资源依赖图。UE 用 import/extraction 边界连接两种生命周期：

- `FLumenSceneData` 持有 pooled resources 和跨帧逻辑状态；
- `FLumenSceneFrameTemporaries` 保存本帧注册后的 RDG 资源；
- RDG 负责本帧 pass 顺序、barrier、async compute 和资源别名；
- graph 结束前，需要继续存在的结果被提取回持久 owner。

这是生命周期安全要求，而不仅是代码风格。如果持久资源不注册进 RDG，graph 无法可靠知道它们何时读写；如果把 RDG 临时 handle 跨帧保存，下一帧可能引用已经结束生命周期或被复用的资源。

如果使用过 Unity RenderGraph，可以把这个动作有限地联想到 `RenderGraph.ImportTexture`：一张由 graph 外部长期持有的纹理，在本帧被导入后，RenderGraph 才能跟踪本帧 pass 对它的读写。这一类比只解释“外部持久资源如何进入本帧依赖图”。

它不表示两边生命周期完全等价。Unity 中一个 persistent `RenderTexture` 可能由 feature、camera 或用户代码持有；Lumen 的持久 atlas 由当前 view 选中的 `FLumenSceneData` 维护，并与 card/page identity、scene updates、feedback 和多 GPU 状态绑定。`FLumenSceneFrameTemporaries` 也不是另一份永久缓存，而是本帧 RDG 对这些资源的发布面。Import 让 graph 获得本帧依赖知识，不会把跨帧 ownership 自动转给 graph；帧末仍要把需要继续存在的结果提取回正确 owner。

### 4.2 一帧中的状态交接

把源码名降到最低后，状态流可以描述为：

```text
CPU 准备阶段
  选择当前 view/GPU 对应的 Lumen Scene Data
  消费 ring 已接受、已完成 hash/compaction、且 GPU readback copy 已 ready 的反馈，以及其他 scene readback
  处理 primitive/card/page 的待办与预算
        ↓
当前 RDG 图
  把持久 buffer/atlas 注册为本帧资源
  在旧映射仍有效时重采样需要搬迁的 lighting
  上传新的 page/card 数据
  执行 capture、Surface Cache lighting、final gather 和 reflections
        ↓
帧末发布
  把需要跨帧的 atlas、buffer 和 histories 提取回各自 owner
```

这里存在一个容易误解的顺序：新 page 的 CPU 分配决定可以先形成，但在 GPU 端 page table 真正切换前，需要先保存或重采样旧位置上仍有价值的 lighting。否则地址先变，旧历史就失去可定位来源。

### 4.3 Worked case：同一张 atlas 为什么看起来换了 owner

第 N 帧开始时，红墙 atlas 以 pooled resource 形式由当前 `FLumenSceneData` 持有。注册进入 RDG 后，本帧 pass 通过 RDG handle 访问它；这并不代表跨帧所有权已经交给 RDG。

本帧可能重分配 page、capture 新材质、更新 direct/radiosity lighting。graph 中这些写入受 RDG 管理。帧末，更新后的资源被提取回持久 `FLumenSceneData`，供某个后续帧再次 import。

调试资源丢失时，必须区分：

- 持久资源本来就不存在或被释放；
- 持久资源存在，但没有注册进本帧 graph；
- 本帧产生了新结果，却没有提取到正确的持久 owner；
- 多 view/GPU 情况下，查错了另一份 Lumen Scene Data。

## 5. Card Capture：从 card 方向重新观察材质

### 5.1 为什么不能直接复制主视图 GBuffer

主视图 GBuffer 只包含当前相机看见的表面。红墙转出屏幕后，主视图不再拥有红墙像素，但地面 ray 仍可能命中红墙并需要其 albedo、normal 和 emissive。

Card Capture 因此使用独立于主相机的渲染上下文，从 card 的方向重新渲染目标表面到临时 capture atlas。它的目标不是生成最终画面，而是建立 Surface Cache material representation。

如果直接复制主视图 GBuffer，系统会退化成屏幕空间缓存：离屏表面、遮挡后表面和从其他方向可见的表面都无法建立稳定 cache。

### 5.2 谁决定本帧 capture 哪些 page

Capture 受 page 数量、texel 数量、临时 capture atlas 空间和持久 atlas 空间限制。进入本帧列表的需求大致包括：

- 新增或变化的 primitive 需要建立/更新 mesh cards；
- 距离请求，或由 reflection/visualization feedback 的 ready readback 形成的 CPU request，需要更细 page；
- 材质或表面变化使已驻留 page 失效；
- 长时间未刷新、首次在某 GPU 使用或需要周期 refresh 的 page；
- 分辨率重分配后需要重新 capture 的 page。

“请求存在”不等于“本帧一定 capture”。请求还要通过物理空间、临时 capture atlas 和每帧预算。预算不足时，旧的粗 page 或旧 capture 可能暂时继续服务查询。

为什么不每帧 capture 所有 card？因为 capture 本质上仍是一次渲染：要准备 view、culling、draw commands、raster/material 输出和 atlas copy。全量重画会让成本随整个 Lumen Scene，而不是随当前变化和需求增长。

### 5.3 为什么先重采样旧 lighting

Page 分辨率变化会改变物理 atlas 坐标。旧 lighting 不能按原地址继续使用，但它仍然是新 page 的有价值初始估计。

UE 在发布新 page table 前，把旧 page 的 direct/indirect lighting 重采样到临时 capture 对应区域。新 page 上线时先带着旧 lighting 的空间重映射结果，再由后续预算逐渐刷新。

如果直接丢弃历史，新 page 会从黑或未收敛状态开始，走近红墙时容易看到明显闪烁和冷启动噪声。如果无条件保留旧地址，则 page table 已经变化，shader 会读到错误区域。

### 5.4 Classic mesh 与 Nanite 为什么是两条 capture 分支

**Classic/non-Nanite capture**使用共享 capture view，准备 GPUScene instance culling 或 primitive ID 输入，并在该 capture view 上使用 `ForceLowestLOD`。这是一种明确的成本选择：Surface Cache 需要稳定、便宜的材质代理，不追求主视图每个像素的最高几何 LOD。代价是低 LOD 几何与主视图或 ray tracing 几何不完全一致时，可能出现投影误差。

**Nanite capture**不能概括为“同样用最低 LOD 重画”。它进入 Nanite 的 `LumenCardCapture` mesh pass，为 card page 建立独立 packed views，并通过 Nanite 的 LOD scale、culling、raster 与 shading command 路径写 capture atlas。Nanite 仍然服务同一份 Surface Cache contract，但几何选择和 raster 机制由 Nanite 自己推进。

为什么不强行统一成一条 draw path？Classic mesh 和 Nanite 的几何组织、LOD 选择、可见性与材质执行模型不同。强行统一可能减少接口数量，却会失去 Nanite 的 cluster/raster 优势，或迫使 classic mesh 模拟不属于它的数据结构。

本篇边界只要求记住：两条分支最终都让指定 card page 的 capture 数据成立；Nanite 内部细节归第 16 篇。

### 5.5 Capture atlas、持久 atlas 与 opacity 边界

临时 Card Capture 的主要 raster 输出包括 albedo、normal、emissive 和 depth/stencil。随后 copy/update 阶段把 capture 数据转换并写入持久 Surface Cache 的各个 material layer；持久查询面最终包含 albedo、opacity、normal、emissive 和 depth。

因此，不应把“持久 Surface Cache 有 opacity atlas”简化成“capture pass 直接绑定并写了一张 opacity render target”。Capture attachment 与最终持久 layer 是两个不同边界，中间还有 copy、压缩或格式转换。

### 5.6 为什么需要 dilation 与多种 copy 路径

Card page 会被双线性过滤，也可能在 atlas 中紧邻其他 page。若边界外没有合理扩展，采样 footprint 会混入空白或邻页数据，表现为红墙边缘漏出错误颜色。Dilation 把有效边缘向外扩展一个 texel，给过滤提供保护带。

写回持久 atlas 时，UE 会根据平台格式能力和压缩配置选择直接压缩写入、经临时压缩 atlas 再 copy，或未压缩 copy。不同路径服务同一数据 contract，但在带宽、格式支持和临时内存之间取舍。

### 5.7 Card Capture 的 last-valid-state

红墙 capture 异常时，按下面顺序判断：

```text
红墙是否进入正确 Lumen Scene Data
  → 是否生成有效 mesh cards / cards
  → 目标 page 是否已分配并进入 capture list
  → classic 或 Nanite 分支是否真的产生 capture 数据
  → dilation/copy 是否完成
  → 持久 material atlas 对应区域是否有效
```

若 card visualization 已经没有覆盖，继续调整 lighting 或 Screen Probe 没有意义；若 capture 正确而 GI 颜色仍旧，再进入下一节检查 lighting atlas。

## 6. Surface Cache Lighting：把“红墙是红的”变成“红墙提供多少红光”

### 6.1 为什么材质 capture 之后还需要 lighting cache

Albedo 只说明红墙反射光时偏红，并不说明它此刻接收到多少太阳光、被什么阴影遮挡、间接反弹累积到什么程度。Ray 命中红墙后真正需要的是出射 radiance 的近似。

Surface Cache lighting 把灯光和 radiosity 结果写到与 card/page 对齐的 lighting atlas。默认 Surface Cache hit lighting 模式下，大量 GI 和 reflection ray 可以在命中后直接查询这个结果，而不是每条 ray 都重新执行完整材质、灯光列表和 shadow ray。

没有 lighting cache 时有两种选择：

- 命中只读 albedo，无法得到受光后的 radiance，结果物理意义不足；
- 每个命中点现场做完整 lighting，质量可以更直接，但 GPU 成本随命中数放大。

Surface Cache lighting 选择的是“允许有限分辨率和更新延迟，换取大规模查询可负担”。

### 6.2 Direct、Radiosity 与 Final 为什么分开

Lighting 大致经历三种数据状态：

```text
material layers
  → direct lighting：当前灯光和阴影对 card texel 的直接贡献
  → radiosity / indirect lighting：表面之间的间接传播与累积
  → final lighting：组合成 tracing 最常读取的表面 radiance
```

Direct 与 radiosity 分开，不只是为了代码整洁。它们的变化速度和成本不同：灯光或阴影变化可能要求 direct 快速更新；radiosity 需要跨表面传播和时间累积，通常更适合预算化更新。Final atlas 则为消费者提供稳定、统一的采样面。

若所有层都绑定成一次全量更新，任何小变化都会迫使昂贵的 indirect 重新计算；若 final 直接引用未组合的中间状态，消费者就必须理解更多分支和历史有效性。

### 6.3 `BuildCardUpdateContext` 解决的是“本帧把预算花在哪里”

Surface Cache 可能包含大量 card pages，不可能每帧刷新全部 texel。更新上下文会根据 view、frustum 距离、surface cache frame index、history 是否有效和配置的 update speed，分别计算 direct 与 indirect 能处理多少 tile。

它先形成优先级分布，再在预算内选择 page/tile 子集。Direct 与 radiosity 使用不同 update factor，因此可以拥有不同刷新速度。

启用反馈且实际运行的 Lumen Reflections，或显式启用反馈且实际运行的 Lumen visualization，在执行允许反馈写入的 Surface Cache 采样时，feedback-enabled shader permutation 与非零 feedback resources 才允许它们更新 page last-used buffers。后续还要满足硬件 RT scene-lighting feedback 的消费条件，这些跨帧 buffer 才参与 lighting page 优先级。Writer 条件与 consumer 条件是两道门；不能把它们压成“任何 tracing 或任何 feedback 开启后都会同样驱动 lighting 更新”。软件路径和其他条件下仍主要依赖 view/frustum/frame 等输入。

这里的 scene-lighting last-used prioritization 仍是 GPU→GPU 证据，不是第 7 节面向 CPU 的 compacted readback。Raw feedback 写入尝试与 last-used frame index 可以由同一个 sampling writer 产生，但 raw buffer capacity 可以拒绝某个 element，而位于该内层容量检查之外的 last-used 仍可更新。Compacted unique/count output 则由 ring 接受 submission 后的 hash/compaction pass，基于成功落入 raw buffer 的子集另行生成。Last-used 调整已有 page 的 lighting 更新顺序；compacted page/resolution 需求才可能经 readback 形成新 CPU request。

### 6.4 预算带来的可见代价

红墙刚进入重要区域时，material page 可能已经 capture，但 direct 或 radiosity 仍在排队。于是 Surface Cache 中“红墙是什么”已经正确，“红墙提供多少光”却仍旧或不完整。

增加 lighting update speed 可以更快响应，但会占用更多 GPU 时间；降低预算会提高稳定帧率，却增加 lighting 变化传播延迟。强制全量更新适合验证“问题是否由预算/历史造成”，不适合作为大场景常规解决方案。

### 6.5 Hit lighting 是替代方案，不是免费升级

硬件 RT lighting mode 提供三种重要取舍：

- mode 0：GI 和 reflections 的 ray hit lighting 使用 Surface Cache，性能最好，但质量受 card coverage、分辨率和更新状态限制；
- mode 1：GI 与 reflections 在命中点计算 lighting，质量可提高，但每个命中要执行更完整的材质与 lighting；Surface Cache 仍用于 secondary bounce；
- mode 2：主要让 reflections 使用 hit lighting，GI 和 secondary bounce 仍依赖 Surface Cache，包括 reflection 中看到的 GI。

因此，“开启 HWRT”只说明几何命中可以使用硬件路径，不代表自动选择完整 hit lighting，也不代表 Surface Cache 被绕过。

对镜面质量优先、实例规模和 GPU 预算可控的项目，reflection hit lighting 可能更合适；对大量 diffuse/rough 查询和大场景，Surface Cache 往往是更可持续的成本选择。

## 7. Feedback：两个不同闭环，不是固定下一帧回信

### 7.1 为什么仅靠相机距离不够

如果只按相机距离决定 Surface Cache 分辨率，会出现两种浪费：

- 某个近处表面虽然靠近相机，却没有被启用反馈的 reflection 或 visualization 查询，仍可能消耗高分辨率 page；
- 某个屏幕外表面虽然不符合简单距离预测，却频繁被启用反馈的 reflection 查询，纯距离策略可能低估它的重要性。

Feedback 允许**具有写权限的消费者**把实际需求反向传回缓存维护系统。但写权限不是“所有 tracing 自动拥有”：UE5.7 的 Screen Probe tracing 不进入 feedback-enabled 写入路径，因此既不写 CPU page-feedback buffer，也不更新 page last-used buffers。这里还存在两条数据与生命周期不同的闭环，必须分开。

### 7.2 缺页 feedback：GPU→CPU 的异步 producer-consumer

UE5.7 中，可以进入 Surface Cache feedback-enabled shader 写入路径的 GPU producer 被限定为两类：

- 当前 view 实际运行 Lumen Reflections，并且其 Surface Cache feedback 开关实际启用；
- Lumen visualization，并且当前确实在运行 Lumen visualization，同时显式启用 `r.Lumen.Visualize.SurfaceCacheFeedback`。

只有共享 Surface Cache feedback 机制启用、Surface Cache 没有被冻结、上述至少一种 producer 条件成立时，系统才会为本帧分配非零 feedback resources。当前实现要求 sampling shader 使用 feedback-enabled permutation，并且 feedback resource size 非零。在会产生 raw feedback 的 sampling 分支中，外层抽样条件成立后，shader 先递增 raw allocator 并取得 `WriteOffset`；只有 `WriteOffset < BufferSize` 时，包含 card index、desired res level 与 local page 的 packed element 才真正落入 raw feedback buffer。若 raw buffer 已满，allocator 仍记录这次尝试，但该 raw element 被丢弃。对应 page 的 last-used 写入位于这层容量检查之外，因此 raw element 丢弃时 last-used 仍可更新。Raw payload 本身不包含重复 count。

这不表示 raw feedback 与 last-used 是同一份数据。Raw feedback 等待本次 submission 的 ring capacity 检查；last-used buffers 保持在 GPU 资源生命周期中，供后续符合条件的 lighting priority 消费。Screen Probe tracing 使用 feedback-disabled 路径：它读取有效 Surface Cache、在高层缺失时接受合法粗 fallback，但不会写 raw feedback 或 last-used，也不会产生由它发起的 compacted feedback。

Sampling 阶段结束后，submission 先检查 readback ring capacity。若 ring 已满，本次 submission 立即停止；用于建立 hash、生成 compacted output 和 enqueue readback copy 的 pass 都不会为本次 submission 建立。只有 ring 接受本次 submission，后续 GPU pass 才从 raw feedback 建立 hash；其工作量按 `min(allocator, BufferSize)` 截断，因此只消费成功落入 raw buffer 的 elements，不会读取 allocator 超容量部分。相同 packed element 被归到同一 key，并在这里统计重复 count；compaction 再输出唯一元素数组，并把该 count 编入 compacted element。之后系统才 enqueue GPU→CPU readback copy。重复 count 完全由 hash/compaction 阶段从成功写入的重复 raw elements 统计产生，不属于 sampling producer 的原始 payload。

CPU 不能假定第 N 帧提交的数据在第 N+1 帧必定 ready。后续帧开始时，系统只取 ring 中**已经 ready 的最新结果**：

- GPU 很快完成时，N+1 可以读到，这是一个常见示例；
- GPU 尚未完成时，可能 N+2 或更晚才读到；
- 没有任何 pending buffer ready 时，本帧没有新的 CPU feedback 可消费；
- readback ring 已满时，本次新的 submission 在 hash/compaction/readback pass 建立前就会停止，因为不能覆盖仍在 pending copy 的 buffer；已经 ready 的旧 submission 和此前已经形成的 CPU request 不会因此被撤销。

这条链必须拆成以下可观察状态：

```text
feedback-enabled reflection / visualization sampling shader
  → 外层抽样条件成立后，raw allocator 先递增并得到 WriteOffset
  → WriteOffset < BufferSize：raw element 落盘；否则该 element 丢弃
  → 对应 page last-used 在这层容量检查之外更新
  → readback ring capacity 接受本次 submission（ring 满时在这里停止）
  → accepted submission 只对成功落盘的 raw elements 建立 hash并统计重复 count
  → compaction 输出 unique elements 与对应 count
  → enqueue readback copy；某个后续帧 copy 才 ready
  → CPU 消费 ready compacted data
  → CPU 形成 Surface Cache request，再接受 capture/atlas/lighting budget
```

这里有两个不能混淆的容量边界。Raw buffer 满时，allocator 可以超过 `BufferSize`，超出的 raw elements 被丢弃，但对应 last-used 仍可更新；compacted unique/count 只代表成功落盘的 raw 子集。Readback ring 满则发生得更晚：本次 raw buffer 中即使已有成功元素，也不会为这次 submission 建立 hash/compaction output或 enqueue readback copy，更没有形成 CPU request。被跳过的是新的 submission，不是某个已经存在的 request。CPU 得到 ready compacted data 后形成的 requests，才会继续经过 capture 数量、atlas 空间、重复 count 转换出的优先级信息和当前 scene state。**Readback ready 也不等于 page 已经 capture。**

### 7.3 Last-used buffer：GPU→GPU 的跨帧使用证据

Card page last-used 与 high-res last-used buffers 按 feedback 的抽样条件记录**有反馈写权限的 shader 查询**最近使用过哪些 page，而不是完整记录所有 Surface Cache 查询。当前实现中，只有启用了反馈且实际运行的 Lumen Reflections，或显式启用反馈且实际运行的 Lumen visualization，才会同时满足 feedback-enabled permutation 与非零 feedback resources 这组写入条件；纯 Screen Probe 不产生这类记录。

帧末 buffer extraction 只把已经存在的 last-used buffer 延续回持久 scene data，后续帧再 import 给符合条件的 GPU lighting update priority 使用。Extraction 是生命周期交接，不是 producer：如果本帧没有合资格 shader 写入，它不会凭空生成新的 last-used frame index。这条路径不需要 CPU 逐项解释内容，是 GPU 资源的跨帧延续，不是缺页 feedback 的 GPU→CPU readback。

把两者都叫“读回”会掩盖关键差异：

| 闭环 | Producer | Consumer | 同步边界 | 用途 |
| --- | --- | --- | --- | --- |
| 缺页 Surface Cache feedback | Sampling shader 递增 allocator并尝试写 raw；accepted submission 的 hash/compaction shader 只从成功落盘子集生成 unique elements 与重复 count | CPU request setup | allocator → raw capacity 接受/丢弃 element → ring capacity acceptance → hash/compaction output → enqueue copy → 非固定帧 ready → CPU 消费 | 分配/补充 page 与分辨率需求 |
| Page last-used buffers | Feedback-enabled reflection/visualization sampling shader；要求非零 feedback resources | 符合 scene-lighting 条件的后续帧 GPU lighting priority | RDG extraction/import 延续既有 buffer，不经过 CPU compacted readback | 决定已有 page 的 lighting 更新优先级 |

### 7.4 为什么不强制同帧补页

第 N 帧中，具备反馈权限的 reflection/visualization sampling shader 尝试写 raw feedback并独立更新 last-used 时，本帧 capture list 和大部分 RDG 结构已经建立。只有 raw capacity 接受的 elements 才能在 ring 接受 submission 后进入 hash/compaction 并 enqueue copy。即便如此，若 CPU 仍被要求立即读取 GPU 结果、修改 page allocation，再向同一帧插入 capture 和 lighting：

1. CPU 要等待 GPU 到达 feedback copy 完成点；
2. GPU 也可能等待 CPU 重建后续工作；
3. 原本可并行的 frame pipeline 被硬同步切断；
4. 帧时延和抖动会随 feedback 数量、GPU 负载和场景变化放大。

跨帧 feedback 是用可见收敛延迟换取流水并行和可控预算。

### 7.5 还有其他设计吗

下面是架构层面的替代方案，不是对 UE 当前实现的源码描述：

- **预测式预取**：根据相机速度、反射体和历史方向提前申请 page。可以降低转头后的缺页延迟，但会提高误预测和内存浪费。
- **更大常驻粗层/更高 capture budget**：减少明显冷启动，但增加 atlas 与每帧成本。
- **完全 GPU-driven page allocation/capture scheduling**：可能减少 CPU readback，但需要把 allocator、scene mutation、draw preparation 和资源安全更多迁入 GPU，控制与调试复杂度显著增加。
- **同帧同步读取**：适合离线工具、截图或不关心帧时延的特殊流程，不适合作为高并发实时默认。

### 7.6 Worked case：相机猛转头后，红墙为什么不是固定一帧到位

第 N 帧相机把红墙转出屏幕，新出现的地面 probe 需要查询屏幕外红墙。Screen trace miss 后，Screen Probe 的世界 trace 命中红墙，并读取当前已有效的 Surface Cache。若高分辨率 page 不存在，它接受合法的粗 page fallback；这条纯 Screen Probe 查询不写 CPU page feedback，也不写 page last-used buffers。

同一场景中的镜子随后实际运行 Lumen Reflections。若其 Surface Cache feedback 已启用，镜面 reflection ray 命中红墙并发现目标 page 缺失或过粗时，sampling shader 会递增 raw allocator、尝试写入包含 card index、desired res level 与 local page 的 raw element，并在内层 raw capacity 检查之外更新所采样 page 的 last-used frame index。只有 `WriteOffset < BufferSize` 时该 raw element 才落盘，才可能在 submission 被 ring 接受后进入 hash/compaction；它不直接写 compacted feedback。下面的跨帧时间线描述的是这条 reflection feedback，而不是地面 Screen Probe。

之后存在多种合法时间线：

```text
快速情况：N 帧 sampling 尝试 raw 并更新 last-used → raw capacity 接受 element → ring 接受 submission → hash/compact → enqueue copy → N+1 ready → N+1 CPU 生成 request → 预算允许 capture
延迟情况：N 帧 sampling 尝试 raw 并更新 last-used → raw capacity 接受 element → ring 接受 submission → hash/compact → enqueue copy → N+1 未 ready → N+2 ready → N+2/N+3 才获得 capture/lighting 预算
raw 压力：allocator 递增后 WriteOffset >= BufferSize → 本 element 丢弃，last-used 仍可更新，后续 compacted output 不包含它
submission 压力：readback ring 满 → 在 hash/compaction/readback pass 建立前停止，本批 raw data 不会形成 CPU request
调度压力：CPU request 已形成，但 atlas/capture/lighting budget 紧张 → request 被推迟，当前查询继续使用粗 fallback
```

所以“一两帧逐步到位”可以是设计行为，但不能把 N→N+1 写成保证。调试 reflection/visualization feedback writer 时，要按证据深度逐层确认：sampling writer 的 raw allocator 是否增长，并把 allocator 与 `BufferSize` 比较；若 allocator 超容量，目标 raw element 缺失并不表示 last-used writer 未运行，应单独检查对应 last-used。随后确认 ring 是否接受本次 submission；accepted submission 是否仅从成功落盘的 raw 子集产生 hash 与 compacted unique/count output；readback copy 是否 enqueue、何时 ready；CPU 是否消费并形成 request；既有 request 是否获得 page/capture/lighting 预算。Last-used 的帧末 extraction 是另一条 GPU 生命周期证据。地面纯 Screen Probe 侧只检查 world hit、当前 Surface Cache 是否有效、粗 fallback 是否可用以及 final lighting 是否有效，不应等待它产生 raw、compacted、CPU readback 或 last-used 写入。

## 8. Screen Probe：用共享采样把每像素问题降成有限查询

### 8.1 为什么不让每个像素独立发完整半球射线

白色地面上每个像素都需要半球方向上的入射 radiance 积分。如果每个像素独立发射足够多的 ray，成本会随分辨率和采样数直接增长；相邻像素又会重复查询非常相似的世界区域。

Screen Probe Gather 先在当前 view 上放置较稀疏的 probe，让 probe 承担多方向 tracing，再把过滤后的结果插值回像素。它用空间共享降低 ray 数，以 probe 分辨率、插值误差和时间历史换取成本。

### 8.2 Final Gather 不只有 Screen Probe 一条实验路径

UE5.7 的 final gather 入口可以选择：

- **Screen Probe Gather**：默认主路径，在屏幕均匀网格、自适应补点、分层 tracing、过滤与像素积分之间取得平衡；
- **Irradiance Field Gather**：实验性 opaque final gather，从预计算/缓存 probe irradiance 插值，目标是更便宜但质量更低；当 GPU 成本比细节质量更重要、且内容能接受更平滑结果时可能有价值；
- **ReSTIR Gather**：prototype 路径，依赖硬件 RT 和受支持的 SM6 平台。UE5.7 中默认关闭，源码说明其当前质量更低且支持功能更少，更适合算法实验和特定验证，而不是默认替代 Screen Probe。

这三者共享“为当前 view 生成 indirect signals”的上层 contract，但内部数据、history 和质量成本不同。选择另一条 gather path 时，不能继续用 Screen Probe 的 probe/history 结构解释它。

### 8.3 为什么先均匀，再有限自适应

默认 Screen Probe 以当前 view rect 和 downsample factor 建立均匀网格。常见默认值包括 16 倍屏幕降采样、8×8 的八面体 tracing 分辨率，并允许每个均匀 probe 周围产生有限数量的 adaptive candidates。

均匀网格的优点是：

- probe 地址规则，容易建立 atlas 和 indirect dispatch；
- 相邻帧布局相对稳定，history 更容易重投影；
- 成本可由 view size 和 downsample factor 直接估算；
- 相邻像素可以共享同一组 probe。

但纯均匀网格会在深度/法线不连续处失败。例如红墙与地面的交界落在一个 16×16 tile 内，单个 probe 可能代表错误表面，插值时就会漏光。

Adaptive placement 先根据下采样 depth、normal、world position 等证据标出不连续区域，再在预算内补充 probe。它不是完全自由的自适应采样，而是“规则基底 + 有限修补”。

为什么不完全 adaptive？因为完全根据当前帧细节动态生成 probe，数量、地址和邻接关系会剧烈变化，allocator、indirect dispatch 和 temporal history 都更难稳定。为什么不取消 adaptive？因为薄几何、边缘和小物体会持续被低密度网格跨越。

### 8.4 Probe 的 O/D/C/L

- **Owner**：当前 view 的 frame data；跨帧 probe/history 由 `FViewState` 持有。
- **Data**：probe 屏幕位置、深度、法线、世界位置、ray directions、trace radiance/hit、filtered irradiance。
- **Control**：view rect、downsample factor、uniform placement、adaptive mark/spawn、trace/filter/integration 配置。
- **Lifetime**：placement 和 ray buffers 主要属于当前帧；可重用的 probe radiance/depth/world-position history 跨帧存在。

### 8.5 Unity/HDRP 有限迁移桥：Screen Probe 不是离线 Probe Volume

Screen Probe 可以帮助 Unity 读者联想到两个熟悉方向，但都只能类比一部分：

- 它像 screen-space GI，因为 placement 由当前 view rect、depth、normal 和屏幕不连续性驱动，screen trace 也优先复用当前/历史屏幕证据；但它不是纯屏幕空间效果，screen miss 仍会进入 HWRT、SDF、Surface Cache 和 Radiance Cache。
- 它也像 HDRP probe volume，因为多个像素会插值共享 probe lighting；但 Screen Probe 不是预先烘焙或长期固定在世界里的 volume。它的 uniform/adaptive placement 随当前 view 每帧生成，当前帧 probe buffers 属于 view/frame 查询过程。

这个桥最重要的不是“都叫 probe”，而是 owner 差异：Surface Cache 的 card/page/material/lighting 属于当前 view 选中的 scene cache；Screen Probe placement 和 screen-domain histories 属于当前 view；Radiance Cache 虽然使用世界空间 clipmap，却由 per-view state 维护并可被多个当前-view feature 消费。不能把一套 HDRP volume 直觉同时套在这三类数据上。

### 8.6 红墙在屏内与屏外时，Probe 本身没有换 owner

相机正对红墙时，地面 probe 朝墙方向的 ray 可能由 screen trace 直接解决。相机转头后，同一个 view 仍按屏幕放置 probe，但红墙不再存在于屏幕历史的有效可见区域，ray 必须进入世界表示。

变化的是 ray 的解决路径，不是 probe 的 owner。若把“红墙离屏后 GI 错”直接归因于 probe placement，可能会错过真正问题：world trace representation、Surface Cache coverage、lighting 是否有效，或 Screen Probe 是否只能读取粗 fallback。只有同场景中启用了 Surface Cache feedback 的 Lumen Reflections，或显式反馈的 Lumen visualization 实际提出需求时，CPU page feedback 才可能在后续帧改善高分辨率驻留。

## 9. Trace fallback：让“未解决 ray 集合”逐层缩小

### 9.1 核心数据不是“用了哪个 backend”，而是“还有哪些 ray 没解决”

分层 tracing 最重要的抽象，是维护一组尚未得到有效 radiance 的 ray。每一层只处理当前需要的集合：

```text
初始化全部 ray 为 unresolved
  → screen trace 解决可由屏幕历史回答的 ray
  → compact：只保留 screen miss
  → HWRT 或 Mesh SDF/Heightfield 解决主世界命中
  → compact：只保留仍未解决的 ray
  → Global SDF voxel / Radiance Cache / sky 处理最终 fallback
```

Compaction 不是普通性能小优化，而是把控制流转换成数据流：后续昂贵 pass 通过紧凑列表只处理 miss。

如果没有 compaction，每个 backend 都要扫描全部 probe rays；已经 screen hit 的 ray 仍会进入 HWRT/SDF，既浪费成本，也需要额外规则防止后层覆盖前层有效结果。

### 9.2 第一层：Screen Trace 保留屏幕细节，但受可见性约束

Screen trace 使用可用的历史屏幕输入与 HZB，优点是：

- 能利用已经渲染出的高频几何和材质细节；
- 不需要为这些 ray 访问离屏世界结构；
- 对角色、细小表面和当前可见红墙可能比粗 Surface Cache/SDF 更精细。

缺点来自信息边界：

- 屏幕外表面不存在；
- 被遮挡表面没有直接证据；
- 历史可能陈旧或在 camera cut 后无效；
- depth thickness 与真实几何不完全一致，可能产生漏光或错误遮挡。

关闭 screen trace 的好处是减少历史依赖和部分屏幕空间不稳定；坏处是更多 ray 落到 HWRT/SDF，成本上升，并可能失去屏幕中已有的精细信息。对快速变化、屏幕历史问题明显且世界 trace 预算充足的场景，关闭它可以作为质量取舍或诊断手段；不是无条件升级。

### 9.3 第二层：HWRT 或软件 Detail Trace

若当前 Lumen path 允许 HWRT，screen miss 可以进入硬件加速结构。HWRT 提供更直接的几何命中，但有两个成本层要分开：

1. 加速结构随实例和几何变化的维护成本；
2. 命中后是否执行完整 hit lighting 的成本。

仅打开 HWRT 并不会自动承担第二项。默认 mode 0 仍可以在命中后查询 Surface Cache。

软件路径则使用 Mesh SDF，必要时加入 heightfield。Mesh SDF 保留单 mesh 级别的距离结构，适合 detail trace；heightfield 专门服务 Landscape 一类表示。它们要求项目和当前 show/scalability 条件允许 distance field tracing。

HWRT 与 SDF 的取舍不是“新技术一定更好”：

- HWRT 更接近实际几何，适合镜面和复杂命中，但动态大场景与 hit lighting 成本高；
- Mesh SDF 可在无 HWRT 平台工作，查询成本可控，但薄几何、变形和距离场近似会限制质量；
- Heightfield 对地形高效，但不能代表任意 mesh。

### 9.4 第三层：软件与硬件路径的最终 fallback 不完全相同

主世界 trace 后，系统再次 compact 剩余 miss。

在软件路径中，若 Global SDF tracing 开启，可以用更粗的全局 voxel/distance representation 继续覆盖远距离；同时 Radiance Cache 和 sky 为低频或没有明确表面命中的方向提供结果。

在 HWRT path 中，最终阶段会使用 Radiance Cache interpolation 等 fallback，而不是再走软件 Global SDF voxel trace。看到事件名类似 `RadianceCacheInterpolate` 时，不应据此认定“没有发生任何远距离 fallback”；它反映的是硬件路径下最终未解决 ray 的另一种处理。

### 9.5 命中几何与读取 lighting 是两个状态

地面 probe 的 ray 通过 HWRT 找到红墙，只证明“几何命中成立”。随后还要决定 radiance 从哪里来：

- Surface Cache hit lighting：查询红墙 card/page 的 final lighting；
- 完整 hit lighting：在命中点执行更完整的材质、直接光与阴影计算；
- fallback/radiance cache/sky：当没有可接受表面结果时提供低频或环境结果。

因此 last-valid-state 必须把“hit valid”和“lighting valid”拆开。HWRT visualization 看见正确几何，但最终 GI 仍旧，问题可能在 Surface Cache coverage、lighting atlas 或 hit lighting mode，而不是加速结构。

### 9.6 Worked case：同一条红墙 ray 的状态变化

**屏内阶段**：红墙存在于有效屏幕历史中。Ray 在 screen trace 得到命中与 radiance，状态从 unresolved 变成 resolved，不进入后续 compact list。

**刚转出屏幕**：screen trace miss，ray 进入 compact list。HWRT 可用时由硬件结构找到红墙；否则 Mesh SDF/heightfield 尝试命中。命中后默认仍可能采 Surface Cache lighting。

**Surface Cache 高层 page 尚未准备**：几何命中有效，Screen Probe 读取粗 fallback 或较低质量 cache；这条查询不产生 CPU page feedback 或 page last-used 写入。若同场景中启用了反馈的 Lumen reflection ray 也查询这块红墙，它才可能尝试写 raw feedback并独立更新相应 last-used buffer；只有 raw capacity 接受的 element，才会在 ring 接受该 submission 后进入 hash/compaction 并产生 unique/count output。

**主世界仍 miss**：ray 进入最终 compact list。软件路径可继续查询 Global SDF voxel；Radiance Cache 或 sky 提供低频 fallback。

这个案例说明“红墙 GI 不对”至少可能是四个不同状态：screen history 无效、world geometry miss、surface lighting 无效、最终 fallback 不合适。它们不能靠一个 `r.Lumen.*` 总开关区分。

## 10. Radiance Cache：世界空间低频查询的共享层

### 10.1 它与 Surface Cache 的缓存键不同

Surface Cache 的键接近“card + page + card UV”，数据贴在表面上。Radiance Cache 的键接近“世界空间 clipmap cell → probe index”，数据存在世界空间 probe atlas 中。

因此：

- Surface Cache 擅长回答某个命中表面的材质和 lighting；
- Radiance Cache 擅长回答某个世界空间邻域的低频 radiance/irradiance；
- Surface Cache 缺失不能靠把 Radiance Cache 当成同一张 atlas 修复；
- Radiance Cache 冷启动也不意味着 card/page 出错。

### 10.2 为什么使用 clipmap

若在整个世界铺固定高密度 probe grid，内存和 trace 数会随世界体积增长。Clipmap 用多个同分辨率、不同世界尺度的层级围绕 view 覆盖空间：近处 cell 小、细节多；远处 cell 大、表示更低频。

它解决了“有限 probe 数如何覆盖不同距离”问题。代价是层级过渡、相机移动时的重映射以及远处细节损失。

对小型固定体积，单层 dense grid 可能更简单；对开放世界或移动相机，多级 clipmap 能让成本主要与固定 cache 容量相关，而不是世界绝对尺寸相关。

### 10.3 3D Indirection Texture 为什么存在

Clipmap cell 不直接存完整 radiance，只存“这个世界位置应该查 probe atlas 的哪个 probe”。3D indirection texture 把世界空间位置映射到 probe index；probe atlas 则保存真正的 radiance、irradiance、depth、occlusion 或 sky visibility 数据。

分离 indirection 与 atlas 有三个好处：

- 当前帧只为被消费者标记的 cell 分配 probe；
- 相机移动时可以让新 cell 重用仍有效的旧 probe；
- probe atlas 容量可以固定，而世界 cell 的映射每帧重建。

若 world cell 直接拥有固定 atlas 槽，移动 clipmap 会频繁搬运大量 radiance；若完全没有 indirection，就难以表达“当前需要哪些位置”和“这些位置复用了哪个历史 probe”。

### 10.4 Consumer 先 mark，Cache 再分配

Radiance Cache 不会无条件更新所有 cell。Screen Probe、visualization、hair 或 translucency 等消费者先通过 mark callback 声明“这些区域后续可能采样”。随后 cache 才：

1. 清理当前帧 indirection；
2. 在允许复用时，把上一帧 probe 传播到新的 clipmap 映射；
3. 接收各消费者的 mark；
4. 为已用 cell 分配或复用 probe；
5. 建立 priority histogram，在 trace budget 内选择 probe；
6. 只 trace 被选中的 probe；
7. 过滤、修边、生成 mip，并发布 interpolation parameters；
8. 把结果转回外部资源，供后续帧复用。

这条控制流与 Surface Cache feedback 不同：Radiance Cache 的当前帧 mark 主要在 GPU 侧直接形成需求，不需要把每个 probe request 读回 CPU 后再决定。

### 10.5 Persistent reuse 什么时候成立

上一帧 Radiance Cache 只有在以下条件大体兼容时才可作为 persistent cache 传播：

- 没有强制 full update；
- 当前 view 有持久 state；
- indirection 和 probe resources 的尺寸、格式与配置仍兼容；
- 本帧没有因 resize 重建 history resources；
- 没有要求传播的全局 lighting 大变化。

即使 persistent cache 有效，也不是所有 probe 永久不变。Probe 会记录最近使用和最近 trace 状态，超出保留窗口的未使用 probe 可以回收；保留太短会增加重复 trace，保留太长会增加陈旧 radiance 被过滤进结果的风险。

全局 lighting 变化时，系统可以提高 trace budget，但预算仍然是一种折中：快速刷新提高响应，GPU 成本也随之增加。

### 10.6 为什么不让 Screen Probe 自己保存这些 world probes

Screen Probe 的索引域是当前屏幕网格；Radiance Cache 的索引域是世界空间 clipmap。二者生命周期和消费者不同：reflection、hair 或 translucency 也可能消费 Radiance Cache。

如果 Screen Probe 独占 Radiance Cache 数据，其他消费者要么重复建立 world probes，要么被迫理解 Screen Probe 的屏幕布局。独立 cache state 让多个消费者共享世界低频结果，同时保留各自的 query/history。

### 10.7 Worked case：红墙后面的远处角落

地面 probe 的一条 ray 指向红墙后方的远处角落。Screen trace 看不到，Mesh SDF/HWRT 可能因距离、预算或没有合适命中而不能提供理想结果。

Screen Probe 先 mark 可能采样的世界区域。Radiance Cache 把该位置映射到 clipmap cell：若上一帧有兼容 probe，就传播并按需要重 trace；若没有，则分配新 probe，并根据预算决定是否本帧更新。

当 interpolation parameters 有效时，ray 可以得到低频 radiance。这里的结果适合“远处环境大致有多少光”，不适合还原镜子里清晰的红墙轮廓。

调试顺序是：目标世界位置是否被 mark、indirection 是否指向有效 probe、probe 是否分配、是否进入 trace budget、atlas 是否过滤完成、消费者是否拿到 interpolation parameters。

## 11. Filtering 与 Integration：从 ray radiance 变成像素 lighting

### 11.1 Trace 输出为什么不能直接加到 SceneColor

Probe 的每个八面体方向只有有限 ray，原始 radiance 会有噪声、miss 和不同 backend 的不连续；它还停留在 probe atlas 中，并不对应最终像素。

因此需要两个不同阶段：

- **Filter**：让每个 probe 的方向分布更稳定，并建立可采样的 irradiance/radiance representation；
- **Interpolate and Integrate**：根据当前像素的深度、法线、材质和周围 probe，把方向分布积分成像素 lighting。

如果省略 filter，少量随机 ray 会直接表现为强噪声；如果省略像素积分而直接把 probe 颜色放大，表面方向、粗糙度和几何边界都无法正确参与。

### 11.2 Probe-domain filtering 的状态流

Screen Probe filtering 大致把数据变成：

```text
各 backend 写入的 trace radiance / hit
  → CompositeTraces：统一成 probe tracing atlas
  → 判断 lighting 是否快速变化
  → 可选 probe temporal accumulation
  → 可选 spatial filtering
  → 转换为 irradiance 表示
  → 修复八面体边界并生成 mip
```

Probe temporal reuse 会检查上一帧 probe radiance、probe depth、translated world position、view rect、资源尺寸和相关配置是否兼容。它减少噪声，但也会引入响应延迟和 history rejection 问题。

Temporal 与 spatial filter 解决的不是同一种噪声来源，也不能互相无代价替代：

- **Temporal accumulation** 用时间换样本。静止或缓慢变化区域可以积累更多有效 ray，细节保持通常优于大范围模糊；代价是 lighting 变化响应变慢，重投影错误会形成拖影。降低最大累积帧数能更快响应，但噪声会增加。
- **Spatial filtering** 用邻域换样本。它不必等待多个 frame，适合当前帧立即压制高频噪声；代价是跨深度、法线或材质边界混合时会漏光和过度模糊，因此必须依赖几何权重和有限 kernel。
- **提高每 probe ray 数或 probe 密度** 可以减少对两种 filter 的依赖，但会直接增加 tracing、atlas 和 integration 成本。在离线截图、高端目标或小分辨率视图中可能更合适；在常规实时分辨率下，时间与空间复用通常更经济。

UE 同时保留 temporal、spatial、probe density 和 ray count 这些控制维度，是因为项目对噪声、拖影、细节和 GPU 时间的优先级不同。调试时关闭某种 filter 可以定位问题来源，但不应把关闭后的高噪声结果当成可直接发布的最终方案。

### 11.3 为什么 rough specular 可以从 probe 中顺带积分

粗糙 BRDF 会把很宽的入射方向卷积到一起，相当于对环境高频做低通。Probe 的有限方向 radiance 和 mip 因而可以近似这类低频 specular。

Diffuse 与 rough specular 都可以从过滤后的 probe distribution 积分，但使用的材质权重不同。镜面反射需要保留非常窄的方向高频，不能只依靠同一份低分辨率 probe，后面第 12 节会分开。

### 11.4 `InterpolateAndIntegrate` 实际产生哪些数据

Integration 会创建并写入：

- `DiffuseIndirect`：白色地面得到的红墙 GI；
- `BackfaceDiffuseIndirect`：双面植被等路径需要的背面 diffuse；
- `RoughSpecularIndirect`：probe distribution 产生的低频粗糙镜面；
- `LightIsMoving`：帮助时间历史判断 lighting 变化速度的内部信号。

这里必须钉死输出 contract：`RenderLumenScreenProbeGather` 返回给上层的 signal 槽位是：

| 槽位 | Screen Probe Gather 返回内容 |
| --- | --- |
| 0 | `DiffuseIndirect` |
| 1 | `BackfaceDiffuseIndirect`，不支持时为 black |
| 2 | `RoughSpecularIndirect` |
| 3 | **不由 Screen Probe Gather 产生**；由更上层填入 Lumen Reflections 或 SSR |

`LightIsMoving` 不占返回槽位 3。它参与 Screen Probe history 更新，是内部控制数据。

### 11.5 Full-resolution history 与 probe history 不是一份东西

Probe filtering 可以复用 probe-domain radiance/depth/world-position；Integration 之后，系统还会为完整分辨率的 diffuse、backface、rough specular、累积帧数和 short-range signals 建立另一类 temporal history。

前者回答“这个 probe 上一帧看到什么”；后者回答“这个屏幕像素重投影后能否复用上一帧最终 indirect signal”。两者的分辨率、key 和 rejection 条件不同，不能只用一句“最后写进 per-view history”带过。

### 11.6 为什么不在 final gather 里直接写 SceneColor

Final gather 可能运行在 async compute，并产生多张后续需要共享的纹理。SceneColor composite 还要考虑 AO、screen bent normal、Substrate tile、reflection method、SSR/Lumen reflection 输入和 render target blend。

把“生成 lighting signals”和“改写 SceneColor”拆开有几个收益：

- final gather 可以与 graphics direct lighting 重叠；
- reflections 可以独立完成后再填槽位 3；
- 同一组 signals 可以被 history、debug 和 composite 使用；
- composite 集中处理 SceneColor load/blend 和材质路径。

代价是多一层中间纹理和所有权交接。对非常简单、不需要 async 或多消费者的 renderer，直接写最终颜色可能更省内存；UE 的通用 deferred renderer 更需要这层解耦。

### 11.7 Composite 是“画面成立”的边界

上层在需要时运行 Lumen Reflections 或 SSR，把结果填入 signal 槽位 3。若 reflection method 不是 Lumen，相应路径还会避免同时保留 Lumen rough specular，防止双重 specular。

随后 `DiffuseIndirectComposite` 绑定槽位 0–3 与其他 composite 参数，把 Lumen indirect 添加到已有 SceneColor。对常规 Lumen 路径，核心颜色关系是已有 SceneColor 加 indirect；AO-only 或其他非 Lumen组合可能使用不同 blend 关系，不能把它们反推成 Lumen gather 的固定公式。

因此：

> Trace hit 只证明 ray 找到了证据；Integration 输出只证明 Lumen lighting texture 成立；只有 Composite 绑定正确输入并写目标后，当前画面中的红色 GI 才成立。

## 12. Reflections：粗糙地面与镜子为什么需要两种频率模型

### 12.1 Roughness 是信号频率问题，不只是材质参数

粗糙地面对入射环境做宽范围卷积。即使红墙边缘很清晰，卷积后的 specular 也是低频、模糊的。Screen Probe 的有限方向分布和 mip 可以较经济地近似它。

镜子对应非常窄的 BRDF lobe。它需要知道几乎单一反射方向上究竟命中了红墙的哪一部分；probe 插值会把方向和空间细节抹平，镜中红墙会变成模糊色块。

这就是两条路径存在的根本原因：

| 路径 | 适合的频率 | 数据与成本 | 主要 owner |
| --- | --- | --- | --- |
| `RoughSpecularIndirect` | 低频、较粗糙 specular | 复用 Screen Probe tracing/filter/integration，成本较低 | Screen Probe gather |
| `RenderLumenReflections` | 更高频、镜面或专用 reflection | 独立 tile、ray、trace、resolve、temporal/spatial denoise | Reflection path + `FReflectionTemporalState` |

### 12.2 专用 Reflection path 做了什么

Lumen Reflections 会先按材质/roughness 对屏幕 tile 分类，为需要的像素生成 reflection rays，再执行分层 tracing、resolve 和 denoise。它可以复用 Radiance Cache interpolation parameters，但拥有自己的 ray 数据、resolved specular、second moment 和 accumulated frame count。

它与 Screen Probe 在“screen trace → 世界 trace → fallback”思想上相似，但不是同一份 dispatch、输出或 history。

### 12.3 为什么 reflection history 不能直接复用 diffuse history

Diffuse history 的信号是半球积分后的低频 lighting；reflection history 的信号与单一反射方向、roughness、命中深度和高频变化相关，还需要 second moment 估计方差。

若二者共用 history：

- diffuse 的宽方向平均会污染镜面细节；
- reflection 的高频变化会让 diffuse history 过度 rejection；
- 不同 depth/normal/roughness key 无法用一套有效性条件表达。

因此 opaque、front-layer translucency 和 water 还可以拥有不同 reflection temporal states。

### 12.4 替代方案何时更合适

- **SSR**：只需要屏幕内反射、成本严格受限时很有价值；离屏和遮挡信息仍会缺失。
- **Reflection Capture**：静态或低频环境反射很便宜、稳定，但无法表达当前动态红墙的精确镜像。
- **Lumen Reflections + Surface Cache lighting**：覆盖屏幕外且成本受控，但质量受 Surface Cache representation 限制。
- **Lumen Reflections + hit lighting**：镜面质量更直接，但材质、灯光和 shadow rays 显著增加成本。
- **Path tracing**：离线或允许大量样本时提供更统一的光传输解法，不适合常规实时帧预算。

### 12.5 红墙三种 specular 结果

白色粗糙地面上的红色 specular 泛光，可能来自 `RoughSpecularIndirect`。镜子中的清晰红墙来自专用 Lumen Reflections。若项目改用 SSR，红墙一旦离屏，镜中结果可能失去依据；若使用 reflection capture，看到的是预捕获环境而不是当前动态状态。

调试时先根据材质 roughness 和当前 reflection method 判断 owner，再检查对应 output/history。不要用 `RoughSpecularIndirect` 的正确与否证明镜面 reflection path 一定正确。

## 13. History Taxonomy：Lumen 不是只有“一张上一帧结果”

### 13.1 为什么必须分类

跨帧复用可以减少噪声和重复计算，但旧数据只有在“缓存键仍代表同一事物”时才有效。不同 Lumen 子系统使用不同缓存键：card/page、world clipmap cell、screen probe、full-resolution pixel、reflection ray 或 readback submission。

把它们统一称为 history，会导致两种典型错误：

- camera cut 后清空了所有 scene cache，造成不必要的巨大冷启动；
- scene/material 已变化却继续接受旧 Surface Cache，画面稳定但错误。

### 13.2 七类跨帧状态

| History / cache | Owner 与 key | 保存什么 | 主要有效条件 | 失效或陈旧症状 |
| --- | --- | --- | --- | --- |
| Surface Cache material persistence | `FLumenSceneData`；card/page/physical mapping | albedo、opacity、normal、emissive、depth | card 仍代表同一表面，page mapping/capture 有效 | coverage 缺失、旧材质、page 跳变 |
| Surface Cache lighting/radiosity history | `FLumenSceneData`；card page/tile | direct、indirect、final、累积状态 | material/page 兼容，lighting change 被正确传播 | 光照响应慢、旧颜色、冷启动噪声 |
| Surface Cache feedback readback | scene feedback ring；reflection/visualization submission identity | allocator 记录写入尝试；raw buffer 只保存容量接受的 elements；accepted path 仅从该子集生成 unique elements 与重复 count；CPU request 只在 copy ready 并被消费后形成 | raw writer 有权限、raw capacity 接受 element、ring 接受 submission、hash/compaction output 有效、readback copy ready、CPU 成功消费 | reflection/visualization 驱动的 page 长期不升级，但当前粗 cache 仍可工作 |
| Screen Probe probe-domain history | `FViewState`；probe screen/world/depth identity | probe radiance、probe depth、translated world position | view/probe layout、尺寸、camera continuity 与配置兼容 | probe 噪声、拖影、边缘 history 错配 |
| Full-resolution indirect history | `FViewState`；重投影后的像素/depth/normal | diffuse、backface、rough specular、frame count、short-range signals | camera 未 cut、transform/extent/closure/config 兼容 | 像素级拖影、闪烁、冷启动 |
| Radiance Cache persistence | `FViewState`；world clipmap cell→probe | indirection、probe atlas、last-used/traced、allocator | resource/config 兼容，无强制 full update 或传播性全局光照变化 | 远处低频 lighting 冷启动或陈旧 |
| Reflection temporal history | 各 `FReflectionTemporalState`；reflection pixel/ray/depth | specular+second moment、accumulated frames、可选 layer depth/normal | camera/transform 连续，资源与 pass 类型兼容 | 镜面拖影、闪烁、方差估计错误 |

若启用 ReSTIR Gather，它还拥有 reservoir resampling 与 full-resolution accumulation history；这进一步说明 gather 分支不能共享一套模糊的“Lumen history”。

### 13.3 Camera cut 变体：应该丢什么，不该丢什么

相机发生 camera cut 时，旧屏幕位置与当前像素没有连续运动关系。Screen Probe probe-domain history、full-resolution indirect history和 reflection temporal history通常应拒绝旧重投影。

但红墙仍是同一 scene 中的红墙。只要 scene data、card/page 和 lighting 没有因其他原因失效，Surface Cache 不需要仅因为 camera cut 全部重建。Radiance Cache 是否可复用则取决于其资源兼容、clipmap 更新和全局 lighting 条件；它不是简单等同于屏幕 history，也不能仅凭 camera cut 一句话推断全部清空。

Camera cut 后可能看到：

- 当前帧 probe/reflection temporal 冷启动，噪声短暂增加；
- Surface Cache 仍能给 world hit 提供红墙 lighting；
- 新 view 区域尚未 mark 的 Radiance Cache probe 需要分配/trace；
- 若启用了反馈的 Lumen Reflections 或显式反馈 visualization 在新视角中实际写入需求，page feedback 才会按异步节奏补充相应高分辨率 page；Screen Probe 自身不会启动这条 CPU readback。

这组现象同时出现是合法的，因为它们属于不同 history owner。

### 13.4 全局 lighting 变化与 camera cut 不同

太阳强度或方向发生需要传播的全局变化时，Surface Cache lighting、Screen Probe history 和 Radiance Cache reuse 都可能需要更激进刷新或拒绝旧数据；material card/page 本身却仍可能有效。

所以失效模型应根据“什么身份变了”来判断：

- 观察关系变了：优先影响 view histories；
- 表面身份/材质变了：影响 card capture 与相关 lighting；
- 灯光状态变了：影响 lighting/radiosity 和依赖它的 histories；
- 资源尺寸/配置变了：影响对应 owner 的 compatibility；
- 具备写权限的 sampling producer 已写 raw、ring 也接受了 submission，hash/compaction 与 readback copy 已排入 graph，但 copy 尚未 ready：只影响对应 reflection/visualization 需求闭环的时机，不等于当前 atlas 自动无效。

## 14. 完整 Worked Case：一个地面像素怎样拿到红墙 GI

现在把红墙、白色地面、粗糙 specular、镜子、转头和 camera cut 放回同一条状态链。

### 状态 1：红墙进入可查询世界

Renderer scene 把红墙 primitive 纳入当前 view 选择的 Lumen Scene Data。系统建立 mesh cards 和 cards；软件 tracing 需要时还会有 Mesh SDF，HWRT 需要时会有加速结构实例。

此时只是“存在表示”，还没有证明 Surface Cache page 有效。

### 状态 2：Surface Cache 建立粗层 representation

红墙 card 获得较粗 page allocation。Card Capture 从 card 方向生成 material 数据，copy 到持久 atlas；Surface Cache lighting 逐步写 direct、radiosity 和 final lighting。

现在 world ray 命中红墙后可以查询一个低分辨率但有效的红色 radiance。

### 状态 3：相机正对红墙

当前 view 在地面布置 uniform probes，并在红墙/地面边缘补 adaptive probes。某条 probe ray 朝向红墙，screen trace 从有效屏幕历史直接解决它。World trace 和 Surface Cache 仍存在，但这条 ray 不需要继续付费。

Probe filtering 和 integration 得到地面像素的 `DiffuseIndirect` 与可能的 `RoughSpecularIndirect`。此时它们仍是中间 signal；Composite 后 SceneColor 才真正出现红色溢色。

### 状态 4：相机转头，红墙离屏

Screen trace 对该方向失去证据，ray 进入 unresolved compact list。HWRT 或 Mesh SDF/heightfield 尝试在世界中找到红墙。命中后，Screen Probe 默认可以查询当前有效的 Surface Cache final lighting；如果只有粗 page，它就接受合法的较低细节 fallback，但不会写 page last-used buffers。

若只有粗 page，Screen Probe 接受较低细节的合法 fallback，不会自行写 CPU page feedback。镜子中的 Lumen reflection ray 若启用了 Surface Cache feedback，并对同一红墙提出更细需求，sampling shader 会递增 allocator、尝试写 raw feedback，并独立更新对应 last-used；只有 raw capacity 接受的 element 才能在 ring 接受 submission 后进入 hash/compaction unique/count output，再 enqueue readback copy。某个后续帧 copy ready 且 CPU 消费后才生成 request，capture 与 lighting 仍要等待各自预算。因此由 reflection feedback 带来的质量改善不是固定一帧事务。

### 状态 5：相机走近红墙

高分辨率 page 获得物理空间。旧 lighting 先重采样，新 material capture 完成，page table 发布新映射，direct/radiosity 再逐步刷新。地面 GI 从粗而稳定的结果收敛到更细结果。

若 atlas 空间或预算不足，系统可能继续使用粗 fallback。此时“没有立刻变清晰”不等于 trace miss。

### 状态 6：远处角落进入 Radiance Cache

另一条 probe ray 指向远处角落。Screen/world trace 没有得到理想表面结果，Radiance Cache 对被 mark 的世界 cell 提供低频 interpolation。它帮助 diffuse/rough 查询稳定，但不会给镜子恢复红墙清晰边缘。

### 状态 7：粗糙地面与镜子分流

粗糙地面从 Screen Probe distribution 积分 `RoughSpecularIndirect`。镜子 tile 进入专用 Lumen Reflections，生成独立 ray、resolve 和 denoise 结果，再由上层填入 signal 槽位 3。

两者最后都能影响 SceneColor 的 specular，但 owner、频率、trace 数和 history 不同。

### 状态 8：发生 camera cut

旧屏幕重投影关系失效，Screen Probe 与 reflection temporal histories 被拒绝或冷启动。Surface Cache 仍可能保持红墙 material/lighting；新 view 需要重新放 probe 并 mark Radiance Cache 区域。只有启用了反馈的 Lumen Reflections 或显式反馈 visualization 在新视角实际写入 page 需求时，非固定延迟的 readback 链才会提升对应 page 质量；Screen Probe 继续读取现有 cache 和粗 fallback。

如果 camera cut 后只有一两帧噪声而世界 GI 大体存在，说明 scene cache 与 view history 正按不同生命周期工作；若红墙彻底失去离屏贡献，应回到 representation/trace/Surface Cache，而不只是调 temporal filter。

## 15. Last-Valid-State：从最后成立的证据向后查

### 15.1 为什么症状表和 CVar 清单不够

“GI 黑了”“镜子没反射”“转头后很糊”都可能由多个 owner 造成。随机关闭 screen trace、temporal、HWRT 或 Surface Cache，虽然可能改变症状，却不能证明根因。

Last-valid-state 方法要求每一步先找到一个可观察证据，证明状态已经成立，再进入下一步。第一个不成立的状态，才是当前最小问题域。

### 15.2 完整证据梯

#### Gate 0：路径资格成立吗

- 当前 view 是否选择并允许 Lumen GI/reflections？
- 平台、项目配置、view state 和 HWRT/软件 tracing 至少有一条满足？
- 是否被 path tracing、debug mode、planar/reflection capture 条件排除？

若不成立，停止。后续资源不存在是结果，不是根因。

#### Gate 1：选中了正确的持久 owner 吗

- 当前 view 使用 default 还是 view/GPU-specific Lumen Scene Data？
- 多 view 或多 GPU 下是否检查了错误缓存？
- `FViewState` 是否存在且可写，还是 read-only/无 history view？

#### Gate 2：几何 representation 存在吗

- 红墙是否进入 Lumen primitive groups / mesh cards / cards？
- 软件路径需要的 Mesh SDF/heightfield 是否存在？
- HWRT path 的实例是否在加速结构中？

这一步只证明“能被世界查询”，不证明 lighting 正确。

#### Gate 3：Surface Cache 地址有效吗

- Card 是否有有效最低分辨率分配？
- Page table 是否映射到物理 page/sub-allocation？
- 高分辨率缺页时是否有合法粗 fallback？

#### Gate 4：Material capture 有效吗

- Classic 或 Nanite Card Capture 是否覆盖目标 page？
- Albedo/normal/emissive/depth 等持久 layer 是否正确？
- Dilation/copy/compression 后边界是否仍有效？

#### Gate 5：Surface Cache lighting 有效吗

- Direct lighting 是否写入？
- Radiosity/indirect history 是否有效或正在预算内刷新？
- Final lighting 是否组合并发布？
- 全局 lighting 变化是否正确触发更新？

#### Gate 6：Feedback 只是未收敛，还是闭环断了

- 当前查询是否来自具备 feedback 写权限且实际运行的 Lumen Reflections，或显式反馈的 Lumen visualization？若只有 Screen Probe，应离开这条 feedback writer 调试支路，转而检查 world hit、现有 cache、合法粗 fallback 与 Surface Cache lighting，不要等待 CPU feedback 或 last-used 变化。
- 有权限的 sampling writer 是否使用 feedback-enabled permutation 和非零 feedback resources？Raw allocator 是否增长，对应 page last-used 是否更新？
- `allocator` 与 `BufferSize` 的关系是什么？只有容量接受的 `WriteOffset` 才应在 raw buffer 中出现预期 card/res/local-page element；allocator 超容量时，raw element 缺失不等于 last-used writer 未运行。
- Readback ring 是否接受了本次新 submission？若 ring 满，停止点在这里，本批 raw data 不会建立 hash/compaction/readback pass，也尚未形成 CPU request。
- Accepted submission 是否只对成功落盘的 raw 子集建立 hash，并产生 compacted unique elements 与重复 count？
- Readback copy 是否已经 enqueue？是否存在 ready readback，而不是假设下一帧必定 ready？
- CPU 是否消费 ready data 并形成了 Surface Cache request？
- 已经存在的 request 是否继续被 capture/atlas/lighting budget 推迟？
- 只有确认 feedback writer 实际写入后，才继续检查与 CPU readback 分离的 last-used buffer 是否正确 extraction/import；帧末 extraction 只延续已有 buffer，不会替纯 Screen Probe 或未写入的帧生成证据。

这一步用于解释质量升级和收敛，不应覆盖前面“最低有效 representation”检查。

#### Gate 7：Screen Probe 放置有效吗

- Uniform probe grid 是否覆盖目标像素？
- 深度/法线/world position 是否属于正确表面？
- 边缘是否产生必要 adaptive probes？

#### Gate 8：Ray 在哪一层从 unresolved 变成 resolved

- Screen trace 是 hit、合法 miss，还是用了错误历史？
- Miss 是否进入 compact list？
- HWRT 或 Mesh SDF/heightfield 是否命中？
- 剩余 miss 是否进入 Global SDF/Radiance Cache/sky fallback？
- 命中后选择的是 Surface Cache lighting 还是 hit lighting？

#### Gate 9：Radiance Cache 可插值吗

- 世界区域是否被 consumer mark？
- Indirection 是否指向有效 probe？
- Probe 是否分配并进入 trace budget？
- Persistent reuse 是否因 resize/global change 被拒绝？
- Filter/mip 是否完成并发布 interpolation parameters？

#### Gate 10：Probe signal 成立吗

- Trace radiance 是否正确合并？
- Probe-domain temporal 是否接受了合法 history？
- Irradiance 与 mip/border 是否有效？
- Integration 是否产生 diffuse、backface 和 rough specular？

#### Gate 11：Reflection signal 单独成立吗

- 当前 material/roughness 是否进入专用 reflection tiles？
- Reflection rays、resolve、temporal/spatial denoise 是否完成？
- 上层槽位 3 是 Lumen Reflections、SSR 还是 black？

#### Gate 12：Composite 真的写入画面了吗

- Gather 槽位 0–2 是否绑定正确？
- Reflection/SSR 槽位 3 是否由正确 owner 填入？
- `DiffuseIndirectComposite` 是否选择 Lumen permutation、加载并写当前 SceneColor target？

#### Gate 13：后续帧所需 history 发布了吗

- Screen Probe probe/full-resolution histories 是否提取到正确 `FViewState`？
- Reflection history 是否进入对应 opaque/water/translucent state？
- Surface Cache atlas/buffers 是否提取回正确 `FLumenSceneData`？
- Radiance Cache state 是否转换为可跨帧资源？

#### Gate 14：不要把“记录了 pass”误认为“GPU 已完成”

RDG 中声明 pass，只表示工作进入候选依赖图；图编译后该 pass 仍被保留，才表示它会进入执行阶段。执行阶段把工作录入 RHI command list；后端随后形成平台命令，Platform Queue Submit 再把这些命令交给 GPU 队列。队列已接收不等于 GPU 已消费；覆盖对应 copy 的 submission fence 完成后，才能继续确认 readback 已 ready，最后才是 CPU 取得并消费结果。对已被 ring 接受的 feedback submission，这些完成深度缺一不可。

因此调试 CPU/GPU 协作时，要逐层确认 RDG declared / retained、RHI recorded、platform commands formed、Platform Queue Submit、GPU consumed、readback ready 与 CPU consumed，不能只说“这一帧已经跑过”或“命令已经提交”。

## 16. 收束：用三层模型记住整章

### 第一层：世界表示

Lumen 不依赖单一“最真实”表示。Screen history 保留当前可见细节；HWRT/SDF/heightfield提供世界命中；Surface Cache 保存表面材质与 lighting；Radiance Cache 保存世界空间低频结果。每种表示都用信息损失换取特定成本优势。

### 第二层：当前视图查询

Screen Probe 用规则网格加有限 adaptive probes 降低每像素 ray 成本。每条 ray 作为 unresolved 状态依次通过 screen、world backend 和最终 fallback，compaction 让后续昂贵阶段只处理 miss。

### 第三层：跨帧有效性

Surface Cache、feedback readback、Screen Probe histories、Radiance Cache 和 reflection histories拥有不同 owner、key 和失效条件。Camera cut、材质变化、全局 lighting 变化、资源 resize 和 GPU readback 未 ready，影响的不是同一份状态。

回到红墙案例：

- 红墙的表面身份与缓存 lighting 属于 Surface Cache；
- 地面当前需要哪些方向的 radiance 由 Screen Probe 查询；
- 红墙屏内时可先复用 screen history，离屏后由 HWRT/SDF 与 Surface Cache 接手；
- 远处低频结果可由 Radiance Cache 共享；
- 粗糙 specular 可以从 probe distribution 积分，镜子需要专用 reflection rays；
- 所有中间结果只有在 composite 写入后才成为当前 SceneColor；
- 后续帧是否能复用，取决于各自 history 的身份和有效条件。

下一篇第 18 篇会转向 MegaLights：它解决的是“很多直接光如何在固定样本预算内被选择和求阴影”。它可以与 Lumen 共享底层 tracing 基础设施，但不会替 Screen Probe 查询间接光，也不会替 Surface Cache 决定 page 和 lighting history。
