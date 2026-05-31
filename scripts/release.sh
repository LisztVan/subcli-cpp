#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

WORKFLOW="release.yml"
REMOTE="${REMOTE:-origin}"
DRY_RUN="${DRY_RUN:-0}"

if ! command -v gh >/dev/null 2>&1; then
  echo "error: GitHub CLI 'gh' is not installed or not on PATH" >&2
  exit 127
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "error: gh is not authenticated" >&2
  echo "run: gh auth login" >&2
  exit 1
fi

VERSION="$(python3 - <<'PY'
import re
from pathlib import Path
text = Path('CMakeLists.txt').read_text(encoding='utf-8')
match = re.search(r'project\s*\(\s*subcli\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)', text, re.I)
print(match.group(1) if match else '')
PY
)"

if [[ -z "$VERSION" ]]; then
  echo "error: could not read project version from CMakeLists.txt" >&2
  exit 1
fi

TAG="v$VERSION"

echo "=== Release preparation ==="
echo "Version: $VERSION"
echo "Tag:     $TAG"
echo "Remote:  $REMOTE"

if ! git diff --quiet || ! git diff --cached --quiet; then
  echo "error: working tree has uncommitted changes" >&2
  git status --short >&2
  exit 1
fi

if git rev-parse "$TAG" >/dev/null 2>&1; then
  echo "error: tag already exists locally: $TAG" >&2
  exit 1
fi

if git ls-remote --tags "$REMOTE" "$TAG" | grep -q "$TAG"; then
  echo "error: tag already exists on remote $REMOTE: $TAG" >&2
  exit 1
fi

if [[ "$DRY_RUN" == "1" ]]; then
  echo "dry-run: would run: git tag -a $TAG -m 'Release $TAG'"
  echo "dry-run: would run: git push $REMOTE $TAG"
  echo "dry-run: would wait for workflow: $WORKFLOW"
  exit 0
fi

read -r -p "Create and push $TAG? Type 'yes' to continue: " CONFIRM
if [[ "$CONFIRM" != "yes" ]]; then
  echo "cancelled"
  exit 0
fi

git tag -a "$TAG" -m "Release $TAG"
git push "$REMOTE" "$TAG"

echo "Waiting for GitHub to create release workflow run..."
sleep 10

RUN_ID=""
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  RUN_ID="$(gh run list --workflow="$WORKFLOW" --limit=1 --json databaseId -q '.[0].databaseId' 2>/dev/null || true)"
  if [[ -n "$RUN_ID" && "$RUN_ID" != "null" ]]; then
    break
  fi
  echo "Run not visible yet, retry $attempt/10..."
  sleep 5
done

if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
  echo "error: could not find release workflow run" >&2
  exit 1
fi

gh run watch "$RUN_ID"
CONCLUSION="$(gh run view "$RUN_ID" --json conclusion -q '.conclusion')"
if [[ "$CONCLUSION" != "success" ]]; then
  echo "error: release workflow failed: $CONCLUSION" >&2
  gh run view "$RUN_ID" --log-failed >&2 || true
  exit 1
fi

echo "=== GitHub Release ==="
gh release view "$TAG"

echo "success: release published for $TAG"
