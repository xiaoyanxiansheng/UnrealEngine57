# 05 RenderGraph Coverage Matrix

> **重建依据**：`origin.md` 第 05 章与最终 `05_RenderGraph.md` 同等审计；事实以 UE5.7 `Engine/Source` 为裁决依据。  
> **用途**：记录最终正文的教学覆盖、双素材信息落点与事实回归，不承载章节状态维护。  
> **结论**：正文以 Filter 为唯一主线，原始稿和当前稿中仍具教学价值的机制均有最终落点。

## 1. 主线与章节边界

| 项目 | 最终覆盖 | 验收 |
|---|---|---|
| 核心误区 | `AddPass` 顺序、资源创建与 GPU 执行不是同一层；RDG 接收局部声明，再编译全帧计划 | pass |
| 唯一正向主线 | Filter 输入 → 中间纹理 → 输出 / extract，贯穿声明、依赖、裁剪、生命周期、alias、barrier、Execute | pass |
| 责任轴 | 调用方声明访问与边界；RDG 推导计划；RHI 录制并落地 transition；GPU 最终消费 | pass |
| 数据轴 | 逻辑句柄 → 参数元数据 → 图边 / 状态 → 分配与 barrier 计划 → RHI 工作 | pass |
| 生命周期轴 | C++ wrapper、RDG logical lifetime、physical allocation lifetime 明确分离 | pass |
| 后续边界 | 不展开具体渲染算法、平台 barrier 编码或 GPU completion；Execute 只到 RHI recording/control transfer | pass |

## 2. Filter 编译链覆盖

| 阶段 | What / Why | Owner / Data / Control | 条件与边界 | Worked case / Debug | 验收 |
|---|---|---|---|---|---|
| 声明候选图 | 资源和 Pass 先成为逻辑节点，不代表物理资源或 GPU 命令已存在 | 调用方提供 descriptor、parameters、flags、lambda；builder 持有图记录 | 构图顺序保留候选先后，但不替代依赖声明 | Filter 的 input/intermediate/output 建立初始账本 | pass |
| 参数元数据 | 参数字段把读写意图转成可分析事实 | 参数结构提供 access；RDG 枚举资源引用 | lambda 内隐式访问不能替代声明 | 缺少读取声明时 consumer 链断裂 | pass |
| 依赖 | producer / consumer 由资源访问和必要显式依赖建立 | RDG 管理图边与顺序约束 | 同一资源相邻出现不自动证明正确语义 | 内容版本错时追 producer、consumer 与 access | pass |
| 裁剪 | 从 external、extract、NeverCull 等可观察根反向保留生产链 | 编译器决定执行工作集 | `AddPass` 成功不等于进入执行计划 | Filter 未连到输出时被裁剪；extract 变体改变根 | pass |
| 生命周期 | 首次必要访问到最后必要访问形成逻辑占用区间 | RDG 汇总引用与分配/释放点 | wrapper 存在不延长物理 allocation；extract 会改变边界 | Filter intermediate 的首次/末次访问可定位 | pass |
| Transient eligibility | 只有满足资源、平台、flags、external/extract 等条件的内部资源才进入 transient allocator | RDG 判定 eligibility，allocator 决定物理复用 | 不合格时回退 pooled / non-transient；不是“创建失败” | 关闭 transient 或资源逃逸时仍保持逻辑正确 | pass |
| Alias transition | 两个逻辑资源复用同一物理区间时必须建立 aliasing 边界 | allocator fences + RDG alias transition 交接物理占用 | alias 复用内存，不合并逻辑身份、内容或访问合同 | Filter A/B 区间不重叠才可复用；重叠时禁止 | pass |
| AsyncCompute | Graphics / AsyncCompute 形成 fork/join 与跨管线依赖 | RDG 编译 cross-pipeline fences 与资源状态 | 生命周期不能只看线性 Pass 编号；调试 flush 会改变并行条件 | Filter 变体用时间线解释占用区间延长 | pass |
| Barrier | 逻辑 access history 被编译成 prologue/epilogue transition | RDG 决策状态边界，RHI 执行 transition | barrier 是结果，不应孤立地当根因；含 UAV/subresource 条件 | 闪烁时先核内容版本，再查 barrier | pass |
| External / Extract | external 导入图外所有权；extract 声明 Execute 后的结果逃逸 | 调用方声明边界，RDG 完成交接 | external 不等于 transient；extract 改变裁剪根和 lifetime | Filter Variant A/B 对比保留/逃逸 | pass |
| Execute | 编译计划变成 RHI resource/view/transition/pass recording | RDG 驱动 RHI command lists；责任向下游转移 | RDG Execute ≠ platform Queue Submit ≠ GPU Completion | 调试证据按 RDG、RHI、platform、GPU 分层 | pass |
| ParallelPassSet | 合法 Pass 可并行录制，集合完成后按计划交接 command lists | `FParallelPassSet` 管理录制任务、prologue/pass/epilogue 与 finish/submit 协调 | 并行录制不改变图依赖，也不意味着 GPU 并行或完成 | TaskMode 与 ParallelExecute 条件用于定位“未录制/未等待” | pass |

## 3. 三种 Lifetime 审计

| Lifetime | 起止语义 | 不应混淆 | Filter 落点 | 验收 |
|---|---|---|---|---|
| C++ wrapper lifetime | builder、resource wrapper、Pass object 在 CPU 侧可访问的对象寿命 | 不代表 RDG 资源仍被图需要，也不代表物理显存占用 | 声明期对象可存在到 Execute 收口 | pass |
| RDG logical lifetime | 资源首次必要使用至最后必要使用的图内语义区间 | 不等于 wrapper 寿命或 GPU completion | intermediate 由 producer 至最后 consumer | pass |
| Physical allocation lifetime | transient / pooled backing 实际被分配、占用和可复用的区间 | 不等于逻辑身份；复用前需要 allocator fences / alias transition | Filter 中间资源可与不重叠资源共享 backing | pass |

## 4. 双素材信息价值审计

| 教学单元 | Origin 价值 | 当前最终稿处理 | 最终落点 | 结论 |
|---|---|---|---|---|
| RDG 是帧内编译器 | 建立声明/编译/执行三层 | 扩展为局部声明到全帧计划 | 开场与主线回顾 | 保留并强化 |
| Filter 贯穿案例 | 用临时过滤纹理串联全章 | 增加 Variant A/B/C 与调试路线 | 主线、统一编译账本、调试 | 保留并深化 |
| 资源声明与延迟分配 | 区分 logical resource 与 GPU allocation | 纳入三 lifetime 模型 | 第一、四阶段 | 融合 |
| 参数宏与访问登记 | 解释元数据怎样建立读写事实 | 改写为依赖输入与内容版本 | 第二阶段 | 转译保留 |
| AddPass 节点语义 | flags、lambda、参数形成候选节点 | 与 task mode 和主线状态合并 | 第一阶段 | 融合 |
| 依赖与裁剪 | 从可观察结果反推必要工作 | 明确构图顺序不能替代依赖 | 第二、三阶段 | 修正并保留 |
| Compile 全局计划 | 原稿列出裁剪、merge、fork/join | 改为统一编译账本 | 第三至第六阶段 | 融合 |
| AsyncCompute fork/join | 原稿已有跨管线时间线 | 增加生命周期非线性边界 | 第四、五阶段 | 深化 |
| Transient aliasing | 原稿解释逻辑多、物理少 | 补 eligibility、fallback、allocator fence、alias transition | 第四阶段 | 深化 |
| Barrier 编译 | 原稿解释 prologue/epilogue | 加入 subresource/UAV 与“症状非根因” | 第五阶段 | 深化 |
| External / Extract | 原稿解释图边界 | 用 Filter Variant 展示裁剪根与 lifetime 改变 | 第六阶段 | 深化 |
| Execute 到 RHI | 原稿描述分配、view、lambda、command list | 按 03/04 完成深度限制为 RHI recording | Execute | 修正并保留 |
| RDG 调试工具 | Immediate、validation、clobber、flush 等 | 只保留服务于假设隔离的调试模型 | 调试路线 | 降级为证据工具 |
| Parallel recording | 原稿已有并行 command list | 明确 `FParallelPassSet`、TaskMode、等待与交接 | 并行执行小节 | 强化 |
| 源码走读 | 提供真实实现含义 | 路径、行号和验证记录移入 sidecar；正文保留最小符号 | 全章 | 技术含义保留，载体降级 |

**移除说明**：被压缩的主要是重复 API 清单、按函数顺序的源码走读、重复流程图和验证性路径文本。其独有技术含义均迁移到上表对应的编译阶段、边界条件、Filter 变体或调试判断；未发现仍有教学价值但无最终落点的段落。

## 5. UE5.7 事实回归

| 高风险事实 | UE5.7 核验锚点 | 正文使用方式 | 验收 |
|---|---|---|---|
| Pass task mode 有 Inline / Await / Async 条件 | `ERDGPassTaskMode`、lambda traits | 解释录制任务与等待，不把它当 GPU pipeline | pass |
| Compile 包含裁剪、render-pass merge、async fork/join | `FRDGBuilder::Compile` | 作为编译职责，不做源码调用链教学 | pass |
| Transient 有 eligibility 与 fallback | `IsTransient` / `IsTransientInternal`、allocate/deallocate collection | 明确不合格资源仍可走非 transient backing | pass |
| Alias 依赖 allocation fences 与 transition | transient allocator、`AddAliasingTransition` | 区分 backing reuse 与 logical identity | pass |
| Barrier 分 begin/end batch，并支持 cross-pipeline fence | barrier batch、transition collection | 解释 prologue/epilogue 与 fork/join | pass |
| Parallel execution 使用 pass set 和 command-list coordination | `FParallelPassSet`、`SetupParallelExecute`、finish/submit helpers | 解释并行录制的控制与边界 | pass |
| External / Extract 改变图所有权边界 | register external / queue extraction | 解释图外导入和 Execute 后逃逸 | pass |
| Execute 不等于 GPU completion | RDG/RHI 分层事实 | 调试模型保持 completion depth | pass |

## 6. 最终验收矩阵

| 维度 | 结果 |
|---|---|
| 双素材信息价值 | pass |
| UE5.7 事实 | pass |
| 单一主线 | pass |
| 概念首次教学 | pass |
| Owner / Data / Control / Lifetime | pass |
| 条件与边界 | pass |
| Worked case | pass |
| 调试价值 | pass |
| 源码克制 | pass |
| 跨章一致性 | pass |
| 06 标定 | reached |

## 7. 剩余问题

- 无阻断项。
- 本矩阵不改变正文或公共文件中的完成状态。
