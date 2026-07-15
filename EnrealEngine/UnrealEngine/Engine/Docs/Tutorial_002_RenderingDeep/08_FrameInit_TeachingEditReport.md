# 08 Frame Init Teaching Edit Report

> 依据最终正文重新生成；不继承旧结论，不改变章节状态。

## 最终结构
- 核心误区：把 Frame Init 当同步函数，或把 `EndInitViews` 当整帧完成。
- 唯一主线：Scene publication → View → visibility → relevance → dynamic geometry → GPUScene/RDG input → pass 可消费状态。
- 三轴：责任、数据、生命周期；六个阶段门；八级完成证据。
- 双合同：CPU visibility task graph 与 RDG/GPU input contract 并行协作、互不替代。
- 静态砖墙与动态火焰对照 persistent/cached 和 dynamic/frame-local 路径。
- 调试沿状态成立链寻找最后成立台阶。

## 事实与表达修正
- `FScene::Update()` 统一为 Scene publication，不称 Submit。
- `BeginInitViews` 只启动生产，不保证 visibility/relevance/GDME 完成。
- Occlusion 是条件化历史证据消费，不假定本帧 HZB 或统一路径。
- 可见与进入某 pass 分离，由 relevance 建立 per-pass 输入。
- GDME 几何合同与 dynamic GPUScene collector/range 合同分离。
- GPUScene 拆成 identity/range、RDG resources、producer、Scene Uniform window，不使用单一“上传完成”。
- `EndInitViews` 只证明相关 CPU producers 收口，不证明 pass、RHI、Queue、GPU completion。

## 八级证据、阶段门与双合同
- 每道门都说明允许继续的消费者与仍未成立的更深状态。
- Begin 负责启动，GDME finish 负责动态几何交账，End 负责要求的 CPU task graph 收口。
- CPU task graph 收敛不证明 GPU producer 执行；RDG entrance 成立不证明 CPU visibility 已交账。
- 完成模型与 03/04 分层兼容，可用于跨线程、RHI 和 GPU 调试。

## 信息价值与缩减审计
- Origin/旧稿的阶段顺序、visibility 角色、task graph、GDME、GPUScene dynamic、culling 条件和 RDG 入口均有落点。
- 调用顺序转译为阶段门；packet/allocator/range 转译为生产期 Owner 与调试证据。
- CVar、源码块、函数列表和验证记录被压缩，其条件化行为与技术意义仍在正文。
- 按物理换行计数，当前约 814 行，Origin 区间约 704 行，篇幅增加约 15.6%，未触发缩短预警。仍完成双素材逐项信息价值审计，未发现无落点单元；新增篇幅用于承载六个阶段门、八级完成证据、双合同边界和两个贯穿案例。

## 案例、源码克制与跨章
- 砖墙说明 cached command 可复用但每帧 visibility/relevance/包装仍存在；火焰说明 GDME、range、producer、window 分级成立。
- 正文不依赖路径、行号、验证记录或调用清单；最小符号用于定位阶段与责任。
- 与 02/03 一致：publication 不是 Submit；与 04/05 一致：RDG、RHI、Queue、GPU 分层。
- 与 06 一致：GPUScene 是复合一致性窗口；与 07 一致：08 建立输入，不重教 Command 算法；在 09/10 pass 消费前退出。

## 剩余问题与验收
- 阻断项：none。
- 源码快照变化时应回归 Occlusion 条件、GPUScene producer 窗口、culling 启动条件与 `EndInitViews` 等待范围。

| 维度 | 结果 |
|---|---|
| 双素材信息价值 / 事实安全 / 单一状态链 | pass |
| 八级证据 / 阶段门 / CPU-RDG 双合同 | pass |
| Owner-Data-Control-Lifetime / Worked case | pass |
| 调试价值 / 源码克制 / 跨章边界 | pass |
| 06 标定 | reached |

本报告不修改或重新判定章节完成状态。
