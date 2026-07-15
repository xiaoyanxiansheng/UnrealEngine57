# 16 Nanite Teaching Edit Report

## 1. 报告边界与材料清单

本报告从当前最终正文、冻结原版、指定独立 review 与 UE5.7 源码重新建立。旧 TeachingEditReport 和旧 CoverageMatrix 的结论全部作废，未用于证明当前质量。

物理行数统一使用 `.NET File.ReadAllLines`：

| 材料 | 角色 | 行数 | SHA256 | 读取结果 |
| --- | --- | ---: | --- | --- |
| `Engine/Docs/Tutorial_002_RenderingDeep/16_Nanite.md` | 当前最终正文 | 584 | `90C2A4EB4EC1F0031692306103408372F4D02870FE4C1B64A3B4F07B8544790D` | 完整读取 |
| `.codex/tmp/renderingdeep_11_24_original_20260713/16_Nanite.md` | 用户声明的冻结原版 | 382 | `415D0ACA726C64349707D0477B055E9351CD6A18C4FA984DCFCFBB1F53F6F1AA` | 完整读取，未改动 |
| `.codex/tmp/review_15_16.md` | 指定 BODY review | 114 | `4ECEF53DE13D9A907D650D27D35EDA7BE91DD9C364D635774A80B08B60A48E1F` | 完整读取；其 `FAIL-16-1` 关闭证据已与源码独立复核 |
| UE5.7 本地源码 | A 类事实与高风险时序 | 不适用 | 固定源码快照 | 已核验 builder、资源注册、culling/shading、streaming manager 与 page uploader |

最终正文比冻结原版多 202 行，约增加 52.9%。这只说明没有触发“短于原版 15%”异常，不作为质量证明；原版价值仍按事实单元逐项审计。

## 2. 独立 BODY 结论

**BODY PASS。**

结论由本轮独立读取最终正文、冻结原版、指定 review 并复核 UE5.7 高风险源码得出，不依赖章节头状态或旧 sidecar。最终正文把 Nanite 教成一条完整的状态链：构建阶段制造 group-compatible Cluster DAG，编码成 packed hierarchy/pages；运行时注册并通过 proxy/GPUScene 发布整数身份；packed view 把对象误差翻译成像素误差；GPU 选择 compatible cut、执行 culling/raster/VisBuffer/shading，最终交付共享 GBuffer；缺页请求再沿跨帧 streaming 闭环变成可供后续 traversal 消费的 GPU resident page。

指定 review 中的唯一阻断 `FAIL-16-1` 已关闭。本轮再次确认正文所有相关位置都使用同一时序：request merge/initial priority 在前，parent expansion/priority propagation 在 final selection 前，随后才是 selection/LRU、slot、data request、CPU install、GPU transcode、scatter、GPU completion 与后续 traversal。未发现 BODY 阻断。

本结论不修改章节状态，也不表示公共 Gate 或共享索引已更新。

## 3. 实际教学主线

1. 从“Nanite 不是自动 LODGroup”进入，先定义变化的是几何决策粒度与决策位置，而不是只描述近细远粗的结果。
2. 构建阶段把 raw triangles 变成 leaf clusters、groups、共同简化的 parents 和累计误差，解释为什么 compatible replacement 必须先在资产中成立。
3. 将逻辑 DAG、packed hierarchy 与 pages/residency 分开，再用 root pages 说明缺细节应退化为粗 cut 而不是消失。
4. 资源通过 Render Thread streaming manager 获得版本化 id/hierarchy offset，再由 Nanite proxy 与 GPUScene 参数发布给 GPU。
5. Packed view 把对象空间误差变成当前 view 的 projected error；GPU 随后选择兼顾误差、group compatibility 与 residency 的 cut。
6. 五阶段管线依次处理 instance、hierarchy/cluster、raster bins + HW/SW、VisBuffer/SceneDepth/ShadingMask、shading bins + BasePass compute。
7. Streaming 闭环从 GPU request/readback/version filter 开始，经 parent priority、budget selection、LRU slot、IO、CPU install、GPU transcode/scatter 和 GPU completion，最后才允许尚未执行的 traversal 读取新页。
8. 岩壁状态表与证据梯按“最后成立状态”区分完全不显示、遮挡缺块、深度正确但材质错、长期过粗四类问题。

## 4. 冻结原版价值审计

| 原版价值单元 | 最终处理 | 最终落点 / 说明 |
| --- | --- | --- |
| Nanite 不是整对象 LODGroup | 保留并深化 | 开篇、Unity 桥接；加入 GPU-driven object LOD 与 group-compatible cut 的区别 |
| 资源注册为 GPU id/hierarchy offset | 保留并深化 | 第 2 节；增加 version filtering、proxy fallback 与 instance/cluster boundary |
| Packed view 带入像素尺度 | 保留并深化 | 第 3 节；明确对象空间误差、view rect、投影、动态分辨率和质量尺度 |
| GPU 五阶段 | 保留并扩写 | 第 5 节；每阶段补 what/why/input/output/failure、设计替代与岩壁状态变化 |
| HW/SW raster 与 shading bin 区分 | 保留并深化 | 第 5.3/5.5 节；加入 raster bin vs shading bin、VisBuffer identity-first 原因 |
| Nanite compute shading 写共享 GBuffer | 保留并限定 | 第 5.5 节；明确共享的是消费者合同，不是前半段 stage/feature 完全等价 |
| GPU request 驱动页流送 | 保留并重构 | 第 6 节；从概略回读/补父/驻留扩展成可逐级取证的完整异步闭环 |
| 原版四类现象调试 | 保留并统一 | 第 9 节状态表和证据梯；仍覆盖不显示、LOD、材质、遮挡，但改为统一状态链 |
| 原版缺少 leaf/group/parent 的构建理论 | 新增承重教学 | 第 1 节；解释 external-edge locking、累计误差和三种结构 |
| 原版把 LOD 选择概括成可见簇集合 | 精化 | 第 4 节建立 compatible cut，分开 parent/child error、group compatibility、residency |
| 原版 streaming 只到“页驻留更新” | 深化并校准时序 | 第 6 节与 9.1/9.2；补 parent priority、selection/LRU、slot、install/transcode/scatter/consumer |

未发现被删除且无去向的独特原则、条件、案例或调试判断。原版按现象拆分的四条调试路径没有丢失，而是迁移到“岩壁全过程状态表 + 最后成立状态证据梯”，从而减少重复并提升跨阶段定位能力。

## 5. O-D-C-L 审计

| 数据层 | Owner | Data | Control | Lifetime | 判定 |
| --- | --- | --- | --- | --- | --- |
| 构建期逻辑 | Nanite builder | adjacency、clusters、groups、replacement DAG、simplification error | grouping/simplification/locking/encoding | 构建过程 | 清楚 |
| 序列化资源 | mesh/resource system | packed hierarchy、root/streamable page 描述、压缩 cluster data、dependencies | cook/load/resource init | 跟随资源 | 清楚 |
| 全局 streaming 状态 | Render Thread `FStreamingManager` | versioned id、hierarchy/cluster buffers、page pool、registered/pending/LRU | request merge、parent expansion、selection、slot、install | 跨帧 | 清楚 |
| Primitive 状态 | scene proxy / `FScene` / GPUScene publication | resource id、hierarchy offset、transform、flags、material pipeline identity | proxy choice、scene publication、shader parameter build | primitive 注册期间 | 清楚 |
| 当前 RDG/GPU 状态 | Nanite renderer + RDG | packed views、queues、cut、raster/shading data、transcode/scatter passes | culling/raster/shading与 RDG dependencies | 当前图到 GPU 完成窗口 | 清楚 |
| 跨帧 request/data 状态 | streaming manager/resource IO | readback、priority、parents、SelectedPages、slot、IO/DDC/memory、ready staging | CPU async update 与 uploader | 非固定延迟跨帧 | 清楚 |

## 6. 设计理由与替代方案

- **Group-compatible simplification**：按 group 共同简化并锁外边，换取混合精度边界正确；逐 cluster 独立简化更简单但会产生不一致边界，过大 group 又增加构建和工作集成本。
- **Projected error**：比固定距离更能适配 FOV、分辨率、split-screen 与 shadow views；对象距离阈值更简单，适合 authored LOD 或规模较小的项目。
- **Hierarchy traversal**：让一次 node test 代表许多 descendants，避免平铺扫描全部 clusters；代价是维护 bounds/error/queues，几何简单时 classic draw 可能更便宜。
- **Main/post occlusion**：用额外 queue/HZB/post work换取对不确定候选的保守复测；只用 previous HZB 更便宜但证据陈旧，完全不做遮挡最稳但浪费 raster。
- **HW/SW 混合光栅**：大三角交给成熟硬件、微三角可交给 compute；hardware-only 更简单，纯 compute 更统一，但两者都不能无条件覆盖所有 workload。
- **Visibility-first + compute shading**：先确定最终像素 identity，再只对可见像素执行材质；代价是 VisBuffer、属性恢复、derivative、binning 和 indirect dispatch 复杂度。
- **Feedback streaming**：GPU 提供真实缺页需求，避免纯 CPU 距离预测的误加载；代价是 readback/IO 延迟。显式 prefetch、CPU prediction、full residency 都是条件式替代方案。
- **有限 page pool**：用固定显存预算支撑大数据集；增大 pool、降低质量、prefetch 或 full residency分别交换显存、画质、确定性和资产规模。

## 7. 案例与调试价值

- **Nanite 岩壁**从离线构建、运行时注册、packed view、compatible cut、HW/SW raster、石头/苔藓 shading bins 到缺页 streaming 全程不换对象，主线连续。
- **石头/苔藓双材质槽**把 VisBuffer identity、ShadingMask、ShadeBinning、indirect args 和 BasePass compute 的数据变化落到可观察状态。
- **Streaming leaf fallback**解释走近时“先粗后细”为什么是合法状态，而不是 cut 一旦要求更细就必须消失或同步等待。
- **长期过粗**案例把 request/version、merge、parent priority、selection/LRU、slot、data ready、CPU install、transcode、scatter、GPU completion 与 traversal consumer 分开。
- **证据梯**有 23 层，明确 CPU resident bookkeeping 不足以证明 GPU page 可访问，也明确 queue submit 不等于 GPU 完成。

## 8. 事实修正与源码克制

### 事实修正

- 将“像素级 LOD”修正为以像素误差衡量、以 group-compatible cluster cut 表达，而不是每像素/每三角形独立 LOD。
- 增加逻辑 DAG、packed hierarchy 与 pages 三分法，避免把 hierarchy node 存在误当页已驻留。
- 将轮廓、遮挡与 HW/SW raster 从 LOD 选择中分离，避免通过 HZB 或 raster path 解释精度错误。
- 将共享 GBuffer 限定为输出合同共享，不推导 classic/Nanite 前半段 feature/stage 完全等价。
- 将 streaming 统一校准为：多来源 request merge和初始 priority -> parent expansion/priority propagation -> `MaxSelectedPages`/pending-budget final selection + registered LRU update -> reusable slot -> pending/registered -> IO/DDC/memory -> CPU install staging -> independent/dependent transcode -> cluster/hierarchy scatter -> GPU completion -> later traversal。
- 明确 requested registered pages 主要更新引用/LRU，new pages 才进入 priority heap形成 `SelectedPages`；不能把所有 parents 写成都会 IO。
- 明确 `SelectedPages` 仍可能因无 slot、legacy request 上限或 staging 空间而没有数据请求。

### 源码克制

正文先建立 cluster replacement、compatible cut、visibility-first 与 streaming state chain，再使用少量 UE symbol 作为调试路标。Builder、manager、uploader、shader 的路径和验证记录全部留在 CoverageMatrix/本报告；正文没有变成函数调用栈或源码阅读顺序。

## 9. 残余风险

- 本轮未运行项目级 GPU capture；main/post HZB、HW/SW overlap、async transcode 和同图 consumer 的实际顺序仍需在目标平台验证。
- Streaming manager 还有 legacy request、staging allocator 等实现级限流项，正文没有穷举；当前主线已覆盖主要 slot 与 budget 边界。
- Builder 的 voxel/shape-preservation 等特殊分支未展开；正文聚焦通用 StaticMesh 岩壁主线。
- 材质功能限制、VSM/Lumen capture 的内部消费者逻辑属于相邻章节，不能由“共享 GBuffer”向外推导。

## 10. 最终记录

- 独立 BODY：**PASS**。
- `FAIL-16-1`：**已关闭**；正文各落点和源码时序一致。
- CoverageMatrix：已从当前最终正文重建，旧结论未继承。
- 原版价值：已逐项保留、迁移、深化或按事实修正；未发现无去向的信息价值损失。
- 状态/公共文件：**未修改**；本报告不声称公共 Gate、章节状态或共享索引已更新。
