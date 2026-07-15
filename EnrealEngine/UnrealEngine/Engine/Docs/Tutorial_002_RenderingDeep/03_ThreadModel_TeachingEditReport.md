# 03_ThreadModel Teaching Edit Report

> **章节**: `03_ThreadModel.md`  
> **报告范围**: 仅描述当前最终正文的实际结构、双素材处理、事实修正与源码克制；不改变完成状态  
> **重建依据**: 当前正文、`origin.md` 对应 H1 区间、公共规范、UE5.7 源码事实与 06 教学标定

---

## 1. 最终教学结构

本章不再以“三线程名称 + 函数列表”组织，而以一个 Primitive 的 Transform 更新作为贯穿案例，追踪数据、控制权和完成证据跨 GT、RT、Renderer Scene、RHI/backend 与 GPU 的换手。

最终结构分为五层：

1. 先用生产者—消费者链稳定 `work/task`、`queue`、`pipe`、两类 command list、`submit`、`fence`。
2. 解释多时间线的责任分割和逻辑归属，不把 named thread 等同于固定物理线程。
3. 沿 GT 快照、ENQUEUE、Dispatcher、RT 消费、Scene publication、RHI recording、Submit、frame sync 正向推进。
4. 用并行汇合、工具模式、模式切换、启动关闭和数据所有权补足条件与生命周期。
5. 用动态上传 slot A 和移动物体六级排障建立“最后成立状态”调试模型。

全章统一使用六级完成模型：RT queued、RT consumed、Scene published、RHI recorded、Platform submitted、GPU consumed。RHI 内部 dispatch/translate/formation 只解释第 4 级向第 5/6 级推进的内部过程，不形成第二套互相竞争的完成术语。

---

## 2. 双素材融合与信息价值

`origin.md` 与当前分章稿被视为同等候选素材。最终稿保留并融合了两者仍有教学价值的内容：

- Origin 的完整 GT→RT→RHI→GPU 命令推进、Dispatcher 分支、TaskGraph named-thread、Scene 更新、fence/frame sync、并行录制与数据安全。
- 当前稿强化的概念首用、六级完成证据、pipe 状态、模式切换、生命周期排干、资源复用和“最后成立状态”调试表。
- 原源码走读中的技术含义被转译为产品是什么、谁拥有、何时交接、完成证据覆盖到哪里，而不是随路径和行号一起删除。

被合并或移除的是重复定义、重复案例、路径/行号清单、对同一 Submit/fence 结论的多次复述，以及由统一六级模型可以替代的零散阶段命名。没有以整版覆盖或重新摘要的方式丢弃另一份素材。

---

## 3. 大幅缩减信息价值审计

本章在本轮中曾因压缩而触发高风险预警。最终正文约 988 行，Origin 对应区间约 865 行；最终行数不作为质量结论，但说明此前压缩不是最终交付状态。

专项审计确认以下价值已恢复：

- pipe recording、launch、replay、completion event 的状态差异；
- GT 等待期间的 pumping/reentry 与 local queue 语义；
- `ERenderCommandPipeMode` 的条件化退化与运行时切换排干；
- RHI None、DedicatedThread、Tasks 的逻辑归属与物理载体边界；
- 并行 command-list 录制、封口、parent 汇合和 translate；
- 启动、关闭与健康检查作为生产者—消费者生命周期协议；
- 动态上传缓冲 slot 的复用与 wrapper/native 退休差异；
- 从 RT queued 到 GPU consumed 的统一完成证据；
- 移动物体案例的六级排障路线。

因此，当前最终稿的删除项只剩真正重复、过时/不精确表述、越界细节和不承担教学作用的源码载体。未发现仍有价值的 Origin 独有机制、条件、例外、案例或调试判断无最终落点。

---

## 4. 事实修正与边界稳定

最终正文采用以下事实口径：

- `FScene::Update()` 建立 Renderer Scene publication，不称为 Submit。
- Submit 表示 command-list 控制权进入下一消费链；Platform Queue Submit 与 GPU Completion 分开。
- `RHIThread` 可以是逻辑归属，执行载体受模式影响，不保证存在独立 OS 线程。
- pipe completion、RT fence、RHI CPU 安全点和 GPU completion 证据具有不同覆盖范围。
- command-list C++ 类型、逻辑 pipeline 和物理 queue 是三条不同轴。
- 并行 worker 只写其私有产品；共享 Renderer 状态回到 owner 与阶段门修改。
- 资源复用必须匹配最后 GPU consumer，CPU 引用归零或 wrapper 删除不是通用 GPU fence。

本章边界停在时间线、命令交接、完成证据和所有权协议；RHI 资源/PSO/binding/barrier 交给 04，RDG 编排交给 05。

---

## 5. Source Restraint

正文没有源码路径、行号、验证记录或重复“源码锚点”块。保留的 UE 符号只承担两种职责：已经解释过的概念名，或读者调试时可定位真实机制的最小路标。

源码验证集中在 CoverageMatrix 的稳定符号锚点中。正文主线在移除所有函数名后仍然成立：它依赖的是状态变化、数据形态、owner、control transfer、lifetime 和 completion evidence，而不是调用栈记忆。

---

## 6. Worked Cases 与调试价值

贯穿案例是移动 Primitive。局部案例分别承担明确教学任务：

- 连续移动两次：解释 task 与快照身份。
- 500 个更新：解释 queue、pipe 与积压。
- 移动椅子：区分 render command list 与 RHI command list。
- BasePass 主/子列表：解释 record、封口、汇合、translate、formation、submit。
- 资源销毁：解释 fence depth。
- 动态上传 slot A：解释最后消费者、复用和两层退休。

最终排障表不再问模糊的“命令执行了吗”，而是依次查 RT queued、RT consumed、Scene published、RHI recorded、Platform submitted、GPU consumed 的最后成立证据。

---

## 7. 剩余问题

- Gate blocker: none。
- 事实问题: none；正文中的高风险事实已按 UE5.7 源码口径收束。
- 教学问题: none；概念首用、worked case、边界和调试模型达到 06 标定。
- 状态维护: 本报告不修改章节头、`OUTLINE.md`、`SOURCE_INDEX.md` 或 `GENERATION_GUIDE.md`。
