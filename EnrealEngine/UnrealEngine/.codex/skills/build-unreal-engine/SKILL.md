---
name: build-unreal-engine
description: Build and diagnose Unreal Engine source targets in this UE 5.7 workspace. Use when Codex must compile or rebuild UnrealEditor, UnrealBuildTool, an engine module, or a project target; repair stale source/binary mismatches after repository synchronization; investigate UBT, compiler, linker, SDK, or toolchain failures; or verify that newly built editor binaries can load engine plugins.
---

# Build Unreal Engine

Build UE targets through the repository's `Engine/Build/BatchFiles/Build.bat`, preserve user changes, and verify runtime startup after successful compilation.

## Workflow

1. Read the applicable `AGENTS.md` and inspect `git status --short`. Never revert unrelated changes.
2. Confirm the engine root contains `Engine/Build/BatchFiles/Build.bat` and `Engine/Build/Build.version`.
3. Identify the required target, platform, and configuration. Default engine editor work to `UnrealEditor Win64 Development`.
4. Check that no `UnrealEditor` process is running. Ask the user to close it rather than terminating it unless they explicitly authorize termination.
5. Run `scripts/build.ps1` from this skill. Use an incremental build by default; pass `-Clean` only when the user explicitly requests a clean build or evidence shows incremental state is invalid.
6. Keep monitoring long builds. Report the current phase periodically, and wait until the process exits.
7. On failure, find the first actionable UBT/compiler/linker error. Treat later cancellation, pipe, socket, and missing-PDB messages as secondary unless no earlier error exists.
8. On success, verify relevant binary timestamps and run the startup smoke test below when the task concerns editor/plugin loading.

## Build Commands

From the engine root:

```powershell
& .\.codex\skills\build-unreal-engine\scripts\build.ps1
```

Override the target or pass additional UBT arguments only when needed:

```powershell
& .\.codex\skills\build-unreal-engine\scripts\build.ps1 `
  -Target ShaderCompileWorker `
  -ExtraArgs @('-Verbose')
```

Do not invoke Visual Studio or MSBuild directly for UE targets unless UBT specifically requires diagnosing a generated project issue.

## Source/Binary Mismatch

When tracked source or plugin descriptors changed while ignored `Engine/Binaries` remained in place, rebuild the complete owning target. For plugin descriptor parsing, build `UnrealEditor`, not only `UnrealEditor-Projects.dll`, so UBT can update all affected dependencies consistently.

Do not edit valid current-source descriptors merely to accommodate stale binaries unless the user explicitly chooses a temporary compatibility workaround.

## Verification

After building `UnrealEditor`, inspect at least:

```powershell
Get-Item Engine\Binaries\Win64\UnrealEditor.exe,
  Engine\Binaries\Win64\UnrealEditor-Projects.dll |
  Select-Object FullName, Length, LastWriteTime
```

For an engine-only startup check, run:

```powershell
& .\.codex\skills\build-unreal-engine\scripts\smoke-test-editor.ps1
```

Require exit code `0` and inspect output for `LogPluginManager: Error`, `Fatal error`, assertions, or unhandled exceptions. A successful compile without this smoke test is insufficient when the original problem occurred during plugin discovery or editor startup.

## Repository Hygiene

- Do not stage generated binaries, `Intermediate`, `Saved`, DDC, IDE state, or `.codex/tmp` logs.
- Do not delete build products or run broad clean commands without explicit justification.
- Keep engine refreshes and authored documentation/project changes separate.
- Report generated outputs as verification artifacts, not source changes.
