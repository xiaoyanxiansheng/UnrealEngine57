# Teaching Edit Report - 05_RenderGraph.md

> 本报告按最终正文重建，不继承旧报告结论；不修改章节状态或公共状态文件。

## 1. 最终教学结构

- 唯一主线是 Filter：输入被读取，中间纹理被写入，结果被消费或 extract。
- 正文按“候选图 → 依赖 → 裁剪 → 生命周期 → transient/alias → barrier → external/extract → Execute”推进，而不是按 `FRDGBuilder` 函数顺序走读。
- 责任轴固定为：调用方声明局部事实，RDG 编译全帧计划，RHI 录制并落地 transition，平台提交与 GPU completion 留给 03/04 的完成深度模型。
- 数据轴固定为：logical handles / parameters → access metadata → graph edges / states → allocation and barrier plan → RHI work。
- 生命周期轴明确拆成 C++ wrapper、RDG logical resource、physical allocation 三层。

## 2. 06 标定后的教学增量

- 每个承重概念都交代 what、why、owner、data、control、conditions、boundary 与 debug consequence。
- Transient 不再被写成“临时资源必然 alias”，而是完整解释 eligibility、fallback、allocation fences 与 alias transition。
- AsyncCompute 不再被写成第二条独立 Pass 列表，而是 fork/join 改变可并行区间、跨管线同步和 lifetime 判断。
- `FParallelPassSet` 被放在“并行录制责任”中解释，避免把 CPU command-list recording、GPU pipeline overlap 和 GPU completion 混为一谈。
- Filter Variant A/B/C 分别承担未 extract、extract 改变根、subresource access 的状态变化，不是装饰性案例。
- 调试采用“最后成立状态”：执行工作集、内容版本、lifetime/alias、cross-pipeline、barrier、Immediate mode 依次缩小责任层。

## 3. 双素材融合与信息价值

- Origin 的声明/编译/执行三层、Filter 案例、参数元数据、依赖裁剪、AsyncCompute、alias、barrier、external/extract、Execute 和调试工具均被保留。
- 当前稿新增或强化了构图顺序边界、三 lifetime、transient fallback、alias transition、统一编译账本、ParallelPassSet 和完成深度分层。
- 重复 API 列表、函数顺序走读、路径/行号、验证记录和重复流程图被移出正文；其技术含义已迁移到过程阶段、边界表、案例变体和调试判断。
- 没有采用 origin 或当前稿整版覆盖；最终结构由教学任务决定。

## 4. 事实修正与安全边界

- 修正“AddPass 顺序等于完整依赖”的误读：构图顺序存在，但不能代替资源访问、裁剪根和跨管线约束。
- 修正“Create 立即分配显存”的误读：logical declaration、RDG lifetime 与 physical allocation 分层。
- 修正“transient 资源必然 alias”的绝对化：只有满足 eligibility 且占用区间可复用时才成立，不满足时回退。
- 修正“alias 只是同一地址复用”的不完整说法：物理区间交接还需要 allocator fence / alias transition，逻辑身份与内容不合并。
- 修正“RDG Execute 等于已提交或 GPU 完成”：正文只将其推进到 RHI work recording/control transfer，并保留后续 completion depth。

## 5. 源码克制状态

- 正文没有源码路径、行号、验证日志或密集函数清单。
- `FRDGBuilder`、`FParallelPassSet`、task mode、external/extract 等符号只作为概念定位和调试路标。
- UE5.7 事实依据保留在 CoverageMatrix / SOURCE_INDEX；正文先建立语言模型，再使用最小符号锚点。

## 6. Worked Case 与调试价值

- Filter 贯穿资源声明、producer/consumer、裁剪、logical lifetime、physical reuse、barrier 和 Execute。
- Variant A 展示未 extract 时的完整图内 lifetime 与 alias 机会。
- Variant B 展示 extract 同时改变可观察根、生命周期和复用机会。
- Variant C 展示 mip/subresource access 不能被资源级粗粒度叙述替代。
- 闪烁调试路线避免从“多加 barrier”开始，先确认 Pass、内容版本、allocation overlap 和跨管线 ordering。

## 7. 剩余问题

- 无事实或教学阻断项。
- 未修改正文、章节状态、`OUTLINE.md`、`SOURCE_INDEX.md` 或 `GENERATION_GUIDE.md`。
