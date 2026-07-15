# 00 创建 UE5.7 RenderingPractice 工程

## 本篇结果

完成后你将拥有一个可由当前 UE 5.7 源码版打开和编译的空白 C++ 工程：

`D:\Unreal\EnrealEngine\Project\RenderingPractice`

本篇不搭场景。重点是熟悉源码版编辑器入口、Project Browser、`.uproject`、项目目录和首次编译。

## 开始前

需要：

- 当前 UE 5.7 源码工作树：`D:\Unreal\EnrealEngine\UnrealEngine`
- Windows 10/11
- JetBrains Rider，并启用 Unreal Engine 支持
- Microsoft C++ 编译工具链和 Windows SDK
- 足够空间保存工程、Shader 和派生数据

Rider 是本课程使用的 IDE，但 Windows 上编译 UE C++ 仍然需要 Microsoft C++ 编译器、链接器和 Windows SDK。IDE 负责代码浏览、项目模型、调试界面和构建命令入口；真正解析 Target、Module 和依赖关系的是 UnrealBuildTool，最终执行本机代码编译的是 Microsoft 工具链。安装 Rider 不能替代编译器，安装编译器也不要求你把 Visual Studio 当作日常 IDE。

开始前应在 Rider 的 Toolchain 设置或首次打开 UE 工程时确认它能发现有效的 MSVC 与 Windows SDK。若 UnrealBuildTool 报告找不到编译器，问题发生在构建工具链层，不应通过把工程改成 Blueprint 或删除 `Source` 来规避。

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

### 为什么工程放在 Engine 源码树之外

课程工程放在：

`D:\Unreal\EnrealEngine\Project\RenderingPractice`

而不是放进 `UnrealEngine` 源码树。这样可以：

- 让引擎源码刷新与项目内容修改保持清晰的目录边界。
- 让同一份源码引擎服务多个项目，而不把项目模块误当作 Engine Module。
- 让项目自己的 `Config`、`Content`、`Source` 和 `.uproject` 具有独立的生命周期。

这里要区分“源码树边界”和“Git 仓库边界”。本工作区唯一的 Git 根是 `D:\Unreal`，所以 `EnrealEngine\Project\RenderingPractice` 虽然位于 Engine 源码树之外，仍属于同一个 Git 仓库；不要在项目目录再次执行 `git init`。项目的 authored Asset、配置和源码应由这个现有仓库管理，`Binaries`、`DerivedDataCache`、`Intermediate`、`Saved` 等生成内容通过路径局部的 `.gitignore` 排除。

工程名使用 `RenderingPractice`，因为 UE 会用它生成模块名、Target、Solution 和部分代码符号。项目创建后再重命名会涉及 `.uproject`、模块、Target.cs、Build.cs 和源码符号，不适合作为普通文件夹改名。

## 3. 等待首次生成和编译

首次创建源码版 C++ 工程可能经历：

- 生成项目文件
- 编译 `RenderingPracticeEditor`
- 启动编辑器
- 编译基础 Shader

不要在 Shader 编译尚未结束时判断画面或性能。编辑器右下角通常会显示 Shader 编译进度。

### 首次等待实际在做什么

- **生成项目/IDE 元数据**：让 Rider 等工具根据 UnrealBuildTool 的 Target、Module 和依赖信息建立可导航、可构建的项目模型；这些元数据不是 UE 真正的构建规则来源。
- **编译 Editor 模块**：生成能被 `UnrealEditor.exe` 加载的项目 DLL。
- **编译 Shader**：根据当前 RHI、Shader Model、材质和项目设置生成 GPU 程序。
- **构建 DDC**：缓存派生资源，后续打开相同内容时可以复用。

因此“编辑器窗口已经出现”不等于环境已经稳定。Shader 和 DDC 尚未完成时，画面可能临时使用默认材质，帧时间也没有比较价值。

如果项目模块编译失败：

1. 关闭编辑器。
2. 在 Rider 中打开 `RenderingPractice.uproject`，等待 Rider 与 UnrealBuildTool 建立项目模型。
3. 构建目标选择 `RenderingPracticeEditor`，平台选择 `Win64`，配置选择 `Development`。
4. 执行 Build，确认失败发生在项目模块而不是仍在运行的编辑器占用了 DLL。
5. 构建成功后再双击 `.uproject`。

如果 Rider 的项目模型尚不可用，可直接从 PowerShell 调用与 IDE 相同的构建入口：

```powershell
& 'D:\Unreal\EnrealEngine\UnrealEngine\Engine\Build\BatchFiles\Build.bat' `
  RenderingPracticeEditor Win64 Development `
  -Project='D:\Unreal\EnrealEngine\Project\RenderingPractice\RenderingPractice.uproject' `
  -WaitMutex
```

这条命令不是绕过 Rider 创建另一套构建系统。Rider 最终同样让 UnrealBuildTool解析 `RenderingPracticeEditor` Target、项目 Module 和 Engine Module 依赖，再调用 MSVC 完成本机编译。直接运行脚本的价值是把“IDE 项目模型问题”和“UE 工程本身编译失败”分开。

`RenderingPracticeEditor / Development / Win64` 的含义是：构建能加载进 Windows 64 位 Unreal Editor 的项目 Target，并使用适合日常开发和调试的优化级别。`Editor` Target 会链接编辑器所需模块；它不是最终独立游戏。后续打包会改用 Game Target 以及 Development、Test 或 Shipping 等配置，因此“编辑器模块编译成功”只证明当前编辑工作环境成立，不代表打包链路已经验证。

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

这一节会修改两个不同层级。先把它们分开：

```text
Default RHI = DirectX 12
决定 Windows 进程使用哪一个图形 API 后端与驱动/GPU 通信

D3D12 Targeted Shader Format = PCD3D_SM6
决定项目为 D3D12 编译哪一种 UE Shader Platform 和能力集合
```

RHI 是 UE 在 CPU 侧的图形接口抽象。Renderer 通过 RHI 创建 Buffer、Texture、Pipeline State 和命令列表；选择 D3D12 后，这些操作由 `D3D12RHI` 转换为 D3D12 Device、Command List、Descriptor Heap、Resource Barrier 和 Command Queue 操作。

Shader Model 则描述 Shader 编译目标和可使用的 GPU 程序能力。UE 把 Windows D3D 的 SM6 路径表示为 `PCD3D_SM6`。它会影响 Shader 编译器、可生成的指令、平台能力判断以及哪些 Renderer 功能能够编译和运行。

两者相关，但不是同一个开关。UE 5.7 的 D3D12 Targeted Shader Formats 可以包含 `PCD3D_SM5` 或 `PCD3D_SM6`；选择 D3D12 并不会自动证明当前项目已经使用 SM6。因此必须分别设置并分别验证。

在编辑器中选择 **Edit > Project Settings**：

1. 打开 **Platforms > Windows**；如果当前布局不同，可搜索 `Default RHI`。
2. 将 **Default RHI** 选择为 **DirectX 12**。
3. 在 **D3D12 Targeted Shader Formats** 中启用 **Shader Model 6 / PCD3D_SM6**。
4. 记录 SM5 是否仍被保留。课程运行基线是 SM6；保留 SM5 只用于明确的兼容目标，不应成为未记录的第二条测量路径。
5. 暂时保留模板默认的 Lumen、Nanite 和 VSM 项目设置；具体系统在对应章节调整。
6. 关闭并重启编辑器，等待由 Shader Platform 变化触发的 Shader/DDC 工作稳定。

这里仅固定平台基础，不提前关闭 UE5 功能。后续章节会通过明确案例解释每个功能为何开启或关闭。

### 为什么选择 DirectX 12

选择 D3D12 不是因为图形 API 名称本身会自动提高画质。相同材质、灯光和渲染算法可以在不同 RHI 上得到相近画面；真正变化的是 UE 能使用的驱动接口、资源和同步模型、平台功能以及调试路径。

本课程选择 D3D12，原因是：

- 目标平台固定为 Windows，高端 UE5 渲染功能和硬件光线追踪在这条平台路径上具有清晰的源码、工具和驱动支持边界。
- 后续需要比较 Nanite、VSM、Lumen 硬件追踪和 MegaLights。统一 RHI 能避免实验中途把 API 后端、Shader Platform、GPU Event 和驱动行为同时改变。
- D3D12 提供显式命令列表、资源状态、描述符和队列模型。课程后续分析 RDG/RHI、异步工作和 GPU 捕获时，可以把 UE 的抽象映射到一个确定的平台后端。

D3D12 是课程的 Windows 工程基线，不是跨平台硬约束。Vulkan SM6 可以支持部分相同功能；D3D11 更适合需要旧硬件和旧驱动兼容、且不使用本课程高端功能集合的项目。选择替代 RHI 时，需要重新验证功能支持、Shader Platform、工具链和性能，不能沿用本课程的捕获和数值结论。

### 为什么选择 Shader Model 6

SM6 不是“更高画质”按钮。它让 UE 能为 `PCD3D_SM6` 编译 Shader，并允许 Renderer 使用该平台声明的指令和能力。UE 5.7 的 Windows 平台配置给出了明确边界：

| 功能或能力 | `PCD3D_SM5` | `PCD3D_SM6` | 对本课程的意义 |
|---|---|---|---|
| Nanite 平台支持 | 不支持 | 支持 | 第 15 篇需要 SM6；缺失时 Nanite Asset 不能按预期显示 |
| Ray Tracing 平台支持 | 不支持 | 支持 | Lumen 硬件追踪与 MegaLights 的硬件追踪路径需要继续通过硬件能力检查 |
| Virtual Shadow Maps | 当前 Windows 路径不满足要求 | UE 5.7 明确要求 SM6 | 后续 VSM 章节不能建立在 SM5 基线上 |
| Wave Operations | 不是该平台的保证能力 | 平台声明为运行时保证 | MegaLights 等并行 Shader 算法会继续检查此能力 |
| Lumen GI | 支持部分路径 | 支持 | 不能笼统地说“Lumen 必须 SM6”；具体软件/硬件与高级分支要分别判断 |

这些不是根据功能宣传推断的。`Engine/Config/Windows/DataDrivenPlatformInfo.ini` 明确把 `PCD3D_SM5` 标为不支持 Nanite 和 Ray Tracing，把 `PCD3D_SM6` 标为支持 Nanite、Ray Tracing、DXC 和 Wave Operations；UE 5.7 的 Nanite、VSM 与 Ray Tracing 设置检查也会在 Windows 下要求 D3D12 SM6。

课程因此启用 SM6，不是为了让当前空场景立即变好看，而是确保后续章节需要的 Shader Platform 从工程创建时就是稳定基线。这样 Shader、DDC、资产派生数据和截图不会在课程中途因为切换 Shader Platform 被整体重建或改变。

### 硬要求、课程选择与硬件能力不能混在一起

- **硬要求**：在当前 UE 5.7 Windows 实现中，Nanite、VSM 和 Ray Tracing 的目标路径要求 SM6；MegaLights 代码还会检查 SM6、Wave Operations 和 Ray Tracing。
- **课程选择**：在 Windows 上使用 D3D12，而不是 Vulkan SM6，是为了统一源码后端、工具与实验环境。
- **硬件条件**：项目启用 SM6 只表示允许编译该 Shader Platform，不保证当前 GPU 和驱动支持硬件光追、足够的 Resource Binding Tier 或每个可选子能力。运行时仍会查询适配器能力。

如果不区分这三层，就会产生两类错误：把课程选择误写成所有 UE 项目的强制要求，或者以为勾选 SM6 后任何 GPU 都自动具备全部高端功能。

### 代价与后续影响

- 切换 RHI 或 Shader Platform 会使当前 Shader/DDC 基线失效，首次重启和后续编译会变慢。
- 同时保留 SM5 与 SM6 会增加需要编译、Cook、缓存和测试的 Shader Platform；只保留 SM6 则会放弃不满足该目标的旧硬件或驱动。
- D3D12 与 D3D11/Vulkan 的驱动、资源管理、GPU Event 和性能特征不同。本课程后续测量只对记录的 D3D12/SM6 环境负责。
- 项目以后若增加其他平台，必须为每个平台重新选择 RHI/Shader Format 和功能降级策略，不能把 Windows 设置复制成全平台结论。

这些代价解释了为什么课程现在做决定，也解释了为什么不能把“全部格式都勾上”当作更保险的默认答案。

### 如何确认设置生效

修改 Default RHI 后必须重启，因为 RHI Device、Command Queue 和大量平台资源在进程启动阶段创建，正在运行的编辑器不会把现有 D3D11/Vulkan Device 原地替换为 D3D12。

重启后分别验证：

1. 在 **Output Log** 或项目 `Saved/Logs` 的启动日志中搜索 `LogRHI`、`D3D12` 和 `PCD3D_SM6`。
2. 确认当前 RHI 是 D3D12，而不是只确认配置文件写入了 `DefaultGraphicsRHI_DX12`。
3. 确认 Shader Platform 为 `PCD3D_SM6`，并且没有 Nanite、VSM 或 Ray Tracing 提示缺少 SM6。
4. 等待本次平台变化引发的 Shader 编译结束，再记录截图或性能。

UI 设置值证明“下一次启动应该选择什么”；启动日志证明“当前进程实际选择了什么”。两种证据回答的问题不同，不能互相替代。**Help > About Unreal Editor** 可以确认引擎版本，但不能独立证明当前 RHI 与 Shader Platform。

## 常见问题

### 找不到 C++ 选项

通常是 Microsoft C++ Build Tools、MSVC Toolset 或 Windows SDK 未安装，或没有被 UnrealBuildTool 识别。先检查 Rider Toolchain 和 UnrealBuildTool 日志；是否安装 Visual Studio IDE 本身不是判断标准。

### 创建后提示 Missing Modules

先关闭编辑器，在 Rider 中构建 `RenderingPracticeEditor / Development / Win64`；若 Rider 项目模型也失败，使用本篇给出的 `Build.bat` 命令获取独立构建日志。不要删除 `Source` 或把工程改成 Blueprint 来规避模块错误。

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
- [ ] Rider 能识别 Microsoft C++ Toolchain 与 Windows SDK
- [ ] C++ 模块能以 Development Editor Win64 编译
- [ ] 编辑器能正常启动
- [ ] 启动日志确认当前 RHI 为 D3D12、Shader Platform 为 `PCD3D_SM6`
- [ ] Shader 编译已稳定

下一篇将进入编辑器，创建课程资产目录和第一张 Sandbox 地图。
