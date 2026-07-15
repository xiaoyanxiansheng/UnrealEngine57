# Explanation Expansion Method

## Contents

1. Purpose
2. Open-ended explanation
3. Reconstructing the causal model
4. Operations and persistent state
5. Settings, values, and interacting systems
6. Alternatives, costs, and boundaries
7. Verification and recovery
8. Evidence and authority
9. Cross-chapter continuity
10. Anti-patterns
11. Iterative improvement

## Purpose

Use this method to discover and insert missing teaching value in a practical UE tutorial. It is not a completion form. A topic may require only some prompts below, or it may require additional explanation not anticipated here.

The goal is reader capability: after the insertion, the reader should be able to perform the work, explain the causal reason, predict important consequences, verify the real state, and debug a failed result.

## Open-ended explanation

Do not equate detail with length or with answering a fixed list. Begin with the reader's actual uncertainty and expand until the mechanism is usable.

Ask continuously:

- Which sentence still requires the reader to trust an unexplained conclusion?
- Which term is doing argumentative work before its role is established?
- Which state change is invisible in the UI instructions?
- Which dependency, condition, or exception would change the result?
- Which claim sounds universal but is only local to this platform, configuration, asset, or course stage?
- What would a capable Unity TA naturally ask next?
- What evidence would distinguish a real fix from a coincidental visible result?

These questions help discover gaps. They do not prove that no other gap exists.

## Reconstructing the causal model

For a load-bearing concept or operation, establish a positive model before warnings and exclusions.

Useful dimensions include:

- the concrete object, data, process, or state;
- the UE layer that owns it;
- the problem it exists to solve;
- its inputs, outputs, producer, and consumer;
- its scope and lifetime;
- the state before and after the operation;
- the reason for the ordering;
- conditions that enable, alter, or invalidate the result;
- nearby concepts that look similar but have different ownership or consequences;
- the observation or debugging question this model enables.

Follow the actual topic. A filesystem operation may need ownership and version-control detail but no shader discussion. An RHI setting may require platform, compilation, runtime, hardware, and restart boundaries. A modeling operation may require input-object lifetime, output-asset ownership, topology, collision, pivot, and rollback.

## Operations and persistent state

An exact click path is necessary for a first-use UE operation, but it is not sufficient.

Teach:

- where the operation begins and what must be selected;
- whether the target is a Project setting, editor preference, Level, Actor, Component, Asset, external source file, or transient tool object;
- whether the action previews, applies, accepts, saves, compiles, cooks, reimports, or restarts;
- where the resulting state is stored;
- whether the current process sees the new state immediately;
- what other objects or systems consume the change;
- what survives closing the editor, changing maps, switching machines, or rebuilding derived data;
- what the operation deliberately leaves unchanged.

Explain why the operation occurs at this point in the workflow. If moving it earlier or later would be harmless, say so. If order is required by a dependency, name that dependency.

## Settings, values, and interacting systems

For a setting, explain more than its display name.

Explore:

- the system layer and state it controls;
- the range or option categories and what changes between them;
- the current value and why it serves this case;
- whether the value is an engine default, template default, project override, asset override, instance override, user preference, or course convention;
- when it is read and whether Apply, Save, Compile, Reimport, Cook, or Restart is required;
- downstream rendering, shader, build, memory, streaming, platform, or asset effects;
- interactions with other settings without reducing the explanation to only those known interactions;
- hardware, platform, content, quality-level, or version conditions;
- how to verify this setting independently of nearby settings.

For a numeric value, explain its unit, reference frame, useful range, why this value was chosen, what increasing or decreasing it changes, where the relationship is linear or non-linear, and what other parameter can produce a superficially similar result.

When several settings appear together, first establish their individual layers and then their dependency graph. Continue following other load-bearing dependencies discovered in the topic. Do not assume that explaining one pair makes the configuration understandable.

## Alternatives, costs, and boundaries

Distinguish:

- physical or API hard constraints;
- requirements imposed by the current UE 5.7 implementation;
- engineering choices made by UE;
- choices made by the course to establish a stable baseline;
- temporary experiment settings;
- production recommendations that depend on content or platform.

Name plausible alternatives only when their role can be explained. For each meaningful alternative, state the objective it improves, what it sacrifices, and the conditions under which it becomes preferable.

Do not write that a choice is "better", "modern", "high quality", or "required" without naming the evaluation objective and operating conditions.

Costs may include more than frame time: shader permutations, cook size, build time, DDC churn, memory, IO, streaming granularity, driver requirements, asset maintenance, iteration speed, debugging ambiguity, collaboration risk, and loss of compatibility.

## Verification and recovery

Verification must test the resulting state, not merely repeat the input action.

Prefer independent evidence such as:

- a persisted config or asset property after restart;
- an expected Asset, Actor, Component, section, UV channel, collision shape, or source reference;
- startup or build logs;
- a controlled visual change with fixed camera and exposure;
- an editor diagnostic or platform capability warning;
- a source-control diff showing the intended persistent owner;
- a repeatable failure/success contrast.

Explain what the evidence proves and what it does not prove.

A recovery path must preserve valid state and must not contradict the chapter's target. Explain the likely cause, the visible symptom, the least destructive correction, and how to re-verify. Avoid generic fixes that hide the error through compensating transforms, exposure, collision, or duplicate settings.

## Evidence and authority

Use evidence proportional to the claim's risk.

- Use UE 5.7 source/configuration for defaults, capability flags, ownership, implementation requirements, and version-specific behavior.
- Use editor execution for menu paths, UI labels, importer behavior, generated assets, restart behavior, and visible results.
- Use official Epic documentation for supported platforms, intended workflows, and externally documented constraints.
- Use RenderingDeep only when its verified theory directly clarifies the practical consequence.

Translate evidence into teaching. Do not ask the reader to infer meaning from a file path, symbol, property initializer, or documentation quote.

When evidence exposes multiple layers, keep them separate. A property initializer is not necessarily the active preset. A project setting is not necessarily the current running process. A platform capability flag is not proof that every GPU and driver supports the feature. A successful screenshot is not proof of the internal rendering path.

## Cross-chapter continuity

Every insertion must respect the evolving project.

Check:

- chapter entry state versus the prior chapter's completion state;
- exact names, dimensions, paths, maps, assets, settings, and temporary objects;
- whether a temporary baseline is later removed or redefined;
- whether a value promised for later comparison remains reproducible;
- whether a correction invalidates the next chapter's instructions, screenshot, or acceptance criteria;
- whether a backup, hidden object, redirector, generated asset, or external source file has a clear lifecycle.

Repair dependent prose in the same requested scope. Report dependencies outside the authorized scope rather than silently leaving a contradiction.

## Anti-patterns

Reject these patterns:

- a click list followed by a generic "this is important" paragraph;
- one group reason standing in for several settings with different responsibilities;
- a checklist whose completed cells are treated as proof of understanding;
- unexplained labels such as modern, standard, production-ready, expensive, optimized, or compatible;
- a warning built only from "do not" statements without a positive state model;
- a source symbol used as the explanation;
- a Unity analogy that hides a UE ownership or lifetime difference;
- an option table introduced before the reader understands what category is being chosen;
- a recovery instruction that merely compensates for a bad source contract;
- deferring necessary understanding to a later chapter when the reader must act correctly now;
- expanding every paragraph equally instead of following load-bearing causal dependencies;
- claiming the chapter is perfect or exhaustive because the current review found no more gaps.

## Iterative improvement

Treat reader feedback as new evidence about the method.

When a user finds an omission:

1. identify why the current method allowed it;
2. strengthen the general discovery or expansion rule without encoding only the example;
3. search the requested material for other places where the same reasoning failure can occur;
4. improve the prose in context;
5. record remaining editor-dependent or source-dependent uncertainty honestly.

The target is a progressively stronger teaching system, not a promise that one pass anticipates every question.
