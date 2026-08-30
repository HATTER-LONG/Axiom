# Axiom Core Framework — 当前设计

> 状态：以当前实现为准（0.1.x）
> 范围：`Axiom::Core` 的动态值、模块/Action 注册、发现与同步调用

## 1. 目标与边界

Axiom 当前提供一个可安装、领域无关的 C++20 Core：把 C++ callable 以 `Module + Action`
描述和注册，在统一的动态值边界上同步调用，并返回结构化错误。HTTP、CLI、Python、Agent
或 JSON 适配器应位于 Core 之外。

当前不包含异步/取消/进度、权限、中间件、插件加载、网络协议、JSON 序列化、日志、指标
和追踪后端。`InvocationContext` 只传递宿主提供的诊断字段，不参与业务路由或授权。

## 2. 分层和目录

```text
应用 / 未来适配器
          |
          v
     Axiom::Core
   base  <-  action
          |
          v
     业务 callable
```

`base` 不依赖 `action`、应用或测试；`action` 依赖 `base`，但不依赖前端。架构检查目前
禁止 `src/core/` 直接包含 `apps/` 或 `tests/` 的头文件。

```text
src/core/include/axiom/core/
├── core.hpp                         # 公开入口，包含稳定 Core 头
├── base/{value,error,result,type_descriptor}.hpp
└── action/
    ├── {action_id,descriptor,invocation_context,module,module_builder,runtime}.hpp
    └── detail/                      # 内部 Registry、Dispatcher 和类型适配器
src/core/src/
├── core.cpp
├── base/type_descriptor.cpp
└── {action/descriptor.cpp,dispatcher.cpp,module_builder.cpp,registry.cpp,runtime.cpp}
```

`action/detail` 中的 `IAction`、`Registry`、`Dispatcher`、`TypedActionAdapter` 和
`ValueConverter` 是实现细节，不构成稳定的安装者 API。

## 3. 边界数据和错误

`Value` 支持 `Null`、`Boolean`、`Integer`（`int64_t`）、`Number`（`double`）、`String`、
`Array` 和 `Object`。Object 使用按字典序排列的 `std::map<std::string, Value>`，Array 使用
`std::vector<Value>`；容器 payload 通过不可变共享存储持有，读取接口只暴露 const 引用。
`Arguments` 是命名参数 Object。

`TypeDescriptor` 描述标量、数组元素、精确字段对象或同质值对象，并支持 `nullable`、描述
文本和递归嵌套。注册前会校验结构、命名、参数唯一性、版本非空，以及默认值是否匹配类型。

`Result<T>`/`Result<void>` 以 `std::variant` 表示成功或 `Error`。Error code 包括：
`InvalidArgument`、`MissingArgument`、`UnknownArgument`、`TypeMismatch`、`NotFound`、
`AlreadyExists`、`InvalidDescriptor`、`InvocationFailed` 和 `InternalError`。错误可带输入
path 与结构化 details；路径支持 `size`、`points[2]`、`shape.size.x` 及带转义的
`shape["a.b"]`。

## 4. Action 模型和注册

`ModuleDescriptor` 由 `[a-z0-9_]+` 命名空间和字符串元数据组成。`ActionId` 必须严格是
`module.action`，且只能包含一个句点。`ActionDescriptor` 包含 ID、说明、参数列表、返回
类型、可选版本和标签；参数包含名称、说明、必填性、推导出的类型和可选默认值。

`ModuleBuilder` 是 detached、move-only 的暂存对象。`add()` 从 callable 签名推导参数和
返回类型，`param(name, description, default)` 只提供文档和默认值，不重复声明 C++ 类型。
它接受普通函数、无歧义且可复制的 callable（包括可复制 lambda）；泛型/重载/非可复制/
type-erased callable 以及裸成员函数指针在编译期拒绝。`bindMember()` 通过 `std::shared_ptr`
显式保持成员函数接收者生命周期。

`Runtime::registerModule()` 在一次提交中校验模块、所有 Action、实现对象和冲突；成功后
才改变 Registry，失败保持原状态不变。注册成功会消耗 builder，失败则 builder 仍可处理。
Runtime 允许在其生命周期内继续注册模块，但注册、发现和调用不得并发。

## 5. 调用链和转换规则

```text
ActionId -> Registry 查找 -> 参数校验 -> Value 转换 -> C++ callable -> Value -> Result<Value>
```

支持的参数/返回类型为 `bool`、有符号整数、浮点类型、`std::string`、递归 `std::vector<T>`，
以及字符串键的 `std::map`/`std::unordered_map`。容器策略需要可默认构造。输入允许 Integer
转浮点，整数窄化必须在目标范围内；拒绝 Number 转整数、隐式字符串解析和不支持的领域对象。
`void` 返回 Null；也支持 `Result<T>` 与 `Result<void>`，业务 Error 原样传播。

`Dispatcher` 是动态调用异常边界：查找和结构校验后取得内部实现，再由 adapter 完成转换
和调用。业务 callable、Value 访问或 adapter 抛出 `std::exception` 时返回 `InvocationFailed`；
未知异常返回 `InternalError`。

## 6. Registry、生命周期与线程模型

内部 Registry 按 namespace 和完整 Action ID 有序保存模块与 Action；发现结果分别按命名空间
和完整 ID 升序返回。它拥有描述符和实现，不执行 Action、不做 Value 转换、不定义日志策略。
注册使用准备后交换的状态提交方式，并复制描述符的递归 TypeDescriptor；查询返回的引用在
所属 Runtime 销毁前有效。

Runtime、Registry 和 callable 当前均不保证线程安全。注册、发现、调用不能并发；callable
自身的并发安全由业务层负责。公开 API 不暴露第三方实现类型。

## 7. 可观察验证

GoogleTest 覆盖 Value、Result/Error、描述符校验、Action ID、容器转换和错误路径，以及
Registry 冲突/有序发现、Runtime 端到端调用、默认参数、`void`/`Result` 返回、业务错误和
异常归一化。demo 注册 `math.add` 与 `math.divide`，展示成功调用、业务错误和未知参数错误。
安装消费测试验证 `find_package(Axiom CONFIG)` 与 `Axiom::Core` 导出目标。

构建契约由 CMake 3.25+ 和 C++20 定义，生产 target 为默认静态、可选动态的 `Axiom::Core`（内部 target 名
`axiom_core`），公开头安装到 `include/axiom/core`。质量流程以 `checkflow.json` 为准：
`fast` 执行架构、配置、构建、CTest 和覆盖率；`full` 增加格式、复杂度、cppcheck 和
clang-tidy，以及独立的静态/动态库安装消费测试；`hardening` 执行 ASan/UBSan 测试及
Mull 变异测试。覆盖率和 hardening 均使用静态 Core，动态兼容性使用独立构建目录。

## 8. 演进约束

新增 JSON/Schema、协议适配器、异步能力、权限或可观测性前，必须先有真实消费者和测试，
并保持 `外部类型 <-> Value <-> Runtime` 的单向适配关系。稳定公共 API 应继续保持薄，优先
在 `detail` 或拥有行为的模块内演进；不得让 Core 依赖业务领域或调用协议。

> 一句话定义：Axiom::Core 是一个以 `Module + Action` 描述并同步调用 C++ 能力、以 `Value`
> 和 `Result` 作为统一边界模型、为外部适配器提供最小基础的 C++20 库。
