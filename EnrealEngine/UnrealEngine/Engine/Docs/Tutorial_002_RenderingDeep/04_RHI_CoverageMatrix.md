# 04_RHI Coverage Matrix

> **章节**: `04_RHI.md`  
> **重建依据**: 当前最终正文、`origin.md` 的 04 章节、公共规范与 06 教学标定  
> **用途**: 记录最终正文的教学覆盖、事实裁决与双素材信息落点；不改变章节完成状态  
> **事实口径**: UE5.7 `Engine/Source` 为裁决依据；源码符号只在本矩阵承担验证锚点

---

## 一、章节主线与验收摘要

本章以“一颗红色粗糙金属 mesh 使用动态上传环形缓冲完成一次 draw”为贯穿案例，把 RHI 解释为一组资源、状态、draw、命令与完成合同，而不是 UE 版 CommandBuffer。

命令主线固定为五个阶段：

| 阶段 | 权威产物 | Owner / Control | 成立证据 | 不能推断 |
|---|---|---|---|---|
| 1. Record | 平台无关 RHI command work | command-list recorder | 工作已记录并封口 | 已 translate 或 submit |
| 2. Translate | context 可消费的后端操作 | executor / RHI context | RHI work 已被 context 消费 | 已形成可提交 native 包 |
| 3. Platform Command Formation | 可提交的 native command lists/packages | platform backend | native work 已 finalize/formation | platform queue 已接管 |
| 4. Queue Submit | 已进入目标 platform queue 的工作 | backend / queue | queue submit 成立 | GPU 已越过最后消费者 |
| 5. GPU Completion | 覆盖指定最后消费者的完成证据 | GPU timeline / retirement protocol | fence/completion value 等满足 | 其他 queue 或全设备均完成 |

资源旁线固定为两层生命周期：

| 层 | 管理对象 | 终点 | 关键边界 |
|---|---|---|---|
| Wrapper lifetime | UE CPU 侧 `FRHIResource` wrapper 与引用关系 | wrapper final check / C++ deletion | 引用归零只说明 CPU wrapper 不再被需要 |
| Native lifetime | 平台 native object、allocation 及其 GPU 使用 | backend retirement 后回收 | 必须覆盖最后平台/GPU consumer；不要求与 wrapper 同时结束 |

验收摘要：五阶段命令模型、wrapper/native 两层退休、资源创建事务、状态合同、PSO/binding、三轴队列模型、worked case 与最后成立状态调试均达到 06 标定。

---

## 二、教学单元覆盖矩阵

| 教学单元 | What / Why | Owner / Data / Control / Lifetime | 条件与边界 | Worked case / Debug | 事实状态 | 覆盖 |
|---|---|---|---|---|---|---|
| RHI 合同总览 | RHI 将上层渲染意图逐级降格为平台可提交、可同步、可回收的 GPU 工作 | Renderer/RDG 决策输入；RHI/backend 建立资源、状态、命令和完成合同 | RHI 不重新判断 visibility 或材质语义 | 红色金属 mesh draw 全章主线 | source-checked | deep |
| Dynamic RHI 与能力 | 启动阶段选择实现 RHI 合同的后端和能力集 | 平台/RHI 初始化拥有 backend 实例与 capability state | backend、feature level、shader platform、配置共同影响路径；NullRHI 不等于真实呈现 | 功能缺失先查 backend/capability | source-checked | deep |
| Resource Desc | desc 冻结类型、尺寸、格式、usage、flags、initial access 等需求 | 调用方描述意图，后端决定合法 native layout | desc 不等于具体 heap/layout；不同后端实现可不同 | 动态 vertex/upload buffer 创建合同 | source-checked | deep |
| Initializer / Writable Range / Finalize | 两阶段创建把资源需求、初始数据生产和句柄交付分开 | initializer 临时拥有写入事务；Finalize 交付 wrapper | Finalize 不证明 upload 已被 GPU 消费，也不证明后续 access 合法 | stride、subresource、初始数据错位排查 | source-checked | deep |
| Wrapper lifetime | CPU wrapper 由引用和 pending-delete/final check 管理 | CPU 引用系统拥有 wrapper 退休 | wrapper 删除不是通用 GPU fence | 资源引用归零但 draw 仍在飞行 | source-checked | deep |
| Native lifetime | native object/allocation 按 backend retirement 条件回收 | backend 与 GPU timeline 管理最后使用 | 不固定为某帧数或统一 fence；可晚于 wrapper | 上传 slot 复用与 native backing 回收 | source-checked | deep |
| Transition / Subresource / UAV | transition 描述资源区间从前一访问到后一访问的合法性 | command list 记录；backend 形成 barrier/sync | 资源存在不等于 access 合法；UAV 连续访问仍可能需要 ordering | native command 有但结果错误时查 state/dependency | source-checked | deep |
| PSO 配方与 Native Ready | PSO 固化 draw 的不可变管线状态并允许缓存/预准备 | 上层提供 initializer；cache/backend 准备 native PSO；消费点要求 ready | cache entry 存在不等于 native ready；异步策略依平台/配置 | 首帧 hitch 区分配方、cache miss、ready wait | source-checked | deep |
| Shader Binding | 把逐次参数、uniform、resource view/sampler 等压成后端可消费绑定 | 调用方提供参数值；RHI 打包；backend 写 descriptor/root binding | uniform、structured buffer、SRV/resource、bindless handle 不可混称 | 颜色正确但几何错位时分离 binding 与 resource state | source-checked | deep |
| Command-list Type / Logical Pipeline / Platform Queue | 三条轴分别表达接口能力、逻辑同步域与物理提交载体 | 类型系统限制 API；pipeline 标记同步语义；queue 实际执行 | C++ 类型不能证明物理 queue；AsyncCompute 仍需平台能力与依赖 | queue/state 错误定位 | source-checked | deep |
| Bypass | 缩短 record/dispatch 路径，便于某些模式直接执行 | 当前执行上下文消费 RHI API | 不绕过 platform queue 和 GPU 异步时间线 | 单线程调试正常、线程化失败的生命周期差异 | source-checked | deep |
| Record | 形成平台无关 RHI work | recorder 拥有 list，封口后移交 | `FinishRecording` 不证明 translate/submit/complete | worked case 第 6 步 | source-checked | deep |
| Translate | context 消费 RHI work 并调用后端实现 | executor/context 接管控制 | 可串行或并行，不能从旧参数名推断实际策略 | 延迟参数寿命问题 | source-checked | deep |
| Platform Command Formation | 汇合、finalize 并形成 native command packages | backend/context 管理 native command state | native command 存在不等于 queue 已接管 | capture 中看到 native command 但无结果 | source-checked | deep |
| Queue Submit | 把 native 工作交给目标 platform queue | backend/queue 接管顺序与依赖 | submit 不等于 GPU completion | 上传槽此时仍不可覆盖 | source-checked | deep |
| GPU Completion | 用匹配最后消费者的证据判断安全复用/退休 | GPU timeline 与 backend retirement protocol | 证据有范围；不得泛化到其他 queue、Present 或全设备 | 动态上传环形缓冲最后读取 | source-checked | deep |
| 完整资源+Draw 生命周期 | 把 desc、initializer、wrapper、state、PSO、binding、五阶段命令和两层退休串成一条状态链 | 每一步明确 producer、product、consumer 与 evidence | 任一步成立都不能跳推后续深度 | 红色金属 mesh 的 11 步 worked case | source-checked | deep |
| 最后成立状态调试 | 先确定当前最深成立证据，再追下一责任方 | 权威产物随阶段转移 | 避免用“创建成功”“提交成功”覆盖多个深度 | 症状—最后成立状态—下一检查表 | source-checked | deep |

---

## 三、A 类事实回归锚点

| 事实组 | 稳定源码锚点 | 结论 |
|---|---|---|
| 动态后端选择与能力 | `RHIInit`、`PlatformCreateDynamicRHI`、`IDynamicRHIModule::CreateRHI`、`FDynamicRHI`、`FRHIGlobals` | 后端在初始化期建立统一实现落点和能力状态 |
| 资源描述与包装 | `FRHIBufferCreateDesc`、`FRHITextureCreateDesc`、`FRHIBuffer`、`FRHITexture` | desc 表达合同，wrapper 隔离平台 native 对象 |
| 两阶段资源创建 | `RHICreateBufferInitializer`、`RHICreateTextureInitializer`、initializer writable-range/finalize APIs | 初始数据事务与 wrapper 交付分阶段；Finalize 不等于 GPU 消费 |
| Wrapper 退休 | `FRHIResource` 引用计数、pending-delete 与 `DeleteResources` 路径 | CPU wrapper 生命周期由引用和延迟删除管理，不能替代 GPU completion |
| 资源状态与 barrier | `ERHIAccess`、`FRHITransitionInfo`、`Transition` / `BeginTransitions` / `EndTransitions` | transition 描述资源/子资源/访问/pipeline 的同步合同 |
| PSO | `FGraphicsPipelineStateInitializer`、pipeline state cache、graphics/compute PSO 创建路径 | 配方、cache entry 与 native ready 是不同状态 |
| Shader binding | `FRHIBatchedShaderParameters`、`RHISetShaderParameters`、uniform-buffer/layout 与 descriptor-handle APIs | 参数打包和后端绑定具有明确数据形态与生命周期 |
| Command-list 层级 | `FRHICommandListBase`、`FRHIComputeCommandList`、`FRHICommandList`、`FRHICommandListImmediate` | 类型表达接口能力，不直接等于物理 queue |
| Record 与回放 | `FRHICommandBase`、RHI command execute inlines、`IRHICommandContext` | 平台无关命令被记录后由 context 消费并进入后端 |
| 平台命令与 draw | 平台 RHI command context 的 draw/dispatch 实现 | translate/formation 最终产生平台可提交工作 |
| Submit 与完成 | command-list submit/executor、平台 queue submit 与 completion/retirement APIs | Queue Submit 与 GPU Completion 分层；安全复用需覆盖最后消费者 |

---

## 四、双素材信息价值审计

`origin.md` 的 04 区间约 501 行；当前最终正文约 434 行，缩短约 13%，未触发 15% 强制预警，但仍执行完整信息价值审计。

| Origin 独有或较强内容 | 当前最终落点 | 处理结论 |
|---|---|---|
| 后端选择全链与能力状态 | “后端合同”概念护照 + 源码锚点矩阵 | 保留技术含义，压缩调用链载体 |
| Desc 与平台对象分离 | “资源合同：Desc 不等于最终内存布局” | 保留并增强边界 |
| Initializer / writable range / Finalize | 两阶段创建小节 + worked case | 保留并增加完成深度判断 |
| 引用归零不等于 GPU 不再使用 | wrapper/native 两层生命周期 | 重构为更稳定的双生命周期模型 |
| Transition、subresource、UAV ordering | 状态合同小节 | 保留并加入相邻概念边界 |
| PSO 创建、缓存与异步 ready | PSO 从配方到 Native Ready | 保留，避免把所有 miss 写成同一种后台编译 |
| Shader binding 数据形态 | Binding 与生命周期小节 | 保留并区分 uniform/resource/view/handle |
| 命令列表层级 | Type / Logical Pipeline / Platform Queue 三轴 | 保留并纠正“类型等于队列”误读 |
| DrawCall 记录为命令节点 | Record 与 Translate 阶段 | 从源码走读转译为产品与控制权变化 |
| Context 进入平台后端 | Translate 与 Platform Command Formation | 保留并拆清形成/提交深度 |
| 资源与 Draw 完整生命周期 | 11 步 worked case | 保留并扩展为两条线交汇 |
| 调试主线 | “寻找最后成立状态” | 从函数顺查升级为证据模型 |

被删除或合并的主要是重复源码路径、行号、字段清单、平台调用细节和同一结论的多次说明。其技术意义已进入合同模型、五阶段命令、双生命周期、worked case 与调试表；未发现 Origin 中仍有教学价值的原理、条件、例外或调试判断无落点。

---

## 五、最终验收矩阵

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
