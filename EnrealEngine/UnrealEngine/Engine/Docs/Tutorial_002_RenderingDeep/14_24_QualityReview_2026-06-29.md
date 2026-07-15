# 14-24 教学质量复审报告

> **复审日期**: 2026-06-29  
> **复审类型**: 教学质量复审 + 正文教学补强，不是最终事实回归  
> **参考标准**: `03_ThreadModel.md` 2026-06-28 质量完成口径  
> **状态边界**: 本报告不改变章节完成状态，不更新 `OUTLINE.md` 状态列，不向 `SOURCE_INDEX.md` 沉淀新事实，不刷新 CoverageMatrix 或 TeachingEditReport

---

## 总结

本轮覆盖 `14_Translucency.md` 至 `24_Optimization.md` 共 11 篇。

结论：11 篇正文均已按 03-level 教学标准完成质量补强。补强重点不是重新判定完成状态，而是把 2026-06-25 Gate 3 已完成稿进一步补成更稳定的教学形态：首次概念先解释、抽象机制用 worked case 承载、资源和状态变化有 owner / consumer 边界、调试问题能落到具体证据链。

本轮没有修改：

- `OUTLINE.md`
- `SOURCE_INDEX.md`
- `GENERATION_GUIDE.md`
- 各章 `*_CoverageMatrix.md`
- 各章 `*_TeachingEditReport.md`
- 各章完成状态行

原因：质量复审不等同于最终事实回归；当前 14-24 已是完成态，本轮只做教学质量补强。状态维护和事实沉淀需要另行按 Gate 3 或显式状态维护任务执行。

---

## 本轮写入清单

| 章节 | 修改文件 | 本轮补强重点 |
| --- | --- | --- |
| 14 | `14_Translucency.md` | pass 队列、排序域、ResourceMap、OIT、TLV、Distortion、FrontLayer、RT 输出的概念护照与资源状态案例 |
| 15 | `15_PostProcessing.md` | resolved HDR SceneColor、TSR history、Bloom、Tonemap、Debug/Visualize、ViewFamilyTexture 输出边界 |
| 16 | `16_Nanite.md` | cluster / hierarchy node / page、instance vs cluster culling、two-pass HZB、HW/SW raster、shade binning 与跨章边界 |
| 17 | `17_Lumen.md` | Surface Cache page/table/atlas、feedback 跨帧闭环、Screen Probe ray、Radiance Cache、temporal reuse 与 composite 边界 |
| 18 | `18_MegaLights.md` | candidate / sample / ray / reservoir 护照、固定槽位 worked case、VSM mark/trace、history 与 denoise 边界 |
| 19 | `19_VirtualShadowMaps.md` | virtual page / physical page / page table / flags / cache metadata、clipmap/local light 地址、page marking 与 invalidation |
| 20 | `20_MaterialPipeline.md` | 材质四类对象换手、ShaderMapId/DDC 状态流、runtime 参数更新、BasePass 消费端查表案例 |
| 21 | `21_ShaderSystem.md` | BasePass shader 类型护照、VF 轴 lookup、参数 owner 分层、MeshDrawCommand 产物检查表 |
| 22 | `22_ComputeShader.md` | `FViewDepthCopyCS` global shader 护照、参数双重身份、AddPass 到 GPU 写入证据链、thread group 与 async compute 边界 |
| 23 | `23_Debugging.md` | GPU scope / RenderDoc resource / RDG validation / Insights / CVar 五类证据链，debug CVar 记录模板 |
| 24 | `24_Optimization.md` | stat unit 到 RDG/CPU scope 的证据链、draw/pass 成本、BasePass/overdraw 成本交换、实验后优化决策与瓶颈转移检查 |

---

## 横向发现

### 1. 14-24 的共同短板已经从“事实覆盖”转向“概念承载”

这些章节在 2026-06-25 已通过最终事实回归，事实覆盖和主线基本可靠。本轮按 03 标准看，主要薄弱点是：部分 dense 概念仍容易靠定义、表格或流程图支撑，读者未必能回答“这个对象现在装着什么、谁拥有它、下一步谁消费、出了问题先查哪里”。

本轮补强统一采用“资源护照 / 概念护照 + 局部 worked case”的方式，把抽象术语落到具体状态变化上。

### 2. Part 4/5/6 更需要 owner/consumer 边界

`16-19` 的专题技术不是一帧顺序的线性片段，而是各自有独立数据结构和缓存闭环。补强时重点避免把 Nanite、Lumen、MegaLights、VSM 混成“某个 pass 的分支”，而是明确每套系统自己拥有的数据、跨帧 history、与其他系统的握手边界。

`20-22` 的材质 / shader / compute 章节则重点区分：编译身份、运行时参数、VertexFactory 轴、RDG 参数、dispatch 是否实际写 output。

`23-24` 的调试和优化章节则把工具清单改成证据链，避免“看到一个 CVar / scope 就直接下结论”。

### 3. 本轮不需要公共状态维护

本轮没有发现需要回退或推进完成状态的 blocker。章节头和 `OUTLINE.md` 仍保持完成态是合理的；`SOURCE_INDEX.md` 不应因为质量补强而追加临时教学案例或调试建议。

---

## 分章记录

### 14 Translucency

已补“新词放稳”层，覆盖 pass 队列、排序域、ResourceMap、OIT、TLV、Distortion、FrontLayer、RT 输出。新增多个局部案例：玻璃拆分到不同路线、AfterDOF 烟雾写 ResourceMap、OIT triangles/pixels 分层、Distortion 三段状态、FrontLayer normal/depth 证据链。调试重心从“有没有画”转为“结果停在哪个资源护照上，下一位消费者是谁”。

### 15 PostProcessing

已补后处理链的资源护照：resolved HDR SceneColor、当前 SceneColor、ViewFamilyOutput / ViewFamilyTexture、TSR/TAA history。新增最终输出权 worked case，区分 Tonemap、AA/Upscale、Visualizer / callback。补强 TSR 输入输出和 history、Bloom 多级 additive、CombineLUT/Tonemap/ACES、Debug/Visualize 作为链上节点，以及 ViewFamilyTexture 到章外 present 的边界。

### 16 Nanite

已补 cluster / hierarchy node / page 首用解释与岩壁案例。澄清 instance culling 与 cluster culling 的 owner / debug 边界。补强 two-pass HZB 的状态换手、HW/SW raster 的执行路径差异、ShadingMask / ShadeBinning / BasePass compute 的材质解析案例，并新增与 DepthPrepass、BasePass、VSM、Lumen Card Capture 的边界小节。

### 17 Lumen

已补 Surface Cache card/page/atlas 的 worked case，说明红墙走近时 page table、fallback、物理页、sub-allocation、GPU uniform 的变化。补强 `FLumenSceneData` 与 RDG 单帧资源的 owner 换手、feedback 跨帧闭环、Screen Probe 单条 ray 的三层命中状态、Radiance Cache mark/indirection/probe state、temporal reuse、diffuse GI / reflections / SceneColor composite 边界。

### 18 MegaLights

已补 candidate / light sample / sample ray / reservoir 概念护照。新增“15 盏候选灯变成 4 个槽位”案例，绑定候选枚举、固定槽位、shadow evidence、resolve / denoise 的状态线。补强 VSM sample 的 mark vs trace 两阶段，visible light hash、lighting history、spatial denoise 写回 SceneColor 的边界和排查顺序。

### 19 VirtualShadowMaps

已补 virtual page、physical page、page table entry、flags、cache metadata 首用解释。补强 virtual page 到 physical page 的 worked case，包括 fallback 标志与 LOD offset。澄清 single-page VSM 是 reserved page-table 入口，不是共享同一物理页。强化 directional clipmap / local light / point light face 地址差异，以及 page marking、allocation、Nanite/non-Nanite rendering、lighting lookup、cache invalidation 的 owner 边界。

### 20 MaterialPipeline

已补四个局部案例：材质四类对象换手、ShaderMapId/DDC miss 状态流、runtime Roughness 参数更新、BasePass 消费端查表。核心目的：让读者区分“改代码结构需要重建 shader map”和“改普通参数只更新 uniform 数据”，并知道 BasePass 只是消费已有 shader，不现场翻译节点图。

### 21 ShaderSystem

已补 `TBasePassVS` / `TBasePassPS` 的类型护照，明确它们为什么同时依赖 material 与 VertexFactory。新增同一材质换 VF 后 lookup 坐标变化的案例。补强参数 owner 分层，把 material uniform、primitive/GPUScene、LocalVF 顶点流、pass/RDG 资源拆清。新增 MeshDrawCommand 产物检查表，帮助判断问题落在 shader refs、VF binding、batched parameters 还是 PSO / draw 参数。

### 22 ComputeShader

已补 `FViewDepthCopyCS` 的 global shader identity 护照，讲清注册、参数结构、平台过滤、`THREADGROUP_SIZE`、`SF_Compute` 的职责。补强 `FParameters` 的双重身份，以及 `AddPass` 到真正 GPU 写 output 的证据链。强调断点在 `AddPass` 处只能证明声明发生，不能证明 output 已写。补强 thread group / group count、SRV/UAV transition、UAV barrier、async compute fork/join 和 output 未写入的四段排查链。

### 23 Debugging

已补“证据链写法”，把黑屏案例拆成 GPU scope、RenderDoc resource、RDG validation、Insights、CVar 五类证据，并逐项映射到引擎状态和下一步排查。新增 GPU stat / Visualizer 翻译规则，说明 scope 是否存在、是 draw 还是 dispatch、是否为 hang 前最后可信范围。补强 CVar 记录模板，要求区分正常形态、实验形态、可得结论和下一步。

### 24 Optimization

已补优化证据链模型：`stat unit -> Insights/ProfileGPU -> RDG/CPU scope -> 内容变量 -> 单变量实验`。新增 draw/pass 提交成本 worked case，避免把 Render Thread pass setup 直接误读成 draw call 多。补强 BasePass / overdraw 案例，强调 PrePass、BasePass、HZB、VSM、velocity 的成本交换。新增实验后的优化决策流和“是否转移瓶颈”检查。

---

## 后续维护建议

1. 不把本轮质量补强写入完成状态依据。14-24 已完成，本轮只是教学质量增强。
2. 如果后续要刷新 CoverageMatrix，可增加 `case coverage` / `03-level verdict` 字段，但那应作为单独维护任务处理。
3. `SOURCE_INDEX.md` 只沉淀可复用事实和源码锚点，不沉淀本轮新增的教学案例、调试模板或质量判断。
4. 后续如继续按 03 标准推进，可优先把 02、04-13 也逐章应用正文补强，而不仅保留质量复审建议。
