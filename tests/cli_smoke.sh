#!/usr/bin/env bash
set -euo pipefail

bin="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

export XDG_CONFIG_HOME="$tmp/config"
export XDG_DATA_HOME="$tmp/data"
export XDG_CACHE_HOME="$tmp/cache"
export XDG_STATE_HOME="$tmp/state"

"$bin" init >/dev/null
"$bin" template validate >/dev/null

cp "$($bin template get mihomo normal)" "$tmp/valid-mihomo.yaml"
cp "$($bin template get sing-box normal)" "$tmp/valid-singbox.json"
cp "$($bin template get xray normal)" "$tmp/valid-xray.json"
"$bin" template set mihomo normal "$tmp/valid-mihomo.yaml" >/dev/null
"$bin" template set sing-box normal "$tmp/valid-singbox.json" >/dev/null
"$bin" template set xray normal "$tmp/valid-xray.json" >/dev/null

echo '{invalid json' >"$tmp/invalid-singbox.json"
"$bin" template set sing-box normal "$tmp/invalid-singbox.json" >/dev/null
"$bin" template validate >/dev/null 2>&1 && exit 1 || true
template_validate_json="$($bin template validate --json 2>/dev/null || true)"
if [[ "$template_validate_json" != *'"failed":1'* || "$template_validate_json" != *'"parse_ok":false'* || "$template_validate_json" != *'"error":"template JSON parse failed"'* ]]; then
    printf '%s\n' "$template_validate_json"
    exit 1
fi
"$bin" template set sing-box normal "$tmp/valid-singbox.json" >/dev/null

profile_list="$($bin profile list)"
if [[ "$profile_list" != *"bypass-cn"* || "$profile_list" != *"global"* || "$profile_list" != *"direct"* ]]; then
    printf '%s\n' "$profile_list"
    exit 1
fi
"$bin" profile validate profiles/bypass-cn.json >/dev/null
invalid_profile="$tmp/invalid-template-policy.json"
cat >"$invalid_profile" <<'JSON'
{
  "version": 1,
  "name": "invalid-policy",
  "template_policy": {
    "targets": {
      "sing-box": {
        "paths": {
          "route.rules": "merge"
        }
      }
    }
  }
}
JSON
"$bin" profile validate "$invalid_profile" >/dev/null 2>&1 && exit 1 || true
profile_explain="$($bin profile explain bypass-cn)"
if [[ "$profile_explain" != *"profile: bypass-cn"* || "$profile_explain" != *"rules:"* ]]; then
    printf '%s\n' "$profile_explain"
    exit 1
fi
profile_explain_target="$($bin profile explain bypass-cn --target sing-box)"
if [[ "$profile_explain_target" != *"target: sing-box"* || "$profile_explain_target" != *"required_assets:"* ]]; then
    printf '%s\n' "$profile_explain_target"
    exit 1
fi
profile_explain_all="$($bin profile explain bypass-cn --target all)"
if [[ "$profile_explain_all" != *"target: mihomo"* || "$profile_explain_all" != *"target: sing-box"* || "$profile_explain_all" != *"target: xray"* ]]; then
    printf '%s\n' "$profile_explain_all"
    exit 1
fi
profile_explain_json="$($bin profile explain bypass-cn --json)"
if [[ "$profile_explain_json" != *'"profile"'* || "$profile_explain_json" != *'"name":"bypass-cn"'* ]]; then
    printf '%s\n' "$profile_explain_json"
    exit 1
fi
"$bin" profile explain bypass-cn --target unknown >/dev/null 2>&1 && exit 1 || true

extends_profile="$tmp/extends-profile.json"
cat >"$extends_profile" <<'JSON'
{
  "version": 1,
  "name": "extends-check",
  "extends": "bypass-cn",
  "default_outbound": "DIRECT"
}
JSON
extends_explain="$($bin profile explain "$extends_profile")"
if [[ "$extends_explain" != *"extends: bypass-cn"* ]]; then
    printf '%s\n' "$extends_explain"
    exit 1
fi

profile_json="$($bin profile get bypass-cn)"
if [[ "$profile_json" != *'"name"'* || "$profile_json" != *'"bypass-cn"'* ]]; then
    printf '%s\n' "$profile_json"
    exit 1
fi
"$bin" profile get unknown >/dev/null 2>&1 && exit 1 || true

export_help="$($bin export --help)"
if [[ "$export_help" != *"--profile"* || "$export_help" != *"--download-assets"* || "$export_help" != *"--explain-policy"* ]]; then
    printf '%s\n' "$export_help"
    exit 1
fi

config_json="$($bin config list --json)"
if [[ "$config_json" != *'"output_dir"'* ]]; then
    printf '%s\n' "$config_json"
    exit 1
fi

profile_path="$tmp/profile.json"
"$bin" config set profile_path "$profile_path" >/dev/null
actual_profile_path="$($bin config get profile_path)"
if [[ "$actual_profile_path" != *"$profile_path"* ]]; then
    printf 'expected profile_path to contain %s, got %s\n' "$profile_path" "$actual_profile_path"
    exit 1
fi
"$bin" config remove profile_path >/dev/null
removed_profile_path="$($bin config get profile_path)"
if [[ -n "$removed_profile_path" ]]; then
    printf 'expected removed profile_path to be empty, got %s\n' "$removed_profile_path"
    exit 1
fi

sub_json="$($bin sub list --json)"
if [[ "$sub_json" != *'"subscriptions"'* ]]; then
    printf '%s\n' "$sub_json"
    exit 1
fi

template_json="$($bin template list --json)"
if [[ "$template_json" != *'"templates"'* ]]; then
    printf '%s\n' "$template_json"
    exit 1
fi

doctor_json="$({ "$bin" doctor --json; } 2>&1 || true)"
if [[ "$doctor_json" != *'"checks"'* ]]; then
    printf '%s\n' "$doctor_json"
    exit 1
fi

completion="$($bin completion bash)"
if [[ "$completion" != *"_subcli_completion"* ]]; then
    printf '%s\n' "$completion"
    exit 1
fi

"$bin" sub add --name help --url "file://$tmp/missing" --force --header 'User-Agent: subcli-test' >/dev/null
"$bin" sub validate missing >/dev/null 2>&1 && exit 1 || true
"$bin" sub remove help extra >/dev/null 2>&1 && exit 1 || true
set +e
"$bin" export mihomo extra >/dev/null 2>&1
code=$?
set -e
if [[ "$code" -ne 2 ]]; then
    printf 'expected usage exit code 2 for invalid export usage, got %s\n' "$code"
    exit 1
fi
"$bin" check xray extra >/dev/null 2>&1 && exit 1 || true
"$bin" config set timeout 0 >/dev/null 2>&1 && exit 1 || true
"$bin" sub edit help --header 'Authorization=Bearer test' --remove-header User-Agent >/dev/null

"$bin" sub add --name tagged --url "file://$tmp/missing" --force --tag hk >/dev/null
out="$({ "$bin" sub update --tag hk; } 2>&1 || true)"
if [[ "$out" == *"no subscriptions selected"* ]]; then
    printf '%s\n' "$out"
    exit 1
fi
if [[ "$out" != *"update failed for tagged"* ]]; then
    printf '%s\n' "$out"
    exit 1
fi

printf '%s\n' 'trojan://password@1.2.3.4:443#HK-Node' > "$tmp/valid-sub.txt"
"$bin" sub add --name explain --url "file://$tmp/valid-sub.txt" --force >/dev/null
explain_export="$({ "$bin" export mihomo --profile bypass-cn --sub explain --explain-policy; } 2>&1 || true)"
if [[ "$explain_export" != *"policy explain: profile=bypass-cn"* || "$explain_export" != *"policy explain: target=mihomo"* ]]; then
    printf '%s\n' "$explain_export"
    exit 1
fi
if [[ "$explain_export" != *"mihomo capability summary:"* ]]; then
    printf '%s\n' "$explain_export"
    exit 1
fi

export_json="$({ "$bin" export mihomo --profile bypass-cn --sub explain --json; } 2>/dev/null || true)"
if [[ "$export_json" != *'"summary"'* || "$export_json" != *'"targets"'* || "$export_json" != *'"capabilities"'* || "$export_json" != *'"findings"'* ]]; then
    printf '%s\n' "$export_json"
    exit 1
fi

strict_export_json="$({ "$bin" export all --profile bypass-cn --sub explain --strict-capabilities --json; } 2>/dev/null || true)"
if [[ "$strict_export_json" != *'"strict_capabilities_blocked":true'* || "$strict_export_json" != *'"violations"'* ]]; then
    printf '%s\n' "$strict_export_json"
    exit 1
fi

"$bin" export all --profile bypass-cn --sub explain --strict-capabilities >/dev/null 2>&1 && exit 1 || true
