# 20 MaterialPipeline Teaching Edit Report（按最终正文重建）

## 材料与独立依据

- 最终正文：`20_MaterialPipeline.md`，581 行，SHA256 `9E076AC693F5572E58E0E6CD0A6B3D4C2EC12BF0C29BB08EC0669A849B353A18`。
- 冻结原版：`.codex/tmp/renderingdeep_11_24_original_20260713/20_MaterialPipeline.md`，466 行，SHA256 `459CDEFEBBD6C5ADBAA8C5298F18FBBB54139561D37E7ADAD62FF03EDE03027C`。
- 指定审计/复核：`.codex/tmp/audit_20_21.md`（840 行）、`.codex/tmp/review_20_21.md`（47 行），均完整读取成功。
- 事实复核：重新打开material object/proxy、ShaderMap identity/cache/compile、legacy/new translation、layout/gates/jobs、BasePass/Nanite/Substrate consumers、cook/fallback与PSO边界源码。
- 旧sidecar内容不作为事实、迁移或BODY证据；未用任何章节或第06章作质量标杆。

## 最终正文的真实结构

最终正文用一份红色金属材质走完十六段生产与消费链：

1. 先固定终点为“可用ShaderMap和consumer输入”，并给出static switch/runtime value/texture三类变更预期。
2. 建立`UMaterial* -> FMaterial -> FMaterialResource -> FMaterialRenderProxy -> FMaterialShaderMap`责任链与线程/lifetime。
3. 解释material include复用、ShaderMapId与完整DDC identity、缓存/异步编译状态机。
4. 讲legacy/new translation、Material.ush、UniformExpressionSet与shared/per-job environment。
5. 把identity、layout candidates、compile coordinate、material gates、job/result与consumer request逐层分开。
6. 跟踪job回填、finalize、material-relative complete、GT install与RT publication。
7. 讲runtime parameters经proxy/cache/UB/resources发布，不把改值误当改代码。
8. 分别讲traditional BasePass、Nanite programmable raster、Nanite BasePass shading与Substrate output contracts。
9. 以cook/fallback/PSO/completion ladder收束，再按last-valid-state倒查。

## 原版信息迁移

| 冻结原版教学价值 | 最终处理 | 最终落点 |
| --- | --- | --- |
| 红色金属材质贯穿案例与static/runtime分叉 | 保留并扩展texture、Nanite/Substrate、cook/PSO症状 | 开篇、第十一至十六节 |
| 资产、render resource、proxy分层 | 从“四类对象”重建为五类责任链，修正`FMaterial`/`FMaterialResource`同义化 | 第一、三节 |
| 生成Material.ush而非完整pass shader | 保留，补设计替代与consumer边界 | 第二、六、七节 |
| ShaderMapId与DDC lookup | 保留，分开显式字段、间接hash、完整DDC key与runtime values | 第四、五节 |
| legacy translation和compile environment | 保留，新增new IR、shared/per-job污染边界 | 第六至八节 |
| “四层permutation” | 类别错误被拒绝；迁移为identity/layout/coordinate/gate/job/request决策链 | 第九、十节 |
| 结果落入ShaderMap/DDC | 保留，扩展partial/finalized/complete/GT/RT publication | 第十、十五节 |
| runtime Roughness案例 | 保留，补proxy cache、preshader、UB/resource与texture对照 | 第十一节 |
| BasePass只lookup现有shader | 保留，补editor/ODSC只推进未来状态与PSO边界 | 第十二节 |
| Nanite同材质不同执行形态 | 重写为programmable raster与BasePass shading两个consumer | 第十三节 |
| Substrate仅translator gate | 扩展为compilation output、storage/tile、BasePass/Nanite output contract | 第十四节 |
| 倒序调试路线 | 保留，升级为completion ladder和last-valid-state evidence | 第十五、十六节 |

最终正文比冻结原版增加115个物理行；diff为266 insertions / 151 deletions。没有缩短异常。所有20-U00至20-U17原版单元都已在最终结构中有落点；删除的是责任混淆、类别合并和完成深度过度表述，独有教学价值被迁移而非丢弃。

## O-D-C-L 教学闭环

| 核心层 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| 资产/实例 | UObject material/interface/instance | graph、static/runtime overrides、usage | GT edits、inheritance、cook | asset/package生命周期 |
| 渲染材质 | `FMaterial`/具体`FMaterialResource` | compile parameters、identity、GT/RT map refs | cache/compile/publication | ref-count/deferred destruction |
| 实例消费入口 | `FMaterialRenderProxy` | current values、uniform-expression cache、fallback material | invalidation/deferred update | proxy/render resource生命周期 |
| 编译产物 | `FMaterialShaderMap` content/resource | per-type/permutation/per-VF results、compilation output | layout/gates/jobs/finalize | shared/ref-counted，GT/RT发布 |
| runtime参数 | proxy/preshader/uniform cache | constants、textures/VT/resources、UB | scalar/vector/texture setters与cache serial | 高频更新，独立于code identity |
| consumers | BasePass/Nanite/Substrate | shader refs、bindings、output contracts | pass/VF/platform/path conditions | draw/pass/PSO下游生命周期 |

## 设计推理与替代方案

- 图与pass shader分离让一份material include复用于多个pass/VF；为每材质生成完整pass shader模型更直观，但会放大重复、key与compile成本。
- identity/layout/gates/jobs分层让cache复用、material usage、pipeline sharing和增量编译各自控制；中央大规则表更易审计，但耦合和扩展成本更高。
- runtime values从identity分离避免每次改Roughness/texture重编；GPU-side parameter tables适合大量同构实例，但仍需layout、residency与publication。
- Nanite将coverage与完整material shading分开以减少不可见工作；传统VS/PS BasePass对non-Nanite mesh更合适。
- Substrate用更复杂closure/storage/tile contract换layered BSDF表达；legacy fixed GBuffer在简单shading model上成本更低。
- editor full compile/ODSC提高迭代性；cooked shipping依赖预生成coverage以换确定性。运行时JIT不应被当成所有平台兜底。

## Worked Cases 与 Last-Valid-State

- Static switch改变时，最后有效状态若停在旧identity，下一步查ShaderMapId/DDC/layout/jobs；runtime Roughness或texture改变时，先查proxy/cache/UB/resources，不先触发compile。
- LocalVF BasePass job案例按job result -> per-VF map insertion -> finalized -> complete for material -> GT installed -> RT visible推进。
- BasePass fallback案例分开proxy fallback、missing coordinate、editor future request与PSO active skip。
- Nanite worked cases分为coverage/depth错（raster consumer）与depth正常/material错（shading bin/BasePass CS/work graph）。
- Substrate案例从translation output、closure/storage requirements、BasePass targets到final buffer writes逐层验证。
- Cook案例按editor/ODSC/requires-cooked/普通material/default-required material给确定分支。

## 最终事实修正

1. `FMaterial`与`FMaterialResource`不再交替当同义词；resource数量与配置关系改为条件式。
2. ShaderMapId、完整DDC key、layout、compile coordinate、material gate、job与lookup request分开命名。
3. async compile由二态改为inline/in-process/DDC/job cache/in-flight/partial/finalized/complete/publication状态机。
4. cached layout只给候选；具体`Material->ShouldCache`决定material-relative requirement；gate通过不证明job/result/binary存在。
5. Nanite由单一“不同执行形态”改为programmable raster与BasePass material shading两个consumer。
6. Substrate从translator gate扩展到compilation output和consumer/storage contract。
7. cooked missing按环境与material重要性明确fallback/fatal；不再列模糊可能性。
8. shader map完成、RT publication、shader ref、PSO ready、recorded、queue submitted、GPU consumed分层。

## 源码克制

正文先讲责任链、状态机和红色材质案例，再引入必要UE symbols。源文件、函数清单、验证记录只保留在CoverageMatrix；即使删除正文中的符号名，读者仍可沿object responsibility、identity/gate/job、consumer与completion梯度复述主线。

## 独立 BODY 依据

本报告直接读取当前BODY，而非引用review结论：

- 第71-182行附近建立五类对象、GT/RT/lifetime和identity，明确`FMaterialResource`只是具体`FMaterial`实现。
- 第184-328行把cache/translate/environment/identity/layout/gates/jobs按生产状态分开；第309-323行明确candidate membership与specific material gate、job existence不是同一证据。
- 第330-420行给job回填、GT/RT publication与runtime proxy/UB的具体状态转换。
- 第422-503行分别建立BasePass、Nanite双consumer和Substrate output contract，并用局部worked cases承担首次教学。
- 第505-543行给cook/fallback确定分支和完整completion ladder，没有把PSO/GPU写成material侧完成。
- 第545-581行的last-valid-state表与前文对象/状态一一对应。

因此最终BODY内部具备真实结构、O-D-C-L、设计理由/替代、worked cases、last-valid-state、事实修正与源码克制。这里只记录BODY依据，不改章节状态或公共Gate。

## 残余风险

- material layout/gate/job策略、Nanite执行形态和Substrate storage配置都可能随UE版本演进，后续升级需沿稳定symbols重核。
- ODSC/editor行为不能用于证明shipping cook coverage。
- `complete for material`不得在后续文档中被简写成PSO或GPU完成。
