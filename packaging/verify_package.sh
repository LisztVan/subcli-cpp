#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <subcli-package.tar.gz>" >&2
  exit 2
fi

pkg="$1"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

tar -xzf "$pkg" -C "$tmp"
bin="$(find "$tmp" -type f -path '*/bin/subcli' | head -n 1)"
if [[ -z "$bin" ]]; then
  echo "subcli binary not found in package" >&2
  exit 1
fi

export XDG_CONFIG_HOME="$tmp/xdg/config"
export XDG_DATA_HOME="$tmp/xdg/data"
export XDG_CACHE_HOME="$tmp/xdg/cache"
export XDG_STATE_HOME="$tmp/xdg/state"

"$bin" --help >/dev/null
"$bin" init >/dev/null
"$bin" doctor
