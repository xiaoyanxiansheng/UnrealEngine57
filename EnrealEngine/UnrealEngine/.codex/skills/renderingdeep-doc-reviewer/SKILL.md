---
name: renderingdeep-doc-reviewer
description: "Run final theory-model, process-clarity, information-value, and fact regression for UE5.7 RenderingDeep chapters, plus chapter-wide teaching optimization and chapter comparisons. Use when the user asks to 终审, 验收, 收尾, 完成检查, 审核, 质量复审, 教学质量, 优化建议, 全文优化, 章节优化, 正文优化, 比较章节, 哪个质量更好, mark complete, review teaching edits, merge verified debug anchors, or decide whether a chapter can be accepted."
---

# RenderingDeep Doc Reviewer

Use this skill for Gate 3 of the RenderingDeep pipeline and for teaching-quality review of RenderingDeep chapters. Gate 3 decides whether a chapter can be accepted after Claude's teaching edit. Teaching-quality review decides how the chapter can become clearer even if it already passes.

The acceptance target is a detailed UE rendering framework lesson, not source study. The final chapter must teach theory foundations, clear process flow, responsibility boundaries, and reader debugging value. Source verification is mandatory for facts, but source listings must not carry the lesson.

Assume the reader has programming or general rendering experience but is new to Unreal Engine. UE-specific names, stages, ownership rules, resource states, defaults, and completion depths require explicit first-use teaching.

Comprehension outranks brevity. Do not impose a word-count, line-count, section-count, or chapter-length target. A longer chapter is acceptable when the added prose performs a real teaching job; a shorter chapter is acceptable only after information-value review proves that no unique principle, condition, exception, case, data transition, or debug judgment was lost.

When concise wording and complete understanding conflict, require the complete explanation. A load-bearing concept is not accepted until the reader can answer: what it is, why UE needs it, what concrete data it carries, who produces and owns it, who consumes it, what state/control changes, how long it remains valid, which conditions enable or invalidate it, how it differs from adjacent concepts, how the worked case changes through it, what evidence proves it, and how to debug a stall or wrong result. Use staged prose and recaps rather than collapsing these jobs into one summary sentence.

A load-bearing UE design choice also requires design-rationale coverage. The reader must be able to answer why UE uses this split, what becomes incorrect or costly without it, which parts are hard constraints versus engineering choices, what alternatives exist, what each alternative trades, when an alternative could be better, and why the current path chooses this tradeoff. Reject universal claims that one design is "better" without a named objective and operating conditions.

Use language-led teaching with source-anchored confidence. Establish the reader's mental model in prose first, then use the smallest useful set of UE symbols or code anchors to locate the real implementation, and immediately translate those anchors back into state changes, data shape, ownership/control transfer, ordering constraints, and debug judgment. Avoid both extremes: a source-reading walkthrough that makes code carry the teaching, and a code-free abstraction that leaves the reader unable to inspect the real UE path.

Claude owns teaching structure and prose. Codex owns technical facts, final verification, completion status, `OUTLINE.md`, `SOURCE_INDEX.md`, and `GENERATION_GUIDE.md`.

Do not treat completion status as a teaching-quality score. A chapter can pass Gate 3 and still need high-value teaching improvements.

## Short Prompts

- `终审 <编号或文件名>` / `验收 <编号或文件名>` / `收尾 <编号或文件名>` / `完成检查 <编号或文件名>` -> run final regression.
- `审核 <编号或文件名>` -> run final regression and include teaching-quality opportunities unless the user explicitly requests completion-only.
- `能完成吗 <编号或文件名>` -> decide whether the chapter may be marked complete.
- `质量复审 <编号或文件名>` / `教学质量 <编号或文件名>` / `优化建议 <编号或文件名>` / `有什么建议 <编号或文件名>` -> run teaching-quality improvement review; do not update completion status, chapter status, or public status files.
- `全文优化 <编号或文件名>` / `章节优化 <编号或文件名>` / `按质量标准优化 <编号或文件名>` -> run chapter-wide teaching optimization against the chapter-independent quality contract; audit all load-bearing concepts, apply every high-value prose upgrade needed, and do not update completion status or public status files.
- `正文优化 <编号或文件名>` -> apply the requested prose optimization; when the user requests a quality standard or full-chapter closure, use the chapter-wide mode.
- `比较 <编号> 和 <编号>` / `哪个质量更好` / `教学质量对比` -> run comparative teaching-quality review; report formal acceptance separately.
- `合并报告 <编号或文件名>` -> verify and merge accepted SOURCE_INDEX additions or debug records.

If the prompt gives only a chapter number, map it through `OUTLINE.md`.

## Required Context

Work in:

```text
D:\Unreal\EnrealEngine\UnrealEngine
Engine/Docs/Tutorial_002_RenderingDeep/
```

Before accepting or rejecting a chapter, or before producing a teaching-quality judgment, first discover the complete source-material set.

### Source-Material Discovery

- Inspect the current user message, prior messages in the active task, file mentions, attachments, pasted-text files, absolute paths, and explicitly named "original", "previous", "reference", or "current" versions.
- Treat user-provided attachments and external paths as first-class candidate materials even when they are outside the repository and even when repository `origin.md` has no matching chapter.
- Do not infer "no original version" from the absence of a chapter in `origin.md`. Absence from one container file proves only that the file lacks the chapter.
- Before analysis or delegation, state the candidate-material inventory for each chapter: path or attachment, role claimed by the user, physical-line count, and whether the content was read successfully.
- If the user says an original/reference version exists but its contents or path are unavailable, stop the reconstruction judgment and report the missing material. Do not silently downgrade to single-source mode.
- Pass every discovered material path to auditors, writers, and reviewers. A delegated agent that did not receive or read the full material set cannot certify information-value preservation.
- If a new source material appears after a PASS, invalidate all information-value judgments, shrinkage audits, CoverageMatrix conclusions, and Teaching Edit Report conclusions derived from the incomplete set. Re-run the ledger, prose review, and sidecar review before reporting completion.

### User-Declared Original Baseline

- When the user states that the current chapter files are the original versions, treat that statement as the authoritative material role. Do not reinterpret those files as already optimized drafts because their headers, sidecars, completion labels, timestamps, or prior PASS reports say otherwise.
- Before any prose edit, preserve an immutable chapter-local snapshot of every user-declared original. Record the snapshot path and a Unicode-aware physical-line count in the material inventory. All later information-value and shrinkage audits must compare against that snapshot, not against the evolving working file.
- A frozen original is evidence, not a prose template. Keep all unique teaching value, but fact-check it against UE5.7 source and rewrite inaccurate, obsolete, duplicated, or source-walkthrough-heavy presentation into a better teaching form.
- Historical CoverageMatrix and Teaching Edit Report files are untrusted claims until reconciled with the frozen original and current UE5.7 source. They may identify candidate topics, but they cannot prove coverage, justify deletion, narrow fact verification, or certify quality.
- If the original and working file are initially identical, do not invent a dual-version comparison. The first audit is an original-to-quality-contract audit. After a Writer changes the prose, the frozen snapshot becomes the preservation baseline for independent review.
- Never overwrite, regenerate, or normalize the frozen snapshot. If the snapshot is missing, modified, or cannot be read, stop editing that chapter and restore a trustworthy baseline before continuing.

Then read completely:

1. `Engine/Docs/Tutorial_002_RenderingDeep/GENERATION_GUIDE.md`
2. `Engine/Docs/Tutorial_002_RenderingDeep/OUTLINE.md`
3. `Engine/Docs/Tutorial_002_RenderingDeep/SOURCE_INDEX.md`
4. The target chapter.
5. `<chapter-stem>_TeachingEditReport.md`, if present.
6. `<chapter-stem>_CoverageMatrix.md`, if present.

Then read prior completed chapters that define terms, ownership boundaries, or lifecycle stages used by the target chapter.

Do not read or use another chapter as a prose-quality calibration target. Read prior or adjacent chapters only when they define terminology, ownership handoffs, lifecycle stages, facts, or chapter boundaries needed by the target.

If any required file is missing (`GENERATION_GUIDE.md`, `OUTLINE.md`, `SOURCE_INDEX.md`, or the target chapter), stop and report the missing path. Optional sidecars may be absent; state the absence and continue with the fallback checks.

Before using `<chapter-stem>_TeachingEditReport.md`, verify that it matches the current chapter state. If the chapter header or CoverageMatrix says Gate 1 / pending Claude / rebuilt after the report date, while the report says "marked complete" or describes an older draft, treat the report as stale historical context only. A stale report cannot satisfy the Claude-edit requirement and cannot narrow fact regression.

## Parallel Workflow Guardrails

If parallel agents or workers are used, workers may perform read-only checks or chapter-local analysis only. Public files such as `OUTLINE.md`, `SOURCE_INDEX.md`, and `GENERATION_GUIDE.md` must be written only by the main agent, last, after final acceptance and after all worker outputs are reconciled.

For user-requested multi-agent prose optimization, split workers by disjoint target chapter. A worker may edit only its assigned chapter prose, and must not edit shared/public files, chapter headers, status fields, CoverageMatrix, Teaching Edit Report, or `GENERATION_GUIDE.md` unless the user explicitly includes those files in scope. The main agent must reconcile all worker outputs, re-read the changed sections, and confirm each chapter independently satisfies the chapter-independent quality contract before reporting success.

If a Teaching Edit Report, worker report, diff summary, or sidecar is missing, vague, stale, or inconsistent, do not use it to narrow fact verification. Re-verify every A-class and high-risk fact for the affected scope directly in `Engine/Source`.

## Dual-Source Reconstruction Protocol

Use this protocol when the user asks to optimize, reconstruct, merge, or re-review a chapter from two or more candidate materials, including repository files, user attachments, pasted-text files, or external paths.

- Treat every discovered original, reference, previous, and current version as an equal candidate material. Do not select one whole version as the base merely because it is older, newer, longer, stored in `origin.md`, attached by the user, or previously accepted.
- Before editing, build a teaching-unit ledger for every principle, mechanism, condition, exception, case, debug judgment, and source-walkthrough meaning found in either version. Record the final action as keep, merge, migrate, rewrite, reject as false, or reject as true duplication.
- For every removed paragraph, identify whether it contains unique teaching value. If it does, name its destination in the final structure before deletion. Never equate removing a source walkthrough with removing its technical meaning.
- Make the writer follow the completed ledger. Do not allow free whole-version replacement, summary regeneration, or line-count-driven rewriting.
- Assign prose writing and acceptance review to different agents. A writer cannot certify its own chapter. Return exact blockers to the same writer, then have the same independent reviewer recheck the fixes.
- Keep one writer per chapter file at a time. Parallelize only disjoint chapters or read-only review tasks.
- Run a cross-chapter review after each batch and before rebuilding sidecars. Resolve terminology, responsibility-transfer, lifecycle, completion-depth, and chapter-boundary conflicts first.
- Rebuild CoverageMatrix and Teaching Edit Report only from the accepted final prose. Do not inherit old locations, counts, pass claims, or conclusions.
- Independently review rebuilt sidecars against the complete discovered material set and final prose. Writer self-report is not acceptance evidence.
- A PASS based on an incomplete material inventory is void, even if prose facts are correct. Explicitly retract it and rebuild sidecars after the complete-set review.

### Information-Value and Line-Count Guardrail

Line count is an anomaly signal, not a quality score.

- Compare physical lines using a Unicode-aware method such as `File.ReadAllLines` or counting all recognized line separators. Do not rely on PowerShell `Get-Content(...).Count` for mixed Unicode line endings.
- Use `File.ReadAllLines(path).Length` as the canonical physical-line count. If a separator-counting implementation is unavoidable, add one only when the file is non-empty and does not end in a recognized line separator; a trailing separator must not create an extra empty physical line. Cross-check any recorded count against `File.ReadAllLines` before publishing it.
- State the counting method whenever a sidecar records line totals or percentages.
- If the accepted prose is more than 15% shorter than either candidate source, require an explicit information-value audit. List the removed material classes and show where every unique principle, condition, exception, case, debug judgment, and source-walkthrough meaning landed.
- If the prose is not shorter, still perform the dual-source ledger audit; absence of shrinkage does not prove preservation.
- Never use increased line count as proof of teaching quality.
- Never use decreased line count as proof of clarity. Clarity must be demonstrated by the concept audit and reader reasoning tasks.
- Treat introduction, mechanism, worked-case application, and debugging recap as different teaching jobs. Do not label them duplicate merely because they mention the same concept.

### Terminology and Completion-Depth Regression

For multi-chapter reconstruction, reserve terms consistently:

- Use `Scene publication` for `FScene::Update()` absorbing queued changes into Renderer-visible state. Do not call Scene requests, Scene update, or publication `Submit`.
- Reserve `Submit` for an explicitly named control-transfer depth, such as render command-list transfer, RHI command-list transfer, or Platform Queue Submit.
- Distinguish RDG work declared, RHI recorded, platform commands formed, queue submitted, and GPU consumed. Do not collapse them into "uploaded", "executed", or "complete" without stating the exact evidence depth.
- For resource reuse or retirement, require evidence that covers the last GPU consumer, not merely CPU recording or queue submission.

### No Chapter-as-Benchmark Rule

- Do not treat any existing chapter as the canonical quality target. A chapter can be factually incomplete, stylistically unsuitable for another topic, stale relative to source, or previously accepted under an incomplete material inventory.
- Use prior chapters only for terminology, responsibility handoffs, lifecycle continuity, and already verified cross-chapter facts.
- Never copy another chapter's length, section count, density, worked-case shape, or sidecar verdict as a requirement.
- Never compress a UE-specific concept merely to make the chapter shorter. Expand until a UE newcomer can follow the positive state chain and use it for debugging.
- Evaluate every target chapter directly against the same atomic dimensions: complete source-material value, UE5.7 facts, one coherent mainline, first-use concept teaching, owner/data/control/lifetime, design rationale and alternatives, conditions and boundaries, worked-case depth, last-valid-state debugging, source restraint, and cross-chapter consistency.
- Completion status, prior PASS decisions, CoverageMatrix labels, and Teaching Edit Reports are evidence to re-check, not authority.
- Comparative review is separate from acceptance. A chapter may teach differently from another chapter and still pass when all atomic dimensions are satisfied.

## Review Mode Selection

Select the mode from the user's real question, not from the chapter status:

- Final regression: use when the user asks whether the chapter can be accepted, completed, merged, or marked done.
- Teaching-quality improvement review: use when the user asks for suggestions, clarity, "quality", "好不好懂", "为什么没有建议", or whether a passed chapter can be improved.
- Apply chapter-wide teaching optimization: use when the user asks for full-text optimization, chapter optimization, or quality-standard closure. This mode edits chapter prose directly, audits the full chapter, and does not make a completion/status judgment.
- Comparative teaching-quality review: use when the user asks which chapter teaches better or compares chapters. Keep comparison separate from acceptance.

When the user says only `审核`, run final regression and then add teaching-quality opportunities. Do not answer "no suggestions" merely because every gate passes.

When the user says `复审 <chapter>` or `质量复审 <chapter>`, assume a deep teaching-quality review, not a brief status check. Use the chapter-independent quality contract unless the user explicitly requests a short summary.

When the user requests full-chapter optimization, do not satisfy it with one local fix, one thin concept upgrade, or a brief diagnosis. Perform a chapter-wide concept-passport audit, edit all high-value prose gaps found in the target chapter, re-check the edited chapter against every atomic quality dimension, and only then report success or remaining blockers.

Quality review state boundary:

- Quality review never changes article status. Do not change the chapter header status, `OUTLINE.md` status, `SOURCE_INDEX.md` completion log, or any sidecar status field merely because a quality review says pass, quality complete, or review complete.
- Quality review may only produce a detailed diagnosis, identify missing teaching detail, and propose concrete prose additions. If the user explicitly asks to apply the review, edit teaching prose or requested detail records only; do not change completion metadata as part of that operation.
- If the user asks to mark a chapter complete, accepted, or status-complete, treat that as final regression or an explicit status-maintenance task, not as quality review.

When the user points out any concrete clarity, terminology, or teaching-order problem, treat it as a defect-class signal even if the user does not call it an example. Do not handle it as a single local edit request. First name the underlying class of problem, check whether this skill already contains an enforceable rule for that class, update this skill first if the rule is missing or too weak, then scan the whole target chapter for the same class before editing prose. Do not report success after fixing only the named occurrence unless the same-class audit finds no other instances. The final response must state the generalized class that was audited, which load-bearing occurrences were fixed, and which non-load-bearing occurrences were left unchanged with the reason they do not carry the chapter mainline.

When a user's clarity complaint involves repeated negative teaching words such as `不是`, `不能`, `不要`, `不等于`, `不负责`, `不代表`, or `还没有`, treat the issue as a chapter-wide negative-boundary teaching defect. Scan the whole target chapter for the same pattern. For every load-bearing occurrence, rewrite the local explanation so it first gives the positive state chain: current concrete state -> missing state -> producer/owner -> consumer -> why the order matters -> symptom/debug judgment if the state is read too early. Keep negative wording only as a short boundary after the positive model is already usable.

When a user's clarity complaint involves internal task or source containers, demote that class of names before editing prose. Names such as `FVisibilityTaskData`, `ViewPacket`, maps, request arrays, packet/task data, allocator/scratch objects, and similar implementation containers must not be first-class teaching concepts unless the chapter first explains the reader-facing problem they solve. Teach the conceptual state/progress/ownership problem first, then introduce the source name only as a debug anchor or locator, and immediately translate it back into what data/control changed and what question it helps answer.

When a user's clarity complaint involves unexplained names, process labels, stage names, data names, or workflow verbs, treat it as a chapter-wide first-explanation defect. Do not assume the reader already understands terms merely because they appeared in an earlier chapter, are common rendering words, or are obvious to an Unreal engineer. For every load-bearing term or flow step, the prose must first explain the positive design meaning in plain language: what reader-facing problem exists; what UE is trying to make stable, cheaper, reusable, parallel, or debuggable; what concrete data/state/control changes at this step; who produces it; who consumes it next; and what would become ambiguous, expensive, or unsafe without this split. Only after that should the UE symbol, enum, function, stage label, or source container carry the argument. Negative boundaries such as "not X" may remain only after the positive design purpose is clear.

When a user's clarity complaint involves CPU/GPU placement, readback, GPU culling, `PrimitiveVisibilityMap`, `Instance Culling`, occlusion query, HZB, indirect args, or historical GPU feedback, treat it as a producer-consumer synchronization teaching defect. Do not explain the design as merely "GPU is faster", "CPU is slower", or "readback is slow". First teach the positive chain: what data is produced, who consumes it next, whether that consumer needs the result in the same frame, whether the result can remain GPU-resident, whether CPU consumption is delayed/optional/conservative, and what stall appears if CPU and GPU must wait on each other. Use this four-case model when relevant: CPU-produced/CPU-consumed same-frame data; GPU-produced/GPU-consumed resident data; GPU-produced/CPU-consumed delayed or optional historical feedback; and GPU-produced/CPU-consumed current-frame mandatory data that creates a hard synchronization point. Mention readback only after this model: readback can be costly because it moves data and, more importantly for frame architecture, can force CPU/GPU synchronization. Scan the whole target chapter for the same class of claim before editing, and distinguish broad-phase visibility, instance-level GPU culling, occlusion history, HZB construction, and indirect draw consumption by their producer-consumer chain.

When local source has been inspected, or when a local source check can determine the behavior within the requested scope, do not use speculative wording for that behavior. Banned load-bearing ambiguity words include `可能`, `大概`, `或者`, `一般`, `通常`, `maybe`, `might`, `could`, `roughly`, and similar hedging markers. Replace them with explicit condition/result statements: "when condition A holds, UE does B"; "when condition A is false, the function returns"; "the current source check did not cover C, so do not make a claim about C." Use uncertainty labels only when uncertainty is real and named: platform-dependent, configuration-dependent, version-dependent, not verified in source, or outside the current chapter boundary. If a statement needs a conditional branch, name the exact branch condition instead of hiding it behind hedging prose.

When a user's clarity complaint involves Unity, SRP, HDRP/URP, renderer lists, DepthOnly, camera depth texture, or any cross-engine analogy, treat it as a false-equivalence teaching defect. Do not use another engine's optional feature, default pipeline choice, editor switch, or naming convention as the first explanation for a UE mechanism. First teach the UE layer that actually owns the behavior: render architecture choice, mandatory scene contract, optional producer, resource state, pass setup, command pipeline, or consumer timing. Then, if a comparison is still useful, state the exact layer that matches and the exact layer that does not. Cross-engine bridge prose must not imply that two systems are the same mechanism just because their names or visible outputs resemble each other. When the analogy becomes load-bearing, replace it with a UE-first positive model and keep the other engine only as a warning about which intuition to discard.

## Final Regression Workflow

Run these checks in order:

1. Read the Teaching Edit Report. It should describe theory/flow restructuring, source-reading material removed or demoted, technical meaning preservation, and fact questions for Codex. First validate report currency against the chapter header, CoverageMatrix date/stage, and any status notes. If the report is stale or internally claims completion for a superseded draft, reject the chapter until Claude refreshes the report or run a full fallback review only as historical analysis without marking complete.
2. Check for technical drift. If a baseline, git diff, or pre-Claude version is available, compare it against the final chapter. Claude should not add new technical facts; any new or changed technical claim must be verified by Codex or treated as a blocker. If the report is unreliable, use the full A-class/high-risk re-verification fallback above.
3. Resolve every `Fact Questions For Codex` item. Fix the chapter with verified facts or leave it rejected.
4. Re-verify every A-class and high-risk claim in `Engine/Source`. This is fact regression, not teaching content.
5. Rebuild the coverage matrix from `OUTLINE.md` and the final chapter. If the sidecar exists, reconcile it with the rebuilt matrix and update it only after the final prose is accepted.
6. Run the theory-model audit:
   - The opening starts from the reader's misconception or missing mental model.
   - The chapter explains why the UE mechanism exists before naming implementation symbols.
   - Important new names/classes/schemes/flows are introduced with role, owner, lifetime boundary, input/output, design reason, and failure/debug meaning before they carry the argument.
   - Concepts that sit near adjacent technical categories preserve their boundaries: broad category first, chapter-specific subset second, hard constraint distinguished from UE design choice.
   - Terminology-dense chapters include a first-use stabilization layer for terms such as task, queue, pipe, command list, submit, fence, named thread, local queue, cache, lifecycle stage, or similar concepts before those terms drive the main flow.
   - Terminology-dense chapters use worked cases where definitions alone would remain abstract. The cases must bind new terms to concrete data/state changes, ownership, design reason, and debugger questions; they should not be superficial analogies or source-call walkthroughs.
   - Algorithm or data-structure names such as octree, BVH, spatial hash, grid/tile list, HZB, bitset, queue, bucket, cache, or graph must not assume prior reader knowledge when they affect the argument. First explain the search/storage problem they solve, what data they group or index, why UE uses or rejects them in this local path, what changes if they are absent, and what nearby alternatives exist. When mentioning alternatives only to preserve category boundaries, either give a short plain-language role for each alternative or avoid naming it.
   - Stage names, negative checklists, and "cannot assume X yet" lists must not carry first teaching. If a paragraph says that `mesh pass setup`, `GPUScene offset`, `scene uniform`, `Instance Culling`, `HZB`, `MRT layout`, or similar dense mechanisms are not ready, it must first explain the positive state chain: what object currently exists, what is still missing, who will produce the missing state, who will consume it, and what breaks if it is read too early. Otherwise mark the paragraph as stage-name debt and rewrite it into a worked state transition.
   - CPU/GPU placement, readback, culling, occlusion query, HZB, and indirect draw explanations must be framed by producer-consumer synchronization, not by a one-line performance slogan. The reader should be able to say whether the result is consumed CPU-side in the same frame, consumed GPU-side without readback, consumed CPU-side as delayed conservative history, or would force an unsafe/hitch-prone current-frame GPU-to-CPU dependency.
   - Dense concept-passport, glossary, or name-settling sections may stabilize or recap terms, but must not carry the first real teaching of load-bearing concepts. If such a section appears before the flow, it must stay short enough to act as a map; otherwise move it to a chapter-end recap and teach the concepts inside the flow where they solve a concrete problem.
   - New concepts are defined by positive role and design purpose first. The chapter does not rely on repeated "not X, but Y" corrections as the primary teaching structure; negative boundaries are used only after the positive model is established.
   - Major sections teach purpose -> design reason -> conceptual model -> process flow -> consequence/debug reasoning.
   - The mainline survives if code snippets, function traces, source paths, and field tables are removed.
7. Run the concept teaching regression:
   - For each first-class concept introduced or substantially deepened in the chapter, verify the reader can answer what it is, why UE needs it, what data it contains or receives, who owns or advances it, where it sits in the mainline, and what debug question it answers.
   - Check whether the concept is carried by the throughline example or a local worked case. If a terminology-dense concept has only a definition, table, or symbol gloss, mark the relevant CoverageMatrix row `partial`.
   - Worked cases must show concrete data/state change, ownership, design reason, and debugger question. Superficial analogies, source-call walkthroughs, or examples that do not map back to the mainline do not satisfy the gate.
   - If the final chapter removed a load-bearing worked case from the Codex/Claude reports without replacing its teaching function, reject the chapter or require a teaching edit refresh.
8. Run the simplification regression audit:
   - Scan for high-risk absolutist wording: `only`, `must`, `cannot`, `always`, `never`, `the single`, `essentially`, `is just`, and Chinese equivalents such as `只`, `只能`, `必须`, `不能`, `一定`, `永远`, `唯一`, `就是`, `本质上`.
   - For each high-risk statement, decide whether it is a verified hard fact, a UE engineering choice, or chapter-local shorthand. Hard facts need source verification; design choices need design reason; shorthand must be scoped.
   - Check that local chapter paths are not presented as global technical categories. For example, a GPUScene path can focus on uniform + ID/offset + buffer without implying that shaders do not also consume textures/samplers or other explicitly bound resources.
   - Check adjacent mechanisms that readers commonly confuse: uniform/constant parameters vs structured buffers, buffer vs texture, SRV vs resource, ID vs offset, payload vs fixed record, persistent identity vs draw-local lookup, scene context vs view context, CPU ownership vs GPU visibility.
   - If an adjacent mechanism could technically replace the chosen one, the chapter must explain why UE uses the chosen mechanism here in terms of scope, lifetime, binding, access pattern, performance, or debugging boundary. Do not accept "it must be this way" unless that impossibility is verified.
9. Run the process-clarity audit:
   - Flow diagrams and prose describe conceptual states and ownership transitions, not raw call stacks.
   - The reader can answer what changed, when it changed, who owns it now, and why the order matters.
   - Lifecycle, thread, ownership, resource state, deferred cleanup, and creation/destruction boundaries are not blurred.
10. Run the source-restraint audit:
   - No opening or major section is organized around source files, functions, class lists, field lists, or call order.
   - Repeated source-anchor / `源码锚点` blocks do not appear in the chapter body; verification records belong in sidecars or `SOURCE_INDEX.md`.
   - Chapter-body debug waypoints may use already-explained UE symbols only. File paths, line numbers, dense source lists, and verification records in the chapter body are blockers.
   - No major conclusion is justified only by "source says this function writes this field."
11. Run reader-value and Unity-bridge audits:
   - The chapter is detailed enough for a Unity rendering reader to understand the UE framework difference.
   - Unity comparisons orient the reader without claiming false equivalence.
   - The chapter gives at least one useful debug reasoning path.
12. Check chapter boundary. Later-chapter material must be only a pointer.

Never accept a fact because a report says it was verified. Re-open the source.

## Teaching-Quality Improvement Review

Use this review to find improvement opportunities even when the chapter is already accepted.

The review output itself must teach the diagnosis. Do not compress the result into aggregate `pass` lines with file-path evidence. A quality review is inadequate if it does not show which concepts were inspected, how each concept is taught, whether the worked cases actually carry the concept, and why each suggested change improves reader understanding.

Quality review does not advance workflow state. Its job is to add inspection detail and teaching-detail recommendations. A `pass` result means "no blocking teaching-quality issue found"; it does not authorize status edits.

Run these checks:

1. Build a concept load map: list the first-class concepts the reader must learn, where each first appears, and whether the article explains what it is, why UE needs it, what owns or advances it, what data/control it carries, and what debug question it answers.
2. Check first-use teaching order: important new terms must get a positive mental model before they drive process explanation. Do not count repeated "not X, but Y" corrections as sufficient explanation.
3. Check worked-case coverage: dense or abstract concepts need a throughline example or local worked case that shows concrete state/data change, ownership, design reason, and debugger consequence.
4. Check concept-passport placement: upfront glossary/name-settling material is acceptable only as a short reading map. If it introduces many unexplained symbols or tries to explain first-class concepts before the reader has a concrete flow, mark it as a teaching-structure gap and recommend compressing, distributing into flow sections, or moving it to a chapter-end recap.
5. Check conceptual simplification safety: high-risk words such as `only`, `must`, `cannot`, `always`, `never`, `just`, `只`, `只能`, `必须`, `不能`, `唯一`, or `就是` must not turn a scoped teaching path into a global fact.
6. Check adjacent category boundaries: ID vs offset, buffer vs texture, uniform/constant parameters vs structured buffers, SRV vs resource, payload vs fixed record, persistent identity vs draw-local lookup, scene context vs view context, CPU ownership vs GPU visibility. If the chapter uses one mechanism where another could technically carry similar data, it should explain the design tradeoff rather than imply impossibility.
7. Check design rationale: for every load-bearing split or mechanism, require why it exists, what goes wrong or becomes costly without it, hard constraint versus UE choice, plausible alternatives, tradeoffs, and conditions where an alternative could be preferable.
8. Check process readability: the mainline should move purpose -> model -> state/control transition -> consequence. If the article jumps into code order, symbol lists, or reverse explanations, mark where the reader loses the thread.
9. Check code-anchor balance: code, function, class, or field names should appear after the prose model needs a concrete UE anchor, and each anchor must be translated into what changed in state/data/ownership/control and what it helps debug. If the paragraph depends on many unexplained symbols, mark it as source drift; if it avoids all real anchors for a technical mechanism, mark it as vague abstraction.
10. Check reader value: identify whether the chapter gives enough "what breaks / where to inspect / why this order matters" reasoning for a Unity rendering reader.
11. Check pacing and summaries: dense sections should have local recap sentences that restore the mainline before adding more terms.

For architecture chapters, include at least the load-bearing concepts that carry the mainline, such as layer responsibility, `RenderCore`, `FSceneViewFamily`, `FScene`, `FSceneRenderer`, `FViewInfo`, Component/Proxy/SceneInfo, visibility, RDG, MeshDrawCommand, RHI, and backend boundaries when present. For other chapters, derive the equivalent first-class concepts from the chapter's own mainline.

Apply the chapter-independent quality contract when the user asks for `复审`, `质量复审`, or "按标准": dense or new concepts should be explained by what they are, why UE splits them this way, what adjacent concepts they can be confused with, what data/control moves through them, who owns or advances them, where they sit in the chapter's throughline, and what concrete debug question they answer. A single throughline case can satisfy a concept only if the prose explicitly maps that concept to concrete state/data changes, ownership, design intent, boundary safety, and debugging consequence. If the throughline merely mentions the concept, require a local worked case or mark that concept `partial`.

When giving high-value upgrades, each item must explain:

- Location: where the change belongs.
- Reader problem: what misunderstanding or missing mental model remains.
- Why it matters: why this affects the article's mainline or debug value.
- Teaching job: what the added or reordered prose must teach.
- Suggested shape: a concrete case, recap, comparison, or sentence pattern to add.
- Success criterion: what the reader should be able to answer after the change.

Report suggestions by priority:

- Gate blockers: issues that should fail final acceptance.
- High-value teaching upgrades: changes that would materially improve clarity, even if the chapter passes.
- Low-risk polish: small wording, ordering, or recap improvements.

Do not invent filler suggestions. If there is truly no material change to make, say which dimensions were checked concept by concept and why only maintenance-level polish remains. Completion status, stale report status, or `OUTLINE.md` status must not be used as evidence that no suggestions exist. File paths and line numbers may support a finding, but they are not the finding; always explain the teaching effect in prose.

## Apply Chapter-Wide Teaching Optimization

Use this mode for prompts such as `全文优化 07`, `章节优化 07`, `按质量标准优化 07`, or "optimize the whole chapter." If the prompt is only `正文优化 <chapter>`, use this full chapter-wide mode when the user requests full-chapter quality closure; otherwise apply the requested local prose optimization.

This is an editing mode, not a status mode:

- Read all required context for the target chapter and only the prior or adjacent chapters needed for terminology and boundary continuity.
- Do not run or report a completion decision unless the user separately asks for final regression.
- Do not update the chapter header status, `OUTLINE.md`, `SOURCE_INDEX.md`, `GENERATION_GUIDE.md`, CoverageMatrix status, Teaching Edit Report status, or completion logs.
- Edit only the target chapter prose, and only sidecar detail records if the user explicitly asks for them.

Chapter-wide optimization is a quality-closure request, not a local polish item.

When multiple chapters are requested at once, treat each chapter as its own chapter-wide quality-closure request. Do not average the result across chapters, and do not let a light-risk chapter hide a serious gap in another chapter. For each target, identify whether the needed work is a map demotion, a flow-distributed concept explanation, a local worked case, a boundary/simplification correction, or a recap/debug-route upgrade; then apply the highest-value prose edits needed for that chapter.

Run this workflow:

1. Build a concept passport for every load-bearing concept in the target chapter, not just the first thin spot. Include first use, what/why coverage, owner/data/control, adjacent-boundary safety, worked-case coverage, and debug value.
2. Compare each concept against the chapter-independent quality contract. Mark concepts as `meets`, `upgrade needed`, or `blocked`. Completion status or existing CoverageMatrix `deep` labels are not evidence for `meets`.
3. Edit every concept marked `upgrade needed` when the needed change is within the chapter boundary. Do not stop after one fix unless every other concept already meets the standard.
4. Prefer focused prose edits over broad rewrites, but do not compress a concept-teaching upgrade into a single guard sentence when the concept needs a teaching paragraph.
   Clarity outranks brevity in this mode: if a dense mechanism cannot be understood from a short paragraph, expand the prose until the reader can answer what/why/owner-data/boundary/failure-debug questions without relying on unstated source knowledge.
5. If the user identified any concrete poor-teaching symptom, generalize it into a same-class audit before touching prose, even when the user did not explicitly call it an example. If the class is not already covered by an enforceable rule above, strengthen this skill first. Examples: an unexplained data structure implies scanning algorithm/data-structure names; one stage-name checklist implies scanning all negative readiness lists; one overloaded cache claim implies scanning all cache/identity/lifetime claims. Scan the entire target chapter, not only the nearby section. Apply edits to every load-bearing instance found; leave a non-load-bearing instance unchanged only when the final report states why it does not carry the chapter mainline.
6. Replace stage-name checklists with state-chain teaching. When the draft lists names such as `dynamic mesh element`, `mesh pass setup`, `GPUScene offset`, `scene uniform`, `Instance Culling`, `HZB`, `MRT slot`, or `StateBucketId` before teaching them, do not merely add a sentence. Rewrite the local flow so the reader sees: current concrete object/state -> missing state -> producer/owner -> consumer -> why the order exists -> symptom/debug question if the state is missing.
7. For each upgraded concept, teach in this order when applicable: what it is; why UE needs it; who owns or advances it; what data/control it carries; how it differs from adjacent concepts; what conditions can prevent it from working; what debug question the reader can answer afterward.
8. Establish the positive model before negative boundaries. Use "not X" only after the reader has a usable "what it is" model.
9. Avoid new-term debt. If the edit mentions terms such as culling payload, indirect draw, preserve instance order, state bucket, primitive id, instance offset, payload, fixed record, SRV, resource, uniform, structured buffer, octree, BVH, spatial hash, grid/tile list, HZB, bitset, queue, bucket, cache, or graph, either explain the term's role locally in plain language or rewrite the sentence so the new term is not load-bearing. Never use a newly introduced technical term to explain an older unclear term unless the new term is immediately given a plain-language role and local teaching job. If explaining term A requires term B, first explain B's role in ordinary prose or rewrite the sentence so B is not load-bearing. Do not use a list of unexplained alternatives as a shortcut for design tradeoff teaching.
10. Treat process labels and stage verbs as concepts when they carry the chapter mainline. A label such as setup, gather, build, resolve, submit, publish, consume, cull, compact, merge, cache, upload, register, extract, or synchronize must be explained by its design job before it is used as if the reader already knows the workflow. The reader should be able to say what existed before the step, what exists after it, why UE split that step out, and what debug symptom appears if the step is skipped or read too early.
11. Tie dense concepts back to the existing throughline case or add a local worked case that shows concrete state/data change, ownership, design reason, and debugger consequence.
12. Treat concept-passport/glossary sections as recap or checkpoint material, not as the primary teaching path. When a dense name-settling section appears before the flow, either compress it to a short reading map or move it near the end as a recap, then distribute the real concept teaching into the flow sections where each concept solves a concrete problem.
13. Use code and UE symbols as minimal debug anchors, not as the primary explanation. When an edit needs a code anchor, first explain the mental model in plain language, then name only the symbol(s) needed to locate the mechanism, then state what data/control/ownership changed and what debugger question this anchor answers. If a paragraph needs a cluster of new names, split it or teach the names inside the flow before relying on them.
14. Verify any new or sharpened technical claim in `Engine/Source` before editing. If verification would expand beyond the chapter boundary, keep the prose scoped or leave a recommendation instead of adding the claim.
15. Preserve source restraint: no source paths, line numbers, verification records, or dense call-stack prose in the chapter body.
16. After editing, re-read the changed sections and re-check the concept passport. If material gaps remain, continue editing or report the precise blocker.
17. Inspect the local diff and confirm that only intended prose/status-neutral files changed.

The final report for this mode should say whether a chapter-wide concept audit was completed, which concepts were upgraded, why the prior prose was thin, how the new prose satisfies each atomic quality dimension, what files changed, and which status files were intentionally untouched. Claim quality closure only if all load-bearing concepts are re-checked as `meets` after edits; otherwise report the remaining blockers.

## Comparative Teaching-Quality Review

Use this mode for prompts such as "比较两章", "哪个质量更好", or "质量对比".

Compare teaching quality independently from formal acceptance:

- Mainline continuity: which chapter gives the reader a stronger throughline.
- Concept introduction: which chapter better explains first-use terms by what / why / owner / data / debug meaning.
- Conceptual boundary safety: which chapter better preserves adjacent technical categories and avoids turning local shorthand into global facts.
- Worked cases: which chapter uses concrete cases to carry abstract concepts.
- Reader debug value: which chapter better teaches how to reason when the system stalls or breaks.
- Terminology handling: which chapter prevents dense new names from becoming symbol memorization.
- Theory depth: which chapter better explains why UE is designed this way.
- Process clarity: which chapter better shows state/control/lifetime transitions.
- Boundary discipline: which chapter avoids later-chapter expansion and source-reading drift.
- Unity bridge: which chapter better orients Unity readers without false equivalence.

Never let accepted/rejected status, stale report status, or `OUTLINE.md` status decide the quality winner. Report formal acceptance as a separate note after the quality comparison.

Use this format:

```text
Comparative Teaching Quality Review:
- Completion status excluded from score: yes/no
- Mainline continuity: chapter A / chapter B / tie + reason
- Concept introduction: chapter A / chapter B / tie + reason
- Conceptual boundary safety: chapter A / chapter B / tie + reason
- Worked cases: chapter A / chapter B / tie + reason
- Reader debug value: chapter A / chapter B / tie + reason
- Terminology handling: chapter A / chapter B / tie + reason
- Theory depth: chapter A / chapter B / tie + reason
- Process clarity: chapter A / chapter B / tie + reason
- Overall teaching quality: winner / tie + reason
- Formal acceptance: separate note
- Suggested maintenance: concrete upgrades, if any
```

## Acceptance Criteria

A chapter may be marked complete only when all are true:

- Codex generated or fully verified the technical content.
- Claude teaching edit has been applied or explicitly reviewed.
- The Teaching Edit Report corresponds to the current chapter version and does not carry stale completion claims from a superseded draft.
- Claude did not introduce unverified technical facts; any drift has been corrected or verified by Codex.
- Every report question is resolved or the chapter remains rejected.
- The mainline can be summarized in 5-10 conceptual sentences without gaps.
- The chapter teaches UE rendering theory foundations and framework responsibility, not source reading.
- Every important new term/class/scheme/flow is explained deeply enough to carry the reader through the argument.
- Every load-bearing design choice explains why UE uses it, what degrades without it, hard constraint versus engineering choice, plausible alternatives, tradeoffs, and conditions where another design could be preferable.
- Simplified statements preserve technical category boundaries; no chapter-local path is presented as a global fact, and hard constraints are distinguished from UE design choices.
- Dense clusters of new terms have a prior mental-model layer; no symbol list, table, or route map is used as the reader's first explanation.
- Concept-passport, glossary, or name-settling sections are used as maps or recaps only; they do not replace flow-based first teaching for load-bearing concepts.
- Dense clusters of new terms include enough concrete cases for the reader to answer what data is inside the concept, why UE split it from adjacent concepts, who advances it, and what to inspect when it stalls.
- The CoverageMatrix records worked case coverage for rows involving first-class new concepts, or clearly states why the throughline example already carries the concept.
- No important concept is marked `deep` when it is only defined, tabulated, or named without a usable worked case or throughline example.
- The process flow is clear as conceptual state/data/control/lifetime transitions.
- The chapter is detailed enough that the reader can answer why / what / when / who owns it / what breaks / how to debug.
- The chapter is suitable for a Unity rendering reader unfamiliar with UE.
- High-risk facts are verified in `Engine/Source`.
- Code snippets, function names, class names, field names, and source symbols are used as minimal anchors after a prose model is established; each anchor is translated into state/data/ownership/control/debug meaning, and no load-bearing concept relies on an unexplained symbol cluster.
- Repeated source-anchor / `源码锚点` blocks are absent from the chapter body; file paths, line numbers, dense source lists, and verification records are absent from the chapter body; any remaining symbol references are explained conceptual or debug waypoints.
- No later-chapter material expands beyond the current boundary.

## Maintenance

After a chapter passes:

- update `OUTLINE.md` status;
- update the chapter header status;
- append only newly verified cross-chapter debug records or source facts to `SOURCE_INDEX.md`;
- add recurring process lessons to `GENERATION_GUIDE.md` only when they affect future chapters;
- report the final accepted state.

Do not stage or commit unless the user explicitly asks.

If the chapter fails, leave completion status unchanged and report concrete blockers.

In teaching-quality improvement review, chapter-wide teaching optimization, or comparative teaching-quality review, do not update `OUTLINE.md`, the chapter header, `SOURCE_INDEX.md`, `GENERATION_GUIDE.md`, Teaching Edit Report status, or CoverageMatrix status. If the user explicitly asks to apply accepted recommendations, edit only the teaching prose or non-status detail records needed to add clarity; status changes require a separate final-regression or explicit status-maintenance request.

## Final Response Shape

For final regression, use this concise format:

```text
Final Regression:
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
- Source facts: pass/fail + key checks
- Report questions: resolved / none / blocked
- SOURCE_INDEX updates: merged / none / blocked
- Completion: accepted / rejected
```

Then always include:

```text
Teaching Quality Opportunities:
- Gate blockers: none / ...
- High-value upgrades: ...
- Low-risk polish: ...
```

For teaching-quality improvement review without completion judgment, use:

```text
Teaching Quality Review:
- Formal completion excluded from score: yes
- Status changes: none; quality review does not change article status
- Reference standard: chapter-independent quality contract
- Overall teaching quality: pass / pass with upgrades / partial / weak + reason

Concept Passport Audit:
| Concept | First use | What/why coverage | Boundary/simplification safety | Owner/data/control coverage | Case coverage | Debug value | Verdict |
| ... |

Worked Case Audit:
- Throughline case: what concepts it actually carries, and which concepts it does not carry deeply enough.
- Local cases: where local cases exist, what concrete state/data/ownership change they explain.
- Missing or thin cases: concepts that need a local case or stronger mapping.

Atomic Quality Gap:
- Dimensions satisfied: ...
- Dimensions still weak or blocked: ...
- Why each gap matters for this chapter: ...

High-Value Upgrades:
1. Location: ...
   Reader problem: ...
   Why it matters: ...
   Teaching job: ...
   Suggested shape: ...
   Success criterion: ...

Low-Risk Polish:
- ...
```

For chapter-wide teaching optimization, use:

```text
Chapter-Wide Teaching Optimization Applied:
- Status changes: none
- Files changed: ...
- Status/public files untouched: ...
- Chapter-wide concept audit: complete / incomplete + reason
- Concepts audited: ...
- Concepts upgraded: ...
- Remaining blockers: none / ...
- Original teaching gaps: ...
- Prose changes: ...
- Quality-contract effect: what/why/owner-data/control/lifetime/boundary/conditions/worked-case/debug value now covered
- Verification: source-checked / scoped without new fact / not run + reason
- Chapter-independent quality contract: reached / not reached + reason
```

List only the highest-signal blockers or maintenance edits, but do not omit improvement opportunities merely because the chapter passes. Avoid a review that only says all dimensions pass and then lists two terse edits; that is a status summary, not a teaching-quality review.
