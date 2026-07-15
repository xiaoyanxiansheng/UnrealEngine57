# 09_DepthPrepass Coverage Matrix

> 本矩阵依据两份同等候选素材重新建立：原版附件 `pasted-text.txt`（637 个物理行）与最终正文 `09_DepthPrepass.md`（464 个物理行）。UE5.7 本地源码用于事实裁决；原版与当前稿均不被默认视为正确版本。矩阵只记录最终正文的教学落点，不修改正文状态或公共状态文件。

## 1. 最终教学模型

**核心误区：** PrePass 被安排执行，不等于 BasePass 前的 early depth 已达到目标完整度，也不等于目标消费者已经取得正确阶段、正确版本的可读深度证据。

```text
08 交付候选与对象数据窗口
→ ShouldRenderPrePass 决定 ordinary PrePass 是否存在
→ EDepthDrawingMode 定义覆盖策略
→ Traditional / Velocity / Nanite producer 分担写入
→ command formation / culling / recording / platform execution
→ SceneDepth.Target 获得阶段性内容
→ PartialDepth / Resolve 建立相应可读表达
→ HZB 生成 current closest / furthest 证据
→ current furthest extraction 为 previous history
→ consumer 选择正确证据并产生 GPU 内结果
→ last-valid-state 定位最后成立状态
```

**责任轴：** Renderer policy 决定策略；具体 producer 承担写入；SceneTextures/RDG 组织阶段资源与依赖；ViewState 持有跨帧历史；consumer 选择证据并产生后续结果。

**数据轴：** CPU primitive evidence → pass-local candidate → pass command / culling range → Target content → Partial/Resolve → HZB → visible instance buffer / indirect args 等 consumer output。

**生命周期轴：** 帧内候选与命令、当前 attachment 写入窗口、阶段性可读窗口、current HZB、跨帧 extraction、下一帧 previous furthest validity 分属不同生命周期。

## 2. 双素材逐教学单元账本

| 教学单元 | 原版独有价值 | 当前稿/最终模型价值 | 事实裁决与最终处理 | 最终正文落点 |
|---|---|---|---|---|
| 核心误区 | 用“PrePass 已运行但对象仍未进入 early depth、Target 有值但 consumer 未读到、遮挡使用历史证据”等反直觉现象建立问题 | 收束为“执行不等于完整、写入不等于可消费” | 融合；保留当前主句，并保留多阶段证据冲突 | 开篇与状态链 |
| Unity 迁移边界 | `_CameraDepthTexture` / DepthOnly 心智不能直接等价 UE 多 producer、多阶段深度 | 当前稿恢复短边栏，不扩展成 API 对照 | 保留最小迁移价值；避免机械一一对应 | 开篇 Unity 边栏 |
| 08→09 入口 | 详细解释 CPU primitive visibility、pass relevance、静态/动态 DepthPass 输入、GPUScene 窗口 | 当前稿压缩成入口合同并解释 CPU coarse / GPU fine 分层 | 融合；不重复 08 算法，但保留两级可见性不是二选一 | “入口状态” |
| PrePass 存在性 | `ShouldRenderPrePass()` 与 drawing mode 是不同判断 | 当前稿建立三层判断 | 保留并强化：pass existence → coverage policy → object acceptance | `EDepthDrawingMode` 前置说明 |
| `EDepthDrawingMode` | 完整枚举及各项含义 | 改为策略矩阵，纠正线性等级误读 | 采用当前模型；`DDM_MaskedOnly` 是分支，`DDM_None` 不代表无最终 SceneDepth | 策略表 |
| Producer 责任 | Traditional、Velocity、Nanite 分散描述 | 统一 responsibility matrix，并补 BasePass fallback 边界 | 融合；写入目标共享，生产路径和成立条件不同 | Producer matrix |
| Traditional processor | `FDepthPassMeshProcessor`、position-only/material path、masked/WPO 条件 | 用候选→拒绝→路径→command 教学 | 保留技术意义，删除源码走读载体；补入 PDO 与最终轮廓一致性原则 | Traditional producer |
| Command 到 Target 控制链 | command build、parallel pass、dispatch/submit draw 的实现含义 | 最终稿补回 command→culling→recording→RHI→platform→GPU raster→Target | 严重遗漏已补回；严格区分 formation、recording、Queue Submit、GPU raster | Traditional producer 控制链、证据梯 |
| Position-only | 为什么便宜，以及 masked/WPO 不能无条件走该路径 | 用“必须匹配最终可见表面”统一条件 | 融合；保留原理，不恢复长分支列表 | Traditional producer |
| Velocity 补深度 | `DDM_AllOpaqueNoVelocity` 下 velocity 可完成 early depth write | 当前稿补齐 pass 安排、对象接受、状态和时序条件 | 保留当前条件模型，拒绝“所有 movable opaque 必然由 velocity 写” | Velocity producer |
| Nanite depth producer | Nanite 独立 raster/culling，最终贡献主场景 depth | 当前稿强调共享最终深度合同、不共享传统 MeshPass | 融合；不把 Nanite 中间 raster 结果等同普通 depth command | Nanite producer |
| `SceneDepth.Target` | attachment 写入角色；Target 截图有值不证明 consumer 读到同版本 | 当前稿避免“Target 绝对不可读”的平台断言 | 保留资源角色与证据边界 | Target 小节、调试梯 |
| `PartialDepth` | 阶段性第一阶段证据 | 当前稿明确 first-stage resource 存在时使用，否则回退主 SceneDepth | 使用当前事实修正；不把它写成必然独立纹理 | PartialDepth 小节 |
| Resolve | 写入表达与 shader-readable 表达之间的边界 | 当前稿改为逻辑可读边界，不承诺固定物理复制 | 使用当前事实修正 | Resolve 小节 |
| Device-Z / reversed-Z | 解释数值方向可能反直觉 | 当前稿恢复最小首次教学 | 保留；调试优先使用 closest/furthest 语义，不机械记 min/max | HZB 7.1 |
| HZB 金字塔原理 | mip 覆盖更大屏幕区域、bounds 选层、保守遮挡判断 | 最终稿补回远处路牌案例 | 严重遗漏已补回；宁可多画，不可错剔 | HZB 7.1、街道案例 |
| Closest / furthest | 两类层级语义与消费者用途 | 当前稿纠正固定双输出印象 | HZB 可按需求生成两者或仅 furthest | HZB 7.2 |
| Current / previous | 当前与历史资源、camera cut、ViewState | 当前稿纠正 previous closest/furthest 虚假对称，并补 extraction 生命周期 | 通用历史主线为 previous furthest；closest 是当前可选证据 | HZB 7.3–7.4 |
| 最小 UE 锚点 | `View.HZB`、history 等真实定位 | 当前稿恢复克制锚点 | 保留定位，不让符号承担主教学 | HZB 生命周期、章节出口 |
| Nanite two-pass | main/post、阶段中更新遮挡证据、GPU 内复测 | 当前稿改成“已有证据→增加证据→复测不确定候选” | 纠正固定 `main=previous/post=current final` 公式 | Nanite two-pass |
| GPU 内闭环 | HZB 不应成为当前帧 CPU 主答案；readback 会同步 | 当前稿连接 visible buffers / indirect args 和后续 GPU 消费 | 融合；调试 readback 仅为例外 | Instance Culling 小节 |
| Instance Culling 数据轴 | GPUScene + range + HZB → visible instance buffer → indirect args | 最终稿恢复条件化完整链 | 严重遗漏已补回；不是所有路径无条件使用 HZB | 9.1 |
| Screen-space consumer | 半透明/屏幕空间路径可能读取 resolved depth 或 closest HZB | 当前稿只保留一个条件化例子 | 合理压缩系统名单，保留消费语义对照 | 9.2 |
| Opaque BasePass 边界 | BasePass 消费 depth/stencil 合同，不等于普遍采样 HZB | 当前稿将具体策略交给 10 | 保留 `BasePassDepthStencilAccess` 最小出口锚点，不提前教授 BasePass shading | 消费者表、章节出口 |
| Worked case | 多个局部案例：masked 植被、车辆、Nanite、远处对象 | 当前稿统一为一条街道的四对象 | 使用当前贯穿结构；四对象覆盖 producer 与 HZB consumer | 贯穿案例与状态表 |
| 调试模型 | checklist、症状表、Target 有值反例 | 当前稿统一为 last-valid-state ladder | 合并合理；补入 culling range、recording、consumer binding / RDG dependency | 证据梯与症状映射 |
| 章节出口 | 总结深度系统 | 当前稿只交付第 10 章需要的可判断深度合同 | 使用当前边界 | 最后一节 |

## 3. 严重遗漏补回审计

| 原审计阻断项 | 补回后的教学任务 | 最终状态 |
|---|---|---|
| DepthPass command 到实际写入的控制断层 | 明确 candidate、processor command、culling range、recording、RHI translate/platform formation、Queue Submit、GPU raster 与 Target 内容是不同完成深度 | pass |
| HZB mip、bounds 与保守判断原理缺失 | 解释屏幕覆盖、mip 选择、furthest 保守证据、证据不足时保留对象，并由远处路牌贯穿 | pass |
| Instance Culling GPU 数据轴缺失 | 建立 GPUScene/range/optional HZB → GPU culling → visible instance buffer → indirect args → later draw | pass |
| 源码克制过度、缺少定位锚点 | 恢复 `FDepthPassMeshProcessor`、`ParallelMeshDrawCommandPass`、`View.HZB`、ViewState history、`BasePassDepthStencilAccess` 等最小锚点 | pass |
| Evidence ladder 在 command/consumer 处断层 | 新增 culling range、command recording、consumer binding/RDG dependency 与最后 consumer completion | pass |

## 4. 合理压缩审计

以下原版内容没有逐字恢复，但技术意义已保留或因边界/事实原因合理缩减：

- 合并重复的开篇、阅读地图、框架问题和长篇小结，以唯一状态成立链替代同义宣言。
- 压缩第 08 章的 task graph、GDME 和 pass setup 复述，只保留 09 的入口合同。
- 将 `BuildRenderingCommands`、dispatch/submit 等源码调用链转译为 command formation、culling、recording、platform execution 和 GPU raster 的控制深度。
- 合并 Traditional producer 与 processor 的重复说明，但保留 position-only/material path 的成立条件。
- 删除宽泛消费者名单，只保留 occlusion、Nanite、条件化 Instance Culling 和一个 screen-space 对照。
- 用单一 evidence ladder 替代多套 checklist 和重复症状表。
- 不恢复把 primed/current/previous 固化为普遍公式的叙述，只保留阶段中增加 occluder evidence 的技术含义。
- 不把 Target/Resolve 教成所有平台固定发生一次物理复制。

## 5. 原版风险表述与事实修正

| 原版风险或过度统一 | 最终事实模型 |
|---|---|
| Resolve 容易被理解为固定物理复制 | Resolve 是逻辑 shader-readable 边界，底层行为受平台与资源配置影响 |
| 每次 HZB 固定同时生成 closest 和 furthest | 输出按消费者请求建立，某些路径仅生成 furthest |
| previous closest 与 previous furthest 对称 | 通用历史主线是 previous furthest；current closest 可选 |
| Nanite main 固定读 previous、post 固定读 current final | main 使用当前阶段已有保守证据，阶段中增加证据，post 复测不确定候选 |
| 所有 Instance Culling 都使用 HZB | HZB 是条件化输入，取决于配置、阶段和具体路径 |
| `DDM_None` 表示本帧没有 SceneDepth | 仅表示 ordinary early mesh producer 不运行；其他 producer 仍可能建立最终 depth |
| `EDepthDrawingMode` 是线性完整度阶梯 | 它是覆盖策略集合，`DDM_MaskedOnly` 是独立分支 |
| Target 绝对不可被读取 | Target 的主角色是 attachment 写入/测试；实际读取能力取决于资源表达、绑定和平台条件 |
| Velocity 必然为所有 movable opaque 写 depth | 必须满足 pass 安排、对象接受、状态、输出与时序条件 |
| Processor command 已形成即可证明 depth 已写 | command formation、recording、Queue Submit、GPU raster 与 Target 内容逐级分离 |

## 6. 贯穿案例覆盖

| 对象 | 主要教学任务 | 状态变化 |
|---|---|---|
| Masked 栅栏 | material depth path 与 alpha cutout | candidate → material-depth command → Target 轮廓 → HZB 遮挡边界 |
| Movable 金属车 | `AllOpaqueNoVelocity` 下的写入责任转移 | PrePass 不写 → velocity 条件成立 → 补写主 SceneDepth |
| Nanite 建筑 | 非传统 command producer 与 two-pass 证据更新 | Nanite culling/raster → 主 depth → HZB → uncertain cluster 复测 |
| 远处路牌 | HZB consumer、bounds、mip、history 与保守性 | bounds 选择层级 → current/previous evidence → 保留或剔除 → visible output |

案例贯穿输入、策略、producer、Target、Partial/Resolve、HZB/history、consumer output 和调试，不是局部装饰。

## 7. Last-Valid-State 证据覆盖

最终证据梯区分：

1. CPU primitive candidate；
2. DepthPass pass-local input；
3. Processor command；
4. culling 后可执行 range；
5. command recording；
6. producer / platform work 已安排；
7. `SceneDepth.Target` 内容；
8. `PartialDepth` / Resolve produced；
9. current HZB produced；
10. consumer binding 与 RDG dependency；
11. history extraction / validity；
12. GPU consumer output；
13. 覆盖最后 GPU consumer 的 completion evidence。

核心反例保持成立：Target 截图中已有对象，只能证明该时刻 attachment 含有深度，不能证明目标 consumer 绑定了同阶段的 Resolve、PartialDepth 或 HZB，也不能证明最后 GPU consumer 已完成。

## 8. Owner / Data / Control / Lifetime

| 轴 | 覆盖 |
|---|---|
| Owner | Renderer/scene policy、Traditional/Velocity/Nanite producer、SceneTextures/RDG、ViewState、consumer 各自责任明确 |
| Data | candidate、command、range、Target、Partial/Resolve、HZB、history、visible buffer、indirect args 的换形完整 |
| Control | pass existence、coverage strategy、object acceptance、recording、platform execution、RDG dependency、history validity 逐级分离 |
| Lifetime | 帧内 command、当前写入窗口、阶段可读窗口、current HZB、cross-frame extraction、previous history 分离 |

## 9. 源码克制与章节边界

- 正文没有源码路径、源码行号、验证日志或编辑过程语言。
- UE 符号只作为定位与调试锚点，均被翻译为状态、数据、责任、控制和证据含义。
- 06/08 只作为对象数据和 Frame Init 输入合同，不重新教授其内部机制。
- 第 10 章只接收 depth completeness、资源阶段与 depth/stencil 判断条件；09 不提前展开 material getter、GBuffer ABI、DBuffer 或 surface publication。
- Nanite 只讲本章所需 depth/HZB 交接，不展开完整 cluster culling/raster 算法。

## 10. 637→464 缩减专项审计

- 原版附件：**637 个物理行**。
- 最终正文：**464 个物理行**。
- 减少：**173 行**。
- 缩减比例：`173 / 637 ≈ 27.2%`，超过 15% 异常阈值，因此已执行逐教学单元信息价值审计。

缩减来源判定：

| 来源 | 判断 |
|---|---|
| 重复开篇、地图、问题清单、小结 | 合理合并 |
| 对第 08 章输入机制的重复展开 | 合理压缩，入口合同仍保留 |
| 密集源码调用链 | 转译为控制权和完成深度，技术意义保留 |
| Target/Resolve 重复说明 | 合理合并，并完成事实修正 |
| HZB 与消费者长名单 | 名单合理压缩；核心 HZB 原理、Instance Culling 数据轴和 screen-space 对照已补回 |
| 多套调试 checklist | 合并为单一证据梯，并补齐原断层 |
| Unity 迁移与最小 UE 锚点 | 已定点补回，不恢复长篇 API 对照 |

结论：剩余 27.2% 缩减主要来自重复载体、越界展开、源码清单和可被统一模型替代的同义内容；原版仍有教学价值的原理、条件、例外、案例和调试判断均有最终落点。

## 11. 物理行统计方法

- 使用 `.NET` 的 `File.ReadAllLines(path).Length` 统计物理行。
- 该方法正确处理当前 UTF-8 文件的末尾换行，不采用“换行符数量无条件加一”。
- 若使用分隔符算法，只在文件非空且末尾不是受支持换行分隔符时加 1。
- 不使用 PowerShell `Get-Content(...).Count` 作为混合 Unicode 换行文件的唯一统计依据。
- 行数只用于异常预警，不作为质量通过证明。

## 12. 最终验收矩阵

| 维度 | 结果 |
|---|---|
| 真实双素材信息价值 | pass |
| UE5.7 事实 | pass |
| 单一正向主线 | pass |
| 概念首次教学 | pass |
| Owner / Data / Control / Lifetime | pass |
| 条件与边界 | pass |
| Worked case | pass |
| Last-valid-state debug | pass |
| 源码克制 | pass |
| 08→09→10 跨章一致性 | pass |
| 637→464 缩减专项审计 | pass |
| 06 教学标定 | reached |

**剩余阻断项：无。**
