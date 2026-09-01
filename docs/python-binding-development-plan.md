# Axiom::Python 首版开发文档

> 状态：实施中（MVP 构建、Core Task 查询与 Python 表面已落地；宿主集成和完整 Python 行为矩阵仍需扩展）  
> 对应路线：Roadmap 4 — Python Binding  
> 基线：当前 `main` 工作树中的 Action Runtime、Resource、Task 与 Introspection 接口  
> 定位：`Axiom::Python` 是基于 pybind11 的语言适配层，不拥有、扩展或重新定义 Core Runtime 语义。

## 1. 目标与结论

新增独立的 `Axiom::Python` 模块，将当前 Axiom C++ Runtime 的发现、描述、调用和观察能力
无损暴露给 Python：

```text
Python
  |
  v
Axiom::Python  +  pybind11
  |
  v
Axiom::Axiom
  |- Foundation / Action
  |- Introspection
  |- Resource
  `- Task
```

本模块遵守以下边界：

- C++ Runtime 及其 Descriptor、Query、Error、Task 状态机是唯一事实来源；
- Python 只改变表示方式和错误传递方式，不增加注册、缓存、权限、等待或调度语义；
- pybind11、Python 头文件和 GIL 处理只允许出现在 `src/python/`；
- `Axiom::Axiom` 及现有 Core 头文件不得包含 Python 或 pybind11；
- Descriptor 返回独立值，运行时对象由宿主拥有并显式注入 Python；
- 首版不允许 Python callable 注册为 Action，因此不存在 Binding 自身发起的 C++ → Python
  业务回调。

模块命名固定为：

| 层次 | 名称 |
|---|---|
| CMake target | `axiom_python` |
| 构建树 alias | `Axiom::Python` |
| 安装导出名 | `Axiom::Python` |
| C++ namespace | `axiom::python` |
| Python import | `axiom` |

不使用 `Axiom::PyBind`；pybind11 是实现手段，不是架构概念。

## 2. 当前项目事实与需求映射

首版应直接复用下列现有接口，而不是建立平行模型：

| 需求 | 当前事实来源 | Binding 行为 |
|---|---|---|
| 动态值 | `axiom::Value` / `Arguments` | 与 Python 基础容器递归互转 |
| 失败 | `axiom::Error` / `Result<T>` | 失败统一抛出 `axiom.AxiomError` |
| Action 身份 | `axiom::ActionId` | 解析、字符串表示、比较与 hash |
| 调用上下文 | `axiom::InvocationContext` | Python 值对象，调用时复制到 Core |
| Module / Action 描述 | `ModuleDescriptor` / `ActionDescriptor` | 返回独立只读 Python 值 |
| 参数与返回类型 | `ParameterDescriptor` / `TypeDescriptor` | 递归、只读暴露现有类型描述 |
| Action 运行时 | `axiom::Runtime` | 转发 discovery、describe、invoke |
| 统一发现 | `introspection::IntrospectionService` | 原样转发现有 Query 和 snapshot 语义 |
| Resource | `ResourceId` / `ResourceDescriptor` | 仅发现和描述，不映射资源对象 |
| Task | `TaskId` / `TaskDescriptor` / `TaskRegistry` | 描述、状态、进度、取消与受限结果读取 |

当前实现已经满足的重要前提：

- `Runtime` 的注册、发现、查找和调用支持并发，调用期间不持有注册锁；
- `IntrospectionService` 返回拥有值，但非拥有地引用三个来源，且 snapshot 不是跨 Registry
  原子快照；
- `ResourceDescriptor` 只有 `id` 和逻辑 `type`，Core 没有名称、描述或任意 Resource metadata；
- `TaskDescriptor` 已包含 `id`、`name`、`state`、`progress`、可选 `error` 和可选 `origin`；
- `TaskHandle<T>::result()` 是非阻塞的类型化读取，`TaskRegistry` 当前不能按 `TaskId` 安全读取
  类型擦除后的结果。

### 2.1 对原始范围的两项必要补充

1. **必须暴露 `TypeDescriptor` 与 `TypeDescriptor::Kind`。** 否则 Python 虽能得到
   `ActionDescriptor`，却无法回答参数及返回类型，不能满足验收标准。
2. **Task result 需要一个 Core 前置增量。** Binding 不得直接读取
   `task::detail::TaskControl` 的 `shared_ptr<const void>`，也不得猜测模板参数。第 7.3 节定义
   最小、安全的 Core 扩展。

## 3. 范围

### 3.1 MVP 包含

- `Value` 与 `None/bool/int/float/str/list/dict` 双向转换；
- `ErrorCode`、`AxiomError` 及 `Result<T>` 到返回值/异常的统一适配；
- `ActionId`、`ResourceId`、`TaskId`；
- `InvocationContext`；
- `TypeDescriptor`、`ModuleDescriptor`、`ActionDescriptor`、`ParameterDescriptor`；
- `Runtime` 的 Module/Action discovery、describe 和同步 invoke；
- `ActionQuery`、`ResourceQuery`、`TaskQuery`、`RuntimeSnapshot` 与
  `IntrospectionService`；
- `TaskState`、`Progress`、`TaskOrigin`、`TaskDescriptor`；
- Task 的 describe、state、progress、cancel 和非阻塞 result；
- Resource discovery 与 describe；
- 构建、安装和 `import axiom` smoke test；
- 值转换、错误保真、生命周期、并发和 GIL 回归测试。

### 3.2 MVP 不包含

- Python 注册 Action、`@axiom.action` 或 Python callable 回调；
- Python Plugin 或 Python-defined Resource；
- 任意 C++ Resource 对象到 Python object 的映射或 `resolve()`；
- Task 提交、Task change Python 回调、阻塞等待或 `asyncio`；
- JSON、JSON Schema、Agent Tool Adapter、MCP、RPC；
- wheel/PyPI 发布流程和多 Python ABI 一包多用；
- Python 对 Module、Action、Resource 或 Task 的修改能力。

## 4. 代码与依赖结构

建议结构在需求草案上增加一个模块内部声明头和独立 Python 测试目录：

```text
src/python/
|- CMakeLists.txt
|- include/axiom/python/
|  |- conversion.hpp       # 模块私有；Value/Error 转换声明
|  `- error.hpp            # 模块私有；AxiomError 注册与抛出
`- src/
   |- bindings.hpp          # 各 bindXxx() 的模块内部声明
   |- module.cpp
   |- value_binding.cpp
   |- action_binding.cpp
   |- introspection_binding.cpp
   |- task_binding.cpp
   `- resource_binding.cpp

tests/python/
|- CMakeLists.txt
|- host_fixture.cpp         # 仅测试使用，创建已注册的 C++ Runtime/Registry
|- test_value.py
|- test_action.py
|- test_introspection.py
|- test_task.py
`- test_resource.py
```

`conversion.hpp` 与 `error.hpp` 位于 Python 模块自己的 include 树，但首版不安装，避免形成
额外的 C++ 公共 API。若未来宿主确需复用这些函数，应另行稳定其 ABI、Doxygen 契约和导出符号，
不能直接把首版内部头当作承诺。

依赖规则为：

```text
axiom_python -> Axiom::Axiom
axiom_python -> pybind11::module

Axiom::Axiom -X-> axiom_python
Core source  -X-> Python.h / pybind11/* / axiom/python/*
```

`quality/architecture_rules.json` 应增加两类检查：

- `src/foundation`、`action`、`resource`、`task`、`introspection`、`logging`、`events`、
  `async` 及所有 `include/axiom/<core>/` 禁止包含 `axiom/python/`；
- 除 `src/python/` 和 `tests/python/` 外，项目源码禁止直接包含 `pybind11/` 或 `Python.h`。

## 5. 构建与安装设计

### 5.1 CMake

顶层新增：

```cmake
option(AXIOM_BUILD_PYTHON "Build the Axiom Python extension" OFF)

if (AXIOM_BUILD_PYTHON)
    add_subdirectory(src/python)
endif ()
```

默认关闭，保证嵌入 Axiom 的纯 C++ 工程不会查找 Python、下载 pybind11 或改变既有构建图。
启用后：

- 使用 `find_package(Python3 REQUIRED COMPONENTS Interpreter Development.Module)`；
- 使用项目现有 CPM 机制获取并固定 pybind11 版本；如采用系统包，版本下限也必须固定并在配置时
  校验，不能静默接受任意版本；
- 以 `pybind11_add_module(axiom_python MODULE ...)` 创建扩展；
- 设置 `OUTPUT_NAME axiom`，使产物可被 `import axiom` 加载；
- `target_link_libraries(axiom_python PRIVATE Axiom::Axiom pybind11::module)`；
- 调用现有 `axiom_configure_target()`，使用 C++20、统一告警和 sanitizer 策略；
- 无论 `BUILD_SHARED_LIBS` 为何，Python 产物始终是 Python `MODULE`；Core 继续按现有静态/共享
  预设构建，当前 `POSITION_INDEPENDENT_CODE ON` 可支持静态 Core 链入扩展；
- 创建 `add_library(Axiom::Python ALIAS axiom_python)`；安装导出时使用 `EXPORT_NAME Python`。

不得把 pybind11 链到 `axiom` Core target，也不得把它加入 `AxiomConfig.cmake` 的 Core 必需依赖。
只有安装包包含并消费 Python C++ target 时，Python 组件配置才声明相应依赖。

### 5.2 安装边界

首版安装必须同时验证两条路径：

1. 不启用 `AXIOM_BUILD_PYTHON` 时，现有 C++ 安装内容和 `find_package(Axiom)` 行为完全不变；
2. 启用时，把扩展安装到由 `AXIOM_PYTHON_INSTALL_DIR` 指定的相对目录，并在 staged install
   测试中把该目录加入 `PYTHONPATH` 后执行 `python -c "import axiom"`。

`AXIOM_PYTHON_INSTALL_DIR` 的默认值应由当前 Python 解释器的 `sysconfig` 计算为安装前缀下的
相对 site-packages 路径；若无法得到可重定位的相对路径，配置应失败并要求调用者显式指定，
不得写入开发机的绝对系统 site-packages。wheel 打包留待后续路线。

## 6. Python API 契约

Python 命名采用 `snake_case`，但每个操作必须能一一映射到已有 Core 方法或值字段。Descriptor、
ID、Query 和 Context 均作为值对象；Descriptor 属性只读。

### 6.1 Value

转换表：

| Axiom | Python | 约束 |
|---|---|---|
| `Null` | `None` | 双向唯一映射 |
| `Boolean` | `bool` | Python 输入时必须先于 `int` 判断 |
| `Integer` | `int` | 仅接受 `int64_t` 范围，越界抛 `OverflowError` |
| `Number` | `float` | 按 C++ `double` 映射，不私自改变 NaN/Inf 规则 |
| `String` | `str` | Python Unicode 以 UTF-8 复制；非法 C++ UTF-8 转出抛 `UnicodeError` |
| `Array` | `list` | 递归复制，不返回 C++ 容器引用 |
| `Object` | `dict` | key 必须是 `str`，按 Python 插入结果复制到 Core 的有序 map |

Python `tuple`、任意 `Mapping`、NumPy scalar、dataclass 和用户自定义隐式转换首版不接受，避免
产生 Core 不存在的宽松输入规则。Python list/dict 可以自引用，而 `Value` 必须是树；转换时使用
当前递归栈检测环，遇到环抛 `ValueError`。同一非循环容器在不同分支重复出现允许重复复制。

转换失败属于 Python 调用边界错误，使用 `TypeError`、`ValueError`、`OverflowError` 或
`UnicodeError`；只有来自 Axiom `Result` 的失败才转换为 `AxiomError`。

### 6.2 Error / Result

公开 `axiom.ErrorCode` 枚举，成员与 C++ `ErrorCode` 一一对应。公开异常：

```python
class AxiomError(Exception):
    code: ErrorCode
    message: str
    path: str | None
    details: object | None
    has_details: bool
```

规则：

- `Result<T>` 成功：返回转换后的 `T`；
- `Result<void>` 成功：返回 `None`；
- 任意 `Result` 失败：抛 `AxiomError`；
- `str(error)` 等于 `message`；
- `code` 保留枚举值，`path` 保留缺省与文本，`details` 通过 Value 转换；
- `has_details` 对应 `Error::details.has_value()`，用于区分“无 details”和“details 是 Axiom
  Null”；
- 不把 Axiom failure 改写为 `KeyError`、`RuntimeError` 等另一套异常分类。

内部使用一个 `throw_if_error`/`unwrap` 路径，避免每个 binding 文件分别实现错误映射。

### 6.3 ID 与值对象

`ActionId`、`ResourceId`、`TaskId`：

- Python 构造函数接收规范字符串并调用各自的 C++ `parse()`；
- 非法文本抛保存原始 `InvalidArgument` 的 `AxiomError`；
- `str(id)` 返回规范完整文本，`repr` 包含类型名与规范文本；
- `ResourceId` 与 `TaskId` 按现有 Core 相等/排序规则比较并可 hash，不同 ID 类型不相等；
- 当前 `ActionId` 没有 C++ 相等与 hash 运算，首版不在 Python 单独发明；如需要该能力，应先在
  Action Core 补齐规范 ID 的值语义和 C++ 测试，再原样绑定；
- `ActionId` 额外公开只读 `module` 与 `action`。

`InvocationContext` 公开关键字参数 `request_id`、`trace_id`、`caller` 和 `metadata`，默认值与
C++ 默认构造一致。metadata 只接受 `dict[str, str]`，调用时复制，不保存 Python 引用。

`Progress`、`TaskOrigin`、各 Descriptor、Query 和 `RuntimeSnapshot` 都返回独立值。嵌套 list、
dict 也返回副本，Python 对返回容器的修改不能改变 Core 或后续查询。

### 6.4 Action Runtime

生产环境中的 `Runtime` 对象由 C++ 宿主注入；Python 不公开 `Runtime()` 构造函数，也不公开
`register_module`。推荐表面：

```python
runtime.modules() -> list[ModuleDescriptor]
runtime.actions() -> list[ActionDescriptor]
runtime.describe_module(namespace: str) -> ModuleDescriptor
runtime.describe_action(action: str | ActionId) -> ActionDescriptor
runtime.invoke(
    action: str | ActionId,
    arguments: dict[str, object],
    context: InvocationContext | None = None,
) -> object
```

其中：

- `modules/actions` 分别转发 `discoverModules/discoverActions` 并深复制 Descriptor；
- describe 转发 `findModule/findAction`；
- 字符串 Action ID 必须先调用 `ActionId::parse()`；
- arguments 只接受 `dict[str, Value-compatible]`，转换为 `Arguments`；
- invoke 保持同步，不创建 Task、不重试、不做超时或权限判断；
- Core 返回的 `Result<Value>` 按第 6.2 节处理。

示例：

```python
result = runtime.invoke(
    "mesh.rebuild",
    {"quality": 0.8},
    InvocationContext(request_id="req-1", caller="python"),
)
```

### 6.5 Descriptor 与 TypeDescriptor

字段名直接采用现有 C++ 字段的 Python `snake_case` 形式。`TypeDescriptor` 至少公开：

```text
kind, nullable, description, element_type, fields, value_type
```

`ModuleDescriptor` 公开：

```text
namespace_name, description, version, tags, metadata
```

`ActionDescriptor` 公开：

```text
id, description, parameters, return_type, version, tags, metadata
```

`ParameterDescriptor` 公开：

```text
name, description, required, type, default_value
```

可选 `default_value` 与 Axiom Null 都映射为 Python `None`，仅凭属性值无法区分“无默认值”和
“默认值为 Null”。为保真，额外提供只读 `has_default_value: bool`；它只是
`std::optional::has_value()` 的表示适配，不是新业务语义。

### 6.6 Introspection

Query 是可构造值对象，字段完全对应当前 Core：

```python
ActionQuery(module: str | None = None, tags: list[str] = [])
ResourceQuery(type: str | None = None)
TaskQuery(
    state: TaskState | None = None,
    origin_action_id: str | None = None,
    origin_request_id: str | None = None,
)
```

`IntrospectionService` 由宿主注入，不允许 Python 自行拼接非拥有来源。公开：

```python
introspection.modules()
introspection.actions(query: ActionQuery | None = None)
introspection.describe_action(id)
introspection.resources(query: ResourceQuery | None = None)
introspection.describe_resource(id)
introspection.tasks(query: TaskQuery | None = None)
introspection.describe_task(id)
introspection.snapshot()
```

Python 的可选 Query 参数只在空值时选择现有无参重载，在有值时选择现有 Query 重载。匹配、
all-of tags、校验、排序、NotFound 及空列表语义全部来自 Core。`snapshot()` 明确是按
Module/Action、Resource、Task 顺序采样的非原子观察，Binding 不加缓存或全局锁。

宿主返回的 `Host` 还提供 `host.task(id) -> Task`，用于把现有 `TaskRegistry` 与 `TaskId`
组合成第 6.7 节的控制代理。它不提交、复制或重新注册 Task。

示例：

```python
actions = introspection.actions(
    ActionQuery(module="geometry", tags=["read"])
)
```

### 6.7 Task

公开 `TaskState`、`Progress`、`TaskOrigin`、`TaskDescriptor`，字段与 Core 一一对应。再提供一个
非拥有业务对象 `Task`，内部只保存宿主生命周期令牌、`TaskRegistry` 引用和 `TaskId`：

```python
task.id
task.describe() -> TaskDescriptor
task.state() -> TaskState
task.progress() -> Progress
task.cancel() -> None
task.result() -> object | None
```

- `describe/state/progress` 每次从 Registry 读取当前快照，不缓存 Descriptor；
- `cancel()` 转发 `TaskRegistry::cancel()`，未知/已移除任务抛 `AxiomError`；
- `result()` 非阻塞，运行中返回 `None`；成功返回 Value 转换结果，失败或取消抛
  `AxiomError`；
- 成功的 Axiom Null 也返回 `None`。调用方通过 `state()` 区分“尚未完成”和“完成且结果为
  Null”，Binding 不引入阻塞 Future 语义；
- 对非 `Value`/`void` 结果任务，Core 返回 `TypeMismatch`，Python 原样抛 `AxiomError`；
- `void` 成功返回 `None`；
- Python 不提供 submit、remove、事件订阅或 TaskContext。

`IntrospectionService.tasks()` 仍返回 Descriptor 列表；需要控制某一 Task 时，由宿主暴露的
Task 访问入口按 `TaskId` 创建上述代理。该入口只组合已有 Registry 和 ID，不改变 Task 的身份
或生命周期。

### 6.8 Resource

公开 `ResourceId`、`ResourceDescriptor` 以及 Introspection 的 resources/describe_resource。
Descriptor 只有 `id` 和 `type`。首版不公开 `ResourceRegistry.add/resolve/remove`、
`Handle<T>`、`ResourceRef<T>` 或真实资源对象；不得从 RTTI、地址或 ID 猜测名称和 metadata。

## 7. 必须先确定的 Core / 宿主契约

### 7.1 宿主注入

当前项目没有拥有 Action、Resource、Task 三者的应用总 Runtime，`IntrospectionService` 本身也
是非拥有聚合。单独 `import axiom` 无法凭空找到某个应用进程中的 Registry。因此生产入口固定为：

1. C++ 宿主以 `std::shared_ptr` 创建并填充 `Runtime`、`ResourceRegistry`、`TaskRegistry`；
2. 宿主先导入 `axiom` 以注册三个非公开、不可由 Python 构造的 pybind11 holder 类型；
3. 宿主用 `py::cast(shared_ptr)` 把三个 holder 传给模块内部 `_attach()`；
4. `_attach()` 返回公开的 `Host`，其只读属性为 `runtime` 和 `introspection`，并提供
   `task(id)`；
5. `Host`、Runtime/Task/Introspection wrapper 共同持有 Binding-owned `HostContext`；Context
   持有三个 `shared_ptr` 并在其上构造 `IntrospectionService`；
6. Python 不能构造空 Runtime、Registry 或 Host，也不能替换 Host 的来源。

`_attach()` 是 C++ 宿主接线入口，不是面向普通 Python 业务代码的注册 API。它只接受已由
当前 `axiom` 模块登记且带正确 `shared_ptr` holder 的三种 Core 对象；任意 capsule、整数地址
或裸指针均拒绝。宿主接线 helper 放在 `axiom::python` 自己的头/源中，pybind11 不进入 Core。
不得用裸指针加文档约定代替可验证的生命周期，也不得让 Python 对象延长 Descriptor 内部
引用——Descriptor 必须始终复制。

Context 的成员声明顺序必须让 `IntrospectionService` 先析构，三个来源后析构。Runtime wrapper
只持有 Context，不拥有第二份 Runtime；Introspection wrapper 同理。该设计也为 `host.task(id)`
提供安全的 Registry keepalive。

测试专用 `host_fixture` 按同一方式注入对象，不能加入生产模块或形成隐藏的全局单例。

### 7.2 并发与 GIL

- Value 转换、Python 对象构造和抛 Python 异常时必须持有 GIL；
- Core 的线程模型不得查询、持有或依赖 GIL；
- discovery/describe 通常只复制小型值，首版保持 GIL，避免无收益的边界切换；
- `Runtime::invoke()` 只有在 arguments 与 context 已完全复制到 C++ 后才能释放 GIL，并在读取
  Python 返回值或抛异常前重新获取；
- 默认不释放 GIL。只有宿主明确声明本 Runtime 的 Action 及其日志 sink 不依赖“调用线程已持有
  GIL”这一隐式条件，并通过并发测试后，才启用 invoke 的 GIL release 路径；
- 即便未来释放 GIL，也不改变 Runtime 允许同一 Action 重叠调用、由 callable 自行同步的契约；
- Python wrapper 析构与 Core 来源析构不得并发，统一由 `HostContext` 的所有权顺序保证。

首版不绑定 `TaskRegistry::onChanged()`，因此 Binding 不需要从工作线程获取 GIL并回调 Python。

### 7.3 Task 动态结果的最小 Core 前置增量

当前 `TaskControl` 用 `shared_ptr<const void>` 保存 `Result<T>`，只有持有正确
`TaskHandle<T>` 的 C++ 调用者能安全读取。Python 按 `TaskId` 发现任务后没有模板参数，直接
`static_pointer_cast<Result<Value>>` 会产生未定义行为。

在 Task 所有模块内增加最小类型标记和只读结果查询，建议公共契约为：

```cpp
enum class TaskResultKind : std::uint8_t { Void, Value, Opaque };

struct TaskResultSnapshot final {
    TaskResultKind kind{TaskResultKind::Opaque};
    std::optional<Result<Value>> value;
};

[[nodiscard]] Result<TaskResultSnapshot>
TaskRegistry::result(const TaskId& id) const;
```

准确语义：

- 未知或已移除 ID：`NotFound`；
- Pending/Running：成功返回对应 kind 且 `value == std::nullopt`；
- `T == Value` 的终态：返回复制的 `Result<Value>`；
- 任意 `T` 的 Failed/Cancelled 终态：从已保存的 `Error` 返回失败
  `Result<Value>`，确保业务失败与取消优先保真；
- `T == void` 成功：返回 `Void`；
- 其他 `T` 成功：返回 `kind == Opaque`，Binding 将其映射为 `TypeMismatch`；
- 查询不等待、不移除任务、不改变状态，返回值不引用内部存储；
- 类型标记在 submit 接受时写入并在任务生命周期内不可变；
- 保留现有 `TaskHandle<T>::result()` 行为和签名。

`TaskResultSnapshot` 属于 Task Core，因为“任务结果是否可动态表示为 Axiom Value”是跨语言适配
共同需要的可观察事实；Python 模块只消费它。若团队不接受这项 Core API，首版必须明确删去
Python `Task.result()`，不能通过访问 `detail` 或只支持碰巧取得的 `TaskHandle<Value>` 来伪装
满足验收。

## 8. 实现组织

### 8.1 `module.cpp`

只负责 `PYBIND11_MODULE(axiom, module)`、模块文档、异常注册及绑定函数调用顺序。顺序固定为：

```text
ErrorCode / AxiomError
Value helpers and TypeDescriptor
IDs and shared value types
Action Runtime
Introspection
Task
Resource
```

不同翻译单元通过 `src/bindings.hpp` 的小型 `bindXxx(py::module_&)` 接口协作，不共享可变全局
状态。

### 8.2 转换与异常

- `conversion.hpp/.cpp`（实现可位于 `value_binding.cpp`）集中处理 Value 递归转换、整数范围、
  dict key 和环检测；
- `error.hpp` 集中注册 `AxiomError` 并从 `Error` 设置四个属性；
- 为常用 `Result<Value>`、`Result<void>`、Descriptor Result 和列表 Result 提供小型内部 unwrap
  helper，不把 `Result<T>` 注册成 Python 类；
- C++ `std::bad_alloc`、pybind11 转换异常和 Python 异常沿 pybind11 既有规则传播，不伪装成
  Axiom business error。

### 8.3 绑定值的可变性

ID 与 Descriptor 的 Python 属性默认只读。Query 和 InvocationContext 是调用输入，可以构造并
修改自身字段，但每次进入 Core 前完整复制和校验。不要用 `reference_internal` 暴露
`TypeDescriptor` 的 shared pointer、Runtime 的 descriptor 引用或 Registry entry。

## 9. 分阶段开发顺序

每一步保持可独立构建和验证：

1. **构建骨架**：增加 `AXIOM_BUILD_PYTHON`、固定 pybind11、创建 `axiom_python`/
   `Axiom::Python`、空模块 import 测试和架构规则。
2. **Value**：实现七类递归互转、范围/类型/环/UTF-8 边界测试。
3. **Error / Result**：注册 `ErrorCode` 与 `AxiomError`，验证 code/message/path/details 保真。
4. **描述基础**：绑定三类 ID、InvocationContext、TypeDescriptor、Module/Action/Parameter
   Descriptor。
5. **Runtime**：实现宿主 Context 与注入、discovery、describe、同步 invoke；先保持 GIL。
6. **Introspection**：绑定三个 Query、RuntimeSnapshot 和 Service，验证过滤、排序、深复制及
   非原子 snapshot 文档契约。
7. **Task Core 前置**：在 Task 模块实现 result kind 与按 ID 的只读动态结果查询，保持现有
   TaskHandle API。
8. **Task Binding**：绑定状态与描述值、代理查询、取消和非阻塞 Value/void result。
9. **Resource Binding**：只绑定 ID、Descriptor、discovery 和 describe。
10. **生命周期/并发/GIL**：验证 HostContext 销毁顺序、多 Python 线程 invoke、异常路径；审核并
    决定是否启用可选 GIL release。
11. **安装交付**：扩展 staged install consumer，完成静态/共享 Core 组合和 `import axiom`
    smoke test，同步 README、架构文档和路线状态。

每个 coherent increment 运行 `checkflow fast`；Task Core 前置、宿主生命周期和 GIL 等实质语义
完成后运行 `checkflow hardening`；最终候选运行 `checkflow full`。不通过排除 Python 源、跳过
测试、降低覆盖率或 suppress 诊断来通过 Gate。

## 10. 测试设计

### 10.1 Value 与错误

- 七种 Value 类型双向往返，嵌套 Array/Object 和空容器；
- `bool` 不被当成 Integer；int64 最小/最大值通过，边界外抛 `OverflowError`；
- 非字符串 dict key、tuple、自定义对象拒绝；直接/间接循环容器拒绝；
- Unicode 正常往返，非法 C++ UTF-8 转出失败；
- 每个 ErrorCode、缺省/非缺省 path、缺省/Null/嵌套 details；
- `AxiomError.args`/`str()`、四个 Error 字段与 `has_details` 稳定。

### 10.2 Descriptor / Runtime / Introspection

- ID 合法/非法解析、字符串、比较与 hash；
- 递归 TypeDescriptor、可选默认值和 `has_default_value`；
- 测试宿主注册真实 C++ Action，Python 完成 discover、describe、invoke；
- 缺参、未知参数、类型不匹配、业务失败和异常归一化保留 Core Error；
- InvocationContext 四类字段原样进入 contextual Action；
- Query 的精确、AND、all-of、空条件、合法无匹配和非法 Resource type；
- 所有列表保持 Core 顺序，返回对象修改不影响后续查询；
- snapshot 字段完整，并发变化下只验证来源级一致性，不声称全局原子。

### 10.3 Task / Resource

- Task 五种状态、进度消息、origin、可选 error 的字段保真；
- 已知/未知/已移除 Task 的 describe 与 cancel；
- Pending/Running result 非阻塞返回 `None`；Value 成功、Null 成功、void 成功、业务失败、取消；
- 非 Value C++ 任务 result 返回 `TypeMismatch`，无错误类型转换或未定义行为；
- Resource 多类型发现与稳定排序，describe 的成功/NotFound；
- Resource Descriptor 不延长对象生命，不提供 resolve 或内部对象属性。

### 10.4 生命周期、并发与安装

- Python wrapper 持有 HostContext 时来源不会提前析构；释放最后一个 wrapper 后按
  IntrospectionService → Registry 的安全顺序销毁；
- 多 Python 线程对 discovery、describe、invoke、Task query/cancel 的行为符合各 Core 契约；
- GIL release 路径若启用，使用 barrier/promise 证明两个长 invoke 可重叠，不使用 sleep；
- 失败转换和异常构造始终在持有 GIL 时发生；
- `AXIOM_BUILD_PYTHON=OFF` 的嵌入构建不出现 Python/pybind11 target；
- 静态和共享 Core 分别构建扩展并运行 Python 测试；
- staged install 后从非源码目录 `import axiom` 并执行一次真实 discover/invoke smoke test；
- 解释器版本、扩展 ABI 后缀和 Debug/Release 配置匹配，不能用源码目录中的产物误通过安装测试。

Python 测试由 CTest 启动，使用目标文件目录构造隔离的 `PYTHONPATH`。同步测试使用 barrier、
promise 或条件变量，不依赖时间睡眠。C++ 单元测试继续使用 GoogleTest，重点覆盖 Task Core
前置 API 和转换 helper；Python 表面行为由 Python 脚本覆盖。

## 11. 验收场景

测试宿主至少注册：

- 一个带 description、tags、metadata、默认参数和递归返回 TypeDescriptor 的 Action；
- 一个成功 Action、一个返回结构化 Error 的 Action、一个可控并发的长调用 Action；
- 两种 Resource 注册；
- Pending、Running、Completed、Failed、Cancelled Task，其中至少一个结果为 `Value`、一个为
  `void`、一个为 opaque C++ 类型。

Python 必须能完整回答并执行：

```python
modules = runtime.modules()
actions = introspection.actions(ActionQuery(module="geometry"))
descriptor = runtime.describe_action("mesh.rebuild")

result = runtime.invoke(
    "mesh.rebuild",
    {"quality": 0.8},
    InvocationContext(request_id="req-1", caller="python"),
)

resources = introspection.resources()
tasks = introspection.tasks()
task = host.task(tasks[0].id)
state = task.state()
progress = task.progress()
task.cancel()
value = task.result()  # 非阻塞；按 state 判断 None 的含义
```

验收答案必须来自当前 C++ Descriptor、Query、Registry 和 Result，不得从 Python 侧维护镜像注册
表、重算状态或生成 Core 不存在的 Resource 元数据。

## 12. 完成定义

Roadmap 4 首版完成需同时满足：

- `axiom_python`、`Axiom::Python` 和 `import axiom` 命名正确；
- pybind11/Python 依赖没有进入 Core 的 target、头文件或包依赖；
- Value、Error、Descriptor、Query 和 Snapshot 的 Python 表示保真；
- Python 能发现并同步调用宿主已注册 Action；
- Resource 仅暴露现有描述能力；
- Task 能描述、观察、取消并安全读取 Value/void 结果，opaque 结果明确失败；
- Descriptor、Runtime、Introspection、Task wrapper 的所有权与销毁顺序有自动化测试；
- GIL 策略经过测试且文档与实际行为一致；
- `checkflow full`、要求的 hardening、静态/共享构建及 staged install/import 测试实际运行并通过；
- README、`docs/architecture/architecture.md`、路线文档和公共 Python API 说明同步更新；
- 没有已知影响上述场景的未解决问题。

一句话定位：

> `Axiom::Python` 是基于 pybind11 的宿主注入式语言适配层，用 Pythonic 表示无损暴露现有
> Axiom C++ Runtime；它不拥有业务注册、不复制 Core 模型，也不把 Python 或 GIL 反向带入
> Core。
