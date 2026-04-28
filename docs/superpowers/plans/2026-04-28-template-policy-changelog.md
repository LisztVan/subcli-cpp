# Template Policy V1 Stabilization Changelog

## Scope

Stabilization-focused completion of V1 template policy support for profile-driven export across Mihomo, sing-box, and Xray.

## What Changed

- Added `template_policy` model to `ResolvedProfile` and loader parsing/validation.
- Introduced shared template policy action model (`replace`, `append`, `merge`, `reject`) and centralized target/path/action compatibility checks.
- Unified validation paths so loader and `profile validate` use the same support matrix.
- Added parent-child path conflict detection in profile policy parsing (for example `dns` with `dns.nameserver`).
- Integrated explicit `template_policy` behavior into all three exporters:
  - sing-box paths: `outbounds`, `dns.servers`, `dns.rules`, `route.rules`, `route.rule_set`
  - Xray paths: `outbounds`, `dns.servers`, `routing.rules`, `routing.balancers`
  - Mihomo paths: `proxies`, `proxy-groups`, `dns`, `dns.nameserver`, `dns.fallback`, `rules`
- Standardized reject warning behavior using code `template_policy_reject_preserved`.
- Strengthened `template validate` to require parseable target formats:
  - Mihomo template root must be YAML map
  - sing-box/Xray template root must be JSON object
- Updated profile and README docs to document `template_policy`, action semantics, and V1 constraints.

## Test Coverage Added/Extended

- Loader tests for unknown target/path/action and action/path compatibility.
- Conflict test for parent-child path declarations.
- Export tests for reject/merge/append behavior across sing-box/Xray/Mihomo.
- Cross-target parseability regression test for template policy exports.
- CLI smoke validation for invalid template JSON and `template validate --json` output fields (`failed`, `parse_ok`, `error`).

## Stability Outcomes

- Unsupported merge/action combinations are rejected early during profile validation.
- Reject behavior is non-fatal and preserves template content as designed.
- Export outputs remain parseable for all three targets under template policy usage.
- Full test suite and release-gate validation commands pass.
