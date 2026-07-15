# 13 体积效果与大气：把已光照的 SceneColor 扩展成天空、雾和云

> **源码版本**: UE5.7 本地源码快照  
> **前置阅读**: `05_RenderGraph.md`、`08_FrameInit.md`、`09_DepthPrepass.md`、`10_BasePass.md`、`12_Lighting.md`  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）

## 这章不回答“UE 有哪些天气效果”

设想一个清晨场景：太阳低悬，远山被空气染成淡蓝，地面贴着一层薄雾被阳光照亮、被树影切开，天上飘着一团蓬松的云。打开 UE，你会发现这些效果分别由 Sky Atmosphere、Volumetric Fog、Height Fog、Local Fog Volume、Volumetric Cloud 这几个互相独立的系统产生。一个 Unity 读者很自然会问：它们不都是“天空和空气”吗，为什么不放在一个 pass 里？画错了又该从哪查？

这正是本章要解决的真问题：**BasePass 和延迟光照已经把不透明物体写进 `SceneColor` 之后，UE 怎样把不属于 opaque surface 的天空、空气、雾团和云接入同一帧，同时保证谁先算、谁后混、谁被谁遮挡都是可调试的。**

Unity SRP 里你可能会把这段理解成 Skybox、Fog、Cloud、Transparent 之间的 pass 顺序——“越透明的越后画”。UE 的设计理由不在透明度，而在**数据形态**。这些效果看起来都像“空气”，但它们在一帧里最合适的存在方式完全不同：

- 大气散射对很多像素是共享的，适合先**预计算成查找表（LUT，Look-Up Table，把昂贵积分的结果按少量参数缓存成一张可采样纹理）**；
- 体积雾需要沿相机视线累计，适合先写一张**低分辨率 3D 体纹理**；
- 高度雾的密度沿视线有闭式积分，适合用一个全屏 shader **解析地**算出来；
- 云层细节强依赖视线穿过云体的路径，必须**逐步 ray march**；
- 局部雾体则视情况，既可能并入体积雾，也可能只在屏幕空间补一层。

所以 UE 不把它们塞进一个大 Atmosphere pass，而是让每个系统先生产自己最合适的中间表达，再按**资源依赖**顺序合成回 `SceneColor`。这里还要提前加上一条时间轴：有些资源会在 PrePass / BasePass 附近就开始准备，以便和几何阶段重叠；真正改写 `SceneColor` 的合成却发生在 Lighting 之后。理解了“早期准备、晚期合成”这两个窗口，本章其余内容才不会被误读成一个连续的天气 pass。

本章会用上面那个清晨场景作为贯穿例子：太阳是 atmosphere light，远山要叠空气透视，近处薄雾既有体积雾也有高度雾，天上那团云走 ray marching。读这章时只跟住三个问题：**当前步骤的输入是什么，输出资源归谁，后面谁会消费它。** 必要的 UE 符号只作为调试路标，不承担主教学。

## 本篇边界

本章属于 Part 3 的一帧流程章节，只讲接入点、输入输出、资源流和调度位置。

必须讲透：

- Sky Atmosphere 为什么先生成散射 LUT，以及这些 LUT 何时从 RDG 临时资源变成后续可采样输入。
- Volumetric Fog 为什么用低分辨率 3D froxel grid，怎样从介质属性、光照到 Z 向积分结果。
- Height Fog 为什么可以用全屏解析近似，以及它怎样和 Volumetric Fog 的 3D 结果组合。
- Volumetric Cloud 的 ray marching 在一帧里如何接入，为什么有 VRT 和 per-pixel 两条路径。
- Local Fog Volume 为什么不能简单当作“另一个 fog pass”，它如何避免和前面两类雾重复叠加。
- 这些体积效果之间的最终合成顺序，以及顺序错误时应该沿哪条资源线倒查。

只点到：

- Lumen、VSM、MegaLights、Clustered/Forward light data 作为体积雾或云的光照输入，细节回到对应专题。
- 云的完整物理模型、噪声建模、多重散射近似和材质编译，本文只讲 ray marching 接入和输出语义。
- Single Layer Water、Translucency、PostProcessing 只作为本章之后的消费者或特殊合成条件。

## 本篇必须能回答

读完这章，你应该能回答下面这些问题。它们就是后面各小节的目标：

- 为什么这五个系统不合成一个 pass，而是各自生产不同形态的中间资源？
- 为什么 Sky Atmosphere 的部分 LUT 在 PrePass / BasePass 前就开始准备，而天空颜色要等到 Lighting 后才写入 `SceneColor`？
- Sky 的 LUT 在“RDG 里算完”和“后续 pass 能采样”之间，差了哪一步？
- `IntegratedLightScattering` 这张 3D 纹理是谁生产的、谁消费的，没有它时画面会缺什么？
- Height Fog 和 Volumetric Fog 既然都是雾，为什么要排除距离、再组合，否则会怎样？
- 同一个局部雾体为什么可能走三条不同路径，又怎样保证它不被画两遍？
- 云的 alpha 和 depth 为什么不能当普通透明贴图来理解？
- 画面出错时，应该按“可见现象在哪个 pass”倒查，还是按“资源在哪生产、何时可读、由谁消费”倒查？

## 1. 先建立五种数据形态

回到清晨场景。同样是“空气效果”，UE 先问的不是“它叫什么”，而是“它应该以什么数据形态在一帧里传递”。下面这张表是本章的总地图——后面每个小节都是在把其中一行讲透：

| 系统 | 角色 | 主要输出（数据形态） | 后续消费者 |
| --- | --- | --- | --- |
| Sky Atmosphere | 把大气散射压缩成可复用查表数据，并把天空/远距空气混入场景 | 一组 LUT（透过率 / 多次散射 / 天空视图 / 相机空气透视），随后写 `SceneColor` | sky pass、cloud、height fog、材质天空 |
| Volumetric Fog | 在相机体积网格里计算“从相机到深度的体积散射与透过率” | `IntegratedLightScattering` 3D 纹理 | Height Fog pass、cloud fog-on-cloud |
| Height Fog | 按像素深度解析计算指数高度雾，并把体积雾结果组合进主场景 | 直接写回 `SceneColor` | 后续 local fog、cloud、water/translucency |
| Local Fog Volume | 局部雾体，可被折进体积雾、折进高度雾，或独立合成 | 取决于路径：写入 froxel、在 height fog shader 内贡献，或写 `SceneColor` | 前两类雾或独立 pass |
| Volumetric Cloud | 对云层体积材质 ray march，输出云颜色、透过率和云深度 | 离屏 VRT 资源，或直接写 `SceneColor` | VRT compose 或本身完成合成 |

注意表格最右两列：有的系统**生产中间资源给别人采样**（Sky 的 LUT、体积雾的 3D 纹理、云的 VRT），有的系统**直接改 `SceneColor`**（Height Fog）。这条“生产者 vs 合成者”的区分，是后面理解合成顺序和调试方向的关键。

### 清晨场景的资源账本

把开篇清晨场景拆成资源账本，会比按"天空、雾、云谁更透明"来记顺序更稳：

| 系统 | 本帧生产或修改什么 | 谁消费它 | 第一处调试边界 |
| --- | --- | --- | --- |
| Sky Atmosphere | 透过率、多次散射、sky view、camera AP 等 LUT；随后把天空 / 空气透视写进 `SceneColor` | sky pass、cloud、height fog、后续合成 | LUT 是否已建立 view / scene 可读绑定，消费者是否拿到当前版本 |
| Volumetric Fog | `IntegratedLightScattering` 3D 纹理，以及可选 history | Height Fog、cloud fog-on-cloud、后续体积消费者 | `ComputeVolumetricFog` 是否真的 produced，fog uniform 是否启用体积雾 |
| Height Fog | 解析高度雾 + 体积雾采样后的 `SceneColor` | cloud、Local Fog Volume 独立 pass、水、半透明、后处理 | 输入 `SceneDepth`、Sky / VF 资源是否 ready，写回位置是否在 cloud 前 |
| Local Fog Volume | view-local instance / tile 数据；再按路径成为 froxel 介质、height fog 贡献或独立 `SceneColor` 写入 | Volumetric Fog、Height Fog 或独立 LFV pass | 当前 view 选择了哪条接入路径，以及是否已经被前面路径处理 |
| Volumetric Cloud | VRT color/depth 或直接 per-pixel 写 `SceneColor`；输出 transmittance / cloud depth 等语义 | VRT compose、后续水 / 半透明 / 后处理 | 当前 view 走 VRT 还是 per-pixel，trace 结果是否真正合成回主场景 |

这张账本给调试一个很实用的入口：先问“现象依赖的资源由谁生产、何时变成可采样输入”，再问“哪个 pass 看起来画错了”。很多大气问题发生在生产 / 可读绑定 / 消费边界，而不是最终颜色公式。

**这些数据的所有者在哪条线程？** 主要在 Render Thread / RDG 图内。Game Thread 负责把组件参数送进 scene proxy；到本章这段时，Renderer 已经在渲染线程上把每帧 view、scene textures、光照/阴影输入和 RDG 资源组织好了。RDG 负责单帧临时资源的生命周期与同步；view state 负责跨帧历史，例如体积雾的 history 和云的 VRT history。

**为什么 UE 要这样拆？** 因为每种效果的最优计算维度不同。天空散射对很多像素共享，适合 LUT；体积雾需要沿相机深度累计，适合 3D grid；高度雾有指数密度的闭式解，适合全屏解析；云层细节强依赖视线穿过云体的路径，适合 ray marching；局部雾体则既可能参加 froxel 积分，也可能只在屏幕空间局部补一层。如果强行统一成一个全屏 shader，资源依赖会变简单，但性能、质量和调试边界都会变差——你将无法分别为云调步数、为体积雾调网格、为高度雾用闭式解。

## 2. 一帧里有两个窗口：早期资源准备，晚期颜色合成

体积与大气系统不能只按“Lighting 后谁先画”理解，因为 Sky Atmosphere 的资源准备比最终天空合成早得多。主要桌面 deferred 路径可以拆成两个窗口。

### 窗口 A：在几何阶段附近提前准备可复用资源

Sky Atmosphere 先判断本帧是否要重新计算共享 LUT，再准备当前 view 的其余 LUT。默认开启 state versioning 时，透过率和多次散射 LUT 在缓存尚未初始化或输入版本变化时重算；如果关闭 state versioning，则不再依赖版本命中，而是每帧重算这两张共享 LUT。异步计算可用时，LUT 工作可以在 PrePass 前启动；走 graphics/普通 compute 路径时，也会在 BasePass 前启动。这样做的目标不是提前把天空画进 `SceneColor`，而是让昂贵的大气查表资源在后续消费者真正需要它们之前完成，并尽量与几何工作重叠。

```text
Scene / atmosphere lights / view 参数成立
  -> state versioning 开启：缓存未初始化或输入版本变化时重算共享 LUT
  -> state versioning 关闭：每帧重算 Transmittance / Multi-Scattering LUT
  -> 为每个 view 生成 SkyView / Camera Aerial Perspective 等 LUT
  -> 即使共享 LUT 本帧未重算，已注册的外部 LUT 仍完成 external SRV access commit/conversion
  -> 更新 scene/view uniform，建立后续消费者的可读入口
  -> sky、cloud、fog 等消费者之后才能绑定这些资源
```

这一步完成后仍不能说“天空已经画好”。它只证明大气查询数据已经准备到可读边界。把 LUT 准备提前，是 UE 为 GPU overlap 和多消费者复用做出的工程选择；如果把这些工作全部拖到晚期 sky pass，再执行同样的积分，结果可以相同，但会延长 Lighting 后的关键路径，也会让 cloud、fog 等消费者等待同一批计算。

### 窗口 B：Lighting 后生产本帧体积结果并合成颜色

进入这个窗口时，`SceneDepth` 已经能描述 opaque 表面距离，`SceneColor` 已经包含 opaque lighting，光照与阴影系统也准备了体积雾和云可能读取的输入。此时才沿着本章的颜色主线推进：

```text
已光照 SceneColor
  -> Volumetric Fog 生成 IntegratedLightScattering
  -> 可选：Cloud VRT 先 trace 到离屏目标并准备重建
  -> Sky Atmosphere 消费早期 LUT，把天空 / 空气透视写入 SceneColor
  -> Height Fog 读取深度与体积雾积分结果，写回 SceneColor
  -> LFV 按覆盖区间由 froxel + 解析路径协作，或走独立 fallback
  -> Cloud per-pixel 直接合成，或把 VRT 结果 compose 回来
  -> 水、Translucency、PostProcessing 继续消费
```

在主要 deferred、非 ray-traced overlay 路径里，sky、height fog、local fog、cloud 的晚段顺序如上。下面四条分支状态链说明何时不能机械套用这条顺序：

| Condition | Data / consumer 怎样改变 | Last-valid-state 与下一检查点 |
| --- | --- | --- |
| **Desktop deferred 默认路径**：非 forward、无 ray-traced overlay，正常主视图 | 早期 LUT 和体积资源由 late sky、standalone Height Fog/LFV、cloud pass 消费，逐步改写当前 `SceneColor` | 最后成立状态应沿 LUT 可读入口 → sky color → fogged color → cloud-composited color 推进；缺哪一级，就查该级 producer/consumer |
| **Forward shading** | 晚段 standalone `RenderSkyAtmosphere` 与 standalone `RenderFog` 不执行；相关天空/雾贡献必须由 forward/base-pass-compatible 路径或该视图的其他合约承担，而不是等待 deferred late pass | 看不到 late sky/fog RDG 事件本身不是漏画证据；先查 forward 路径的 view/material 输入与 BasePass 后 `SceneColor`，不要从不存在的 standalone pass 倒查 |
| **Underwater / water-behind 目标分支**：Single Layer Water 建立 `SceneWithoutWater`，这里的 underwater 指水面后方场景 | fog 读取 water depth，写 `SceneWithoutWaterTextures.ColorTexture`；该 fog 路径强制把可见 LFV 一起组合，cloud VRT 也可在水面最终合成前写入这份水后背景，随后由 Single Layer Water 消费 | 最后成立状态是“水后颜色已含 fog/LFV/必要 cloud”，不是主 `SceneColor` 已完成；若隔水看不到局部雾，先查水后目标与 LFV 组合，再查 water consumer |
| **Reflection capture view** | sky source disk 关闭，非 reflection 的 alpha propagation 不启用；Height Fog 受 `Visible in Reflection Captures` 条件门控；cloud 不使用需要 view state/history 的 VRT，而走 capture 可用的 per-pixel 路径写 capture 目标 | 没有 VRT history 或太阳圆盘是预期状态；先分别确认 capture sky 输出、fog visibility gate、per-pixel cloud 目标，再检查 capture 最终颜色 |

这些分支改变的是 consumer、目标或功能门控，不会把“资源身份存在”自动升级成“目标颜色已完成”。例如 forward view 仍可能准备可复用的大气数据，却没有 deferred late sky pass；reflection capture 也能有云颜色，却不应期待 VRT 重建链。

这条顺序的关键依赖有四个：

**体积雾积分要先于 Height Fog 消费。** `IntegratedLightScattering` 不存在时，Height Fog 仍可画解析高度雾，但无法还原那段被光照和阴影调制的 froxel 体积结果。

**天空颜色要先成为 Height Fog 的背景。** Height Fog 合成的是“雾自身散射 + 透过雾后还能看到的背景”。如果天空在它之后覆盖颜色，雾对天空和远距背景的作用会丢失。

**云的生产和出现可能分离。** VRT 路径先生产离屏资源，再重建、合成；per-pixel 路径直接在晚段写当前 `SceneColor`。因此“cloud tracing 执行”不能证明云已经进入主画面。

**LFV 要按射线覆盖区间协作。** 被 froxel 覆盖的近段和需要解析补足的远段可以由不同路径共同负责；只有当前 fog pass 没承担 LFV 合成时，独立 tiled pass 才接手。第 6 节会把这个边界讲透。

## 3. Sky Atmosphere：LUT 是从大气模型到场景合成的边界

回到那座被空气染蓝的远山。读者容易这样误解 Sky Atmosphere：既然最终只是天空颜色和远处蒙色，为什么不在 sky pass 里对每个像素直接积分大气？答案是**复用**和**边界**。大气透过率、多次散射、当前 view 的天空颜色、相机前方的空气透视，都不止被一个阶段使用——天空要用，云要用，高度雾也要用。UE 先把这些高复用项折叠成 LUT，再为后续 pass 建立稳定的可读绑定。

可以把 Sky Atmosphere 看成两段职责：早期**准备查询资源**，晚期**消费查询资源并合成颜色**。准备阶段内部还要再分成两类生命周期，因为不是所有 LUT 都是同一种临时资源。

```text
Scene / View 中的大气参数
  │
  ├─ 持久 / 可缓存资源
  │     透过率 LUT、多次散射 LUT：默认按初始/输入版本变化重算；关闭 state versioning 时每帧重算
  │
  ├─ per-view 资源
  │     天空视图 LUT、相机空气透视 LUT：随当前 view 生成
  │
  ├─ 建立可读入口：完成必要资源转换和访问状态，
  │     让 scene/view uniform 指向后续 shader 可绑定的资源
  │
  └─ 第二段：sky raster pass 采 LUT + 深度，
        把天空和空气透视写进 SceneColor
```

**这些 LUT 各是什么（首次出现，逐个点名）。** 它们不是最终图像，而是把高维大气积分投影到少量查询轴上的中间表达：

- **透过率（transmittance）**：回答“光从某个大气高度沿某个方向传播后还剩多少”。它主要由大气本身决定，所以适合跨 view 复用，并在大气参数版本变化时失效。
- **多次散射（multi-scattering）**：把多次散射的高成本递归过程压成可复用近似。近似和分辨率降低了成本，也意味着极端观察条件下不等价于无限次精确积分。
- **天空视图（sky view）**：把当前相机看到的天空方向映射到颜色。它依赖 view referential，因此不能简单作为所有相机共享的静态天空贴图。
- **相机空气透视（camera aerial perspective）**：沿当前相机射线和深度切片组织的 3D LUT。它让远山像素按自身深度取得散射与透过率，而不必在每个像素重新走完整大气积分。

**所有者和生命周期。** 参数来自场景里的 SkyAtmosphere proxy 和 atmosphere lights（清晨场景里的太阳就是其中之一）。Render Thread 把工作声明进 RDG；GPU 执行 LUT 更新。透过率和多次散射使用可缓存的外部资源身份：默认 state versioning 开启时，缓存初始状态或输入版本变化触发重算；state versioning 关闭时，即使输入不变也每帧重算。SkyView 和 Camera AP 更接近当前 view 的单帧结果。共享 LUT 本帧没有重算，也不代表后续交接可以省略：只要外部 LUT 已注册进 RDG，Renderer 仍要完成 external SRV access commit/conversion，以结束可能的异步资源转换，再更新 scene/view uniform 指向。这个“可读入口成立”不等于 Platform Queue Submit，更不等于 GPU Completion；它描述的是图内资源依赖、访问状态和绑定身份已经正确连接。

**输入输出，不要混。** 输入是大气物理参数、view、太阳/天空光、cloud shadow / sky AO 等条件。输出分两类：一类是 **LUT 资源**（准备可采样数据），另一类是 sky pass 写入的 **`SceneColor`**（真正把天空画上去）。准备数据和画天空是两件事，分别在两段生命周期里。

**为什么需要 LUT 这一层。** 如果每个 sky、cloud、fog 像素都完整积分大气，成本会被“消费者数量 × 屏幕像素 × 积分步数”同时放大，而且不同消费者很容易使用略有差异的近似。LUT 把高成本工作集中到受控分辨率，让多个系统共享一致结果。代价是参数量化、插值误差、有限深度范围和版本管理。对于少量离线视角，直接高质量 ray march 可能更简单；对于需要任意相机、实时云雾复用的大型场景，分层 LUT 更符合实时预算。UE 的选择是工程权衡，不是“大气只能这样算”的物理硬约束。

**失败模式（沿生命周期倒查，不要只盯最终 pass）。** 天空发黑、或 sky material 采不到大气时，按这个顺序查：当前 view 属于 desktop deferred、forward、water-behind 还是 reflection capture → state versioning 是否开启 → 开启时缓存初始/输入版本是否要求重算，关闭时本帧重算是否执行 → 外部共享 LUT 即使未写入是否完成 SRV access commit/conversion → per-view LUT 是否生成 → scene/view uniform 是否指向当前资源 → 当前分支真正的颜色 consumer 是否读取它。看到 state versioning 关闭后每帧都有共享 LUT pass，不应误判成缓存持续失效；看到共享 LUT 本帧未重算，也不能跳过 external-access 交接检查。

## 4. Volumetric Fog：低分辨率 3D froxel 先积分，Height Fog 后采样

清晨草地上那层被阳光照亮、被树影切开的薄雾，就是 Volumetric Fog 负责的部分。它的误区是把它想成 Height Fog shader 里临时做的一段 marching。UE 的实际模型更像“先做一张相机体积积分表，再让全屏 fog pass 按深度查表”。

先解释一个贯穿本节的名字：**froxel**（frustum + voxel）是按相机视锥切出来的体素——XY 跟着屏幕分块，Z 沿深度切片。把整个相机前方的雾，离散成这样一格一格的小体积，就是 Volumetric Fog 的工作底盘。

这张“积分表”的 owner 是当前 view 的体积雾资源。它在渲染线程的 RDG 图中生成，结果挂到 view 的 `VolumetricFogResources`；需要时还会把部分历史放进 view state，供下一帧做时间重投影（temporal reprojection，复用上一帧结果来降噪/省算）。

这条 producer 只有在一组明确条件同时成立时才启动：当前 view 的普通 fog 路径允许渲染，Scene 有效，全局 `GVolumetricFog` 开启，ViewFamily 的 VolumetricFog show flag 开启，Scene 中至少存在一个 Exponential Height Fog，而且当前使用的首个 fog component 启用了 Volumetric Fog 并给出大于零的可视距离。任一条件失败，本帧都不会生产这套 view-local froxel integration 资源；Height Fog 仍可沿自己的解析路径工作，因此“屏幕有高度雾”不能替代对 Volumetric Fog producer gate 的检查。

一个 froxel 从“空格子”到“积分结果”的生命周期是：

```text
View + Exponential Height Fog 参数
  -> 建立低分辨率 3D grid（XY 来自屏幕分块，Z 按非线性深度分布）
  -> 写介质属性：scattering（散射）/ extinction（消光）/ emissive（自发光）
  -> 可选：把 Local Fog Volume 体素化进 grid
  -> 注入局部光、阴影光等辅助体积输入
  -> 每个 froxel 计算光照散射
  -> 沿 Z slice 从近到远积分
  -> 输出 IntegratedLightScattering（3D 纹理）
```

这条流程里有两次容易被一句“积分”掩盖的数据换形。

第一次是**局部介质变成局部受光结果**。`scattering`、`extinction` 和 `emissive` 先描述这个小体积怎样散射、吸收和自己发光；局部光、方向光、阴影和天空光再决定这个 froxel 当前能获得多少入射能量。得到的 `LightScattering` 更接近“这一格本地产生什么”，还没有回答相机前方累计了多少空气。

第二次才是**从近到远累计**。设第 `i` 个 slice 贡献的局部散射为 `S_i`，穿过该 slice 后保留的透过率为 `T_i`，前面已经累计的透过率为 `T_accum`，那么概念上每推进一格都在做：

```text
L_accum  <- L_accum + T_accum * S_i
T_accum  <- T_accum * T_i
```

最终 3D 纹理的某个深度位置保存的是“从相机到这里”的累计散射 `L_accum` 和累计透过率 `T_accum`。这就是 `IntegratedLightScattering` 能被 Height Fog 按像素深度直接查表的原因：它已经替消费者完成了前面所有 slice 的组合。

**角色。** Volumetric Fog 负责“空气本身被光照亮并被遮挡”的那部分（草地上的光束和树影）。它**不是最终混合 pass**，它的产物是 `IntegratedLightScattering` 这张 3D 纹理。真正把雾盖到屏幕上的是后面的 Height Fog。

**输入（理解为“一组光照与体积证据”，不必逐个记名字）。** 概念上它需要三类输入：雾的介质参数（来自 Exponential Height Fog 和 local fog）、各类光照与阴影证据（forward light、directional/VSM/ray traced shadow、cloud shadow、SkyLight，以及来自 Lumen、MegaLights 的体积光照输入），以及可选的上一帧 history。这里的重点不是每种光照算法——那些属于各自专题——而是体积雾 pass 需要前面系统已经把这些输入放到可采样边界上。

**输出。** 关键输出是 `IntegratedLightScattering`，语义是“从相机到某个 froxel 深度累计后的、pre-exposed 的散射和透过率”。注意中间还有一张 `LightScattering`，它可以被提取成 history 用于下一帧重投影，但它**不是** Height Fog 直接采样的最终接口——别把这两张混了。

**为什么使用相机对齐 froxel，而不是世界空间统一体素。** 当前帧最终只需要沿当前 view 的射线查询雾，把有限体素集中在视锥内能直接控制屏幕成本；相机附近使用更密的 Z slice，也把精度放在最敏感区域。代价是相机移动会改变格子覆盖，需要 temporal reprojection 稳定结果，并可能产生块状、拖影和视角相关分辨率。世界空间稀疏体素更适合长期复用和大范围一致性，但需要维护稀疏结构、流送和更复杂的采样；逐像素 ray march 质量更直接，却会让相邻像素重复计算相似介质。UE 在这里选择 camera froxel，是为了固定实时预算与屏幕相关质量。

**Z 为什么非线性。** 相机附近对雾层变化最敏感，远处可以更粗。UE 用 grid Z 参数把 slice index 和 view depth 互相映射，让有限的 Z slice 更集中在近处。调试“近处雾块状”或“远处突然截断”时，要看 grid pixel size、grid size Z、深度分布、start distance，以及 shader 侧 Z slice 采样是否互相一致。

**为什么要独立这一阶段，而不是塞进全屏 fog shader。** 如果全屏 fog shader 对每个像素都做体积步进，成本随像素数和步数放大，而且同一个 froxel 的光照会被相邻像素重复计算。先在低分辨率 3D grid 中算一次，能把体积光照、阴影、history 和 local fog 输入集中处理，再让不同像素按深度采样同一张表——这就是“先积分成表，后查表”的收益。

**失败模式。** 有 Height Fog 但体积雾不显示时，先查体积雾是否满足渲染条件、是否生成了 `IntegratedLightScattering`，以及 fog uniform 是否把 `ApplyVolumetricFog` 设为有效。如果 `ApplyVolumetricFog` 是无效状态，问题在体积雾的生产或绑定边界，**不在** Height Fog 的最终混合公式——不要去改混合代码。

## 5. Height Fog：解析高度雾负责把雾真正盖到 SceneColor 上

上一节体积雾生产了 3D 积分结果，但它还没改 `SceneColor`。把雾真正盖到屏幕上的主要消费者，是 Height Fog pass。

Height Fog 的角色**不是“低配体积雾”**，而是另一种数据形态：指数高度密度沿视线有闭式积分，所以它适合用一个全屏 raster pass 按像素深度解析地算出来。它读 `SceneDepth`，算出当前像素从相机到表面的高度雾贡献，再采样 Volumetric Fog 的 3D 结果，把两者组合后写回 `SceneColor`。

单像素的处理可以概括为：

```text
SceneDepth -> 重建相机到像素的射线
  -> CalculateHeightFog：解析指数高度雾
       start/end distance、observer height clamp
       一层或两层 height fog
       directional / cubemap / sky atmosphere 贡献
  -> 如果启用 local fog in height pass，累加 LFV 解析贡献
  -> CombineVolumetricFog：
       按 volume UV 采 IntegratedLightScattering
       去除 pre-exposure
       用 VolumetricFogStartDistance 门控近处
       “体积雾在前、高度雾在后”地组合
  -> 写回 SceneColor
```

**所有者和生命周期。** Height Fog pass 是渲染线程上的一个 RDG raster pass。它的 fog uniform 是本帧 view 的参数包，里面**要么绑定真实的体积雾 3D 纹理，要么绑定一个黑色全透的占位资源**（这样 shader 代码不必分叉）。它直接写主 `SceneColor`，因此它之后的云、局部雾、水和半透明都看到的是已经雾化过的颜色。

**为什么解析近似成立。** 指数高度雾的密度函数沿射线有闭式积分。UE 用一个共享的线积分函数处理 height falloff，并在 falloff 接近 0 时用 Taylor 展开退化来避免数值问题。这让 Height Fog 既不必像云那样逐步 marching，也不必像体积雾那样提前建 3D grid——同样是雾，数据形态不同，算法就不同。

这里要区分物理约束和工程选择。指数密度能闭式积分，是数学条件；把它放进一个全屏 pass，是 UE 利用这个条件做出的工程选择。若介质包含任意三维噪声、局部阴影和复杂光照，闭式高度模型就不够，必须交给 froxel 或 ray marching。反过来，如果只需要平滑的全球高度衰减，把所有内容都塞进 3D volume 会浪费内存、带宽和时间稳定成本。

**为什么要排除体积雾覆盖的距离。** Volumetric Fog 已经负责了“相机到它最大距离”那段体积。Height Fog 在支持体积雾的 permutation 下，会把这段距离从解析高度雾里**排除**，再由 `CombineVolumetricFog` 把体积雾结果放回前景。否则同一段空气会被体积雾和解析雾算两遍，近处雾就会变厚、发灰或过亮。

**组合的先后关系。** `IntegratedLightScattering` 表示近处体积段的散射和透过率；解析 Height Fog 表示排除段之后的雾贡献。所以组合时，远处的解析雾要被近处体积雾的透过率衰减——这就是注释里“体积雾在前、高度雾在后”的含义。调试“远处雾莫名穿过近处浓雾”时，优先查组合顺序、`VolumetricFogStartDistance` 和体积雾的 alpha 语义。

把它写成颜色合成式会更直观。若近段体积雾已经给出散射 `S_near` 和透过率 `T_near`，远段解析高度雾与原背景合成后的颜色是 `L_far`，那么最终结果遵循：

```text
L_out = S_near + T_near * L_far
```

这不是普通 UI alpha blend，而是参与介质的辐亮度/透过率组合。顺序反过来会让远段雾没有被近段介质衰减；同一距离段重复参与两次，则会造成雾过厚、发灰或过亮。

**失败模式。** Height Fog 有效但体积雾不参与——查 fog uniform 的 `ApplyVolumetricFog` 和绑定的 integrated texture。雾“贴脸”出现——查体积雾的 start distance 与 final integration 的软淡入。整体曝光不对——查体积雾采样后是否按 view 的曝光关系把 pre-exposure 还原回了合成空间。

## 6. Local Fog Volume：不是三选一，而是按射线区间协作

清晨场景里如果再加一团贴地局部雾，最容易犯的错误是把三条实现路径理解成互斥开关。真实问题不是“这团雾归哪个 pass”，而是：**沿当前像素射线，这团雾的哪一段已经被 froxel 覆盖，哪一段还需要解析补足，当前 fog pass 是否已经把完整贡献写入颜色。**

它的 owner 分两层。每个 local fog volume 的持久场景数据来自组件 / proxy；本帧渲染时，`InitLocalFogVolumesForViews` 为每个 view 准备排序后的 instance 数据、tile 列表、间接绘制数据和 uniform。Volumetric Fog、Height Fog shader 与独立 tiled pass 读取的是同一份 view-local 描述，只是负责的射线区间和光照模型不同。

LFV 的共同入口先要求 Scene 中存在 Local Fog Volume、Fog show flag 开启、当前 view 不处于 DebugViewPS 路径，并且项目平台支持该功能且总 CVar 允许。共同 gate 通过后再按 consumer 分流：`RenderIntoVolumetricFog` 成立时，LFV 先成为 froxel 介质；`RenderDuringHeightFogPass` 成立时，Height Fog shader 承担相应解析区间；前两条路径都没有完成当前 LFV 的颜色合成时，standalone tiled pass 才接手写入 `SceneColor`。因此三条路径不是同时无条件执行，也不能只凭某个 LFV instance 存在就推断某个具体 producer 已启动。

### 默认协作：近段进入 froxel，剩余区间解析补足

当 Volumetric Fog 有效且允许把 LFV 写进 froxel 时，位于体积雾覆盖范围内的局部介质会参与 `VBuffer`、光照散射和最终 Z 积分。这样近段局部雾能获得与全局体积雾一致的光照、阴影和 temporal history。

但 froxel 有最大覆盖距离。Height Fog / LFV 的解析求值并不会因此完全消失；它会从体积雾覆盖区间之后继续计算，避免近段重复、同时补上远段。这是**区间协作**：

```text
相机
  -> [0, VolumetricFogMaxDistance]
       LFV 作为 froxel 介质，进入 LightScattering / FinalIntegration
  -> [VolumetricFogMaxDistance, 当前表面深度]
       LFV 使用解析积分补足剩余区间
  -> Height Fog pass 把两段按透过率顺序组合进 SceneColor
```

当项目显式选择在 Height Fog pass 中解析 LFV 时，同一个全屏 pass 可以直接读取 tile/instance 数据并计算贡献；当当前 fog pass 没有承担 LFV 合成时，Height Fog 后的独立 tiled raster pass 才作为 fallback 写入 `SceneColor`。Underwater 等特殊路径还会为了水后可见性强制在 fog 组合中处理 LFV。

| 覆盖方式 | 适合的目标 | 代价与限制 |
| --- | --- | --- |
| Froxel 近段 | 需要体积光照、阴影、history，与全局体积雾一致 | 受 froxel 分辨率、最大距离和 temporal artifact 影响 |
| Height Fog 解析段 | 平滑、局部、可按射线闭式求值的剩余区间 | 不会获得与 froxel 完全相同的多光源体积细节 |
| 独立 tiled pass | 前两者没有负责当前 LFV 时的可见 fallback | 额外颜色 pass，且必须维护与前序雾的正确排序 |

**为什么不固定成单一路径。** 全部体素化能统一光照，却受网格范围和分辨率限制；全部独立绘制更直接，却无法自然共享 froxel 的光照、阴影和 history；全部解析成本可控，但不能表达任意复杂的体积光照。UE 选择按区间和能力协作，以获得连续覆盖，同时避免同一段介质重复积分。

**失败模式。** 局部雾过厚时，检查 froxel 覆盖距离之后的解析起点是否正确，而不是只问“是否同时开启两条路径”；局部雾远处突然消失时，检查解析补段或独立 fallback；近处块状或拖影则检查 froxel 分辨率、history 和密度 clamp。独立 pass 不执行并不等于漏画，它可能表示 fog pass 已经完成了完整覆盖。

## 7. Volumetric Cloud：ray marching 的关键是输出语义和合成位置

天上那团蓬松的云，是本章唯一需要真正 ray march 的系统。它在本章只讲一帧接入，不展开云物理百科。核心问题是：云既不是普通半透明 mesh，也不是 Height Fog 的解析公式。云层需要沿视线穿过体积材质，逐步采样密度、光照和透过率，最后输出“云贡献”。

它的生命周期如下：

```text
Cloud component/proxy + volume material
  -> InitVolumetricCloudsForViews：准备 per-view cloud 参数和阴影/AO 输入
  -> RenderVolumetricCloud：决定当前 view 走 VRT 还是 per-pixel
       VRT（Volumetric Render Target，离屏体积渲染目标）：
            写离屏 tracing color/depth，后续 reconstruct 和 compose
       per-pixel：直接以 SceneColor 为 destination
  -> RenderVolumetricCloudsInternal：执行 compute 或 pixel 外壳
       可选 empty-space skipping（跳过空区域省算）
       CloudView CS/PS 调用共同 marching 逻辑
  -> 输出 luminance（云颜色）/ transmittance（透过率）/ cloud depth
  -> 可选 AP-on-cloud、fog-on-cloud、VRT compose
```

**所有者和线程。** 云组件参数通过 scene proxy 进入渲染线程；每帧 view 的 cloud 参数、shadow/AO 输入和 render target 由 Renderer/RDG 组织。VRT 路径的长期状态放在 view state 里，因为它有 history、重建、噪声 pattern 和当前/历史 buffer；per-pixel 路径则主要是本帧 RDG 资源。

**输入（同样按“一组可采样边界”理解）。** 云读取 scene depth 或 min/max depth 来避免穿过 opaque；还读取大气光、cloud shadow、sky/空气透视、height fog、local fog、volumetric fog 和局部光等输入。重点不是每种光照算法，而是云 pass 需要前面这些系统已经把对应输入放到了可采样边界上——又一次印证第 1 节的“先生产、后消费”。

**输出语义（最容易踩坑的地方）。** 云颜色的 alpha **不是普通 coverage，而是 transmittance**（透过率：还能看见多少背景）；云 depth 也**不是主场景 depth**，而是云 tracing 的近/远深度表达，用于重建、边缘修正、AP/fog-on-cloud 和合成。把云当成普通透明贴图、按 alpha 去调，会得到完全错误的结论。

**VRT 与 per-pixel，分别适合什么。** VRT 路径适合把昂贵的 marching 放到较低分辨率、可重建的离屏目标里，再晚些 compose 回 `SceneColor`；它需要 view state，受 VRT 模式、shader 平台、反射捕获和投影条件影响。per-pixel 路径直接写 `SceneColor`，通常发生在 Height Fog 之后，因此它必须在自身 shader 或相邻 pass 里处理好 fog / 空气透视的关系。

这两条路径不是质量等级的简单高低关系。VRT 用更少样本和跨帧历史换取性能，代价是重建边缘、快速运动、薄云和 history 失效时更容易暴露噪声或拖影；per-pixel 避免离屏重建，适合对边缘、遮挡或反射捕获有特殊要求的 view，但成本更直接地随像素和 marching 步数增长。项目选择哪条，应由目标平台、云覆盖面积、相机运动和允许的时域伪影共同决定。

### 两条 cloud path trace

同一朵云在画面里出现，可以来自两条完全不同的资源路径：

| 路径 | 资源旅程 | 如果画面没有云，先查 |
| --- | --- | --- |
| VRT trace -> reconstruct -> compose | 云先写离屏 VRT color / depth；view state 参与 history / 重建；随后把重建后的云合成回 `SceneColor` | VRT 是否有内容、reconstruct 是否执行、compose target 是否是当前主场景、water / underwater 条件是否改了合成目标 |
| per-pixel direct | 云 pass 直接以当前 `SceneColor` 为 destination，在 Height Fog 之后 ray march 并混合 | 当前 view 是否真的选择 per-pixel，scene depth / AP / fog 输入是否可读，shader 是否把 transmittance 当作合成语义而不是普通 alpha |

因此"云 shader 跑了"还不等于"云进入主画面"。VRT 路径要额外确认离屏结果被重建并合成；per-pixel 路径则要确认它在正确的 `SceneColor` 时间点上直接写入，并且使用的是云自己的 transmittance / depth 语义。

**失败模式。** 云穿过物体——先查当前路径是否读取了正确的 scene depth 或 min/max depth。云有 VRT 内容但画面里没有云——查 reconstruct 和 compose 是否执行、compose target 是否因水下或单层水条件而改变。云上没有雾或空气透视——查云 pass uniform 里相关的 fog/AP 开关，以及这些效果是否被推迟到了重建/合成阶段。

## 8. 贯穿案例：一条射线怎样穿过远山、局部雾和云

现在只跟一个像素。它先穿过一团贴地 LFV，再穿过全局薄雾，最后命中远山；远山上方还有一层云。这个像素能把整章的数据轴连成一条线。

| 状态 | 当前具体数据 | Owner / 生命周期 | 下一位消费者 |
| --- | --- | --- | --- |
| 大气准备 | 持久 Transmittance/Multi-Scattering LUT + 当前 view 的 SkyView/AP LUT | Scene 缓存 + 当前 view；由 RDG 更新和建立可读入口 | sky、cloud、fog |
| Opaque lighting 完成 | 远山的 HDR `SceneColor` 与表面 `SceneDepth` | 当前帧 SceneTextures | atmosphere/fog composite |
| Froxel 局部受光 | 每格 scattering/extinction/emissive 与光照结果 | 当前 view RDG 3D textures | Final Integration |
| 积分完成 | 到远山深度为止的 `S_near` 与 `T_near` | `IntegratedLightScattering`，当前帧 | Height Fog |
| Height/LFV 解析 | froxel 最大距离之后的高度雾和 LFV 补段 | Height Fog pass 单帧参数 | 颜色合成 |
| 雾合成 | `L_fogged = S_near + T_near * L_far` | 当前 `SceneColor` 新版本 | cloud compose |
| 云 trace | 云 luminance、transmittance、cloud depth | VRT/view state 或本帧 per-pixel RDG 资源 | reconstruct/compose |
| 云合成 | 云散射 + 云透过率 × 已雾化背景 | 当前 `SceneColor` 新版本 | Translucency / PostProcessing |

这条射线也解释了为什么顺序不能随意交换。若云先合成、后面的 fog 又不知道 cloud depth/transmittance，云和空气透视的关系会错；若 LFV 的 froxel 近段和解析近段重叠，同一段透过率会乘两次；若只生成了 cloud VRT 却没 compose，主场景颜色仍停在“只有天空和雾”的状态。

## 9. 合成顺序与最后成立状态

把前面五个系统放回 desktop deferred 默认 `SceneColor` 路径，late composite 顺序是：

```text
1. SceneDepth / GBuffer / SceneTextures 已可读，SceneColor 已有 opaque lighting。
2. Volumetric Fog 生成 IntegratedLightScattering，但尚未写 SceneColor。
3. 如果当前 view 走云 VRT，云先 trace 到离屏目标，并完成重建准备。
4. Sky Atmosphere 把天空和空气透视写入 SceneColor。
5. Height Fog 读 SceneDepth 与 IntegratedLightScattering，把解析雾和体积雾写入 SceneColor。
6. Local Fog Volume 如果尚未由 froxel 与解析 fog 路径完成当前射线覆盖，再由独立 fallback 写 SceneColor。
7. 云如果走 per-pixel，直接 trace/blend 到 SceneColor。
8. 云如果走 VRT，把重建后的云合成到 SceneColor。
9. Single Layer Water、Translucency、PostProcessing 继续消费。
```

每一步都能用“某个资源还没准备好”来解释它为什么不能提前——这就是本章反复强调的“顺序来自资源依赖，不来自透明度”：

- Sky LUT 没有形成当前 view/scene 的有效可读绑定 → sky/cloud/height fog 采样会黑，或退回占位。
- `IntegratedLightScattering` 没生成 → Height Fog 表现不出体积雾光照。
- Height Fog 没做 → per-pixel 云的雾关系和后续透明层级会错。
- LFV 路径判断错 → 局部雾重复或消失。
- VRT trace 有内容但没 compose → 云只存在于离屏资源，不影响主场景。

由此得到本章最实用的一条调试原则：**按最后成立的资源状态倒查，而不是按可见现象所在的 pass 倒查。** 先确定哪一级证据已经成立，再检查下一位 producer/consumer：

| 最后成立状态 | 能证明什么 | 不能证明什么 | 下一检查点 |
| --- | --- | --- | --- |
| 大气参数进入 Renderer | scene/view 参数存在 | 不证明共享 LUT 的 state-versioning gate 已正确选择 | 初始/输入版本变化，或关闭 versioning 后的逐帧重算 |
| LUT 资源存在 | 有可引用资源身份 | 不证明外部 LUT 已完成本帧 SRV access commit/conversion | external access 与 uniform binding |
| per-view LUT 可读入口成立 | sky/cloud/fog 可以声明读取 | 不证明 sky pass 已写 `SceneColor` | sky composite |
| `LightScattering` produced | froxel 局部介质已受光 | 不证明沿 Z 的累计结果正确 | Final Integration |
| `IntegratedLightScattering` produced | Height Fog 有累计体积输入 | 不证明 Height Fog 已采样或组合 | fog uniform 与 fog pass |
| Fog pass 写入颜色 | sky/height/VF/LFV 的当前组合已进入目标 | 不证明 LFV 覆盖区间无重叠 | LFV coverage ownership |
| Cloud VRT 有内容 | cloud tracing 已产生离屏结果 | 不证明重建、compose 或水下目标正确 | reconstruct/compose target |
| 当前 `SceneColor` 含云 | 13 章颜色主线已成立 | 不证明后续透明、后处理或 GPU 最终完成 | 14/15 消费与完成证据 |

RDG pass 已声明只证明图中存在这项工作；Platform Queue Submit 只证明平台命令已交给 GPU 队列。要判断画面资源是否正确，应检查对应 producer 的 GPU 输出和 consumer 绑定；要安全复用资源，则还需要覆盖最后 GPU consumer 的完成证据。

下表把常见现象映射到第一个该查的资源边界：

| 现象 | 先查什么 |
| --- | --- |
| 天空黑或大气 LUT 黑 | 当前 view 分支、state versioning gate、共享 LUT 重算条件、external SRV access commit、view/scene 绑定与实际 consumer |
| 有 Height Fog 但体积雾不亮 | `ComputeVolumetricFog` 是否生成 integrated texture、fog uniform 是否启用体积雾 |
| 体积雾亮度或阴影不对 | LightScattering 阶段的 forward light、shadow、SkyLight、Lumen/MegaLights、cloud shadow 输入 |
| 近处雾块状或远处截断 | grid pixel size、grid size Z、深度分布、start/max distance、Z slice 映射 |
| 局部雾重复 | 当前 LFV 是否同时进入 volumetric fog、height fog 和独立 pass |
| 云穿过 opaque | 当前云路径的 scene depth / min-max depth 输入，以及 cloud depth 输出语义 |
| 云 VRT 有内容但画面没有云 | reconstruct、compose、view state、water/underwater compose 条件 |

## 10. 本章小结

回到那个清晨场景。本章先在几何阶段附近准备可复用的大气资源，再在 Lighting 后把 `SceneColor` 扩展成包含天空、空气透视、全局高度雾、体积雾、局部雾和体积云的主场景颜色：Sky Atmosphere 的持久 LUT 默认在缓存初始化或输入版本变化时重算，关闭 state versioning 后改为每帧重算；无论本帧是否重算，已注册的外部 LUT 都要完成 SRV access commit/conversion，per-view LUT 再随视图生成并建立可读入口。Desktop deferred 使用 late sky/fog/cloud 主线；forward、water-behind 与 reflection capture 则改变 standalone pass、目标或功能 gate。Volumetric Fog 在低分辨率 froxel grid 里先得到局部受光介质，再沿 Z 累计成 `IntegratedLightScattering`；Height Fog 用解析积分形成远段结果，并用散射/透过率公式组合近段体积雾；Local Fog Volume 由 froxel 近段、解析补段和独立 fallback 协作覆盖；Volumetric Cloud 则按当前 view 支持的 VRT 或 per-pixel 路径输出 luminance、transmittance 和 cloud depth，最后交给该分支的颜色 consumer。

如果只记一句话：**UE 的体积与大气阶段不是一个天气 pass，而是“早期准备共享资源、晚期生产体积结果、按散射与透过率合成颜色”的资源系统；正确顺序来自数据依赖和覆盖区间，不来自视觉上“谁更透明”。** 出错时先问最后成立的是参数、资源身份、可读入口、局部散射、累计积分还是最终颜色，再进入对应 pass。

下一章 `14_Translucency.md` 会从已经完成天空、雾和云合成的 `SceneColor` 接手，讨论半透明排序、lighting volume、distortion、front layer 和半透明材质如何继续接入主场景颜色。
