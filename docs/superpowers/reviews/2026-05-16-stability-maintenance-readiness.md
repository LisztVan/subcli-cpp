# Stability Maintenance Readiness

**Branch:** `feat/stability-maintenance`

**Version policy:** No version bump.

## Local Verification

Command:

```bash
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_OPENSSL=/home/lisztzy/prj/subcli-cpp/build/_deps/openssl-src \
  -DFETCHCONTENT_SOURCE_DIR_CURL=/home/lisztzy/prj/subcli-cpp/build/_deps/curl-src \
  -DFETCHCONTENT_SOURCE_DIR_YAML-CPP=/home/lisztzy/prj/subcli-cpp/build/_deps/yaml-cpp-src \
  -DFETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON=/home/lisztzy/prj/subcli-cpp/build/_deps/nlohmann_json-src \
  -DFETCHCONTENT_SOURCE_DIR_CLI11=/home/lisztzy/prj/subcli-cpp/build/_deps/cli11-src
cmake --build build -j
cmake --build build --target package
ctest --test-dir build --output-on-failure
```

Result:

```text
CPack: - package: /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance/build/subcli-0.2.7-Linux-x86_64.tar.gz generated.
CPack: - package: /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance/build/subcli-0.2.7-Linux-x86_64.zip generated.
Test project /home/lisztzy/prj/subcli-cpp/.worktrees/stability-maintenance/build
    Start 1: subcli_tests
1/6 Test #1: subcli_tests .......................   Passed    1.47 sec
    Start 2: subcli_cli_basic
2/6 Test #2: subcli_cli_basic ...................   Passed    0.08 sec
    Start 3: subcli_stability_user_journey
3/6 Test #3: subcli_stability_user_journey ......   Passed    9.18 sec
    Start 4: subcli_stability_package_journey
4/6 Test #4: subcli_stability_package_journey ...   Passed    9.29 sec
    Start 5: subcli_platform_boundary_scan
5/6 Test #5: subcli_platform_boundary_scan ......   Passed    0.01 sec
    Start 6: subcli_cli_smoke
6/6 Test #6: subcli_cli_smoke ...................   Passed    0.58 sec

100% tests passed, 0 tests failed out of 6
```

Note: local configure used `FETCHCONTENT_SOURCE_DIR_*` paths from the already-populated root build cache because direct GitHub FetchContent clone attempts failed earlier with network/TLS connection errors before compilation. CI should use normal networked FetchContent.

## CI Verification

Workflow run:

```text
https://github.com/LisztVan/subcli-cpp/actions/runs/25990761591
```

Head SHA:

```text
d64dbbebe030fc686be780125251d06ab1fcd3f2
```

Results:

- Linux x86_64: success
- macOS arm64: success
- Windows x86_64: success

## Coverage Summary

- `subcli init [DIR]` initializes and remembers the default workspace.
- `subcli workspace init [DIR]` initializes and remembers the default workspace.
- Build-binary user journey passed.
- Package-extraction user journey passed.
- Local HTTP subscription simulation passed.
- HTTP error, empty, malformed, slow, and Unicode boundary cases passed.
- Help text first-use checks passed.
- Chinese command glossary exists.
- Platform boundary scan passed.
