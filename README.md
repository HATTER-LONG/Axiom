# Axiom

A minimal CMake project skeleton with a reusable `core` library and a `demo`
executable. The framework is intentionally empty so new modules can be added
incrementally.

[中文文档](README.zh-CN.md)

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

## Developer tools

Install the following tools yourself before using the matching preset or feature.
They are not downloaded by CPM.

| Tool                        | Used for                                                                                          | When required                                                             |
| --------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| CMake 3.25+ and Ninja       | Configure and build                                                                               | Always                                                                    |
| A C++20 compiler            | Build the project                                                                                 | Always                                                                    |
| LLVM/Clang                  | `clang++`, `clang-tidy`, `clang-format`, clangd, AddressSanitizer, and UndefinedBehaviorSanitizer | Recommended toolchain; required by the Clang, tidy, and sanitizer presets |
| cppcheck                    | Static analysis                                                                                   | `cppcheck` preset                                                         |
| Include-What-You-Use (IWYU) | Include analysis                                                                                  | `iwyu` and `static-analysis` presets                                     |
| Doxygen                     | API documentation                                                                                 | `-DAXIOM_BUILD_DOCS=ON`                                                   |
| ccache                      | Compiler cache                                                                                    | `ccache` preset                                                           |
| Lizard and Python 3.8+      | Cyclomatic-complexity analysis                                                                    | Run manually                                                              |

`clang-tidy` and `clang-format` are distributed as part of LLVM. `clang-format`
is an editor/command-line formatting tool and is not run automatically by a
CMake preset.

The sanitizer presets use LLVM's compiler and runtime. On Windows, the build
copies the AddressSanitizer runtime DLL next to the executable when available.

### IWYU and LLVM compatibility

IWYU is tightly coupled to LLVM/Clang internals. Use an IWYU release or branch
that matches the installed Clang version. For example, Clang 22 requires IWYU
0.26 or the `clang_22` branch. The `iwyu` and `static-analysis` presets expect
`include-what-you-use` to be available on `PATH`.

Official IWYU releases are source releases. Building it standalone requires an
LLVM development installation that exports `LLVMConfig.cmake`, in addition to
the Clang libraries and headers. The standard Windows LLVM installer used by
this project provides the compiler tools but not that CMake package, so it
cannot build IWYU standalone by itself. Use a full LLVM build/development
package, then build IWYU against it:

```sh
git clone --branch clang_21 https://github.com/include-what-you-use/include-what-you-use.git
cmake -S include-what-you-use -B iwyu-build -G Ninja -DCMAKE_PREFIX_PATH=<llvm-development-prefix> -DCMAKE_INSTALL_PREFIX=<iwyu-install-prefix>
cmake --build iwyu-build
cmake --install iwyu-build
```

Alternatively, build LLVM, Clang, compiler-rt, and IWYU in one tree. The
following reference configuration targets LLVM/Clang 22 and IWYU 0.26; replace
the source and install paths with local paths:

```sh
cmake -S <llvm-source-dir>/llvm -B <llvm-build-dir> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<llvm-install-prefix> -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" -DLLVM_ENABLE_RUNTIMES=compiler-rt -DLLVM_EXTERNAL_PROJECTS=iwyu -DLLVM_EXTERNAL_IWYU_SOURCE_DIR=<iwyu-source-dir> -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
cmake --build <llvm-build-dir>
cmake --install <llvm-build-dir>
```

### Lizard complexity analysis

Lizard is a Python-based complexity analyzer for C/C++ that does not require a
complete include graph. Install it with Python 3.8 or later:

```sh
python -m pip install lizard
```

Analyze the project sources and tests, using Lizard's default CCN threshold of
15:

```sh
lizard -l cpp src apps tests
```

Use `-C <limit>` to set a cyclomatic-complexity threshold. Functions above the
limit produce warnings and a non-zero exit status, making the command suitable
for local checks or CI:

```sh
lizard -l cpp -C 15 src apps tests
```

### CPM-managed dependencies

CPM downloads only project dependencies and CMake integrations into `.cache/`:

- GoogleTest 1.18.0, when `BUILD_TESTING=ON`;
- CPM.cmake itself when a CPM-managed feature is configured;
- the `cmake-scripts` sanitizer integration when a sanitizer preset is used;
- the `Ccache.cmake` integration when the `ccache` preset is used.

These downloads do not install the compiler, analysis tools, Doxygen, or the
`ccache` executable.

### Presets

```sh
cmake --workflow --preset test
cmake --workflow --preset tidy
cmake --build --preset iwyu
cmake --build --preset cppcheck
cmake --workflow --preset asan-ubsan
```

The `tidy` preset treats all clang-tidy diagnostics as errors.

The demo contains an intentional heap-use-after-free test path for sanitizer
validation:

```sh
cmake --workflow --preset asan-ubsan
./build-asan-ubsan/apps/demo/axiom_demo --memory-error
```

On Windows, run `build-asan-ubsan/apps/demo/axiom_demo.exe` instead. The
`--memory-error` argument is required; the default demo path is unaffected.

To test UBSan specifically:

```sh
./build-asan-ubsan/apps/demo/axiom_demo --ubsan-error
```

This triggers a signed integer overflow. On Windows, append `.exe` to the
executable path.
