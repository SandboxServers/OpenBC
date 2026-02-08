# Phase 1 Build Plan - Key References

## Implementation File Order
1. Root CMakeLists.txt
2. CMakePresets.json
3. cmake/OpenBCVersion.cmake
4. cmake/CompilerWarnings.cmake
5. cmake/FetchDependencies.cmake
6. include/openbc/config.h.in
7. include/openbc/types.h (platform socket abstraction)
8. src/version.h.in
9. src/CMakeLists.txt (orchestrates subdirs, defines openbc_core + openbc_server)
10. src/engine/CMakeLists.txt
11. src/compat/CMakeLists.txt
12. src/network/CMakeLists.txt
13. src/scripting/CMakeLists.txt
14. vendor/flecs/ (download v4.1.4 single-header release)
15. tests/CMakeLists.txt
16. .github/workflows/build.yml
17. .github/workflows/release.yml
18. docker/Dockerfile.server + .dockerignore

## Target Dependency Chain
```
flecs_static + Python3::Python + ws2_32(Win)/pthread(Linux)
    |
openbc_engine -> openbc_compat -> openbc_scripting -> openbc_network
    |                |                |                    |
    +--------- openbc_core (static lib) ------------------+
                     |
              openbc_server (executable)
              openbc_tests (test executable)
```

## Critical CMake Patterns
- Use `Python3::Python` imported target, NOT raw variables
- flecs_static needs `wsock32 ws2_32` on Windows, `pthread` on Linux
- Suppress warnings on vendored code with `-w` (GCC/Clang) or `/W0` (MSVC)
- `CMAKE_EXPORT_COMPILE_COMMANDS=ON` for IDE support
- Default build type to Release for single-config generators

## BC Protocol Constants (from reverse engineering)
- Default port: 22101 (0x5655)
- Checksum opcodes: 0x20 (request), 0x21 (response), 0x22/0x23 (fail)
- Game opcodes: 0x00 (settings), 0x01 (status)
- GameSpy queries on same UDP socket (peek-based demux)
- 4 checksum rounds: App.pyc, Autoexec.pyc, ships/*.pyc, mainmenu/*.pyc
