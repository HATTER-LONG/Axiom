# Axiom Agent 开发指南

以小而可验证的增量构建 Axiom。优先复用现有设计，保持 Public API 精简；不得为了通过检查而降低质量要求。

## 设计与范围

- 优先采用 **薄接口、深模块**。Public interface 只暴露调用方真正需要的操作、数据和失败语义；协调、表示选择、缓存与平台细节应由实现内部吸收。
- 用模块为调用方消除的复杂度衡量其深度，而不是用代码行数衡量。相关 policy 和 state 应由一个内聚 owner 管理，不能让调用方自行按正确顺序拼接多个浅层 helper。
- 只有当前调用方确实需要时，才新增 public type、method、option、callback 或 template parameter。优先使用 private helper、value type 与实现细节，不要通过大量配置项泄漏内部决策。
- Public API 必须少、稳定、清晰，并隐藏内部复杂性。边界处应明确 ownership、生命周期、线程安全、顺序、错误和 no-op 契约；不得要求调用方了解内部状态或调用顺序。
- 避免泄漏抽象：除非模块明确是适配层，否则不得暴露 storage、第三方类型、平台 handle、可变内部 collection 或实现专用错误细节。
- 行为放在真正拥有职责的模块中；保持单向依赖：Core 不依赖 UI，Library 不依赖 Application，生产代码不依赖测试代码。
- 优先 RAII、清晰的 ownership 和组合；除适配层外，不跨模块暴露 Qt、OCC、Boost 等第三方实现类型。
- 优先修改实现，而不是扩展 Public API。不创建推测性抽象、`utils`、`common`、`manager` 等职责模糊的万能模块，也不做无关重构。
- 修改前确认：所属模块、可复用接口、API 变更是否必要、必须保持的行为，以及验证方案。

## 跨平台支持

Windows、Linux、macOS 都是支持目标。平台差异必须收敛在小型可移植边界中，并使用 CMake 平台/编译器条件（`WIN32`、`APPLE`、`UNIX`、`MSVC`）选择，不能假设当前宿主环境。

- 优先标准 C++；只有确有必要时才引入操作系统头文件或 API。
- 共享库 Public API 使用模块导出宏。`AXIOM_CORE_API` 在 Windows 映射为 `__declspec(dllexport/dllimport)`，在 Linux/macOS 映射为默认可见性；不得让未防护的 Windows 专用声明出现在其他平台。
- 必须保持静态库默认构建可用。修改构建或安装行为时，同时验证 `-DBUILD_SHARED_LIBS=ON` 和已安装包使用方。
- 优先 CMake target 属性和 generator expression，避免硬编码路径、shell 命令或文件扩展名。路径分隔符、可执行文件后缀、动态库加载、文本编码和运行时部署都属于需要显式设计与测试的平台差异。

## 实现与测试

- 实现满足需求的最小正确方案；控制流、状态、错误处理、ownership 和生命周期必须清晰，不得无契约地静默吞掉失败。
- 单元和集成测试使用 GoogleTest。新增行为必须测试，Bug 修复必须有回归测试；测试必须确定、独立，并优先验证外部可观察行为。
- 默认使用 `EXPECT_*`；只有后续逻辑依赖条件成立时才使用 `ASSERT_*`。

## 文档

Public header 的每个 public type、free function 和 public method 都使用 Doxygen 注释描述 API contract。记录不明显的不变量、ownership、生命周期、顺序与状态转换；注释解释 why，不重复明显控制流。文档必须与实现同步。

```cpp
/**
 * @brief 注册 sink，并返回析构时自动注销的 RAII subscription。
 *
 * @param sink 共享所有权，subscription 存活期间保留。
 * @param filter consume() 前应用的筛选条件。
 * @return move-only subscription；销毁时移除 sink。
 * @throws std::bad_alloc 注册存储分配失败时抛出。
 * @note 当前实现同步分发。
 */
[[nodiscard]] LogSubscription addSink(std::shared_ptr<ILogSink> sink, LogFilter filter = {});
```

Public header 使用 `@file` 与 `@brief`；当契约需要时补充 `@tparam`、`@param`、`@return`、`@throws`、`@pre`、`@post`、`@note` 或 `@warning`。

## 质量与工作区

在仓库根目录使用全局 `checkflow`，不要创建项目级 Python 环境。每个可验证步骤后运行 `fast`，主体完成后运行 `hardening`，交付前运行 `full`。门禁失败时修复根因；不得用 suppression、排除项、禁用测试或降低规则绕过。架构规则位于 `quality/architecture_rules.json`。

修改前运行 `git status --short`，保留无关既有修改。Agent 可读写 `~/.cache/CPM`，但仅将它用于 CPM 的持久化依赖缓存；不得将其作为项目源码，也不得整体删除其内容。

## 完成定义

只有当请求行为在正确模块实现、测试和文档同步、依赖方向与生命周期清晰，并且所需质量门禁全部通过后，才能交付。
