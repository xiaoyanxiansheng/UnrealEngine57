# Teaching Edit Report - 14_Translucency.md

> 重建依据：最终正文 591 行、冻结原版 400 行、`review_13_14.md` 与必要 UE5.7 源码定位。  
> 本报告记录最终正文已发生的教学编辑，不修改章节状态，不声称公共 Gate、`OUTLINE.md` 或 `SOURCE_INDEX.md` 已更新。

## 1. 最终主线

最终正文不再把材质 flag 直接等同透明 pass，而沿一条资源责任链推进：

```text
material composition/blend/shading/lighting intent
-> Renderer resolves current-view schedule and pass mask
-> per-pass visible commands + sort / optional OIT
-> Lit/Unlit then lighting-mode input selection
-> direct color target or separate color/modulate/depth
-> optional distortion / FrontLayer / RT consumer
-> transparency-block composite or PostProcessing handoff
-> last GPU consumer completion
```

这条主线把“材质意图”“本帧调度”“GPU producer”“资源交接”“视觉合成”分成不同状态。

## 2. O-D-C-L 与过程教学

- **Owner**：材质/primitive 拥有长期意图，Renderer 拥有当前 view schedule，mesh pass 拥有 visible commands，RDG/ResourceMap 管单帧资源，distortion/post/Lumen/RT 是不同 consumer。
- **Data**：pass mask、sort key、OIT samples、TLV、color/modulate/depth、distortion offset/stencil、FrontLayer normal/depth、RT radiance/background visibility 各自独立。
- **Control**：AfterDOF、Auto Before DOF、Standard separated、underwater、OIT、lighting mode、distortion 和 RT 条件决定资源下一站。
- **Lifetime**：材质意图跨帧；schedule/commands/ResourceMap 服务当前 view/frame；AfterDOF 资源跨到 post consumer；底层复用必须等待最后 GPU consumer。

正文新增精确完成深度表，避免用“draw 已提交”概括 command formation、RHI recording、Platform Queue Submit、GPU producer 和 composite。

## 3. 设计理由与替代方案

- 显式 `SceneColorCopy` 和 separate targets 提供跨 RHI 可预测读写；framebuffer fetch/subpass input 可省带宽但限制平台、MSAA 和 render-pass 组织。
- per-pass 排序保持不同合成时机的 ownership；单一全局透明队列无法正确表达 AfterDOF/AfterMotionBlur/distortion。
- low-resolution separate translucency 交换 pixel/overdraw/bandwidth 与边缘质量；全分辨率适合锐利玻璃和小覆盖关键效果。
- per-object sort 便宜但不解 mesh 内和像素内多层；sorted triangles、sorted pixels、weighted OIT、depth peeling、linked list 各有质量、内存和平台代价。
- TLV 用低频 3D 场服务大量烟雾/粒子；Surface ForwardShading 增加逐像素成本换锐利局部灯响应；FrontLayer/RT 处理更专门的反射/透射证据。
- FrontLayer 只保存最近透明 normal/depth，以较窄成本服务后续系统；完整多层表示更强但内存、trace 和材质成本更高。

## 4. Worked Cases

**染色折射玻璃**实际承载三条并行语义：Standard color、Distortion offset、可选 FrontLayer normal/depth。它在 Standard 域排序，使用 Surface ForwardShading 读取 forward light data，先写 ResourceMap[Standard]，再由 distortion apply/merge 把扭曲背景、玻璃颜色和 modulation 合回 `SceneColor`。

**AfterDOF 烟雾**先由 Renderer 确认 AfterDOF 未被禁用或 Auto Before DOF 移走，再通过 Lit + volumetric directional mode 采 TLV，写低分辨率 separate color、可选 modulate，并使用匹配尺度的 depth 合约，最后由 DOF 后 post consumer upscale/composite。目标纹理有内容时主 SceneColor 仍无烟雾是合法中间状态。

两个案例共同证明：同一个资源结构不代表相同生命周期终点，透明着色完成也不等于视觉交付完成。

## 5. Last-Valid-State

最终正文建立：material intent -> Renderer schedule -> visible/sorted commands -> lighting input -> GPU target -> ResourceMap handoff -> optional OIT/distortion/FrontLayer/RT consumer -> transparency-block composite -> post consumer -> final GPU completion。

症状表把对象消失、排序跳变、透明全黑、背景 copy 错、downsample target 未合成、distortion 后玻璃消失、AfterDOF target 有内容但画面无烟雾、FrontLayer 反射缺失和 OIT 溢出分别路由到下一条最可疑边。

## 6. 事实修正

相对冻结原版，最终正文实际修正或强化了：

- 材质只声明 composition intent；Renderer 结合 view、项目和效果条件决定最终 pass mask。
- 同附件读写不是物理绝对禁止；UE 通用路径选择显式 copy/separate 以获得跨平台可预测语义。
- `SceneColorCopy` 按实际 consumer gate 创建；普通透明、特定 Single Layer Water 和 underwater 分支不能混写。
- `TranslucencyAll` 是 staged translucency 关闭或特殊调用上下文中的聚合/回退桶，不是普通材质类别。
- Separate translucency 至少有 post 延迟、distortion 暂存、downsample immediate composite 三类原因和不同最后 consumer。
- OIT sorted triangles 与 sorted pixels 解决不同层级；sorted-pixel storage 有平台、MSAA、pass mask 和容量边界。
- 先判 Shading Model 是否 Lit，再由 lighting mode 选择 TLV/forward；玻璃并非固定采 TLV。
- Stereo TLV 由 secondary `PrimaryViewIndex` 映射到 primary texture pair；共享资源减少独立 pair/clear，但 injection/filter 仍可能按 view 调度。
- FrontLayer 是最近透明 normal/depth 的窄证据，不直接写颜色，也不代表完整多层透明。
- Lumen RT radiance 可直接更新 SceneTextures.Color，也可因 distortion/Standard separated 写入 ResourceMap 等 merge；“算出颜色”不是统一终点。

## 7. 原版价值迁移

| 冻结原版价值 | 最终迁移 |
| --- | --- |
| 透明不是 opaque 后再画一遍 | 扩成 intent -> schedule -> resource -> consumer 的全章主线 |
| 玻璃与 AfterDOF 烟雾双案例 | 保留并拆成两个完整 worked cases，补 owner/control/completion |
| EMeshPass 分域 | 修正为 Renderer 最终 schedule，补 Auto Before DOF、AllTranslucency、underwater |
| ResourceMap 区分“画了/合成了” | 扩成三类 separate 原因、color/modulate/depth 和最后 consumer |
| per-object 与 OIT 两层排序 | 保留并补有界 storage、替代方案和平台条件 |
| TLV 两级 cascade | 扩成 Shading Model/lighting mode 双轴、stereo mapping、history 与调参权衡 |
| Distortion 三段合并 | 保留并用玻璃 ResourceMap 状态逐步承载 |
| FrontLayer 与 RT 接回 SceneColor | 保留并强化窄证据与 direct-vs-ResourceMap 接入选择 |
| 资源线倒查 | 重建为精确 last-valid-state 和 completion depth |

## 8. 源码克制

正文使用 `ETranslucencyPass`、`FTranslucencyPassResourcesMap`、`AddOITComposePass`、`FTranslucencyLightingVolumeTextures::Init`、`RenderDistortion`、`LumenFrontLayerTranslucencyGBuffer`、`RenderRayTracingTranslucency` 等最小锚点。实现路径、行号和核验记录留在 sidecar/review，正文由资源状态和案例承担教学。

## 9. BODY Review 依据与残余风险

`review_13_14.md` 记录 14 为 BODY PASS；该轮重新源码核验的唯一阻断是 14-N1 stereo TLV，并检查了邻近 361-393 行的 owner/data/control/lifetime、调试链和完成深度。其余单元沿用整章既有 BODY 结论，本报告不把局部复查夸大为逐行重新终审。

残余风险集中在 staged pass routing、OIT/platform 条件、dynamic separate resolution、Lumen/RT 接入和 post consumer 的版本变化；未执行运行时 GPU capture。本报告不改变正文、完成状态或公共 Gate。
