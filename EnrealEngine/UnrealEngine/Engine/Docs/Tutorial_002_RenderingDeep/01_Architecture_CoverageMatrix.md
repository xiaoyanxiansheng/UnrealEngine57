# 01 Architecture Coverage Matrix

> 依据最终正文重新生成。`origin.md` 与当前分章稿为同等候选素材；UE5.7 源码为事实裁决依据。本矩阵不承载完成状态。

## 双素材教学价值账本

| 教学单元 | Origin 价值 | 当前稿价值 | 最终落点 | 处理 |
|---|---|---|---|---|
| 核心误区 | 纠正“UE 只是更长 SRP 调用栈” | 聚焦 GT 为什么不能直接画 | 开篇主问题 | 融合 |
| 双输入汇合 | 视图请求与对象数据分流 | 强化持久 Scene 与本帧工作集差异 | 压缩模型、第三次换形 | 融合 |
| 四次换形 | 请求、场景、工作集、命令 | 增加责任轴与寿命轴 | 三套坐标轴及四个主节 | 扩展 |
| 五层责任 | Engine/Renderer/RenderCore/RHI/后端 | 按数据形态解释层间合同 | 五层数据形态与分层理由 | 重写 |
| 请求成形 | Viewport、ViewFamily、Renderer 入口 | 冻结条件与控制权交接 | 第一次换形 | 融合 |
| 场景成形 | Component→Proxy/SceneInfo/FScene | 三种寿命与 Scene publication | 第二次换形 | 深化 |
| 工作集成形 | visibility/relevance 筛选 | Scene 存在不等于本帧消费 | 第三次换形 | 深化 |
| 命令与完成 | RDG/MDC/RHI/后端主路径 | record/formation/queue submit/GPU completion 分层 | 第四次换形与完成条件 | 校准 |
| 执行域 | GT/RT/RHI 概览 | 区分 thread/task/pipe/list/queue | 横切面专节 | 重构 |
| 金属球案例 | Component 到 GPU draw | 四阶段账本与五层证据 | 状态账本、Worked Case | 扩展 |
| 源码走读 | 真实类型与入口 | 最小锚点、语言先行 | 各换形节 | 转译后降级 |

## 承重概念覆盖

| 概念 | What / Why | Owner / Data / Control / Lifetime | 条件与边界 | Case / Debug | 状态 |
|---|---|---|---|---|---|
| 四次换形 | 渲染是数据连续换形 | Engine 发起；Renderer 维护 Scene/计划；RHI/后端消费 | 不等于四个函数或线程 | 金属球四阶段账本 | source-checked |
| 三套坐标轴 | 判断过程、责任、寿命 | 每个状态说明 owner、控制权和有效期 | “完成”必须指明深度 | 三轴定位 | source-checked |
| 请求成形 | 冻结视图与输出请求 | GT 组织，经接口交给 Renderer | 不证明 Scene、draw、GPU 状态 | 阶段 1 | source-checked |
| 场景成形 | UObject 投影为稳定 Renderer 状态 | Component 归 GT；Scene 数据归 Renderer | 创建、入队、publication 不等价 | Scene 查询证据 | source-checked |
| 工作集成形 | View 压缩持久 Scene | Renderer 生成 per-view/per-pass 数据 | Scene 存在不保证 visibility/relevance | View/Pass 对照 | source-checked |
| 命令成形 | Pass 合同与 visible draw 变成命令 | RDG 管依赖；MDC 管 draw；RHI/后端形成命令 | declaration/record/submit/completion 不等价 | 平台捕获 | source-checked |
| 完成条件 | 防止 CPU 里程碑冒充 GPU 完成 | 最后 GPU consumer 决定证据 | Present/Queue Submit 非通用复用证明 | Completion/Output | source-checked |
| 执行域 | 线程、任务、管线、队列是不同抽象 | GT 修改；RT 编排；worker 切片；RHI/backend 消费 | 类型名不能证明物理 queue | owner/evidence | source-checked |

## 条件与边界

- `FScene::Update()` 只表示 Scene publication，不称为 command-list 或 Platform Queue Submit。
- `FScene` 跨帧持久；`FSceneRenderer` 服务本帧 ViewFamily。
- RDG declared、RHI recorded、platform command formed、Queue Submit、GPU completion 分层成立。
- 资源复用依赖覆盖最后 GPU consumer 的 completion evidence，不能写成模糊的“CPU 已提交”。
- 线程、RHI、RDG、GPUScene、MDC 和具体 Frame Init 算法分别留给 03–08。

## Worked Case 与调试

| 状态门 | 金属球证据 | 失败首查 |
|---|---|---|
| 请求成形 | ViewFamily、View、输出目标有效 | Engine/request formation |
| Scene 发布 | Proxy/SceneInfo 可被 `FScene` 查询 | Scene publication |
| 工作集成形 | 当前 View/Pass 有 visibility/relevance | per-view/per-pass |
| 命令成形 | Pass 执行、draw 形成平台命令 | RDG/MDC/RHI/backend |
| 最终输出 | 覆盖最后 consumer 的完成证据 | Platform/GPU/output |

调试原则：找到最后一个成立的数据状态，只检查下一次换形的 owner、输入、条件和证据。

## 源码克制与验收

- 正文仅保留必要 UE 符号，无源码路径、行号、验证日志或长函数清单。
- 源码走读价值已转译为数据形态、所有权、控制权、寿命、条件和调试证据。

| 维度 | 结果 |
|---|---|
| 双素材信息价值 | pass |
| UE5.7 事实 | pass |
| 单一主线 | pass：四次换形 |
| Owner/Data/Control/Lifetime | pass |
| 条件与边界 | pass |
| Worked case | pass |
| 调试价值 | pass |
| 源码克制 | pass |
| 跨章一致性 | pass |
| 06 标定 | reached |