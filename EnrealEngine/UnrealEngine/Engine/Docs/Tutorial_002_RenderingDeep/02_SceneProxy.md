# 02 从 Component 到 SceneProxy

> **源码版本**: UE5.7  
> **前置阅读**: 01（渲染架构总览）必读；03（三线程模型与渲染命令）可在本篇后回读  
> **当前状态**: ✅ 完成（Codex 最终事实回归通过，2026-06-25）  
> **验证记录**: 见同目录 `02_SceneProxy_CoverageMatrix.md`

---

## UE 把可变组件转换成稳定的 Renderer 场景记录

UE 把一个可渲染对象分成两套生命周期：Game Thread 拥有可随游戏逻辑变化的 Component，Renderer 拥有可跨帧查询、剔除、缓存和安全撤销的场景记录。Component 先生成 Proxy 内容快照与 SceneInfo 登记卡；Render Thread 接管后把变化排入更新队列；`FScene::Update()` 再把同一版本的内容、空间与登记关系发布给后续消费者。

Unity 读者常从 `GameObject`、`Transform`、`MeshRenderer` / `MeshFilter` 建立“渲染器每帧读取组件”的直觉。UE 的对应边界不同：Render Thread 的绘制热路径消费 Proxy、SceneInfo 与 `FScene`，不会临时回到 live `UObject` 世界查询 mesh、材质和矩阵。物体没出现、移动不更新或删除后崩溃时，应先找状态停在请求、快照、待发布更新还是已发布 Scene，而不是先问 Renderer 为什么没读到组件最新值。

这项拆分服务两个明确需求：

- Game Thread 这一侧，组件随时可能被蓝图、C++、加载流程、异步编译改写或销毁；
- Render Thread 那一侧，渲染器需要一份跨帧稳定、可剔除、可缓存、可安全销毁的场景数据。

如果让 Render Thread 直接读 live `UObject`，要么得加锁把两条线程互相拖住，要么会读到半更新或已经释放的状态。UE 的解法是：**先把组件摘成一份渲染侧快照，再由 Renderer 给这份快照分配索引、做空间登记、维护缓存状态。** 从此渲染侧只认这份快照，不回头看组件。

01 篇已经建立了 Proxy 概念和创建时序的全局印象。本篇不重复全局架构，而是把这条“对象桥”深入到可维护、可调试的生命周期。我们跟一把**可移动的 StaticMesh 椅子**走完整条路：它在游戏侧是 `UStaticMeshComponent`；进入 Renderer 后变成 `FPrimitiveSceneProxy` 快照与 `FPrimitiveSceneInfo` 登记卡，最后登记进 `FScene` 的紧凑数组、空间八叉树和静态网格批次里。之后移动它、换材质、删除它，也都不会直接改这份快照，而是经由 dirty 标记和渲染命令，汇聚到同一个发布点。

本篇要回答的核心问题只有一个：

> **一个 `UPrimitiveComponent`，怎样跨过 Game Thread / Render Thread 边界，成为 Renderer 可查询、可剔除、可提供 draw 输入、可安全撤销的场景 Primitive？**

---

## 先建立心智模型：四个对象、三本账、一条状态线

在跟流程之前，先把本篇会反复出现的几个名字压成一条状态线。读这张图时，重点不是“有几个类”，而是**每一步里数据归谁所有、谁能改它**：

```text
Game Thread / Engine 模块
  UPrimitiveComponent          游戏逻辑可改的 UObject 组件（真相源）
        |
        | CreateSceneProxy()   摘出一份渲染快照
        v
  FPrimitiveSceneProxy         给 Renderer 看的只读快照（Engine 定义）
        |
        | new FPrimitiveSceneInfo(...)   给快照套一张登记卡
        v
  FPrimitiveSceneInfo          Renderer 私有的场景登记卡（Renderer 定义）
        |
        | render command -> PrimitiveUpdates   交出所有权，排队
        v
Render Thread / Renderer 模块
  FScene 紧凑数组 / PrimitiveOctree / StaticMeshes
                               Renderer 跨帧维护的场景数据库
```

这条桥上不是三个对象，而是四个责任层。前三个是对象，最后一个是 Renderer 持有的场景数据库；少看任何一层，都会把“对象已创建”“命令已执行”和“场景状态已成立”误认为同一件事。

| 形态 | 所在线程 / 模块 | 它解决的问题 |
| --- | --- | --- |
| `UPrimitiveComponent` | Game Thread / Engine | 游戏侧可变状态，允许蓝图、C++、加载流程随时修改 |
| `FPrimitiveSceneProxy` | GT 创建、RT 消费 / Engine 定义 | 把 Renderer 需要读的状态镜像成一份不再回读 `UObject` 的快照 |
| `FPrimitiveSceneInfo` | GT 创建、RT 管理 / Renderer 定义 | 保存 Renderer 私有的索引、缓存、八叉树、光照交互、GPUScene 等登记状态 |
| `FScene` 数据结构 | Render Thread / Renderer | 给可见性、GPUScene 上传、draw 收集提供跨帧稳定的输入 |

四层对象同时维护三本彼此关联、但不能互相替代的账：

| 账本 | 权威内容 | 主要维护者 | 成立后能证明什么 |
| --- | --- | --- | --- |
| 内容账 | mesh、材质、LOD/section、渲染标志，以及 Proxy 能回答的静态/动态绘制契约 | Component 产生真相；Proxy 固化 Renderer 所需快照 | Renderer 不必回读 live `UObject`，就能询问这份 primitive 能提供什么 |
| 空间账 | transform、world/local bounds、previous transform，以及空间索引中的位置 | Component 产生新空间真相；GT 捕获载荷；`FScene::Update()` 统一发布 | Proxy、packed 空间数据和 octree 描述的是同一版位置 |
| 登记账 | SceneInfo 身份、packed 位置、持久句柄、静态批次及派生系统关系 | SceneInfo 组织关系；`FScene` 控制场景级容器 | primitive 已成为当前 Renderer Scene 版本中可被查询的成员 |

这三本账解释了本章所有更新策略：**当变化使旧 Proxy 无法继续诚实回答 mesh、材质或 pass 合约时，RenderState 重建替换快照；只有空间账变化且组件契约允许保留旧身份时，才走局部 transform 更新；登记账的建立和撤销必须由 Renderer 在统一发布点完成。**

### Proxy 提供内容快照，SceneInfo 管理登记关系

最容易困惑的是：既然都是“渲染侧的对象描述”，为什么要拆成 Proxy 和 SceneInfo 两层？

关键在于它们处在**相反的依赖方向**上。

- **Proxy 定义在 Engine 模块**。它的职责是让形形色色的 Component 都能把自己的渲染数据“交出来”。Engine 不应该知道 Renderer 内部有八叉树、有紧凑数组、有 Nanite bin。
- **SceneInfo 定义在 Renderer 模块**。它的职责是让 Renderer 管理这份快照在场景里的位置：八叉树 id、紧凑索引、缓存的 draw command、Nanite / 光追 / Lumen 等子系统的注册关系。

如果把 Renderer 的私有字段塞进 Proxy，Engine 就反向知道了 Renderer 的实现细节，依赖方向被破坏；如果让 Renderer 直接去读 Component，又破坏了线程和生命周期边界。两层结构让依赖保持单向：SceneInfo 可以持有并包裹 Proxy，Proxy 不必知道 SceneInfo 的内部。

所以更准确的心智模型是：

```text
Proxy   = “这把椅子给 Renderer 看的一份快照。”
          它回答：mesh 是什么、材质是什么、这个 View 里走哪些路径、有哪些静态/动态元素。

SceneInfo = “Renderer 管理这份快照的登记卡。”
          它保存：这份快照在 FScene 里的索引、缓存、和各子系统的注册关系。
```

这两个对象通常一一对应，但**不是继承关系**，也不是“同一职责的两个名字”。

---

## 本篇边界

本篇只讲一件事：**Component 到 Renderer 场景数据库的对象桥接。** 它必须讲清五个问题：

- Proxy 和 SceneInfo 为什么分成两层，各自解决哪个所有权问题。
- Proxy 的虚函数体系，怎样让 Renderer 用统一方式询问不同的 Component 子类。
- 静态网格的“注册时缓存”和动态网格的“每帧收集”，为什么要分开。
- Transform dirty 和 RenderState dirty，为什么是两条不同的更新路径。
- Proxy 销毁为什么必须延迟到 Render Thread 的安全点。

下面这些主题本篇只点到，深入归后续篇：

| 主题 | 本篇怎么处理 | 深入出处 |
| --- | --- | --- |
| render command 投递、RenderCommandPipe、fence、帧同步 | 只把它当作“跨执行域投递”的黑盒 | 03 ThreadModel |
| GPUScene buffer layout、PrimitiveID、增量上传 | 只说椅子会成为它的上游输入 | 06 GPUScene |
| `FMeshBatch` → `FMeshDrawCommand`、排序、合批、PSO | 只交付到 `FMeshBatch` 这一层 | 07 MeshDrawCommand |
| 可见性如何用 bounds、octree、show flag、遮挡 | 只说椅子已成为可被查询的输入 | 08 FrameInit |
| Nanite cluster、page、material binning 内部机制 | 只说椅子可能走 Nanite proxy | 16 Nanite |

所以本篇的终点不是 GPU draw，而是：**椅子已经成为 `FScene` 里稳定的 primitive 数据，后续可见性、GPUScene 和 MeshDrawCommand 章节会继续消费它。**

---

## 本篇读完，你应该能回答

带着这几个问题往下读，读完应该都能答上来：

- 渲染器要画一把椅子时，它读的是组件、Proxy、还是 `FScene` 里的数据？
- `CreateSceneProxy()` 返回 `nullptr` 意味着什么？它和“被剔除了”有什么区别？
- 为什么 Render Thread 收到 add 命令后，椅子还没真正进入场景？真正入场发生在哪一步？
- 一个静态网格的几何，是每帧从组件取，还是注册时就缓存好了？由谁决定？
- 把椅子拖到桌子旁，引擎到底改了什么、没改什么？为什么不直接改 Proxy 的矩阵？
- 删除椅子时，谁负责断引用、谁负责撤销场景关系、谁负责最终 `delete`？

---

## 入场总览：五次状态转换建立可消费场景记录

椅子从组件变成场景 primitive，要走五步。先看一遍全景，再逐步深入。每一步的关键都不是“调了哪个函数”，而是**数据现在是什么形态、归谁、下一步去哪里**：

```text
① GT 提出注册请求      组件说“我要进渲染场景”，但还没有任何渲染对象
② GT 摘成快照+登记卡    创建 Proxy 快照和 SceneInfo 登记卡，打包成 add 命令
 ③ RT 初始化并入队       在 RT 上写入 transform、建 RT 资源，把 SceneInfo 排进待发布队列
④ FScene::Update 发布   批量吸收变化，统一更新索引、紧凑数组、八叉树与静态批次——正式入场状态在这里成立
⑤ 成为 draw 收集输入    椅子成为可见性和 MeshDrawCommand 章节的上游数据
```

注意第 ③ 步：很多人以为“Render Thread 收到命令 = 物体进场景了”。在 UE5.7 里不是。**真正入场是第 ④ 步的 `FScene::Update()`**，原因后面会讲清楚。

为了让后面的细节不退化成函数名列表，可以把椅子的生命周期看成一张持续更新的“状态账本”：

| 阶段 | 当前权威数据 | 谁拥有修改权 | Renderer 此时能做什么 | 还不能做什么 |
| --- | --- | --- | --- | --- |
| 注册前 | Component 的 mesh、材质、transform、flags | Game Thread | 什么也不能做；场景里还没有它 | 不能剔除、不能收集 draw、不能上传 GPUScene |
| Proxy / SceneInfo 已创建 | Proxy 保存内容快照；add 命令保存空间参数；SceneInfo 尚无场景身份 | GT 只能继续改 Component；已打包对象等待 RT 接管 | 只能等待命令执行 | 不能把 SceneInfo 当成已入场对象 |
| RT 初始化并入队 | Proxy 已有 RT 空间状态和资源；SceneInfo 位于 `PrimitiveUpdates` | Render Thread | 能在统一发布点处理它 | 仍不能用 `PackedIndex` 查询，因为索引尚未分配 |
| `FScene::Update()` 后 | `FScene` 的数组、八叉树、SceneInfo 缓存成为渲染侧权威状态 | Renderer / Render Thread | 可见性、静态批次、GPUScene 上游都能消费 | Game Thread 不能直接修改 Proxy 或场景数组 |
| 移除排队后 | Component 已断开引用；Renderer 仍持有待撤场对象 | Render Thread | 只能按顺序撤关系和释放资源 | GT 不能假设对象已物理删除 |

这张表的关键不是“谁先调用谁”，而是**权威状态发生了迁移**：注册前，Component 是真相源；创建快照后，内容被复制或引用到 Proxy；正式入场后，Renderer 只相信自己的场景数据库；删除时，GT 先放弃控制权，RT 再完成最后清理。

### 三本账必须在同一个发布版本里相互一致

UE 并不是只想解决“跨线程不能直接读 UObject”，它还要让前面定义的内容账、空间账和登记账在可消费边界上描述同一个版本。把三本账分开，才能理解为什么 Proxy、SceneInfo、`PrimitiveUpdates` 和 `FScene::Update()` 缺一不可。

| 账本一致性 | 要保证什么 | 如果只做一半会怎样 |
| --- | --- | --- |
| 内容一致性 | mesh、材质、可见性标志、LOD 选择结构等渲染内容来自同一版组件状态 | Proxy 可能把新材质和旧 section、旧资源引用拼在一起 |
| 空间一致性 | transform、world bounds、local bounds、previous transform 和空间索引描述同一个位置版本 | 物体画在新位置，却仍在旧八叉树节点被剔除；或 motion vector 使用错误历史 |
| 登记一致性 | packed arrays、持久索引、八叉树、静态批次和各专项子系统对“该 primitive 是否存在”达成一致 | 可见性认为它存在，但 draw cache 已撤销；或场景数组已删除，子系统仍持有旧句柄 |

Proxy 主要稳定**内容一致性**：它把 Renderer 需要的内容固定成一份可跨线程读取的渲染快照。SceneInfo 主要组织**登记一致性**：它保存这份快照的场景身份，以及它与 Renderer 派生结构之间的登记关系。

**空间一致性是一条责任链，而不是某个对象单独拥有的一块数据：**Component 产生新的 transform / bounds 真相；Game Thread 在投递命令时捕获这一版空间载荷；Render Thread 接管控制权后，由 `FScene::Update()` 把同一版本发布到 Proxy 空间状态、packed transform / bounds 和空间索引。命令只携带“要更新到什么”，真正有权让三处空间表示同时生效的是 Renderer 的统一发布阶段。

因此 add / transform / delete 命令负责运输新的空间状态或生命周期意图，`FScene::Update()` 负责把内容、空间和登记三本账推进到同一个可消费版本。

这也解释了为什么“命令已经在 Render Thread 执行”仍不等于“场景已经更新”。命令执行只证明控制权已经到 RT；只有批量吸收并发布完成后，所有消费者才获得一个可用版本。后续可见性、GPUScene、静态 draw 缓存不需要各自猜测“这个 primitive 更新到一半了吗”，因为它们面对的是正式发布后的场景状态。

用椅子举例：组件换了材质但旧 Proxy 仍在场，是内容版本不一致；组件已经移动，命令也捕获了新 transform，但 Proxy、packed bounds 与八叉树没有发布同一版本，是空间版本不一致；packed arrays 已移除但光照或静态缓存仍引用它，是登记版本不一致。这三类错误在屏幕上都可能表现为“物体不对”，但调试入口完全不同。先判断坏的是内容、空间还是登记，比一开始就追某个 draw call 更有效。

---

## 第一步：Component 发出注册请求

椅子注册时，入口是 `UPrimitiveComponent::CreateRenderState_Concurrent`。这个名字容易让人以为它会立刻创建 GPU 资源。实际上它做的事很克制：只是让组件**开始进入渲染侧注册流程**。

它先更新 Bounds，再检查这个组件该不该加入场景。Bounds 必须在这一步先更新——因为后面 `FScene` 的空间查询、可见性、遮挡、光照和若干缓存都依赖世界包围体。如果椅子还拿着旧 Bounds，Renderer 即使知道它有 mesh，也无法可靠判断它占据世界中的哪块空间。

检查通过，才向 `FScene` 发出“这个 primitive 需要进入渲染场景”的加入请求。**此时还没有 Proxy、没有 SceneInfo，更没有写入 `FScene`。** 跨线程快照要到下一步才创建。

如果组件不该渲染——被隐藏、detail mode 不允许、没有 render state——流程就停在这里。

> **调试路标｜物体完全没出现，先查这一层**：组件有没有进入 `CreateRenderState_Concurrent`？`ShouldComponentAddToScene()` 是否为真？如果这一层就没过，后面的 Proxy / SceneInfo / 可见性都不用查。

---

## 第二步：Game Thread 把组件摘成快照

请求进入 `FScene` 后，仍在 Game Thread，但开始把游戏侧对象整理成 Renderer 能持有的数据。对我们的 StaticMesh 椅子来说，这一步要做三件事：创建 Proxy 快照、创建 SceneInfo 登记卡、把它们打包成一条 add 命令。

### Proxy 创建 gate：输入完整性决定是否生成快照

创建 Proxy 之前，UE 会先排除“无法安全创建快照”的情况。椅子的快照会在下列任一情况下创建失败，返回 `nullptr`：

- `StaticMesh` 为空；
- StaticMesh 仍在异步编译；
- `RenderData` 为空或尚未初始化；
- PSO 预缓存策略要求等 PSO 准备完成；
- Nanite 已启用但当前不允许走 Nanite，又没有可用的 fallback mesh；
- LOD 资源为空或无顶点。

这里有一个对调试极重要的概念：**返回 `nullptr` 不是“画一个默认物体”，而是“当前没有一份安全的渲染快照，Renderer 干脆不登记它”。** 所以一个刚导入、仍在编译、PSO 还没准备好、或 Nanite fallback 不可用的 mesh，可能短时间不可见。这不是后续 draw 阶段“漏画”，而是**入场阶段主动拒绝了不完整数据**——它和“物体进了场景但被剔除”是完全不同的失败，查的地方也不同。

资源齐备后，椅子会按条件创建 `Nanite::FSceneProxy` 或传统 `FStaticMeshSceneProxy`。两者都是 `FPrimitiveSceneProxy` 的子类，后续走同一条 add / update / delete 生命周期。

### 快照里放什么、不放什么

传统 StaticMesh proxy 的构造，会从组件摘取一批描述：StaticMesh 渲染数据、材质相关性、LOD / section、阴影与主通道标志、WPO / Nanite 相关标志、编辑器调试状态等。它通常**引用**重资源，而不是复制整份几何。

这里要分清三类数据，因为它们的“定稿时机”不同：

```text
Proxy 构造时算出的轻量描述:
  pass relevance、材质相关性、LOD / section、shadow / lighting / debug flags

Proxy 引用（不复制）的重资源:
  StaticMesh RenderData、material interface、Nanite resources、BodySetup

不在构造时定稿的空间状态:
  Transform、WorldBounds、LocalBounds、AttachmentRootPosition
```

为什么 Transform 不在构造时写成最终渲染位置？因为 UE 把“内容快照”和“空间状态写入”刻意拆成了两个阶段。add 命令会把 RenderMatrix、WorldBounds、LocalBounds、AttachmentRootPosition、PreviousTransform 捕获进去；真正写入 Proxy 要等 Render Thread 执行命令时调用 `Proxy->SetTransform(...)`。记住这个拆分，后面讲“移动椅子”时会再用到它。

用椅子的具体数据代入，就能看出这个拆分解决了什么问题：mesh、材质槽和“是否投射阴影”回答的是**这把椅子是什么渲染对象**；transform、bounds 和 previous transform 回答的是**这一刻它在哪里**。前一类变化通常让旧快照失效，后一类变化在满足条件时可以沿用旧身份。若把两类数据混在一个可随时被 GT 改写的对象里，Renderer 就无法判断一次变化究竟只需移动空间索引，还是必须重建静态批次、材质相关性和专项管线登记。

### SceneInfo 在 GT 就和 Proxy 一起创建

紧接着，Game Thread 上就 `new` 出 `FPrimitiveSceneInfo`，并把 Proxy 的 `PrimitiveSceneInfo` 指针指回这张登记卡。

这里有一个常见误解需要纠正：**UE5.7 中 SceneInfo 不是“Render Thread 收到 Proxy 之后才创建”的；它在 GT 的 add 路径里随 Proxy 一起创建。** Render Thread 后面只是接管它，并把它纳入待发布变化。

这一步结束时，椅子的状态是：

```text
UStaticMeshComponent
  └─ FStaticMeshSceneProxy / Nanite::FSceneProxy   （快照已就绪）
       └─ FPrimitiveSceneInfo                       （登记卡已就绪）
            └─ AddPrimitiveCommand                  （打包好，等待进入 Render Thread）
```

它已经被打包，但还没有真正入场——没有紧凑索引、没有持久索引、没有八叉树 id，也没加入 `Scene->StaticMeshes`。

---

## 第三步：Render Thread 初始化快照并排入待发布队列

打包好的 add 命令被投递到 Render Thread。它在 RT 上按顺序做三件事：

```text
① Proxy->SetTransform(...)              把空间状态写进 Proxy
② Proxy->CreateRenderThreadResources()  让子类有机会建 RT 端资源
③ AddPrimitiveSceneInfo_RenderThread()  把 SceneInfo 排进待发布队列
```

第 ① 步 `SetTransform` 把 LocalToWorld、WorldBounds、LocalBounds、ActorPosition 写入 Proxy，并触发 transform 变化相关的更新——这正是第二步里被推迟的“空间状态写入”。第 ② 步是 Proxy 子类准备 RT 资源的入口；很多子类这里是空实现，但生命周期上保留了这个安全点。

最容易误读的是第 ③ 步 `AddPrimitiveSceneInfo_RenderThread`。它的名字像“把 primitive 加进场景”，但在 UE5.7 里它只做一件小事：检查这个 SceneInfo 还没有紧凑索引，然后把它通过 `PrimitiveUpdates` 排队。此刻 SceneInfo **仍不属于** `FScene` 的紧凑数组，也没有八叉树 id。

为什么不在这里直接插入场景？因为“真正入场”不是写一个字段，而是**一组必须保持一致的状态修改**：紧凑数组、类型偏移表、持久 id 映射、空间八叉树、velocity、distance field、Lumen、光追、静态网格缓存，都可能同时被牵动。UE 让 `FScene::Update()` 批量吸收这些变化并统一发布，使后续可见性和 draw 收集看到的永远是同一个一致版本的场景。

`FScene::Update()` 开头还会先等待外发的 RHI command list 完成，再去修改 proxy 和场景数组。本篇不展开这背后的线程模型（那是 03 篇的事），但它解释了为什么 `FScene::Update()` 是本章的“安全发布点”：它不是“又一个被调用的函数”，而是 Renderer 在 Render Thread 上批量吸收 add / update / delete，并让正式场景状态成立的地方。

---

## 第四步：`FScene::Update()` 发布可消费场景版本

`FScene::Update()` 把 `PrimitiveUpdates` 排干后，先把命令分成 added、removed、deleted、transform、instance 等几类，再集中维护 `FScene` 的持久结构。对第一次入场的椅子，它先分配两种索引。这里不能只记“有两个 id”，而要记住它们分别服务两种完全不同的数据形态：

```text
PackedIndex       FScene 热路径 packed arrays 的下标。
                  可见性、遮挡、PrimitiveTransforms / PrimitiveBounds 等并行数组都用它。
                  add/remove 为了保持紧凑可能 swap，所以它不是跨帧稳定句柄。

PersistentIndex   当前 SceneInfo / Proxy 入场周期内稳定的 primitive 句柄。
                  它来自带洞的持久 id 分配器，适合给“每个已登记 primitive 一份跨帧状态”的系统做直接索引。
                  它不是 Component 的永久 ID，也不保证跨 Proxy 重建保持不变；访问 packed arrays 时仍要映射回当前 PackedIndex。
```

这个区别非常重要。`PackedIndex` 回答的是“这个 primitive 现在在 `FScene` 的紧凑场景数组第几格”；`PersistentIndex` 回答的是“在这个注册生命周期里，我怎样稳定地追踪同一个 primitive”。GPUScene、Virtual Shadow Map cache、某些调试或跨帧 bitset 会偏爱稳定句柄，并在需要读取 scene-side packed data 时映射回当前 `PackedIndex`；但可见性循环、bounds 读取、proxy 指针读取、transform 更新这些 `FScene` 热路径数组本身仍然按 `PackedIndex` 直接下标。

一个最小 add / remove / swap 例子可以把这两个索引钉住：

| 时刻 | Packed arrays 视角 | Persistent 视角 | 调试含义 |
| --- | --- | --- | --- |
| A、B 都在场 | A 在 packed 槽 0，B 在 packed 槽 1 | A、B 各有稳定句柄 | 热路径按 packed 槽遍历 |
| A 被删除 | B 可能被换到 packed 槽 0 | B 的稳定句柄不变 | 跨帧记录不能把旧 packed 槽当身份 |
| C 新入场 | C 拿到当前可用 packed 槽 | C 获得自己的稳定句柄 | 新对象不要继承旧对象的调试结论 |

所以看一份 primitive 是否“还是同一个”，先问 persistent 句柄；看这一帧要从哪个数组读 transform、bounds、proxy，才问当前 `PackedIndex`。

分配好后，SceneInfo 和 Proxy 被写入一组**并行 packed arrays**。这一步不是简单地“把 SceneInfo 指针放进 `FScene`”，而是 Renderer 对热路径字段选择性地转成了按字段分列存储（Structure of Arrays，SoA）：把全 primitive 循环里最常读的列拆出来，让不同 pass 只读自己需要的数据，不必把整个 `FPrimitiveSceneInfo` 的重状态拖进 cache。

| 这类数组保存 | 例子 |
| --- | --- |
| 对象指针 | `Primitives`、`PrimitiveSceneProxies` |
| 空间状态 | `PrimitiveTransforms`、`PrimitiveBounds`、`PrimitiveOcclusionBounds` |
| 热标志 / ID | `PrimitiveFlagsCompact`、`PrimitiveVisibilityIds`、`PrimitiveOcclusionFlags`、`PrimitiveComponentIds` |
| 专项紧凑数据 | ray tracing primitive flags / data、octree node index 等 |

所以 `FScene` 同时保留两种看待同一个 primitive 的方式：

```text
SceneInfo / Proxy         单个 primitive 的生命周期、资源、缓存和子系统注册状态
FScene packed arrays      全场景线性遍历时的热字段列，统一由 PackedIndex 对齐
```

对象式布局会把一个 primitive 的 bounds、flags、资源和缓存聚在同一对象里；SoA 则把所有 primitive 的 bounds 连续放在一列、flags 连续放在另一列。这里不是把 SceneInfo 的所有字段都按列拆开。留在 `FPrimitiveSceneInfo` 里的，是更适合“按单个 primitive 处理”的重状态：静态 mesh 列表、cached mesh draw command 信息、Nanite bins、光照交互、间接光缓存、Lumen / distance field / ray tracing 相关注册状态等。换句话说，`SceneInfo` 仍是 FScene 管理 primitive 的对象外壳；`FScene` 只把 visibility、occlusion、GPUScene 上传前置输入、ray tracing 紧凑状态这类热字段拆出来按列访问。

空间查询还有第三种形态。`FScene` 的八叉树并不直接塞完整 `FPrimitiveSceneInfo`，也不直接查询上面的 packed arrays；它使用一个轻量的 `FPrimitiveSceneInfoCompact`，里面只放 SceneInfo 指针、Proxy 指针、bounds、draw distance、visibility id 和 compact flags。这样八叉树能快速做空间 / frustum 查询，而线性 visibility 或 GPUScene 路径仍然可以走 packed arrays。两套结构并行存在：**octree 负责空间索引，packed arrays 负责线性热遍历**。

数组位置确定后，登记卡才真正去各子系统“报到”：创建或更新间接光相关状态、插入空间八叉树、写 bounds / flags / visibility id / occlusion bounds / component id、登记 reflection / lightmap / 动态间接阴影等关系。到这一步，椅子才成为 `FScene` 中可被空间查询和可见性阶段使用的 primitive。

### SceneProxy 登记依赖哪些场景不变量

把 `FScene` 称为“常驻渲染场景数据库”还不够。对 SceneProxy 生命周期而言，真正需要掌握的是：Renderer 为不同查询保存同一 primitive 的多种视图，而这些视图必须由统一发布阶段共同维护：

| 数据形态 | 最擅长回答的问题 | 典型消费者 |
| --- | --- | --- |
| `FPrimitiveSceneInfo` / Proxy 对象 | “这个 primitive 有哪些资源、缓存和专项登记？” | add/remove、缓存维护、单对象更新 |
| packed arrays | “遍历全场景时，只读 bounds / flags / transform 的哪一列？” | visibility、occlusion、GPUScene 前置整理 |
| compact octree element | “世界某块空间里可能有哪些 primitive？” | 空间查询、粗粒度候选筛选 |

对象视图负责单 primitive 的关系与生命周期，packed arrays 服务按字段线性扫描，compact octree element 服务空间候选查询。这里不需要展开 `FScene` 的全部内部结构；对本章最重要的是它们不是 SceneInfo 自行修改的容器。SceneInfo 保存登记身份和关系，真正修改 packed arrays、空间索引以及场景级容器的是 `FScene::Update()` 所控制的 Renderer 发布阶段。

关键不变量是：**这些视图必须指向同一个逻辑 primitive，并在发布边界一起变化。**

`Primitives[PackedIndex]`、`PrimitiveSceneProxies[PackedIndex]`、`PrimitiveTransforms[PackedIndex]` 和 `PrimitiveBounds[PackedIndex]` 必须描述同一个已发布版本；八叉树 compact element 也必须能追到同一 SceneInfo / Proxy。移除时为了保持 packed arrays 紧凑，尾部 primitive 可能被交换到空槽。于是被移动 primitive 的 `PackedIndex`、各平行数组位置以及相关索引记录都必须一起改，不能只移动 `Primitives` 指针而忘记 bounds 或 flags。

这个 swap-remove 设计换来的是后续大规模线性遍历没有空洞，但代价是：**PackedIndex 是位置，不是身份。** 任何跨帧缓存如果偷偷把它当稳定 id，都会在别的 primitive 被删除后产生“数据串到另一个物体”的隐蔽错误。

`PersistentIndex` 正是为另一类需求服务：它允许跨帧系统稳定地说“我追踪的是当前 SceneInfo / Proxy 入场周期中的同一个 primitive”。但稳定身份并不意味着对象永久不变，更不意味着它是 Component 的永久身份。RenderState 重建会结束旧注册生命周期，新 Proxy / SceneInfo 会重新入场；此时调试工具应把它看作“同一组件产生的新渲染侧实例”，而不是旧实例原地变形。

### 为什么入场适合批量吸收并发布，而不是逐对象立即修改

`FScene::Update()` 会先把待处理记录分类，再对 added、removed、deleted、transform 等集合集中推进。批处理首先是为了正确性：remove 和 add、索引交换、缓存撤销与重建需要明确顺序，避免一个消费者看到半完成状态。它同时也服务性能：

- 分配索引和扩展并行数组可以集中进行，减少每个 primitive 都触发一次结构调整；
- 同类型 Proxy 可以在紧凑区间里维护，后续类型相关遍历更容易跳过无关对象；
- 静态 mesh 收集、draw command 缓存和部分子系统登记可以面向一批 SceneInfo 组织工作；
- GPUScene 等下游只需消费“本轮哪些 primitive 变脏”，而不是重新扫描整个组件世界。

这并不意味着所有注册成本都会凭空消失。大批 StaticMesh 同帧 Spawn 时，Proxy 创建、资源准备、SceneInfo 入场、静态批次建立和各专项系统登记仍然是真实工作。批处理的价值是把这些工作放到可排序、可并行、可保持一致性的阶段，而不是让 Game Thread 在每次组件变化时直接撕扯 Renderer 内部结构。

因此分析一次“关卡流送后 RT 突然变重”时，不能只看最终 draw 数。应区分 Game Thread 是否在批量创建组件和 Proxy、Render Thread 是否在本轮集中兑现大量 add、成本主要来自场景索引还是静态 mesh 与专项缓存，以及下一帧稳定后成本是否回落。若峰值只发生在入场帧，问题往往是场景数据库构建成本；若之后每帧仍高，则要继续判断这些对象是否错误地走了动态收集或频繁 RenderState 重建。

对 StaticMesh，还要多做一件事：把稳定几何交给后续 draw 收集路径。`FPrimitiveSceneInfo::AddStaticMeshes` 会对每个 SceneInfo 调 Proxy 的 `DrawStaticElements`，收集出这个 primitive 的静态批次（`FStaticMeshBatch` / `FMeshBatch`），存进 SceneInfo 自己的 `StaticMeshes` / `StaticMeshRelevances`，再把这些 batch 的指针挂到全局的 `Scene->StaticMeshes`。如果需要缓存，它还会触发 MeshDrawCommand、Nanite material bins、光追 primitive 的缓存更新。

注意，这条 static mesh 路径不是刚才那次 `FScene` packed arrays 的 SOA 拆分。它是另一条整理动作：**SceneInfo 内部 per-primitive mesh 列表 → 场景级 static mesh 候选 / cached draw command**。前者解决“全 primitive 热字段怎么按列遍历”，后者解决“这个 primitive 能贡献哪些稳定 mesh，后续 MeshPass 怎样复用”。把这两条混在一起，会误以为 `StaticMeshes` 也是 `FScene` primitive SOA 的一列；实际上它仍然以 SceneInfo 为归属，只是被场景级 draw list 引用和缓存。

入场完成后，椅子的状态可以这样理解：

```text
Proxy      已有 transform、bounds、RT 资源，能回答 Renderer 的提问
SceneInfo  已有 PersistentIndex / PackedIndex，成为 FScene 的管理单位
FScene 数组/八叉树   已能被可见性、GPUScene、遮挡、光照交互使用
StaticMeshes         已成为第 07 篇 MeshDrawCommand 的上游输入
```

本篇不展开 `FMeshBatch` 怎样变成 `FMeshDrawCommand`（那是 07 篇）。这里只要抓住一点：**对稳定的 StaticMesh，很多 draw 输入不是每帧从组件摘取，而是在注册/更新阶段就沉淀到了 Renderer 侧场景里。** 这正是下一节虚函数体系要解释的分界。

### Worked case：同一把椅子，为什么“已入队”和“已入场”表现不同

假设椅子的 add 命令已经在 Render Thread 执行，Proxy 也完成了 `SetTransform`，但断点停在下一次 `FScene::Update()` 之前。此时你能看到一个有效 Proxy 和 SceneInfo，却仍然不该期待 BasePass 找到它：SceneInfo 没有可用于 packed arrays 的 `PackedIndex`，八叉树里没有它的 compact 记录，静态批次也还没通过 `DrawStaticElements` 沉淀下来。

等 `FScene::Update()` 批量吸收并发布后，同一个对象才同时获得三种可消费身份：

1. **线性遍历身份**：`PackedIndex` 让 bounds、transform、flags 等热数据能按列读取；
2. **空间查询身份**：八叉树里的 compact 记录让可见性按 bounds 找到它；
3. **绘制候选身份**：SceneInfo 保存的静态批次让后续 mesh pass 有稳定输入。

这三个身份必须一起建立，才叫“入场”。因此调试时发现 Proxy 存在，只能证明快照创建成功；发现 add command 执行，只能证明 RT 接管成功；只有确认 `FScene::Update()` 已建立索引、空间记录和必要缓存，才能证明 Renderer 数据库真正接受了它。

---

## 插曲：Renderer 只向快照提问——Proxy 的虚函数体系

走到这里，椅子已经入场。在继续讲“移动、换材质、删除”之前，先停下来认识一套**贯穿全程的接口**——它是理解后续所有 pass 怎么消费这个对象的钥匙，刚才第四步的 `DrawStaticElements` 也属于它。

Proxy 不是一包静态字段，而是 Renderer 用来**询问具体 primitive 子类的接口面**。Renderer 不知道这个对象原本是 `UStaticMeshComponent`、`UInstancedStaticMeshComponent` 还是 `USkeletalMeshComponent`；它只知道自己能向 Proxy 提三类问题：

| Renderer 问的问题 | 提问入口 | 答案用途 |
| --- | --- | --- |
| 这个 View 里它该走哪些路径？ | `GetViewRelevance(View)` | static / dynamic / shadow / custom depth / editor 等 relevance |
| 它有哪些可缓存的稳定 mesh？ | `DrawStaticElements(PDI)` | 注册或静态更新时产出静态批次 |
| 它这一帧有哪些临时 mesh？ | `GetDynamicMeshElements(...)` | 可见性阶段按 View 收集动态元素 |

这三类提问，恰好划出了“注册时缓存”和“每帧收集”的分界线。

普通 StaticMesh 椅子的主体几何，通常是 **static relevant**。它在注册时通过 `DrawStaticElements` 一次性交出稳定批次；之后每帧的可见性只是去筛这些已缓存的数据，而不必回头问组件。反过来，编辑器调试形状、view-dependent 绘制、动态变形、临时线框这类**真正每帧才知道**的内容，才需要走 `GetDynamicMeshElements` 每帧生成。

接口契约对这条边界规定得很直接：

- `DrawStaticElements` 只在 `GetViewRelevance` 声明了 static relevance 时才有意义；
- `GetDynamicMeshElements` 只在声明了 dynamic relevance 时才会被调用；
- 动态收集路径**明确禁止在 Render Thread 解引用 `UObject`**——Game Thread 的状态必须提前镜像到 Proxy 上。

这也是 UE 和 Unity / SRP 在“暴露给用户的工作模型”上差异最大的地方。Unity SRP 常让你每帧围绕 `CullingResults` / `RendererList` 组织绘制；UE 的 StaticMesh 路径则把可缓存的对象描述**提前沉淀**到 `FScene`，每帧主要把“场景里有什么”筛成“当前 View 要画什么”。这不是说 UE 没有动态路径，而是动态路径只服务那些真正每帧才知道的内容。

> **调试路标｜不要只问“`DrawStaticElements` 被调用了吗”**。按这个顺序问：
> 1. `GetViewRelevance` 声明的是 static 还是 dynamic？
> 2. 若是 static：注册/静态更新时 `AddStaticMeshes` 有没有产出批次？
> 3. 若是 dynamic：可见性阶段有没有进入 `GetDynamicMeshElements`？
> 4. Proxy 在这些函数里有没有回读 `UObject`？有，就是线程边界写错了。

### 同一条生命周期，不同的快照内容

虚函数体系还解决了另一个问题：StaticMesh、Nanite、Instanced、Skeletal 看起来像四套完全不同的数据，但 Renderer 不能给它们各写一套入场、移动、删除生命周期。UE 的设计不是“把所有东西压成同一个字段表”，而是让所有 Proxy 都回答同一组渲染问题，至于回答这些问题需要什么数据，由子类自己保存。

先不要从类名看，先从 Renderer 的提问看。一份 Proxy 快照大致由五类内容组成：

```text
Proxy 快照内容
  = 几何表示
  + 选择结构
  + 材质 / Pass 合约
  + 运行时变化状态
  + 专项管线入口
```

**几何表示**回答“这个 primitive 最终能拿什么形状去画”。StaticMesh 的 `RenderData` 是已经面向渲染准备好的资产数据：LOD、section、顶点 / 索引缓冲、vertex factory 所需布局、材质槽映射，以及距离场、card、光追等可选表示。它不是每个组件独有的一份“当前状态”，而更像资产侧可共享的渲染成品，Proxy 只是引用它，并在注册时把可缓存部分变成 static mesh 批次。

Nanite 的 resources 也属于资产侧渲染成品，但它和传统 StaticMesh `RenderData` 的组织目标不同。传统路径更像“LOD + section + 顶点索引缓冲”，Renderer 后面围绕 mesh batch 和 mesh pass 消费它；Nanite resources 则是给 Nanite 管线消费的层级 cluster / page / material 组织。它回答的不是“第几个 LOD 的第几个 section 交出一个普通静态批次”，而是“哪些 Nanite 资源、材质分组和后续 binning 输入可以交给 Nanite 管线”。所以 Nanite 不是 StaticMesh 多了几个 flag，而是同一份 primitive 生命周期下，几何消费契约换成了另一套管线入口。

**选择结构**回答“有多少候选，怎样选”。传统 StaticMesh 的选择结构主要是 LOD 和 section；InstancedStaticMesh 还多了一层 instance：同一个基础网格被放置很多次，每个 instance 有自己的 transform、bounds、custom data、剔除距离或 GPU LOD 相关状态；SkeletalMesh 则要在 LOD、section、骨骼影响、隐藏 section、当前 pose 之间选择。这里的差异不是语法差异，而是 multiplicity 和 mutability 的差异：Instanced 的核心问题是“一个几何源对应很多份放置”；Skeletal 的核心问题是“资产拓扑稳定，但最终顶点流由当前姿态和变形状态决定”。

**材质 / Pass 合约**回答“这个 primitive 能进哪些渲染路径”。材质槽、material relevance、shadow、WPO、custom depth、distance field、debug view 等状态不是装饰信息；它们决定 `GetViewRelevance`、`DrawStaticElements`、`GetDynamicMeshElements` 后面会声明哪些 pass、能不能缓存、需不需要动态收集。如果这层合约错了，几何资源本身可能完全正常，但物体仍然不会进目标 pass。

**运行时变化状态**回答“哪些东西会随组件、实例或动画变化”。StaticMesh 的基础几何通常稳定，移动主要改变 transform / bounds；Instanced 的基础几何也稳定，但 instance 列表可能增删、重排或更新 transform；SkeletalMesh 的 `FSkeletalMeshRenderData` 是资产侧 LOD / section / skin weight 等渲染布局，而 `MeshObject` 是每个组件自己的运行时变形状态，用来把 pose、morph、deformer、skin cache、velocity 等变化转成渲染线程能消费的顶点流或 vertex factory 状态。把这两者混成一个“skeletal render data”会丢掉关键区别：一个偏资产拓扑，一个偏当前组件的变形结果。

**专项管线入口**回答“这个 primitive 是否还要被 Nanite、GPUScene、光追、distance field、skin cache 等系统登记”。这些入口不改变共同生命周期，但会改变入场后需要维护哪些子系统关系。也正因为有这些入口，RenderState dirty 往往不能靠改一个字段解决：换 mesh、换材质、切 Nanite/fallback、改变 skinning 能力，都可能让旧快照的专项登记关系整体失效。

这样再看各子类，比较才有意义：

| Proxy 子类 | 几何表示是什么 | 稳定部分 / 变化部分 | 因此怎样回答 Renderer |
| --- | --- | --- | --- |
| `FStaticMeshSceneProxy` | 传统 StaticMesh 渲染资产：LOD、section、顶点 / 索引缓冲、vertex factory 输入、材质槽，以及可选距离场 / 光追表示 | 基础几何和 section 通常稳定；transform、材质覆盖、pass relevance 或部分调试状态可能变化 | 适合在注册或静态更新时交出可缓存 static mesh 批次，每帧主要做可见性和 pass 筛选 |
| `Nanite::FSceneProxy` | Nanite 专用资源：cluster / page / material 分组及后续 binning 所需输入 | 资产资源稳定；可见 cluster、streaming page、材质 binning 等由 Nanite 管线按视图和资源状态处理 | 仍是 primitive proxy，但几何消费交给 Nanite 管线，不等价于普通 StaticMesh section 批次 |
| `FInstancedStaticMeshSceneProxy` | 一个基础 StaticMesh 渲染资产 + 多个 instance 的场景数据 / buffer | 基础几何稳定；instance transform、bounds、custom data、增删和 GPU LOD / cull 状态变化 | 一个 Proxy / SceneInfo 管很多份放置，核心是把 per-instance 数据交给剔除、GPUScene 和 draw 路径 |
| `FSkeletalMeshSceneProxy` | SkeletalMesh 资产侧 render data + 每组件 `MeshObject` 运行时变形状态 | LOD / section / skin weight 等资产布局较稳定；pose、morph、deformer、skin cache、velocity 随帧变化 | 拓扑和材质合约来自资产 render data，最终可画顶点流由 `MeshObject` 的当前变形状态提供 |

这张表的重点不是记住四个子类的字段，而是记住分类目的：**同一条生命周期让 Renderer 能统一 add / update / delete；不同快照内容让每类 primitive 保留自己最自然的数据形态。** 如果强行把它们压成一个扁平结构，StaticMesh 会失去注册时缓存的优势，Instanced 会表达不出“大量放置”的问题，Skeletal 会把资产拓扑和当前姿态混在一起，Nanite 会被迫伪装成传统 section 批次。反过来，如果每类 primitive 都有自己的生命周期，Renderer 又无法统一维护 `FScene` 索引、八叉树、GPUScene、光追和销毁顺序。

所以，生命周期始终是同一条：

```text
CreateSceneProxy
  → new FPrimitiveSceneInfo
  → AddPrimitiveCommand
  → PrimitiveUpdates
  → FScene::Update
  → AddToScene / AddStaticMeshes（或每帧 dynamic 收集）
```

子类差异只决定：它用什么几何表示、选择结构和运行时状态来回答这条生命周期里的统一提问；以及它入场后要不要登记到 Nanite、光追、distance field、skin cache 等专项系统。

把同一条生命周期换成三个局部镜头：

- Instanced：一份基础网格仍是共享几何，但“这一棵草”的 transform、custom data、可见性状态来自 instance 记录；查缺失时先分清是基础网格坏了，还是某个 instance 没同步。
- Skeletal：资产侧 LOD / section 是稳定描述，当前 pose、deformer、skin cache 是每组件运行时结果；查姿态错误时不要把资产拓扑和当前变形当成同一层。
- Nanite：primitive 仍按同一条 add / update / delete 入场，但几何消费问题要落到 Nanite 专项入口和材质分组是否建立，而不是期待普通 static section 批次。

> **调试路标｜分两层看一个 primitive**。第一层是共同生命周期：Proxy 是否创建、SceneInfo 是否入队、`FScene::Update()` 是否分配索引、是否插入八叉树。第二层问快照内容是哪一类坏了：资产侧几何表示缺失吗？LOD / section / instance / pose 选择结构错了吗？材质和 pass 合约没声明目标路径吗？per-instance 或 skinning 运行时状态没有同步吗？Nanite、光追、distance field、skin cache 这类专项入口没有建立或已经失效吗？只查第一层，会漏掉“Proxy 有了但子类资源不全”；只查子类字段，又会误以为每种 proxy 都有完全不同的注册生命周期。

---

## 第五步：椅子怎样成为 draw 收集的输入

以一把被选中的 Movable StaticMesh 椅子为例：椅子主体的稳定网格可以在注册 / 静态更新时沉淀成静态候选；编辑器选中轮廓、调试轴或视图相关辅助线不能长期缓存，只能在当前 View 需要时走动态收集。排查“主体可见但高亮不见”时，先查 dynamic relevance 和 `GetDynamicMeshElements`；排查“主体本身没进 BasePass”时，先回到静态批次、可见性和 pass relevance。

椅子进入 `FScene` 之后，Renderer 在绘制阶段**仍然不会回读组件**。后续 draw 收集看到的输入只来自两类地方，区别在“时间点”：

```text
静态输入（注册/静态更新时就沉淀好）:
  SceneInfo.StaticMeshes / StaticMeshRelevances
    → Scene.StaticMeshes
    → 每帧可见性按 View 筛出可见的静态候选

动态输入（当前 View 需要时才现取一帧）:
  Proxy.GetDynamicMeshElements(...)
    → View.DynamicMeshElements
    → 每帧按 View 计算 pass relevance
```

两条路都从 Proxy / SceneInfo 出发，但静态路在注册阶段就把稳定 `FMeshBatch` 沉淀进了 SceneInfo 和场景的静态网格容器，每帧只需问“当前 View 要不要消费这些已有候选”；动态路则只在当前 View 需要时才向 Proxy 现取一帧临时 mesh element，适合选中高亮、调试绘制、视图相关绘制这类不能长期缓存的内容。

仍以椅子为例：椅子主体的 LOD/section/material slot 在注册时已经足以形成稳定批次，因此适合走 static 路；但当编辑器要求画选中轮廓、碰撞线框或某种依赖当前 View 的调试几何时，这部分结果直到本帧视图确定后才知道，适合走 dynamic 路。**static / dynamic 不是“物体会不会移动”的同义词，而是“这份 mesh 描述能否跨帧缓存”的契约。** 一把 Movable 椅子的主体几何仍可贡献稳定 static mesh batch；移动时更新的是它的空间状态，而不是每帧重新向 Component 索取全部几何。

这个 worked case 也给出一个条件判断：若某份绘制数据只依赖长期稳定的资产与 Proxy 内容，就应尽量在注册或静态更新时沉淀；若它依赖当前 View、当前调试模式或每帧变化的生成结果，就必须在动态收集时产生。选错路径的后果也不同：把真正动态的数据塞进静态缓存，会得到过期结果；把稳定数据全部放到动态路径，则会把可复用工作变成逐 View、逐帧重复工作。

这一步的结果**还不是**最终 GPU draw。它只是把“这个 primitive 能怎样贡献 mesh”整理成后续 MeshPass 能消费的输入。07 篇会接着讲 `FMeshBatch` 怎样被 MeshPassProcessor 转成 `FMeshDrawCommand`，以及排序、合批和 PSO 怎样发生；本篇只需把上游边界讲清：**draw 收集的输入来自 `FScene` 和 Proxy，不来自 live 组件。**

### 静态缓存保存稳定 draw 配方，View 仍决定本帧消费

“静态物体可以预缓存 DrawCommand”这句话若不拆开，容易制造两个误解：一是把 static 等同于 Mobility=Static；二是以为缓存后 Renderer 每帧什么都不用判断。

更准确的过程是：Proxy 在注册阶段通过 `DrawStaticElements` 描述长期稳定的 mesh batch；SceneInfo 保存这些批次及其 relevance；符合条件的路径还能把材质、shader、顶点输入、渲染状态等可复用组合进一步整理为 cached mesh draw command。缓存省掉的是**从稳定 mesh 描述反复推导 draw 配方**的工作，不是把本帧视图判断和 GPU 执行缓存掉。

每一帧仍然需要回答：当前 View 能否看到这个 primitive；本帧选择哪个 LOD，哪些 section / element 有效；它与当前 mesh pass 的 relevance 是否匹配；缓存模板是否仍有效，当前实例数据和 primitive id 怎样接入；排序与实例裁剪留下多少 draw work，后续 RHI recording、Platform Queue Submit 和 GPU consumption 又推进到哪一层。

因此以下四句话要严格区分：

`有 Proxy` 只说明渲染快照存在；`有 SceneInfo` 只说明 Renderer 有管理外壳；`有 StaticMeshes` 说明稳定几何候选已经登记；**本帧有实际 draw work** 还要求可见、pass 合法且命令有效；是否已形成平台命令、进入 Platform Queue 或被 GPU 消费属于更深证据。

这组区别非常适合解释“为什么我在 SceneInfo 里看见 mesh，屏幕上却没有物体”。此时不应回头怀疑 Component 是否有材质，而应沿已存在的候选继续查 view relevance、visibility、LOD/section 选择和 mesh pass 消费。反过来，如果 `StaticMeshes` 本身为空，就不该直接跳到 PSO 或 RHI；问题仍在 Proxy 的稳定几何描述、资源准备或注册阶段。

缓存也有明确失效边界。只要旧 Proxy 对 mesh、材质和 pass 合约的回答不再可靠，旧批次和旧命令就不能被“顺手改几个字段”继续使用。RenderState 重建虽然昂贵，却能确保旧缓存先从所有引用者撤销，再用新快照重新建立。这个成本模型正是下一节区分 Transform dirty 与 RenderState dirty 的基础。

> **调试路标｜物体没进 pass，按静态/动态分叉**：
> - 静态物体没进 BasePass：先查 SceneInfo 有没有静态批次、当前 View 是否把它们判为可见、pass relevance 是否允许进入对应 mesh pass——**不要马上查 RHI**。
> - 动态物体没出现：查 `GetViewRelevance` 是否声明 dynamic relevance、动态收集是否真的调到了 Proxy、Proxy 是否已提前镜像了它要用的游戏侧状态。

---

## 运行时变化（一）：移动椅子时保留身份并发布空间状态

椅子注册完成后，玩家把它拖到桌子旁边。这里最容易犯的错，是以为 Game Thread 会找到 Proxy，然后直接改它的 `LocalToWorld`。UE 不这么做——Proxy 的所有权已经交给了 Render Thread，**Game Thread 不能直接写它**。

那移动到底改了什么？Game Thread 调 `UActorComponent::MarkRenderTransformDirty`，设一个 transform dirty 标记，并请求帧末更新。帧末统一处理时有一个**优先级**很关键：先看 render state 是否 dirty，只有在“旧 render state 不需要重建”时，才去处理 transform、dynamic data、instance data。

```text
RenderState dirty 优先:   旧 Proxy 本身可能已经无效 → 先 remove + add 重建
Transform dirty 其次:    同一份 Proxy 仍能代表这个物体 → 只更新空间状态
```

为什么要这个优先级？因为如果旧 Proxy 已经要销毁了，再给它排一条 transform 更新不仅没意义，还可能把更新发给一个即将退出场景的对象。

确认只是 transform 变化后，组件先更新 Bounds，再请求场景更新这个 primitive 的变换。这里先把一个容易混在一起的词拆开：**Mobility 会参与组件的更新策略，但它不是 static/dynamic draw contract 的同义词，也不能单独决定所有 PrimitiveComponent 的更新路径。** Static / Stationary / Movable 表达对象空间与构建关系的稳定承诺；具体是局部更新还是重建，还要看组件类型、Proxy 契约和变化是否让旧渲染快照失真。

对本章这类 `UStaticMeshComponent`，是否能保留原 Proxy / SceneInfo 身份由其 transform-update 契约决定。常见的 Movable 椅子允许在原身份上发布新空间状态；当 StaticMesh 的组件策略判断旧 Proxy 不应原地接受 transform 变化时，则通过 render-state 重建走 remove + add。**这是一条 StaticMeshComponent 的条件化策略，不是“所有非 Movable Primitive 一律重建”的全局规则。**

为什么要这样分？因为位置在 Renderer 里不是一个孤立矩阵。某个组件若把空间、构建数据或派生登记承诺为稳定关系，强行只改矩阵就等于要求所有相关消费者都支持局部补丁；只要有一个系统没有同步，渲染侧就会出现“bounds 是新的、缓存还是旧的”这类难查问题。反过来，能够保留旧 Proxy 的组件，也必须保证 `FScene::Update()` 能把 Proxy、packed 空间数据、空间索引和相关派生状态推进到同一版本。

本章的 Movable 椅子从一开始就承认自己会移动，因此它的 StaticMesh 组件契约允许保留同一份 Proxy / SceneInfo 身份，只把空间账同步到 Renderer 的各个索引和缓存入口。但这不妨碍它的稳定 mesh 描述贡献 static mesh batch；**可否局部移动**与**mesh 描述可否跨帧缓存**是两条不同判断轴。

所以这里的分叉不是“要不要改矩阵”这么小，而是两种设计合同：

| 路径 | 触发条件 | 保留什么 | 重新做什么 | 直接影响 |
| --- | --- | --- | --- | --- |
| 轻量 transform 更新 | StaticMesh 是 Movable，且旧 Proxy 内容仍有效 | 同一个 Proxy、SceneInfo、资源引用、已建立的静态批次 / draw 缓存身份 | 只发布新的 transform、bounds、previous transform，并同步空间相关系统 | 适合每帧或频繁移动，成本集中在空间状态更新 |
| transform 触发重建 | StaticMesh 组件策略判断旧 Proxy 不应原地接受这次 transform 变化 | 不保留旧 Proxy / SceneInfo 身份 | remove 旧 primitive，再按当前位置 add 一次，重新建立登记和缓存关系 | 成本明显更高，但避免旧内容、空间与登记假设混杂 |
| RenderState 重建 | mesh、材质、pass relevance、proxy 子类选择等内容失效 | 不保留旧 Proxy 内容 | destroy + create render state，重走入场五步 | 用于“旧快照已经不是同一个渲染对象”的变化 |

我们的椅子是 **Movable**，所以走轻量路径。这里的“轻量”不是“只改一个矩阵字段”，而是**不重建 Proxy / SceneInfo 的局部场景空间发布**。Game Thread 捕获一组新的空间参数：

```text
WorldBounds、LocalBounds、LocalToWorld、
AttachmentRootPosition、PreviousTransform、PrimitiveSceneProxy
```

这些参数可能先被批量积累，也可能直接投递一条 transform 更新命令。Render Thread 执行后，把它排进 `PrimitiveUpdates`——终点仍然是 `FScene::Update()`。在这条路径里，UE 不会重新 `CreateSceneProxy()`，不会重新 `CreateRenderThreadResources()`，也不会因为移动这把 Movable 椅子而重新生成它的 StaticMesh proxy；它只是把“同一个 primitive 现在位于新的空间位置”交给 Renderer 等待统一发布。

`FScene::Update()` 处理 transform 更新时，把这次移动当成一次**场景空间状态发布**，而不是单字段写入：必要时先把 SceneInfo 从旧的空间关系里移出，再按新 bounds 加回；更新 velocity / previous transform；调 `Proxy->SetTransform(...)`；写 `PrimitiveTransforms[PackedIndex]`；并更新 distance field、Lumen、光追 group bounds 等所有依赖空间状态的系统。对 StaticMesh 这种使用 proxy primitive uniform buffer 的静态元素，轻量路径的关键就是：**静态 draw 输入的身份可以保留，矩阵和 bounds 通过已有 Proxy / SceneInfo 的空间状态推进。**

所以“移动 Movable 椅子”的真实含义是：

```text
GT 不写 Proxy。
GT 只捕获新的空间状态。
RT 把 transform 命令排进 PrimitiveUpdates。
FScene::Update 在一个安全点，统一同步 Proxy、紧凑数组、八叉树、velocity 和各子系统。
```

（`r.WarningOnRedundantTransformUpdate` 和 `r.SkipRedundantTransformUpdate` 只影响“冗余 transform 更新”的告警或跳过策略，不改变上面这条生命周期。）

---

## 运行时变化（二）：内容合同失效时替换旧快照

Transform dirty 的前提是“同一份 Proxy 仍然有效”。**RenderState dirty 表示相反情况：旧快照的内容已经不能代表这个组件了。** 典型触发：

- 换 StaticMesh 资源；
- 换材质集合；
- 改变会影响 pass relevance、静态批次或缓存 draw command 的渲染标志；
- StaticMesh 的 transform 变化触发其 Proxy 重建策略（见上一节）。

这时 UE 不去给旧 Proxy 打局部补丁，而是做一次**完整的销毁+重建**：

```text
RecreateRenderState_Concurrent
  = DestroyRenderState_Concurrent   → RemovePrimitive   → 旧 Proxy / SceneInfo 退出 FScene
  + CreateRenderState_Concurrent    → AddPrimitive      → 用当前组件状态重新走一遍入场五步
```

为什么宁可整段重建，也不局部更新？因为这类变化可能牵动材质相关性、静态批次、draw command 缓存、光照交互、资源引用，甚至 proxy 子类的选择。UE 没有要求每个 Renderer 子系统都支持对旧 Proxy 做任意局部补丁；remove + add 更可控——旧 SceneInfo 先从所有索引和缓存里撤干净，新 SceneInfo 再按完整流程重新进入。

这也解释了一个常见的性能差异：**本章 Movable StaticMesh 椅子的纯空间变化走轻量 transform 更新；当组件策略要求重建，或 mesh、材质、pass relevance、Proxy 类型等内容合同失效时，则触发完整重建。** 知道这个分界，就能解释“为什么有些改动很便宜，有些改动会卡一下”，同时不会把一个组件类型的策略误写成全局 Mobility 定律。

可以用一个很实用的判据区分 Transform dirty 与 RenderState dirty：

> **变化之后，旧 Proxy 是否仍能诚实回答 Renderer 的三个问题——有哪些稳定 mesh、有哪些本帧动态 mesh、能进入哪些 pass？**

若答案仍然成立，只是对象换了位置，那么保留 Proxy / SceneInfo 身份并发布空间状态即可。若换材质导致 pass relevance、WPO、阴影或缓存资格变化，换 mesh 导致 LOD/section/资源引用变化，或者 proxy 子类选择发生变化，旧 Proxy 的回答已经失真，就必须重建。这个判据比背一张“哪些属性调用哪个 dirty 函数”的清单更可靠，因为它直接对应快照契约。

---

## 运行时变化（三）：删除椅子时先断引用，再撤关系和回收对象

椅子被删除时，Game Thread **不能立刻 `delete Proxy`**。原因很直接：Render Thread 的命令队列里可能还有持有 Proxy 指针的命令在排队；`FScene` 里也还有八叉树、静态网格缓存、光照交互、GPUScene、Lumen、光追等一堆注册关系。直接删掉，这些地方就全成了悬空指针。

所以删除被拆成三段责任清晰的工作：

```text
① GT 断引用    DestroyRenderState_Concurrent → RemovePrimitive
               取出 Proxy/SceneInfo，调 ReleaseSceneProxy
               —— 只清空组件侧的引用，不释放 Proxy 对象本身
② RT 撤关系    remove 命令在 RT 执行：把 delete 排进 PrimitiveUpdates，
               再 DestroyRenderThreadResources 释放 RT 资源
③ RT 删对象    FScene::Update 撤销所有场景关系后，才真正 delete
```

特别注意第 ① 步里的 `ReleaseSceneProxy`：它断开的是 **Game Thread 侧的引用**（清空组件的 `SceneProxy` 指针、处理 always-visible 引用计数），**不是释放 proxy 对象**。把这一步误读成“已经删了”，是删除相关 bug 的常见根源。

真正的撤场和删除发生在 `FScene::Update()`，但不必背一长串子系统名称。它依次处理三类责任：先撤销**场景身份**，让 packed arrays、稳定索引映射和空间索引不再发现该 primitive；再撤销**派生消费者关系**，例如静态绘制缓存、GPUScene 或光照关系不再引用旧 SceneInfo / Proxy；最后处理**对象与资源**，释放 RT 资源，并在安全阶段删除 Proxy 和 SceneInfo，HitProxy 等特殊引用按自己的延迟清理契约结束。核心原则是：先停止发现对象，再让派生消费者放手，最后释放共同上游对象。

一句话概括这条所有权边界：

> **组件负责断开 Game Thread 侧引用；Render Thread 负责按顺序撤销 Renderer 的注册关系；最终 Proxy 和 SceneInfo 由 Renderer 在 `FScene::Update()` 的安全阶段删除。这里的 CPU 对象删除不是 GPU 完成证据。**

这条顺序还揭示了三个不同的“已经结束”：组件侧 `SceneProxy == nullptr` 只表示 **GT 已经放弃访问权**；`FScene::Update()` 完成撤场并执行最终 delete，表示 **这组 CPU 侧 Renderer 对象的生命周期结束**；相关 GPU 资源能否复用或释放，还要由最后一个 GPU 消费者对应的 queue / fence / completion 证据裁决。三者之间的窗口不是异常，而是跨线程、跨设备时间线为了保证排队命令、场景索引和 GPU 消费安全而保留的过渡态。调试 use-after-free 时，先问是谁越过了权利边界；调试资源过早复用时，则必须继续追到 03、04 章定义的完成深度。

### 撤场按消费者依赖顺序拆除关系

表面上看，删除似乎只是 add 的逆过程；实际上撤场更像拆一张依赖图。入场时，SceneInfo 逐步成为八叉树、光照交互、静态 mesh 容器、cached command、GPUScene 分配和其他专项系统的共同上游。删除时必须先让这些消费者停止引用，再释放它们共同依赖的 Proxy / SceneInfo。

可以把撤场责任分成四层证据：

| 层次 | 撤销的东西 | 完成证据 | 过早删除的典型后果 |
| --- | --- | --- | --- |
| 组件引用层 | Component 到 Proxy 的 GT 引用 | 组件不再能取得 SceneProxy | GT 后续逻辑误写已移交对象 |
| 场景身份层 | packed arrays、PersistentIndex 映射、type offset、octree element | primitive 不再能被场景索引找到 | visibility 访问空洞或读到交换后的别物体 |
| 派生关系层 | static meshes、cached commands、light interaction、GPUScene / Nanite / 光追等登记 | 各消费者不再保留旧 SceneInfo / Proxy 关系 | 残影、错误 draw、悬空缓存或异步任务访问释放内存 |
| CPU 对象与 RT 关系层 | Proxy、SceneInfo、HitProxy 引用和 RT 管理关系 | 最终 delete / cleanup 完成 | use-after-free、双重释放或清理顺序断言 |
| GPU 消费层 | RHI 已录制工作或 Platform Queue 已接管工作仍引用的原生资源 | 与最后消费者匹配的 fence / completion evidence | 上传区、显存或后端对象被过早复用/释放 |

为什么 `DestroyRenderThreadResources()` 也不能简单理解为“对象已经没了”？因为它只表示 Proxy 子类拥有的 RT 资源开始按契约释放；SceneInfo 仍可能需要作为撤销登记关系的管理对象存在。反过来，组件引用已经断开、Proxy 已经执行 CPU `delete`，也都不代表 GPU 立即停止使用相关原生资源；资源层还要遵守更下游的命令和 fence 生命周期，具体由 03、04 篇继续展开。

一个很实用的调试方法是给“残留”分类：

- **屏幕残留但场景索引已无对象**：优先检查下游缓存、历史 buffer 或 GPU 命令完成边界；
- **场景查询仍能找到对象**：撤场命令或 `FScene::Update()` 的场景身份清理还没完成；
- **只在编辑器选择 / HitProxy 路径崩溃**：检查延迟 cleanup 引用是否仍在；
- **随机在另一个物体上出现旧数据**：检查 packed index 交换后是否有跨帧记录错误地保存了位置而非稳定身份；
- **重建期间偶发访问旧 Proxy**：检查调用方是否把 Component 断引用当成了 Renderer 已删除，或把旧注册生命周期的指针带进新生命周期。

这比只在最终 `delete` 上下断点更有价值：最终 delete 只能告诉你对象何时死，不能告诉你哪一条派生关系没有先撤销。

---

## 用“最后成立状态”统一复盘与调试

复盘生命周期和排查故障不需要两套函数调用链。统一问：**最后一个已经成立、且能拿出证据的状态是什么？** 下一状态没有成立的地方，就是当前最小故障域。

| 状态 | 权威数据与控制者 | 成立证据 | 尚不能推断 |
| --- | --- | --- | --- |
| Component 请求有效 | Component 是游戏侧真相源，GT 决定是否注册 | render state 已创建，bounds 已更新，组件满足入场条件 | Proxy 已存在 |
| Proxy 内容快照有效 | Proxy 保存内容快照；SceneInfo 保存待登记关系 | Proxy、SceneInfo 与 add 载荷已形成 | Renderer 已能查询它 |
| RT 已接管待发布对象 | RT 拥有控制权，`PrimitiveUpdates` 持有记录 | RT 资源与初始空间状态已建立，记录已入队 | 场景数组和索引已更新 |
| 场景版本已发布 | `FScene::Update()` 控制场景级容器 | 稳定身份与当前 packed 位置有效；Proxy、transform、bounds、octree 指向同一版本 | 当前 View 一定选择它 |
| 绘制输入已登记 | SceneInfo 保存静态批次或能提供本帧动态批次 | relevance 正确；目标路径具有相应批次输入 | 已形成 draw 或 GPU 已绘制 |
| 撤场关系已清空 | Renderer 撤销场景身份和派生消费者关系 | 场景查询与缓存消费者不再持有旧关系 | 对象已物理删除、GPU 已完成 |
| CPU 侧 Renderer 生命周期结束 | Renderer 完成对象与管理关系清理 | 最终 delete 或延迟 cleanup 完成 | GPU 已越过最后消费者、原生资源可立即复用 |

源码符号只用于定位转换：注册看 `CreateRenderState_Concurrent`，快照看 `CreateSceneProxy`，统一发布看 `FScene::Update()`，静态输入看 `DrawStaticElements`，撤场看 `RemoveFromScene`。断点要检查是否产生了下一份状态证据，而不只是函数是否来过。

### 完全没出现

按表中前五个状态寻找最后成立点：请求未成立就检查 render state、bounds 与入场条件；快照未成立就检查 mesh、编译状态、RenderData/LOD 与 Proxy 创建条件；RT 接管未成立就检查 SceneInfo、add 载荷与待发布记录；场景版本未成立就检查稳定身份、当前 packed 位置、Proxy、bounds 与 octree 是否描述同一版本；绘制输入未成立就检查 static/dynamic relevance 与对应批次契约。若这些证据都成立，问题已进入 08 FrameInit 的可见工作集或 07 MeshDrawCommand 的命令形成阶段。

### 出现了，但移动不更新

移动问题直接复用“三本账”：Component 是否产生新空间真相；GT 是否捕获同一版 transform、bounds 与 previous transform；`FScene::Update()` 是否把它统一发布到 Proxy、packed transform/bounds 与 octree element。Proxy 已是新矩阵但 packed bounds 仍旧，说明统一发布不完整；packed bounds 已更新但 octree 仍旧，说明空间索引账未同步；三处都正确则继续检查下游工作集或 GPU 消费。若变化已使旧 Proxy 无法诚实回答 mesh、材质或 pass relevance，就必须重建，而不是强行局部修改。

### 删除后残留或崩溃

- 组件引用已断但场景仍能查询：RT 撤场记录或场景身份清理尚未完成。
- 场景身份已清但屏幕仍残留：检查派生缓存、历史 buffer 或下游 GPU 完成边界。
- 只在专项路径崩溃：对应派生消费者仍保存旧关系；按症状选择代表性入口，不必遍历全部子系统。
- 随机串到另一个物体：检查跨帧记录是否错误保存了会被 swap-remove 改写的 `PackedIndex`。
- 重建期间访问旧 Proxy：检查调用方是否把 Component 断引用误当成 Renderer 生命周期结束。
- 最终 delete 附近崩溃：先证明场景身份与派生关系都已撤销；delete 只说明何时死亡，不说明谁没有及时放手。

注册、移动和删除由此共享同一套“最后成立状态”模型，不再维护另一套容易过时的函数清单。

---

## 下一篇怎样接上

到这里，椅子已经从 `UStaticMeshComponent` 变成了 Renderer 侧稳定的场景数据：Proxy、SceneInfo、紧凑数组、空间八叉树和静态网格批次。它可以被可见性系统查询，也能作为 GPUScene 和 MeshDrawCommand 的上游输入。

后续章节会沿这条数据继续走：

| 后续篇 | 接着讲什么 |
| --- | --- |
| 03 ThreadModel | 本篇的 add / update / remove 命令怎样跨线程投递、执行、同步 |
| 06 GPUScene | `FScene` 里的 primitive / instance 数据怎样上传成 shader 可读的 buffer |
| 07 MeshDrawCommand | 本篇收集出的 `FMeshBatch` 怎样按 MeshPass 变成 pass-specific `FMeshDrawCommand` 配方与本帧可见工作项 |
| 08 FrameInit | `FScene` 的 bounds、flags、八叉树和缓存数据怎样在帧初始化阶段被当前 View 转成可见性与 pass relevance 工作集 |

本篇的核心结论：

> **UE 不让 Render Thread 直接读组件。Game Thread 创建 Proxy 快照和 SceneInfo 登记卡，Render Thread 通过 `FScene::Update()` 批量吸收 add / update / delete，并发布正式成立的 Renderer 场景状态；后续可见性和 draw 收集，只消费这些渲染侧数据。**
