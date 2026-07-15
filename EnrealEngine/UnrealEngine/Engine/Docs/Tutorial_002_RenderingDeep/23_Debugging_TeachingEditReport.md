# Teaching Edit Report - 23 Debugging

> 本报告描述当前 713 行最终正文实际形成的教学结构。冻结原版只作为信息价值基线；旧 sidecar、章节状态和公共 Gate 不参与结论。未使用其他章节作质量标杆，未读取或使用第 06 章。

## 1. 实际主线

当前正文的主线是：

`symptom routing -> choose highest-information owner -> output/pass/resource/capture/timeline/feature-shader/CVar evidence -> controlled reproduction -> last-valid-state -> completion depth`

核心变化是把“从屏幕逐层向下查”的固定五层流程改成症状驱动路由。黑屏、旧值、pass消失、GPU hang、shader missing、CVar无效从不同owner进入，但最终都使用同一套证据上限与完成词表收口。

## 2. 冻结原版价值与迁移

| 冻结原版独特价值 | 当前正文迁移位置 | 处理结果 |
| --- | --- | --- |
| 先判断证据层，再选工具 | 31-55、87-100、126-164 | 保留并改为高信息量路由 |
| output/pass/resource/timeline/feature-shader分解 | 168-465 | 保留；取消固定顺序要求 |
| 合成纹理黑屏贯穿案例 | 20-29、112-164、585-607 | 保留为主案例 |
| RDG参数、裁剪、validation/clobber入口 | 190-255 | 保留并补每种工具的证据边界与侵入性 |
| RenderDoc/Insights/GPU Visualizer互补 | 259-399 | 保留并补capture边界、owner与完成深度 |
| DebugView/Visualize与shader permutation | 403-465 | 保留并补ODSC-only与shader map状态链 |
| CVar不是只看当前值 | 466-515 | 保留并扩展为六段生效链 |
| 六条常见回溯路线 | 581-683 | 保留；每条路线输出last-valid-state与下一测试 |

冻结原版“pass名是共同坐标”和固定层级顺序等危险表述没有保留为价值，而是被条件化或重写。

## 3. O-D-C-L 教学模型

| 证据单元 | Owner | Data | Control | Lifetime |
| --- | --- | --- | --- | --- |
| 症状路由 | 最可能失效的系统owner | symptom、frame/build/RHI、last good output | 选择最高判别力测试 | resource、queue、cache各有独立时间轴 |
| RDG pass/resource | graph compiler/validation | manifest、producer/consumer、access、clobber pattern | retention、validation、clobber | transient/alias与额外clobber producer改变寿命 |
| Capture | provider、immediate list、RDG builder | begin/end边界、native command、resource snapshot | next-frame/immediate/RDG-local | 请求时刻与实际capture frame分离 |
| Timeline | CPU trace、GPU profiler、queue/breadcrumb | task/wait、scope、marker、fence | trace/profile/crash instrumentation | profile可改变运行形态；fence绑定queue/payload |
| Feature/shader | feature branch、ODSC、shader map | mode、permutation、shader availability、output | build/cook/ODSC/recompile | request、compile、map可用与目标frame分离 |
| CVar | console manager、sink/cache、consumer | flags、set-by、shadow、cached/consumed value | priority、sink、restart/rebuild/recreate | 对象值、线程值、资源状态在不同时间生效 |

## 4. 设计理由、替代与重访条件

| 选择 | 选择理由 | 替代 | 何时重访 |
| --- | --- | --- | --- |
| 症状路由 | 避免在错误层采集低价值证据 | 黑屏可从output回溯；hang直接从queue/crash证据进入 | 新证据改变最可能owner时立即换路 |
| Validation优先 | 对RDG合同问题歧义较低、侵入性较小 | clobber或capture | validation无报错但仍怀疑内容/时序时 |
| Clobber单独启用 | 让未覆盖区域可见 | capture资源历史 | 额外clear改变graph、barrier或复现时序时撤回 |
| Capture边界按问题选 | 完整帧、immediate、RDG-local各自控制范围 | trace/profile/breadcrumb | capture改变现场或不能证明queue完成时 |
| ProfileGPU只做定位 | scope/timestamp可解释 | normal-run trace、vendor profiler | 需要正常async/parallel性能结论时必须回normal run |
| CVar逐段审计 | 当前值可能未到consumer | restart/rebuild/recreate或直接检查owner状态 | flags/set-by接受但症状不变时继续追sink/cache/consumer |

## 5. 案例结构

**主案例：合成纹理黑屏**

- 先确认最终输出consumer，再判断pass是否declared/retained/recorded，随后检查resource producer、capture snapshot、timeline与shader/CVar条件。
- 每一步只把证据推进到相应完成层，不从“看见pass名”直接跳到“GPU完成”。

**六条路由**

1. 黑屏：output/consumer优先。
2. 未初始化、NaN、旧内容：resource producer、subresource、lifetime和capture优先。
3. Pass消失：RDG manifest、consumer/culling、validation优先。
4. GPU hang：queue、breadcrumb、DRED/vendor dump、fence/device state优先。
5. DebugView/Visualize异常：feature branch、ODSC permutation、shader map、pass/output优先。
6. CVar无效或改变症状：flags、set-by、shadow、sink/cache、consumer与restart/rebuild/recreate优先。

## 6. Last-Valid-State

统一完成链是：

```text
Declared
-> Retained
-> Recorded
-> Submitted
-> Marker entered
-> Marker exited
-> GPU complete
-> Output verified
```

每个工具只能推进其中一段：graph dump可证明declared/retained；capture可证明边界内recorded command和resource snapshot；breadcrumb可证明marker-in/out状态；queue fence可证明目标payload完成；最终consumer/readback/画面才证明output verified。

复现模板同时记录：`Observation / Last valid state / First invalid or unknown / Next discriminating test / Invalidation conditions / Artifact`。

## 7. 事实修正

1. **固定五层顺序**：改为症状驱动有向路由，允许GPU hang、shader missing、CVar无效短路进入高信息owner。
2. **Pass/event名字**：从跨工具“共同坐标”修正为有build、profile、backend、frame和queue条件的关联键。
3. **RDG调试工具**：明确event、culling、validation和clobber各自能证明什么、不能证明什么，以及clobber的额外producer/barrier/lifetime侵入性。
4. **Capture边界**：区分GT enqueue、immediate RHI与RDG-local；next-frame请求不等于请求时刻，capture中的native command不等于原始运行GPU complete。
5. **Profile形态**：明确`GTriggerGPUProfile`改变RDG async eligibility并限制parallel translate。
6. **GPU hang**：区分recorded、submitted、marker entered/exited、fence complete、device removed；D3D12 `GetCompletedValue()==UINT64_MAX`进入device-removed检查，不是正常完成。
7. **Visualize shader**：明确BasePass `FVisualizeDim`为ODSC-only；普通cook不保证该debug permutation常驻。
8. **CVar生效**：从“打印值”扩展为flags、set-by、thread shadow、sink/cache、consumer、restart/rebuild/recreate六段链。
9. **实验归因**：补baseline -> 单变量 -> 恢复数值和`LastSetBy`/状态 -> baseline再现。

## 8. 源码克制

正文没有把源码路径、行号和实现调用栈作为教学骨架。必要的`RDG_EVENTS`编译条件、`FScopedCapture`、`FVisualizeDim`、`GTriggerGPUProfile`、CVar flags等符号只用于定义证据边界。稳定`path::symbol`和条件核对集中在`23_Debugging_CoverageMatrix.md`。

## 9. 残余风险

1. “Capture看到GPU命令”必须继续限定为捕获边界内的recorded/native command，不能外推执行完成。
2. D3D12的`UINT64_MAX`、DRED和device-removed语义不能写成所有RHI统一ABI。
3. ProfileGPU结果只用于profile形态定位；正常性能结论必须回到normal-run证据。
4. Pass/event字符串会重复或被编译条件移除，必须同时保存frame、queue、resource/marker状态。

## 10. 独立 BODY 结论

**BODY PASS**。当前正文保留冻结原版有效分层与六条路线，完成了routing/CVar/hang/tool boundary指定面；O-D-C-L、设计替代、案例、last-valid-state和事实边界均闭环。未发现需要修改正文的阻断。本结论不修改也不依赖章节状态或公共Gate。
