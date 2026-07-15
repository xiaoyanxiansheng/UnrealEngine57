# 21 ShaderSystem Teaching Edit Report（按最终正文重建）

## 材料与独立依据

- 最终正文：`21_ShaderSystem.md`，514 行，SHA256 `0268246D92E828F9273343A093B977DA56288CC98BBF6B02853C8F424A3CA390`。
- 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/21_ShaderSystem.md`，422 行，SHA256 `0F78C43EE879ED7B85513C0B9AB56B721E0E083D47778D13AD691B20797C46CA`。
- 指定审计/复核：`.codex/tmp/audit_20_21.md`（840 行）、`.codex/tmp/review_20_21.md`（47 行），均完整读取成功。
- 事实复核：重新打开shader registry、map/category、compile decision、VertexFactory、parameter metadata/binding、MDC/PSO与cook/runtime源码。
- 旧sidecar结论、状态和落点不参与；未以任何章节或第06章作质量标杆。

## 最终正文的真实结构

最终正文用一个LocalVF BasePass draw组织18段收敛链：

1. 定义type registry到MDC/PSO的终点和不包含的queue/GPU深度。
2. 将注册拆成static collection、`CommitAll` construction/registration、`InitializeShaderTypes` derived initialization。
3. 分开`FShaderType`、job、`FShader`、map content/resource、`TShaderRef`与RHI shader。
4. 按依赖解释Global/Material/MeshMaterial category、map owner和GT/RT visibility。
5. 把compile coordinate、layout/caller、VF/shader/material gates与environment顺序分开。
6. 走完VF type -> `FDataType`/components -> streams/elements/declaration -> input streams/submit overrides。
7. 走完metadata/RHI layout -> compiled parameter map -> binding records -> runtime batched write。
8. 让Material/View/Primitive/VF/Pass owners在MDC汇合。
9. 分开minimal PSO recipe、precache/full initializer、active skip、PSO ready、RHI recorded与更深完成。
10. 以cook/runtime/ODSC和last-valid-state收束。

## 原版信息迁移

| 冻结原版教学价值 | 最终处理 | 最终落点 |
| --- | --- | --- |
| HLSL文件不是可查询type来源，C++注册决定shader type | 保留并精修为三阶段registry生命周期 | 第1节 |
| Global/Material/MeshMaterial按依赖分类 | 保留，新增type/result/map/RHI层级与per-VF map owner | 第2-4节 |
| permutation与VF/shader两道filter | 保留，补layout caller、material gate、pipeline/platform条件与environment顺序 | 第5节 |
| VertexFactory有type与instance两重身份 | 扩写为type/data/component/stream/element/declaration/input-stream/resource binding完整链 | 第6-9节 |
| 参数绑定在draw时不按名字查 | 保留，重建为metadata/layout、parameter map、records、runtime batched write | 第10-12节 |
| Material/View/Primitive/VF/Pass多owner参数 | 保留，补publication/lifetime和GPUScene/primitive UB分叉 | 第13节 |
| BasePass取得shader并进入MDC | 保留，补map/resource lifetime、minimal PSO与static/dynamic command边界 | 第14节 |
| MDC build与RHI PSO分开 | 扩展precache、full initializer、async/skip/wait、recorded/submit/GPU完成 | 第15节 |
| cook/DDC/ODSC零散提示 | 新增独立cooked binary/runtime lookup单元 | 第16节 |
| 按失败阶段倒查 | 保留并按last-valid-state重建八类以上路线 | 第17节 |

最终正文比冻结原版增加92个物理行；diff为251 insertions / 159 deletions。没有缩短异常。21-U00至21-U17的原版教学价值均有最终落点；被替换的是“VF binding”“参数反射”“shader ready”等过度合并表达，技术意义被拆入可验证状态链。

## O-D-C-L 教学闭环

| 核心层 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| Type registry | registration instances、`FShaderType`/VF/pipeline registries | lazy accessors、type metadata、derived source/UB state | module phase、CommitAll、InitializeShaderTypes | module/process级 |
| Compiled result | compile manager与shader map content/resource | `FShader`、code/resource index、permutation/VF coordinates | layout/gates/jobs/cook/publication | map/ref-count生命周期 |
| VF input | `FVertexFactoryType`与VF instance/render resources | components、streams/elements、declarations、input streams、UB/SRV | feature/path、InitRHI、MDC build/submit override | type长期；instance/resource随mesh |
| Parameter contract | metadata/layout、compiled map、binding records | C++ offsets、HLSL allocations、resource categories、batched writes | compile/bind validation、runtime SetShaderParameters | metadata长期；batched短期 |
| Draw recipe | mesh processor/MDC/draw-list context | shader refs、declaration、streams、bindings、minimal state、draw args | pass/material/VF selection | cached或one-frame command |
| Platform pipeline | PSO precache/cache/RHI command list | full initializer、platform PSO、recorded state/draw | hit/create/async/skip/wait | PSO cache与command/queue生命周期 |

## 设计推理与替代方案

- C++ registry让compile/cache前拥有完整type metadata；扫描HLSL无法提供constructor/filter/root metadata。显式manifest/codegen可替代静态注册，但增加build step；动态热注册需要版本化layout/map，当前UE模型不支持。
- category/map按真实依赖拆分，避免global shader乘上所有material/VF组合；单一shader asset+keywords API更短，但会隐藏key和missing归属。
- VF把输入策略变成compile axis以支持Local/Skin/Landscape等复用pass/material shader；统一GPU-driven schema/manual fetch可减少VF类型，但会改变shader/resource/PSO模型。
- parameter records将字符串匹配移到compile/bind阶段，draw热路径按紧凑records写入；固定root layout可更快但灵活性更低。
- precache以预测/存储换submit hitch；同步create简单但可能卡顿，async降低阻塞却引入skip/wait，全离线library提高shipping确定性但要求完整覆盖。
- cook将可恢复开发状态变成目标平台确定产物；editor/ODSC适合迭代，不是shipping runtime compile兜底。

## Worked Cases 与 Last-Valid-State

- Registry案例依次检查registration instance、CommitAll后type lookup、InitializeShaderTypes后derived metadata；plugin phase错时停在对应层。
- Type/result案例说明`TBasePassPS` type已注册仍可能没有LocalVF coordinate job/result/map/RT publication/RHI shader。
- VF案例分别检查component offset/stride、declaration attribute解释、stream slot/source、primitive-id override、manual-fetch SRV，拒绝“VF binding错”总称。
- Parameter案例覆盖layout hash变化、optimized-out member不生成record、loose constant/texture/UB/RDG SRV分类与batched write。
- Multi-owner案例用transform/normal/Roughness/UV/pass texture症状定位Primitive/VF/Material/Pass owner。
- PSO案例明确`Active + skip`终止当前draw，不能继续寻找本次state/stream/parameter/draw recording。
- Cook案例对custom VF区分uncooked full compile、ODSC request与cooked shipping missing。

## 最终事实修正

1. 注册由两阶段修正为static collection -> `CommitAll` construction/registration -> `InitializeShaderTypes` derived initialization。
2. 新增`FShaderType -> job -> FShader -> map content/resource -> TShaderRef -> RHI shader`生命周期。
3. 把两道filter扩展为layout/caller、VF gate、shader gate、material gate、pipeline/platform条件的完整decision。
4. VF从type/instance标签扩展为data/component/stream/element/declaration/input-stream/resource binding链。
5. 参数从“提前反射”修正为metadata/RHI layout -> compiled parameter map -> binding records -> runtime batched parameters。
6. vertex declaration、stream source与VF shader resources建立类别护栏。
7. MDC built、full initializer、PSO request/complete、RHI recorded、queue submit、GPU consumed分层；`Active + skip`明确终止当前draw。
8. 新增cooked binary/runtime lookup/ODSC，明确type registered不等于binary cooked。

## 源码克制

正文先用LocalVF draw建立状态链，再用symbols定位真实UE实现。路径、行号、验证清单集中在CoverageMatrix；正文中的symbol均被翻译为owner/data/control/lifetime与debug问题，没有用宏/类列表代替首次教学。

## 独立 BODY 依据

本报告直接读取当前BODY，不以review的PASS作为依据：

- 第54-115行附近完整讲注册三阶段与type/result/map/RHI分层，并分别给last-valid-state。
- 第117-263行按依赖、map owner、GT/RT publication和compile decision推进，没有把category/filter当作全部存在性证明。
- 第265-325行把VF五类相邻概念逐段教学，并用UV/stream slot案例建立类别护栏。
- 第327-371行按metadata/layout、parameter map/records、batched write给出三个完成点和RDG lifetime边界。
- 第373-438行让多个parameter owners与shader refs在MDC汇合，同时明确minimal state不是RHI PSO。
- 第440-479行写出precache/full initializer/active skip/PSO ready/recorded与cook环境分支。
- 第481-514行的last-valid-state表和总结保持相同完成术语，没有把queue/GPU重新压成“执行完成”。

因此当前BODY具备真实结构、O-D-C-L、设计替代、worked cases、last-valid-state、事实修正与源码克制。该结论只服务sidecar重建，不更新章节状态或公共Gate。

## 残余风险

- shader registry/loading phase、VF input策略、parameter binding类别和PSO cache策略均可能随UE版本演进，后续应按稳定symbols重核。
- editor/ODSC成功不能证明目标platform cook coverage。
- 后续章节不得把`MDC built`、`PSO ready`、`recorded`、`queue submitted`、`GPU consumed`重新合并。
