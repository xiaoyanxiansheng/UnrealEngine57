# 01 Architecture Teaching Edit Report

> 依据最终正文重新生成；不继承旧报告结论，不改变章节状态。

## 实际最终结构

正文以“Game Thread 为什么不能直接把世界画出去”为核心问题，用红色金属球贯穿全章。唯一主线是请求成形 → 场景成形 → 工作集成形 → 命令成形。三套坐标轴补充过程、责任和寿命；五层责任域解释 Engine、Renderer、RenderCore、RHI 与平台后端。后半章从执行域、完成条件、性能和调试复盘同一主线。

## 信息价值保存

- Origin 的双输入汇合、模块分层、View/Scene 区分、RDG/MDC/RHI 主路径、输出链与金属球案例均保留。
- 当前稿的四次换形、三轴模型、Scene publication、逻辑执行域、完成深度和最后成立状态均保留。
- 重复调用链被合并为状态转换；源码走读的接口、数据、线程和完成语义已转译。
- 降级的仅是重复入口、过密符号、验证过程和调用栈式载体。

## 事实与术语修正

- `FScene::Update()` 固定为 Scene publication，不与 Submit 混用。
- RDG declaration、RHI recording、platform command formation、Queue Submit、GPU completion 分层表达。
- `FScene` 的跨帧寿命与 `FSceneRenderer` 的本帧寿命分开。
- RDG 表达 Pass/资源合同，MeshDrawCommand 表达 draw state；二者都不等于平台提交。
- command-list 类型、logical execution domain、worker task 与 physical queue 不依据命名等同。
- 资源复用依赖覆盖最后 GPU consumer 的 completion evidence。

## 教学、调试与源码克制

- Owner/Data/Control/Lifetime 已贯穿请求、Scene、工作集、计划和平台命令。
- 金属球经历请求目标、Scene 记录、View 工作集、Pass/draw、平台命令和最终输出。
- 故障按 Scene、View、Pass/Draw、RHI、Completion/Output 五层证据收敛。
- 正文无源码路径、行号、验证日志或源码顺序 walkthrough；UE 类型仅作定位锚点。

## 章节边界与剩余问题

- 02 接管 Proxy 生命周期；03 接管线程/完成；04/05 接管 RHI/RDG；06/07 接管 GPUScene/MDC；08 以后接管具体帧阶段。
- 无事实阻断项，无双素材信息价值缺口。
- 后续维护必须保护 Scene publication/Submit 边界。