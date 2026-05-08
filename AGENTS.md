# AGENTS.md

## Build And Verify
- please use `build` name to cmake configure
- Build with `cmake -S . -B build && cmake --build build -j`.
- Run tests with `ctest --test-dir build --output-on-failure` after building.
- First configure is networked: `CMakeLists.txt` pulls `yaml-cpp`, `nlohmann_json`, `openssl-cmake`, and `curl` via `FetchContent`.
