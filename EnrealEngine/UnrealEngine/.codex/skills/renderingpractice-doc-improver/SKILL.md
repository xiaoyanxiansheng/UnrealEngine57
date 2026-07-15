---
name: renderingpractice-doc-improver
description: "Improve, expand, and correct UE 5.7 RenderingPractice tutorial prose in Engine/Docs/Tutorial_003_RenderingPractice. Use when the user asks to 完善, 修正, 补充解释, 优化教程, 深入解释, 插入原理, audit thin operation instructions, correct technical facts, or apply a newly discovered teaching rule to one or more practice chapters. The skill verifies UE-specific claims, inserts explanation near the operation it supports, and preserves the continuous project case."
---

# RenderingPractice Doc Improver

Use this skill to turn an executable instruction into an understandable UE lesson. Insert the missing teaching directly where the reader needs it, while preserving the chapter's practical order and the course project's state continuity.

The target reader is an experienced programmer and Unity TA who understands general rendering, profiling, assets, and controlled experiments, but is new to Unreal Engine terminology, editor state, defaults, ownership boundaries, and project workflows.

Comprehension outranks brevity. There is no word-count, line-count, section-count, or chapter-length target. Add as much explanation as performs a real teaching job. Do not add repetition, generic background, or source trivia merely to make the chapter longer.

Do not claim that a fixed checklist makes the explanation complete. The prompts in this skill are discovery tools and minimum safeguards, not a ceiling. Follow the causal dependencies of the actual topic. State the audited scope and any remaining unverified behavior instead of claiming perfection.

## Required Reference

Read [references/explanation-expansion.md](references/explanation-expansion.md) completely before analyzing or editing a tutorial. Apply it as an open-ended reasoning method, not as a form to fill mechanically.

## Scope Modes

Select scope from the user's real request:

- **Local insertion**: improve the named paragraph or operation, then scan the whole target chapter for the same underlying defect class.
- **Chapter-wide improvement**: inspect every load-bearing concept, operation, setting, value, and verification path in the chapter; insert all high-value missing explanation.
- **Multi-chapter improvement**: improve each chapter independently, then run a cross-chapter state and terminology review.
- **Fact correction**: verify the relevant UE 5.7 behavior first, correct the prose, and scan for dependent claims that become stale.
- **Analysis only**: diagnose and propose insertions without editing when the user explicitly asks to analyze first.

Never reduce a broad improvement request to the user's one example. Treat an example as evidence of an underlying teaching defect, generalize the defect, and scan the requested scope for other manifestations.

## Required Context

Work in:

```text
D:\Unreal\EnrealEngine\UnrealEngine
Engine/Docs/Tutorial_003_RenderingPractice/
```

Before editing:

1. Inspect `git status` and preserve unrelated user changes.
2. Read the applicable workspace `AGENTS.md` rules.
3. Read completely:
   - `README.md`
   - `CURRICULUM.md`
   - `PROJECT_STATE.md`
   - `_Authoring/AUTHORING_GUIDE.md`
   - `_Authoring/TEACHING_EXPLANATION_STANDARD.md`
   - `_Authoring/TEACHING_ARCHITECTURE.md`
   - `_Authoring/CASE_CONTINUITY.md`
   - the complete target chapter
4. Read prior chapters whose completed state, terminology, or asset contract the target chapter consumes.
5. Read the next chapter when the edit changes a value, asset, map, setting, or promise that the next chapter depends on.
6. Inventory any user-provided original, reference, attachment, or external source before reconstruction. Do not silently ignore candidate material.

Use other chapters for continuity, not as length or prose-quality benchmarks.

## Improvement Workflow

### 1. Reconstruct the reader's path

Identify what the reader knows on entry, what concrete result they must produce, what new UE-specific knowledge carries the chapter, and what state the next chapter expects.

Separate:

- visible instructions;
- the mental model needed to understand them;
- persistent state changed by them;
- evidence that the change actually worked.

### 2. Find thin teaching surfaces

Inspect the complete requested scope. Look for commands, numbered steps, parameter tables, defaults, warnings, recovery advice, diagrams, and conclusions that rely on unstated UE knowledge.

Treat an item as load-bearing when misunderstanding it can prevent progress, corrupt persistent state, invalidate an observation, change a rendering/build/platform path, create an asset-lifecycle problem, or teach a false mental model.

Do not assume an item is unimportant because it looks like ordinary UI work. Saving an Asset versus a Level, applying an Override, accepting a modeling tool, reimporting a source file, changing an RHI, or hiding an Actor can all cross different ownership and persistence boundaries.

### 3. Expand the explanation

For each thin load-bearing item, follow the topic's causal structure using the required reference. At minimum, make the prose answer what the reader is manipulating, why it exists, why this action or value is chosen now, how to perform it, what changes, where the state persists, what consumes it next, what alternatives mean, what it costs, and how to prove it worked.

Continue beyond those prompts whenever the topic introduces additional dependencies, ambiguous terms, interacting settings, version/platform conditions, lifecycle boundaries, or likely Unity false equivalences.

Explain related settings individually when they perform different jobs, but do not stop after describing their pairwise relationship. Follow every load-bearing dependency required to understand the actual system.

### 4. Verify before sharpening facts

Use the most direct available authority:

1. reproducible UE 5.7 editor behavior;
2. UE 5.7 source and configuration;
3. Epic official documentation that matches the version and platform;
4. already verified RenderingDeep theory;
5. clearly labeled design analysis.

Use `rg` to locate source. Distinguish:

- verified hard requirement;
- UE engineering choice;
- course baseline;
- platform, hardware, configuration, or content-dependent behavior;
- unverified practical behavior awaiting editor execution.

Do not turn a source initializer, one platform flag, one import preset, or one observed run into a universal claim. Source evidence anchors confidence; it must be translated into reader-facing consequences rather than pasted as a source walkthrough.

### 5. Insert instead of append-dumping

Place explanation next to the operation, setting, or observation it supports. Prefer this local rhythm:

```text
reader problem -> mental model -> exact operation -> state change -> verification -> consequence
```

Use a table only after the prose establishes the category and relationships. A table may compare options or summarize a contract; it must not replace first teaching.

Preserve the chapter's real production order. Do not move all explanations into a distant theory section or collect all reasons at the end.

### 6. Repair dependent material

After an insertion or correction, scan for:

- the same defect class elsewhere in the chapter;
- stale summaries, checklists, warnings, and completion criteria;
- changed values or contracts in adjacent chapters;
- inconsistent object names, dimensions, paths, ownership, or lifecycle claims;
- a recovery path that contradicts the intended final state.

Do not fix only the sentence named by the user when the same misunderstanding remains elsewhere.

### 7. Re-read as a learner

Re-read the changed chapter top to bottom. Confirm the reader can:

- execute without external searching for basic UE-specific steps;
- explain why each load-bearing action is necessary;
- predict what changes and what does not;
- distinguish course choice from engine requirement;
- verify success using observable evidence;
- recover from the common failure without destroying valid state;
- carry the resulting project state into the next chapter.

If any of these still depends on unstated UE knowledge, continue expanding or report the precise remaining uncertainty.

## User Feedback Must Improve the Skill

When the user identifies a missing explanation or bad teaching pattern:

1. Name the broader defect without overfitting to the example.
2. Check whether this skill and its reference already force a meaningful response.
3. If not, strengthen the reusable rule before editing the article.
4. Scan the full requested scope for the same class.
5. Report what was generalized, what was inserted, and what remains unverified.

Do not promise a perfect one-pass result. Treat the skill as a maintained teaching system: incorporate concrete feedback, improve the general method, and reapply it.

## Editing Boundaries

- Edit tutorial prose and directly dependent non-status guidance only.
- Do not mark chapters complete, change public status, or update completion logs unless the user explicitly requests final acceptance.
- Preserve the continuous Room -> Building -> District -> World case.
- Do not create unrelated Labs, assets, source changes, or generated project data.
- Do not stage or commit unless explicitly requested.

## Verification

After editing:

1. Inspect the scoped diff.
2. Re-read every changed section in context.
3. Search for stale values and contradictory claims across the affected chapters.
4. Re-check all new or sharpened high-risk facts in UE 5.7 source or an authoritative observation.
5. Distinguish static verification from hands-on editor verification. Menu paths, importer layout, restart behavior, visual results, and asset outputs remain pending until executed in UE 5.7.
6. Confirm only intended files changed.

Report:

- files changed;
- explanation classes expanded;
- facts corrected and evidence used;
- cross-chapter contracts repaired;
- hands-on checks still required;
- status files intentionally untouched.
