# 19 Virtual Shadow Maps Teaching Edit Report（按最终正文重建）

## 材料与独立依据

- 最终正文：`19_VirtualShadowMaps.md`，633 行，SHA256 `24D77ECCBFD53F3F88ABF7FAFAA3F5435CE007196E8A77458243E77772D71F96`。
- 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/19_VirtualShadowMaps.md`，497 行，SHA256 `DDB030CB43B412825BCE108F7B0193D1C4A590370E39F91DF069D179BEC07773`。
- 指定审计/复核：`.codex/tmp/audit_18_19.md`（274 行）、`.codex/tmp/review_18_19.md`（39 行），均完整读取成功。
- 事实复核：重新打开 `RenderUtils.cpp`、`VirtualShadowMapArray.cpp`、`ShadowSceneRenderer.cpp`、`VirtualShadowMapCacheManager.cpp`、physical page management/page access/projection shaders与 deferred handoff。
- 旧 sidecar 的 PASS、状态、落点和事实归纳均未复用；未以其他章节或第 06 章作为教学标杆。

## 最终正文的真实结构

最终正文沿“需求如何变成可消费阴影证据”推进：

1. 从固定 shadow map 的分辨率/存储耦合切入，先加项目/平台运行资格门。
2. 用太阳、Nanite角色、non-Nanite地面和远处塔灯建立贯穿案例。
3. 分开 virtual address、physical pool、page table，以及 frame-local array 与 persistent cache manager。
4. 教页表 entry、full/single-page/clipmap，并把 VSM ID 定义为帧内地址索引。
5. 按 marking -> allocation -> initialization/raster -> sampling/projection 讲 owner 转移。
6. 在采样端新增 local OPP，区分 directional screen-mask路径。
7. 在缓存端讲 cache identity/ID remap、invalidation、directional/local coarse例外与 pool pressure。
8. 用 gate -> request -> mapping -> depth -> projection -> lighting -> cache 的 last-valid-state 倒查。

## 原版信息迁移

| 冻结原版教学价值 | 最终处理 | 最终落点 |
| --- | --- | --- |
| 固定 shadow map 的分辨率/存储耦合问题与 Virtual Texture 类比 | 保留，并在主线前新增运行门 | 开篇与资格门 |
| 太阳/角色/地面/塔灯贯穿案例 | 完整保留并扩展到 non-Nanite gate、OPP、ID remap、pool pressure | 贯穿案例与后续各节 |
| virtual/physical/page table 与 frame/cache owner | 保留，补 resource ref 与 underlying storage 双生命周期 | 第 1、2、8 节 |
| clipmap/local full/single-page | 保留；“环状存储”修正为 nested levels 覆盖模型 | 第 3 节 |
| 每帧 ID 分配顺序 | 重写为 frame-local ID + persistent cache identity + remap | 第 3、8 节 |
| marking/allocation/render三权分离 | 保留；“当前一个物理页都没有”修正为本帧映射尚未发布 | 第 4-6 节 |
| allocation四列表与选择性初始化 | 保留状态机；“只清新页”修正为 shader判定需初始化/重建的页 | 第 5 节 |
| 缺页可退粗层 | 条件化：仅 `bAnyLODValid` 时成立；完全无映射为 invalid | 第 7 节 |
| cache invalidation与pool resize | 保留；补 local coarse mode 2例外、ID remap和pressure反馈 | 第 8 节 |
| request -> allocation -> render -> sample -> cache倒查 | 保留并扩展 gate、OPP、remap、pressure和完成深度 | 第 9 节 |

最终正文比冻结原版增加 136 个物理行；diff 为 191 insertions / 55 deletions。无缩短异常。原版独有的页模型、case、mark/allocation/render、cache和调试价值均有最终落点；删除内容主要是“稳定 ID”“物理环”“完全缺页必退粗”“只初始化新页”等错误或过度绝对化表达，其可保留意图已迁移到更精确状态链。

## O-D-C-L 教学闭环

| 核心对象 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| 功能资格 | RenderCore/runtime project-platform gate | enable、platform/Nanite support、non-Nanite config | project setting、shader platform、atomics/support | 渲染时判断 |
| 本帧 VSM 组织 | `FVirtualShadowMapArray` | current IDs、page table/RDG refs、request/allocation resources | active lights/views、mark/allocation passes | 当前帧/RDG graph |
| 持久缓存 | cache manager/per-light cache entry | physical pool、metadata、previous IDs/addresses、projection evidence | cache key、invalidation、resize、extraction/import | 跨帧 external storage |
| 页需求 | marking producers | detail/coarse request flags、receiver masks | screen/froxel/MegaLights等需求源 | 本帧 demand |
| 页兑现 | allocation/init/raster passes | list state、page entry、physical depth | pool availability、Nanite/non-Nanite gates、dirty state | 本帧写入；pool可持久 |
| 投影/消费 | projection与deferred lighting | virtual-to-physical result、screen mask或OPP packed bits | directional/local、OPP条件、max-lights | 本帧 lighting |

## 设计推理与替代方案

- 虚拟地址与物理存储分离，让高虚拟分辨率只为实际需求付物理页；传统固定 shadow map在阴影规模小、预算确定或平台不支持VSM时更简单。
- clipmap服务大尺度 directional覆盖；local full提供多页局部细节，single-page适合小屏幕占比的远灯。三者是不同需求形状，不是质量等级表。
- marking、allocation、raster分权使需求、容量和caster submission可独立演进；全量预分配/清理更简单但浪费显存/带宽。
- cache identity与ID remap允许当前活跃集合紧凑重排，同时复用physical pages；固定数值ID会把地址索引误当对象身份。
- coarse pages用低频覆盖换成本；关闭non-Nanite coarse或抑制local dynamic invalidation可省成本，但可能缺失或保留较旧粗影。
- OPP聚合多个local lights以减少逐灯screen-mask pass和带宽；关闭OPP、directional或需传统合成时，per-light projection更通用。
- pool pressure提高global bias以细节换页需求；增池以显存换覆盖。overflow不是无损fallback，必须作为缺影状态处理。

## Worked Cases 与 Last-Valid-State

- 贯穿案例把太阳clipmap、Nanite角色、non-Nanite地面、远处塔灯分别绑定到 directional/local、caster gate、single-page/cache与projection问题。
- ID remap案例：塔灯cache entry保持同一identity，但frame N与N+1数值ID可不同；错投到其他灯先查previous/current ID和address remap。
- Coarse案例：detail正确、远处volume/coarse stale时停在coarse demand/invalidation；directional查clipmap coarse levels，local查mode 2，不能互换。
- Allocation案例：request有效但entry invalid，停在pool/list/allocation；entry valid且`LODOffset>0`是粗页fallback；entry可写但depth旧，查initialize/dirty。
- OPP案例：physical depth正确但第17盏local light缺影，停在configured max/overflow；directional缺影不查local OPP。
- Pool案例：free pages接近阈值后bias上升；仍overflow则当前未服务request可直接缺影，不能声称自动降级已解决。

## 最终事实修正

1. 新增 `UseVirtualShadowMaps` 项目/平台门与non-Nanite独立gate。
2. 把跨帧“保持VSM ID”修正为保持cache identity，并用previous/current ID与physical address remap复用。
3. 把clipmap“物理环”修正为重叠nested levels；环只作覆盖直觉。
4. 把“当前没有物理页”修正为本帧request/mapping尚未建立，persistent pool/cached pages可能存在。
5. 把“只初始化新页”修正为只初始化/重建shader判定所需页。
6. 把“缺页会退粗层”修正为仅有更粗valid mapping时成立；完全缺页为invalid。
7. 新增local OPP packed bits、默认16/硬上限32、overflow与directional边界。
8. 把coarse mode 2限定为local VSM non-detail动态失效抑制；directional clipmap demand独立。
9. 新增2048默认pool、0.85 pressure threshold、2.0 max bias与overflow缺影边界。

## 源码克制

正文的主骨架是页状态与owner交接，而不是源文件/函数列表。`FVirtualShadowMapArray`、cache manager、ID/remap、OPP等符号只在概念已建立后作为定位路标；path、默认值核验和稳定symbols集中在 CoverageMatrix。

## 独立 BODY 依据

不引用review中的PASS作为前提，直接读取当前正文可确认：

- 第 39-56 行附近先建立运行资格与失败回落，避免在unsupported状态从page marking起查。
- 第 112-286 行完整区分virtual/physical/page table、frame/cache owner、clipmap/local与frame-local ID/remap。
- 第 288-462 行按marking、allocation、raster、sampling推进具体数据状态，并给出request/entry/depth不同完成点。
- 第 464-482 行把OPP限定为local packed-mask路径，保留directional screen-mask边界。
- 第 484-552 行按cache identity、remap、directional/local coarse差异和pool pressure解释跨帧复用。
- 第 554-625 行的last-valid-state表覆盖gate、request、mapping、depth、projection/OPP、lighting、cache，没有用bias/filter替代前置证据。
- 第 627-633 行回放保持同一owner/data链，没有新增未教学结论。

因此当前 BODY 内部已有真实结构、O-D-C-L、设计替代、worked case、last-valid-state和事实条件。该判断仅为sidecar重建依据，不修改章节状态、公共文件或Gate。

## 残余风险

- local `MarkCoarsePagesLocal=2`、directional clipmap coarse demand与single-page特殊失效条件必须持续分开。
- OPP configured max与overflow、dynamic-resolution反馈滞后均可能随项目设置改变表现。
- 默认CVar不是跨项目保证；后续版本升级应以本表symbols重新核验。
