# Subcli v2 Ecosystem Workspace Design

## Summary
Subcli v2 phase 1 introduces a cross-platform environment model that upgrades the current CLI into a portable, migratable subscription and strategy generation ecosystem. The design keeps zero-friction defaults for existing users while adding an explicit workspace mode for teams and advanced users.

## Goals
- Build a stable, cross-platform environment layout for subscriptions, profiles, templates, assets, outputs, cache, and state.
- Preserve existing user workflows (`init`, `sub`, `profile`, `template`, `asset`, `export`) without mandatory migration steps.
- Add explicit workspace lifecycle commands so users can create/share/migrate isolated environments.
- Keep compatibility with current v1 data and support low-risk migration.
- Provide deterministic path resolution behavior across Linux, macOS, and Windows.

## Non-Goals
- GUI/TUI strategy editing in phase 1.
- v2rayN/Clash Verge import in phase 1.
- Profile schema major version bump in phase 1.
- Runtime/daemon architecture redesign in phase 1.

## Design Principles
- Default-simple, power-user-capable.
- Environment is explicit and inspectable.
- No silent writes to unexpected locations.
- Migration is additive and reversible.
- Cache/state are ephemeral; policy/config are durable.

## Environment Model
Subcli resolves one active environment per command invocation.

Logical environment tree:

```text
subcli environment
├── config.yaml
├── sub.yaml
├── profiles/
├── templates/
├── assets/
├── cache/
├── outputs/
└── state/
```

Durable policy/config data:
- `config.yaml`
- `sub.yaml`
- `profiles/`
- `templates/`
- `assets/`

Ephemeral/runtime data:
- `cache/`
- `outputs/`
- `state/`

## Path Resolution Priority
For every command, resolve environment in this order:

1. `--workspace <dir>` command option.
2. `SUBCLI_WORKSPACE=<dir>` environment variable.
3. Workspace marker discovery from current directory up to filesystem root.
4. User-selected default workspace (set via `workspace use`).
5. Platform default environment directories.

Behavior constraints:
- If a higher-priority workspace is invalid, command fails with actionable diagnostics.
- Do not silently fall back to lower-priority roots after an explicit override.
- `doctor` surfaces full resolution trace in text and JSON output.

## Workspace Markers
Two marker files are supported:
- `.subcli-workspace` (presence marker)
- `subcli.env.yaml` (optional metadata)

`subcli.env.yaml` optional fields:
- `version`
- `name`
- `created_at`
- `description`

Marker discovery uses parent traversal from current working directory.

## Cross-Platform Default Directories
When no explicit workspace is active, subcli uses platform-native directories.

Linux:
- config: `${XDG_CONFIG_HOME:-~/.config}/subcli`
- data: `${XDG_DATA_HOME:-~/.local/share}/subcli`
- cache: `${XDG_CACHE_HOME:-~/.cache}/subcli`
- state: `${XDG_STATE_HOME:-~/.local/state}/subcli`

macOS:
- base: `~/Library/Application Support/subcli`
- cache: `~/Library/Caches/subcli`
- state: `~/Library/Application Support/subcli/state`

Windows:
- base: `%APPDATA%\\subcli`
- cache: `%LOCALAPPDATA%\\subcli\\cache`
- state: `%LOCALAPPDATA%\\subcli\\state`

Notes:
- Path normalization must preserve Unicode-safe behavior and platform separators.
- CLI path arguments (`--output-dir`, `--file`, template/profile paths) remain resolved from shell cwd unless explicitly documented otherwise.

## Configuration Roles
`config.yaml` remains app-level settings only:
- core paths
- network settings
- selected profile/profile_path
- default output/template/asset directories
- node management options

Policy stays in `profiles/*.json`.

This separation keeps strategy logic portable while avoiding policy drift in `config.yaml`.

## New Command Surface
Add `workspace` command group:

```text
subcli workspace init [DIR]
subcli workspace status [--json]
subcli workspace use DIR
subcli workspace unset
subcli workspace migrate [--to DIR]
subcli workspace doctor
```

Semantics:
- `init`: create environment tree and marker files; write defaults if missing.
- `status`: show active environment source (flag/env/marker/default), resolved paths, and validity.
- `use`: persist a default workspace selection in user config scope.
- `unset`: remove persisted default workspace.
- `migrate`: import v1 environment data into v2 layout.
- `doctor`: workspace-specific health checks (paths, files, required directories, writeability).

## Existing Command Compatibility
All existing commands continue to work, now against resolved active environment:
- `init`, `doctor`
- `sub ...`
- `config ...`
- `profile ...`
- `template ...`
- `asset ...`
- `export ...`
- `run/status/stop/restart/check/daemon`

No command should require users to understand workspace internals for default usage.

## Migration Strategy
Introduce `subcli workspace migrate` for v1 -> v2 transition.

Migration steps:
1. Detect source v1 paths (current configured roots and legacy defaults).
2. Create target v2 environment structure.
3. Copy durable data (`config.yaml`, `sub.yaml`, `profiles`, `templates`, `assets`) with conflict policy.
4. Optionally copy `outputs` (default on), skip `cache/state` by default.
5. Write migration report (human + JSON).
6. Leave source unchanged.

Conflict policy:
- Default: fail on conflict unless `--overwrite` is provided.
- Provide dry-run mode in CLI plan (`--dry-run`) for safe preview.

## Data Versioning
Environment metadata carries explicit version (`env_version: 2`) in marker/config metadata.

Version handling:
- v2 understands v1 data and can migrate.
- Future versions should use explicit upgrader functions, not ad-hoc in command paths.

## Error Handling
Rules:
- Explicit override invalid -> fail fast with fix hints.
- Missing optional dirs (`cache`, `outputs`, `state`) -> auto-create on demand.
- Missing required files:
  - `config.yaml`/`sub.yaml` absent in initialized env -> create defaults.
  - `profiles` or `templates` absent -> warn in `doctor`, fail in commands that require them.
- Permission errors always include offending path and attempted operation.

## Observability
Add environment diagnostics to:
- `doctor --json`
- `workspace status --json`

Include:
- resolution_source
- active_workspace_root
- resolved_path_map
- validation findings

## Implementation Units
1. `env_resolver` module
   - parses flags/env/marker/default
   - returns resolved environment object + source metadata
2. `workspace_commands` module
   - init/status/use/unset/migrate/doctor
3. `env_layout` module
   - ensure directory/file defaults
4. `migrator` module
   - v1 detection and copy/report logic
5. Integration updates in existing command handlers
   - swap global path initialization to resolver output

## Testing Strategy
Unit tests:
- path priority ordering
- marker traversal
- platform path mapping
- explicit override failure behavior
- migration conflict handling

Integration/smoke tests:
- `workspace init/status/use/unset`
- `workspace migrate` from fixture v1 data
- `export` success in explicit workspace
- `doctor --json` includes environment diagnostics

Regression tests:
- existing profile/template/export behavior unchanged under default environment

## Risks and Mitigations
- Risk: path behavior regressions in existing commands.
  - Mitigation: resolver test matrix + smoke coverage for high-traffic commands.
- Risk: silent writes to unintended location.
  - Mitigation: explicit source tracing and strict failure on invalid overrides.
- Risk: migration confusion.
  - Mitigation: dry-run report, no destructive operations, unchanged source data.

## Rollout Plan
Phase 1 (this spec):
- environment resolver + workspace commands + migration + docs + tests.

Phase 2:
- strategy authoring UX improvements on top of stable environment model.

Phase 3:
- ecosystem interoperability (importers, richer policy tooling).

## Acceptance Criteria
- User can run `subcli` without workspace and preserve current behavior.
- User can initialize and select a workspace and see deterministic path switching.
- User can migrate v1 data to v2 layout without data loss.
- Existing export/profile/template workflows pass tests in both default and workspace modes.
- `doctor --json` and `workspace status --json` expose path resolution source and diagnostics.
