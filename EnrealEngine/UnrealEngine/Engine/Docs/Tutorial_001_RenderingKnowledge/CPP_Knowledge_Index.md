# C++知识点索引

**用途**：避免Deep文档重复讲解C++知识

> **注意**：C++知识应**融入主题讲解**，不是独立章节。
> 本索引用于：检查某知识点是否已在其他文档讲解过，避免重复。

---

## 📋 使用规则

1. **写新Deep前**：查本索引，看C++特性是否已讲解
2. **如果已讲解**：简短引用（"详见X.X章节"），不重复展开
3. **如果未讲解**：在相关主题中自然引出讲解，然后更新本索引

---

## 📊 C++知识点分配表

### C++核心语法

| 特性 | 首次讲解 | 引用 |
|------|----------|------|
| 右值引用/移动语义 | 1.1 RenderLoop | 2.1, 2.5 |
| 完美转发 | 1.1 RenderLoop | 2.5 |
| Lambda捕获 | 1.1 RenderLoop | 1.4, 2.1 |
| 模板元编程(SFINAE) | 2.1 DataFlow | 2.5, 3.3 |
| constexpr | 2.3 GBuffer | 4.2 |
| 位运算/位域 | 2.3 GBuffer | 3.3, 4.2 |
| 内存对齐(alignas) | 2.1 DataFlow | 2.3, 3.3 |
| 原子操作(TAtomic) | 2.1 DataFlow | 1.4 |
| 类型萃取 | 2.5 RenderGraph | 3.3 |
| CRTP | 4.1 MDC | - |
| 表达式模板(Expression Templates) | 4.2 MaterialSystem | - |
| 模板策略模式(constexpr if分派) | 4.3 Shadow | - |
| Halton低差异序列 | 6.1 PostProcess | - |
| TInlineAllocator栈内存优化 | 6.1 PostProcess | - |

### UE宏系统

| 宏 | 首次讲解 |
|----|----------|
| UCLASS/GENERATED_BODY | 1.3 Init |
| ENQUEUE_RENDER_COMMAND | 1.1 RenderLoop |
| DECLARE_DELEGATE | 5.1 Console |
| RDG宏 | 2.5 RenderGraph |

### UE并发

| 特性 | 首次讲解 |
|------|----------|
| 三线程模型 | 1.1 RenderLoop |
| Task Graph | 1.4 ThreadModel |
| Lock-Free | 1.4 ThreadModel |
| FRWLock | 2.1 DataFlow |

### UE容器/指针

| 类型 | 首次讲解 |
|------|----------|
| TSharedPtr | 1.1 RenderLoop |
| TArray | 2.1 DataFlow |
| TMap | 5.1 Console |
| TSparseArray | 3.3 GPUScene |

---

## 📝 引用格式

```markdown
**关于右值引用**：详见[1.1 RenderLoop](./Phase01_Fundamentals/1.1_RenderLoop_Deep.md)
简要：`Type&&`、`MoveTemp()`用于避免拷贝
```

---

**最后更新**: 2026-03-30

---

## 📝 更新日志

- 2026-03-30: 添加 Halton低差异序列、TInlineAllocator栈内存优化，首次讲解于 6.1 PostProcess
- 2026-03-11: 确认 DECLARE_DELEGATE、TMap 在 5.1 Console 首次讲解（已在索引中）
- 2026-03-10: 添加 模板策略模式(constexpr if分派)，首次讲解于 4.3 Shadow
- 2026-03-10: 添加 表达式模板(Expression Templates)，首次讲解于 4.2 MaterialSystem
- 2026-02-03: 目录重构，编号更新为新的学习路径顺序
- 2026-02-03: 确认 1.4 ThreadModel_Deep 首次讲解 Task Graph、Lock-Free、Lambda捕获机制
