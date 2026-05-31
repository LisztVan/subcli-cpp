#!/usr/bin/env bash
set -euo pipefail

WORKFLOW="release-validation.yml"
REF="${1:-$(git branch --show-current)}"

if ! command -v gh >/dev/null 2>&1; then
  echo "error: GitHub CLI 'gh' is not installed or not on PATH" >&2
  echo "install: https://cli.github.com/" >&2
  exit 127
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated" >&2
  echo "run: gh auth login" >&2
  exit 1
fi

if [[ -z "$REF" ]]; then
  echo "error: could not determine git ref; pass branch/tag as first argument" >&2
  echo "usage: scripts/verify-cross-platform.sh [ref]" >&2
  exit 2
fi

echo "=== Triggering $WORKFLOW on ref: $REF ==="
gh workflow run "$WORKFLOW" --ref "$REF"

echo "Waiting for GitHub to create a run..."
sleep 8

RUN_ID=""
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  RUN_ID="$(gh run list --workflow="$WORKFLOW" --branch "$REF" --limit=1 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)"
  if [[ -n "$RUN_ID" && "$RUN_ID" != "null" ]]; then
    break
  fi
  echo "Run not visible yet, retry $attempt/10..."
  sleep 5
done

if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
  echo "error: could not find a workflow run for $WORKFLOW on $REF" >&2
  gh run list --workflow="$WORKFLOW" --limit=10 || true
  exit 1
fi

echo "=== Watching run: $RUN_ID ==="
gh run watch "$RUN_ID"

echo "=== Run summary ==="
gh run view "$RUN_ID"

CONCLUSION="$(gh run view "$RUN_ID" --json conclusion -q '.conclusion')"
if [[ "$CONCLUSION" != "success" ]]; then
  echo "error: cross-platform validation failed with conclusion: $CONCLUSION" >&2
  echo "=== Failed logs ===" >&2
  gh run view "$RUN_ID" --log-failed >&2 || true
  exit 1
fi

echo "success: Linux, macOS, and Windows validation passed"
