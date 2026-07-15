# Project Agent Rules

## Version Control

- This project is managed by the existing Git repository rooted at `D:/Unreal`; all future work must follow a Git-based workflow.
- Before changing files, inspect the current branch and working tree, and preserve unrelated user changes.
- After changing files, review the scoped diff and working-tree status, then run verification appropriate to the change.
- Do not initialize a nested Git repository. Create branches, stage, commit, or push only when the task requires those actions.

## Development Environment

- The user's IDE is JetBrains Rider, not Visual Studio. Use Rider-oriented instructions, project-opening steps, and debugging workflows by default.
- Do not assume the Visual Studio IDE is installed or available. Windows Unreal builds may still use the Microsoft C++ compiler and SDK toolchain when required by Unreal Build Tool.

## Communication

- Default to concise, summary-first responses.
- Unless the user explicitly requests detailed analysis, a full plan, exhaustive explanation, or long-form tutorial content, answer only with the key conclusion and necessary next actions.
- Do not repeat background, prior reasoning, or large outlines when a short answer is sufficient.
- Ask or explain further only when missing information materially blocks the task.

## Teaching Documents

- The concise-response rule does not apply to formal tutorial content under `Engine/Docs/Tutorial_003_RenderingPractice/`.
- RenderingPractice is teaching material for an experienced programmer and Unity TA who is new to Unreal Engine. Keep general programming and graphics explanations compact, but explain Unreal-specific concepts, editor operations, project settings, terminology, defaults, and consequences in detail.
- Do not present a sequence of choices as unexplained instructions. For every consequential option or parameter, explain what it controls, why the course selects it, what tradeoffs or side effects it introduces, what alternatives exist, and when the choice should be revisited.
- Support important teaching claims with the most relevant available evidence: UE 5.7 behavior, source code, official documentation, RenderingDeep chapters, or a controlled practical observation. Do not add references merely for appearance.
- A tutorial step is complete only when the reader knows how to perform it, how to verify success, why it is needed, and how it affects rendering, memory, streaming, build time, platform support, or later course work where relevant.
