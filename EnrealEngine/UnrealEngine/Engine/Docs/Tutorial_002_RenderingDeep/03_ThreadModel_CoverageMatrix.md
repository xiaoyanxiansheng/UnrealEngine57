# 03_ThreadModel Coverage Matrix

> **章节**: `03_ThreadModel.md`  
> **重建依据**: 当前最终正文、`origin.md` 的 03 章节、公共规范与 06 教学标定  
> **用途**: 记录最终正文的教学覆盖、事实裁决、双素材信息落点与调试价值；不改变章节完成状态  
> **事实口径**: UE5.7 `Engine/Source` 为裁决依据；源码符号只在本矩阵承担验证锚点

---

## 一、章节主线与验收摘要

本章围绕一个 Primitive 的 Transform 更新，建立唯一的生产者—消费者主线：GT 捕获稳定快照，render command 进入 RT 可消费路径，RT 生成 Renderer 场景待办，`FScene::Update()` 建立 Scene publication，帧级渲染录制 RHI command list，RHI/backend 将工作提交到平台队列，最后由覆盖末端消费者的完成证据允许资源复用或退休。

全章统一使用六级完成模型：

| 级别 | 最后成立状态 | 能证明 | 不能证明 |
|---|---|---|---|
| 1. RT queued | render command 已进入 RT 可消费路径 | 完整 CPU work 已交给目标时间线 | RT 已执行 lambda |
| 2. RT consumed | RT 已执行目标 work | Renderer owner 已消费 CPU 命令 | Scene publication 已建立 |
| 3. Scene published | 场景变化已在阶段门被吸收 | 后续 visibility/pass 可查询新状态 | draw 已录制 |
| 4. RHI recorded | draw/dispatch/transition 已形成并封口为平台无关 RHI work | GPU 工作意图可进入 RHI/backend 链 | 平台 queue 已接管 |
| 5. Platform submitted | 平台工作已提交到目标 queue | queue 已接管指定工作 | GPU 已越过最后读取 |
| 6. GPU consumed | 匹配最后消费者的完成证据已满足 | 证据覆盖范围内可安全复用或退休 | 其他 queue、显示扫描或全设备均完成 |

验收摘要：单一主线、术语首用、owner/data/control/lifetime、条件边界、worked case、最后成立状态调试模型和源码克制均达到 06 标定要求。

---

## 二、教学单元覆盖矩阵

| 教学单元 | What / Why | Owner / Data / Control / Lifetime | 条件与边界 | Worked case / Debug | 事实状态 | 覆盖 |
|---|---|---|---|---|---|---|
| 多时间线设计 | 解释 GT、RT、RHI、GPU 为什么不能被压成一条同步调用栈 | GT 拥有可变游戏状态；RT 拥有 Renderer 场景；RHI 时间线推进平台无关工作；GPU 异步消费 | 逻辑时间线不等于固定 OS 线程；工具模式只改变载体 | 移动物体从 GT 更新到 GPU 消费的全章案例 | source-checked | deep |
| Work / Task | task 是携带函数、输入与依赖的可调度 CPU work | 生产者生成，调度系统持有，目标 named thread 或 worker 消费 | task 完成只覆盖它声明的 CPU work | 同一 Primitive 连续移动两次，区分两份快照与两份 task | source-checked | deep |
| Queue | 面向明确消费者保存待执行 work | queue 持有等待项，消费者按自身时间线推进 | queue 身份不能由执行它的物理 worker 反推 | 500 个 Transform 更新说明排队、积压与 pumping | source-checked | deep |
| Pipe | render command 的批录、launch、replay 与完成事件协议 | producer 写 pipe recording；pipe 将批次交给 RT 消费 | pipe completion 不自动等于 Scene publication、platform submit 或 GPU completion | 主 pipe、显式 pipe、模式切换和排干案例 | source-checked | deep |
| Command list 双层含义 | 区分装 CPU lambda 的 `FRenderCommandList` 与记录 GPU work 的 `FRHICommandList` | 前者服务 RT work 交接；后者由录制者封口并交给 RHI/backend | C++ 类型、逻辑 pipeline、物理 queue 不可互换 | 移动椅子先更新场景，再在目标 pass 录制 draw | source-checked | deep |
| Submit | 转移 command-list 控制权并建立后续依赖，不承诺 GPU 完成 | submit 前录制者拥有列表；submit 后 RHI/backend 链拥有推进责任 | Scene publication 不叫 Submit；Queue Submit 仍浅于 GPU Completion | BasePass immediate list 与两个 additional lists 汇合 | source-checked | deep |
| Fence / Frame Sync / Flush | fence 暴露消费者进度；frame sync 限制时间线距离；flush 是重型收束 | 等待方选择证据深度，生产者与消费者按事件或 GPU completion 协议同步 | RT/RHI CPU 安全点不等于 GPU complete；Present 不是通用全 GPU fence | 材质/mesh 资源销毁与帧末等待深度案例 | source-checked | deep |
| ENQUEUE 与 Dispatcher | 同一 render command 入口适配 TLS list、显式 pipe、主 pipe、TaskGraph 或 inline | GT 捕获快照；Dispatcher 选择载体；RT 执行 lambda | inline 调试结果不能代表线程化生命周期 | 同一命令在单线程、线程化与批录模式下的载体变化 | source-checked | deep |
| Named Thread 与 Local Queue | named thread 表示逻辑归属，local queue 允许等待者推进同一线程的本地 work | TaskGraph 管理逻辑队列；RT 或 worker 执行符合归属的 task | `RenderThread_Local` 是推进协议，不是第二条 Renderer owner 线程 | RT 等并行子列表时泵入 local work | source-checked | deep |
| Scene publication | RT 执行更新命令后先形成 `PrimitiveUpdates`，由阶段门建立可查询场景状态 | RT 拥有待办与 `FScene`；`FScene::Update()` 批量吸收 | RT consumed 不等于 Scene published；publication 不等于 Submit | 移动物体检查 lambda、待办和 Scene 阶段门 | source-checked | deep |
| RHI 模式与并行 translate | None、DedicatedThread、Tasks 改变执行载体，不改变产品与完成层级 | RHI logical timeline 消费已封口 lists；worker 可承载 tasks | 不能从线程名推断一定存在专用 OS 线程 | BasePass 子列表 dispatch、translate、formation、submit | source-checked | deep |
| 并行录制与汇合 | 并行只在明确的私有列表/任务边界内成立 | worker 写自己的 command list，parent 在汇合点接管 | worker 不应直接写共享 Renderer 状态 | 左右场景子列表汇入 immediate list | source-checked | deep |
| 模式切换、启动与关闭 | 运行形态变化本身是产消协议，需要停止生产、排干旧消费者、再切换载体 | 生命周期 owner 管理 pipe/RHI 执行载体和依赖资源 | 只改 CPU 投递形态未必需要 GPU idle；影响 native 使用时需第 6 级证据 | capture/单线程切换、shutdown 排干 | source-checked | deep |
| 跨线程数据安全 | 安全来自快照与所有权，而不是 lambda 语法 | GT 生产值快照；RT 消费稳定数据；资源由匹配证据保护 | 裸指针、共享写入和过浅 fence 会破坏契约 | 好/坏捕获对照 | source-checked | deep |
| 资源复用与退休 | 复用依据是最后消费者，而不是 CPU 引用或较浅提交状态 | allocator 管 slot；wrapper 由 CPU 引用管理；native 使用由 backend/GPU 完成协议管理 | slot 可复用不等于整个 wrapper 应销毁；wrapper 删除不是 GPU fence | 动态 mesh 上传环形缓冲 slot A | source-checked | deep |
| 最后成立状态调试 | 把“有没有执行”改写为六级证据定位 | 每一级都有权威产物、owner、证据与下一消费者 | 较浅证据不能反推较深状态 | 移动物体六级排障表 | source-checked | deep |

---

## 三、A 类事实回归锚点

| 事实组 | 稳定源码锚点 | 结论 |
|---|---|---|
| Render command 入口与 pipe 模式 | `FRenderCommandDispatcher::Enqueue`、`FRenderThreadCommandPipe::Enqueue`、`GetValidatedRenderCommandPipeMode` | TLS、显式 pipe、主 pipe、TaskGraph 与 inline 是条件化入口载体；pipe mode 会按运行条件收缩 |
| RT named-thread 消费 | `RenderingThreadMain`、`ENamedThreads::RenderThread`、`ENamedThreads::RenderThread_Local` | RT 以 TaskGraph named-thread 身份推进主队列，并可在等待场景推进 local queue |
| Transform 更新与 Scene publication | `FScene::UpdatePrimitiveTransformInternal`、`UpdatePrimitiveTransform_RenderThread`、`PrimitiveUpdates`、`FScene::Update` | render command 被 RT 消费后先形成场景待办；publication 在 Scene 阶段门建立 |
| RHI Submit 链 | `FRHICommandListExecutor::Submit`、RHI task pipe、command-list recording/finalization APIs | RHI lists 经过 dispatch、translate、platform formation 与 queue submit；CPU list 完成不等于 GPU completion |
| RHI 执行模式 | `ERHIThreadMode::None`、`DedicatedThread`、`Tasks` | RHI 逻辑归属可由当前线程、专用线程或 worker task 承载 |
| Fence 与帧末同步 | `FRenderCommandFence::BeginFence`、`FFrameEndSync::Sync`、`ESyncDepth` | 等待深度区分 RT、RHI 提交链及更深完成协议；CPU fence 不应被泛化为 GPU 完成 |
| 并行列表汇合 | `QueueAsyncCommandListSubmit` 及 command-list prerequisite/recording APIs | 并行子列表必须封口并满足依赖后才能汇入后续消费链 |

---

## 四、双素材信息价值与大幅缩减审计

### 规模预警

`origin.md` 的 03 区间约 865 行；当前最终正文约 988 行。相较本轮执行前曾出现的大幅压缩版本，最终稿已恢复并超过 Origin 的教学承载量。这里的审计不以行数判优，而是逐项确认被压缩阶段容易丢失的独有价值已经有最终落点。

| 风险教学价值 | Origin / 压缩稿风险 | 当前最终落点 | 结论 |
|---|---|---|---|
| task、queue、pipe、command list、submit、fence 首用解释 | 容易被压成术语表或调用链 | “先用一条生产者—消费者链放稳新词”及各局部案例 | preserved and deepened |
| pipe recording / launch / replay / completion 状态 | 压缩时容易只剩“pipe 用于批处理” | Pipe 小节明确产品、owner、控制权与完成边界 | restored |
| GT wait pumping / reentry | 容易被省略为等待实现细节 | Queue、named thread、local queue 与调试证据中保留 | restored |
| RenderCommandPipe 模式收缩与运行时切换 | 容易只写默认模式 | 工具模式、模式切换、启动关闭协议中明确条件与排干 | restored |
| RHI None / DedicatedThread / Tasks | 容易误写成固定三条物理线程 | RHI 模式小节按逻辑归属与执行载体区分 | preserved and corrected |
| 并行录制和 translate 汇合 | 源码走读容易消失或承担主教学 | 以 BasePass worked case 转译为产品封口、依赖和控制权 | translated |
| fence depth 与 GPU completion | 容易把 RT/RHI 等待写成 GPU 完成 | 六级完成模型统一裁决 | corrected and strengthened |
| 生命周期与资源复用 | 容易只保留 lambda 捕获规则 | 动态上传 slot A 案例覆盖 wrapper/native/最后消费者 | restored and expanded |
| 启动、关闭、heartbeat/健康协议 | 容易被视为旁支删除 | “启动、关闭与健康检查也是产消协议” | preserved within boundary |
| 移动物体完整排障 | 容易退化为函数调用检查表 | 六级“最后成立状态”路线 | rebuilt |

删除或合并的内容仅包括重复定义、重复源码路径/行号、过密函数清单和可由统一模型替代的同义说明。其技术含义已迁移到 owner/data/control/lifetime、条件边界、worked case 或调试证据中；未发现 Origin 独有且仍有教学价值的单元无落点。

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
