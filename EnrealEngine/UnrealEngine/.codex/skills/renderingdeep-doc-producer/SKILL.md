---
name: renderingdeep-doc-producer
description: "Create theory-first, flow-driven UE5.7 RenderingDeep teaching chapters in Codex. Use when the user asks to 生产, 初稿, 生成章节, 重写章节, create a Codex draft, prepare a chapter for Claude, or decide whether a draft is ready for teaching optimization. The chapter must teach UE rendering framework theory and clear process models; source is used for fact verification and debug anchors, not as the teaching path."
---

# RenderingDeep Doc Producer

Use this skill for Gate 1 of the RenderingDeep pipeline: Codex writes the theory-first technical draft, verifies facts against UE5.7 source, and creates the chapter-local coverage matrix.

The target reader understands Unity rendering, SRP-style frame organization, passes, command buffers, GPU resources, and render targets. They do not understand UE's rendering object model, thread boundaries, lifetime rules, naming conventions, or Renderer data structures. Do not reteach generic graphics. Teach the UE framework model in enough detail that the reader can reason about the pipeline without reading source.

The product is not a source walkthrough. Source is an internal verifier and a debugger map. A chapter is good only if the reader still understands the model after removing code snippets, function traces, file paths, and field lists.

The pipeline has three gates:

1. Codex produces a theory-first, source-verified draft and `<chapter-stem>_CoverageMatrix.md`.
2. Claude Desktop runs the repo-local skill at `.claude/skills/renderingdeep-doc-teaching-editor/SKILL.md` (`renderingdeep-doc-teaching-editor`) for teaching structure and prose only. Claude must not add new technical facts.
3. Codex runs `$renderingdeep-doc-reviewer` for final theory-model regression, fact regression, and completion.

A chapter is never complete at Gate 1.

## Core Standard

"Detailed" means detailed at the framework level:

- what UE problem this layer solves;
- why this layer exists instead of a simpler Unity-like model;
- what objects or resources participate and what responsibility each has;
- what changes over time, as a state/process flow;
- who owns the data before and after each transition;
- which thread or system is allowed to mutate the state;
- what would break if the order, ownership, or boundary changed;
- how a rendering bug would be debugged conceptually.

"Detailed" does not mean more source lines, more function names, longer call stacks, larger field tables, or paragraphs that say only "the source calls X then Y".

Assume the reader can program and understands general rendering ideas but is new to Unreal Engine. Do not assume familiarity with UE classes, renderer stages, resource conventions, editor terminology, defaults, ownership rules, or completion semantics.

Comprehension outranks brevity. There is no target word count, line count, or chapter length. Do not shorten a chapter to resemble another chapter or to make the structure look tidy. Expand a concept until the reader can explain its purpose, data, owner, control transition, lifetime, conditions, boundary, worked example, and debug consequence without relying on unstated UE knowledge.

When concise wording and complete understanding conflict, choose complete understanding. For every load-bearing concept, explain all applicable parts: plain definition, problem solved, design reason, concrete data shape, producer/owner, consumer, control or state transition, lifetime, enabling conditions, failure conditions, adjacent-category boundary, worked example, observable evidence, and debugging consequence. Do not skip an applicable part merely to reduce length. Split dense explanations into staged subsections and local recaps instead of compressing them into summary sentences.

For every load-bearing UE design choice, add a design-rationale analysis:

- Why this split or mechanism exists in the first place.
- What would become incorrect, ambiguous, expensive, serial, memory-heavy, difficult to stream, or difficult to debug if UE did not use it.
- Which parts are hard constraints imposed by GPU execution, data dependencies, API contracts, or lifetime safety, and which parts are UE engineering choices.
- What plausible alternative designs exist.
- What each alternative would improve and what it would worsen.
- Under which project, platform, content, or workload conditions an alternative could be better.
- Why UE chooses the current tradeoff in the path being taught.

Do not present an alternative as better in the abstract. State the objective and conditions that make it better. Verify implementation-specific comparisons in source; when the comparison is architectural rather than source-specific, label it as a design analysis instead of an observed UE fact.

Repeated material is not automatically duplication. An introduction, mechanism explanation, worked-case application, and debugging recap may mention the same concept while performing different teaching jobs. Merge only passages that truly perform the same teaching task and add no new condition, state change, example, or diagnostic value.

## Chapter-Independent Concept Quality Contract

A concept is deep only when the reader can use it to reason: what it contains, why UE splits it here, who owns or advances it, where it sits in the mainline, and what to inspect when it stalls.

Do not use another RenderingDeep chapter as the quality benchmark. Prior chapters may define terminology, ownership handoffs, lifecycle contracts, or facts that the new chapter must respect, but their prose structure, length, status, sidecars, and prior PASS decisions are not acceptance evidence. Evaluate every chapter against this contract directly.

Before drafting, build a concept load map:

- list the first-class concepts introduced or substantially deepened in this chapter;
- mark which concepts are new to a Unity rendering reader;
- mark any terminology-dense cluster where several scheduling, ownership, lifecycle, resource, or process terms appear together;
- choose which concepts need a worked case because a definition alone would remain abstract.

For every first-class concept inside the chapter boundary, create a concept passport in the planning notes:

```text
Concept:
Plain role:
Why UE needs it:
Why UE designs it this way:
What breaks or becomes costly without this split:
Technical category:
Adjacent concepts it can be confused with:
Hard boundary or design choice:
Could this be implemented with another mechanism:
Alternative mechanisms and tradeoffs:
When an alternative could be better:
Why UE chooses this mechanism here:
Input / output / contained data:
Scope / lifetime:
Binding or publication path:
Owner or advancing system:
Mainline position:
Failure or debug question:
Worked case:
```

The worked case can be short, but it must bind the concept to a concrete state change, data shape, owner, design reason, and debugging question. If no suitable worked case exists, the concept usually is not explained deeply enough yet.

### Conceptual Simplification Safety

Before drafting, identify any concept that sits near another technical category: ID vs offset, buffer vs texture, uniform vs structured buffer, SRV vs resource, record vs payload, view context vs scene context, persistent identity vs draw-local lookup, CPU ownership vs GPU visibility, or similar boundaries.

For each such cluster:

- first state the broader category accurately, then narrow to the chapter-specific path;
- distinguish hard constraints from UE engineering choices;
- explain why UE uses this mechanism here, not only what becomes true after that choice;
- note whether an adjacent mechanism could technically replace it, and what semantic, lifetime, binding, performance, or debugging cost would change;
- state what failure, coupling, synchronization, memory, streaming, or maintenance cost appears when the split is removed;
- compare alternatives by explicit objective and operating conditions rather than declaring one universally better;
- avoid turning a local teaching shortcut into a global fact.

Statements that use words such as "only", "must", "cannot", "always", "never", "the single", "essentially", or "is just" are high-risk. Keep them only when they are verified hard facts. If the statement is chapter-local shorthand, scope it explicitly, for example "in this GPUScene path" or "for this pass boundary".

## Teaching Surface Rules

Keep verification and teaching on separate surfaces:

- The chapter body teaches the UE framework model. Do not create repeated "source anchors" / `源码锚点` blocks in chapter sections.
- File paths, line numbers, dense symbol lists, and verification records belong only in the coverage matrix, Teaching Edit Report, or SOURCE_INDEX maintenance. Do not put them in the chapter body.
- The chapter may name already-explained UE symbols as conceptual or debug waypoints, but each waypoint must explain what state transition it validates and why the reader should check it.
- If verification records consume space that should explain a concept, move them out of the chapter body and expand the concept.

Every important new name must pass a concept-introduction gate before it is used as an explanation. For a UE class, system, scheme, queue, index, cache, or lifecycle stage, define:

- plain-language role in the UE rendering model;
- owner thread/module and lifetime boundary;
- inputs and outputs or what data it holds;
- why UE needs it instead of a simpler Unity-like model;
- what breaks or how to debug when this concept is wrong.

Do not assume a reader understands a new symbol because it was listed in a table. A table can summarize differences only after the prose establishes the role and process.

### Terminology-Dense Chapter Rule

If a chapter introduces many new scheduling, lifecycle, ownership, or process terms at once, first create a short "new terms stabilization" layer before the main flow. The purpose is to prevent readers from reverse-engineering a mental model from UE names such as `task`, `pipe`, `queue`, `command list`, `submit`, `fence`, `named thread`, `local queue`, or similar terms.

For each first-class new term in that layer, explain:

- what it is in plain language;
- why UE needs it;
- what it contains or receives as input;
- who owns or advances it;
- where it sits in the chapter's state/control flow;
- what failure or debug question it answers.

Do not use a table as the first explanation. Use prose to establish the role and reason, then use a compact table only to compare or summarize. Avoid making the chapter primarily "not X, but Y"; define the positive role and design purpose first, then state boundaries only when needed for safety or debugging.

If the reader still cannot attach a new term to a concrete state change after the first-use explanation, add a short worked case near that term. The case should show a real object or command moving through the concept: what data is inside, who owns or advances it, why UE separates it from the neighboring concept, and what debugger question it answers. Use cases to carry the mental model, not to narrate source execution. For example, distinguish `task` with two captured Primitive movement snapshots, `queue` and `pipe` with a worker batch of render commands, `command list` with scene update versus later draw recording, `submit` with one immediate list plus additional lists, and `fence` with resource lifetime depth.

## Short Prompts

- `生产 <编号或文件名>` / `初稿 <编号或文件名>` / `生成 <编号或文件名>` -> create or replace the Codex theory-first draft.
- `交给Claude` / `交给 Claude` -> provide the Claude Desktop handoff prompt for the current chapter.
- `能交给Claude吗 <编号或文件名>` -> run the production review and decide whether the draft is ready for Claude.
- `终审 <编号或文件名>` / `验收 <编号或文件名>` / `收尾 <编号或文件名>` / `完成检查 <编号或文件名>` -> use `$renderingdeep-doc-reviewer`.

If the prompt gives only a chapter number, map it through `OUTLINE.md`.

## Required Context

Work in:

```text
D:\Unreal\EnrealEngine\UnrealEngine
Engine/Docs/Tutorial_002_RenderingDeep/
```

Before drafting or replacing a chapter, discover the complete source-material set.

### Source-Material Discovery

- Inspect the current user message, prior task messages, file mentions, attachments, pasted-text files, absolute paths, and explicitly named original/reference/current versions.
- Treat external attachments as first-class materials. Do not conclude that no original exists merely because repository `origin.md` lacks the chapter.
- Inventory every candidate with its path, user-assigned role, successful-read status, and Unicode-aware physical-line count before planning.
- If the user says a source exists but it cannot be read, stop and report the missing material instead of generating from an incomplete set.
- Pass the complete inventory to every delegated auditor or writer.
- If a new material appears after drafting or review, invalidate the prior information-value judgment and repeat planning, drafting review, and sidecar generation against the complete set.

Then read completely:

1. `Engine/Docs/Tutorial_002_RenderingDeep/GENERATION_GUIDE.md`
2. `Engine/Docs/Tutorial_002_RenderingDeep/OUTLINE.md`
3. `Engine/Docs/Tutorial_002_RenderingDeep/SOURCE_INDEX.md`

Then read prior completed chapters that define terms, ownership boundaries, or lifecycle stages used by the requested chapter. If a required file is missing, stop and report the missing path.

Use `Engine/Source` to verify technical claims. Do not use source order as the chapter outline.

## Parallel Workflow Guardrails

If parallel agents or workers are used, workers may write only their assigned chapter file and chapter-local sidecars. Public files such as `OUTLINE.md`, `SOURCE_INDEX.md`, and `GENERATION_GUIDE.md` must be written only by the main agent, last, after all chapter-local work and reviews are reconciled.

If a worker report, prior report, or sidecar is missing, vague, stale, or inconsistent, do not use it to narrow fact verification. Re-verify every A-class and high-risk fact for the affected scope directly in `Engine/Source`.

When a chapter is regenerated or restructured back to Gate 1, any existing `<chapter-stem>_TeachingEditReport.md` from an earlier accepted version is stale. Do not let it imply completion. Either update that sidecar with a short superseded notice or state in the new CoverageMatrix and handoff that the old report is historical only and Claude must refresh it.

## Coverage Matrix

Before drafting, create a chapter-local matrix from the `OUTLINE.md` core question and any unavoidable subquestion discovered during fact verification:

```text
Question:
Reader misconception or pain point:
Why UE needs this concept:
Framework responsibility:
Conceptual model:
Process flow / state transitions:
Data, control, or lifetime transition:
Ownership / thread boundary:
Unity bridge:
Failure mode / debug reasoning:
Worked case coverage:
Source verification status:
Coverage status: deep / partial / missing
```

Rules:

- Persist the final reviewed matrix next to the chapter as `<chapter-stem>_CoverageMatrix.md`.
- `deep` means the chapter explains the theory, process, boundary, reader value, and debug reasoning without depending on source listings.
- `deep` for a row with first-class new concepts also requires worked case coverage or an explicit reason why the mainline example already carries that concept.
- `partial` means the draft names the topic, gives a route map, cites source, lists implementation details, or defines a concept without a usable worked case when one is needed. `partial` is not pass.
- If any row is `partial` or `missing`, keep drafting. Do not hand the chapter to Claude.
- Do not use word count as the target. Use the matrix to decide whether explanation is sufficient.

## Production Planning

Create this internal plan before writing:

```text
Chapter:
Core theory question:
Teaching purpose:
Reader already knows:
UE-specific gap:
Mainline process:
Throughline example:
Conceptual states:
Ownership/thread boundaries:
Unity bridge:
Reader misconceptions:
Must explain in detail:
New terms / classes / schemes to introduce:
For each new term, required first-use explanation:
Concept load map:
Concept passports:
Simplification boundary checks:
Worked cases required:
Verification-record placement plan:
Only mention:
Do not cover:
High-risk facts to verify:
Candidate source materials and roles:
Material inventory verified complete: yes / no
Prior chapters to rely on:
Later chapters to defer:
```

## Drafting Rules

- Start from the reader's misconception or missing mental model, not from the source file or old chapter structure.
- Organize every major section as: reader question -> UE design reason -> conceptual model -> process flow -> consequence / debug reasoning.
- Use clear process diagrams, state flows, ownership maps, and compact responsibility tables. These are conceptual flows, not call stacks.
- Do not put function lists, class lists, field tables, source snippets, or call stacks at the start of a chapter or major section.
- Do not write paragraphs whose logic is only "the source writes this field" or "this function calls that function." Translate each fact into the framework consequence it supports.
- Use UE symbol names only when they help identify a concept or a debug waypoint. Introduce each important UE term through the concept-introduction gate; a short gloss is enough only for secondary names.
- For every first-class mechanism inside the chapter boundary, explain purpose, inputs, outputs, timing, owner, lifetime boundary, failure mode, and debug route.
- For every first-class mechanism that can be confused with an adjacent mechanism, preserve the category boundary: define the broad resource/input/lifecycle category first, then state the subset this chapter uses. Do not say "shader only consumes X" when the accurate model is "this chapter's path uses X".
- When a mechanism is a design choice rather than a hard impossibility, explain the tradeoff. For example, small read-only scoped context can technically be stored in a larger indexed buffer, but uniform/constant parameters may be the better semantic and binding boundary for the current scope.
- Keep one continuous data/control/lifecycle mainline. Avoid switching examples unless the chapter boundary requires it.
- Integrate Unity comparisons where they reduce UE-specific confusion. State ownership and lifetime differences; do not imply false one-to-one mappings.
- Keep verification records out of the chapter body. Use the coverage matrix for source verification; chapter-body debug paths may use already-explained symbols only, not file paths, line numbers, or dense source lists.
- Avoid large code snippets. Include a snippet only when the user explicitly asks or when a tiny excerpt clarifies a concept already explained in prose.
- Explain only this chapter's boundary. Later-chapter material should be a pointer, not a mini-chapter.
- Do not pad length. Do not over-compress theory. If a reader cannot answer why / when / where is the data now / who owns this / how to debug it, restore the explanation.
- For terminology-dense sections, keep the throughline example visible while introducing names. Each stage should say: previous state -> why this stage exists -> what the new term means -> who owns it now -> what debug question it resolves.
- For terminology-dense sections, include enough local worked cases that the names become usable mental models rather than dictionary definitions. A good case answers "what is inside this thing", "why UE split it here", "who advances it", and "what would I inspect when it stalls". Do not remove these cases merely to reduce length; rewrite them only if they stop serving the mainline.
- Avoid four-or-more unexplained UE names in one sentence or table row. If several names must appear together, introduce the shared mental model first and delay exact symbols until the role is clear.

## Source Verification Rules

Use source rigorously, but keep that work behind the teaching:

- Verify all mainline and high-risk facts directly in `Engine/Source` with `rg` first.
- Prefer anchors as file plus symbol name; line numbers are auxiliary.
- Treat `SOURCE_INDEX.md` as a search entry point, not proof.
- If a fact cannot be verified, do not write it as truth. Either remove it, narrow it, or flag it as unresolved.
- Always verify UE5.7 APIs, ownership/lifetime rules, thread/RHI/RDG boundaries, creation/destruction order, CVar defaults, platform branches, and diagram edges crossing systems.

## Mandatory Production Review

Before Claude handoff:

1. Re-read the chapter top to bottom.
2. Rebuild the coverage matrix from `OUTLINE.md` and the final draft, then persist it to `<chapter-stem>_CoverageMatrix.md`.
3. Mark every row `deep`, `partial`, or `missing`.
4. Re-run targeted source verification for every A-class or high-risk claim.
5. Run these reviews:
   - Coverage matrix: every core question is `deep`.
   - Mainline: the chapter can be summarized in 5-10 conceptual sentences.
   - Theory depth: the chapter explains why the mechanism exists and what problem UE solves.
   - Concept introduction: every important new name/class/scheme/flow is explained before it carries the argument.
   - Simplification safety: broad technical categories are accurate before chapter-specific narrowing; "only/must/cannot/always" statements are either verified hard facts or explicitly scoped as local shorthand.
   - Terminology density: if many new names appear in one chapter, the draft has a first-use stabilization layer and does not force the reader to infer concepts from symbol lists.
   - Worked cases: every abstract or terminology-dense first-class concept has a case that shows data, ownership, state transition, design reason, and debugger question; missing cases make the relevant matrix row `partial`.
   - Process clarity: the chapter teaches state/data/control flow, not a call stack.
   - Source restraint: the model stands without source snippets, function traces, source paths, or field tables; repeated verification/source-anchor blocks are absent from the body.
   - Reader value: the reader can explain why it matters, where it sits, and how to debug failure.
   - Unity bridge: UE-specific concepts are bridged without reducing depth.
   - Boundary: later-chapter details are only pointers.
   - Source facts: high-risk facts are verified and unresolved facts are absent.
   - Claude readiness: remaining work is teaching/prose restructuring, not fact research or missing theory.

If any review fails, revise and repeat. Do not call the draft ready.

## Gate 1 Output

Leave the chapter in a clear "needs Claude teaching edit" state. Do not mark it complete.

Final response after Gate 1 must include:

```text
Coverage Matrix:
- <core question>: deep / partial / missing + reason

Codex Production Review:
- Mainline: pass/fail
- Theory depth: pass/fail
- Concept introduction: pass/fail
- Simplification safety: pass/fail
- Worked cases: pass/fail
- Process clarity: pass/fail
- Source restraint: pass/fail
- Reader value: pass/fail
- Unity bridge: pass/fail
- Boundary: pass/fail
- Source facts: pass/fail + high-risk checks performed
- Claude readiness: ready / not ready
```

Provide a short handoff:

```text
Claude Desktop next step: 优化 <编号或文件名>
```

Mention that Claude must use `.claude/skills/renderingdeep-doc-teaching-editor/SKILL.md`, restructure for theory-first teaching clarity, preserve technical meaning, avoid adding facts, refresh the sidecar Teaching Edit Report, and ignore any older report that claims completion for a superseded draft.

Do not update `OUTLINE.md`, `SOURCE_INDEX.md`, or completion status at Gate 1 unless the user explicitly asks for separate maintenance.
