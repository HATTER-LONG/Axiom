# Axiom

一个最小化的 CMake 项目骨架，包含可复用的 `core` 库和 `demo` 可执行程序。
框架保持轻量，便于逐步增加模块。

[English README](README.md)

## 构建

```sh
cmake --preset dev
cmake --build --preset dev
```

## 质量门禁

通过 uv 运行门禁，使自动化工具获得紧凑的 JSON 结果，而非编译器日志。所选门禁返回非零退出码即表示失败。

```sh
uv run --quiet python tools/check.py fast
uv run --quiet python tools/check.py full
uv run --quiet python tools/check.py hardening
```

在门禁名称前或后添加 `-v` / `--verbose`，可将执行的命令、合并后的输出及退出码打印到 stderr；JSON 报告始终输出到 stdout。

使用 `--report <path>` 可将报告保存给后续自动化流程。`fast` 只对本次增量构建重新编译的源文件运行复杂度、cppcheck 和 clang-tidy，同时始终扫描整个项目的架构规则。`full` 还会对所有项目编译单元执行格式、cppcheck、clang-tidy 和 Include-What-You-Use（IWYU）检查。`hardening` 在启用 AddressSanitizer 与 UndefinedBehaviorSanitizer 后构建并运行测试套件。

`fast` 与 `full` 使用 LLVM 源码级覆盖率插桩构建，测试套件行覆盖率低于 90% 时失败；报告中同时记录区域覆盖率与分支覆盖率，并在构建目录生成 `coverage-export.json`。附加 `--coverage-html` 可在同目录额外生成可浏览的 `llvm-cov` HTML 报告（`coverage-html/`）。

仅用于诊断、不会宣称门禁通过的命令：

```sh
uv run --quiet python tools/check.py inspect format
uv run --quiet python tools/check.py inspect tests --preset quality-fast
uv run --quiet python tools/check.py inspect coverage
uv run --quiet python tools/check.py inspect cppcheck
uv run --quiet python tools/check.py inspect clang-tidy
uv run --quiet python tools/check.py inspect iwyu
uv run --quiet python tools/check.py --list
```

两个分析器诊断以及 `full` 门禁都会从 CMake 编译数据库中扫描所有项目编译单元。

## 开发工具

下列工具需要自行安装；CPM 不会下载或安装它们。

| 工具 | 用途 | 何时需要 |
| --- | --- | --- |
| uv 与 Python 3.10+ | 运行质量检查脚本 | 质量门禁 |
| CMake 3.25+ 与 Ninja | 配置和构建 | 构建和质量门禁 |
| 支持 C++20 的编译器 | 构建项目 | 构建 |
| LLVM/Clang | `clang++`、`clang-tidy`、`clang-format`、clangd、AddressSanitizer、UndefinedBehaviorSanitizer | 所有质量门禁 |
| `llvm-profdata`、`llvm-cov` | 测试覆盖率度量 | `fast`、`full` 及覆盖率诊断命令 |
| cppcheck | 静态分析 | `fast`、`full` 及其诊断命令 |
| Include-What-You-Use（IWYU） | 头文件依赖分析 | `full` 及其诊断命令 |
| Doxygen | API 文档 | `-DAXIOM_BUILD_DOCS=ON` |
| ccache | 编译缓存 | 可选：`-DAXIOM_USE_CCACHE=ON` |
| Lizard | 圈复杂度分析 | `fast`、`full` 及其诊断命令 |

`clang-tidy`、`clang-format`、`llvm-profdata` 和 `llvm-cov` 随 LLVM 一同提供。`clang-format` 是编辑器或命令行格式化工具，不会由 CMake 预设自动执行。

`quality-hardening` 配置使用 LLVM 的编译器与运行时。在 Windows 上，构建在可用时会把 AddressSanitizer 的运行时 DLL 复制到可执行文件旁。

### IWYU 与 LLVM 版本兼容

IWYU 深度依赖 LLVM/Clang 内部接口，必须选用与已安装 Clang 匹配的 IWYU 版本或分支。例如 Clang 22 对应 IWYU 0.26 或 `clang_22` 分支。`full` 门禁与 `inspect iwyu` 要求 `include-what-you-use` 和 `iwyu_tool.py` 位于 `PATH` 中。

IWYU 官方发布的是源码。standalone 构建还要求完整 LLVM 开发安装，其中必须包含导出的 `LLVMConfig.cmake`、Clang 库和头文件。项目当前使用的标准 Windows LLVM 安装包提供了编译器工具，但没有该 CMake 配置包，因此不能单独用于构建 IWYU。请使用完整 LLVM 开发构建或开发包，然后执行：

```sh
git clone --branch clang_21 https://github.com/include-what-you-use/include-what-you-use.git
cmake -S include-what-you-use -B iwyu-build -G Ninja -DCMAKE_PREFIX_PATH=<llvm-development-prefix> -DCMAKE_INSTALL_PREFIX=<iwyu-install-prefix>
cmake --build iwyu-build
cmake --install iwyu-build
```

也可以在同一个构建树中同时构建 LLVM、Clang、compiler-rt 和 IWYU。下面的参考配置适用于 LLVM/Clang 22 与 IWYU 0.26；请将源目录和安装目录替换为本机实际路径：

```sh
cmake -S <llvm-source-dir>/llvm -B <llvm-build-dir> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<llvm-install-prefix> -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" -DLLVM_ENABLE_RUNTIMES=compiler-rt -DLLVM_EXTERNAL_PROJECTS=iwyu -DLLVM_EXTERNAL_IWYU_SOURCE_DIR=<iwyu-source-dir> -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
cmake --build <llvm-build-dir>
cmake --install <llvm-build-dir>
```

### Lizard 圈复杂度分析

Lizard 是基于 Python 的复杂度分析工具，分析 C/C++ 时不需要完整的头文件依赖图。请先安装 Python 3.10 或更高版本，再执行：

```sh
python -m pip install lizard
```

按质量门禁使用的阈值分析本项目源代码和测试：

```sh
lizard -l cpp -C 10 -L 80 src apps tests
```

可通过 `-C <阈值>` 设置圈复杂度阈值，通过 `-L <阈值>` 设置函数长度阈值；违反规则时命令返回非零退出码。

### CPM 自动管理的依赖

CPM 只会将项目依赖与 CMake 集成下载到 `.cache/`：

- `BUILD_TESTING=ON` 时下载 GoogleTest 1.18.0；
- 配置 CPM 管理功能时下载 CPM.cmake 本身；
- 启用 `AXIOM_SANITIZERS` 时下载 `cmake-scripts` sanitizer 集成；
- 启用 `AXIOM_USE_CCACHE=ON` 时下载 `Ccache.cmake` 集成。

这些下载不会安装编译器、分析工具、Doxygen 或 `ccache` 可执行文件。

### CMake 预设

```sh
cmake --preset quality-fast
cmake --build --preset quality-fast
ctest --preset quality-fast
```

质量检查脚本是推荐入口：它会将上述命令与架构、复杂度、格式及分析器检查组合起来。

## 使用 Sanitizer 验证 demo

demo 包含用于验证已构建 `quality-hardening` 配置的预期失败路径：

```sh
uv run --quiet python tools/check.py hardening
./build-quality/hardening/apps/demo/axiom_demo --memory-error
```

Windows 下运行 `build-quality/hardening/apps/demo/axiom_demo.exe --memory-error`。默认 demo 路径不会触发该问题。

也可单独验证 UBSan：

```sh
./build-asan-ubsan/apps/demo/axiom_demo --ubsan-error
```

该命令会触发有符号整数溢出；Windows 下请为可执行文件追加 `.exe`。
