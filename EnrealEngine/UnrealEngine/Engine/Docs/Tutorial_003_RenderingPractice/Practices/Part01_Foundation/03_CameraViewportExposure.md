# 03 Camera、Viewport、曝光与画面基准

## 本篇结果

继续修改 `M01_RenderingRoom`，建立一套后续材质、灯光和性能对比都能复用的观察条件：

- 固定相机 `Camera_Room_Baseline`
- 临时基准灯 `Light_Room_Baseline`
- 全局后处理体积 `PPV_Room_Baseline`
- 16:9、70° 水平视场角的固定构图
- 固定 EV100 的 Manual Physical Exposure、固定 Viewport 质量和固定截图分辨率
- 第一张房间基准图 `03_Room_CameraExposure.png`

本篇追求的是**可重复观察**，不是最终灯光或电影摄影效果。第 07 篇会重建环境灯光，第 09 篇会重新评估自动曝光、物理相机曝光和最终后处理。

## 开始前的工程状态

- 已完成 00–02。
- `M01_RenderingRoom` 中已有 6 m × 8 m 灰盒房间、门洞和窗洞。
- World Outliner 中已有 `01_Lights`、`02_Cameras` 和 `03_Volumes` Folder。
- 当前地图来自 Empty Level，因此不能假定其中已有可用于曝光判断的灯光。

先打开 `M01_RenderingRoom`，执行 **File > Save All**。如果房间不是上一章的完成状态，先不要用本篇设置掩盖几何问题。

## 1. 先建立 Scene、Camera、View 的工作模型

关卡中的房间、灯光和 CameraActor 都属于同一个 World；Renderer 每次真正出图时，还需要一份观察请求，也就是 View。View 至少包含观察位置、朝向、投影、输出尺寸、Show Flags 和后处理设置。

因此需要区分三个对象：

| 对象 | 在本篇中的实例 | 职责 |
|---|---|---|
| Scene 内容 | 房间、灯光、体积 | 提供当前世界中可能被观察和渲染的内容 |
| CameraActor | `Camera_Room_Baseline` | 在关卡中保存相机 Transform 和投影参数 |
| View | 当前 Level Viewport 的一次观察 | 将相机、Viewport、Show Flags、画质和后处理合成为实际渲染请求 |

自由透视 Viewport 不依赖关卡中的 CameraActor。它可以观察同一个房间，却拥有另一套位置、FOV 和编辑器显示条件。只有切换到固定 CameraActor，并同时固定 Viewport 与曝光，前后截图才具有可比性。

Unity 中的 Camera Component 可以帮助建立初始类比，但不要把 UE 的编辑器自由视口直接理解成“另一个必须存在于场景中的 Camera”。Level Viewport 自己就能构造编辑器 View。

## 2. 添加临时基准灯

选择 **Add > Lights > Point Light**，然后设置：

```text
Actor Name:         Light_Room_Baseline
Outliner Folder:    01_Lights
Location:           0, 100, 240
Mobility:           Movable
Intensity Units:    Lumens
Intensity:          3000
Attenuation Radius: 1000
Use Inverse Squared Falloff: On
Cast Shadows:       On
Light Color:        White
```

如果 Details 中属性分类折叠，直接搜索英文属性名。设置完成后，在 Lit View 中房间表面应能被看见，墙角和门窗附近应出现明暗变化。

### 为什么现在需要一盏临时灯

曝光只能解释“已有光能如何映射到屏幕亮度”，不能让完全没有照明的房间凭空变亮。Empty Level 中没有可靠的照明条件，因此本篇先引入一个可记录的输入。

选择 Point Light 是因为它不需要先建立天空、大气和太阳关系，变量较少；选择 Movable 是为了修改强度后立即观察，不依赖 Lightmass 烘焙。`3000 lm` 只是课程的中性比较值，不是室内照明规范，也不是最终艺术参数。

Point Light 向多个方向投影阴影，其成本与 Directional Light 或 Spot Light 不同，不能用本篇性能数值代表最终灯光成本。第 07 篇建立正式灯光后，应删除、禁用或重新定义这个 Actor，不能让临时灯与正式灯叠加而不知情。

## 3. 创建固定 CameraActor

1. 选择 **Add > Cinematic > Camera Actor**。如果 Add 菜单分类有布局差异，搜索 `Camera Actor`。
2. 重命名为 `Camera_Room_Baseline`。
3. 放入 `02_Cameras` Folder。
4. 在 Details 设置：

```text
Location:               0, -250, 160
Rotation Pitch/Yaw/Roll: -3, 90, 0
Projection Mode:         Perspective
Field Of View:           70
Constrain Aspect Ratio:  On
Aspect Ratio:            1.777778
Post Process Blend Weight: 0
```

UE 使用 X Forward、Y Right、Z Up。这里将 Yaw 设为 `90°`，使相机从靠近门口的位置朝房间后方观察；`160 cm` 接近站立视点；轻微向下的 Pitch 能同时保留地面、墙面和天花板边界。

### 为什么使用 CameraActor，而不是 CineCameraActor

CameraActor 已经能保存 Transform、透视投影、FOV 和长宽比，足以承担调试与画面对比。CineCameraActor 额外提供 Filmback、焦距、镜头和对焦工作流，适合 Sequencer 与电影镜头，但会在本篇引入无关变量。

`70°` 是当前房间的观察基线，不是通用最佳 FOV。更大的 FOV 能覆盖更多房间，但会加强边缘透视形变；更小的 FOV 更接近长焦观察，却可能需要把相机移到墙外。第 09 篇形成最终画面时可以更换镜头，但性能和画质对比应继续保留这台基准相机。

CameraComponent 的 `Field Of View` 在当前透视路径中表示水平视场角。70° 描述相机从画面左边到右边能覆盖的角度，不是焦距毫米数，也不是相机离墙的距离。相同位置下增大 FOV 会让更多物体进入 View，并减小单个物体的屏幕覆盖；这不仅改变构图，也会影响 LOD、Nanite、遮挡和像素成本的比较条件。

`Aspect Ratio = 1.777778` 表示宽/高约为 16:9。开启 `Constrain Aspect Ratio` 后，目标 Viewport 比例不一致时，UE 用黑边保留相机要求的成像区域，而不是拉伸画面。UE 还存在“保持水平 FOV”或“保持垂直 FOV”的轴约束；当前通过固定 16:9 输出和 Constrain Aspect Ratio 避免让窗口形状改变有效投影。以后若关闭比例约束，必须重新记录 FOV 轴策略，不能只保留数值 `70`。

`Constrain Aspect Ratio` 会在目标 Viewport 比例不一致时加入黑边，保护构图。它不会把任意窗口都变成相同像素数；截图分辨率仍需单独固定。

相机自身也能携带 Post Process Settings。这里把 `Post Process Blend Weight` 设为 `0`，让全局基线只有下一节的 Post Process Volume 一个所有者。以后若需要某个镜头专属的景深或曝光，可以重新启用相机后处理，但必须记录它与 Volume 的混合关系。

## 4. Pilot、Eject 与锁定相机

1. 在 World Outliner 选中 `Camera_Room_Baseline`。
2. 右键选择 **Pilot 'Camera_Room_Baseline'**；也可以使用 `Ctrl+Shift+P`。
3. Viewport 顶部应出现正在 Pilot 该 Actor 的提示。
4. 使用常规 Viewport 操作微调构图时，注意此时移动的是 CameraActor，不是临时自由视角。
5. 使用 Viewport 中的 **Stop Piloting Actor** / Eject 按钮退出。
6. 把 Transform 恢复为上一节的精确数值，保证课程基线一致。
7. 在 Outliner 右键相机，选择 **Transform > Lock Actor Movement**。

锁定只防止常规编辑中误移动 Actor，不是权限或运行时约束。需要重新构图时，先取消 Lock，改完后重新锁定并记录新基线。

以后进入固定视图时，在 Viewport 的相机/透视下拉菜单中选择 **Placed Cameras > Camera_Room_Baseline**。不同 5.7 编辑器布局可能把 Placed Cameras 放在 Viewport Camera 菜单中，但 Actor 名称应一致。

Pilot 与“从 Placed Cameras 观看”不是同一种状态。Pilot 把 Viewport 导航输入写回 CameraActor Transform，适合构图；普通相机视图只使用 CameraActor 生成 View，退出到自由透视后不会修改 Actor。锁定 Actor Movement 是防误编辑措施，而不是让渲染使用固定矩阵的开关：真正固定基线的是保存下来的 CameraActor 参数和每次明确选择该相机。

### 成功检查

- 切回自由 Perspective 后移动视角，不会改变 CameraActor 的 Transform。
- 再选择 `Camera_Room_Baseline`，画面会回到同一构图。
- 锁定后拖动相机 Gizmo 不应改变它的位置。

## 5. 固定 Level Viewport 条件

在用于对比的 Viewport 中设置：

1. 进入 `Camera_Room_Baseline`。
2. View Mode 选择 **Lit**。
3. 在 Viewport Options 中确认 **Realtime** 已启用。
4. 按 `G` 开启 **Game View**，隐藏选择轮廓、图标、Volume 线框和其他编辑器辅助显示。
5. 打开 **Performance and Scalability > Viewport Scalability**，将各组设为 `Epic`。
6. 打开 **Performance and Scalability > Material Quality Level**，选择 `High`。
7. 打开 **Performance and Scalability > Screen Percentage**，启用当前 Viewport Override，并设为 `100%`。

完成后，记录当前条件：

```text
View Mode:          Lit
Realtime:           On
Game View:          On
Viewport Quality:   Epic
Material Quality:   High
Screen Percentage:  100%
Camera:             Camera_Room_Baseline
```

### 这些设置分别控制什么

- **Lit** 使用当前项目的正常照明路径；Unlit 会绕过主要光照，不能代替最终画面。
- **Realtime** 允许动态灯光、曝光和时间相关效果持续更新。关闭后画面可能只在交互时刷新。
- **Game View** 主要清除编辑器辅助元素，不等于启动 Play In Editor，也不会自动使用 GameMode 中的玩家相机。
- **Viewport Scalability** 会改变阴影、效果、后处理、GI 等质量和成本。低档位截图不能与 Epic 直接比较。
- **Material Quality Level** 可以选择不同材质编译分支。
- **Screen Percentage** 决定主渲染分辨率与上采样输入。即使输出窗口大小相同，内部像素工作量也可能不同。

这些是课程的桌面画质基线，不是性能目标。第 29 篇会在多个 Scalability 档位上重新验收。硬件无法交互运行 Epic 时可以暂时降低，但必须把该截图标成非基线，不能静默替换本篇记录。

这些 Viewport 条件也不全部属于 Level Asset：CameraActor 和 Post Process Volume 保存在地图中，而 Game View、Realtime、Viewport Scalability、Material Quality 与部分 Screen Percentage Override 可能属于当前编辑器 Viewport 或用户配置。换机器、重置布局或打开另一个 Viewport 后，它们不一定跟随地图恢复。因此每次对比都要重新核对记录值；“上一回保存过 Level”不能证明当前 View 已经使用同样的观察条件。

成功检查应包含一个有意的对照：临时把 Screen Percentage 改为非 100% 并观察显示值或画面变化，再恢复 100%；临时退出 Game View 确认编辑器图标重新出现，再进入 Game View。这个过程证明你知道每个状态由哪个 Viewport 控制，而不是只抄下最终列表。

## 6. 创建全局 Post Process Volume

1. 选择 **Add > Volumes > Post Process Volume**。
2. 重命名为 `PPV_Room_Baseline`。
3. 放入 `03_Volumes` Folder。
4. 在 **Post Process Volume Settings** 中设置：

```text
Enabled:                   On
Infinite Extent (Unbound): On
Blend Weight:              1.0
Priority:                  0
```

5. 展开 **Post Process Settings > Exposure**。
6. 勾选下列属性左侧的 Override，并设置：

```text
Metering Mode:                  Manual
Exposure Compensation:         0.0
Apply Physical Camera Exposure: On
Aperture (F-stop):              4.0
Shutter Speed (1/s):            8.0
ISO:                            400
```

只勾选本篇明确设置的六个 Camera/Exposure Override。Bloom、Color Grading、GI、Reflections、Motion Blur 等保持未 Override，避免在对应章节之前制造隐式画面状态。

### Enabled、Unbound、Blend Weight 与 Priority 分别控制什么

这四个字段共同决定一个 Volume 是否参与 View 的最终后处理，但职责不同：

- **Enabled**：决定该 Volume 是否进入计算。关闭后，即使 Override 和参数仍保存在 Actor 上，它也不贡献最终设置。
- **Infinite Extent (Unbound)**：决定贡献是否受空间边界约束。关闭时，只有位于 Volume 内部或 Blend Radius 范围内的 View 才受影响；开启后，它覆盖整个 World。
- **Blend Weight**：决定该 Volume 的有效设置以多大权重混入当前 View。`0` 没有效果，`1` 为完整贡献，中间值用于过渡。它不会自动让未勾选 Override 的字段参与混合。
- **Priority**：决定多个 Post Process Volume 的应用顺序。UE 按 Priority 从低到高处理，因此后处理的高 Priority Volume 可以在其有效字段上覆盖或继续混合较低 Priority 的结果。相同 Priority 仍会按体积范围和稳定排序规则处理，但不应依赖同优先级的隐式顺序表达设计意图。

本篇使用 `Enabled = On`、`Unbound = On`、`Blend Weight = 1`、`Priority = 0`，含义是建立一个完整生效的全局基础层，而不是强行压过以后所有局部设置。后续局部 Volume 可以使用更高 Priority 和空间 Blend；相机自己的 Post Process 还会在 View 构建过程中参与最终混合。因此“当前 Volume 的 Details 数值正确”不等于它独占最终结果。

### Override 为什么必须单独勾选

Post Process Settings 保存大量字段，但每个字段还有独立的 `bOverride_...` 状态。只有勾选 Override 的字段才表达“这个来源要参与决定最终值”；未勾选字段显示的数值只是结构中的当前值，不应覆盖其他来源或引擎默认。

这也是本篇只勾选六项的原因：我们要固定曝光，不要顺手把 Bloom、GI 或 Color Grading 也变成全局课程合同。验证时不仅要读数值，还要检查每个字段左侧 Override；值相同但 Override 状态不同，最终 View 的所有权关系不同。

### 为什么使用 Unbound Volume

普通 Post Process Volume 只在 View 位于体积内部或 Blend Radius 范围内时参与混合。`Infinite Extent (Unbound)` 让它对整个关卡生效，因此自由视口、门外视点和固定相机都能共享曝光基线。

代价是它成为全局状态。以后添加局部 Volume 或相机后处理时，最终结果会按 Priority、Blend Weight、空间关系和 Override 字段混合。只看某一个 Volume 的 Details，不能证明它独占最终设置。

### 为什么选择 Manual Physical Exposure

自动曝光会从当前画面亮度估计目标曝光。相机从室内转向窗外、隐藏天花板或替换亮色材质时，即使灯光没有变化，画面也可能被重新缩放。它适合模拟视觉适应和处理大动态范围场景，却会破坏早期单变量比较。

Manual 模式固定曝光，不再根据当前画面统计量持续适应。开启 `Apply Physical Camera Exposure` 后，UE 5.7 使用 Aperture、Shutter Speed 和 ISO 形成基础 EV100：

```text
EV100 = log2(Aperture² × ShutterSpeed × 100 / ISO)
      = log2(4² × 8 × 100 / 400)
      = 5
```

`3000 lm` Point Light 与 `EV100 = 5` 为当前房间提供一组使用物理单位、容易记录的室内起点。它仍不是摄影规范或最终艺术曝光；价值在于后续每次观察都能恢复同一输入。

三个物理相机参数在这条 Manual Exposure 计算中承担不同数学角色：

- **Aperture / F-stop** 以平方进入 EV。f-number 增大表示孔径相对变小；从 f/4 改到接近 f/5.6 会提高约一档 EV，使画面变暗。在启用景深时，它还会影响景深模型。
- **Shutter Speed (1/s)** 这里填分母，例如 `8` 表示约 1/8 秒。分母加倍到 `16` 会提高一档 EV，使曝光变暗。
- **ISO** 位于分母。ISO 从 400 加倍到 800 会降低一档 EV，使曝光变亮。

`f/4、1/8 s、ISO 400` 的选择目标是得到可记录的 `EV100 = 5`，并让三个数值容易识别，不是宣称虚拟房间必须使用某套真实摄影参数。其他组合可以得到同一 EV，例如 f/4、1/4 s、ISO 200 仍为 EV100 5；它们在当前只观察 Manual Exposure 时能形成相同基础曝光，但如果以后启用景深或其他相机模型，参数不一定继续完全等价。因此课程固定完整三元组，而不是只写“EV5”。

UE 5.7 的 Manual Exposure 源码直接使用 `log2(FStop² × ShutterSpeed × 100 / ISO)` 计算物理相机 EV100。这条源码证据支持公式和参数方向；它不证明当前灯光具有艺术正确性，也不替代对最终 View 中 Override、Volume 混合和实际亮度的验证。

UE 的 `Aperture (F-stop)` 同时也是景深模型会使用的参数。当前没有为基准相机启用镜头景深，因此本篇只观察它对物理曝光的作用；第 09 篇若加入景深，调整 Aperture 会同时改变曝光与景深，届时必须用 ISO、Shutter Speed 或补偿值重新解耦画面亮度，而不能把它当作只控制虚化的旋钮。

替代方案是关闭物理相机曝光，此时 Manual 模式不再使用 Aperture、Shutter Speed 和 ISO，需要用 Exposure Compensation 或固定 EV 路径定义亮度。那种方案更简洁，却容易与本篇已经选择的 Lumens 光源失去直观联系。课程因此保留物理曝光，并把全部参数写入基线。

`Exposure Compensation` 以档位进行对数调整：`+1` 约为两倍亮，`-1` 约为一半亮。当前使用 `0` 是为了让 EV100 直接由已记录的物理相机参数决定，不代表最终画面必须为 0。如果当前画面不美观，先检查灯光、单位和相机参数是否正确，不要立即用补偿值掩盖问题。

## 7. 验证固定曝光确实生效

保持固定相机和其他条件不变：

1. 把 `Light_Room_Baseline` 的 Intensity 从 `3000 lm` 临时改为 `300 lm`。
2. 观察 Manual 模式下画面明显变暗。
3. 临时把 `Metering Mode` 改为 `Auto Exposure Histogram`，等待数秒，观察系统是否尝试把暗画面重新抬亮。
4. 恢复 `Metering Mode = Manual`。
5. 恢复灯光 `Intensity = 3000 lm`。
6. 确认最终画面和开始时一致，再保存。

本实验只证明自动模式会根据图像亮度改变曝光，而 Manual 保持固定映射。它不能单独证明某个像素来自哪一条光照路径，也不能把一次视觉比较当作曝光算法的完整验证。

如果 Auto 与 Manual 看起来完全相同，先检查：

- Metering Mode 左侧 Override 是否真正勾选。
- 相机自己的 `Post Process Blend Weight` 是否仍为 `0`。
- 是否存在另一个更高 Priority 的 Unbound Post Process Volume。
- Realtime 是否开启，是否给自动曝光留出了适应时间。

恢复步骤本身也是实验的一部分。最终必须把灯光、Metering Mode 和所有 Override 恢复到记录值，再用固定相机截图对照；否则这次“验证 Manual”的实验会污染后续基线。不要用 `Ctrl+Z` 是否可用来猜测恢复完成，应逐项读取最终状态。

## 8. 生成可复现的基准截图

1. 进入 `Camera_Room_Baseline`。
2. 确认 Game View、Lit、Epic、High、100% 和 Manual Exposure 均符合记录。
3. 等待 Shader 编译和纹理加载结束。
4. 打开编辑器 Console，执行：

```text
HighResShot 1920x1080
```

5. 在项目的 `Saved/Screenshots/` 平台子目录中找到输出图，确认分辨率为 1920 × 1080。
6. 若需要把基准纳入版本管理，将它复制到项目根目录下：

   `SourceAssets/Reference/Baselines/03_Room_CameraExposure.png`

`Saved` 通常不提交版本管理，不能把其中的文件当作长期课程记录。参考图也不需要导入 Content；否则它会成为运行时 Asset 候选并产生不必要的导入与 Cook 管理。

`HighResShot` 固定输出像素数，CameraActor 固定构图比例，两者解决的是不同问题。只固定其中一个仍不足以形成完整画面基线。

## 9. 保存最终状态

执行 **File > Save All**。最终 Outliner 应至少包含：

```text
01_Lights/
└── Light_Room_Baseline
02_Cameras/
└── Camera_Room_Baseline
03_Volumes/
└── PPV_Room_Baseline
```

关闭并重新打开编辑器后再次检查：

- CameraActor Transform、FOV 和 Aspect Ratio 保留。
- Post Process Volume 的六个 Camera/Exposure Override 保留。
- 固定相机画面与基准截图构图一致。
- 灯光强度已恢复为 `3000 lm`。

Viewport 的部分显示状态属于编辑器用户配置，不一定随 Level 资产提交给其他机器。进入对比前仍应按本篇记录重新确认，而不是只相信上次关闭编辑器时的状态。

## 常见问题

### Pilot 后相机位置被改乱

退出 Pilot，在 Details 中恢复本篇 Transform，然后重新锁定 Actor Movement。不要用自由拖动大致找回位置。

### 画面全黑

确认临时 Point Light 可见、Intensity 为 `3000 lm`、Attenuation Radius 覆盖房间、Actor 未被隐藏，并确认 View Mode 是 Lit。随后检查 Exposure Override，而不是直接把 Exposure Compensation 调到极高。

### 画面亮度仍会缓慢变化

通常是另一个 Volume 或相机后处理仍在提供自动曝光。搜索 Outliner 中全部 Post Process Volume，检查 Priority、Unbound、Blend Weight 和 Metering Mode；同时确认相机的 Post Process Blend Weight 为 `0`。

### 黑边太宽

这是 16:9 Camera 进入非 16:9 Viewport 后的约束结果。最大化 Viewport 或调整编辑器布局可以增大有效画面，但不要为了填满窗口而关闭 `Constrain Aspect Ratio`，否则构图会随窗口比例变化。

### Game View 中无法看到 Volume 和灯光图标

这是预期结果。按 `G` 退出 Game View 后再编辑，截图前重新进入 Game View。

### Epic 档位交互太慢

编辑时可以临时降低画质，但截图和性能对比前必须恢复记录值。后续性能章节还会区分“用于看最终质量的基线”和“用于定位问题的调试配置”。

## 对渲染和后续工程的意义

- 固定 Camera 冻结可见集合、投影和屏幕覆盖率，是材质、阴影、LOD、Nanite 与 GPU 对比的前提。
- 固定 Exposure 防止亮度统计补偿真实的灯光或材质变化。
- 固定 Screen Percentage 和 Scalability 防止内部渲染分辨率与功能档位改变成本。
- Unbound Post Process Volume 提供全关卡基线，但后续必须管理它与局部 Volume、相机设置的混合。
- 临时 Point Light 只是已记录的输入，不能演变成无人管理的最终灯光。

## 可选延伸与 UE 5.7 校准锚点

- `RenderingDeep/08_FrameInit.md`：View 如何建立当前帧的观察条件和可见工作集。
- `RenderingDeep/15_PostProcessing.md`：SceneColor、曝光和后处理如何形成最终输出。
- `Engine/Source/Runtime/Engine/Private/Camera/CameraComponent.cpp`：`FieldOfView`、`AspectRatio` 和 Camera Post Process 如何写入 View。
- `Engine/Source/Runtime/Engine/Classes/Engine/Scene.h`：`Metering Mode`、`Exposure Compensation` 和 `Apply Physical Camera Exposure` 的属性定义。
- `Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp`：Manual Exposure 对物理相机参数的使用边界。

这些源码锚点用于校准职责与属性，不要求在完成本篇前阅读。

## 完成后的工程状态

- [ ] `Camera_Room_Baseline` 已创建、锁定并放入 `02_Cameras`
- [ ] 相机使用 70° FOV、16:9 Aspect Ratio 和固定 Transform
- [ ] `Light_Room_Baseline` 已恢复为 3000 lm
- [ ] `PPV_Room_Baseline` 为 Unbound，且只覆盖本篇六个 Camera/Exposure 字段
- [ ] 能解释 Enabled、Unbound、Blend Weight、Priority 和字段 Override 各自控制的层级
- [ ] Manual Physical Exposure 为 f/4、1/8 s、ISO 400、EV100 5
- [ ] Manual Exposure 已通过单变量切换验证
- [ ] 对比 Viewport 使用 Lit、Realtime、Game View、Epic、High 和 100%
- [ ] 已生成 1920 × 1080 基准截图
- [ ] 地图和全部 Actor 设置已保存并重新打开验证

下一篇将导入第一套外部 Static Mesh 建筑模块，用固定相机检查模块替换前后的尺度、轮廓和画面变化。
