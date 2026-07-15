# 10 BasePass Coverage Matrix

## 1. 素材与口径

- 原版：用户附件，451 个物理行、29,189 个字符。
- 最终正文：`10_BasePass.md`，465 个物理行、19,019 个字符。
- 两份素材同等候选；UE5.7 本地源码裁决事实，第 06 章标定教学质量。
- 旧 343 行稿未纳入用户提供的完整原版附件，本矩阵已废弃由该错误素材范围产生的结论。

最终正文相对原版增加 14 行，约 **+3.1%**；字符减少 10,170，约 **-34.8%**。行数不是质量目标，字符压缩仍执行逐项信息价值审计。

## 2. 双素材逐项落点

| 教学单元 | 原版价值 | 当前结构/修正 | 最终落点 | 结论 |
|---|---|---|---|---|
| 核心误区 | Unity Deferred/GBuffer 迁移直觉 | “表面合同交付”上位模型 | 开篇与三段合约 | 融合 |
| 三段合约 | 输入、编码、发布展开 | 每段明确最后成立状态 | 全章唯一主线 | 强化 |
| Unity→UE | draw、对象数据、decal、attachment 对照 | 增加生命周期与调试意义 | 三列迁移表 | 补回 |
| 四类输入 | Command、GPUScene、Depth、DBuffer 机制与症状 | producer/consumer/缺席条件 | 输入责任表 | 融合 |
| Processor | eligibility、blend/domain、main-pass、fallback、light-map、shader/PSO/binding/sort | 决策树替代源码分支清单 | `AddMeshBatch`/`Process` 段 | 补回 |
| Cached/Dynamic | 形成时机与缓存重建 | 分离失效、重建、view selection、recording、GPU consumption | 材质变化案例 | 补回 |
| Depth contract | PrePass 影响 BasePass depth access | 按覆盖、masked、WPO/PDO、Nanite、平台和 path 条件化 | 输入合约 | 保留修正 |
| Layout | 典型 SceneColor、GBuffer A–E、Velocity 与错位症状 | 四维 ABI | ABI 表与条件化实例 | 融合 |
| Material context | `FMaterialPixelParameters` 与 `FPixelMaterialInputs` | 稳定数据换形 | Material 数据轴 | 补回 |
| `FGBufferData` | 具体 payload 账本 | 逻辑记录，不等于 MRT | 红色金属像素账本 | 融合 |
| ShadingModel/CustomData | Clear Coat 前后变化 | model/layout/writer/consumer 约束 | Clear Coat 变体 | 补回 |
| DBuffer | before-base patch 资源直觉 | feature→write→receiver→response→apply | 输入与编码段 | 融合 |
| Nanite bin | 材质区域重组成执行工作 | bin 不等于 Component slot | 双材质区域案例 | 补回 |
| Indirect dispatch | bin 范围到 GPU shading | 不展开第 16 章算法 | bin→args→`ShadeGBufferCS` | 补回 |
| Publication | 写完 MRT 不等于标准可读 | BasePass 生产，Renderer publication | 发布合约 | 保留修正 |
| 调试 | 多组故障表与 15 步路线 | last-valid-state 证据梯 | 九级证据 | 合理合并 |

## 3. 七类补回

| 补回类别 | 最终教学任务 | 结果 |
|---|---|---|
| Unity→UE 迁移模型 | 纠正 draw 生命周期、对象数据、DBuffer、publication 错误先验 | pass |
| Processor 决策树 | eligibility、fallback、light-map、shader/PSO/binding/sort | pass |
| Cached/Dynamic 生命周期 | 材质合同变化、缓存失效、重建与不同完成深度 | pass |
| Material 数据换形 | `FMaterialPixelParameters → FPixelMaterialInputs → getter → FGBufferData → layout` | pass |
| Payload 与 Clear Coat | 红球像素账本与前后 payload 对照 | pass |
| 条件化 layout 实例 | 四维 ABI 下的常见 deferred 目标集合 | pass |
| Nanite bin/dispatch | raster result→material regions/bin→indirect args→compute shading | pass |

## 4. 旧 343 行版本的信息损失

| 损失 | 旧版本表现 | 当前修复 |
|---|---|---|
| Unity 迁移 | 只剩一句提示 | 恢复完整迁移表 |
| Processor 深度 | 过滤与 shader/state 摘要 | 恢复完整决策树 |
| Light-map/fallback | 缺失 | 纳入 BasePass policy |
| Cached 生命周期 | 无失效重建案例 | 增加材质变化状态链 |
| Material 中间层 | 上下文与 getter 合并 | 恢复两个中间形态 |
| 红球 payload | 只有抽象链 | 恢复具体字段账本 |
| Clear Coat | 只有结论 | 恢复前后解释合同 |
| Layout 实例 | 只有 ABI | 增加条件化典型布局 |
| Nanite bin | 退化为检查清单 | 恢复材质工作重组过程 |

## 5. 合理压缩

字符减少主要来自删除源码路径、行号、验证记录和长调用链；合并重复的“非最终 Lighting”“写完不等于可读”；把固定 MRT/DBuffer 槽位枚举改为 ABI 与条件模型；把重复故障表合并为证据梯；压缩与 07、09、12、16、20/21 重叠的专题展开。被删除的是重复载体、易过时的固定记忆和越界展开，不是承重机制、条件、案例或调试判断。

## 6. 保留的事实修正

| 事实边界 | 最终口径 | 结果 |
|---|---|---|
| Publication owner | BasePass/Nanite 生产，Renderer 建立 SceneTextures publication | pass |
| 完成深度 | production、RDG readability、publication、Queue Submit、GPU Completion 分层 | pass |
| Depth | PrePass 后不是无条件 read-only | pass |
| Layout | 四维 ABI 为主，实例不是永久槽位表 | pass |
| Getter / `FGBufferData` | generated-code 接口；逻辑记录，不是物理 MRT | pass |
| CustomData | 依赖 ShadingModel、layout、writer、consumer | pass |
| DBuffer | 条件链，不等于所有 decal 或固定 A/B/C | pass |
| Nanite | 同类 deferred 消费合同，layout 可变；bin 不等于 Component slot | pass |
| `SetupMode` | 标准读取范围，不是 GPU 完成证据 | pass |
| Submit | formation、recording、Queue Submit、GPU Completion 分开 | pass |

## 7. O/D/C/L、案例与调试

| 轴 | 最终模型 |
|---|---|
| Owner | 前序生产 inputs；Processor 固化 policy；raster/Nanite 写资源；Renderer publication；后续消费 |
| Data | inputs → pixel context → material inputs → getters → `FGBufferData` → payload → layout → SceneTextures |
| Control | candidate → policy → cached/dynamic command → view selection → draw/dispatch → RDG production → publication → consumer |
| Lifetime | cached、frame-local、GPUScene、graph target、publication、GPU completion 分离 |

红色金属球贯穿 deferred、Depth、DBuffer、Clear Coat、Nanite 双材质区域、layout、publication 和 Lighting 前证据边界。调试梯覆盖 candidate、command/cache、object data、depth、DBuffer、material inputs/getters、`FGBufferData`、GBuffer produced、SceneTextures publication。

## 8. 最终矩阵

| 维度 | 结果 |
|---|---|
| 双素材逐项落点 / 七类补回 | pass |
| 旧 343 行损失修复 | pass |
| UE5.7 事实 / 条件边界 | pass |
| 单一主线 / 三段合约 | pass |
| 概念首次教学 / O-D-C-L | pass |
| Worked case / Last-valid-state | pass |
| 源码克制 / 跨章一致性 | pass |
| +3.1% 行数与 -34.8% 字符审计 | pass |
| 06 标定 | reached |

**Coverage 结论：PASS。**
