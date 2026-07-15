# 00 创建 UE5.7 RenderingPractice 工程

## 本篇结果

完成后你将拥有一个可由当前 UE 5.7 源码版打开和编译的空白 C++ 工程：

`D:\Unreal\EnrealEngine\Project\RenderingPractice`

本篇不搭场景。重点是熟悉源码版编辑器入口、Project Browser、`.uproject`、项目目录和首次编译。

## 开始前

需要：

- 当前 UE 5.7 源码工作树：`D:\Unreal\EnrealEngine\UnrealEngine`
- Windows 10/11
- Visual Studio 2022，安装“使用 C++ 的游戏开发”和 Windows SDK
- 足够空间保存工程、Shader 和派生数据

如果 `Engine\Binaries\Win64\UnrealEditor.exe` 不存在，先按引擎根目录的源码构建流程生成并编译 `UnrealEditor Win64 Development`。本课程不在此重复完整引擎编译教程。

## 1. 启动 Project Browser

1. 在资源管理器打开：

   `D:\Unreal\EnrealEngine\UnrealEngine\Engine\Binaries\Win64`

2. 双击 `UnrealEditor.exe`。
3. 没有指定项目时，编辑器应打开 Unreal Project Browser。

如果直接打开了最近项目，在编辑器中选择 **File > Open Project**，再回到 Project Browser。

### 为什么从源码版 UnrealEditor 启动

课程后半段会涉及自定义 Shader、RDG Pass、项目模块和源码定位。如果从 Epic Launcher 的二进制引擎创建工程，前面的编辑器操作同样可以完成，但后续切换到源码引擎时还要重新绑定、生成项目文件并处理模块兼容。

因此本课程从第一天就使用当前 UE 5.7 源码版，保证项目、符号、Shader 和引擎源码处于同一版本。代价是首次编译和启动时间更长，磁盘占用也明显高于二进制版。

## 2. 创建 Blank C++ 工程

Project Browser 的卡片和分类在小版本中可能略有调整，以字段名称为准：

### 先理解这些选择

| 选项 | 本课程选择 | 它控制什么 | 为什么这样选 | 有意义的替代方案 |
|---|---|---|---|---|
| Category | Games | 选择模板集合和默认项目用途 | 本课程需要标准 Game World、Camera、Play、Scalability 和实时渲染工作流 | Film/Video、AEC 等模板会带入特定插件和流程，不适合作为通用基线 |
| Template | Blank | 决定初始地图、代码、输入和预置 Actor | 从空工程亲手建立环境，避免 Third Person 等模板隐藏角色、输入、灯光和项目配置 | Third Person 适合玩法原型；Open World 适合直接研究大世界，但会过早引入 World Partition |
| Project Type | C++ | 是否从创建时生成项目模块和 Source 目录 | 后续需要 Global Shader、RDG、调试模块和源码断点；C++ 工程仍可正常使用 Blueprint | Blueprint 工程启动更轻，但后续首次添加 C++ 类时仍会生成模块并转换工程 |
| Target Platform | Desktop | 设置面向桌面/主机还是移动平台的默认能力取向 | 课程以 Windows D3D12、SM6 和完整 UE5 渲染功能为基线 | Mobile 会采用移动渲染限制，适合移动端专项课程 |
| Quality Preset | Maximum | 决定模板初始的质量与性能取向 | 先建立完整桌面画质基线，再在 Scalability 章节系统学习降级 | Scalable 更适合低端硬件起步，但会让部分默认效果和本文截图产生差异 |
| Starter Content | No Starter Content | 是否复制 Epic 的示例材质、纹理、Mesh 和关卡 | 保持资产来源清晰，后续每个环境资产和材质都知道从哪里产生 | 启用 Starter Content 可快速原型，但容易绕过材质和资产创建教学 |
| Ray Tracing | Off | 是否在项目创建时启用硬件光线追踪相关配置 | 先减少驱动、硬件能力和 Shader permutation 变量；Lumen 可先使用软件追踪，VSM 不依赖硬件光追 | 到 Lumen 硬件追踪对比章节再启用，需要兼容 GPU、D3D12/SM6，并可能触发大量 Shader 重编译 |

这里的选择是课程基线，不是所有 UE 项目的最佳实践。移动游戏、纯 Blueprint 原型、影视渲染或已有资产库的项目会有不同选择。

1. 选择 **Games**。
2. 选择 **Blank** 模板。
3. Project Type 选择 **C++**，不要选 Blueprint。
4. Target Platform 选择 **Desktop**。
5. Quality Preset 选择 **Maximum**。
6. Starter Content 选择 **No Starter Content**。
7. Ray Tracing 暂时关闭；后续只在 Lumen 硬件追踪对比时重新评估。
8. Project Location 设为：

   `D:\Unreal\EnrealEngine\Project`

9. Project Name 输入：

   `RenderingPractice`

10. 点击 **Create**。

### 为什么工程放在 Engine 仓库之外

课程工程放在：

`D:\Unreal\EnrealEngine\Project\RenderingPractice`

而不是放进 `UnrealEngine` 源码仓库。这样可以：

- 避免项目资产和生成文件污染庞大的引擎 Git 状态。
- 让工程独立绑定、升级和版本管理。
- 允许同一份源码引擎服务多个项目。
- 避免误把项目的 `Binaries`、DDC 或 Saved 当成引擎文件。

工程名使用 `RenderingPractice`，因为 UE 会用它生成模块名、Target、Solution 和部分代码符号。项目创建后再重命名会涉及 `.uproject`、模块、Target.cs、Build.cs 和源码符号，不适合作为普通文件夹改名。

## 3. 等待首次生成和编译

首次创建源码版 C++ 工程可能经历：

- 生成项目文件
- 编译 `RenderingPracticeEditor`
- 启动编辑器
- 编译基础 Shader

不要在 Shader 编译尚未结束时判断画面或性能。编辑器右下角通常会显示 Shader 编译进度。

### 首次等待实际在做什么

- **生成项目文件**：让 Visual Studio 认识项目模块、引擎模块和构建目标。
- **编译 Editor 模块**：生成能被 `UnrealEditor.exe` 加载的项目 DLL。
- **编译 Shader**：根据当前 RHI、Shader Model、材质和项目设置生成 GPU 程序。
- **构建 DDC**：缓存派生资源，后续打开相同内容时可以复用。

因此“编辑器窗口已经出现”不等于环境已经稳定。Shader 和 DDC 尚未完成时，画面可能临时使用默认材质，帧时间也没有比较价值。

如果项目模块编译失败：

1. 关闭编辑器。
2. 在项目目录右键 `RenderingPractice.uproject`。
3. 选择 **Generate Visual Studio project files**。
4. 打开生成的 `RenderingPractice.sln`。
5. Visual Studio 配置选择 **Development Editor** 和 **Win64**。
6. 编译 `RenderingPractice` 或整个 Solution。
7. 再双击 `.uproject`。

`Development Editor | Win64` 的含义是：为 Windows 64 位编辑器构建开发配置。它包含足够的调试信息和开发能力，又不会像 Debug 配置那样极慢。后续打包游戏时会使用不同 Target 和配置。

## 4. 认识工程目录

关闭或最小化编辑器，在资源管理器查看工程：

```text
RenderingPractice/
├── Config/
├── Content/
├── Source/
├── RenderingPractice.uproject
├── Binaries/             生成目录
├── DerivedDataCache/     派生数据
├── Intermediate/         中间文件
└── Saved/                日志、自动保存、临时数据
```

需要长期保留和版本管理的是：

- `Config`
- `Content`
- `Source`
- `.uproject`
- 后续可能创建的 `Plugins`

通常不提交：

- `Binaries`
- `DerivedDataCache`
- `Intermediate`
- `Saved`

这和 Unity 中保留 `Assets`、`Packages`、`ProjectSettings`，忽略 `Library`、`Temp`、`Logs` 的思路相近。

### 各目录为什么重要

| 目录 | 主要内容 | 对课程的影响 |
|---|---|---|
| `Config` | 项目和平台配置 | RHI、默认地图、渲染功能、输入和 Scalability 等设置会落入这里 |
| `Content` | `.uasset`、`.umap` 等资产 | 主案例的地图、材质、Mesh、Blueprint 和 PCG 内容都在这里 |
| `Source` | C++ 模块 | 后续 Global Shader、RDG 和调试代码的入口 |
| `Plugins` | 可独立启停的功能模块 | 适合隔离工具或实验性渲染扩展，后续按需创建 |
| `DerivedDataCache` | Shader、压缩纹理、派生 Mesh 等缓存 | 可重新生成，不应当作源资产；删除会换来较长重建时间 |
| `Intermediate` / `Binaries` | 编译中间物和输出 | 与本机工具链和配置相关，不是课程源数据 |
| `Saved` | 日志、崩溃、自动保存和临时状态 | 排错很有用，但不作为正式资产来源 |

## 5. 确认项目使用当前源码引擎

找到 `RenderingPractice.uproject`：

1. 右键选择 **Switch Unreal Engine version**。
2. 选择与当前源码工作树对应的 UE 5.7 引擎注册项。
3. 确认后重新生成项目文件。

如果双击 `.uproject` 能启动当前源码版编辑器，并且 **Help > About Unreal Editor** 显示 5.7，则绑定正确。

### EngineAssociation 控制什么

`.uproject` 中的 `EngineAssociation` 告诉桌面系统应该用哪个已注册引擎打开项目。它不复制引擎，也不保证项目模块已经兼容；切换引擎版本后通常需要重新生成项目文件并编译。

本课程固定 UE 5.7，是为了避免菜单、默认配置、源码符号和 Shader 行为在版本间漂移。后续若升级版本，应当把它当作一次迁移任务，而不是直接覆盖打开。

## 6. 建立最小渲染配置

在编辑器中选择 **Edit > Project Settings**：

1. 搜索 `Default RHI`。
2. Windows Default RHI 选择 **DirectX 12**。
3. 确认 DirectX 12 对应的 Shader Model 6 目标已启用。
4. 暂时保留模板默认的 Lumen、Nanite 和 VSM 设置；具体系统在对应章节调整。
5. 修改 RHI 后重启编辑器。

这里仅固定平台基础，不提前关闭 UE5 功能。后续章节会通过明确案例解释每个功能为何开启或关闭。

### 为什么选择 D3D12 与 SM6

- D3D12 是 Windows 上 Nanite、硬件光追以及多项现代 UE5 渲染能力的主要路径。
- SM6 提供现代 HLSL/DXIL 能力，是许多 UE5 高端特性的基础。
- 后续 GPU Visualizer、RenderDoc、Nanite、Lumen、VSM 和 MegaLights 都以这条桌面路径为主要环境。

替代方案并非“错误”：D3D11 兼容面更广、调试链更成熟，移动 RHI 面向完全不同的硬件约束，Vulkan 适合跨平台。但如果课程一开始允许多条 RHI 并行，Shader、事件树和功能可用性会出现大量分叉，不利于建立共同基线。

### 如何确认设置生效

修改 Default RHI 后必须重启，因为 RHI 在编辑器启动早期选择。重启后可以通过 **Help > About Unreal Editor**、启动日志或后续 GPU 捕获确认当前使用 D3D12。仅在 Project Settings 中看到下拉框已改变，不代表当前进程已经切换。

## 常见问题

### 找不到 C++ 选项

通常是 Visual Studio C++ 工作负载未安装，或编译器/Windows SDK 未被 UnrealBuildTool 识别。

### 创建后提示 Missing Modules

先生成 Visual Studio 项目文件并使用 `Development Editor | Win64` 编译。不要删除 `Source` 规避问题。

### 工程被错误引擎打开

使用 **Switch Unreal Engine version** 重新绑定，再生成项目文件。

### 首次启动很慢

源码版模块加载、Shader 编译和 DDC 构建都可能产生较长等待。等状态稳定后再进行下一篇。

## 对后续渲染学习的意义

- Project Settings 会决定 RHI、Shader Model、Lumen、Nanite、VSM 和默认渲染路径。
- `Config` 中保存的不是普通用户偏好，而可能直接改变 Shader permutation 和项目运行方式。
- `Source` 和后续 `Plugins` 将承载自定义 Shader、RDG 和调试工具。

## 完成状态

- [ ] 工程位于 `D:\Unreal\EnrealEngine\Project\RenderingPractice`
- [ ] 使用 UE 5.7 源码版打开
- [ ] C++ 模块能以 Development Editor Win64 编译
- [ ] 编辑器能正常启动
- [ ] D3D12 / SM6 已确认
- [ ] Shader 编译已稳定

下一篇将进入编辑器，创建课程资产目录和第一张 Sandbox 地图。
