# 17 Lumen Teaching Edit Report

## 1. 报告边界与材料清单

本报告从当前最终正文、冻结原版、指定独立 review 与 UE5.7 源码重新建立。旧 TeachingEditReport 和旧 CoverageMatrix 的结论全部作废，未用于证明当前质量。

物理行数统一使用 `.NET File.ReadAllLines`：

| 材料 | 角色 | 行数 | SHA256 | 读取结果 |
| --- | --- | ---: | --- | --- |
| `Engine/Docs/Tutorial_002_RenderingDeep/17_Lumen.md` | 当前最终正文 | 1195 | `982130C4BF17CC88A17A9486481F1CB3C950E938D842DD69AC10230DEF5F79A6` | 完整读取 |
| `.codex/tmp/renderingdeep_11_24_original_20260713/17_Lumen.md` | 用户声明的冻结原版 | 549 | `AF7ED540BE035E3E31B5ACAF490D8FAAECF50FF9B604F5F20DBC8EEF450D0E04` | 完整读取，未改动 |
| `.codex/tmp/review_17.md` | 指定 BODY review | 53 | `076716598F700BE6378F4323C17954A8D4F9C8EFFAE7713F8802B311B34DA0FF` | 完整读取；B1-R1-A1 与相邻 Screen Probe 边界已独立复核 |
| UE5.7 本地源码与 shader | A 类事实与高风险 feedback | 不适用 | 固定源码快照 | 已核验 raw writer、last-used、compaction、ring/readback、CPU request、Screen Probe 禁写及主线 anchors |

最终正文比冻结原版多 646 行，约增加 117.7%。这只说明没有触发“短于原版 15%”异常，不作为质量证明；原版价值仍按事实单元逐项审计。

## 2. 独立 BODY 结论

**BODY PASS。**

结论由本轮独立读取最终正文、冻结原版、指定 review 并复核 UE5.7 源码/shader 得出，不依赖章节头状态或旧 sidecar。正文已经用“世界表示 -> Surface Cache material/lighting -> 当前 view probe/query -> unresolved-ray fallback -> Radiance Cache -> filter/integration -> reflection/composite -> 各自 history”建立一条完整数据有效性主线；O-D-C-L、设计理由、替代方案、红墙案例和 last-valid-state 证据梯都能支撑主线。

高风险 B1-R1-A1 已关闭：allocator 尝试、raw capacity 接受/丢弃、独立 last-used、成功 raw 子集的 hash/compaction/count、ring submission、readback copy/ready、CPU request 与后续预算全部分开；纯 Screen Probe 禁写 raw feedback、compacted feedback、CPU readback 和 page last-used 的边界在机制、案例、history 与调试 Gate 中一致。未发现 BODY 阻断。

本结论不修改章节状态，也不表示公共 Gate 或共享索引已更新。

## 3. 实际教学主线

1. 先把 Lumen 从“屏幕空间 GI”或“每像素完整 path trace”改写为多表示、多查询、缓存与 history 的成本分配系统。
2. 通过 representation 信息保留表说明 screen history、Surface Cache、SDF/HWRT 与 Radiance Cache分别保留什么、舍弃什么，避免把它们都叫 RT backend。
3. 用 O-D-C-L 分开 `FLumenSceneData`、`FLumenSceneFrameTemporaries`、`FViewState` 和 CPU readback ring 的 owner、数据、推进者与生命周期。
4. Surface Cache 从 Card、Page、Page Table、sub-allocation 和 material/lighting atlases 建立世界侧可分页表示，再通过 import/capture/lighting/extraction 跨帧维护。
5. Feedback 章节把 reflection/visualization producer 的 raw 尝试、容量、last-used、compaction、ring/readback 与 CPU request 建成条件式异步链，同时明确 Screen Probe 只读现有 cache。
6. 当前 view 用规则网格 + 有限 adaptive Screen Probes 共享射线；unresolved ray 经 screen、HWRT/SDF、路径相关 fallback 逐层 compact。
7. Radiance Cache 用 world clipmap cell -> probe indirection 共享低频结果；filter/integration再把 probe-domain radiance 变成 diffuse、backface 与 rough specular signals。
8. 专用 Lumen Reflections 处理高频镜面并拥有独立 history；`DiffuseIndirectComposite` 而不是 trace/gather 才是写入 `SceneColor` 的边界。
9. History Taxonomy 与完整红墙案例说明 camera cut、材质变化、lighting 变化、resource resize 和 readback 未 ready 影响不同 owner。
10. Last-valid-state 证据梯从 path qualification 一直走到 GPU/readback completion，避免随机切 CVar。

## 4. 冻结原版价值审计

| 原版价值单元 | 最终处理 | 最终落点 / 说明 |
| --- | --- | --- |
| 红墙染白地的贯穿案例 | 保留并显著扩展 | 开篇、每个 worked case、第 14 节完整状态链；加入粗糙 specular、镜子、转头、走近和 camera cut |
| Surface Cache 的 Card/Page/Page Table/Atlas | 保留并深化 | 第 3 节；增加设计理由、127/128 边界、sub-allocation、合法 fallback 条件 |
| `FLumenSceneData` / frame temporaries / `FViewState` 三类 owner | 保留并重构 | 第 2、4 节；增加 view/GPU-specific scene data、readback mailbox 与完整 O-D-C-L |
| Card Capture、旧 lighting 重采样、Nanite 接入 | 保留并深化 | 第 5 节；分开 classic `ForceLowestLOD` 与 Nanite `LumenCardCapture`，澄清 opacity 持久层边界 |
| Surface Cache direct/radiosity/final lighting 与预算 | 保留并深化 | 第 6 节；加入 hit-lighting mode 与 last-used 的条件式 priority 角色 |
| Screen Probe 均匀网格 + adaptive 补点 | 保留并深化 | 第 8 节；加入 gather alternatives、O-D-C-L 和 Unity/HDRP 有限迁移桥 |
| Screen -> HWRT/SDF -> fallback 的 trace 顺序 | 保留并精化 | 第 9 节；改写为 unresolved-ray 集合与 compaction，分开几何命中和 lighting valid |
| Radiance Cache world-space probe | 保留并深化 | 第 10 节；补 clipmap、3D indirection、consumer mark、reuse 条件与替代方案 |
| Filtering/Integration 与 composite 边界 | 保留并深化 | 第 11 节；明确 slots 0-2、slot 3、`LightIsMoving`、probe/full-res histories 和 `DiffuseIndirectComposite` |
| Rough specular 与专用 reflections | 保留并重构 | 第 12 节；用信号频率解释 owner、成本、history 和替代方案 |
| 原版“probe/trace 写 feedback，下一帧 CPU 读回” | **按源码事实修正** | 第 7 节及全章一致改为：纯 Screen Probe 禁写；只有合资格 reflection/visualization producer；readback 非固定下一帧 |
| 原版将 feedback 与 last-used 作为同一开口收集闭环 | **拆分并修正** | 第 7.2/7.3：GPU->CPU compacted readback 与 GPU->GPU last-used extraction/import 是两条闭环 |
| 原版未表达 raw/compaction/ring 三个容量边界 | 新增高风险教学 | 第 7 节、History Taxonomy、完整案例、Gate 6/14 |

没有把原版“下一帧补页”的旧表述作为价值保留，因为它对当前 UE5.7 是过度承诺；保留的是“反馈跨越 GPU producer 与 CPU request/capture budget”的教学意义，并改写成准确的非固定延迟、条件式 producer-consumer 模型。其余独特原则、案例和调试判断均有明确落点。

## 5. O-D-C-L 审计

| 系统 | Owner | Data | Control | Lifetime | 判定 |
| --- | --- | --- | --- | --- | --- |
| Lumen Scene / Surface Cache | 当前 view 选择的 `FLumenSceneData` | cards/pages/page table、allocators、material/lighting atlases、pending scene state | scene update、requests、capture/lighting budgets | 跨帧，可 default 或 view/GPU-specific | 清楚 |
| 当前图发布面 | `FLumenSceneFrameTemporaries` / RDG | imported textures/buffers/SRV/UAV 与共享 outputs | RDG dependencies、renderer scheduling、extraction | 当前 graph | 清楚 |
| View histories | `FViewState` | probe-domain、full-res indirect、Radiance Cache、reflection temporal state | validity tests、camera/config change、history extraction | per-view 跨帧 | 清楚 |
| Surface Cache feedback | `FLumenSceneData` feedback object + GPU/CPU stages | raw allocator/buffer、last-used buffers、hash/count、compacted output、readback ring、CPU requests | producer qualification、capacity guards、GPU readiness、CPU filtering/budgets | 非固定延迟跨帧 | 清楚 |
| Screen Probe | 当前 view/frame + `FViewState` history | placement、ray directions、hits/radiance、filtered probe data | uniform/adaptive placement、trace/filter/integration config | 当前图 + 条件式 per-view history | 清楚 |
| Radiance Cache | `FViewState::FRadianceCacheState` | clipmaps、indirection、probe atlases、allocator/last-used/traced | consumer mark、reuse、allocation、trace budget | 条件式跨帧 | 清楚 |
| Reflections | reflection frame outputs + `FReflectionTemporalState` | tile/ray/result、resolved specular、second moment、frame count | reflection method、trace/resolve/denoise/history tests | 当前图 + per-view history | 清楚 |

## 6. 设计理由与替代方案

- **多表示而非单一世界真相**：屏幕历史便宜但无离屏，HWRT/SDF提供命中但不自动提供便宜 lighting，Surface Cache和 Radiance Cache分别缓存表面/世界低频结果。纯 screen-space、纯 HWRT hit lighting、稠密体素/probe grid 都在特定目标下可取，但各自放大信息缺口、动态更新或内存成本。
- **Card/Page/Page Table**：用二维可分页 surface domain降低任意命中点的材质/lighting 成本；静态 lightmap、世界体素和完整 hit lighting 是可行替代，分别牺牲动态性、精细边界或每命中成本。
- **Persistent owner 与 RDG frame view 分离**：让跨帧资源活得足够久，同时让 RDG看见当前图依赖；绕过 import会失去 barrier知识，把 RDG handle 跨帧保存会破坏生命周期。
- **Budgeted capture/lighting**：把成本绑定到变化和需求 subset；全量更新适合诊断或小场景，但常规大场景成本不可控。
- **异步 feedback**：用收敛延迟换取 CPU/GPU 流水和可控预算；预测 prefetch、更大粗层、更高 budget、全 GPU allocation或同帧同步读取分别交换误预测/内存/复杂度/帧时延。
- **规则 probe + 有限 adaptive**：规则基底稳定地址、history 和成本，adaptive 修补边缘；完全规则会漏薄几何，完全 adaptive 会让 allocator/history剧烈变化，每像素追踪则成本随分辨率增长。
- **Unresolved-ray compaction**：后层只处理 miss，避免每个 backend 重扫全部 rays或覆盖已成立结果；代价是 compaction/indirect dispatch。
- **Radiance Cache 独立于 Screen Probe**：让 reflection、hair、translucency等消费者共享 world-space 低频 cache；若由 Screen Probe 独占，其他消费者需重复建 cache或依赖屏幕布局。
- **Rough specular 与 reflection 分流**：宽 BRDF lobe 可复用低频 probe，镜面需高频专用 ray/history；SSR、reflection capture、hit lighting和 path tracing是不同成本/覆盖替代。
- **Signals 与 SceneColor composite 分离**：允许 async gather、独立 reflection、history/debug复用；简单 renderer 可直接写最终颜色，但 UE 通用 deferred path需要更强解耦。

## 7. 案例与调试价值

- **红墙染地**贯穿所有主要 owner：scene cache保存红墙 material/lighting，view probe查询它，reflection另走高频 owner，composite才改变 SceneColor。
- **相机转头**把 screen hit变成 unresolved miss，迫使 world representation接手；同时证明 probe owner没有变化。
- **相机走近**展示高分辨率 page request、物理分配、旧 lighting重采样、capture、page-table发布和 lighting预算的逐步成立。
- **镜子 feedback**精确承载 allocator/raw/last-used/ring/compaction/readback/CPU request；地面纯 Screen Probe则只读现有 cache，两个 consumer不再混写。
- **远处角落**把 Radiance Cache mark/indirection/probe budget/低频 interpolation与 Surface Cache page问题分开。
- **Camera cut**证明 view histories通常失效，但 scene cache不因观察关系变化自动清空。
- **Gate 0-14**能把 path qualification、owner、representation、capture、lighting、feedback、probe、trace、cache、signals、composite、history与完成深度逐层分诊。

## 8. 事实修正与源码克制

### 事实修正

- 修正“所有 Lumen tracing / Screen Probe 都能写 Surface Cache feedback”：UE5.7 纯 Screen Probe 调用 `GetLumenCardTracingParameters(... false ...)`，只把 raw-feedback allocator/buffer 切到 dummy 并把 size 设为 0；last-used UAV 是否绑定真实资源与该布尔值独立。它不写 raw feedback 或 last-used 的决定性原因，是 Screen Probe shader 未启用 `SURFACE_CACHE_FEEDBACK`，相关写块不会编译或执行。
- 修正“raw feedback 写入即 compacted feedback”：sampling只递增 allocator并在容量允许时写 raw packed element；重复 count由后续 hash阶段产生。
- 修正“raw overflow等于 writer未运行”：allocator可记录尝试，element丢弃，但 high-res last-used仍可在内层 capacity guard之外更新。
- 修正“ring满只是晚一点读”：ring满时该 submission在 hash/compaction/readback pass建立前停止，本批 raw不会形成 CPU request。
- 修正“compaction处理全部 allocator尝试”：dispatch按 `min(allocator, BufferSize)`，只消费成功 raw子集；compacted output自身仍有容量。
- 修正“N帧写、N+1帧读”：CPU只消费顺序上 ready的最新 readback，可能 N+1、N+2或更晚，也可能本帧无新数据。
- 修正“ready compacted data必然生成 page”：CPU仍按 hit count、card/page有效性和mapping状态筛选/分流，后面还有 capture/atlas/lighting budget。
- 修正“last-used是 CPU readback”：它是 feedback-enabled producer写入、RDG extraction/import延续、供 GPU lighting priority消费的独立闭环。
- 修正“命中后端就是 lighting后端”和“HWRT绕过 Surface Cache”：命中与lighting有效是两个状态，lighting mode决定是否仍采 cache或执行更完整 hit lighting。
- 修正“所有 history一刀切”：Surface Cache、feedback submission、probe/full-res、Radiance Cache和reflection histories使用不同 key与失效条件。

### 源码克制

正文以 representation、owner、状态流和红墙案例承担教学；`FLumenSceneData`、`RenderLumenScreenProbeGather`、`DiffuseIndirectComposite` 等 symbol只作为定位点。Shader行号、ring字段、compaction kernel与CPU过滤代码全部留在 CoverageMatrix/本报告，没有把 source walkthrough 当作教学路径。

## 9. 残余风险

- 本轮是 UE5.7 本地源码/shader静态回归，未运行项目级 GPU capture；平台、CVar、view method、多 view/GPU与 async调度会改变实际分支。
- Compacted output有固定容量，hash线性 probing也有实现限制；正文没有声称其完整代表全部成功 raw，后续扩写不得抹掉该条件。
- CPU request还受最小 hit count、card/page/mapping有效性、capture数量、atlas空间和lighting budget；不能简化成“accepted raw必然产生并完成 page”。
- `GLumenSurfaceCacheFeedback` 和 producer资格控制当前实现；新增 producer或未来 shader permutation变化时需重新审计 Screen Probe边界。
- HWRT/SDF质量、AS更新成本、card coverage与项目资产特征仍需 runtime profiling/visualization，静态源码不能替代实测。

## 10. 最终记录

- 独立 BODY：**PASS**。
- B1-R1-A1：**已关闭**；raw/last-used/compaction/ring/readback/request与 Screen Probe边界在正文各落点一致。
- CoverageMatrix：已从当前最终正文重建，旧结论未继承。
- 原版价值：准确内容已保留和深化；“Screen Probe写feedback/固定下一帧”作为错误事实被明确纠正，其教学意义迁移到准确的条件式异步闭环。
- 状态/公共文件：**未修改**；本报告不声称公共 Gate、章节状态或共享索引已更新。
