# 主案例与 Lab 连续性规则

## 主案例

主案例持续成长，资产默认复用：

```text
M01_RenderingRoom
→ M02_RenderingBuilding
→ M03_RenderingDistrict
→ M04_RenderingWorld
```

阶段升级时保留旧地图，避免后续系统和配置破坏早期可复现状态。

## Lab

只有主案例出现需要隔离的问题时才创建 Lab。每个 Lab 必须说明：

1. 从主案例抽取了什么问题。
2. 隔离了哪些变量。
3. 使用什么工具和判断标准。
4. 得出了什么结论。
5. 如何把结论应用回主案例。

Lab 不提前批量创建。预计会出现：Nanite Overdraw、Lumen Screen Trace、VSM Invalidation、Foliage Instance Cost 和 World Partition Streaming。

## 地图和资产命名

- 主地图：`M##_Name`
- Lab 地图：`L##_Name`
- Material：`M_`
- Material Instance：`MI_`
- Static Mesh：`SM_`
- Blueprint：`BP_`
- Texture：`T_`
- Render Target：`RT_`

## 教程连续性检查

每篇正文必须写明：

- 开始时需要哪些地图、资产、插件和项目设置。
- 本篇新建或修改哪些内容。
- 完成后下一篇可以依赖什么。
- 是否需要保留旧值、复制地图或恢复实验设置。
