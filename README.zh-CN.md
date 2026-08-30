# Axiom

一个基于 CMake 的紧凑 C++20 应用框架。当前核心是一个可安装的同步能力运行时：
将带类型的 C++ callable 注册为带描述的 `Module`/`Action`，通过元数据发现，并经由
动态 `Value` 边界调用，同时以结构化 `Result` 返回错误。仓库还提供简洁的 demo、测试
和可复现的质量门禁。

[English README](README.md)

## 构建与使用

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

`debug` 提供未优化构建。两个开发预设均构建测试；关闭测试使用
`BUILD_TESTING=OFF`。作为 `add_subdirectory()` 依赖时，Axiom 不启用 CTest。
Demo 演示 Core 的 Action、日志与资源接口，始终链接 `Axiom::Core`，不再有 DemoCore。
运行 `build/apps/demo/axiom_demo`（Windows 下为 `axiom_demo.exe`）即可查看演示。
`apps/demo/resource_demo.cpp` 中的资源示例为累加器特化 `ResourceTraits`，将所有权交给
`ResourceRegistry`，并通过 `ResourceId` 文本往返还原类型化 `Handle`。随后解析出
`ResourceRef` 并更新累加器，再移除注册：新的查询返回 `NotFound`，已有引用在离开
作用域前仍可访问对象。`axiom.demo` CTest 冒烟测试会检查这些步骤，行为不符时失败。

Demo 按职责拆分：`main.cpp` 只负责执行顺序和异常处理；`base_demo` 演示框架标识和
Value；`action_demo` 负责注册、发现和调用；`resource_demo` 演示资源生命周期；
`logging_demo` 封装 sink、订阅和日志查询。各示例通过小型头文件提供入口，实现辅助函数
保持私有；`accumulator.hpp` 保存共享示例类型，`demo_output` 统一控制台格式。

Core 默认构建静态库。需要动态库时，在独立构建目录设置 `BUILD_SHARED_LIBS=ON`。
安装后消费方式保持不变：

```cmake
find_package(Axiom CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE Axiom::Core)
```

公开总头文件为 `<axiom/core/core.hpp>`。类型化 callable 通过 ModuleBuilder 注册，
Runtime 提供发现和同步调用，Value 与 Result 构成动态值和错误边界；资源和日志模块
的详细契约见公开头文件及 `docs/architecture`。

## 质量检查

使用全局安装的 checkflow，不创建项目局部 Python 环境：

```sh
checkflow doctor
checkflow fast
checkflow hardening
checkflow full
```

| 流程 | 内容 |
| ---- | ---- |
| fast | 架构检查、增量静态构建、GoogleTest、LLVM 覆盖率及数据完整性验证。 |
| full | 干净静态覆盖率构建、格式/复杂度/cppcheck/clang-tidy，并验证无插桩的静态与动态库及安装消费者。 |
| hardening | 静态 ASan/UBSan 测试（含 UBSan 必须终止进程的回归测试），然后独立运行静态 Mull 及报告完整性检查。 |

`checkflow.json` 是流程顺序与阈值的唯一来源；`CMakePresets.json` 负责编译器、
库类型、插桩模式和构建目录。旧的 `quality/check_profile.json` 已移除。

行、区域和分支覆盖率**分别不得低于 90%**。测试程序链接整个静态 Core 归档，
避免未引用的翻译单元从统计分母消失。CheckFlow 通过 CTest 发现程序路径，不在
JSON 中硬编码不同平台的产物名。完整性检查拒绝 LLVM 映射警告（包括 mismatched
profiles）和遗漏的 Core 实现文件。覆盖率不足时，构建目录生成 `coverage-export.json`。

Mull 阈值统一为 **90%**，报告必须包含 `src/core/src` 下的实现变异点，不能只统计
头文件实例化。覆盖率和 Mull 要求静态 Core。动态库使用独立 `quality-shared`
预设复用单测，采用 Ninja Multi-Config 的 Release 配置，以持续验证安装时的配置选择。
其中 Registry/Dispatcher 的内部单测在测试程序中编译私有实现；公开 Runtime 测试
仍调用动态库，内部类无需为测试而扩展 DLL 导出接口。

UBSan 检测到错误必须停止进程。配置阶段检查 sanitizer 支持情况及组合兼容性，
不支持时直接失败。MemorySanitizer 还要求适当插桩的依赖和标准库工具链，编译探测
成功并不代表运行时环境已经完整。

命令输出紧凑 JSON；`--verbose` 显示进度，`--diagnostic` 展示详细执行信息。
前置步骤失败会阻止依赖步骤运行。必须检查跳过或缺失的工具：跳过不等于验证通过。
报告遵循已安装 CheckFlow 的 schema，不再描述不存在的独立积分体系。

## 安装与动态库验证

安装消费者测试默认不启用；使用 `AXIOM_BUILD_INSTALL_TEST=ON`，且构建不能带
coverage、sanitizer 或 Mull 插桩。测试先安装并移动安装目录，再用相同工具链和配置
构建、运行独立消费者，检查包是否可重定位。

```sh
cmake --preset quality-shared
cmake --build --preset quality-shared
ctest --preset quality-shared
```

`quality-static` 验证无插桩静态库；full 包含这两条路径。动态库使用默认隐藏符号和
显式 API 导出；静态库保留 PIC，允许嵌入其他共享对象。0.x 阶段 ABI 标识采用主版本.
次版本（当前 0.1），包兼容性限定在同一次版本。消费者仍需兼容的 C++ ABI、标准库
和运行时，不承诺跨工具链二进制兼容。

## 构建配置与依赖

Axiom-owned target 固定启用 C++20 严格语言模式和基础警告；
`AXIOM_WARNINGS_AS_ERRORS` 在独立项目中默认开启，在被嵌入时默认关闭。
项目尊重调用方的标准 CMake analyzer 和 launcher 设置，不删除用户缓存项。

```sh
cmake --preset dev -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --preset dev -DCMAKE_CXX_CLANG_TIDY=clang-tidy
cmake --preset dev -DAXIOM_BUILD_DOCS=ON
cmake --build --preset dev --target docs
```

已移除 `AXIOM_BUILD_TESTS`、`AXIOM_STRICT_LANGUAGE_MODE`、`AXIOM_STATIC_ANALYZERS`
和 `AXIOM_USE_CCACHE`。对应使用 `BUILD_TESTING`、固定语言策略、标准 CMake analyzer
变量和 `CMAKE_CXX_COMPILER_LAUNCHER`。迁移旧的全局工具参数时，应重新创建旧构建目录。

仓库不固定构建线程数；通过 `CMAKE_BUILD_PARALLEL_LEVEL`、`--parallel N` 或本地
`CMakeUserPresets.json` 设置。

CPM 固定 spdlog 1.17.0 和 GoogleTest 1.18.0。spdlog 与其 bundled fmt 是私有头文件
依赖，Core 显式保留 `Threads::Threads` 要求。GoogleTest 只用于测试，并始终静态
构建，不随 Core 的库类型变化。不再下载 sanitizer 和 ccache 的 CMake 集成包。

CPM 尊重 CMake 参数或环境变量中的 `CPM_SOURCE_CACHE`。Windows 默认使用
`%LOCALAPPDATA%/CPM`，Unix 使用 `$XDG_CACHE_HOME/CPM` 或 `~/.cache/CPM`；没有用户
缓存位置时回退到构建目录。CPM 不负责安装编译器和分析工具。

| 工具 | 用途 |
| ---- | ---- |
| CMake 3.25+、Ninja、C++20 编译器 | 构建，动态验证另用 Ninja Multi-Config。 |
| CheckFlow | 质量检查编排。 |
| Clang、llvm-profdata、llvm-cov | 质量构建及覆盖率。 |
| clang-format、clang-tidy、cppcheck、Lizard | full 的静态检查。 |
| 与 Clang 主版本匹配的 Mull | hardening 的变异测试。 |
| Doxygen、ccache | 可选的文档和编译缓存。 |

Mull 需在支持的 Linux/macOS 或 WSL 环境运行。Windows sanitizer 构建通过所选
Clang 驱动与目标架构定位 runtime DLL，缺失时失败，不静默跳过部署。

`quality/architecture_rules.json` 检查声明的分层关系中的直接文本 include；它不是
完整的传递 C++ 依赖分析。修改稳定模块边界时同步维护该文件。
