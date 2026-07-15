# 10 BasePass Teaching Edit Report

## 1. 真实素材与纠错

本轮使用两份同等候选素材：原版附件 451 行、29,189 字符；最终 `10_BasePass.md` 465 行、19,019 字符。旧 sidecars 未纳入完整原版附件，因此其素材范围与信息价值结论全部废弃。旧 343 行版本虽保留三段合约、Depth 条件模型、publication owner、四维 ABI 和证据梯，却造成中层机制与案例信息损失。

本报告只记录最终结构与验收，不修改正文、章节状态或公共文件。

## 2. 最终结构

唯一主线为：

```text
输入合同成立
→ BasePass-specific policy 固化为 command
→ 经典 raster 或 Nanite shading 生产表面语义
→ material semantics 进入 FGBufferData/model payload
→ 当前 GBuffer layout 编码
→ Renderer 建立 SceneTextures publication
→ Lighting 获得标准读取入口
```

三段合约分别是输入、编码、发布；每段都明确最后成立状态。Command formation、recording、RDG production、publication、Platform Queue Submit 和 GPU Completion 不混用。

## 3. 双素材融合与七类补回

### Unity→UE 迁移模型

恢复 draw 生命周期、对象数据、DBuffer 与 attachment/publication 的迁移表，并增加调试意义。它用于纠正 Unity 先验，不制造 API 一一对应。

### Processor 决策树

恢复 eligibility、blend/domain/main-pass、default material fallback、light-map policy、shader、depth/stencil、PSO、binding、sort key。`AddMeshBatch`/`Process` 只作定位锚点，正文教授的是策略选择和状态固化。

### Cached/Dynamic 生命周期

恢复材质合同变化导致旧 command 失效、缓存重建、view selection、recording 与 GPU consumption 的状态案例。它解释“修改材质后的首次绘制异常”为何不能只检查 shader。

### Material 数据换形

恢复并稳定以下数据轴：

```text
FMaterialPixelParameters
→ generated material evaluation
→ FPixelMaterialInputs
→ material getters
→ semantic values
→ FGBufferData
→ model-specific payload
→ current layout
```

Getter 是 generated-code 接口，不是运行时节点解释器；`FGBufferData` 是逻辑记录，不是物理 MRT。

### Payload 与 Clear Coat

恢复红色金属像素的 BaseColor、Metallic、Specular、Roughness、Normal、AO、ShadingModelID、CustomData 账本。Clear Coat 变体比较基础语义、ID 与 CustomData 解释如何变化，同时保持 Lighting/BRDF 章节边界。

### 条件化 layout 实例

四维 ABI 仍是上位模型：target count、slot order、pixel format、conditional presence。正文增加典型 SceneColor、GBuffer A–E、可选 Velocity 实例，但明确 Substrate、GBufferF、SGGX、平台和 permutation 可改变实际集合。

### Nanite bin / indirect dispatch

恢复：

```text
raster result
→ visible material regions
→ shading command / material bin
→ per-bin work range
→ indirect args
→ ShadeGBufferCS 等 compute shading
→ current layout
```

Material bin identity 不等于 Component material slot，bin 也不等于最终 draw 或 GPU completion；完整算法留给第 16 章。

## 4. 旧 343 行版本的信息损失

旧版本过度压缩了 Unity 迁移、Processor 决策深度、light-map/fallback、cached command 重建、材质中间数据形态、红球 payload、Clear Coat 对照、layout 实例和 Nanite bin 数据轴。最终 465 行稿已逐项补回这些价值，同时保留旧版本正确的三段合约、事实边界和证据模型。

## 5. 合理压缩与篇幅审计

- 物理行：451 → 465，增加 14 行，约 **+3.1%**。
- 字符：29,189 → 19,019，减少 10,170，约 **-34.8%**。

字符压缩来自删除源码路径、行号、验证记录和长调用链；合并重复结论与 checklist；以 ABI/条件链替代固定槽位枚举；压缩与 07、09、12、16、20/21 重叠的内容。逐项账本确认原版仍有价值的原理、条件、例外、案例和调试判断均有最终落点。行数增加不作为质量证明。

## 6. 保留的事实修正

- BasePass/Nanite 生产资源，Renderer 建立 SceneTextures publication。
- Production、RDG readability、publication、Queue Submit、GPU Completion 分层。
- Depth contract 根据覆盖、masked、WPO/PDO、Nanite、平台和 renderer path 决定。
- 四维 layout ABI 为上位模型，典型实例不是永久槽位表。
- Getter 是 generated-code 接口，`FGBufferData` 是逻辑记录。
- CustomData 受 ShadingModel、layout、writer、consumer 共同约束。
- DBuffer 使用 feature→write→receiver→response→apply 条件链。
- Nanite 交付同类 deferred 消费合同，layout 可变；material bin 不等于 Component slot。
- `SetupMode` 不证明 GPU 已执行；Depth resolve 不承诺固定物理复制。
- Command formation、recording、Platform Queue Submit 与 GPU Completion 分开。

## 7. O/D/C/L、案例与调试

| 轴 | 最终结果 |
|---|---|
| Owner | 前序生产 inputs；Processor 固化 policy；raster/Nanite 写资源；Renderer publication；后续消费 |
| Data | inputs → pixel context → material inputs → getters → `FGBufferData` → payload → layout → SceneTextures |
| Control | candidate → policy → cached/dynamic command → view selection → draw/dispatch → RDG production → publication → consumer |
| Lifetime | cached、frame-local、GPUScene、graph resource、publication、GPU completion 分开判断 |

红色金属球贯穿 ordinary deferred、Depth、DBuffer、Clear Coat、Nanite 双材质区域、layout 和 publication。Last-valid-state 梯检查 candidate、command/cache、object data、depth、DBuffer、material inputs/getters、`FGBufferData`、GBuffer produced、SceneTextures publication，并说明每级不能证明什么。

## 8. 源码克制与边界

最小锚点包括 `FBasePassMeshProcessor::AddMeshBatch/Process`、`FMaterialPixelParameters`、`FPixelMaterialInputs`、material getters、`FGBufferData`、`SetGBufferForShadingModel`、`EncodeGBufferToMRT`、`FSceneTextures::GetGBufferRenderTargets()`、`ShadeGBufferCS`、`SetupMode`。符号只负责定位，正文以状态、数据、责任、条件和调试为主。

09 只交付 Depth contract；07 负责 MDC；12 负责 Lighting；16 负责 Nanite 算法；20/21 负责材质与 Shader 编译。正文没有源码路径、行号或验证过程。

## 9. 最终矩阵

| 维度 | 结果 |
|---|---|
| 双素材信息价值 / 七类补回 | pass |
| 旧 343 行损失修复 | pass |
| UE5.7 事实 / 条件边界 | pass |
| 单一主线 / 三段合约 | pass |
| 概念首次教学 / O-D-C-L | pass |
| Processor/Cached 生命周期 | pass |
| Material/Payload/Clear Coat | pass |
| Layout ABI 与实例 | pass |
| DBuffer/Nanite 条件链 | pass |
| Worked case / Last-valid-state | pass |
| 源码克制 / 跨章一致性 | pass |
| +3.1% 行数、-34.8% 字符审计 | pass |
| 06 标定 | reached |

剩余阻断项：无。维护期若 GBuffer layout、Substrate、Nanite shading 或平台路径变化，应重新核验条件存在性；本章 publication 成立不证明 Lighting 或最终 GPU completion 正确。

**Teaching Edit 结论：PASS。**
