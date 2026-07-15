# 09_DepthPrepass Teaching Edit Report

## 1. 本轮任务与素材边界

本报告依据最终正文重新生成，不继承旧报告的素材判断或完成结论。

本章使用两份同等候选素材：

1. 原版附件 `C:\Users\User\.codex\attachments\ef486eb4-ecb5-429f-9757-e0741095925d\pasted-text.txt`，637 个物理行；
2. 最终正文 `09_DepthPrepass.md`，464 个物理行。

UE5.7 本地源码是事实裁决依据。旧 CoverageMatrix 与 TeachingEditReport 只作为历史记录，不作为正确性前提。旧报告中“Origin 不适用”的结论已全部删除。

本轮不修改正文、章节状态、`OUTLINE.md`、`SOURCE_INDEX.md`、`GENERATION_GUIDE.md` 或其他公共文件。

## 2. 最终结构

最终正文采用唯一状态成立链：

```text
08 输入合同
→ PrePass existence / coverage strategy / object acceptance
→ Traditional / Velocity / Nanite producer
→ command / culling / recording / platform / GPU raster
→ SceneDepth.Target
→ PartialDepth / Resolve
→ current closest / furthest HZB
→ previous furthest history
→ conditional consumers and GPU outputs
→ last-valid-state debugging
```

结构对应四条承重轴：

- **责任轴：** policy、producer、SceneTextures/RDG、ViewState、consumer 各自拥有不同阶段。
- **数据轴：** candidate、command、range、depth content、readable expression、hierarchical evidence、GPU output 逐次换形。
- **控制轴：** pass existence、策略、接受、recording、Queue Submit、GPU raster、dependency 和 consumer binding 不被合并。
- **生命周期轴：** current write、stage-readable、current HZB、history extraction、next-frame previous validity 分离。

## 3. 双素材融合结果

### 3.1 保留并强化的当前稿结构

- 用一条状态链替代原版多条并行叙事。
- 将 `EDepthDrawingMode` 表达为策略集合，而非线性等级。
- 使用 Traditional、Velocity、Nanite producer responsibility matrix。
- 精确描述 `PartialDepth` 的 first-stage 与 fallback。
- 把 Resolve 定义为逻辑可读边界，不承诺固定物理复制。
- 修正 HZB closest/furthest 的条件输出和 previous furthest 历史主线。
- 将 Nanite two-pass 表达为证据更新与不确定候选复测。
- 用街道四对象统一 producer 与 consumer 案例。
- 用 last-valid-state ladder 统一调试。

### 3.2 从原版补回的独有教学价值

- Unity DepthOnly / `_CameraDepthTexture` 到 UE 多阶段深度合同的最小迁移边界。
- CPU primitive coarse visibility 与 GPU fine culling 分层协作的原因。
- `ShouldRenderPrePass`、`EDepthDrawingMode` 和 processor/producer acceptance 的三层判断。
- DepthPass candidate 到 Target 实际写入之间完整的控制链。
- Device-Z / reversed-Z 的最小首次教学。
- HZB mip、screen bounds 和保守遮挡判断的核心推理。
- `View.HZB`、ViewState history、`BasePassDepthStencilAccess` 等最小定位锚点。
- 条件化 Instance Culling GPU 数据轴及不回读 CPU 的原因。
- 一个条件化 screen-space consumer 对照。
- command recording、consumer binding 与 RDG dependency 两类调试证据。
- 远处路牌作为 HZB consumer 的 worked-case 子对象。

### 3.3 没有原样恢复但技术意义已转译的内容

- 原版密集函数调用链被转译成 command formation、culling、recording、platform command formation、Queue Submit、GPU raster 与 completion depth。
- 多套调试 checklist 被统一为证据梯；独有反例和症状仍有落点。
- 宽泛消费者系统名单被压缩为消费类型和少数条件化实例。
- 重复开篇、阅读地图、框架问题和小结被统一主线取代。
- 对第 08 章的重复说明被压缩为入口合同。

## 4. 严重遗漏补回记录

### 4.1 Command 到 Target 的控制链

旧优化稿曾从 Processor command 直接跳到 producer/Target，缺少控制权和完成深度。最终正文现在明确：

```text
candidate
→ FDepthPassMeshProcessor
→ pass-specific command
→ culling range
→ ParallelMeshDrawCommandPass / recording
→ RHI translate / platform command formation
→ Queue Submit
→ GPU raster
→ SceneDepth.Target content
```

这恢复了原版源码走读的技术意义，并与第 03、04、07 章的完成模型一致。

### 4.2 HZB 原理

旧优化稿曾只保留 closest/furthest 定义，缺少金字塔为什么有效。最终正文补回：

- mip 覆盖与屏幕 bounds 的关系；
- furthest HZB 的保守遮挡语义；
- reversed-Z 下避免机械 min/max 记忆；
- 证据不足时保留对象；
- 宁可多画、不可错剔的判断原则。

### 4.3 Instance Culling GPU 数据轴

最终正文建立条件化数据链：

```text
GPUScene identity/payload
+ command instance range
+ view parameters
+ optional HZB
→ GPU culling
→ visible instance buffer
→ indirect args
→ later GPU draw consumption
```

它解释了 CPU primitive evidence 与 GPU instance evidence 的差异，以及 GPU 结果为何不以同帧 CPU readback 为主线。

### 4.4 最小 UE 锚点与调试断层

最终正文恢复了足够但克制的 UE 定位点，并在证据梯中增加：

- culling 后可执行范围；
- command recording；
- consumer binding 与 RDG dependency；
- 覆盖最后 GPU consumer 的 completion evidence。

## 5. 事实修正

相对原版，最终正文保留以下 UE5.7 事实修正：

1. Resolve 是逻辑 shader-readable 边界，不保证所有平台发生独立整纹理复制。
2. HZB 可按需求生成 closest、furthest 或仅 furthest，并非固定双输出。
3. 通用跨帧历史主线是 previous furthest，不建立 previous closest 的虚假对称。
4. Nanite two-pass 不固化为 `main=previous`、`post=current final`；它是阶段性证据增加和不确定候选复测。
5. HZB 不是所有 Instance Culling 路径的无条件输入。
6. `DDM_None` 只关闭 ordinary early mesh producer，不代表最终 SceneDepth 不存在。
7. `EDepthDrawingMode` 不是线性完整度阶梯。
8. `SceneDepth.Target` 不被绝对描述为所有平台都不可读；实际能力由资源表达、绑定、依赖和平台条件决定。
9. Velocity 只有在 pass、对象、状态、输出和时序条件成立时承担补深度责任。
10. `PartialDepth` 可能来自 first-stage buffer，也可能回退主 SceneDepth。
11. Processor command、recording、Queue Submit、GPU raster、Target 内容和最后 consumer completion 是不同完成深度。

## 6. Worked Case

最终正文使用同一街道的四类对象：

- **Masked 栅栏：** 验证 material depth path、alpha cutout 和最终轮廓一致性。
- **Movable 金属车：** 验证 `AllOpaqueNoVelocity` 下的 producer 责任转移。
- **Nanite 建筑：** 验证独立 raster/depth producer 和 two-pass 证据更新。
- **远处路牌：** 验证 bounds、mip、current/previous HZB、保守判断和 consumer output。

案例贯穿入口、策略、command、写入、可读、HZB、history、consumer 和调试，达到 06 标定所要求的真实状态变化深度。

## 7. 调试模型

最终调试模型按最后成立状态推进，避免从“Target 有值”直接推导“消费者正确”：

1. CPU candidate；
2. pass-local input；
3. processor command；
4. culling range；
5. recording；
6. platform/GPU producer work；
7. Target content；
8. Partial/Resolve；
9. current HZB；
10. consumer binding / RDG dependency；
11. history extraction and validity；
12. GPU consumer output；
13. last-consumer completion evidence。

每级都区分“能证明什么”和“不能证明什么”。

## 8. 637→464 缩减专项审计

### 8.1 数量

- 原版：637 行。
- 最终正文：464 行。
- 减少：173 行。
- 缩减比例：约 27.2%。
- 超过 15% 预警阈值，因此执行完整逐教学单元审计。

### 8.2 合理减少

- 重复开篇、阅读地图、问题列表和长篇小结。
- 对第 08 章输入机制的重复教授。
- 源码调用链、函数清单和可由控制模型承载的实现载体。
- Target/Resolve 的重复解释。
- 未经逐路径展开的消费者长名单。
- 重复 checklist、症状表和同义调试问题。
- 容易形成固定公式的 primed/current/previous 叙述。

### 8.3 已补回的非合理减少

- command→recording→GPU raster→Target 控制链；
- HZB mip/bounds/保守判断原理；
- Instance Culling GPU 数据轴；
- device-Z / reversed-Z 首次教学；
- Unity 迁移边界；
- CPU coarse / GPU fine 分层原因；
- 最小 UE 锚点；
- screen-space consumer 对照；
- GPU readback 非主线原因；
- command recording 与 consumer binding 证据层。

### 8.4 审计结论

剩余缩减来自重复载体、源码式展开、跨章复述和可由统一模型替代的内容。未发现原版仍有教学价值的承重原理、条件、例外、案例或调试判断只存在于原版、在最终正文中没有落点。

行数减少本身不构成质量证明；本结论来自逐教学单元落点检查。

## 9. 物理行统计方法

- 统计工具：`.NET File.ReadAllLines(path).Length`。
- 原版附件统计结果：637。
- 最终正文统计结果：464。
- 该方法正确处理文件末尾换行；不使用“换行分隔符数量无条件加一”。
- 如果使用分隔符计数，只在文件非空且末尾不是受支持换行分隔符时加 1。
- 不使用 PowerShell `Get-Content(...).Count` 作为混合 Unicode 换行场景的唯一依据。

## 10. 源码克制与边界

- 正文没有源码路径、源码行号、验证日志或编辑过程语言。
- UE 符号只作为真实定位和调试入口，不形成源码走读主线。
- 第 06、08 章的对象数据与 Frame Init 只作为前置合同。
- 第 10 章只接收 depth completeness、resource stage 和 depth/stencil access 判断条件。
- Nanite 只展开本章所需 depth/HZB 交接，不教授完整 Nanite 算法。

## 11. 剩余问题

- 无事实阻断项。
- 无教学结构阻断项。
- 无双素材信息价值阻断项。
- 若未来更换引擎版本，应优先复核 velocity depth 条件、`PartialDepth` 分支、HZB 输出请求、ViewState history validity 和 Nanite two-pass 证据来源。

## 12. 最终验收矩阵

| 维度 | 结果 |
|---|---|
| 双素材信息价值 | pass |
| UE5.7 事实 | pass |
| 单一主线 | pass |
| 概念首次教学 | pass |
| Owner / Data / Control / Lifetime | pass |
| 条件与边界 | pass |
| Worked case | pass |
| Last-valid-state debug | pass |
| 源码克制 | pass |
| 跨章一致性 | pass |
| 637→464 缩减专项审计 | pass |
| 06 教学标定 | reached |

**最终结论：09 正文及两份 sidecar 的内容模型一致，真实双素材信息价值审计通过，无剩余阻断项。**
