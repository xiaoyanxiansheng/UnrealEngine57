# RenderingPractice 课程索引

## 模块一：UE 与环境搭建基础

| # | 主题 | 主案例进展 | 状态 |
|---|---|---|---|
| 00 | [创建 UE5.7 实践工程](Practices/Part01_Foundation/00_CreateProject.md) | 创建工程、基础配置和课程目录 | 初稿完成，待实机验证 |
| 01 | [认识编辑器与项目结构](Practices/Part01_Foundation/01_EditorAndProject.md) | 创建主目录、测试地图和资产命名体系 | 初稿完成，待实机验证 |
| 02 | [使用基础几何搭建 Rendering Room](Practices/Part01_Foundation/02_BuildRenderingRoom.md) | 完成 `M01_RenderingRoom` 灰盒房间 | 初稿完成，待实机验证 |
| 03 | [Camera、Viewport、曝光与画面基准](Practices/Part01_Foundation/03_CameraViewportExposure.md) | 建立固定观察与画面基线 | 初稿完成，待实机验证 |
| 04 | [Static Mesh 导入与模块化资产](Practices/Part01_Foundation/04_StaticMeshImportAndModularAssets.md) | 建立可复用建筑模块 | 初稿完成，待实机验证 |

## 模块二：材质、光照与环境画面

| # | 主题 | 主案例进展 | 状态 |
|---|---|---|---|
| 05 | Material、Material Instance 与 Master Material | 建立环境材质体系 | 规划完成 |
| 06 | Decal、透明、玻璃与环境材质 | 加入玻璃、污渍和表面细节 | 规划完成 |
| 07 | 环境灯光与传统阴影 | 建立室内外基础光照 | 规划完成 |
| 08 | 大气、雾、云与环境天空 | 建立室外环境 | 规划完成 |
| 09 | Post Process、TSR 与最终画面 | 完成建筑画面基线 | 规划完成 |

## 模块三：渲染观察与性能工具

| # | 主题 | 主案例进展 | 状态 |
|---|---|---|---|
| 10 | Buffer 与复杂度可视化 | 识别材质、灯光和像素成本 | 规划完成 |
| 11 | `stat unit`、GPU Visualizer 与 Insights | 建立第一份性能基线 | 规划完成 |
| 12 | 可见性、遮挡、LOD、Instance 与 Draw Call | 优化建筑可见工作集 | 规划完成 |
| 13 | ISM/HISM、GPUScene 与重复资产 | 加入大量重复环境资产 | 规划完成 |
| 14 | RenderDoc、Pass、资源与 Shader 调试 | 建立 GPU 捕获能力 | 规划完成 |

## 模块四：UE5 核心渲染

| # | 主题 | 主案例进展 | 状态 |
|---|---|---|---|
| 15 | Nanite 环境资产与几何成本 | `M01` 扩展为 `M02_RenderingBuilding` | 规划完成 |
| 16 | Lumen 室内外 GI 与反射 | 完成动态室内外光照 | 规划完成 |
| 17 | Virtual Shadow Maps | 建立动态阴影与缓存模型 | 规划完成 |
| 18 | MegaLights 与大量动态灯光 | 建立夜景灯光体系 | 规划完成 |
| 19 | 综合夜景街区搭建与优化 | 完成 `M03_RenderingDistrict` | 规划完成 |

## 模块五：大世界与环境系统

| # | 主题 | 主案例进展 | 状态 |
|---|---|---|---|
| 20 | Landscape 地形、材质与分层 | 创建大世界地形 | 规划完成 |
| 21 | Foliage、实例化与植被性能 | 建立植被层 | 规划完成 |
| 22 | PCG 环境生成与规则化布置 | 自动布置环境内容 | 规划完成 |
| 23 | World Partition、Grid、Streaming Source 与 OFPA | 建立流送世界 | 规划完成 |
| 24 | Data Layers、昼夜与区域状态 | 管理世界状态 | 规划完成 |
| 25 | HLOD、远景代理与流送成本 | 建立远景和分层代理 | 规划完成 |
| 26 | Water、道路、地形与建筑整合 | 整合开放世界环境 | 规划完成 |
| 27 | 大世界内存、IO、CPU 与 GPU 分析 | 完成大世界性能基线 | 规划完成 |

## 模块六：最终案例

| # | 主题 | 主案例进展 | 状态 |
|---|---|---|---|
| 28 | 从街区扩展为完整开放世界 | 完成 `M04_RenderingWorld` | 规划完成 |
| 29 | Scalability、画质档位与最终性能验收 | 形成可继续用于案例研究的工程 | 规划完成 |

共 30 篇，编号为 00–29。
