# 08 Frame Init Coverage Matrix

> 依据最终正文重建；不改变章节状态。

## 状态成立链
| 台阶 | Owner / Data / Boundary | 结果 |
|---|---|---|
| Scene publication | `FScene::Update()` 建立可查询状态；queued change 已吸收；不是 Submit | pass |
| Frame/View | Renderer/`FViewInfo` 建立 frame-local 条件；不证明 visibility 完成 | pass |
| per-view 候选 | tasks 生产 visibility bits、frustum/occlusion 结果；历史证据按条件消费 | pass |
| per-pass 分类 | relevance/MeshPassSetup 生产 pass 输入；可见不等于进入所有 pass | pass |
| GDME 交账 | workers/merge 生产 dynamic mesh；消费者前合并冻结 | pass |
| GPUScene dynamic | collector/context 建立 identity/range/payload；与几何合同独立 | pass |
| RDG/Scene Uniform | resources、producers、resource window；入口成立不等于 GPU 已执行 | pass |
| Frame Init 收口 | `EndInitViews` 等待相关 CPU producers；不证明 RHI/Queue/GPU 完成 | pass |

## 八级证据与阶段门
| 证据/门 | 能证明；不能证明 | 结果 |
|---|---|---|
| 1 queued | 变化已登记；不能证明 RT 消费/publication | pass |
| 2 publication | Scene 稳定可查；不能证明 view visibility | pass |
| 3 Begin | producers 已启动；不能证明收敛 | pass |
| 4 visibility/relevance | 某 view 候选与分类存在；不能证明全 view/GDME/setup 完成 | pass |
| 5 GDME merged | dynamic geometry/CPU pass input 稳定；不能证明 GPUScene/RDG 完整 | pass |
| 6 GPUScene/RDG input | range/resources/producers/window 按条件成立；不能证明 producer 已执行 | pass |
| 7 EndInitViews | 要求的 CPU producers 收口；不能证明 pass/RHI/Queue/GPU 完成 | pass |
| 8 deeper execution | recording、Queue Submit、GPU completion 各有独立证据 | pass |

阶段门覆盖 `OnRenderBegin`、`BeginInitViews`、`FinishGatherDynamicMeshElements`、GPUScene/Scene Uniform、Instance Culling input、`EndInitViews`；每道门均说明允许继续与非保证项。

## CPU task graph 与 RDG/GPU input contract
| 合同 | 生产/冻结/消费 | 结果 |
|---|---|---|
| CPU visibility task graph | cull → relevance → GDME/MeshPassSetup；Begin 启动、GDME merge、End 等待；供 post-visibility/pass CPU work | pass |
| RDG/GPU input | dynamic ranges → resources → clear/scatter producers → Scene Uniform；供 culling/GPU consumers | pass |

两条线并行但不互相替代：CPU 收敛不证明 RDG producer 执行；RDG 入口成立不证明 CPU visibility 已交账。

## 机制、案例与调试
- Frustum 建立候选；扫描/加速结构依条件。Occlusion 消费条件化历史证据，不假定本帧 HZB 已成立。
- Relevance 把“可见”切为 pass 输入；GDME 与 dynamic GPUScene collector 分离。
- 静态砖墙说明 cached command 不取消每帧 visibility/relevance；动态火焰说明 GDME、range、producer、window 分级成立。
- 调试链：Scene → View → visibility → relevance → GDME → pass input → range/resources/producers → Scene Uniform/culling → RHI/platform/GPU。

## 双素材与缩减审计
| Origin/旧稿价值 | 最终落点 | 判定 |
|---|---|---|
| 多生产者收敛窗口 | 状态成立链 | strengthened |
| 阶段调用顺序 | 阶段门，不要求背调用栈 | translated |
| Frustum/Occlusion/Relevance | 输入、输出、条件、非保证项 | strengthened |
| task graph 并行与等待 | 启动、局部生产、CPU 收口证据 | strengthened |
| GDME 合并 | 与 GPUScene collector 解耦 | corrected |
| persistent/dynamic GPUScene | identity/range/resource/producer/window | strengthened |
| Instance Culling | 合法 no-op，不重教 07 算法 | boundary-safe |
| RDG 与 GPU work | declaration、ordering、RHI/Queue/GPU 分层 | corrected |
| 源码块/CVar/函数列表/验证记录 | 删除载体，含义自然语言化 | no unique value lost |

- 按物理换行计数，当前约 814 行，Origin 区间约 704 行，篇幅增加约 15.6%，未触发缩短预警。双素材信息价值审计仍逐项执行；新增篇幅主要承担六个阶段门、八级完成证据、CPU visibility task graph 与 RDG/GPU input contract 的分离，以及砖墙/动态火焰双案例的状态贯穿。
- 仍逐项审计；减少来自调用链、源码块、字面量和重复解释，独有过程、条件、例外与调试锚点均有落点。

## 验收
| 维度 | 结果 |
|---|---|
| 双素材信息价值 / UE5.7 事实 / 单一主线 | pass |
| 八级证据 / 阶段门 / CPU-RDG 双合同 | pass |
| Owner-Data-Control-Lifetime / Worked case | pass |
| 调试价值 / 源码克制 / 跨章一致性 | pass |
| 06 标定 | reached |
