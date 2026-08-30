# Testlib

A C++20 library scaffolded with CMake presets, GoogleTest, package install
checks, and CheckFlow quality gates.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`debug` uses an unoptimized build. Run the demo with
`build/apps/demo/testlib_demo` (`testlib_demo.exe` on Windows).

Core is static by default. To select a shared library, use a separate build with
`-DBUILD_SHARED_LIBS=ON`. `BUILD_TESTING=OFF` disables tests. The project does
not enable CTest when embedded with `add_subdirectory()`.

Installation testing is opt-in (`-DTESTLIB_BUILD_INSTALL_TEST=ON`) and
requires an uninstrumented build.

## Use as a CMake package

```sh
cmake --install build --prefix <install-prefix>
```

```cmake
find_package(Testlib CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE Testlib::Core)
```

```cpp
#include <testlib/core/core.hpp>
```

## Quality gates

```sh
checkflow fast
checkflow hardening
checkflow full
```
