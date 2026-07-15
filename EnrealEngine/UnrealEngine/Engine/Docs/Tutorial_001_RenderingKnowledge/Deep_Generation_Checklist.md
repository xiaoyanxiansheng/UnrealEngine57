# Tutorial_001 百科条目扩写流程

本文件用于 001 百科主库的持续扩写。目标是保留并沉淀知识面，不为了文章变短而删减事实、观点或对照。

---

## 扩写前检查

```text
[ ] 在 TOPIC_INDEX.md 中确认目标 slug 和主位置
[ ] 确认目标文件已有稳定 HTML 锚点，slug 使用 topic-xxx 命名
[ ] grep UE5.7 源码确认关键路径、强符号/搜索 token 存在
[ ] 查 SOURCE_INDEX.md，避免重复建立源码入口
[ ] 查 CPP_Knowledge_Index.md，避免重复解释同一 C++ 概念
[ ] 查 EXPANSION_LOG.md，确认是否已有待补观点或来源
```

如果主题还没有 slug，先补 `TOPIC_INDEX.md` 和主文档锚点，再扩写正文。

---

## 条目结构

主条目建议覆盖这些维度；不要求每次一次性写完，但缺口要进入 `EXPANSION_LOG.md`。

```markdown
# X.Y 主题标题

## 本章结论
## 背景：为什么需要它
## UE 架构位置
## 核心流程
## 源码锚点
## 调试与验证
## 性能与成本
## 常见误区
## Unity 对照
## 相关条目
```

重复内容不直接删除。指定一个主条目承载完整解释，其他章节保留摘要和交叉链接。

---

## 源码事实要求

源码解读必须区分事实层级：

| 类型 | 写法要求 |
|------|----------|
| 源码事实 | 写成“路径 + 强符号/搜索 token + 事实类型”，路径和 token 必须在 UE5.7 源码树中验证存在 |
| 实测结论 | 写明测试环境、命令、样例或限制条件 |
| 推论 | 明确说明“推论”或“基于源码结构可推断” |
| 类比 | 标为 Unity/其他引擎对照，不冒充 UE 源码事实 |

不要编造或新增固定行号。行号容易随版本漂移，源码锚点优先写路径和强符号；调试断点写“搜索符号后定位”，不写固定行号范围。

---

## 多智能体分工

- 主控 Codex：合并公共文件，维护 `README.md`、`TOPIC_INDEX.md`、`SOURCE_INDEX.md`、`EXPANSION_LOG.md` 和校验脚本。
- Phase agent：只扩写指定主题正文，默认追加内容，不改 slug，不重编号。
- Claude：做百科编辑，优化标题层级、顺序、可检索性和重复归位，不删知识点。
- Codex 终审：复核源码事实、CVar、线程/RDG/RHI 生命周期、性能断言，并运行校验脚本。

---

## 扩写后必须执行

```text
[ ] 新增或移动主条目时，更新 TOPIC_INDEX.md
[ ] 新增源码入口时，更新 SOURCE_INDEX.md，并确认路径和符号 token 真实存在
[ ] 有未补完观点时，追加 EXPANSION_LOG.md
[ ] 检查正文中新增的本地链接
[ ] 运行 Tools/Check-Tutorial001.ps1
```

验收命令：

```powershell
powershell -ExecutionPolicy Bypass -File Engine\Docs\Tutorial_001_RenderingKnowledge\Tools\Check-Tutorial001.ps1
```

脚本通过是进入下一轮扩写的最低条件；高风险事实仍需要 Codex 做源码复核。
