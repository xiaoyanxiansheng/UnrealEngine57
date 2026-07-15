# 15 PostProcessing Teaching Edit Report

## 1. 报告边界与材料清单

本报告从当前最终正文、冻结原版、指定独立 review 与 UE5.7 源码重新建立。旧 TeachingEditReport 和旧 CoverageMatrix 的结论全部作废，未用于证明当前质量。

物理行数统一使用 `.NET File.ReadAllLines`：

| 材料 | 角色 | 行数 | SHA256 | 读取结果 |
| --- | --- | ---: | --- | --- |
| `Engine/Docs/Tutorial_002_RenderingDeep/15_PostProcessing.md` | 当前最终正文 | 658 | `C2BEA533A8DE30D8B59E01A6644E1AE7A09F3613D26DB40B6D00CA42F9E11EB4` | 完整读取 |
| `.codex/tmp/renderingdeep_11_24_original_20260713/15_PostProcessing.md` | 用户声明的冻结原版 | 412 | `D7BAFDA5E84D70A4C4AF69CF675F96699856E7C684C5317910765E9023D7217E` | 完整读取，未改动 |
| `.codex/tmp/review_15_16.md` | 指定 BODY review | 114 | `4ECEF53DE13D9A907D650D27D35EDA7BE91DD9C364D635774A80B08B60A48E1F` | 完整读取；该 review 明确未重审第 15 章，因此只作为范围记录，不作为本报告 PASS 依据 |
| UE5.7 本地源码 | A 类事实与高风险条件 | 不适用 | 固定源码快照 | 已核验 Local Exposure、FFT Bloom、CombineLUT/Tonemap、pass sequence 与 external output |

最终正文比冻结原版多 246 行，约增加 59.7%。这只说明没有触发“短于原版 15%”异常，不作为质量证明；原版价值仍按事实单元逐项审计。

## 2. 独立 BODY 结论

**BODY PASS。**

这个结论由本轮独立读取最终正文、冻结原版并复核 UE5.7 高风险源码后得出，不沿用章节头状态、旧 sidecar、公共状态或 review 中未重新审查的第 15 章结论。

正文已经形成一条可复述且没有完成深度跳跃的主线：resolved、pre-exposed scene-linear `SceneColor` 进入动态后处理颜色流；After DOF translucency、TSR、曝光、Bloom、LUT/Tonemap 和 visualizer 逐段改变当前颜色或产生旁支；最后 enabled pass 获得外部输出权；`ViewFamilyTexture` 写入之后，RDG、RHI、平台队列、GPU 和 present 继续推进不同完成深度。Local Exposure、FFT Bloom 与 CombineLUT 三组新增高风险事实均已被条件化说明，未发现 BODY 阻断。

本结论不修改章节状态，也不表示公共 Gate 或共享索引已更新。

## 3. 实际教学主线

1. 先用“没有固定 Final Blit”纠正 Unity 后处理直觉，把问题改写为当前颜色与最终输出权如何沿动态 pass sequence 交接。
2. 进入后处理前先确认 `SceneColor` 的 scene-linear 语义、`PreExposure` 数值尺度、working color space 和可读 resource state，避免把所有问题压成“HDR”。
3. After DOF 玻璃证明绘制时机与合成时机必须解耦；TSR 随后在 Tonemap 前完成 temporal reconstruction、分辨率交接与 history extraction。
4. 曝光章节把本帧 `PreExposure`、全局 eye adaptation、当前图 local-exposure 空间修正和异步 average-local-exposure 标量分开。
5. Bloom 先讲 Gaussian 多尺度旁支，再讲 FFT 的资格、强度 gate 与 spectral-kernel cache identity，二者最终都只向 Tonemap 提供 HDR 旁支。
6. `CombineLUT` 先建立 view 级颜色配方与缓存身份，Tonemap 再按 output device/gamut/encoding 把当前颜色推进到目标输出合同。
7. Debug/Visualize 仍然是链上生产者；最后 pass、view rect 与 load action 共同决定 external target 是否正确。
8. 最后用 last-valid-state 证据梯区分颜色正确、外部目标已写、GPU 已完成和玩家已看到。

## 4. 冻结原版价值审计

| 原版价值单元 | 最终处理 | 最终落点 / 说明 |
| --- | --- | --- |
| 动态最后 pass，而非固定 Final Blit | 保留并深化 | 开篇、框架问题、第 2 节、第 9 节；增加 override output、callback 和多 view load action |
| target/resolve 与 external `ViewFamilyTexture` | 重写并澄清 | 第 1、9 节；加入 scene-linear / pre-exposed / working-space / resource-state 四层合同 |
| After DOF translucency 跨入后处理 | 保留并加强因果 | 第 3 节；明确 DOF、TSR、motion blur 三个时序后果 |
| TSR 在链中段、输出 FullRes 与 history | 保留并深化 | 第 4 节；增加 history compatibility、pre-exposure correction、恢复策略与替代方案 |
| Gaussian Bloom 多级累积 | 保留 | 第 6.1 节；仍以霓虹高亮承载输入、downsample、additive 与 Tonemap 交接 |
| CombineLUT + Tonemap 显示边界 | 保留并重构 | 第 7 节；从“ACES 在边界执行”扩展为 working space、缓存身份、output gamut/device contract |
| Debug/Visualize 是链上节点 | 保留并深化 | 第 8 节；增加 pre/post-tonemap 语义与 late debug 覆盖 |
| 写入外部目标不等于 Present | 保留并深化 | 第 9 节；增加 RDG/RHI/submit/GPU/present 完成深度 |
| 原版将 `SceneColor` 概括为 resolved HDR | 改写 | 最终稿明确 scene-linear 语义、PreExposure 数值和 shader 可读状态，避免“HDR”一词承担过多事实 |
| 原版缺少 Local Exposure 双链 | 新增高价值教学单元 | 第 5 节：当前空间数据与 average-local-exposure 异步 readback 分开 |
| 原版缺少 FFT Bloom 资格与缓存 | 新增高价值教学单元 | 第 6.2 节：资格、fallback gate、spectral cache identity 与替代方案 |
| 原版把 CombineLUT 主要解释为预烘计算 | 深化而非删除 | 第 7.2 节加入 cached settings、持久 LUT、临时 LUT、stereo reuse 与完整内容身份 |

未发现被删除且无去向的独特原则、条件、案例或调试判断。原版中重复出现的“当前颜色交棒”“最后 pass 写外部目标”被合并到统一主线，但分别在机制、案例和调试证据梯承担不同教学任务，没有被误判为纯重复而删除。

## 5. O-D-C-L 审计

| 承重概念 | Owner | Data | Control | Lifetime | 判定 |
| --- | --- | --- | --- | --- | --- |
| 当前 `SceneColor` | 当前 RDG 图与刚产出它的 pass | texture/slice、view rect、颜色语义与数值尺度 | enabled pass sequence 与资源依赖 | 当前图，最后 consumer 后可回收 | 清楚 |
| `ViewFamilyTexture` | view family 外部 render target | 最终 view-family color target | last enabled pass、rect/load action、external final access | 跨 Renderer 边界；不拥有 present | 清楚 |
| TSR history | view state | 颜色、guide/metadata、rect、格式、曝光语义 | compatibility、camera cut、extraction/re-registration | per-view 跨帧 | 清楚 |
| Local Exposure 空间数据 | 当前 RDG 图 | bilateral/blurred luminance 与局部参数 | local-exposure pass、BloomSetup、Tonemap | 当前 view/graph | 清楚 |
| Average local exposure | eye-adaptation manager / view state | 跨帧标量 | GPU compute、readback ready、manager consumption、后续 `UpdatePreExposure` | 非固定延迟跨帧 | 清楚 |
| FFT spectral kernel | view state | spectral texture 与 constants | cache CVar 和完整 kernel/image identity | 条件式跨帧 | 清楚 |
| Color grading LUT | cached settings + view/view-state resource或当前 RDG | 配方比较状态与 GPU LUT texels | `AddCombineLUTPass`、identity change、stereo reuse | 持久或当前图，两者明确分开 | 清楚 |

## 6. 设计理由与替代方案

- **动态最后 pass**：为插件、AA、visualizer 与 upscale 保留可插拔性；固定 Final Blit 更简单，但会制造额外 copy，并难以让实际链尾直接写外部目标。
- **PreExposure**：用统一 view 级倍率换取有限精度格式中的数值稳定；更高精度中间格式或每 pass 自行缩放可行，但增加带宽、显存和跨阶段不一致风险。
- **TSR 在 Tonemap 前**：保留场景高亮、深度/速度与 temporal history 的可比较性；纯空间 upscale 不产生历史拖影但无法利用跨帧样本，第三方 temporal upscaler则必须遵守同一输入/输出合同。
- **Local Exposure 双链**：空间修正服务当前画面，平均标量只服务后续 pre-exposure；只用全局曝光更稳定但难兼顾同屏极端动态范围，更复杂局部 tone mapping 则增加成本和伪影控制难度。
- **Gaussian 与 FFT Bloom**：Gaussian 适合常规可扩展 Bloom；FFT 适合大而定制的卷积核，代价是频域工作纹理、平台约束和 cache identity 管理；关闭 Bloom 或美术假光晕适合风格化/严格预算路径。
- **CombineLUT**：把 view 内稳定的颜色配方从 per-pixel 计算移到低分辨率查表；逐像素计算减少 LUT/cache 管理但增加全屏 ALU，高分辨率 LUT 可提高精度但增加纹理成本。
- **Final gather 不等于 Present**：保持 Renderer、RDG、RHI、platform queue 与 viewport 的责任边界；把它们合并成“渲染完成”会使资源复用与窗口问题无法分诊。

## 7. 案例与调试价值

- **霓虹横移**贯穿 TSR 输入、history rejection、Bloom 高亮、ToneMapping 与最终输出，能够展示同一颜色在语义、分辨率和 owner 上的连续变化。
- **After DOF 玻璃**给出半透明绘制/合成解耦的具体后果，不是只列 pass 名。
- **暗巷转向霓虹**把 `PreExposure`、eye adaptation、当前 local-exposure 空间数据和 average-local-exposure readback 放在同一时间线上，明确不保证固定一帧。
- **横向 FFT kernel**展示冷缓存、identity 命中、动态分辨率/mip 失效与无 ViewState capture fallback。
- **双视图外部目标**把 last pass、rect、Clear/Load/NoAction 放在一个可观察故障中。
- 调试章节按 13 级 last-valid-state 从 resolved input 走到 present，Local Exposure、FFT、LUT 与 external output 都有独立证据点，能避免从最终观感反猜 shader。

## 8. 事实修正与源码克制

### 事实修正

- 把“HDR SceneColor”修正为 scene-linear 语义、通常已 pre-exposed、并具有明确 shader-readable resource state 的组合合同。
- 把 `PreExposure` 与当前 eye adaptation 分开，并增加 average-local-exposure 的 ready-only readback 与后续消费条件。
- 增加 `BM_FFT` 的 method/ViewState/platform/kernel 四项资格；明确资格失败后只重新检查 Gaussian gate，不承诺无条件 fallback。
- 增加 FFT spectral cache 的 physical texture、mip、spectral desc、convolution size、image size 与 cache CVar 身份。
- 把 CombineLUT 从单一“预烘配方”修正为 cached settings、持久 LUT、当前 RDG 临时 LUT三类角色；增加 secondary view 的条件式 reuse。
- 把 ToneMapping 后的结果从“LDR”修正为目标 output contract，可为 SDR、PQ、scRGB 或线性 capture。

### 源码克制

正文先讲颜色流、数值尺度、owner 与完成深度，再使用少量 UE symbol 作为定位点；文件路径、行号、cache 字段与验证记录全部留在 CoverageMatrix/本报告。没有重复“源码锚点”块，也没有用函数调用顺序代替教学主线。

## 9. 残余风险

- 本轮是 UE5.7 静态源码审计，未运行项目级 GPU capture；平台、HDR swap chain、scene capture、插件和多 view 组合仍可能改变实际分支。
- Average-local-exposure 和 FFT/LUT cache 的实际命中率、延迟与性能代价需要项目 telemetry；正文只承诺条件和状态转换。
- Stereo LUT reuse 的代码路径成立，但项目若人为给 secondary view 配置不同 output contract，必须重新验证兼容性。
- Present、display scanout 与平台窗口链仍属章节边界，不应把本章的 `ViewFamilyTexture` 正确性扩写成显示完成。

## 10. 最终记录

- 独立 BODY：**PASS**。
- CoverageMatrix：已从当前最终正文重建，旧结论未继承。
- 原版价值：已逐项保留、迁移、深化或按事实修正；未发现无去向的信息价值损失。
- 状态/公共文件：**未修改**；本报告不声称公共 Gate、章节状态或共享索引已更新。
