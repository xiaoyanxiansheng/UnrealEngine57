# 04 Static Mesh 导入与模块化资产

## 本篇结果

把 `M01_RenderingRoom` 的基础 Shape 灰盒替换为第一套可复用建筑模块：

- `SM_Slab_200x200x20`
- `SM_Wall_200x300x20`
- `SM_Wall_Door_200x300x20`
- `SM_Wall_Window_200x300x20`

完成后，房间仍保持约 6 m × 8 m、3 m 层高和原有门窗位置，但几何不再依赖缩放后的 Engine Cube。每个模块拥有明确的源文件、单位、Pivot、法线、UV、材质槽、碰撞和 Reimport 路径。

本篇只建立传统 Static Mesh 资产基线。UE 5.7 的 Interchange 导入管线可能默认勾选 `Build Nanite`，本篇会明确关闭它；第 15 篇再用受控对比启用 Nanite。

## 开始前的工程状态

- 已完成 00–03。
- `M01_RenderingRoom` 的灰盒房间完整。
- `Camera_Room_Baseline`、`Light_Room_Baseline` 和 `PPV_Room_Baseline` 已保存。
- 已有 1920 × 1080 的 `03_Room_CameraExposure.png` 基准图。
- 可使用任意能稳定导出 FBX 的 DCC；本篇用 Blender 4.x 给出完整参考操作，Maya、3ds Max 或其他 DCC 需要实现同一资产合同。

开始前执行 **File > Save All**，并用固定相机确认画面仍与第 03 篇一致。若当前曝光或构图已改变，先恢复基线再导入资产。

选择 Blender 作为参考路径，是为了让正文能够给出可执行的单位、Pivot、Transform、法线、UV、碰撞命名和 FBX 导出步骤，而不是要求读者把抽象合同自行翻译到未知软件。它不是对生产 DCC 的优劣判断。若团队使用 Maya 或 3ds Max，可以替换具体菜单，但 UE 侧验收尺寸、轴向、Pivot、属性和 Reimport 结果不能改变。

## 1. 理解 Source File、Static Mesh Asset 与场景实例

外部模型进入 UE 后会经过三层：

```text
DCC Source / FBX
→ Content 中的 UStaticMesh Asset
→ Level 中的 StaticMeshActor / StaticMeshComponent 实例
```

- **FBX** 是交换文件，不参与打包后的实时渲染。
- **UStaticMesh Asset** 保存导入来源、构建设置、渲染数据、材质槽、碰撞、LOD/Nanite 等资产状态。
- **Actor 实例** 只引用 Asset，并提供关卡 Transform、可见性和覆盖材质等实例状态。

同一个墙体 Asset 被放置十次，几何资产不需要复制十份；但十个独立 StaticMeshActor 仍然是十个实例，可能产生独立的可见性、场景管理和绘制工作。**资产复用不等于自动合批。** 第 12、13 篇再比较 Draw、ISM/HISM 和 GPUScene。

这三层还对应三个不同的修改传播范围：

- 改 DCC Source 后，UE 不会自动获得结果；需要重新导出并 Reimport。
- 改 UStaticMesh Asset 的构建、碰撞或材质槽，会影响所有引用它的 Actor。
- 改某个 Actor 的 Transform 或 Override Material，只改变该 Level 实例，除非把变化再写回资产或 Blueprint。

后续排错时先确定错误属于哪一层。例如所有墙实例都出现法线错误，优先检查 Source/Asset；只有一个墙位置不对，优先检查该 Actor Transform。分层判断能避免用二十个实例补丁掩盖一个源资产错误。

## 2. 建立外部源资产目录

在 `RenderingPractice.uproject` 同级目录创建：

```text
SourceAssets/
└── Architecture/
    └── RoomKit/
        ├── DCC/
        └── Export/
```

建议保存：

```text
SourceAssets/Architecture/RoomKit/DCC/RP_RoomKit_v001.<dcc-extension>
SourceAssets/Architecture/RoomKit/Export/RP_RoomKit_v001.fbx
```

不要把可编辑 DCC 文件和 FBX 放进 `Content/`。Content 主要管理 UE Asset；源文件放在项目外部目录，可以独立版本化，不会被 Asset Registry 当作运行时内容。

`v001` 用于源文件迭代记录，UE Asset 名称不带版本号。这样 Reimport 后关卡引用仍指向稳定的 `SM_` Asset，而不是每次更新都创建一个新名字。

`.blend` 和 `.fbx` 承担的职责也不同：`.blend` 保存可继续编辑的建模历史和场景组织；FBX 是从某个版本导出的交换快照。覆盖 `RP_RoomKit_v001.fbx` 并不会修改 `.blend`，修改 `.blend` 也不会自动更新 FBX。版本管理应能回答“当前 UE Asset 来自哪份可编辑源文件和哪次导出”，而不是只保留最后一个 FBX。

## 3. 在 DCC 中建立 2 m 模块合同

所有模块使用同一局部坐标约定：

- 尺寸单位：厘米，或在 FBX 中写入正确单位元数据。
- Z：向上。
- 墙体宽度沿局部 X，厚度沿局部 Y，高度沿局部 Z。
- 墙体 Pivot：底边中心，即 `(0, 0, 0)`。
- Slab Pivot：几何中心。
- Object Transform：应用/冻结后 Translation `0,0,0`、Rotation `0,0,0`、Scale `1,1,1`。
- 一个渲染对象对应一个 UE Static Mesh Asset。

创建下列几何：

| 对象名 | 几何范围 | 开口 |
|---|---|---|
| `SM_Slab_200x200x20` | X `-100..100`，Y `-100..100`，Z `-10..10` | 无 |
| `SM_Wall_200x300x20` | X `-100..100`，Y `-10..10`，Z `0..300` | 无 |
| `SM_Wall_Door_200x300x20` | 同一外轮廓，由左、右、上三段组成 | X `-50..50`，Z `0..210` |
| `SM_Wall_Window_200x300x20` | 同一外轮廓，由左、右、下、上四段组成 | X `-60..60`，Z `90..210` |

多个对象可以在 DCC 场景原点重叠，因为它们是独立资产，不是用于直接导入为完整关卡的摆放场景。这样即使导入器 Bake Object Transform，各模块的局部原点仍保持可预测。

### 单位、轴向、Pivot 和 Transform 是四份不同合同

- **单位**决定数值 `2` 表示 2 m、2 cm 还是其他长度。错误单位会让所有尺寸、碰撞和距离相关系统一起错误。
- **轴向**决定哪个方向是 Up、Forward 和 Right。坐标转换可以让 Z Up 正确，但不能自动判断一面墙哪一侧是建筑正面。
- **Pivot**是 Actor 放置、旋转、吸附和缩放的局部参考点。墙底中心 Pivot 让所有墙模块以 Z `0` 落地；Slab 中心 Pivot 让上下表面相对中心对称。
- **Object Transform**描述对象节点相对 DCC 场景的 Translation/Rotation/Scale。冻结 Rotation/Scale 是把当前变换烘入顶点并把对象倍率恢复为 1；它不能替代正确单位，也不能自动把错误 Pivot 移到期望位置。

把四者混为“导入缩放问题”会产生多层补偿：DCC Scale 100、FBX Scale 0.01、UE Build Scale 100、Actor Scale 0.01 可能偶然得到正确外观，却让 Reimport、碰撞和其他模块失去共同合同。

### Blender 4.x：建立场景单位和对象原点

1. 打开 **Scene Properties > Units**。
2. 设置 **Unit System = Metric**、**Unit Scale = 1.0**、**Length = Meters**。
3. 保存为 `SourceAssets/Architecture/RoomKit/DCC/RP_RoomKit_v001.blend`。
4. 删除默认 Camera 和 Light；它们不属于交换文件。
5. 为每个模块创建独立 Mesh Object，并让 Object Location/Rotation 为 `0,0,0`、Scale 为 `1,1,1`。

这个 Blender 场景以米建模：UE 中的 200 cm 在 Blender 中输入 2 m，20 cm 输入 0.2 m，300 cm 输入 3 m。FBX 写入场景单位，UE 的 `Convert Scene Unit` 再把米转换为厘米。不要为了看到 UE 数值 200 而在 Blender 中直接把物体放大 100 倍；数值表示不同，物理长度应一致。

### Blender 4.x：创建四个渲染对象

对 Slab：

1. **Add > Mesh > Cube**，命名 `SM_Slab_200x200x20`。
2. 在 Item/Transform 中把 Dimensions 设为 `2, 2, 0.2 m`。
3. 执行 `Ctrl+A > Rotation & Scale`，确认 Scale 回到 `1,1,1`。
4. 保持 Object Origin 位于几何中心。

对完整墙：

1. 创建 Cube，命名 `SM_Wall_200x300x20`，Dimensions 设为 `2, 0.2, 3 m`。
2. 进入 Edit Mode，把全部顶点沿 Z 移动 `+1.5 m`，使几何范围变成 Z `0..3 m`，但 Object Origin 仍在 World Origin。
3. 返回 Object Mode，执行 `Ctrl+A > Rotation & Scale`。

门墙和窗墙不能用一个覆盖开口的完整 Cube。为每种墙创建一个 Mesh Object，进入 Edit Mode 后删除默认顶点，再用 `Shift+A > Cube` 添加多个属于同一 Object 的矩形块。对每个新 Cube 用数值缩放和移动得到以下尺寸与中心：

- `SM_Wall_Door_200x300x20`：左右块 Dimensions 为 `0.5, 0.2, 3 m`，中心分别为 `-0.75,0,1.5` 和 `0.75,0,1.5 m`；上块 Dimensions 为 `1,0.2,0.9 m`，中心为 `0,0,2.55 m`。
- `SM_Wall_Window_200x300x20`：左右块 Dimensions 为 `0.4,0.2,3 m`，中心分别为 `-0.8,0,1.5` 和 `0.8,0,1.5 m`；下块 Dimensions 为 `1.2,0.2,0.9 m`，中心为 `0,0,0.45 m`；上块 Dimensions 为 `1.2,0.2,0.9 m`，中心为 `0,0,2.55 m`。

所有块的 Y 范围都是 `-0.1..0.1 m`。创建后检查每个 Object 的外轮廓仍为 2 × 0.2 × 3 m，Object Origin 仍在墙底中心。使用多个职责明确的矩形块，比先做完整墙再执行未经检查的 Boolean 更容易保持确定拓扑；若使用 Boolean，也必须在应用 Modifier 后重新检查法线、三角化、开口尺寸和对象原点。

### 为什么选择 2 m 网格

房间 6 m × 8 m，可以分别由 3 × 4 个模块覆盖；2 m 也是 10 cm 灰盒吸附的整数倍。它让本篇能够用少量资产验证复用、对齐和 Reimport。

2 m 不是所有建筑的最佳模数。真实项目需要根据门窗族、立面节奏、Texel Density、碰撞、遮挡、HLOD 和关卡设计共同决定。第 15 篇扩展为 Building 时应重新评估模块粒度，而不是无限复制这四个教学资产。

## 4. 准备法线、UV 和材质槽

对每个渲染对象执行：

1. 为 90° 建筑边缘设置正确的 Hard Edge / Smoothing。
2. 导出 Vertex Normals 和 Tangents。
3. 创建 UV0，保证每个表面有合理、稳定的纹理展开。
4. 给全部面分配一个占位材质，材质槽命名为 `Surface`。
5. 在导出前对最终网格进行确定性三角化，或至少在团队中固定同一三角化规则。

### Blender 4.x：完成法线、UV、材质槽和三角化

对四个渲染 Object 逐个执行：

1. 进入 Edit Mode，全选面，使用 **Mesh > Normals > Recalculate Outside**（`Shift+N`），让封闭体表面朝外。
2. 对本篇全由平面和 90° 边组成的模块执行 **Object > Shade Flat**。这会让每个面保持自己的法线，避免把墙角插值成圆角。以后增加 Bevel 时再为斜面建立新的平滑/硬边合同，不能把本篇的全平面规则直接推广到所有建筑资产。
3. 切换到 **UV Editing** Workspace，进入 Edit Mode，全选面，执行 `U > Cube Projection` 作为当前箱体模块的 UV0 起点。
4. 在 UV Editor 检查所有表面都有 UV 岛，没有面积为零的面，并让四个资产使用一致的 Texel Density 约定。第 05 篇建立材质后还会用实际纹理检查比例。
5. 在 **Material Properties** 创建一个材质槽，命名 `Surface`，全选面后点击 **Assign**。这里只需要稳定的 Slot 名，不需要把 Blender 材质导入 UE。
6. 添加 **Triangulate Modifier**，保持四个资产使用一致规则并 Apply。Apply 后保存 `.blend`，再检查门窗开口没有被错误连接。

“确定性三角化”不是为了减少三角形，而是固定四边面最终由哪条对角线拆成三角形。法线和切线在三角形顶点上工作；如果 DCC 显示时、FBX 导出时和 UE Reimport 时各自重新选择对角线，法线贴图、顶点拆分和局部阴影可能在版本间漂移。

本篇选择保留 DCC 法线，而不是让每次导入重新计算。这样法线与硬边属于源资产合同，换机器或 Reimport 时不依赖编辑器构建规则猜测。

替代方案是让 UE 重新计算 Normals/Tangents。它适合没有可靠法线的扫描、程序生成或旧资产，但可能改变硬边、顶点拆分、切线空间和法线贴图结果。若源文件不导出 Tangents，就必须在导入设置中开启 Recompute Tangents，并重新检查所有边界。

只创建一个 `Surface` 槽，是因为材质体系要到第 05 篇才建立。多材质槽会把一个 Mesh 分成多个 Section，通常增加绘制和资产维护成本；应由真实材质边界驱动，不能为了编辑方便任意切分。

Hard Edge 也不是只改变编辑器中的一条显示线。硬边要求相邻表面使用不同顶点法线，导出后通常会在该边产生顶点属性拆分；UV Seam 和不同材质边界也可能产生拆分。因此“几何只有几个角点”不等于 GPU 顶点数据只有同样数量。当前模块用硬边换取正确的箱体光照，这是有意义的数据成本，而不是应消除的错误。

## 5. 创建简单碰撞

在同一个 DCC 文件中创建碰撞对象。它们不需要 UV、材质和精细拓扑，但每个 UCX 对象必须是封闭凸体。

使用命名规则：

```text
UCX_<RenderMeshName>_<Index>
```

本套资产建议：

```text
UCX_SM_Slab_200x200x20_00
UCX_SM_Wall_200x300x20_00

UCX_SM_Wall_Door_200x300x20_00   # 左段
UCX_SM_Wall_Door_200x300x20_01   # 右段
UCX_SM_Wall_Door_200x300x20_02   # 上段

UCX_SM_Wall_Window_200x300x20_00 # 左段
UCX_SM_Wall_Window_200x300x20_01 # 右段
UCX_SM_Wall_Window_200x300x20_02 # 下段
UCX_SM_Wall_Window_200x300x20_03 # 上段
```

门窗不能只用一个包围盒碰撞，否则视觉开口存在，Pawn 和 Trace 却无法穿过。直接使用 Complex Collision 可以贴合渲染三角形，但会增加碰撞数据和查询约束，也不适合所有物理模拟场景。本篇的多块简单凸碰撞更符合模块职责。

渲染 Mesh 与 Collision Mesh 是两份数据。改了窗洞尺寸而不更新 UCX，会出现“看得见开口但过不去”或射线命中空气的问题。

### Blender 4.x：创建 UCX 碰撞对象

在 Object Mode 新建独立 Cube 作为碰撞体，不要把它们 Join 到渲染 Object：

1. Slab 和完整墙各用一个与外轮廓一致的 Box。
2. 门墙使用左、右、上三个 Box；窗墙使用左、右、下、上四个 Box。
3. 每个 Box 的 Dimensions 和中心位置应与对应渲染块一致，然后执行 `Ctrl+A > Rotation & Scale`。
4. 严格使用本节 `UCX_<RenderMeshName>_<Index>` 名称。名称中的 RenderMeshName 必须完整匹配目标 Object；后缀只用于区分多个凸体。
5. 在 Blender Viewport 中临时隐藏渲染 Object，只显示 UCX，确认没有一个碰撞 Box 跨过门洞或窗洞。

每个 UCX 必须是凸体，因为 UE 可以把一个凸体转换成稳定的简单碰撞形状；凹形门框需要拆成多个凸体，不能仅把一个凹网格命名为 UCX 就期待导入器保留凹洞。这里的“简单”描述碰撞表示方式，不表示视觉形状一定只有一个 Box。

## 6. 导出 FBX

选择四个渲染对象和对应 UCX 对象，导出：

`SourceAssets/Architecture/RoomKit/Export/RP_RoomKit_v001.fbx`

导出时遵守：

- 只导出选中对象。
- 不导出 Camera、Light、Animation 或 Skeleton。
- 写入正确的 Up/Forward Axis 与 Scene Unit。
- 导出 Normals、Tangents、UV 和 Material Assignment。
- 保持对象名与本篇完全一致。
- 不在导出器中额外乘一次 `100` 倍缩放。

### Blender 4.x：执行 FBX 导出

1. 在 Object Mode 选中四个渲染 Object 和全部 UCX Object。
2. 选择 **File > Export > FBX (.fbx)**。
3. 输出到 `SourceAssets/Architecture/RoomKit/Export/RP_RoomKit_v001.fbx`。
4. 在 **Include** 中启用 **Selected Objects**，Object Types 只保留 **Mesh**。
5. 在 **Transform** 中保持 **Scale = 1.0**，Forward 设为 `-Y Forward`、Up 设为 `Z Up`，启用场景单位转换，不使用额外 100 倍经验缩放。
6. 在 **Geometry** 中启用 Apply Modifiers；Smoothing 使用能导出当前硬边/法线的模式，并启用 Tangent Space。
7. 关闭 Animation/Bake Animation；本文件不包含骨骼、动画、Camera 或 Light。
8. 点击 **Export FBX**，然后检查文件修改时间和大小确实更新。

Blender 的坐标转换负责把 Blender 对象轴写入 FBX，UE Translator 再转换到 UE 坐标系；`Convert Scene Unit` 负责物理单位。轴转换和单位转换解决不同问题，不能用 Forward/Up 修复 100 倍尺寸，也不能用 Uniform Scale 修复墙体朝向。

其他 DCC 的面板名称不同，但不能省略这一步的操作责任。至少要找到并明确：Selected Objects、Mesh 类型、Scene Unit、Up/Forward Axis、Normals/Tangents、Material Assignment、Animation Off 和最终 Scale。第一次导入后必须在 UE 中测量，因为“DCC 中看起来正确”只能证明 DCC 场景内部一致，不能证明 FBX 元数据和 UE Translator 得到相同结果。

## 7. 打开 UE 5.7 Interchange 导入

在 Content Drawer 中创建：

`Content/RenderingPractice/Environment/Architecture/RoomKit`

进入该目录，点击 **Import**，选择 `RP_RoomKit_v001.fbx`。也可以把 FBX 拖入 Content Drawer；两种入口都会进入当前 5.7 配置的 FBX/Interchange 管线。

UE 5.7 的 Interchange 对话框可能按 Pipeline 分组显示属性，具体折叠顺序会随编辑器布局和保存的导入预设变化。不要直接接受上一次资产留下的设置，逐组核对本篇值。

导入目标目录是资产身份的一部分。FBX 的磁盘位置不会决定 UStaticMesh 的 Content 路径；你在 Content Drawer 中进入的目录与 Import Destination 才决定新 Package 创建在哪里。开始 Import 前确认目标为 `/Game/RenderingPractice/Environment/Architecture/RoomKit`，否则即使几何正确，后续章节和 Reimport 仍会引用错误路径。

Interchange Pipeline 设置还可能继承上一次导入或用户保存的预设，所以“这是一个全新 FBX”不表示对话框回到课程默认。预览树应显示四个渲染 Mesh 节点及其 UCX 节点；如果名称或数量不对，应先回 DCC/FBX 修复，而不是用 Combine、Rename 或 Fallback Collision 在 UE 侧猜测补救。

## 8. 设置导入管线

### Common

| 属性 | 本篇值 | 原因 |
|---|---:|---|
| `Scene Name Subfolder` | Off | 已经进入专用 RoomKit 目录，不再生成额外层级 |
| `Asset Type Subfolders` | Off | 当前只导入 Static Mesh，避免产生无意义目录 |
| `Offset Translation` | `0,0,0` | 修正应回到源文件，不在导入层积累隐藏偏移 |
| `Offset Rotation` | `0,0,0` | 轴向由 FBX 转换负责 |
| `Offset Uniform Scale` | `1.0` | 单位由 Scene Unit 转换负责，不用经验缩放补救 |

三个 Offset 是导入管线额外施加的校正层，不是 FBX 源 Transform 的展示。它们会让 UE 中结果看似正确，却把真实源文件错误藏在每次导入都必须复现的预设里。本课程将它们保持恒等值，是为了让尺寸和轴向合同只有两个明确所有者：DCC/FBX 描述源数据，Translator 负责已知坐标系与单位转换。

### Common Meshes

| 属性 | 本篇值 | 原因 |
|---|---:|---|
| `Force All Meshes As Type` | `Static Mesh` | 本文件是刚性建筑模块，不允许被自动识别为 Skeletal Mesh |
| `Import LODs` | Off | 源文件没有 LOD；第 12、15 篇再比较 LOD 与 Nanite |
| `Bake Meshes` | On | 源对象 Transform 已冻结，Bake 不应改变约定原点 |
| `Keep Sections Separate` | Off | 相同材质不需要被人为拆成多个 Section |
| `Vertex Color Import Option` | `Ignore` | 本套资产没有 Vertex Color，避免 Reimport 覆盖未来编辑数据 |
| `Import Sockets` | Off | 建筑壳体当前不需要 Socket |
| `Recompute Normals` | Off | 使用 DCC 输出的硬边与法线 |
| `Recompute Tangents` | Off | 使用同一份源切线合同 |
| `Use High Precision Tangent Basis` | Off | 普通建筑模块先使用默认精度，减少顶点数据 |
| `Use Full Precision UVs` | Off | 2 m 模块 UV 范围小，默认精度足够 |

如果导入预览报告缺少 Normals/Tangents，不要带着警告继续。回 DCC 修复导出，或明确改为 Recompute 并记录这个资产采用 UE 构建法线的方案。

`Full Precision UVs` 能缓解大范围 UV 或半精度量化造成的接缝，但会增加顶点数据。它是针对实际误差的修复开关，不是所有资产都应打开的“高质量模式”。

`Bake Meshes` 决定导入时是否把源节点 Transform 烘入 Mesh 几何。它与 UE Actor Transform 不同：Bake 发生在资产创建阶段，之后所有实例共享结果。本篇源 Object 已经位于原点并冻结 Rotation/Scale，所以 Bake 不应产生额外偏移；若源对象靠父层级或非零节点 Transform 保存 Pivot，开启 Bake 可能改变结果，需要采用另一份完整场景导入合同。

`Recompute Normals/Tangents` 与精度开关也不是一组“质量越高越好”的按钮。是否 Recompute 决定属性的生产者是 DCC 还是 UE；精度开关决定这些已存在属性用多大数据表示。先确定谁生产正确数据，再根据可见误差和资产规模选择精度，不能用 Full Precision 修复错误法线，也不能用 Recompute 掩盖每次导出不同的硬边合同。

### Static Meshes

| 属性 | 本篇值 | 原因 |
|---|---:|---|
| `Import Static Meshes` | On | 创建 UStaticMesh Asset |
| `Combine Static Meshes` | Off | 四个对象必须保持四个可复用模块 |
| `Import Collisions` | On | 导入 UCX 简单碰撞 |
| `Import Collisions According To Mesh Name` | On | 让 UCX 前缀绑定对应渲染 Mesh |
| `One Convex Hull Per UCX` | On | 每个 UCX Box 保持一个凸体，不做额外分解 |
| `Fallback Collision Type` | `None` | 缺少 UCX 时暴露错误，不自动用包围体封死门窗 |
| `Build Nanite` | **Off** | 保持传统 Static Mesh 基线，第 15 篇再启用 |
| `Build Reversed Index Buffer` | Off | 当前模块不需要为镜像 Transform 增加索引数据 |
| `Generate Lightmap UVs` | On | 从 UV0 生成独立 UV1，保留静态烘焙比较能力 |
| `Min Lightmap Resolution` | `64` | 给当前小模块提供基础打包 Padding 假设 |
| `Source Lightmap Index` | `0` | 使用已有 UV0 岛作为生成来源 |
| `Destination Lightmap Index` | `1` | 不覆盖纹理 UV0 |
| `Build Scale` | `1,1,1` | 不在构建阶段再次改变单位 |

UE 5.7 的 `InterchangeGenericMeshPipeline` 属性声明把 `Build Nanite` 初始化为 `true`，但具体导入模式、预设和已保存 Pipeline 状态仍可以把它改为 `false`。因此源码初始值不能证明当前对话框一定勾选，更不能证明最终 Asset 已启用。本篇主动选择 Off，并在 Static Mesh Editor 检查最终 Nanite 状态；原因不是 Nanite 不适合建筑，而是要先保存一个传统 Static Mesh 的可比较状态。对于这组极低三角形模块，立即启用 Nanite 也不会自动解决 Actor、材质或像素成本。

`Build Reversed Index Buffer` 会为反向绕序准备额外索引数据，主要服务使用负 Scale 镜像实例时的渲染。它增加索引数据和构建工作；本套模块通过明确的正向资产与旋转放置，不计划用负 Scale 镜像，因此关闭。若未来确实依赖镜像 Transform，应重新评估，而不是在看到反面剔除或法线问题后随意开启。

Collision 三个选项形成另一条独立合同：`Import Collisions` 决定是否接收自定义碰撞；`According To Mesh Name` 决定 UCX 根据名称绑定哪一个渲染 Mesh；`One Convex Hull Per UCX` 决定每个 UCX Object 保持一个凸体。`Fallback = None` 则保证缺失或命名错误会暴露为“没有简单碰撞”，而不是自动生成一个封死门窗的包围体。导入成功后仍必须看 Collision 可视化，因为勾选项只能证明管线允许导入，不能证明 DCC 名称和几何真的有效。

生成 Lightmap UV 会增加导入构建时间和一个 UV Channel。课程主要使用动态 UE5 光照，但第 07 篇仍可能比较传统静态方案，因此先保留合法 UV1。如果项目明确禁止 Static Lighting，可以在项目级策略确定后关闭生成，避免无用数据。

### Materials、Textures 与 FBX Translator

设置：

```text
Import Materials: Off
Import Textures:  Off
Convert Scene Unit: On
```

保持默认 FBX Axis/Coordinate System 转换策略，但必须在导入后验证 Z Up 和墙体正面方向。关闭材质、纹理导入，是为了避免 FBX 占位材质污染第 05 篇要建立的 UE Material 体系；`Surface` Slot 仍应保留，暂时使用默认材质。

`Import Materials = Off` 不表示删除 Mesh 上的材质分区。FBX 中的 Material Assignment 仍用于形成 `Surface` Slot/Section；关闭的是为外部材质自动创建 UE Material Asset。这样第 05 篇可以把正式 UE Material 绑定到稳定 Slot，而不需要先清理一批自动翻译但不符合课程体系的材质。

`Convert Scene Unit = On` 读取 FBX 的场景单位并转换到 UE 厘米。它只能在 FBX 元数据正确时工作；若 Blender 导出已经额外乘 100，再让 UE 转换一次，结果仍会错误。正因如此，导入后测量是合同验证，不是可选的视觉检查。

点击 **Import**。等待 Mesh 构建、Shader 编译和保存提示结束，不要在导入任务仍运行时开始替换房间。

Import 完成表示 UStaticMesh 已在内存中创建并运行构建流程，不等于所有 Package 已保存。先检查 Message Log/Output Log 没有法线、碰撞或导入节点警告，再执行 **Save All**。若立即关闭编辑器而 Asset 仍带星号，FBX 文件仍在磁盘上也不能替代尚未保存的 `.uasset`。

## 9. 检查导入结果

Content Drawer 中应只有四个新的 Static Mesh Asset，没有自动生成 Material、Texture 或版本子目录。

逐个双击，在 Static Mesh Editor 中检查：

1. **尺寸**：Slab 为 200 × 200 × 20 cm，墙模块为 200 × 20 × 300 cm。
2. **Pivot**：墙体局部原点位于底边中心；Slab 原点位于几何中心。
3. **Normals/Tangents**：表面朝向正确，90° 边没有意外渐变或黑缝。
4. **UV Channels**：UV0 是纹理展开，UV1 是生成的 Lightmap UV；`Light Map Coordinate Index` 应为 `1`，若没有自动设置则手动改为 `1` 并 Apply。
5. **Material Slots**：只有一个 `Surface`。
6. **Collision**：开启 **Collision > Simple Collision** 显示，门窗开口必须保持可通行。
7. **Nanite Settings**：Nanite 未启用。
8. 保存 Asset，关闭并重新打开，确认 Build Settings 没有未应用修改。

### 用关卡原点验证 Pivot

不要只看 Static Mesh Editor 中的坐标轴：

1. 把一个 `SM_Wall_200x300x20` 拖入 `M01_RenderingRoom`。
2. 设置 Location `0,0,0`、Rotation `0,0,0`、Scale `1,1,1`。
3. 在正交视图检查墙底是否正好位于 Z `0`，墙顶是否位于 Z `300`。
4. 删除这个测试 Actor。

如果尺寸大 100 倍或小 100 倍，先检查 DCC 单位与 `Convert Scene Unit`，不要用 Actor Scale `0.01` 或 `100` 永久补救。错误单位会继续影响碰撞、物理、LOD 屏幕尺寸、距离场、Lumen Card 和模块吸附。

## 10. 处理第一次导入失败

在资产尚未被关卡引用时，修正流程最简单：

1. 记录错误来自单位、轴向、命名、法线、UV 还是碰撞。
2. 回 DCC 修改源文件并重新导出同一路径。
3. 在 UE 中右键对应 Asset，选择 **Reimport**。
4. 如果对象名和资产拆分已完全改变，可以删除这四个未使用 Asset 后重新导入；删除前确认 Reference Viewer 没有关卡引用。

不要同时修改 DCC Scale、FBX Export Scale、UE Offset Uniform Scale 和 Actor Scale。多层补偿可能让结果偶然正确，却使 Reimport、碰撞和其他资产无法共享同一合同。

## 11. 在 Outliner 中保留灰盒回退路径

在 `00_Geometry` 下创建：

```text
00_Geometry/
├── 10_ModularRoom
└── 90_GreyboxBackup
```

1. 把第 02 篇创建的 Floor、Ceiling 和墙体 Actor 移入 `90_GreyboxBackup`。
2. 暂时使用 Outliner 眼睛图标隐藏该 Folder。
3. 对这些 Actor 同时启用 **Actor Hidden In Game**，避免 Play 时与新模块重叠。
4. 新模块全部放入 `10_ModularRoom`。

Outliner 眼睛主要是编辑器可见性状态，`Actor Hidden In Game` 负责运行时隐藏。两者都不是资产删除，也不会减少地图中保存的 Actor 数据。新房间验收完成后应删除 GreyboxBackup，或把恢复责任交给版本管理，不能让双份几何长期留在正式主地图。

Outliner Folder 只组织 Actor，本身不是一个会在运行时统一控制子 Actor 的 Scene Component。点击 Folder 眼睛是在编辑器中批量改变查看状态；要保证 PIE/Game 不渲染旧灰盒，必须确认其中每个 Actor 的 **Actor Hidden In Game**。这仍不代表旧 Actor 没有保存、没有组件或没有内存/场景管理影响，所以它只适合短期 A/B 回退，不是长期性能优化方案。

## 12. 铺设 Floor 与 Ceiling

把 `SM_Slab_200x200x20` 拖入关卡，Scale 始终保持 `1,1,1`。使用复制和 200 cm 移动吸附，创建以下组合：

```text
X = -200, 0, 200
Y = -300, -100, 100, 300
```

每个 X/Y 组合放置一个 Floor 和一个 Ceiling：

| 用途 | Z | Rotation |
|---|---:|---|
| Floor | `-10` | `0,0,0` |
| Ceiling | `310` | `0,0,0` |

最终为 12 个 Floor Actor 和 12 个 Ceiling Actor。命名建议包含网格位置，例如：

```text
Floor_Xm200_Ym300
Ceiling_X000_Y100
```

模块名已经说明 Asset 类型；Actor 名更应表达实例位置或语义，不必给每个实例重复 `SM_` 前缀。

相邻 Slab 边界必须落在 200 cm 网格上。若出现细缝，检查 Actor Location 与 Scale，不要把单个模块拉宽填缝，否则它不再能与其他实例共享同一空间合同。

这些坐标来自模块尺寸，而不是试出来的：X 三列中心为 -200/0/200，分别覆盖 `-300..-100`、`-100..100`、`100..300` cm；Y 四行中心为 -300/-100/100/300，合计覆盖 `-400..400` cm。Slab Pivot 在几何中心，厚度 20 cm，所以 Floor 放 Z `-10` 后上表面正好为 Z `0`；Ceiling 放 Z `310` 后下表面正好为 Z `300`。如果 Pivot 误在底面，这套 Z 值会整体错 10 cm，因此铺设步骤同时也是 Pivot 合同的批量验证。

## 13. 铺设四面墙

墙模块 Pivot 在底边中心，因此全部使用 Z `0`、Scale `1,1,1`。

Z `0` 的意义是墙底与地面上表面对齐；Scale `1` 的意义是关卡实例不再承担尺寸修正。墙宽沿资产 Local X，厚度沿 Local Y。前后墙不旋转时，Local X 对应 World X；侧墙 Yaw 90° 后，Local X 转到 World Y，于是同一个 2 m 墙 Asset 能沿房间深度排列。旋转改变方向，不改变资产单位和 Pivot。

### 后墙

```text
Asset: SM_Wall_200x300x20
Y:     390
X:     -200, 0, 200
Yaw:   0
```

### 前墙与门

```text
SM_Wall_200x300x20:      X -200, Y -390, Yaw 0
SM_Wall_Door_200x300x20: X    0, Y -390, Yaw 0
SM_Wall_200x300x20:      X  200, Y -390, Yaw 0
```

### 左墙

```text
Asset: SM_Wall_200x300x20
X:     -290
Y:     -300, -100, 100, 300
Yaw:   90
```

### 右墙与窗

```text
SM_Wall_200x300x20:        X 290, Y -300, Yaw 90
SM_Wall_200x300x20:        X 290, Y -100, Yaw 90
SM_Wall_Window_200x300x20: X 290, Y  100, Yaw 90
SM_Wall_200x300x20:        X 290, Y  300, Yaw 90
```

完成后，从固定相机观察，房间总体轮廓、门洞、窗台和层高应与灰盒一致。

### 关于当前墙角重叠

为了严格延续第 02 篇的 600 × 800 cm 外轮廓，本篇的前后墙与侧墙在角部保留少量实体重叠。这不会产生可见开口，但会增加重叠几何和碰撞，并可能在共面表面出现 Z-fighting。

它是当前教学套件的已知限制，不是正式建筑模块的推荐终点。生产套件通常使用 Corner、End Cap、不同长度模块或统一中心线规则消除重叠。第 15 篇把 Room 扩展为 Building 时应修订角部合同，而不是继续扩大这个误差。

## 14. 验证模块替换

保持 `90_GreyboxBackup` 隐藏，依次检查：

- Top、Front、Right 正交视图中所有模块都落在 200 cm 网格或已记录的墙体中心线上。
- 所有 Actor Scale 为 `1,1,1`。
- Door 开口约 100 × 210 cm。
- Window 开口约 120 × 120 cm，窗台约 90 cm。
- Floor 总范围约 600 × 800 cm。
- 墙顶 Z 为 300 cm，Ceiling 底面约为 300 cm。
- Player/Pawn 或 Collision View 中门窗没有被整块碰撞封死。
- 固定相机的主要轮廓与 `03_Room_CameraExposure.png` 一致。

切换 GreyboxBackup 与 ModularRoom 的可见性进行 A/B 对比时，只允许一组可见。两组同时渲染会出现重叠表面闪烁，得到的性能和画面结果都无效。

碰撞检查必须来自新 RoomKit 模块，而不是第 02 篇 `SM_Room_Wall_Window` 的临时 Complex As Simple 设置。打开每个新 Asset 的 Simple Collision 可视化，确认 UCX 围绕门窗开口；再在关卡中隐藏或删除旧灰盒，排除“旧碰撞仍然挡住开口”的假失败。此处通过后，第 02 篇的临时碰撞职责结束，正式地图只依赖导入模块的简单碰撞。

## 15. 用固定基线生成模块化截图

恢复第 03 篇条件：

```text
Camera:             Camera_Room_Baseline
View Mode:          Lit
Realtime:           On
Game View:          On
Viewport Quality:   Epic
Material Quality:   High
Screen Percentage:  100%
Exposure:           Manual Physical / EV100 5 / Compensation 0
Baseline Light:     3000 lm
```

执行：

```text
HighResShot 1920x1080
```

将长期参考图保存为：

`SourceAssets/Reference/Baselines/04_Room_Modular.png`

比较 03 与 04：

- 应保持：相机、曝光、灯光、房间尺度、门窗位置和总体构图。
- 允许变化：模块接缝、法线、碰撞、几何边界和局部明暗。
- 若整张图亮度显著变化，先检查是否存在重叠几何、翻转法线、相机或曝光改动，而不是把它直接归因于“导入 Mesh 更高级”。

## 16. 完成一次受控 Reimport

为了确认源资产链路可维护：

1. 在 DCC 中给 `SM_Wall_Window_200x300x20` 的可见硬边增加非常小且统一的 Bevel，例如 `1 cm`。
2. 更新该对象的 Normals/Tangents；保持对象名、Pivot、外轮廓、Material Slot 和 UCX 命名不变。
3. 覆盖导出同一个 `RP_RoomKit_v001.fbx`。
4. 在 UE 中右键 `SM_Wall_Window_200x300x20`，选择 **Reimport**。
5. 等待构建完成，检查关卡中的窗口实例自动更新。
6. 再次检查尺寸、UV、碰撞、Material Slot 和 Nanite 状态。

Reimport 不是只替换顶点。它可能重新运行 Build Settings，并影响法线、UV、碰撞、Section、LOD 和 Nanite 数据。若 Material Slot 名称或顺序变化，关卡中的 Override Material 也可能错位。因此每次 Reimport 都要按资产风险复核，而不是看到形状更新就结束。

Reimport 能保持关卡引用，是因为 Actor 引用的仍是同一个 UStaticMesh Asset/Package；导入器根据 Asset 保存的 Source File 与源节点身份重新生成其内容。它不是把新文件作为第二个 Asset 导入。对象名、拆分方式或源节点身份发生大改时，这种对应关系可能失效，因此“文件路径相同”也不能保证任意结构变化都能安全 Reimport。

执行前先保存 `.blend` 并覆盖 FBX，检查 FBX 修改时间；执行后检查 Asset Import Data 仍指向预期文件。这样可以区分“DCC 没有真正导出”“UE 读取了旧 FBX”和“Reimport 后构建设置产生了不同结果”三类问题。

如果不希望把 Bevel 作为正式设计，完成验证后在 DCC 中撤销并再次 Reimport。最终源文件与 UE Asset 必须一致。

## 17. 清理灰盒并保存

模块化房间通过检查后：

1. 删除 `90_GreyboxBackup` 中旧的基础 Shape Actor。
2. 保留 `SM_Room_Wall_Window` Asset 也可以，因为它记录了第 02 篇 Modeling Tools 结果；但确认正式地图已不再引用它。
3. 删除失败导入、重复 Material、无用 Texture 和错误尺寸 Asset。
4. 对 RoomKit 目录执行 **Fix Up Redirectors in Folder**，前提是本篇确实发生过资产移动或重命名。
5. 执行 **File > Save All**。
6. 关闭并重新打开工程，确认全部模块、碰撞和引用仍然有效。

Redirector 用于资产移动/重命名后的引用过渡。没有发生路径变化时不需要为了“清理”反复执行；执行前应让 Source Control 能记录引用更新。

## 常见问题

### 四个对象被合并成一个 Asset

通常是 `Combine Static Meshes` 被勾选。关闭后重新导入。已经进入关卡的合并资产不要直接覆盖成四个同名资产，应先清理引用并确认最终路径。

### 导入后大 100 倍或小 100 倍

检查 DCC Scene Unit、FBX Unit Metadata、`Convert Scene Unit` 和 `Offset Uniform Scale`。Actor Scale 应保持 1；不要在四个层级同时补偿。

### 墙体躺在地上或朝向错误

检查 DCC Up/Forward Axis 与 FBX Translator 的坐标转换。修复源文件或统一 Translator 设置后 Reimport，不要给每个 Actor 单独加一个纠错 Rotation。

### Pivot 跑到场景原点

确认对象 Transform 已冻结、每个模块的局部原点正确，并检查 `Bake Meshes` 是否与源文件合同一致。包含复杂父子层级的完整场景导入可能需要另一套 Bake 策略，本篇资产库不保留 DCC 场景层级。

### 门窗看得见但无法穿过

开启 Simple Collision 显示。确认 UCX 名称精确匹配 Render Mesh、每块 UCX 为凸体、Fallback Collision 为 None，并确认没有旧灰盒碰撞仍在关卡中。

### 表面出现黑斑或接缝

检查翻转法线、Hard Edge、Tangents、三角化和非统一 Scale。若改用 Recompute Normals/Tangents，应对四个资产使用一致策略并重新比较法线贴图结果。

### UV1 为空或重叠

确认 `Generate Lightmap UVs` 已开启、Source Index 为 0、Destination Index 为 1，且 UV0 本身包含可重新打包的合法 UV 岛。生成器会重排已有 Chart，但不能替你修复完全无效的源 UV。

### 简单模块导入后却启用了 Nanite

打开 Static Mesh Editor 检查 Nanite Settings。UE 5.7 的当前 Interchange Pipeline 默认可能启用 Build Nanite；本篇必须关闭并 Apply。不要只根据导入窗口记忆判断最终 Asset 状态。

### 重复墙体没有减少 Draw Call

这是预期结果。复用 UStaticMesh 降低的是资产重复与维护成本；独立 StaticMeshActor 不会自动变成一个 ISM/HISM 绘制组。第 12、13 篇会用工具观察并转换重复实例。

## 对渲染、内存与构建的意义

- 统一单位、Pivot 和模数让关卡 Transform 保持简单，减少非统一缩放与拼接误差。
- 共享 UStaticMesh 避免重复导入同一几何，但每个 Actor 仍有实例与场景管理成本。
- Normals、Tangents、UV 精度和 Material Section 会直接改变顶点数据、Shader 输入和绘制组织。
- Simple Collision 与渲染几何分离，能控制物理与 Trace 成本，并保持门窗可通行。
- Generate Lightmap UVs 增加构建时间和资产数据，为静态光照路径提供 UV1。
- Nanite、LOD、Distance Field 和 Lumen Cards 都是 Static Mesh 的后续派生数据或设置；源资产合同错误会向这些系统继续传播。
- Reimport 能让所有引用同一 Asset 的实例一起更新，也会扩大错误修改的影响范围。

## 可选延伸与 UE 5.7 校准锚点

- `RenderingDeep/02_SceneProxy.md`：StaticMeshComponent 如何形成渲染侧表示。
- `RenderingDeep/07_GPUScene.md`：大量 Primitive/Instance 如何进入 GPUScene。
- `Engine/Plugins/Interchange/Runtime/Source/Pipelines/Public/InterchangeGenericAssetsPipelineSharedSettings.h`：Bake、Normals、Tangents 和 UV 精度选项。
- `Engine/Plugins/Interchange/Runtime/Source/Pipelines/Public/InterchangeGenericMeshPipeline.h`：Combine、Collision、Nanite 和 Lightmap UV 默认值与职责。
- `Engine/Plugins/Interchange/Runtime/Source/Import/Public/Fbx/InterchangeFbxTranslator.h`：FBX Axis 与 Scene Unit 转换。

## 完成后的工程状态

- [ ] `SourceAssets/Architecture/RoomKit` 中保留 DCC 源文件与 FBX
- [ ] Blender 参考路径或所用 DCC 已实现相同单位、轴向、Pivot、Transform 和导出合同
- [ ] RoomKit 目录中只有四个命名正确的 Static Mesh Asset
- [ ] 导入尺寸、Pivot、轴向、Normals/Tangents 和 Material Slot 已检查
- [ ] UV0 与 UV1 有明确职责，Light Map Coordinate Index 为 1
- [ ] 门窗 Simple Collision 保持开口
- [ ] Nanite 明确关闭，Actor Scale 全部为 1
- [ ] 6 m × 8 m 房间已由 2 m 模块重建
- [ ] 灰盒备份已从正式地图清理
- [ ] 已完成并复核一次 Reimport
- [ ] 已生成 `04_Room_Modular.png` 并与 03 基准图比较
- [ ] 地图和全部 Asset 已 Save All、重启验证

下一篇将在这套模块上建立 Material、Material Instance 与 Master Material 体系，并用固定相机和曝光区分材质参数变化与观察条件变化。
