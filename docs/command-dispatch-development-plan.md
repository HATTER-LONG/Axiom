# Axiom Command Dispatch 开发计划

## 1. 结论

当前仓库已经具备 Command Dispatch 所需的大部分业务事实来源：

- `Runtime` 提供 Action 的注册、发现与同步调用；
- `IntrospectionService` 提供 Module、Action、Resource、Task 的只读聚合查询；
- `ResourceRegistry` 已提供稳定的 `list()` / `describe()`；
- `TaskRegistry` 已提供 `list()` / `describe()` / `cancel()`；
- `Value`、`Result<T>`、`Error` 和 `InvocationContext` 已能作为协议无关的动态边界模型。

因此本需求不应再造一套 Runtime、Registry 或 Descriptor，也不应把 JSON、Python、MCP
概念带入 Core。正确的增量是新增一个位于 Core 服务之上的 `axiom::command` 协调层，负责：

```text
method + Value::Object + InvocationContext
                    |
                    v
        CommandDispatcher
          |       |       |
          v       v       v
       Runtime  Introspection  TaskRegistry
```

同时应处理当前命名上的职责重叠：仓库已有私有 `axiom::detail::Dispatcher`，它实际只负责
Action 查找、Action 参数结构校验、调用和异常归一化。它不是外部 Command 路由器。建议把该
私有类型重命名为 `ActionInvoker`，公共类型使用完整名称
`axiom::command::CommandDispatcher`。二者保持分层，不能合并成一个既懂 Action 实现又懂外部
命令协议的对象。

MVP 可以完整实现以下九个命令，无需先扩充 Resource 或 Task 的业务模型：

```text
action.list       action.describe       action.invoke
resource.list     resource.describe
task.list         task.describe         task.cancel
system.snapshot
```

`task.result`、Task 提交、Resource 解析、订阅、权限、JSON/Python/MCP 实现不进入此 MVP。

## 2. 当前代码基线与差距

### 2.1 可直接复用的能力

| 需求 | 当前事实来源 | 结论 |
| --- | --- | --- |
| Action 列表与过滤 | `IntrospectionService::actions(ActionQuery)` | 直接复用 |
| Action 描述 | `IntrospectionService::describeAction()` | 直接复用 |
| Action 调用 | `Runtime::invoke()` | 直接复用；业务参数校验继续由 Runtime 权威实现 |
| Resource 列表与过滤 | `IntrospectionService::resources(ResourceQuery)` | 直接复用 |
| Resource 描述 | `IntrospectionService::describeResource()` | 直接复用 |
| Task 列表与过滤 | `IntrospectionService::tasks(TaskQuery)` | 直接复用 |
| Task 描述 | `IntrospectionService::describeTask()` | 直接复用 |
| Task 取消 | `TaskRegistry::cancel()` | 直接复用；成功转换为 `Value{nullptr}` |
| 系统快照 | `IntrospectionService::snapshot()` | 直接复用并转换为 `Value` |
| 调用关联 | `InvocationContext` | 原样传给 `Runtime::invoke()` |
| 动态参数和结果 | `Value::Object` / `Result<Value>` | 直接作为 Command 边界 |

### 2.2 需要新增的能力

1. 稳定的 Command method 集合和 method 解析；
2. Command 自身参数的严格结构校验；
3. 所有公开 Descriptor、Snapshot、Task 状态和 Error 的规范 `Value` 编码；
4. 未知 Command 的可区分错误码；
5. 公共 CommandDispatcher 生命周期、并发和异常契约；
6. 面向安装消费者的公共头、导出符号、CMake 与架构规则；
7. 为后续 Python / JSON / MCP Adapter 准备的薄边界测试。

### 2.3 不应重复实现的现有职责

- Command 层不重复 `detail::Dispatcher` 的 Action 必填参数、未知参数和异常归一化逻辑；
- Command 层不重复 `IntrospectionService` 的过滤、排序、深复制和 Snapshot 采集；
- Command 层不解析 Resource 或 Task 内部存储，不读取私有 Registry 状态；
- Command 层不创建第二套 Descriptor、Query、Error 或动态值模型；
- Adapter 不直接分支调用 Runtime / Registry，所有外部能力必须经过 CommandDispatcher；
- Python 不绑定完整 Core 对象图，JSON 不成为 Command 模块依赖。

## 3. 模块边界与依赖方向

建议新增：

```text
include/axiom/command/
    command_dispatcher.hpp
    command_methods.hpp

src/command/
    command_dispatcher.cpp
    command_validation.hpp
    command_validation.cpp
    descriptor_conversion.hpp
    descriptor_conversion.cpp
```

按实现规模决定是否拆出 `action_command.cpp`、`resource_command.cpp`、`task_command.cpp`。
首版优先保持文件少而内聚：当单个 `.cpp` 超过仓库复杂度/长度门禁，或各 domain 已形成独立
逻辑时再拆分，不预先创建空的 handler 抽象或通用路由框架。

依赖方向为：

```text
Foundation <--- Action / Resource / Task
       ^              ^       ^
       |              |       |
       +------ Introspection -+
                      ^
                      |
                   Command
                      ^
          +-----------+-----------+
          |           |           |
        Python       JSON        MCP
```

`command` 可以依赖 `action`、`resource`、`task`、`introspection` 和 `foundation`；这些下层模块
不得依赖 `command`。在 `quality/architecture_rules.json` 增加对应规则，至少禁止
`foundation/action/resource/task/introspection/logging/events/async` 包含
`axiom/command/`。

## 4. 公共接口

### 4.1 推荐接口

```cpp
namespace axiom::command {

class AXIOM_API CommandDispatcher final {
public:
    CommandDispatcher(const Runtime& runtime,
                      const resource::ResourceRegistry& resources,
                      task::TaskRegistry& tasks);
    ~CommandDispatcher() noexcept;

    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;
    CommandDispatcher(CommandDispatcher&&) = delete;
    CommandDispatcher& operator=(CommandDispatcher&&) = delete;

    [[nodiscard]] Result<Value>
    dispatch(std::string_view method,
             const Value::Object& params,
             const InvocationContext& context = {}) const;
};

} // namespace axiom::command
```

构造函数直接接收三个状态拥有者，并在内部构造一个轻量、非拥有的
`IntrospectionService`。不建议要求调用方同时传 `Runtime`、`IntrospectionService` 和
`TaskRegistry`：那会允许 Service 绑定一组 Registry，而 Dispatcher 又使用另一组 Runtime/Task，
形成难以发现的配置错误。

内部状态建议通过私有 `Impl` 隐藏，既保持公共头较薄，也避免把 IntrospectionService 的具体
布局固化进 CommandDispatcher 的 ABI。`Impl` 保存非拥有指针和内部 IntrospectionService；三个
来源必须比 Dispatcher 活得更久。构造 PImpl 的分配失败沿用项目惯例抛出 `std::bad_alloc`，
因此构造函数不声明 `noexcept`。

### 4.2 method 常量

在 `command_methods.hpp` 中以 `inline constexpr std::string_view` 集中定义九个稳定方法名，供
C++ Adapter 和测试复用。Dispatcher 内部把字符串解析到私有枚举后使用穷尽 `switch` 路由：每个
已知 method 都有独立 `case`，直接进入对应处理函数。路由和 schema 查找都按 method 显式对应，
不得依赖枚举数值顺序，也不得把未匹配值默认落到某个现有命令。非法私有枚举是实现缺陷，schema
查找和路由都抛出 `std::logic_error`，不伪装成合法 Command，也不与 `system.snapshot` 的合法空
schema 混用。公开未知 method 字符串仍返回 `UnknownCommand`。

首版不引入动态 handler 注册、字符串到 `std::function` 的可变 map、Command 基类或插件式
路由。命令集合是受版本控制的外部契约，显式解析更易审计，也不会把用户回调生命周期带入
公共边界。

### 4.3 生命周期与并发

- Dispatcher 不拥有 Runtime、ResourceRegistry 或 TaskRegistry；三个对象必须在它之后析构；
- 来源析构不能与 `dispatch()` 并发；
- 不同线程可以并发调用同一个 Dispatcher；具体保证继承各来源现有契约；
- `task.cancel` 与查询/调用并发时沿用 TaskRegistry 的线程安全语义；
- `system.snapshot` 仍是按来源顺序采集的非全局原子快照，Command 层不得强化该承诺；
- Dispatcher 不持有跨调用可变路由状态，不在调用用户 Action 时持有 Command 层锁。

## 5. Command 参数契约

所有 Command 都采用严格对象 schema：

- 缺少必填字段：`ErrorCode::MissingArgument`；
- 出现未知字段：`ErrorCode::UnknownArgument`；
- 字段类型不符：`ErrorCode::TypeMismatch`；
- 字符串 ID 或枚举文本非法：保留相应解析器的 `InvalidArgument`；
- 合法但不存在：保留来源的 `NotFound`；
- 未知 method：新增 `ErrorCode::UnknownCommand`。

错误 `path` 使用现有对象路径语法，定位到 Command 参数自身，例如 `action`、`arguments`、
`tags[1]`、`state`。验证顺序固定为：未知 method → 缺少字段（按 schema 声明顺序）→ 未知字段
（按 `Value::Object` 字典序）→ 类型/值验证。测试应固定该顺序，避免不同 Adapter 得到不稳定
错误。

| method | params | 内部调用 |
| --- | --- | --- |
| `action.list` | `module?: string`, `tags?: string[]` | `IntrospectionService::actions(ActionQuery)` |
| `action.describe` | `action: string` | `ActionId::parse` → `describeAction` |
| `action.invoke` | `action: string`, `arguments: object` | `ActionId::parse` → `Runtime::invoke` |
| `resource.list` | `type?: string` | `resources(ResourceQuery)` |
| `resource.describe` | `resource: string` | `ResourceId::parse` → `describeResource` |
| `task.list` | `state?: string`, `origin_action?: string`, `origin_request?: string` | `tasks(TaskQuery)` |
| `task.describe` | `task: string` | `TaskId::parse` → `describeTask` |
| `task.cancel` | `task: string` | `TaskId::parse` → `TaskRegistry::cancel` |
| `system.snapshot` | 空对象 | `IntrospectionService::snapshot` |

`task.list.state` 接受规范小写文本：`pending`、`running`、`completed`、`failed`、`cancelled`。
过滤行为严格复用现有 Query：多个条件为 AND，合法但无匹配返回空数组。`origin_action` 是 Task
中保存的弱关联文本；首版不额外检查该 Action 当前是否存在。

`action.invoke.arguments` 必须存在且为对象，即使 Action 没有参数也传空对象。这保持 wire
schema 明确，避免把“字段缺失”和“空参数”合并。Action 业务参数的 required/default/type 校验
仍只由 `Runtime::invoke()` 权威执行；Command 只验证 `arguments` 是对象。

## 6. 输出 Value Schema

### 6.1 通用规则

- 对象键由 `Value::Object` 保持字典序；数组保留来源规定的确定性顺序；
- ID 一律输出规范字符串；枚举一律输出稳定小写字符串；
- 可选字段缺失时省略该键，不用 `null` 代替；
- 真实业务 `Value` 为 null 时仍输出 null，因此“省略”和“显式 null”保持可区分；
- 转换函数放在 Command 私有实现中，不给 Core Descriptor 增加 `toValue()`，防止底层模型依赖
  外部表示策略；
- 转换只读取公开字段，不使用 friend、RTTI、Registry 内部对象或可变引用。

### 6.2 ModuleDescriptor

```text
{
  "namespace": string,
  "description": string,
  "version"?: string,
  "tags": string[],
  "metadata": object<string,string>
}
```

### 6.3 ActionDescriptor

```text
{
  "id": string,
  "description": string,
  "parameters": ParameterDescriptor[],
  "return_type": TypeDescriptor,
  "version"?: string,
  "tags": string[],
  "metadata": object<string,string>
}
```

Parameter：

```text
{
  "name": string,
  "description": string,
  "required": boolean,
  "type": TypeDescriptor,
  "default"?: Value
}
```

`default` 仅在 `default_value` 有值时出现；如果默认值本身是 null，则键存在且值为 null。

### 6.4 TypeDescriptor

基础字段：

```text
{
  "kind": "null" | "boolean" | "integer" | "number" | "string" | "array" | "object",
  "nullable": boolean,
  "description": string
}
```

根据 kind 追加且只追加一种结构字段：

- array：`"element_type": TypeDescriptor`；
- 固定字段 object：`"fields": object<string,TypeDescriptor>`；
- 同质值 object：`"value_type": TypeDescriptor`；
- 空的固定 object：输出空 `fields`，与同质 object 保持可区分。

转换函数依赖注册时已经通过的 TypeDescriptor 不变量；遇到内部不可能状态是实现缺陷，不应
静默生成残缺 schema。

### 6.5 ResourceDescriptor

```text
{
  "id": string,
  "type": string
}
```

不要虚构 Resource 名称、描述、metadata、内容或可解析 Handle。

### 6.6 TaskDescriptor

```text
{
  "id": string,
  "name": string,
  "state": string,
  "progress": {
    "value": number,
    "message": string
  },
  "error"?: Error,
  "origin"?: {
    "request_id": string,
    "trace_id": string,
    "caller": string,
    "action_id": string,
    "metadata": object<string,string>
  }
}
```

Task result 不在 Descriptor 中，不能由 Command 层从类型化 TaskHandle 猜测或擦除。

### 6.7 Error

Error 的 Value 表示首先用于 TaskDescriptor 中嵌入的失败信息，其规范转换与其他 Descriptor
转换放在同一 Command 边界策略中：

```text
{
  "code": string,
  "message": string,
  "path"?: string,
  "details"?: Value
}
```

所有 ErrorCode 建立穷尽的稳定小写映射。新增枚举必须使编译期 switch 或测试失败，不能默认
落入 `internal_error`。`dispatch()` 自身仍返回 `Result<Value>`，不会把失败包装成成功 Value；
JSON/Python/MCP Adapter 直接读取 Result/Error，再按目标环境转换，不要求 Command 公共 API
额外暴露一个通用 Error codec。

### 6.8 各命令成功结果

- `*.list`：对应 Descriptor 数组；
- `*.describe`：单个 Descriptor 对象；
- `action.invoke`：Runtime 返回的 Value 原样返回，不加 `data` 包装；
- `task.cancel`：返回 null；重复取消或取消终态 Task 沿用 TaskRegistry 现有成功语义；
- `system.snapshot`：

```text
{
  "modules": ModuleDescriptor[],
  "actions": ActionDescriptor[],
  "resources": ResourceDescriptor[],
  "tasks": TaskDescriptor[]
}
```

## 7. 错误与异常策略

### 7.1 新增 UnknownCommand

在 `ErrorCode` 末尾追加 `UnknownCommand`，避免改变已有枚举值的相对顺序。它只表示 method 不在
当前 Command 契约中；合法 method 下对象不存在仍为 `NotFound`。

未知命令错误建议为：

```text
code    = UnknownCommand
message = "Unknown command: <method>"
path    = none
details = none
```

不在错误中返回“最接近的方法名”，避免模糊匹配、信息泄漏和不稳定行为。

### 7.2 保留来源错误

- ID parser 的 `InvalidArgument` 原样保留，但 path 补到对应 Command 字段；
- Introspection 和 Registry 的 `NotFound` 原样保留；
- Runtime 的 MissingArgument、UnknownArgument、TypeMismatch、业务 Error、InvocationFailed、
  InternalError 原样保留；
- Command 不把业务错误重新分类成 Command 错误。

Runtime 返回的参数 path 仍相对于 Action 的 `arguments` 对象，例如 `shape.size`，不额外改写为
`arguments.shape.size`；这既保留现有 Runtime 契约，也避免 Command 和 Native 调用同一错误时
出现两套路径。只有 Command 自身的结构错误使用 `action`、`arguments` 等顶层 path。

为来源错误补 path 时复制 Error，不修改来源对象；若来源已经给出更具体 path，则保留其 path。

### 7.3 异常边界

现有项目允许查询复制和 Value 分配抛出 `std::bad_alloc`。CommandDispatcher 继续该契约，不把
资源耗尽伪装成 `InternalError`。Action 用户代码的异常继续由 Runtime 内部 ActionInvoker
归一化。Command 参数通过显式类型检查后再调用 `Value::as*()`，因此不依赖捕获
`ValueTypeError` 作为正常控制流。

## 8. 现有私有 Dispatcher 的调整

当前文件：

```text
src/action/detail/dispatcher.hpp
src/action/dispatcher.cpp
tests/dispatcher_test.cpp
```

建议机械重命名为：

```text
src/action/detail/action_invoker.hpp
src/action/action_invoker.cpp
tests/action_invoker_test.cpp
```

类型改为 `axiom::detail::ActionInvoker`，RuntimeState 的成员和相关注释同步更新。该步骤不改变
公共 API、行为或测试断言，只消除“Dispatcher”同时表示 Action 执行器和外部命令路由器的
长期认知负担。

不要借此重构 Registry、Action Adapter 或 Runtime 注册流程；新 CommandDispatcher 必须调用
`Runtime::invoke()`，不能绕过 Runtime 直接持有 ActionInvoker。

## 9. 实施阶段

### 阶段 0：契约冻结与基线

1. 记录当前 `git status`，保留用户已有修改；
2. 运行 `checkflow fast` 建立修改前基线；
3. 用表格测试先冻结本计划第 5、6 节的 method、参数和输出 schema；
4. 确认命名、可选字段省略规则和 TaskState 文本后再实现 Adapter。

完成标准：基线通过，公开契约无未决字段。

### 阶段 1：消除内部命名冲突

1. 将私有 Action `Dispatcher` 机械重命名为 `ActionInvoker`；
2. 更新 RuntimeState、CMake 私有源和内部测试；
3. 不改变任何行为和公共符号；
4. 运行 `checkflow fast`。

完成标准：现有 Action/Runtime 测试全部通过，diff 只包含命名和引用变化。

### 阶段 2：Command 基础与参数验证

1. 追加 `ErrorCode::UnknownCommand`；
2. 新增 method 常量、私有 method 枚举与显式 parser；
3. 实现小型私有 schema helper：必填字段、可选字段、未知字段、string/object/string-array
   读取；
4. 实现 TaskState 文本解析；
5. 新增 CommandDispatcher PImpl、生命周期契约和空路由骨架；
6. 补未知 method、空 method、缺参、多参、错类型、特殊对象键 path 测试；
7. 运行 `checkflow fast`。

完成标准：所有 Command 结构错误确定且无来源调用副作用。

### 阶段 3：规范 Value 转换

1. 实现 ErrorCode、TaskState、TypeDescriptor 的穷尽映射；
2. 实现 Module、Action、Parameter、Resource、Task、Origin、Progress、Error 转换；
3. 实现 RuntimeSnapshot 转换；
4. 使用深层 TypeDescriptor、null 默认值、空/有值 optional、完整 Error details 做精确树比较；
5. 验证列表顺序与 Object 键顺序确定；
6. 运行 `checkflow fast`。

完成标准：每个公开 Descriptor 字段都被编码且不存在并行模型或虚构字段。

### 阶段 4：九个命令端到端接线

1. 接入 Action list/describe/invoke；
2. 接入 Resource list/describe；
3. 接入 Task list/describe/cancel；
4. 接入 system.snapshot；
5. 验证所有来源 Error 原样保留、context 原样转发；
6. 验证查询过滤与直接调用 IntrospectionService 结果一致；
7. 运行 `checkflow fast`，随后运行 `checkflow hardening`。

完成标准：九个命令从公共 Dispatcher 到真实来源通过，无复制业务规则。

### 阶段 5：公共集成与交付

1. 将新公共头加入 `<axiom/axiom.hpp>`；
2. 更新 `src/CMakeLists.txt`、测试目标和安装消费者；
3. 更新 architecture rules、README 外部边界说明和 roadmap 的近期顺序；
4. 验证静态/共享库导出和安装后消费者；
5. 运行最终 `checkflow full`。

完成标准：源码树、安装包、静态库和共享库消费者均可只通过公共头构造 Dispatcher 并调用
九个命令；所有门禁通过。

### 阶段 6：Adapter（后续独立交付）

按风险从低到高建议：

1. JSON codec/CLI：验证 wire schema、Error 序列化与 UTF-8/数字边界；
2. Python：只暴露 `dispatch(method, params, context=None)` 和 AxiomError；
3. MCP/HTTP/RPC：只做协议 method、参数、context、结果和错误映射。

Adapter 必须通过公共 Dispatcher，不能为了便利重新暴露 Runtime、IntrospectionService、
Registry、TaskHandle 或 ResourceRef。首个 Adapter 落地后再评估是否需要 Command 契约版本字段；
MVP 不预先增加未被消费者使用的版本协商框架。

## 10. 测试计划

### 10.1 method 与结构验证

- 九个已知 method 和未知/空/大小写不同 method；
- 每个必填字段缺失；
- 每个字段的 null、bool、integer、number、string、array、object 错型；
- 未知字段和多个未知字段的确定性选择；
- `tags` 中非 string 元素及精确 `tags[i]` path；
- 非规范 ActionId、ResourceId、TaskId；
- 非法 TaskState 和非法 Resource type；
- 验证失败时不调用 Action、不取消 Task。

### 10.2 Action

- 空列表、module/tags 单独和组合过滤、合法无匹配；
- describe 完整编码，包括递归数组、固定 object、同质 object、nullable、null default；
- invoke 的空/复杂 arguments、返回任意 Value；
- InvocationContext 的 request/trace/caller/metadata 原样传递；
- Runtime 的 MissingArgument、UnknownArgument、TypeMismatch、业务错误、std/未知异常归一化不变；
- Command 不重复 default 或 Value 类型转换逻辑。

### 10.3 Resource

- 空列表、type 过滤、合法无匹配；
- describe 已存在、未知、已移除和外部 Registry ID；
- 只暴露 id/type，不延长 Resource 对象寿命；
- Dispatcher 查询期间 Registry 并发变更继承现有一致性。

### 10.4 Task

Command 层证明 wire 字段映射和 Dispatcher 转发；TaskRegistry / Introspection 已覆盖的业务
语义不再在 Command 测试中整表复制。

- 五种状态字符串被 Command 接受；progress、origin、error 的 Value 编码覆盖缺失、空字段、
  完整 metadata 以及含 path/details 的失败；
- `state` / `origin_action` / `origin_request` 作为 Command 字段的单项与组合过滤，用于证明
  字段名映射到 `TaskQuery`，合法无匹配返回空数组；
- cancel：非法 ID 为 `InvalidArgument`；规范但未知 ID 保留来源 `NotFound`；至少一次经
  Dispatcher 取消仍为 active 的 Task，成功值严格为 null；
- 重复取消、终态取消和不隐式 `remove` 由 Task 测试覆盖；
- 不暴露类型化 result。

### 10.5 Snapshot、并发与生命周期

- 空和混合 Snapshot 的四个固定键；
- 转换结果在后续来源变化后保持独立；
- 明确验证 Snapshot 非全局原子但每个 Descriptor 有效；
- 多线程并发 `list` / `describe` / `invoke` / `cancel`，不使用 sleep 控制交错；
- Dispatcher 析构不影响任何来源；
- 来源析构前置条件写入 Doxygen，不编造运行时检测。

### 10.6 安装与 ABI

- 独立安装消费者仅包含 `<axiom/command/command_dispatcher.hpp>`；
- umbrella header 消费者；
- 静态和共享构建；
- Windows 导出、Unix 可见符号检查；
- 公共头不泄漏私有 helper、JSON、pybind11 或第三方类型。

## 11. 文件级变更清单

| 文件 | 计划变更 |
| --- | --- |
| `include/axiom/foundation/error.hpp` | 末尾追加 `UnknownCommand` |
| `include/axiom/command/command_methods.hpp` | 稳定 method 常量 |
| `include/axiom/command/command_dispatcher.hpp` | 唯一公共 dispatch 入口和完整 Doxygen |
| `src/command/*` | 路由、结构验证、Descriptor/状态/Error 转换 |
| `src/action/detail/dispatcher.hpp` | 重命名为 `action_invoker.hpp` |
| `src/action/dispatcher.cpp` | 重命名为 `action_invoker.cpp`，行为不变 |
| `tests/dispatcher_test.cpp` | 重命名为 `action_invoker_test.cpp` |
| `tests/command_dispatcher_test.cpp` | Command 单元与端到端契约测试 |
| `include/axiom/axiom.hpp` | 导出 Command 公共头 |
| `src/CMakeLists.txt` / `tests/CMakeLists.txt` | 新源与测试接入 |
| `tests/install/main.cpp` | 安装消费者覆盖公共 Dispatcher |
| `quality/architecture_rules.json` | 禁止下层反向依赖 Command |
| `README.md` / `docs/architecture/architecture.md` | 外部边界、生命周期和依赖图同步 |
| `docs/roadmap/roadmap.md` | Command 层先于具体 Adapter |

如实际实现发现 `descriptor_conversion.cpp` 超过门禁，再按 Module/Action、Resource、Task 拆分，
但共享 Type/Error helper 保持单一事实来源。

## 12. 兼容性与迁移

- 现有 Native C++ API 全部保留；内部模块继续直接调用 Runtime/Registry/Introspection；
- 现有 Action invoke 行为、排序、错误、Task 取消和 Resource 生命周期不变；
- 私有 Dispatcher 重命名不是公共 API 兼容性变化；
- 新增 ErrorCode 追加在枚举末尾，避免扰动已有值；
- 首版 Command 字段名发布后视为外部协议契约：允许新增可选输出字段，不可静默重命名、改变
  类型或改变枚举文本；
- 对未来 Adapter，外部输入仍建议默认拒绝未知参数。若协议需要 envelope 扩展，应由 Adapter
  消费 envelope 字段，不污染业务 `params`。

当前仓库尚无 Python、JSON 或 MCP 实现，因此无需做旧 Adapter 迁移。README 中“当前不提供
protocol adapters”的描述应在 Command MVP 后改为“提供协议无关 Command 边界，但具体协议
Adapter 尚未提供”。

## 13. 风险与控制

| 风险 | 控制措施 |
| --- | --- |
| 两个 Dispatcher 概念长期混淆 | 私有 Action 类型先机械改名；公共类型始终带 `Command` |
| Command 重复 Runtime 参数校验 | 只验证 `arguments` 为 object，业务字段交给 Runtime |
| Descriptor wire schema 漂移 | 精确 Value 树测试；集中转换；可选字段规则写入文档 |
| PImpl/来源配置错配 | Dispatcher 直接接收三个来源并内部创建 IntrospectionService |
| Task result 被不安全擦除 | MVP 明确不实现 `task.result` |
| Snapshot 被误认为全局原子 | 公共 Doxygen、测试和输出文档都继承现有非原子契约 |
| catch-all 掩盖内存失败 | `std::bad_alloc` 保持抛出；正常验证不用异常控制流 |
| Adapter 绕过统一入口 | 架构文档和 Adapter 集成测试要求只依赖 Command |
| method 动态注册造成安全/生命周期复杂度 | MVP 使用封闭、显式、集中维护的 method 集合 |
| 按枚举序 fallthrough 把新命令误路由到 invoke/snapshot | 穷尽 `switch`；schema 同样按 method 显式对应 |
| 计划过度拆分 | 先用少量内聚文件，达到真实复杂度阈值再按 domain 拆分 |

## 14. 验收标准

开发完成时必须同时满足：

1. 外部动态调用只需一个公共 `CommandDispatcher::dispatch()`；
2. 九个 MVP method 的输入、输出、排序、optional 和错误 schema 与本文一致；
3. Runtime、IntrospectionService、ResourceRegistry、TaskRegistry 仍是业务语义唯一事实来源；
4. Action 业务参数不在 Command 重复校验，InvocationContext 与 params 完全分离；
5. 未知 method、非法 Command 参数、来源 NotFound 和业务 Error 可稳定区分；
6. 所有 Descriptor 转成自有 Value，不泄漏引用、Handle、TaskHandle、第三方或私有类型；
7. `system.snapshot` 不承诺跨 Registry 原子性；
8. Native C++ API 和现有行为无回归；
9. 公共头 Doxygen、README、架构规则、安装消费者同步；
10. `checkflow fast`、`hardening` 和最终 `full` 按阶段实际运行并通过；不得通过跳过、排除或
    降低阈值使门禁通过。

一句话定义：

> `axiom::command::CommandDispatcher` 是建立在现有 Runtime、Introspection、Resource 和 Task
> 事实来源之上的封闭、协议无关动态边界；它只负责 Command 结构校验、路由和值转换，所有
> 业务语义继续由 Core 权威拥有，所有 Python、JSON、MCP 等表示转换继续留在 Adapter。
