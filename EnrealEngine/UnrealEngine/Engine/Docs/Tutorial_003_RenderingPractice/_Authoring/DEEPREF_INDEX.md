# RenderingPractice DeepRef Index（可选延伸）

本文件只维护可选原理延伸。正式课程不要求每篇关联 Deep。引用时包含文件名、章节标题、小节标题和可搜索符号。

| Key | 文件 | 章节 | 小节 | 搜索锚点 |
|---|---|---|---|---|
| `render-frame-skeleton` | `01_Architecture.md` | 01 渲染架构总览 | Render() 是一帧依赖骨架 | `FDeferredShadingSceneRenderer::Render` |
| `basepass-contract` | `10_BasePass.md` | 10 BasePass 与 GBuffer | 8. BasePass 通过 SceneTextures 发布 GBuffer | `BasePass`、`SceneTextures` |
| `deferred-lighting-input` | `12_Lighting.md` | 12 Lighting 与延迟直接光照 | 1. Lighting 的输入合约：接住的是“屏幕”，不是“场景” | `DeferredLightPixelMain` |
| `postprocess-color-flow` | `15_PostProcessing.md` | 15 PostProcessing：SceneColor 如何成为最终输出 | 心智模型：一条不断被重写的颜色流 | `AddPostProcessingPasses` |
| `gpu-scope-evidence` | `23_Debugging.md` | 23 调试工具与方法 | GPU Visualizer 回答“GPU scope 如何组织” | `ProfileGPU` |
| `timeline-classification` | `24_Optimization.md` | 24 Optimization | 1. 先分清瓶颈属于哪条时间线 | `stat unit` |
| `single-variable-experiment` | `24_Optimization.md` | 24 Optimization | 14.4 单变量实验 | `ProfileGPU` |

## 历史原型引用

归档文件 `_Archive/01_FrameAndTools_ConceptPrototype.md` 使用：

- `render-frame-skeleton`
- `basepass-contract`
- `deferred-lighting-input`
- `postprocess-color-flow`
- `gpu-scope-evidence`
- `timeline-classification`

正式 00–02 仅在正文末尾提供少量可选延伸，不维护强制映射。
