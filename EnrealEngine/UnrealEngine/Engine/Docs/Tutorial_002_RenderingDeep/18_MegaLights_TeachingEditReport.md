# 18 MegaLights Teaching Edit Report（按最终正文重建）

## 材料与独立依据

- 最终正文：`18_MegaLights.md`，669 行，SHA256 `E10A2D493DEBC8411DFAC232C67068A73379C2E6FF161029367B205666E92A3D`。
- 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/18_MegaLights.md`，602 行，SHA256 `245553204930859321C35E5FD07E94747174FD296A3502E22130BA26BADB2494`。
- 指定审计/复核：`.codex/tmp/audit_18_19.md`（274 行）、`.codex/tmp/review_18_19.md`（39 行），均完整读取成功。
- 事实复核：重新打开 UE5.7 的 `LightRendering.cpp`、`MegaLights.cpp`、`MegaLightsRayTracing.cpp`、MegaLights sampling/volume shaders、resolve/denoise 实现。
- 旧 sidecar 只证明曾存在历史文本，不参与本报告的结构、迁移、事实或 BODY 判断。未使用任何章节作为质量标杆，也未读取第 06 章作标杆。

## 最终正文的真实结构

最终正文不是源码调用顺序，而是九段教学主线：

1. 从“贵操作逐灯重复”建立问题，并限定固定的是 sample-domain 后端上界。
2. 用工厂多灯案例建立一盏灯从 owner 到最终贡献的贯穿对象。
3. 先讲 sorted-light ownership，再拆五层资格门和更晚的 `DirectLighting` 工作门。
4. 区分 candidate domain、sample domain、frame-local context 与 cross-frame history owner。
5. 用 15->4 worked case讲 sampler/reservoir、权重和两套 history。
6. 把 VSM/screen/world/material retrace 组织成 completed/compaction 驱动的 shadow-evidence 状态机。
7. 讲 resolve、temporal/spatial denoise、`SceneColor` 写回及四层完成深度。
8. 收束 classic deferred、VSM、Lumen、Volume、Hair 的责任与支持矩阵。
9. 用 last-valid-state 表把 ownership -> budget -> sample -> trace -> resolve -> history 反向用于调试。

## 原版信息迁移

| 冻结原版教学价值 | 最终处理 | 最终落点 |
| --- | --- | --- |
| 多灯成本不是列表长度，而是贵操作逐灯重复 | 保留并加上 candidate/sample 成本边界 | 开篇、第 2 节、第 9 节 |
| `MegaLightsLightStart` 切开 classic 与 MegaLights owner | 保留主线，资格门从三层重写为五层，并拆出 `DirectLighting` 工作门 | 第 1 节、第 8.1 节 |
| 15 个候选压到 4 slots，不是 top-4 | 保留案例，修正 sampler 临时数据与 finalize weight 语义 | 第 3 节 worked case |
| visible-light history 与 lighting history 分离 | 保留，并补 owner/lifetime 和 history invalidation | 第 3、6 节 |
| sample-driven VSM marking 与 lookup | 保留，接入统一 completed/compact 状态机 | 第 4 节 |
| RT/screen/software 并列后端 | 重写为 VSM -> screen -> world -> retrace 的条件顺序 | 第 4 节“有序状态机” |
| resolve/denoise 写回 `SceneColor` | 保留，新增 RDG/GPU/UAV/history extraction 完成梯度 | 第 5、6 节 |
| Volume/Hair 只是换采样域 | 作为错误抽象拒绝；迁移其“共享预算思想”后重建支持矩阵 | 第 7 节 Volume/Hair 子节 |
| 按流程倒查问题 | 保留并升级为 owner 边界的 last-valid-state 表 | 第 8 节 |

最终正文比冻结原版增加 67 个物理行；diff 为 142 insertions / 75 deletions。没有触发“缩短超过 15%”异常，但仍完成逐单元迁移审计：原版独有的问题定义、工厂案例、ownership、15->4、history、VSM handshake、resolve/denoise 与调试价值均有明确落点；被删除的是错误、过度同构或不完整的表达，不是其可保留技术意义。

## O-D-C-L 教学闭环

| 核心对象 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| 灯光路由 | Renderer sorted-light routing / MegaLights | sorted ranges、`MegaLightsLightStart` | platform/request/tracing/per-light gates | 每帧分类 |
| 采样工作台 | `FMegaLightsViewContext` | downsampled inputs、samples、rays、tile lists、resolve targets | downsample、slot 数、active work | 当前 view 的当前 RDG graph |
| 抽样账本 | shader `FLightSampler` | packed selected candidates、random thresholds、`WeightSum` | candidate weighting、replacement、finalize | 单次 shader invocation |
| 阴影证据 | MegaLights sample state；VSM/screen/world 各自推进 | `bCompleted`、`bVisible`、`RayDistance`、compact lists | shadow method、后端可用性、material retrace mode | 本帧 sample trace |
| 跨帧历史 | `FMegaLightsViewState` | lighting history、visible-light hash history | camera cut、reprojection/rejection、extraction/import | 跨帧 external resources |
| 输出 | opaque denoiser、Volume/Hair 专用 consumer | `SceneColor` 或专用 lighting target | input type 与 filter support | 当前帧输出；history 另行提取 |

## 设计推理与替代方案

- 固定 slots 的目标是给 selected-sample trace/shade 建立上界；若所有候选都做 shadow/shading，成本重新随候选数放大。灯少、追求确定性逐灯调试或平台门不满足时，classic deferred/clustered 更直接。
- 加权随机抽样用概率覆盖高贡献灯并以反概率权重保持估计；top-k 会产生确定性截断偏差，均匀抽样浪费高贡献样本。提高 slots 或 reference 模式可降低方差，但增加后端成本。
- completed/compaction 让便宜或专用证据先回答，避免昂贵后端重复处理；screen trace 便宜但不完备，VSM依赖页兑现，HWRT覆盖世界表示但平台/成本更高，SWRT是显式允许的受限回退。
- resolve 后再做时空重建，是用邻域和历史换稀疏样本稳定性；关闭 temporal 适合定位 history 问题，但会提高噪声。
- Volume/Hair 不强行复用 opaque 合约，因为输入维度、位置重建、shadow backend、filter、output owner 不同；强行同构会承诺不存在的组合。

## Worked Cases 与 Last-Valid-State

- 工厂案例贯穿 40 盏灯的 owner 路由、15 个 cell candidates、4 个 slots、shadow evidence、resolve/denoise 与最终输出。
- 15->4 案例明确：候选不存在时停在 light grid；候选有权重但未入 slot 时停在 sampler selection；slot 已有灯而 weight 错时查 finalize/guiding；sample 正常后才进入 trace。
- Owner 案例明确：灯在分界前是 classic/other owner；已在尾段但无 sample 时停在 `DirectLighting`/调用/调度，不回查 tracing gate。
- Evidence 案例明确：screen 后 `RayDistance` 前进且未完成时，下一个 owner 是 world trace；compact list 空可能意味着样本已完成，而不是 pass 丢失。
- Output 案例明确：sample -> resolve -> denoise -> target -> history extraction/import 分层检查。
- Volume/Hair 局部案例明确：Volume 不出现在 2D VSM/screen list 是预期；Hair 未产生 VSM page request 对应当前未实现边界。

## 最终事实修正

1. 把“三层门”修正为编译/平台、项目/设备、view/family request+tracing、per-light、owner output 五层，并把 `DirectLighting` 放到 owner 发布后的工作门。
2. 把“固定总成本”修正为 sample-domain/slot 驱动后端上界。
3. 把 `LightSample.Weight` 修正为以 `WeightSum / CandidateWeight` 为基线的 estimator weight，不再称为 reservoir 累计权重。
4. 把并列 shadow backends 修正为 completed/compact 驱动的真实条件顺序。
5. 把 Volume/Hair “只换采样域”修正为不同 input/backend/filter/output/history 合约，并显式记录 Hair VSM marking、Hair spatial filter 与 Volume software path边界。
6. 把“调用/提取完成”拆成 RDG declared、GPU wrote、target consumed、history extracted/imported，不越过证据深度。

## 源码克制

正文先建立 ownership、两个工作域、sample 状态机和输出完成深度，再使用少量 UE 符号作为调试路标。源码 path、行号、验证清单和密集调用栈留在本 sidecar；删除正文中的这些锚点后，工厂案例和状态转换仍能独立承载主线。

## 独立 BODY 依据

本报告没有用 `.codex/tmp/review_18_19.md` 的 PASS 字样代替检查。直接读取当前 BODY 后可独立确认：

- 第 118-143 行附近把 owner request、required tracing、per-light routing、owner output 与更晚的 `DirectLighting` 工作门按真实控制顺序分开。
- 第 145-299 行先定义 context/lifetime 与两个 domain，再用 15->4 案例承载 sampler、weight 与 history，不依赖源码字段表完成教学。
- 第 299-389 行把 sample-driven VSM 与 ordered evidence 状态机写成数据/owner 转移，并给 screen miss 的 last-valid-state。
- 第 391-476 行区分 resolve、denoise、`SceneColor` 与 history extraction 的完成深度。
- 第 478-523 行建立系统责任边界，并将 Volume/Hair 例外写成支持矩阵和局部 worked case。
- 第 525-629 行的调试顺序与前文状态输出一致，没有出现新增术语只在总结首次承担教学的情况。

据此，最终 BODY 的主线、O-D-C-L、设计理由/替代、worked case、last-valid-state、事实边界和源码克制均有正文内证据。该结论仅描述本次 sidecar 重建的 BODY 依据，不修改章节状态或任何公共 Gate。

## 残余风险

- multiview/family 当前仍有 first-view assumption；后续若专讲 VR/stereo 应单列。
- Hair VSM marking、Hair spatial filter 和部分 Volume software tracing 仍为功能边界，不应在后续文档中被概括成完整支持。
- CVar 默认与平台支持可能演进；矩阵保留稳定 symbol 锚点，后续升级需重开源码核验。
