# Teaching Edit Report - 06_GPUScene.md

> 本报告按最终正文重建，不继承旧报告结论；不修改章节状态或公共状态文件。

## 1. 最终教学结构

- 唯一主线由七个状态门组成：Scene publication、identity、allocation、upload set、composite resource window、producer work、resource entrances、Instance Culling/shader consumption。
- 四本账把 Scene identity、GPU allocation、upload/publication、draw consumption 分开，避免把相似数字和 ID 名称当成同一身份。
- PrimitiveSceneData、InstanceSceneData、optional payload 与 Large World 均作为数据合同教学，不按字段或源码文件走读。
- 800 叶草贯穿主线，四个变体分别验证局部更新、realloc、远距精度和 GPU-driven consumption。
- 调试统一为“寻找最后成立状态”，从 Scene publication 一直检查到 RHI/GPU consumption。

## 2. 816 → 473 大幅缩减审计

- 已执行 45 项必保教学价值审计，结果为 45/45 有最终落点。
- 被压缩内容主要是重复名词回看、重复调用链图、字段/常量清单、源码路径与验证文本，以及同一事实的多轮改述。
- Origin 独有的 ID 分层、slot 分配、dirty/upload、Primitive/Instance ABI、Large World、dynamic primitive、Instance Culling 和远距草调试均被保留。
- 当前稿新增或强化的七门、四账、composite publication window、resource entrances、realloc/clear 责任和九级证据链均被保留。
- 技术含义没有随源码走读载体一起删除；它们被迁移到状态门、概念护照、案例变体、边界条件和调试判断。
- 行数减少不是验收理由；本章通过的依据是独有信息完整、模型更统一、重复载体减少。

## 3. 事实修正与安全边界

- 不再把 GPUScene 描述成单一矩阵数组；primitive、instance、payload 与 lookup parameters 构成跨 buffer ABI。
- 不再把 “PrimitiveId” 当成可互换名称；persistent identity、packed index、instance slot、relative instance ID 与 draw-side ID 分账。
- 不再把 allocation、dirty、upload、RDG producer、resource entrance 或 queue submit 写成同一个“更新完成”。
- “版本”被修正为 composite consistency window：resources、capacity、layout、ranges、entrances 与 producer ordering 必须匹配。
- Free 被明确为释放逻辑所有权和登记旧 range 责任，不保证 GPU 数据立即消失。
- Dynamic primitive 被限制在 collector/commit 的一帧发布窗口，不赋予长期 Scene identity。
- Instance Culling 被定义为消费 GPUScene 和 draw metadata 的系统；`InstanceIdsBuffer` 是可见实例 indirection，indirect args 不是 draw completion。

## 4. 06 标定质量

- 每个承重概念都具备 what / why / owner / data / control / conditions / boundary / debug。
- Scene、GPUScene、RDG、Instance Culling、RHI/GPU 的责任转移连续，没有用单一“上传”覆盖多个完成深度。
- 数据生命周期从 Scene residency 到 persistent identity、allocation、one-frame upload、dynamic range 和 GPU consumption 清晰分层。
- 概念首次出现先用语言模型和账本安顿，再给 UE 符号作为定位点。
- Worked case 真正改变状态：单叶移动改变 upload set，扩容改变 allocation，远距抖动检验坐标合同，GPU-driven 为零检验消费账。
- 调试路线能根据症状判断最后成立层，而不是从 shader、material 或重新分配 identity 随机试错。

## 5. 源码克制状态

- 正文未保留源码路径、行号、验证记录或密集字段清单。
- UE 符号只承担必要的概念命名与调试定位，不承担主教学。
- 源码核验留在 CoverageMatrix / SOURCE_INDEX，正文将实现事实转译为身份、数据、所有权、时序与失败证据。

## 6. 跨章衔接

- 与 02 对齐：`FScene::Update()` 建立 Scene publication，GPUScene 不替代 Component/Proxy/SceneInfo 生命周期。
- 与 05 对齐：clear/scatter 是 RDG producer work；resource entrance 与 consumer ordering 属于图内合同，不等于 GPU completion。
- 与 07 对齐：GPUScene 提供 primitive/instance 数据入口，MeshDrawCommand 提供 pass draw 状态，两者不互相替代。
- 与 08 对齐：本章解释生产与消费合同，Frame Init 负责把 collector、upload、uniform 和 culling context 放入阶段门。

## 7. 剩余问题

- 无事实、信息价值或教学阻断项。
- 45 项大幅缩减审计完整记录于 `06_GPUScene_CoverageMatrix.md`。
- 未修改正文、章节状态、`OUTLINE.md`、`SOURCE_INDEX.md` 或 `GENERATION_GUIDE.md`。
