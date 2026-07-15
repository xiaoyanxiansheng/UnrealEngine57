# Tutorial_001 内容审计快照

**审计日期**：2026-06-14  
**性质**：本文件是快照，不是状态源；不维护 `indexed` / `needs_expansion` / `verified`。活状态只在 [TOPIC_INDEX.md](./TOPIC_INDEX.md) 维护。  
**行号债务**：来自 `Tools/Check-Tutorial001.ps1` 的 `LineDebtReport` 口径，扫描正文和代码块，旧债不阻塞结构校验。  
**终审要求**：Codex 终审需要复核 P0/P1/P2 分类是否合理，避免审计误判带偏后续工作。

| 文档 | 主 slug | 行号债务数 | 源码风险 | 结构缺口 | 重复/归位建议 | 002 关联 | 优先级 | 下一步动作 |
|------|---------|------------|----------|----------|----------------|----------|--------|------------|
| `Phase01_Fundamentals/1.1_RenderLoop_Deep.md` | `topic-render-loop` | 4 | 渲染入口调用链需用强符号复核 | 需补“失败时反查路径” | 与 Architecture 的主循环叙述保持交叉链接 | `01_Architecture` | P1 | 审计调用链事实后补调试路径 |
| `Phase01_Fundamentals/1.2_ThreadModel_Deep.md` | `topic-thread-model` | 3 | ENQUEUE/Fence/RHIThread 生命周期高风险 | 需补线程退化路径和常见误区 | 与 002 ThreadModel 对齐术语 | `03_ThreadModel` | P0 | 第二批优先修订 |
| `Phase01_Fundamentals/1.3_RHI_Deep.md` | `topic-rhi` | 0 | RHI 命令/资源生命周期高风险 | 需补 PSO、Barrier、后端边界检索点 | 与 002 RHI 对齐强源码锚点 | `04_RHI` | P0 | 第三批优先修订 |
| `Phase01_Fundamentals/1.4_Initialization_Deep.md` | `topic-engine-initialization` | 4 | 启动阶段符号和模块加载顺序需复核 | 需补与 FrameInit 的边界 | 初始化与每帧初始化分开归位 | `08_FrameInit` | P1 | 去行号后补启动/帧初始化对照 |
| `Phase02_Pipeline/2.1_DataFlow_Deep.md` | `topic-gt-rt-dataflow` | 0 | SceneProxy/Dirty/GPUScene 边界需复核 | 需补对象生命周期图 | SceneProxy 完整生命周期归到 002 交叉引用 | `02_SceneProxy` | P1 | 补 SceneProxy 与 GPUScene 分界 |
| `Phase02_Pipeline/2.2_BasePass_Deep.md` | `topic-base-pass` | 4 | BasePass/MeshPassProcessor 路径高风险 | 需补 DepthPrepass 与 BasePass 边界 | GBuffer 细节归到 `topic-gbuffer` | `10_BasePass` | P1 | 修强符号和行号债务 |
| `Phase02_Pipeline/2.3_GBuffer_Deep.md` | `topic-gbuffer` | 0 | GBuffer 格式/编码可能随版本变动 | 需补 shader 侧强锚点 | BasePass 只保留摘要并指回本条目 | `10_BasePass` | P1 | 复核 GBuffer 格式和 shader token |
| `Phase02_Pipeline/2.4_Lighting_Deep.md` | `topic-clustered-deferred-lighting` | 0 | 光照路径、MegaLights/VSM 交互需复核 | 需补直接光/间接光/阴影边界 | MegaLights 独立归到 `topic-megalights` | `12_Lighting` | P1 | 补光照主路径源码锚点 |
| `Phase02_Pipeline/2.5_RenderGraph_Deep.md` | `topic-render-graph` | 3 | RDG 生命周期、Barrier、Extract 高风险 | 需拆子 topic 并细化源码入口 | 子知识点挂到 `topic-rdg-*` | `05_RenderGraph` | P0 | 作为第一篇试点修订 |
| `Phase03_UE5Features/3.1_Nanite_Deep.md` | `topic-nanite` | 0 | Nanite 管线和版本特性高风险 | 需补 Cluster/Raster/Material 分层索引 | 与实践章只保留交叉链接 | `16_Nanite` | P1 | 补 Nanite 子 topic 候选 |
| `Phase03_UE5Features/3.2_Lumen_Deep.md` | `topic-lumen` | 0 | Lumen 缓存/追踪后端高风险 | 需补 Surface Cache、Screen Probe 子条目 | 与实践章区分原理和调参 | `17_Lumen` | P1 | 复核 Lumen 源码事实 |
| `Phase03_UE5Features/3.3_GPUScene_Deep.md` | `topic-gpuscene` | 8 | GPUScene 上传、实例数据和动态 primitive 高风险 | 需补数据布局和调试路径 | DataFlow 中只保留入口摘要 | `06_GPUScene` | P0 | 第四批优先修订 |
| `Phase03_UE5Features/3.4_MegaLights_Deep.md` | `topic-megalights` | 0 | MegaLights 版本和性能结论高风险 | 需补与 Lighting/VSM 的边界 | Lighting 章节只保留交叉摘要 | `18_MegaLights` | P1 | 复核版本结论和源码符号 |
| `Phase04_Advanced/4.1_MeshDrawCommand_Deep.md` | `topic-mesh-draw-command` | 24 | DrawCommand、PSO、Instancing 高风险 | 需去行号并补提交路径 | BasePass/MeshPassProcessor 重复归位 | `07_MeshDrawCommand` | P0 | 第五批优先修订 |
| `Phase04_Advanced/4.2_MaterialSystem_Deep.md` | `topic-material-system` | 3 | 材质编译、ShaderMap、Permutation 高风险 | 需拆 MaterialPipeline / ShaderSystem 子 topic | 与自定义 Shader 区分资产材质和 GlobalShader | `20_MaterialPipeline`, `21_ShaderSystem` | P0 | 第六批优先修订 |
| `Phase04_Advanced/4.3_Shadow_Deep.md` | `topic-shadow-system` | 5 | CSM/VSM/Contact Shadow 路径易混 | 需补 VSM 独立入口 | VSM 深化应独立子条目 | `11_Shadows`, `19_VirtualShadowMaps` | P1 | 复核阴影路径和 VSM 边界 |
| `Phase05_Practice/5.1_ConsoleCommands_Deep.md` | `topic-console-commands` | 60 | CVar、Shadow Copy、线程安全高风险 | 需去行号并补实测条件 | Debugging 只做工具视角交叉 | `23_Debugging` | P0 | 行号迁移迷你试点候选 |
| `Phase05_Practice/5.2_Practice_NaniteLumen_Deep.md` | `topic-nanite-lumen-practice` | 0 | 实践结论需绑定硬件/项目条件 | 需补实验前提和风险 | 原理回链 Nanite/Lumen 主条目 | `16_Nanite`, `17_Lumen` | P1 | 补实验条件和调参边界 |
| `Phase05_Practice/5.3_Practice_MaterialDebug_Deep.md` | `topic-material-debug` | 0 | 调试路径和可视化命令需复核 | 需补问题到源码的反查表 | 材质系统保留原理，本章保留诊断 | `20_MaterialPipeline`, `23_Debugging` | P1 | 补材质调试反查路径 |
| `Phase05_Practice/5.4_Optimization_Deep.md` | `topic-rendering-optimization` | 2 | 性能断言、CVar 实验和瓶颈分类高风险 | 需补实测条件和单变量实验模板 | 子系统优化回链各主条目 | `24_Optimization` | P0 | 第七批优先修订 |
| `Phase06_Extended/6.1_PostProcess_Deep.md` | `topic-post-process` | 101 | 后处理链行号债务重，Pass 顺序易漂移 | 需去行号并补强符号 | 与 Translucency/Atmosphere 后续拆分 | `15_PostProcessing` | P0 | 行号迁移迷你试点首选 |
| `Phase06_Extended/6.2_CustomShader_Deep.md` | `topic-custom-shader` | 40 | Shader 宏、编译环境和参数绑定高风险 | 需去行号并拆 ShaderType/参数/RDG 调度 | 与 MaterialSystem 区分 GlobalShader 路径 | `21_ShaderSystem` | P0 | 后续按 Shader 子 topic 拆分 |
| `Phase06_Extended/6.3_ComputeShader_Deep.md` | `topic-compute-shader` | 1 | Dispatch、UAV/SRV、Async Compute 结论需复核 | 需补 RDG/线程组/间接调度边界 | 与 RDG 和 ShaderSystem 强交叉 | `22_ComputeShader` | P1 | 复核 Compute 与 RDG 调度事实 |
