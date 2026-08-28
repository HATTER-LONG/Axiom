# Axiom Core Framework — Phase 1 规格

> 状态：建议稿（基于当前仓库）
> 范围：`Axiom::Core` 的能力注册与同步调用基础
> 语言：C++20
> 目标版本：`0.1.x`

## 1. 背景与决策

当前仓库是一个可安装的 C++20 框架骨架：唯一的生产 target 是静态库
`axiom_core`（导出为 `Axiom::Core`），公开头文件位于
`src/core/include/axiom/core/`，并已具备 demo、安装消费测试和确定性的质量门禁。
目前尚未实现动态 `Value`、Action、Registry、Runtime 或日志子系统。

本规格将这些能力作为 `Axiom::Core` 的增量演进，而不是假定仓库已经有一个
`src/runtime` target。这样可以保持当前的安装包契约、CMake 结构和最小公开 API；
若未来某个子系统确实需要独立发布，再以真实的构建、依赖和版本需求为依据拆分 target。

Phase 1 的目标是提供一个领域无关、同步的“定义一次、由多个适配器复用”的能力模型。
该模型**不自动生成** Python、HTTP、CLI 或 Agent 接口；这些是后续外部适配器的职责。

## 2. 范围

### 2.1 本阶段交付

- JSON 形状的动态数据 `Value` 与命名参数 `Arguments`。
- 无异常跨调用边界的 `Error` / `Result<T>`。
- Module、Action、参数和返回值的描述模型。
- 由强类型 C++ callable 注册 Action，并经 `Value` 调用的同步运行时。
- 注册、发现、参数校验、类型转换和异常归一化。
- 对公开 API、安装消费和错误行为的测试。

### 2.2 明确不在本阶段

- Python、JSON 序列化、JSON Schema、HTTP/RPC、CLI、Agent SDK。
- 异步、取消、进度、权限、中间件、追踪、指标或插件加载。
- Qt、业务对象、几何/网格/项目模型，以及任意第三方运行时类型。
- 独立日志框架。现有库还没有日志后端、配置或应用诊断需求；现在抽象 Logger
  会制造未被验证的公共接口。调用上下文和结构化错误应先保留诊断所需数据，日志在
  有具体宿主需求时作为独立基础设施设计。

## 3. 架构边界

```text
应用 / 将来的适配器
          |
          v
     Axiom::Core
  base  <-  action
          |
          v
    业务 callable
```

`base` 不能依赖 `action`、应用、测试或任何前端；`action` 不能依赖应用、测试、
Python、网络或 UI。应用和未来适配器只通过 `Axiom::Core` 的公开头文件使用运行时。
现有 `quality/architecture_rules.json` 已防止 Core 直接包含 `apps/` 与 `tests/`；在
新增目录后，应把相同方向落实为窄而明确的规则，不能以放宽检查来通过构建。

建议的实现布局如下。`detail/` 中的名字不属于稳定 API，也不应被安装消费者依赖。

```text
src/core/
├── include/axiom/core/
│   ├── core.hpp                 # 兼容入口；仅包含稳定的高层入口
│   ├── base/{value,error,result,type_descriptor}.hpp
│   ├── action/{action_id,descriptor,invocation_context,action,module,runtime}.hpp
│   └── action/detail/{registry,dispatcher,value_converter,function_traits,typed_action_adapter}.hpp
└── src/
    ├── core.cpp
    ├── base/{value,action_id,descriptor}.cpp
    └── action/{registry,dispatcher,runtime}.cpp
```

上述是目标布局，不要求一次性创建所有文件；每个可验证的阶段只引入其真正需要的文件。
当前 `frameworkName()` / `isFrameworkName()` 保持兼容，除非另有废弃策略；它们不属于
Action 模型。

## 4. 基础数据与错误模型

### 4.1 Value

`Value` 是跨 Action 边界的唯一动态值，不是业务领域对象，也不是序列化格式。支持
`Null`、`Boolean`、`Integer`（`std::int64_t`）、`Number`（`double`）、`String`、`Array`
和 `Object` 七种逻辑类型。`Arguments` 是 `Value::Object`，仅支持命名参数。

`Object` 应使用具有稳定迭代顺序的字符串键容器（建议 `std::map<std::string, Value>`）。
这使发现结果、测试断言和将来的 schema/JSON 输出可复现；如果性能证据证明需要哈希
容器，应在边界处明确排序，而不是将顺序不确定性暴露给消费者。

访问器在类型不匹配时不得产生未定义行为。它们可以抛出本地的类型错误，或提供
`Result` 形式的读取接口；无论选择哪种形式，该异常都不得穿过 `IAction::invoke()` 或
`Runtime::invoke()`。

### 4.2 TypeDescriptor

`Value` 回答“实际值是什么”，`TypeDescriptor` 回答“参数或结果要求什么”。
Phase 1 的 `TypeDescriptor` 至少应能表达标量、数组元素类型、对象字段、可空性和文本
描述；它不承诺 JSON Schema 兼容性。复杂约束、枚举、结构体映射和默认值语义先不加入，
直到有实际调用方需要它们。

### 4.3 Error 与 Result

公开错误至少包含：`InvalidArgument`、`MissingArgument`、`UnknownArgument`、
`TypeMismatch`、`NotFound`、`AlreadyExists`、`InvalidDescriptor`、`InvocationFailed`
和 `InternalError`。错误包含稳定的 code、面向调用者的 message、可选的参数路径和可选
详情 `Value`。

`Result<T>` 是调用边界的错误传递类型；C++20 实现可基于 `std::variant<T, Error>`，并提供
`Result<void>` 特化。公开语义优先于存储实现，不能承诺与未来 `std::expected` 的二进制 ABI
兼容。预期的业务失败应返回 `Result`；异常只表示不可预期情形。

路径规则：顶层参数为 `size`，数组元素为 `points[2]`，嵌套对象为
`shape.size.x`。路径只描述输入位置，不泄露业务内部对象地址或异常文本。

## 5. Action 模型与公开契约

`Module` 是命名空间和元数据容器，`Action` 是最小可调用能力。完整 ID 为
`module.action`；两个部分使用 `[a-z0-9_]+`，恰有一个 `.`，例如 `math.add`。无效 ID 和
无效描述必须在注册前拒绝，且失败不改变 Runtime 状态。

每个 Action 的描述包含 ID、说明、参数列表、返回 `TypeDescriptor`、可选版本与标签。
参数描述包含名称、说明、必填性、类型和（仅在已经定义其转换语义时的）默认值。参数名在
一个 Action 内唯一，描述列表顺序就是对外呈现和校验顺序。

动态调用接口的语义为：

```cpp
Result<Value> invoke(ActionId, const Arguments&, InvocationContext);
```

`InvocationContext` 只携带请求、追踪、调用者及元数据等诊断信息。Core 不得根据
`caller` 选择业务行为。`IAction` 是 Core 内部的多态调用接口，不构成跨编译器或跨版本
的稳定二进制 ABI。

强类型注册 API 应从 callable 签名推导参数和返回类型；`param("name", "说明")`
仅补充名字和文档，不能再次声明 C++ 类型。这样避免 callable 与 descriptor 出现两份类型
真相。Phase 1 只接受可安全适配的普通函数、无状态或可复制 lambda，以及明确支持的成员
函数包装；对泛型 lambda、重载函数对象和带状态/生命周期不清晰的绑定，应在编译期给出
清晰拒绝，而非猜测。

## 6. 调用、转换与异常边界

调用管线固定为：

```text
ActionId -> 查找 -> 参数名/必填校验 -> Value 转换 -> callable -> Value -> Result
```

Registry 只拥有 Action、Module 与描述，并负责冲突检测、查找和有序发现；它不执行业务，
不做转换，也不规定日志策略。Dispatcher 负责解析、结构校验及唯一的异常归一化边界。

Phase 1 的转换集合为：`bool`、有符号整数、浮点、`std::string`、
`std::vector<T>` 和字符串键映射（元素 `T` 同样必须受支持）。允许 `Integer -> double`；
拒绝 `Number -> integer`、隐式字符串解析、窄化整数转换及未知对象映射。返回值支持
`T`、`void`、`Result<T>` 与 `Result<void>`。转换失败必须带输入路径；业务返回的 `Error`
原样保留其 code 与路径。

Dispatcher 捕获业务 callable 抛出的 `std::exception` 并返回 `InvocationFailed`，同时将
未知异常归为 `InternalError`。任何 `Value` 访问、适配器或注册实现异常也必须在此边界前
被转换，绝不能逃逸到适配器、应用或测试进程。

## 7. 生命周期与并发

Phase 1 采用简单、可验证的模型：一个 `Runtime` 在构造期完成 Module/Action 注册；随后只
进行同步调用和查询。注册与调用不能并发，`Runtime` 不承诺线程安全，业务 callable 的
线程安全也由业务层负责。若未来确有并发调用需求，应以不可变注册表快照或明确同步策略
扩展，并同时定义 descriptor 指针/引用的生命周期；不能仅宣称“只读即可并发”。

## 8. 实施顺序与验收

每一步添加针对外部可观察行为的测试，并运行
`uv run --quiet python tools/check.py fast`；若失败，先修复根因再进入下一步。

1. `Value`、`Error`、`Result` 及其单元测试。
2. `ActionId`、`TypeDescriptor` 与描述符验证。
3. Registry 与发现顺序/重复注册测试。
4. `Value` 转换和精确路径测试。
5. `IAction`、Dispatcher 与异常边界测试。
6. 强类型适配器、ModuleBuilder、Runtime 的端到端测试。
7. 仅在核心调用链稳定后，决定日志或外部适配器的独立规格。

测试继续采用仓库现有的独立测试可执行文件和 CTest；引入测试框架只能因实际测试维护需求，
不能为了实现本规格而预先增加依赖。测试应覆盖成功调用、未知 Action、重复 ID、缺失和
未知参数、类型路径、`void`/`Result` 返回、默认值（如已实现）与两类异常归一化；同时保留
现有安装消费测试，验证 `find_package(Axiom)` 和 `Axiom::Core`。

完成一个可发布的 Phase 1 增量前，运行 `fast`、`hardening` 和 `full`。以
`tools/check.py` 及 `quality/` 中的当前定义为准；不得为适配本规格降低架构、覆盖率、格式
或分析门槛。

## 9. 后续演进门槛

只有在有真实消费者和测试时，才分别增加：JSON/JSON Schema 与 Python 适配器、CLI/RPC/
Agent 适配器、`optional`/枚举/结构体映射、异步取消与进度、日志/追踪/指标。每项都必须
保持 `外部类型 <-> Value <-> Runtime` 的单向适配关系，且不得令 Core 了解调用协议或业务
领域。

## 10. 一句话定义

> Axiom::Core 是一个保持最小公开表面的 C++20 基础库；它以 `Module + Action` 描述并同步调用
> 领域能力，以 `Value` 和 `Result` 作为边界模型，并为未来的外部适配器提供可复用而非耦合的基础。
