# RenderingPractice 项目状态

本文定义各阶段开始和完成时，工程应该具备什么。教程中的路径均相对于项目 Content 根目录。

## 工程位置

`D:\Unreal\EnrealEngine\Project\RenderingPractice`

## 资产目录

```text
Content/RenderingPractice/
├── Core/
│   ├── Materials/
│   ├── Meshes/
│   ├── Blueprints/
│   └── RenderTargets/
├── Maps/
│   ├── Main/
│   └── Labs/
├── Environment/
│   ├── Architecture/
│   ├── Props/
│   ├── Foliage/
│   ├── Landscape/
│   └── Water/
└── Tools/
```

## 外部源资产目录

从 04 开始，DCC 源文件、FBX 交换文件和长期参考图保存在 `.uproject` 同级的 `SourceAssets/`，不放入 Content：

```text
SourceAssets/
├── Architecture/
│   └── RoomKit/
│       ├── DCC/
│       └── Export/
└── Reference/
    └── Baselines/
```

`Content/` 保存 UE Asset，`SourceAssets/` 保存可编辑来源与交换文件，`Saved/Screenshots/` 只保存默认临时截图。三者不能混为同一资产生命周期。

## 阶段地图

| 地图 | 创建章节 | 用途 |
|---|---|---|
| `M00_Sandbox` | 01 | 临时操作和编辑器熟悉，不承载正式案例 |
| `M01_RenderingRoom` | 02 | 房间、材质、灯光、后处理和基础分析 |
| `M02_RenderingBuilding` | 15 | Nanite、Lumen、VSM 和室内外建筑 |
| `M03_RenderingDistrict` | 19 | 夜景街区、大量实例和 MegaLights |
| `M04_RenderingWorld` | 23–28 | World Partition、大世界和最终案例 |

## 状态规则

- 每篇开始前确认上一篇的完成状态。
- 重大阶段不直接覆盖旧地图，复制为下一阶段地图。
- 配置改动必须记录原值和是否需要重启。
- 正式主地图不用于破坏性实验；需要严格变量控制时创建 Lab。
- 不提交 `Binaries`、`DerivedDataCache`、`Intermediate` 和 `Saved`。
