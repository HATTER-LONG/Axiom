# Axiom

一个基于 CMake 的紧凑 C++20 应用框架。当前核心是一个可安装的同步能力运行时：
将带类型的 C++ callable 注册为带描述的 `Module`/`Action`，通过元数据发现，并经由
动态 `Value` 边界调用，同时以结构化 `Result` 返回错误。仓库还提供简洁的 demo、测试
和可复现的质量门禁。

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

## 核心运行时模型

公开 API 以 `Value`/`Arguments` 表示有序动态值，以 `ModuleBuilder` 暂存带类型的
callable Action，以 `Runtime` 注册模块、发现描述符并同步调用，以 `Error`/`Result<T>`
在边界传递结构化失败。Action ID 使用规范的 `module.action` 形式，两部分均只能包含
小写 ASCII 字母、数字和下划线。

当前支持 `bool`、有符号整数、浮点数、`std::string`、递归的 `std::vector<T>`，以及字符
串键的 `std::map`/`std::unordered_map` 转换。整数输入可以转换为浮点数，但会拒绝有损
转换和隐式字符串解析；可选参数通过经过校验的 `param(..., default_value)` 提供默认值。
Runtime 有意不保证线程安全，注册、发现和调用不能并发执行。Core 当前不提供协议适配器、
JSON 序列化、网络、插件、日志、鉴权或异步执行。

## 项目结构

| 路径 | 用途 |
| --- | --- |
| `src/core` | 可安装的 `Axiom::Core` 库及其公开头文件。 |
| `apps/demo` | 使用核心库的最小可执行程序。 |
| `tests` | 单元测试和已安装包的集成测试。 |
| `checkflow.json` | 版本化的 CheckFlow 质量门禁流程定义。 |
| `.checkflow/tools` | Axiom 专属的架构、格式与变异质量 Tool。 |
| `quality` | 版本化质量配置和架构策略。 |

## 质量门禁

通过全局安装的 `checkflow` 命令运行门禁，使自动化工具获得紧凑的 JSON 结果，而非编译器日志。所选门禁返回非零退出码即表示失败。

```sh
checkflow fast
checkflow full
checkflow hardening
checkflow doctor
```

在门禁名称前或后添加 `-v` / `--verbose`，可将执行的命令、合并后的输出及退出码打印到 stderr；JSON 报告始终输出到 stdout。

使用 `--report <path>` 可将报告保存给后续自动化流程。`fast` 校验架构、配置并构建
`quality-fast`，运行 CTest 和 LLVM 覆盖率。`full` 另外运行全项目复杂度、cppcheck、
clang-tidy 和只读格式检查，再运行 CTest 与覆盖率。安装消费测试只在 `quality-fast`
预设中启用，但不是 CheckFlow flow 的步骤；需要时请显式设置
`-DAXIOM_BUILD_INSTALL_TEST=ON` 后运行 CTest。`hardening` 在启用 AddressSanitizer
与 UndefinedBehaviorSanitizer 后构建并运行测试套件，然后使用独立的插桩构建，要求项目
源码的 Mull 变异分数不低于 90。Mutation Testing Elements 报告保存在
`build-quality/mutation/mull/` 下。

`fast` 与 `full` 使用 LLVM 源码级覆盖率插桩构建，测试套件行覆盖率低于 90% 时失败；报告中同时记录区域覆盖率与分支覆盖率，并在构建目录生成面向 agent 的紧凑 `coverage-export.json`。该文件使用仓库相对路径、按命中次数合并的连续行区间、分组后的分支命中次数，并内置数组字段图例；所有有效统计项均达到 100% 的文件会被省略，只记录省略数量。附加 `--coverage-html` 可在同目录额外生成可浏览的 `llvm-cov` HTML 报告（`coverage-html/`）。

仅用于诊断、不会宣称门禁通过的命令：

```sh
checkflow doctor
checkflow doctor fast
checkflow fast --diagnostic
```

分析器诊断以及 `full` 门禁都会从 CMake 编译数据库中扫描所有项目编译单元。

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

在没有跳过项目时，`fast`、`full` 和 `hardening` 的名义总分分别是 75、105 和 90。
`fast` 包含架构（10）、configure（10）、build（20）、测试（15）和覆盖率（10）。
`full` 另外检查格式（10）、复杂度（10）、cppcheck（10）和 clang-tidy（10）。
`hardening` 由 45 分的 ASan/UBSan configure-build-test 路径，以及独立的 45 分 Mull
configure-build-mutation 路径组成。

### 架构规则

[`quality/architecture_rules.json`](quality/architecture_rules.json) 是由 `architecture` 检查读取的小型版本化策略文件。当前规则禁止 `src/core/` 下的文件直接包含 `apps/` 或 `tests/` 下的头文件。这保证了预期的依赖方向：可复用的生产核心代码可以被 demo 和测试使用，但不能反向依赖它们。

对于这个小型项目，这条规则是合理且低成本的护栏：规则采用声明式配置，无需编译数据库，扫描实现文件和头文件，并报告违规文件、行号、include 和规则 ID。不过，它只检查直接文本 `#include`，不构建完整依赖图、不检测传递依赖，也不能覆盖所有可能的模块边界。项目变大后，应为每个稳定层级添加同样明确的规则；当需要强制约束传递依赖时，再使用编译数据库或专门的依赖分析工具。

## 开发工具

下列工具需要自行安装；CPM 不会下载或安装它们。

| 工具 | 用途 | 何时需要 |
| --- | --- | --- |
| CheckFlow | 运行仓库质量门禁 | 质量门禁 |
| CMake 3.25+ 与 Ninja | 配置和构建 | 构建和质量门禁 |
| 支持 C++20 的编译器 | 构建项目 | 构建 |
| LLVM/Clang | `clang++`、`clang-tidy`、`clang-format`、clangd、AddressSanitizer、UndefinedBehaviorSanitizer | 所有质量门禁 |
| `llvm-profdata`、`llvm-cov` | 测试覆盖率度量 | `fast`、`full` 及覆盖率诊断命令 |
| cppcheck | 静态分析 | `fast`、`full` 及其诊断命令 |
| Mull | C++ 变异测试 | `hardening` |
| Doxygen | API 文档 | `-DAXIOM_BUILD_DOCS=ON` |
| ccache | 编译缓存 | 可选：`-DAXIOM_USE_CCACHE=ON` |
| Lizard | 函数复杂度、长度和参数量阈值 | `fast`、`full` 及其诊断命令 |

`clang-tidy`、`clang-format`、`llvm-profdata` 和 `llvm-cov` 随 LLVM 一同提供。`full` 与 `inspect format` 都使用只读的 `--dry-run --Werror`；JSON 结果只列出需要格式化的文件，不包含 clang-format 的逐行诊断，也不会修改源码。

`quality-hardening` 配置使用 LLVM 的编译器与运行时。在 Windows 上，构建在可用时会把 AddressSanitizer 的运行时 DLL 复制到可执行文件旁。独立的 `quality-mutation` 配置会启用 Mull 的 LLVM IR frontend。检查脚本会从 `PATH` 中自动配对无后缀工具，或 `mull-runner-22` 与 `mull-ir-frontend-22` 这类带 LLVM 版本后缀的工具；Mull 版本必须与所用 LLVM 工具链匹配。
Mull 0.34 不支持原生 Windows；请在 WSL、Linux 或 macOS 中运行 `hardening` 门禁。此机器需要先为 WSL 安装 Linux 发行版，再在其中构建并安装 Mull 源码，然后在 Axiom 的 Linux 工作目录中执行 `checkflow hardening`。带版本后缀的 runner 与 frontend 插件必须和 `clang++` 使用相同的 LLVM 主版本。

### Lizard 圈复杂度分析

Lizard 是基于 Python 的复杂度分析工具，分析 C/C++ 时不需要完整的头文件依赖图。请先安装 Python 3.10 或更高版本，再执行：

```sh
python -m pip install lizard
```

按质量门禁使用的阈值分析本项目源代码和测试：

```sh
lizard -l cpp -C 10 -L 80 -a 5 src apps tests
```

`-C` 限制圈复杂度，`-L` 限制函数长度，`-a` 限制参数量；违反规则时命令返回非零退出码。这些快速、语法级的量化指标与 clang-tidy 互补：`readability-function-size` 只保留嵌套深度，`readability-function-cognitive-complexity` 衡量面向人的控制流理解难度，`misc-include-cleaner` 负责语义级头文件诊断。

### CPM 自动管理的依赖

CPM 只会将项目依赖与 CMake 集成下载到用户缓存目录 `~/.cache/CPM`：

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

修改 `checkflow.json` 或 `.checkflow/tools/` 后，先运行
`checkflow doctor`，再根据变更范围运行 `fast`、`full` 或
`hardening`。
