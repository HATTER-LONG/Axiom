# Axiom

A minimal CMake project skeleton with a reusable `core` library and a `demo`
executable. The framework is intentionally empty so new modules can be added
incrementally.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

Developer configurations for Clang tooling and Doxygen are also included.

Optional tools can be enabled during configuration:

```sh
cmake -S . -B build-asan '-DAXIOM_SANITIZERS=Address;Undefined'
cmake -S . -B build-tidy -DAXIOM_STATIC_ANALYZERS=clang-tidy
cmake -S . -B build-iwyu -DAXIOM_STATIC_ANALYZERS=iwyu
cmake -S . -B build-cppcheck -DAXIOM_STATIC_ANALYZERS=cppcheck
cmake -S . -B build-ccache -DAXIOM_USE_CCACHE=ON
```

Sanitizer and ccache integrations download through CPM only when enabled.
Static-analysis presets require the selected locally installed tool. The `tidy`
preset treats all clang-tidy diagnostics as errors.

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
