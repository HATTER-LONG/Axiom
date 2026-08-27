# check-test

This module verifies both the Python contract and the real installed C++
toolchain without depending on Axiom sources. The fixture owns its CMake,
format, tidy, architecture, coverage, and sanitizer configuration.

Run it with the same Python interpreter used for `check.py`:

```sh
python tools/check_test/verify.py --report build-quality/reports/check-test-integration.json
```

The default run has three clearly separated sections:

1. fast dependency-free contract tests;
2. system tool status and versions;
3. real clean-project and broken-project verification.

Use `--quick` for the sub-second mocked contract suite, or
`--integration-only` when only the compiler/toolchain verification is needed.
Add `-v` to print every executed command, its output, and per-step details,
the same way `check.py -v` does.

The real negative sources under `cases/` cover architecture, formatting,
complexity, cppcheck, clang-tidy, IWYU, failing tests, low coverage, ASan
heap-use-after-free, and UBSan signed overflow. A negative case passes only when
the expected check ID reports `fail`; sanitizer cases additionally require the
actual runtime diagnostic in CTest's log. Unsupported tools are reported rather
than silently treated as tested.

`fixture/` is deliberately a standalone C++/CMake project. Its directories,
preset names, cache options, report schemas, and policy paths differ from
Axiom's, proving that portability comes from `quality/check_profile.json`
rather than repository-specific constants. Copy `tools/check.py` and
`tools/checks/` to another C++ project, add that profile, and retain this suite
as the engine's regression test. `fixture/` is the clean 100% baseline; error
sources are copied into temporary projects and never contaminate it.
