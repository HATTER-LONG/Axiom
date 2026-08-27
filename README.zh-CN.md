# Axiom

一个基于 CMake 的紧凑 C++20 应用框架，提供可安装的 `Axiom::Core` 库、简洁的
demo 可执行程序、集成测试和可复现的质量门禁。核心公开 API 保持精简，以便逐步
增加模块而不过早扩大框架接口。

[English README](README.md)

## 构建

```sh
cmake --preset dev
cmake --build --preset dev
```

demo 会输出 `Axiom`。需要未优化的调试构建时，使用 `debug` 预设：

```sh
cmake --preset debug
cmake --build --preset debug
```

## 作为 CMake 包使用

从已配置的构建目录安装库：

```sh
cmake --install build --prefix <install-prefix>
```

使用方可以通过 CMake 3.25 或更高版本发现并链接导出的目标：

```cmake
find_package(Axiom CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE Axiom::Core)
```

公开头文件如下：

```cpp
#include <axiom/core/core.hpp>
```

## 项目结构

| 路径 | 用途 |
| --- | --- |
| `src/core` | 可安装的 `Axiom::Core` 库及其公开头文件。 |
| `apps/demo` | 使用核心库的最小可执行程序。 |
| `tests` | 单元测试和已安装包的集成测试。 |
| `tools/check.py` | 输出 JSON 报告的质量门禁入口。 |
| `quality` | 版本化质量配置、架构策略和 IWYU 映射。 |

## 质量门禁

通过 uv 运行门禁，使自动化工具获得紧凑的 JSON 结果，而非编译器日志。所选门禁返回非零退出码即表示失败。

```sh
uv run --quiet python tools/check.py fast
uv run --quiet python tools/check.py full
uv run --quiet python tools/check.py hardening
```

### 可复用的 check fixture

[`tools/check_test`](tools/check_test/README.md) 是一个独立的 C++20/CMake
demo 工程，用于验证 `check.py` 的公共契约。它拥有自己的源码、测试、CMake
预设和架构策略，不链接 Axiom。可在不修改 Axiom 源码的情况下验证 Python
报告解析器和拦截语义：

```sh
uv run --quiet python tools/check_test/verify.py \
  --report build-quality/reports/check-test-integration.json
uv run --quiet python tools/check.py --project-root tools/check_test fast
```

`--project-root` 会让命令执行、源码发现、构建产物和报告目录都相对于所选
CMake 项目。各项目通过自己的 `quality/check_profile.json` 声明源码目录、
预设名、构建目录、策略路径、缓存选项、报告 schema 与质量阈值。fixture
刻意使用了不同于 Axiom 的命名。测试会先运行干净工程的 `full` 与
`hardening` 基准，再为当前系统支持的每个工具编译隔离的真实错误源码，包括
ASan 与 UBSan。最终报告会分别列出工具支持状态、成功检出的错误、干净基准、
不支持的能力和意外失败。仅需无依赖的快速契约测试时使用 `--quick`。

在门禁名称前或后添加 `-v` / `--verbose`，可将执行的命令、合并后的输出及退出码打印到 stderr；JSON 报告始终输出到 stdout。

使用 `--report <path>` 可将报告保存给后续自动化流程。`fast` 只对本次增量构建重新编译的源文件运行复杂度、cppcheck 和 clang-tidy，同时始终扫描整个项目的架构规则。`full` 还会对所有项目编译单元执行格式、cppcheck、clang-tidy 和 Include-What-You-Use（IWYU）检查。`hardening` 在启用 AddressSanitizer 与 UndefinedBehaviorSanitizer 后构建并运行测试套件，然后使用独立的插桩构建，要求项目源码的 Mull 变异分数不低于 90。Mutation Testing Elements 报告保存在 `build-quality/mutation/mull/` 下。

`fast` 与 `full` 使用 LLVM 源码级覆盖率插桩构建，测试套件行覆盖率低于 90% 时失败；报告中同时记录区域覆盖率与分支覆盖率，并在构建目录生成面向 agent 的紧凑 `coverage-export.json`。该文件使用仓库相对路径、按命中次数合并的连续行区间、分组后的分支命中次数，并内置数组字段图例；所有有效统计项均达到 100% 的文件会被省略，只记录省略数量。附加 `--coverage-html` 可在同目录额外生成可浏览的 `llvm-cov` HTML 报告（`coverage-html/`）。

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

### 门禁结果、评分与卡点

每次执行都会写入一份 `axiom-quality/v2` JSON 报告。每个检查都有最高分，并处于以下四种状态之一：

| 状态 | 含义 | 对门禁和评分的影响 |
| --- | --- | --- |
| `pass` | 检查已执行且满足判定条件。 | 计入全部分值。 |
| `fail` | 检查已执行，但命令、阈值或校验失败。 | 门禁失败，计 0 分。 |
| `blocked` | 前置检查失败，因此未执行此检查。 | 门禁失败，计 0 分。 |
| `skipped` | 所需外部工具未安装，或当前平台不支持该工具。 | 将原因打印到 stderr；不使门禁失败，也不计入总分分母。 |

因此，只有所有记录的检查均为 `pass` 或 `skipped` 时，`passed` 才为 true。总分为通过检查的分值之和，除以除 `skipped` 外所有检查的最高分之和。门禁可能在最高分减少的情况下通过：这表示某项可选能力没有被验证，并不表示它通过了验证。工具已经启动但以非零退出码结束时始终是 `fail`，不会被转换为跳过。

存在构建产物依赖关系的检查会顺序执行。configure 失败会阻断 build、测试、覆盖率和依赖编译数据库的分析器；build 失败也会阻断其下游检查。如果这些前置步骤所需的可执行文件不存在，则前置步骤和下游步骤都会跳过，避免误用旧的构建产物。格式化、复杂度和各个独立分析器则只在自身所需工具缺失时跳过。

在没有跳过项目时，`fast`、`full` 和 `hardening` 的名义总分分别是 95、125 和 90。`fast` 包含架构（10）、configure（10）、build（20）、复杂度（10）、cppcheck（10）、clang-tidy（10）、测试（15）和覆盖率（10）。`full` 还会对整个项目执行格式检查和 IWYU。`hardening` 由 45 分的 ASan/UBSan configure-build-test 路径，以及独立的 45 分 Mull configure-build-mutation 路径组成。

### 架构规则

[`quality/architecture_rules.json`](quality/architecture_rules.json) 是由 `architecture` 检查读取的小型版本化策略文件。当前规则禁止 `src/core/` 下的文件直接包含 `apps/` 或 `tests/` 下的头文件。这保证了预期的依赖方向：可复用的生产核心代码可以被 demo 和测试使用，但不能反向依赖它们。

对于这个小型项目，这条规则是合理且低成本的护栏：规则采用声明式配置，无需编译数据库，扫描实现文件和头文件，并报告违规文件、行号、include 和规则 ID。不过，它只检查直接文本 `#include`，不构建完整依赖图、不检测传递依赖，也不能覆盖所有可能的模块边界。项目变大后，应为每个稳定层级添加同样明确的规则；当需要强制约束传递依赖时，再使用编译数据库或专门的依赖分析工具。

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
| Mull | C++ 变异测试 | `hardening` |
| Doxygen | API 文档 | `-DAXIOM_BUILD_DOCS=ON` |
| ccache | 编译缓存 | 可选：`-DAXIOM_USE_CCACHE=ON` |
| Lizard | 圈复杂度分析 | `fast`、`full` 及其诊断命令 |

`clang-tidy`、`clang-format`、`llvm-profdata` 和 `llvm-cov` 随 LLVM 一同提供。`clang-format` 是编辑器或命令行格式化工具，不会由 CMake 预设自动执行。

`quality-hardening` 配置使用 LLVM 的编译器与运行时。在 Windows 上，构建在可用时会把 AddressSanitizer 的运行时 DLL 复制到可执行文件旁。独立的 `quality-mutation` 配置会启用 Mull 的 LLVM IR frontend。检查脚本会从 `PATH` 中自动配对无后缀工具，或 `mull-runner-22` 与 `mull-ir-frontend-22` 这类带 LLVM 版本后缀的工具；Mull 版本必须与所用 LLVM 工具链匹配。
Mull 0.34 不支持原生 Windows；请在 WSL、Linux 或 macOS 中运行 `hardening` 门禁。此机器需要先为 WSL 安装 Linux 发行版，再在其中构建并安装 Mull 源码，然后在 Axiom 的 Linux 工作目录中执行 `uv run --quiet python tools/check.py hardening`。带版本后缀的 runner 与 frontend 插件必须和 `clang++` 使用相同的 LLVM 主版本。

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

## 门禁回归测试

分析器与编排的故意失败场景统一放在 `tools/check_test`，不再污染生产 demo
源码。先运行 `python tools/check_test/verify.py` 验证反向用例，再运行
`python tools/check.py full` 验证真实工程的全绿基准。
