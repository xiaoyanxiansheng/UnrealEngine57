# 06 GPUScene Coverage Matrix

> **重建依据**：`origin.md` 第 06 章与最终 `06_GPUScene.md` 同等审计；事实以 UE5.7 `Engine/Source` 为裁决依据。  
> **用途**：记录最终正文的教学覆盖、双素材信息落点与大幅缩减审计，不承载章节状态维护。  
> **篇幅预警**：Origin 约 816 行，当前正文 473 行，缩减约 42%。已执行 45 项独立信息价值审计；结果为 45/45 有落点，无有价值内容因压缩而消失。

## 1. 核心模型

| 模型 | 最终定义 | 验收 |
|---|---|---|
| 核心误区 | GPUScene 不是每帧临时拼出的 `StructuredBuffer<float4x4>`，shader 也不能从 C++ 对象直接取得场景状态 | pass |
| 唯一正向主线 | Scene publication → persistent identity → instance/payload allocation → dirty/upload set → composite resource window → clear/scatter producer → resource entrances → Instance Culling/shader consumption | pass |
| 责任轴 | Scene 建立 Renderer 可查询状态；GPUScene 管身份、分配、打包与 producer；RDG 保证图内顺序；Instance Culling/Shader 消费；RHI/GPU 完成在更深层 | pass |
| 数据轴 | SceneInfo/Proxy 状态 → identity/packed mapping → slot/range → CPU upload records → GPU buffers/SRVs → culling indirection/draw consumption | pass |
| 生命周期轴 | Scene residency、persistent identity、allocation range、one-frame upload set、dynamic range、resource window 和 GPU consumption 分层 | pass |
| 章节边界 | 不展开 MeshDrawCommand 构建算法、Frame Init 阶段编排或平台命令执行 | pass |

## 2. 七个状态门

| 状态门 | 进入状态 | 状态变化与负责人 | 成立证据 | 失败症状 / 调试 | 验收 |
|---|---|---|---|---|---|
| 1. Scene publication → GPUScene identity | Primitive 已进入稳定 Renderer Scene | Scene/GPUScene 为当前驻留期建立 persistent identity，并维护到 packed storage 的映射 | identity 有效且映射到当前 packed index | Proxy 重建后误用旧 ID、整物体停在旧状态 | pass |
| 2. Instance / payload allocation | primitive identity 已有，但实例数据无槽位 | allocator 分配 instance offset/count 与可选 payload offset/stride | range、capacity 与 current residency 匹配 | 单实例串位、扩容后旧 offset 被误用 | pass |
| 3. Dirty → upload set | CPU 状态发生变化 | dirty 分类收敛为本次 primitive/instance/payload upload records | 当前 upload set 包含正确层级和目标 range | 移动未更新、只更新 primitive 却漏 instance payload | pass |
| 4. Composite resource window | 逻辑记录和容量需求已知 | GPUScene 确定 buffers、capacity、layout、high-water/max ID、dynamic ranges 与 Scene/View entrances 的一致窗口 | 所有 consumer 参数可由同一窗口解释 | 单独 SRV 新但 capacity/layout/range 旧，产生混合版本 | pass |
| 5. Clear / scatter producer work | upload records 已准备 | RDG producer 清旧 range、scatter 新记录并登记条件性 GPU writers | producer Pass 存在且访问声明连接 consumer | Capture 有 upload 但 draw 读旧值 | pass |
| 6. Resource entrances | producer 和资源窗口已建立 | Scene Uniform / View parameters 暴露当前 SRVs、ranges 与 lookup contract | consumer 入口与 producer 同一窗口 | buffer 内容对但 shader 从旧 entrance 读取 | pass |
| 7. Instance Culling / shader consumption | GPUScene 内容可读 | culling 结合 visible draw work、View、range/runs 生成 indirection 与 indirect args；shader 依合同反查 | culling input/output 与目标 draw 映射一致 | 普通读取正确但 GPU-driven 数量为零 | pass |

## 3. 四本账

| 账本 | 记录内容 | 不可混用项 | 生命周期 / Owner | 验收 |
|---|---|---|---|---|
| Scene identity ledger | primitive 当前驻留期身份、SceneInfo/Proxy 关系、persistent identity 到 packed index 的映射 | Component 身份、packed index、draw primitive id | Scene/GPUScene；跨帧但随驻留期重建 | pass |
| GPU allocation ledger | instance slot、count、payload offset/stride、capacity 与旧 range clear 责任 | relative instance id、visible instance index | GPUScene allocator；可 realloc | pass |
| Upload / publication ledger | dirty 层级、CPU upload records、producer work、composite resource window、resource entrances | “dirty”不等于“已上传”；单个 SRV 不代表完整版本 | GPUScene + RDG；本帧发布窗口 | pass |
| Draw consumption ledger | visible draw work、primitive/dynamic metadata、range/runs、InstanceIdsBuffer、indirect args 与目标 draw 映射 | GPUScene identity、material id、GPU completion | Instance Culling / MeshDrawCommand 消费侧 | pass |

## 4. Composite Publication Window

“GPUScene 版本”不是一个单一整数，而是一组必须同时匹配的发布事实：

| 组成项 | 必须一致的原因 | 验收 |
|---|---|---|
| Primitive / Instance / Payload resources 与 SRVs | consumer lookup 可能跨多张 buffer | pass |
| Capacity、high-water mark、max instance ID | 决定合法索引范围和 clear/upload 工作 | pass |
| Layout、stride、tile / decode 参数 | CPU packing 与 shader decode 必须使用同一 ABI | pass |
| Persistent 与 dynamic ranges | dynamic translation 不能误入 persistent 区间 | pass |
| Scene Uniform / View resource entrances | consumer 必须看到对应窗口，而非旧参数块 | pass |
| Clear / upload / GPU writer producer ordering | 新内容和旧 range 清理必须先于消费 | pass |
| Frame-local collector / commit 状态 | dynamic primitive 的窄窗口不能跨帧误用 | pass |

## 5. 45 项信息价值审计（816 → 473）

| # | 必保教学价值 | 最终落点 / 处理 | 结论 |
|---:|---|---|---|
| 1 | GPUScene 解决 C++ 对象不能被 shader 直接读取 | 开场核心困惑 | 保留 |
| 2 | 反对“每帧拼矩阵数组”的 Unity 直觉 | 开场与边界 | 保留 |
| 3 | 持久数据与 one-frame 工作并存 | 七门总图、dynamic 小节 | 强化 |
| 4 | Scene publication 是上游前提 | 状态门一 | 强化 |
| 5 | SceneInfo / Proxy 当前驻留期关系 | 状态门一、调试 1 | 保留 |
| 6 | Persistent primitive identity 的语义 | 状态门一、四本账 | 保留 |
| 7 | Persistent identity 不等于 packed index | 四本账、ID 边界 | 强化 |
| 8 | Packed storage 可重排 | identity mapping 说明 | 保留 |
| 9 | Primitive identity 不等于 instance slot | 四本账、状态门二 | 强化 |
| 10 | Instance offset/count 分配 | 状态门二 | 保留 |
| 11 | Instance payload offset/stride 分配 | 状态门二、payload 护照 | 保留 |
| 12 | Allocation 与内容上传分离 | 状态门二/三边界 | 强化 |
| 13 | Free 不等于 GPU 数据立即消失 | Free 小节、clear 责任 | 保留 |
| 14 | 旧 range 需要 clear / overwrite 责任 | 状态门二、五、案例 B | 强化 |
| 15 | 实例数量变化可能 realloc | 案例 Variant B | 保留 |
| 16 | Dirty 是 CPU 侧候选，不是完成证据 | 状态门三 | 强化 |
| 17 | Primitive dirty 与 instance dirty 区分 | 状态门三、调试 3 | 保留 |
| 18 | Payload dirty 独立于基础 instance record | 状态门三、payload 护照 | 保留 |
| 19 | CPU upload record 的打包职责 | 状态门三/五 | 转译保留 |
| 20 | Scatter upload 而非全表重建 | 状态门五 | 保留 |
| 21 | 条件性 GPU writer 必须登记 producer | 状态门三/五 | 强化 |
| 22 | PrimitiveSceneData 是 primitive-wide ABI | 专节 | 保留 |
| 23 | Primitive 数据不是材质选择表 | Primitive data 边界与章节出口 | 修正保留 |
| 24 | InstanceSceneData 先关联 primitive | InstanceSceneData 护照 | 保留 |
| 25 | Instance record 不是自包含 world matrix | 开场、instance 专节 | 强化 |
| 26 | Relative instance ID 的语义 | shader lookup、四本账 | 保留 |
| 27 | 可选 instance payload 的存在条件 | payload 护照 | 保留 |
| 28 | Custom data / payload stride 条件 | payload 护照、allocation ledger | 保留 |
| 29 | Large World 需要 high-position/reference 与相对数据配对 | Large World 专节 | 强化 |
| 30 | CPU encode 与 shader decode 必须同合同 | Large World、调试 6 | 强化 |
| 31 | 远处抖动优先查坐标合同 | 案例 Variant C | 保留 |
| 32 | Dynamic primitive 无长期 Scene identity | Dynamic primitive 专节 | 保留 |
| 33 | Dynamic collector / commit 是一帧窗口 | 状态门六、dynamic 专节 | 强化 |
| 34 | Dynamic range 与 persistent range 共用资源但语义不同 | Composite window、调试表 | 强化 |
| 35 | Resource entrances 暴露当前窗口 | 状态门六 | 新模型整合 |
| 36 | “版本”不能简化成单个 buffer 或整数 | 状态门四、composite window | 新模型整合 |
| 37 | Clear / upload 是 RDG producer work | 状态门五 | 保留 |
| 38 | RDG producer-before-consumer 只证明图内顺序 | 调试 5 与完成深度 | 强化 |
| 39 | Instance Culling 是消费者，不是第二份场景 | 状态门七 | 保留 |
| 40 | Visible draw work、View、range/runs 是 culling 输入 | 状态门七、调试 7 | 保留 |
| 41 | InstanceIdsBuffer 是 indirection，不是 Scene 副本 | 状态门七 | 保留 |
| 42 | Indirect args 不是 draw completion | 状态门七 | 强化 |
| 43 | 800 叶草贯穿 identity/allocation/upload/consume | 贯穿案例 | 保留并重构 |
| 44 | 四个案例变体承担局部诊断 | 移动、扩容、远距、GPU-driven=0 | 强化 |
| 45 | 最后成立状态的九级调试证据链 | 唯一调试模型 | 新结构完整承接 |

**审计结论**：45 项全部在最终正文中保留、融合、修正或强化。缩减来自重复术语回看、源码风格字段清单、重复流程图、重复解释、路径/行号和验证记录的移除；没有删除独有原理、条件、例外、案例或调试判断。源码段落的技术含义已转译为七个状态门、四本账、护照、案例变体与最后成立状态。

## 6. UE5.7 事实回归

| 高风险事实 | 核验范围 | 正文结论 | 验收 |
|---|---|---|---|
| GPUScene identity 与 packed storage 分离 | Scene/GPUScene primitive mapping | identity 可跨帧稳定于驻留期，packed index 可变化 | pass |
| Instance 与 payload allocation 分离 | GPUScene allocation records | offset/count 与 payload offset/stride 是不同合同 | pass |
| Dirty/upload/producer 分层 | update/upload paths | dirty 不等于 GPU 可见；producer 必须建立 RDG work | pass |
| GPUScene data 是多 buffer / 多参数 ABI | primitive/instance/payload shader contracts | 逻辑记录不应被教成单一 `struct[]` | pass |
| Large World encode/decode 配对 | CPU packing + shader decode | high/reference 与 relative data 必须一致 | pass |
| Dynamic primitive 使用窄发布窗口 | dynamic collector/upload paths | dynamic range 不获得长期 Scene identity | pass |
| Instance Culling 消费 GPUScene 与 draw metadata | culling context / shader parameters | culling 输出是 indirection/args，不是 Scene 数据库 | pass |
| RDG work 与 GPU completion 分层 | RDG/RHI/GPU completion model | producer 顺序不证明 queue submit 或 GPU completion | pass |

## 7. 最终验收矩阵

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

## 8. 剩余问题

- 无阻断项。
- 大幅缩减已通过 45/45 信息价值审计。
- 本矩阵不修改正文、章节完成状态或公共文件。
