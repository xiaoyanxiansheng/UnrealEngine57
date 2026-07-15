# 04_RHI Teaching Edit Report

> **章节**: `04_RHI.md`  
> **报告范围**: 仅描述当前最终正文的实际结构、双素材处理、事实修正与源码克制；不改变章节完成状态  
> **重建依据**: 当前正文、`origin.md` 对应 H1 区间、公共规范、UE5.7 源码事实与 06 教学标定

---

## 1. 最终教学结构

本章从 Unity 读者容易形成的“RHI 就是 UE 版 CommandBuffer”误区出发，把 RHI 重新建模为六类相互约束的合同：后端、资源、状态、Draw、命令和完成。

正文使用两条互相交叉但不等价的状态线：

- 命令线：Record → Translate → Platform Command Formation → Queue Submit → GPU Completion。
- 资源线：Desc → Initializer/Writable Range → Finalize → Wrapper → Native 使用 → Wrapper/Native 分层退休。

贯穿案例是一颗红色粗糙金属 mesh 使用动态上传环形缓冲完成 draw。它迫使每个概念回答：当前产物是什么、谁拥有、下一消费者是谁、何种证据成立、此刻仍不能推断什么。

---

## 2. 双素材融合与信息价值

`origin.md` 与当前分章稿作为同等候选素材处理。最终正文吸收了 Origin 的后端选择、资源描述、两阶段创建、引用计数/延迟删除、transition、PSO、binding、命令列表层级、命令节点回放和平台后端路径；同时采用当前稿的合同模型、五阶段命令、wrapper/native 双生命周期、三轴队列区分和“最后成立状态”调试结构。

源码走读没有直接消失：

- 创建函数链被转译为资源合同与创建事务。
- 命令节点 Execute 路径被转译为 Record/Translate 的产品与控制权变化。
- 平台 context 调用被转译为 Platform Command Formation。
- 引用计数与 pending delete 被转译为 wrapper retirement，并与 native retirement 分离。
- Submit 和 fence 相关细节被转译为有范围的 completion evidence。

合并或移除的是重复路径/行号、平台 API 罗列、字段清单和不推进主线的调用顺序。所有仍有教学价值的机制、条件和调试判断都有最终落点。

---

## 3. 五阶段 RHI 命令模型

最终正文明确区分：

1. **Record**：平台无关 RHI work 已记录并封口。
2. **Translate**：executor/context 已消费 RHI work。
3. **Platform Command Formation**：后端已形成可提交 native command packages。
4. **Queue Submit**：目标平台 queue 已接管工作。
5. **GPU Completion**：匹配最后消费者的完成证据已满足。

事实修正重点是禁止跳级推断：FinishRecording 不等于 translate；native command 存在不等于 queue submit；Queue Submit 不等于 GPU Completion；GPU completion evidence 也只覆盖其声明的 queue、value 和 consumer 范围。

该模型与 03 的六级全局完成梯子一致：04 的五阶段位于 03 的 RHI recorded、Platform submitted、GPU consumed 之间，作为 RHI/backend 内部展开，不与 Scene publication 混用。

---

## 4. Wrapper / Native 双生命周期

最终正文把 RHI 资源退休拆为两条独立但协调的生命周期：

- **Wrapper lifetime**：由 CPU 引用、pending delete、final check 和 C++ deletion 管理，回答 UE 是否还需要这个包装对象。
- **Native lifetime**：由平台工作引用、GPU 最后使用和 backend retirement 管理，回答 native object/allocation 何时可回收。

引用归零、wrapper 进入 pending delete 或 C++ wrapper 删除都不能作为通用 GPU fence。反过来，某个上传 slot 已可复用，也不代表整个 wrapper 或 native allocation 应销毁。最终 worked case 把 slot reuse、wrapper retirement 和 native retirement 分别落到最后消费者证据上。

---

## 5. 事实修正与边界稳定

最终正文采用以下稳定口径：

- RHI 是合同层，不重新执行 Renderer 的 visibility 或材质决策。
- desc 表达需求，不规定唯一 native heap/layout。
- Finalize 交付 wrapper，不证明初始数据已被 GPU 消费。
- transition 必须描述资源范围、前后 access 和相关 pipeline；资源存在不等于访问合法。
- PSO recipe、cache entry 和 native ready 是不同状态；具体异步策略受平台和配置影响。
- Shader Binding 区分参数值、uniform、resource、view/sampler 和 descriptor handle。
- command-list C++ 类型、logical pipeline、platform queue 是三条不同轴。
- Bypass 只缩短 CPU record/dispatch 路径，不绕过 GPU 异步执行。
- 安全复用与回收必须匹配最后消费者，不以“CPU 已提交”或“引用归零”替代。

章节边界停在 RHI 原语与完成深度；RDG 如何自动编排 pass、barrier、alias 和 AsyncCompute 交给 05，MeshDrawCommand 的生成交给 07。

---

## 6. Source Restraint

正文没有源码路径、行号、验证日志或密集调用栈。UE 符号只在概念已经用自然语言建立后出现，承担最小的实现定位和调试路标职责。

高风险事实的稳定符号锚点集中在 CoverageMatrix。移除函数名后，正文仍能靠合同、状态产物、owner、control transfer、lifetime 和 evidence 完整成立，因此源码没有承担主教学。

---

## 7. Worked Case 与调试价值

红色金属 mesh 案例串联资源声明、初始数据写入、Finalize、状态转换、PSO ready、binding、Record、Translate、Platform Formation、Queue Submit、GPU Completion 和分层退休。

调试模型采用“寻找最后成立状态”：

- wrapper 已创建但内容全零，回到 initializer/writable range；
- draw 已 recorded 但抓帧无 native command，检查 translate/formation；
- native command 已存在但无结果，检查 queue submit、state 和依赖；
- 首帧 hitch，区分 PSO recipe、cache entry 与 native ready；
- 上传槽污染，检查 GPU completion 是否覆盖最后读取。

这使读者不再用一个模糊的“RHI 成功了吗”覆盖资源、命令和完成的多个深度。

---

## 8. 剩余问题

- Gate blocker: none。
- 事实问题: none；高风险事实已按 UE5.7 源码口径收束。
- 教学问题: none；五阶段命令模型、双生命周期、worked case 和调试证据达到 06 标定。
- 状态维护: 本报告不修改章节头、`OUTLINE.md`、`SOURCE_INDEX.md` 或 `GENERATION_GUIDE.md`。
