# 02 SceneProxy Teaching Edit Report

> 依据最终正文重新生成；不继承旧报告结论，不改变章节或公共状态。

## 实际最终结构

正文以“为什么 Renderer 不能直接读取 Component”为核心问题，用 StaticMesh 椅子贯穿生命周期。唯一主线是注册请求 → 摘取 Proxy/SceneInfo → RT 初始化并入队 → `FScene::Update()` 建立 Scene publication → View/Pass 收集 draw 输入。开篇用四对象、三本账和状态线建图；中段解释 Proxy 查询；后段覆盖 Transform dirty、RenderState dirty 和删除；结尾统一为最后成立状态。

## 信息价值保存

- Origin 的对象角色、`nullptr`、虚函数、static/dynamic draw、transform、重建与延迟清理均保留。
- 当前稿的四对象、三本账、五步入场、publication window、已入队/已入场和调试模型均保留。
- 并行数组与调用序列已转译为身份、空间、绘制三本账及一致性条件。
- 删除保留断引用、撤关系、再删除的依赖顺序；降级的只是重复入口和源码清单。

## 事实与术语修正

- 第一状态是发出加入请求/登记意图，不代表 Scene 已发布。
- `FScene::Update()` 固定为 Scene publication，与 command-list/Platform Queue Submit 区分。
- RT 初始化或消费命令只证明已进入渲染侧，不证明三本账已经发布。
- Proxy 是快照与查询契约；SceneInfo 是关系、索引和生命周期节点。
- Scene 中存在不表示 View 可见、Pass relevant 或 draw 已生成。
- StaticMesh transform 更新受 Mobility 与场景合同影响；删除、撤场、资源回收是不同里程碑。

## 教学、调试与源码克制

- Owner/Data/Control/Lifetime 已贯穿 Component、Proxy、SceneInfo、FScene 与本帧 draw 输入。
- 椅子案例覆盖已入队/已入场、移动、重建和删除。
- 调试按完全没出现、移动不更新、删除后残留/崩溃，从最后成立状态向下一状态门收敛。
- 正文无源码路径、行号、验证日志或源码顺序 walkthrough；UE 符号只作定位点。

## 边界与剩余问题

- 向前承接 01 的场景成形；named thread、pipe、task、fence 和 completion depth 交给 03。
- visibility、GPUScene、MeshDrawCommand 和具体 Pass 只说明输入资格，不展开算法。
- 无事实阻断项，无双素材价值缺口；后续必须保护已入队/已入场/已形成 draw/Queue Submit/GPU 完成的层级。