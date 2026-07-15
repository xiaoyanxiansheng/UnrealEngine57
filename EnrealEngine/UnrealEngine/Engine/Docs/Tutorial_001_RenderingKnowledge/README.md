# UE5.7 渲染百科全书

`Tutorial_001_RenderingKnowledge` 是 UE5.7 渲染系统的百科主库。目标不是压缩学习面，而是把现有教程沉淀成可检索、可扩写、可源码复核的知识库。

`Tutorial_002_RenderingDeep` 保留为教学化深读视角。001 可以吸收 002 中已审过的事实，但不会因为 002 存在而省略对应知识点。

---

## 入口

| 入口 | 用途 |
|------|------|
| [TOPIC_INDEX.md](./TOPIC_INDEX.md) | 按稳定 `topic-*` slug 查找知识点、主位置、002 关联和当前状态 |
| [SOURCE_INDEX.md](./SOURCE_INDEX.md) | 从 UE 源码路径反查 001 知识点 |
| [EXPANSION_LOG.md](./EXPANSION_LOG.md) | 追加式记录待扩写观点、缺口和来源 |
| [CPP_Knowledge_Index.md](./CPP_Knowledge_Index.md) | C++ 概念复用索引 |
| [Deep_Generation_Checklist.md](./Deep_Generation_Checklist.md) | 百科条目扩写与终审流程 |
| [Tools/Check-Tutorial001.ps1](./Tools/Check-Tutorial001.ps1) | 链接、slug、速查卡、源码路径一致性检查 |

---

## 目录结构

```text
Tutorial_001_RenderingKnowledge/
├── TOPIC_INDEX.md              # 知识点主索引，唯一维护条目状态
├── SOURCE_INDEX.md             # 源码反查索引，只收录已验证存在的路径
├── EXPANSION_LOG.md            # append-only 扩展日志
├── Deep_Generation_Checklist.md
├── CPP_Knowledge_Index.md
├── Phase01_Fundamentals/       # 渲染循环、线程模型、RHI、初始化
├── Phase02_Pipeline/           # 数据流、BasePass、GBuffer、Lighting、RDG
├── Phase03_UE5Features/        # Nanite、Lumen、GPUScene、MegaLights
├── Phase04_Advanced/           # MeshDrawCommand、材质、阴影
├── Phase05_Practice/           # CVar、Nanite/Lumen实践、材质调试、优化
├── Phase06_Extended/           # 后处理、自定义Shader、ComputeShader等扩展主题
├── Reference/                  # 模块、核心概念、Unity对照
└── Tools/                      # 校验脚本
```

---

## 阅读路径

入门仍建议按 Phase 顺序阅读：

1. Phase01：建立 UE 渲染主循环、线程和 RHI 心智模型。
2. Phase02：理解每帧数据如何进入管线，以及 BasePass、GBuffer、Lighting、RDG 如何协作。
3. Phase03：进入 UE5 核心特性，重点是 Nanite、Lumen、GPUScene、MegaLights。
4. Phase04：补齐 DrawCommand、材质编译和阴影系统这些进阶机制。
5. Phase05：学习调试、抓帧、CVar、性能排查和实践问题定位。
6. Phase06：按需要扩展到后处理、自定义 Shader、Compute Shader 等专题。

需要按问题检索时，不从目录猜文件名，优先查 [TOPIC_INDEX.md](./TOPIC_INDEX.md)。

---

## 维护规则

- 每个主知识点必须有稳定 HTML 锚点，slug 统一使用 `topic-xxx` 命名。
- `TOPIC_INDEX.md` 只链接稳定 `topic-*` slug，不链接容易漂移的小节编号。
- 条目状态只维护在 `TOPIC_INDEX.md`，不在 README、源码索引、日志中复制。
- `SOURCE_INDEX.md` 只收录在 `Engine/Source` 或 `Engine/Shaders` 中真实存在的路径。
- `EXPANSION_LOG.md` 只追加，不回填状态。
- 扩写默认追加内容和交叉链接，不重编号、不改 slug；确实需要重构时先更新索引并跑校验。
- Claude 可做百科编辑：标题、顺序、可检索性、重复归位；不得删知识点。
- Codex 终审负责源码事实、CVar、线程/RDG/RHI 生命周期和性能断言复核。

---

## 本地验收

每次修改 001 后运行：

```powershell
powershell -ExecutionPolicy Bypass -File Engine\Docs\Tutorial_001_RenderingKnowledge\Tools\Check-Tutorial001.ps1
```

校验脚本负责检查：

- Markdown 本地链接存在。
- `#topic-*` 链接可解析，且每个 `topic-*` slug 在目标文件中唯一。
- 速查卡标题编号与文件名前缀一致。
- 速查卡的 Deep 链接指向存在文件。
- 每个 Deep 文档至少被一个 `TOPIC_INDEX.md` 条目引用。
- `SOURCE_INDEX.md` 中源码路径存在于 `Engine/Source` 或 `Engine/Shaders`。

当前状态以脚本输出为准，不再用 README 中的手工状态表判断“最新”。
