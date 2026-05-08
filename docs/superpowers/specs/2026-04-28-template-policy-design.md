# Template Policy Design

## Summary
Add `template_policy` to `profile.json` to let users control how generated config sections are merged into target templates. The policy is applied consistently across Mihomo, sing-box, and Xray via a shared strategy engine. Actions are `replace`, `append`, `merge`, and `reject` with precise, path-scoped semantics. `reject` preserves the template content and emits a warning per rejected path.

## Goals
- Allow profile authors to control template merge behavior per target and path.
- Keep templates clean: no embedded directives inside template files.
- Preserve existing behavior when `template_policy` is absent.
- Emit warnings instead of failing when `reject` preserves template content.

## Non-Goals
- JSONPath/YAMLPath selectors beyond fixed, supported paths.
- Runtime core integration or GUI features.
- Auto-merging of arbitrary rule arrays beyond supported targets.

## Policy Model
`profile.json` gains a top-level field:

```json
{
  "template_policy": {
    "targets": {
      "mihomo": {
        "paths": {
          "rules": "reject"
        }
      }
    }
  }
}
```

### Actions
- `replace`: discard template content and use generated content.
- `append`: append generated items to template array.
- `merge`: merge objects or keyed arrays (keyed by `tag` or `name`).
- `reject`: do not generate the field, preserve template content, emit warning.

### Warnings
Each rejected path emits one warning:
- code: `template_policy_reject_preserved`
- message: `template_policy rejected generated field '<path>'; preserved template content`

## Supported Targets and Paths
Supported paths are fixed and validated. Unsupported paths or actions cause `profile validate` to fail.

### sing-box
- `outbounds`: `merge` (key: `tag`), `replace`, `append`, `reject`
- `dns.servers`: `merge` (key: `tag`), `replace`, `append`, `reject`
- `dns.rules`: `replace`, `append`, `reject`
- `route.rules`: `replace`, `append`, `reject`
- `route.rule_set`: `merge` (key: `tag`), `replace`, `append`, `reject`

Merge detail for `dns.servers`: keep template entries without `tag`; merge by `tag` when present; append new entries.

### Xray
- `outbounds`: `merge` (key: `tag`), `replace`, `append`, `reject`
- `routing.balancers`: `merge` (key: `tag`), `replace`, `append`, `reject`
- `dns.servers`: `replace`, `append`, `reject` (no `merge` in v1)
- `routing.rules`: `replace`, `append`, `reject`

### Mihomo
- `proxies`: `merge` (key: `name`), `replace`, `append`, `reject`
- `proxy-groups`: `merge` (key: `name`), `replace`, `append`, `reject`
- `dns`: `merge` (map merge), `replace`, `reject`
- `rules`: `replace`, `append`, `reject`

## Defaults
If `template_policy` is absent, exporters keep current behavior. If a path has no explicit action, target defaults apply:

- sing-box: `outbounds`/`dns.servers`/`route.rule_set` default to `merge`, rule arrays default to `replace`.
- Xray: `outbounds`/`routing.balancers` default to `merge`, rule arrays and dns default to `replace`.
- Mihomo: `proxies`/`proxy-groups`/`dns` default to `merge`, `rules` default to `replace`.

## Implementation Plan (High-Level)
1. Extend profile model and loader to parse `template_policy` and validate target/path/action.
2. Add shared policy engine in `exporter_common` with JSON/YAML apply helpers.
3. Integrate into sing-box exporter with per-path actions.
4. Integrate into Xray exporter with per-path actions.
5. Integrate into Mihomo exporter with per-path actions.
6. Enhance `profile validate` and `template validate` commands.
7. Update docs and add examples.
8. Run full build and tests.

## Testing Strategy
- Add profile loader tests for `template_policy`.
- Add exporter tests for each target to validate replace/append/merge/reject per path.
- Validate warnings when `reject` preserves template content.
- Run full build and `ctest`.
