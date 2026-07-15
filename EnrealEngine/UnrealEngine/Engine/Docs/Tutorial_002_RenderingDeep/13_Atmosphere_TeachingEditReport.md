# Teaching Edit Report - 13_Atmosphere.md

> 重建依据：最终正文 423 行、冻结原版 341 行、`review_13_14.md` 与必要 UE5.7 源码定位。  
> `review_13_14.md` 对 13 只沿用既有 BODY PASS、未在该轮重新验收；本报告不扩大该证据，也不修改章节状态或公共文件。

## 1. 最终主线

最终正文保留原版“数据形态决定职责”的核心，同时把一帧明确拆成两个窗口：

```text
早期：准备/更新共享与 per-view LUT，建立后续可读入口
晚期：Volumetric Fog 生产积分资源
-> Sky/Height Fog/LFV/Cloud 按资源依赖改写 SceneColor
-> Water/Translucency/PostProcessing 继续消费
```

Sky Atmosphere、Volumetric Fog、Height Fog、Local Fog Volume 和 Volumetric Cloud 不再只是五个系统条目，而是同一条“中间表达 -> 可读边界 -> 颜色 consumer”主线中的不同数据形态。

## 2. O-D-C-L 与过程教学

- **Owner**：scene/proxy 参数、Render Thread/RDG 单帧资源、view-local volume/LUT、view state history 和 SceneColor consumer 分开。
- **Data**：共享 LUT、per-view SkyView/AP、froxel medium、`LightScattering`、`IntegratedLightScattering`、LFV instance/tile data、cloud VRT color/depth/transmittance 各自有明确语义。
- **Control**：state versioning、external-access commit、deferred/forward/special view 路由、LFV coverage ownership、VRT/per-pixel choice 控制下一 consumer。
- **Lifetime**：共享 LUT 可缓存，per-view LUT 与 RDG volume 服务当前 view/frame，fog/cloud history 跨帧，SceneColor 版本持续交给后续系统。

最终正文把“资源存在”“可读入口成立”“producer 已写”“颜色已合成”“最后 GPU consumer 完成”分成不同证据深度。

## 3. 设计理由与替代方案

- LUT 让 sky/cloud/fog 复用昂贵散射积分；直接每像素 ray march 可更直接但成本按消费者和像素放大。
- camera-aligned froxel 将预算集中到当前视锥；world-space sparse volume 更利于长期复用但结构和流送更复杂；逐像素 marching 重复工作更多。
- Height Fog 利用指数密度闭式积分；任意三维噪声和复杂光照则必须交给 froxel/ray march。
- LFV 按近段 froxel、远段解析补足和独立 fallback 协作，交换光照质量、覆盖距离和额外 pass 成本。
- Cloud VRT 用低分辨率与 history 换性能，per-pixel 避免重建但成本更直接；选择取决于平台、相机运动和时域伪影预算。

正文明确区分数学/物理条件与 UE 工程选择，没有把任一路径写成普遍更好。

## 4. Worked Case

最终正文把开篇清晨场景落实为一条像素射线：穿过贴地 LFV 和全局薄雾，命中远山，上方再有云。案例逐步记录：

- shared/per-view atmosphere LUT；
- opaque-lit `SceneColor` 与 `SceneDepth`；
- froxel local scattering 到 Z-integrated `S_near/T_near`；
- Height Fog 与 LFV 解析补段；
- `L_out = S_near + T_near * L_far`；
- cloud luminance/transmittance/depth 与 VRT/per-pixel 合成；
- 最终 SceneColor 交给 Translucency/PostProcessing。

这比原版仅列系统顺序更强，因为同一像素实际承载了数据、owner、覆盖区间和 consumer 变化。

## 5. Last-Valid-State

最终证据梯从 atmosphere 参数、LUT identity、external SRV access 与 uniform binding、`LightScattering`、`IntegratedLightScattering`、fogged SceneColor、Cloud VRT、cloud-composited SceneColor 逐层推进，并明确 RDG declared、Platform Queue Submit 和 GPU/last-consumer completion 不能互相替代。

现象表将天空黑、体积雾不亮、grid 截断、LFV 重复、云穿 opaque、VRT 有内容但画面无云分别路由到第一个相关资源边界。

## 6. 事实修正与强化

相对冻结原版，最终正文实际完成了这些修正：

- 把含混的 “LUT 提交”改成 RDG 资源生成、external SRV access commit/conversion、uniform binding、Platform Queue Submit 和 GPU completion 的不同深度。
- 明确 state versioning 开启时按缓存初始化/输入版本重算共享 LUT，关闭时共享 LUT 每帧重算。
- 增加 early prepare / late composite 双窗口，避免把 LUT 工作误读成 late weather pass。
- 增加 desktop deferred、forward、water-behind 和 reflection capture 的条件分支，不把默认顺序推广成全路径定律。
- 将 LFV 从“三条互斥路径”修正为按射线区间协作；独立 pass 是前序未承担完整覆盖时的 fallback。
- 明确 underwater fog/cloud 可能写水后目标而非主 `SceneColor`。
- 明确 cloud alpha 是 transmittance、cloud depth 不是主 SceneDepth；VRT trace 还需 reconstruct/compose。

## 7. 原版价值迁移

| 冻结原版价值 | 最终迁移 |
| --- | --- |
| 五种数据形态而非天气效果列表 | 保留为总地图并扩成双窗口资源主线 |
| 清晨场景资源账本 | 保留并扩成单射线 worked case |
| Sky LUT 生产/消费分离 | 补 state versioning、external access 和 path 条件 |
| Froxel 与 `IntegratedLightScattering` | 补局部散射到 Z 积分的两次数据换形与替代方案 |
| Height/Volumetric Fog 防重复 | 保留并加入透过率组合公式、硬约束/工程选择 |
| LFV 三种接入 | 修正为覆盖区间协作，保留防重复和 fallback 调试价值 |
| Cloud VRT/per-pixel | 保留并扩展输出语义、质量取舍和 compose 证据 |
| 资源源头倒查 | 强化为 last-valid-state 表和 completion depth |

## 8. 源码克制

正文只用 `RenderSkyAtmosphereLookUpTables`、`CommitToSceneAndViewUniformBuffers`、`ComputeVolumetricFog`、`CalculateHeightFog`、`CombineVolumetricFog`、`InitLocalFogVolumesForViews`、`RenderVolumetricCloud` 等符号作为调试路标。源码路径、行号和验证过程留在 sidecar，不让调用顺序替代资源模型。

## 9. BODY Review 依据与残余风险

`review_13_14.md` 对 13 的 BODY PASS 是继承性记录，该轮未重新验收。本轮 sidecar 重建确认最终正文与冻结原版的价值迁移，并定位承重源码锚点，但不声称完成了另一轮独立终审。

残余风险集中在 Sky Atmosphere cache/external-access、LFV route、VRT mode 和特殊 view target 的版本变化；运行时 GPU capture 未执行。本报告不改变正文、章节完成状态或公共 Gate。
