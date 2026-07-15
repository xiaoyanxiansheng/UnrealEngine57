# Unreal Workspace Rules

## Repository Boundary

- `D:/Unreal` is the only Git repository root for this workspace.
- Do not initialize nested Git repositories under this directory.
- Keep scoped `.gitignore` files. They are path-local rules, not repository boundaries.

## Directory Ownership

- `EnrealEngine/UnrealEngine/` is the Unreal Engine 5.7 source snapshot. Change it only for deliberate engine work.
- `EnrealEngine/UnrealEngine/Engine/Docs/` is first-party tutorial content. Documentation work belongs here even though it is physically inside the engine tree.
- `EnrealEngine/Project/` is reserved for first-party Unreal projects. Track project source, configuration, and authored assets; ignore generated build data.
- `Examples/` contains third-party reference projects and is excluded from Git in full.

## Unreal Engine Updates

- Engine updates are rare and must stay on the UE 5.7 line unless explicitly requested otherwise.
- Prepare updates in a temporary EpicGames/UnrealEngine checkout, then synchronize the engine snapshot into this workspace.
- Preserve first-party paths while synchronizing: `Engine/Docs/`, `.claude/`, `.codex/`, this engine tree's `AGENTS.md`, and the workspace-local blocks at the end of its `.gitignore` and `.gitattributes`.
- Do not import generated directories, downloaded IDE plugins, machine-local configuration, or build products.
- Update `UPSTREAM.md` with the new Epic commit before committing an engine refresh.
- Review the full engine diff separately from documentation and project changes.

## Content Rules

- Never commit generated `Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`, IDE state, or `.codex/tmp/` output. Files already tracked by the recorded Epic engine baseline are the exception.
- Do not globally ignore `Content/` or `Build/`; first-party projects can contain authored assets and platform configuration there.
- Do not commit machine-specific tokens or credentials.
