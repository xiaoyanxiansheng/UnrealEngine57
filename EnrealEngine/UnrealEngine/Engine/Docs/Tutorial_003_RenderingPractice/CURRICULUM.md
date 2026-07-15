# RenderingPractice 课程大纲

## 课程主轴

课程按环境案例的生产顺序组织，不按源码模块或 `RenderingDeep` 章节组织。

| 阶段 | 地图 | 主要能力 |
|---|---|---|
| Room | `M01_RenderingRoom` | 编辑器、资产、材质、灯光、后处理和基础分析 |
| Building | `M02_RenderingBuilding` | 模块化环境、Nanite、Lumen、VSM |
| District | `M03_RenderingDistrict` | 大量实例、夜景灯光、MegaLights 和场景优化 |
| World | `M04_RenderingWorld` | Landscape、Foliage、PCG、World Partition、HLOD 和流送 |

## 每篇教程的固定结构

1. 本篇完成的场景结果。
2. 开始前的工程状态。
3. 本篇需要理解的 UE 概念。
4. 按实际顺序执行的详细操作。
5. 每个关键步骤完成后的检查方式。
6. 关键参数为什么这样设置。
7. 常见错误与恢复方法。
8. 对渲染、内存或流送的影响。
9. 完成后的工程状态。
10. 可选的官方文档、源码或 `RenderingDeep` 延伸。

## 模块目标

### 模块一：基础

从空项目完成可复用房间灰盒，熟悉编辑器、资产目录、Level、Actor、Component、Transform、Snapping、Modeling Tools、Static Mesh 导入和 Camera。

### 模块二：环境画面

完成环境材质、玻璃、Decal、灯光、阴影、大气、雾、云、曝光、TSR 和最终画面基线。

### 模块三：分析工具

学习 Debug View、复杂度视图、统计命令、GPU Visualizer、Insights 和 RenderDoc，并对建筑场景建立第一份性能基线。

### 模块四：UE5 核心渲染

将房间扩展为建筑和夜景街区，掌握 Nanite、Lumen、VSM 和 MegaLights 的使用、限制、调试与成本。

### 模块五：大世界

把街区迁入开放世界，加入 Landscape、Foliage、PCG、World Partition、Data Layers、HLOD、Water 和道路，分析流送、内存、IO、CPU 与 GPU。

### 模块六：最终案例

完成开放世界整合、Scalability、画质档位和最终性能验收，为后续案例仿制与作品集阶段提供工程基础。

## Deep 与源码的角色

- 操作主线不要求关联 `RenderingDeep`。
- 当参数或系统行为需要更深解释时，在文末提供可选阅读。
- 大世界模块优先依据 UE 5.7 实际系统、源码和官方资料，不用不匹配的 Deep 文章填补目录。
