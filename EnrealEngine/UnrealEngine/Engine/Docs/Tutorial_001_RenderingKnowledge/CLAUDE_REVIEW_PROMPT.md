# Claude 审核稿：Tutorial_001 百科化正文复审

你是 Claude，负责对 `Tutorial_001_RenderingKnowledge` 做教学流程和百科文章质量审核。请只输出审核意见，不要直接修改仓库文件。所有实际改动由 Codex 落地，Codex 负责最终源码事实复核。

## 背景

`Tutorial_001_RenderingKnowledge` 的定位是 UE5.7 渲染百科主库，不是精简教程。目标是知识点越来越完整、可检索、可扩展、源码可复核。`Tutorial_002_RenderingDeep` 保留为教学化深读视角，只作为交叉参考，不作为 001 缺省不覆盖的理由。

本轮 Codex 已完成百科化正文修订，并已根据上一轮 Claude 终审意见做过回归修复：

- 23 篇 `*_Deep.md` 全部补齐“百科条目卡”。
- `Tools/Check-Tutorial001.ps1` 的 `LineDebtReport` 已修复检测盲区：现在会识别“源码文件名后接冒号和数字”的固定源码行号格式，包括 C++ 头/源文件和 shader 文件。
- 固定源码行号债务已按修复后的检测口径清零，源码定位改为“路径 + 强搜索 token / 符号”。
- `TOPIC_INDEX.md` 继续作为唯一活状态源。
- `SOURCE_INDEX.md` 只维护脚本可验证的源码路径和 token。
- `CONTENT_AUDIT.md` 保持历史快照，不作为任务队列或第二状态源。
- 条目卡 `相关条目` 已统一使用 `#topic-*` 深链。
- 28 张条目卡的 `性能与误区` 已统一为 `性能特征：... 误区：...` 口径。
- `Reference/CompareWithUnity.md` 与 `Reference/CoreConcepts.md` 已补性能数据免责声明。
- 已补轻量结构归位说明：BasePass/MeshDrawCommand/GBuffer、Lighting/MegaLights/Shadow、MaterialSystem/CustomShader/PostProcess/ComputeShader 的主条目边界已明确。
- 全局脚本 `Tools/Check-Tutorial001.ps1` 已通过，`LineDebtReport` 为 `none`。

上一轮 Claude 已指出并由 Codex 落地的项目包括：

- 修正 `LineDebtReport` 对 `File.cpp:NNNN` 固定行号格式的漏检。
- 清理 MegaLights、Nanite、Lumen、RenderLoop、RDG、Shadow、MaterialDebug、Optimization 等正文和速查卡中的固定行号。
- 修正 `4.2_MaterialSystem_Deep.md` 的 `性能与误区` 字段口径。
- 给 `4.1`、`4.2`、`6.1`、`6.2` 条目卡的相关条目补 `#topic-*` 深链。
- 给 Reference 文档补性能数据免责声明，并软化部分裸百分比 / ms / MB 示例口径。
- 软化少量绝对化措辞，例如“必然选择”“最优路径”“官方推荐”等。

## 审核范围

重点审核：

- `Engine/Docs/Tutorial_001_RenderingKnowledge/**/*.md`
- `Engine/Docs/Tutorial_001_RenderingKnowledge/TOPIC_INDEX.md`
- `Engine/Docs/Tutorial_001_RenderingKnowledge/SOURCE_INDEX.md`

不要修改：

- `Engine/Docs/Tutorial_002_RenderingDeep/**`
- 任何源码文件
- 任何索引或正文文件

## 审核重点

请重点判断以下问题：

1. 教学流程是否仍存在“先用后教”的理解断层。
2. 百科条目卡是否真正帮助检索，而不是机械重复。
3. 8 字段是否一致：一句话结论、UE架构位置、流程、源码锚点、调试/验证、性能与误区、Unity对照、相关条目。
4. “性能与误区”是否都包含明确的“性能特征：... 误区：...”口径；硬性能数字是否都标注为示意或需实测。
5. 固定源码行号是否真的没有残留；如果发现，应优先指出具体文件和格式。
6. 源码锚点是否只在卡片、源码索引或必要正文位置中表达为“路径 + 强 token”，避免重复堆叠或弱 token。
7. 重复内容是否已经按“主条目详解 + 其他位置摘要交叉链接”归位；不要建议删除知识面。
8. 绝对化表述是否仍存在，例如“零成本”“必然”“最优”“彻底消除”“官方推荐”等；如果是在否定式或限定式中出现，请不要误报。
9. 目录和标题是否足够可检索，尤其是读者会搜索的英文术语：RDG、External、Extract、GPUScene、VSM、Lumen、Nanite、CVar、ShaderMap、GBuffer 等。
10. 是否有条目卡引用的“相关条目”与正文讲解顺序冲突。
11. `001` 和 `002` 的关系是否清晰：001 是百科主库，002 是教学深读补充。

## 输出格式

请按下面格式输出，不要泛泛评价。

### 总体判断

用 3-6 句话判断这轮百科化是否能作为后续文章模板。

### 必须修改

列出会影响模板复制、教学理解、事实可信度或百科检索性的硬问题。每条都要包含：

- 位置
- 问题
- 修改建议
- 原因

### 建议修改

列出不阻塞但能显著提升质量的问题。每条同样给出位置和建议。

### 结构归位建议

指出哪些知识点应该指定主条目，哪些位置只保留摘要和交叉链接。要求“不删知识点，只归位”。

### 条目卡一致性

检查 8 字段模板是否一致，指出字段缺失、顺序不一致、源码锚点重复、性能误区口径不一致等问题。

### 给 Codex 的落地清单

用可执行 checklist 输出，让 Codex 可以逐条 patch。不要要求 Claude 自己修改仓库。

## 审核边界

- 不要建议压缩文章长度。
- 不要建议降低知识面。
- 不要把 `002` 的存在当成 `001` 可以不覆盖某知识点的理由。
- 不要新增第二状态系统。
- 不要建议恢复固定源码行号。
- 对源码事实不确定时，标注“需 Codex 源码复核”，不要写成确定结论。
