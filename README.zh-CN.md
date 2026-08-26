# Axiom

一个最小化的 CMake 项目骨架，包含可复用的 `core` 库和 `demo` 可执行程序。
框架保持轻量，便于逐步增加模块。

[English README](README.md)

## 构建

```sh
cmake --preset dev
cmake --build --preset dev
```

## 开发工具

下列工具需要自行安装；CPM 不会下载或安装它们。

| 工具 | 用途 | 何时需要 |
| --- | --- | --- |
| CMake 3.25+ 与 Ninja | 配置和构建 | 始终需要 |
| 支持 C++20 的编译器 | 构建项目 | 始终需要 |
| LLVM/Clang | `clang++`、`clang-tidy`、`clang-format`、clangd、AddressSanitizer、UndefinedBehaviorSanitizer | 推荐工具链；Clang、tidy 与 sanitizer 预设需要 |
| cppcheck | 静态分析 | `cppcheck` 预设 |
| Include-What-You-Use（IWYU） | 头文件依赖分析 | `iwyu` 与 `static-analysis` 预设 |
| Doxygen | API 文档 | `-DAXIOM_BUILD_DOCS=ON` |
| ccache | 编译缓存 | `ccache` 预设 |
| Lizard 与 Python 3.8+ | 圈复杂度分析 | 手动运行 |

`clang-tidy` 和 `clang-format` 随 LLVM 一同提供。`clang-format` 是编辑器或命令行格式化工具，不会由 CMake 预设自动执行。

sanitizer 预设使用 LLVM 的编译器与运行时。在 Windows 上，构建在可用时会把 AddressSanitizer 的运行时 DLL 复制到可执行文件旁。

### IWYU 与 LLVM 版本兼容

IWYU 深度依赖 LLVM/Clang 内部接口，必须选用与已安装 Clang 匹配的 IWYU 版本或分支。例如 Clang 22 对应 IWYU 0.26 或 `clang_22` 分支。`iwyu` 与 `static-analysis` 预设要求 `include-what-you-use` 位于 `PATH` 中。

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

Lizard 是基于 Python 的复杂度分析工具，分析 C/C++ 时不需要完整的头文件依赖图。请先安装 Python 3.8 或更高版本，再执行：

```sh
python -m pip install lizard
```

分析本项目源代码和测试：

```sh
lizard -l cpp src apps tests
```

Lizard 默认将 CCN（圈复杂度）阈值设为 15。可通过 `-C <阈值>` 显式设置；超过阈值时会给出警告并返回非零退出码，适合本地检查或 CI：

```sh
lizard -l cpp -C 15 src apps tests
```

### CPM 自动管理的依赖

CPM 只会将项目依赖与 CMake 集成下载到 `.cache/`：

- `BUILD_TESTING=ON` 时下载 GoogleTest 1.18.0；
- 配置 CPM 管理功能时下载 CPM.cmake 本身；
- 使用 sanitizer 预设时下载 `cmake-scripts` sanitizer 集成；
- 使用 `ccache` 预设时下载 `Ccache.cmake` 集成。

这些下载不会安装编译器、分析工具、Doxygen 或 `ccache` 可执行文件。

### 常用预设

```sh
cmake --workflow --preset test
cmake --workflow --preset tidy
cmake --build --preset iwyu
cmake --build --preset cppcheck
cmake --workflow --preset asan-ubsan
```

`tidy` 预设会把全部 clang-tidy 诊断视为错误。

## 使用 Sanitizer 验证 demo

demo 含有专用于 sanitizer 验证的堆使用后释放路径：

```sh
cmake --workflow --preset asan-ubsan
./build-asan-ubsan/apps/demo/axiom_demo --memory-error
```

Windows 下运行 `build-asan-ubsan/apps/demo/axiom_demo.exe --memory-error`。默认 demo 路径不会触发该问题。

也可单独验证 UBSan：

```sh
./build-asan-ubsan/apps/demo/axiom_demo --ubsan-error
```

该命令会触发有符号整数溢出；Windows 下请为可执行文件追加 `.exe`。
