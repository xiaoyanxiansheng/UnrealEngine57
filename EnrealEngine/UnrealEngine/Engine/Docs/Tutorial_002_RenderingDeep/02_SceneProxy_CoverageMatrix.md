# 02 SceneProxy Coverage Matrix

> 依据最终正文重新生成。双素材同等候选，UE5.7 源码裁决事实。本矩阵不改变章节状态。

## 双素材教学价值账本

| 教学单元 | Origin 价值 | 当前稿价值 | 最终落点 | 处理 |
|---|---|---|---|---|
| Unity 误区 | Renderer/Component 直觉风险 | 渲染侧只消费快照 | 开篇 | 融合 |
| 四个对象 | Component/Proxy/SceneInfo/FScene | owner 与查询责任分层 | 心智模型 | 深化 |
| 三本账 | 对象、空间、绘制并行结构 | 身份/空间/绘制账及同版一致性 | 心智模型、入场总览 | 重写 |
| 五步入场 | 注册、创建、初始化、update、draw 输入 | 创建/入队/入场/可绘制状态门 | 五个主节 | 深化 |
| `nullptr` | 合法拒绝条件 | 明确不是异常 | 第二步 | 澄清 |
| SceneInfo | 关系与索引节点 | 不是第二份 Proxy | 第二步 | 澄清 |
| Scene publication | 批量吸收 pending changes | publication window 与不变量 | 第四步 | 深化 |
| Proxy 查询 | 虚函数和子类差异 | Renderer 向快照提问 | 插曲 | 转译 |
| Draw 输入 | static/dynamic 收集 | 入场不保证 View/Pass 消费 | 第五步 | 深化 |
| Transform dirty | 局部更新与重建 | Mobility/稳定合同解释 | 运行时变化一 | 校准 |
| RenderState dirty | mesh/material 重建 | 旧快照整体失效 | 运行时变化二 | 深化 |
| 删除 | remove/detach/deferred cleanup | 断引用、撤关系、再删除 | 运行时变化三 | 重构 |
| 椅子案例 | StaticMesh 生命周期 | 已入队 vs 已入场 | Worked case | 扩展 |
| 源码走读 | 类型、数组、入口 | 最小符号与路标 | 各机制节 | 转译后降级 |

## 承重概念覆盖

| 概念 | What / Why | Owner / Data / Control / Lifetime | 条件与边界 | Case / Debug | 状态 |
|---|---|---|---|---|---|
| Component | 可变游戏对象 | GT 拥有并发布注册/dirty 意图 | Renderer 不回读 UObject | 完全没出现 | source-checked |
| SceneProxy | Renderer 查询快照 | GT 摘取，Renderer 消费 | 可为 `nullptr`；不是 Component 镜像 | 核对 Proxy | source-checked |
| SceneInfo | 关系、索引、生命周期节点 | 连接三本账 | 创建不等于进入 `FScene` | 查入队/吸收 | source-checked |
| FScene | 跨帧场景数据库 | 维护一致 publication window | Scene 存在不等于 View 可见 | Scene 查询 | source-checked |
| 五步入场 | 请求→快照→RT 入队→publication→draw 输入 | 控制权从 GT 交给 Renderer | 任一步不能跳推后续 | 已入队/已入场 | source-checked |
| Scene publication | 批量吸收变化 | `FScene::Update()` 建立稳定状态 | 不称 Submit；RT consumed 不等于 publication | update 前后 | source-checked |
| Draw 收集 | Scene 记录成为 View/Pass 候选 | per-view/per-pass 消费 | 入场不保证 draw；缓存不缓存可见性 | 存在但不显示 | source-checked |
| Transform dirty | 空间变化由 Scene 更新落地 | GT 标 dirty；Renderer 更新或重建 | Movable/非 Movable 路径不同 | 移动不更新 | source-checked |
| RenderState dirty | 合同变化使旧快照失效 | 撤旧关系并建新快照 | 并非都能局部 patch | 比较新旧状态 | source-checked |
| 删除 | 断消费、撤关系、回收 | GT 停止发布；Renderer 撤登记；延迟清理 | 非注册倒放；CPU 删除不等于 GPU 完成 | 残留/崩溃 | source-checked |

## 三本账、条件与 Worked Case

| 账本 | 数据 | 失配症状 |
|---|---|---|
| 身份账 | Primitive/Proxy/SceneInfo 对应 | 缺失、索引错、悬挂引用 |
| 空间账 | transform、bounds、空间索引 | 移动不更新、误裁剪、残影 |
| 绘制账 | relevance、mesh、缓存输入 | Scene 中存在但目标 Pass 无 draw |

状态线：提出注册请求 → 摘取 Proxy/SceneInfo → RT 初始化并入队 → `FScene::Update()` 吸收并发布 → View/Pass 收集 draw 输入。

- 加入请求只是登记意图；`CreateSceneProxy()` 返回 `nullptr` 是合法边界。
- 静态缓存不等于永久可见、永不更新或 GPU 已完成。
- 椅子案例证明“已入队”与“三本账已一致发布”的“已入场”不同，之后仍需 View/Pass 条件。
- 调试按完全没出现、移动不更新、删除后残留/崩溃三类症状，从最后成立状态检查下一状态门。

## 源码克制与验收

- 正文无源码路径、行号、验证日志或函数清单；并行数组、虚函数、dirty 和删除序列已转译为教学模型。

| 维度 | 结果 |
|---|---|
| 双素材信息价值 | pass |
| UE5.7 事实 | pass |
| 单一主线 | pass：五步入场 |
| Owner/Data/Control/Lifetime | pass |
| 条件与边界 | pass |
| Worked case | pass |
| 调试价值 | pass |
| 源码克制 | pass |
| 跨章一致性 | pass |
| 06 标定 | reached |