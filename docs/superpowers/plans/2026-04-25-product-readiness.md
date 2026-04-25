# Product Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Improve `subcli` product usability with machine-readable output, shell completion, safer fetch behavior, clearer diagnostics, and focused CLI maintainability.

**Architecture:** Add small focused support modules instead of expanding `src/main.cpp`. Keep existing command behavior stable, then layer `--json`, completion generation, fetch limits, and improved diagnostics behind minimal internal APIs.

**Tech Stack:** C++17, CMake, yaml-cpp, nlohmann_json, libcurl, POSIX process checks, existing custom test binary plus `ctest`.

---

## Task Order

- [x] Add JSON output foundation.
- [x] Add `--json` to read-only commands.
- [x] Add Bash completion command.
- [x] Add safer fetch policy.
- [x] Improve `doctor` diagnostics JSON.
- [x] Document stable exit codes.
- [x] Update docs and run final verification.

## Verification Command

Run after each task:

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Scope Notes

This plan prioritizes user-visible product readiness. It avoids a full `src/main.cpp` rewrite and only adds small modules where needed for output and completion.
