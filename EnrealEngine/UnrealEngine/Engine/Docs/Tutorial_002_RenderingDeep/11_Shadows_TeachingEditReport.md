# Teaching Edit Report - 11_Shadows.md

> 重建依据：最终正文 1180 行、冻结原版 549 行、`audit_11_result.md`、`review_11_12.md` 与必要 UE5.7 源码。  
> 本报告描述已经存在于最终正文中的教学编辑结果；不修改章节状态，不声称公共 Gate、`OUTLINE.md` 或 `SOURCE_INDEX.md` 已更新。

## 1. 最终主线

最终正文不再按 CSM、VSM、MegaLights、Contact Shadow 并列成方案百科，而以“为 Lighting 生产可消费的遮挡证据”为唯一主线：

```text
light request -> route -> per-view projected visibility
-> projected-shadow contract -> caster gather -> resource/cache
-> ShadowDepth producer -> projection/filtering
-> Lighting shadow terms -> last GPU consumer completion
```

CSM、regular atlas、VSM、MegaLights、Contact 与 RT shadow 都被放回 route、producer、resource 或 consumer 的具体分支，不再被解释成单向质量等级。

## 2. O-D-C-L 与过程教学

- **Owner**：区分 frame-local `FProjectedShadowInfo`、scene-owned regular cache、VSM cache manager、本帧 VSM array、RDG resource 与 Lighting consumer。
- **Data**：区分 request flags、visibility maps、subject primitives、draw recipe、GPUScene/instance data、atlas/page content、projection mask 与 shadow terms。
- **Control**：route、projected visibility、mesh selection、cache mode、instance culling、RDG dependency 和 consumer binding 逐层推进。
- **Lifetime**：从 request 的单帧决策，到 regular/VSM 跨帧 cache，再到必须覆盖最后 GPU consumer 的资源退休条件，均明确分层。

最终正文反复声明“当前状态能证明什么、不能证明什么”，避免把对象存在、命令形成、queue submit、GPU 写入和 Lighting 消费混成一个“完成”。

## 3. 设计理由与替代方案

正文实际补齐了每个承重设计的动机和代价：

- 先 route 再建 shadow work，避免无消费者的 view、caster、atlas 和 projection 成本；替代是固定 shadow map、纯 RT 或预计算路径。
- light/view/caster 三层可见性分离，保证屏幕外 caster 不漏影；替代是每个 shadow view 全场景遍历。
- atlas 与 border 交换纹理数量、绑定和碎片成本，同时承担过滤隔离；替代是每灯 texture、array 或 bindless。
- static/movable cache layering 避免静态几何每帧重画，又不让动态树留下旧影；替代是整图重画或更细粒度 page cache。
- depth producer 与 projection producer 分离，因为 light-space depth 不是 receiver-space attenuation；替代是 light shader 直接查询相关 shadow data。
- RDG 延迟编排保留 CPU/GPU 并行和资源复用；逐 pass flush/fence 更易观察但破坏吞吐。

正文同时标明哪些是几何/生命周期硬约束，哪些只是 UE5.7 的工程组织选择。

## 4. Worked Case

最终贯穿案例固定为一盏 Directional Light、静态建筑、动态树和一个地面像素。它实际承载：

- request/route 与第二 cascade 的空间成立；
- 主视图不可见但仍是 shadow caster 的动态树；
- 建筑 static layer 与树 movable layer 的更新责任；
- atlas 或 VSM mapping、shadow-view GPUScene、instance culling 与 depth production；
- receiver projection、filter、attenuation 通道和 `GetShadowTerms` 消费；
- 从 RDG 声明到最后 GPU consumer 的完成深度。

MegaLights local-light 群、桌腿 Contact Shadow 和 RT shadow 只作为局部分支案例，没有打断太阳主线。

## 5. Last-Valid-State

最终正文建立了可执行证据梯：light visible -> route -> projected object -> per-view visibility -> subject primitive -> cache/allocation -> mesh command -> GPUScene/instance culling -> RDG/RHI/platform queue -> GPU depth -> projection mask -> Lighting consumption -> final consumer completion。

三个坏结果把证据梯用于实际判断：动态树影停住时查 movable layer/GPUScene；atlas 轮廓正确但地面仍亮时转查 projection/binding；下一帧缺块时查 cache/page identity、invalidation 与 lifetime。

## 6. 事实修正

相对冻结原版，最终正文实际修正了：

- `FProjectedShadowInfo` 只覆盖 projected-shadow 家族及部分 VSM 接入，不是所有阴影技术统一对象。
- deferred BasePass 后 ShadowDepth 不是全路径定律；forward 会更早生产 regular shadow maps，UE5.7 forward 不支持 VSM。
- VSM physical pool 的跨帧 owner 是 cache manager；frame array 负责本帧 RDG 引用和状态推进。
- VSM 可 coarse/保守 marking，不保证只生产逐像素最小需求页。
- shadow depth 可能复用 static cache、只补 movable layer或跳过合法缓存写入。
- Contact Shadow “不产生资源”只适用于 inline 分支；standalone/mobile 可先生产 mask。
- local VSM 只有在严格条件下可让 Lighting 直接读 packed mask bits；不能推广为全部 VSM。
- `bShadowDepthRenderCompleted` 和 RDG external-access submit 都不是 Platform Queue Submit 或 GPU fence。

## 7. 原版价值迁移

冻结原版的独有价值均有最终落点：

| 原版价值 | 最终迁移 |
| --- | --- |
| “阴影是一张证据网/所有权问题” | 提升为 request-to-consumer 的全章主线 |
| CSM split、transition、bounds 四步与失败模式 | 4.1-4.5，并补 request、替代和案例条件 |
| 动态树从 projected shadow 到深度的微案例 | 扩成贯穿 route、cache、GPUScene、projection、Lighting 的主案例 |
| VSM 先 request 后生产 page | 保留并补 mapping、pool owner、cache、projection、lifetime |
| MegaLights sample ownership | 降为接口分支，保留 sample-driven request 与唯一消费权 |
| Contact Shadow 桌腿案例和负 length 陷阱 | 保留 screen-space 边界，并修正 inline/standalone 范围 |
| 方案选型和最终排查线 | 重写为 route 对照、完成阶梯和 last-valid-state |

没有把 introduction、mechanism、worked case 和 debug recap 误判为重复而删除。

## 8. 源码克制

正文先建立语言模型，再使用 `GetLightOcclusionType`、`InitProjectedShadowVisibility`、`FinishDynamicShadowMeshPassSetup`、`RenderShadowDepthMaps`、`RenderProjection`、`GetShadowTerms` 等少量符号作为定位锚点。源码路径、行号、调用栈和验证记录留在 sidecar/review，不让源码列表承担教学。

## 9. BODY Review 依据与残余风险

`review_11_12.md` 对当前 1180 行正文给出 BODY PASS，并完成列明的 UE5.7 高风险事实回归；旧 sidecar 未参与判定。残余风险集中在 VSM packed-mask 条件、cache invalidation、MegaLights/RT 分支和平台路径变化。第 18/19 章算法细节有意不在本章展开。

本报告只重建教学编辑记录，不改变正文、章节完成状态或公共文件。
