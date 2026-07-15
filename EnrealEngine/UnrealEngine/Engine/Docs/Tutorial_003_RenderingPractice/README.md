# UE5.7 RenderingPractice

这是一套面向资深 Unity TA、但默认不熟悉 UE 编辑器的操作型课程。

课程从一个空项目开始，持续搭建同一个环境案例：

```text
空项目
→ Rendering Room
→ Rendering Building
→ Rendering District
→ Rendering World
→ 完整性能验收
```

## 学习目标

- 熟悉 UE 编辑器、资产、Level、Actor、Component 和项目工作流。
- 掌握环境搭建所需的材质、灯光、后处理和场景组织。
- 掌握 Nanite、Lumen、VSM、MegaLights 等 UE5 渲染系统。
- 掌握 Landscape、Foliage、PCG、World Partition、Data Layers 和 HLOD。
- 能使用 UE 工具分析渲染、内存、流送和性能问题。

## 教学方式

每篇都从当前工程状态继续，完成一个可见的场景结果。UE 特有操作首次出现时会给出完整菜单、面板、属性和检查方式；关键参数同时解释其渲染、内存或流送意义。

`RenderingDeep` 只是可选的原理延伸，不是课程目录的组织依据。没有合适 Deep 文章时，教程直接使用 UE 5.7 实际系统和操作建立知识。

## 阅读顺序

1. 从 [课程索引](INDEX.md) 按顺序学习。
2. 开始一篇前检查 [项目状态](PROJECT_STATE.md)。
3. 主案例地图持续成长；需要隔离变量时使用 Lab。
4. `_Authoring/` 是课程维护资料，不属于学习路径。

## 工程约定

课程指导你创建独立工程：

`D:\Unreal\EnrealEngine\Project\RenderingPractice`

第一轮不会替你预生成工程，因为创建项目、认识目录和完成首次配置本身就是第 00、01 篇的重要操作内容。

## 当前版本

- UE 5.7 源码版
- Windows
- D3D12 / SM6
- 第一轮正式内容：00–04
- 00–04 均为初稿，需在 UE 5.7 实际工程中按顺序执行并修订
