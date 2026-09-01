# Runtime Discoverability & Correlation 开发指导

> 状态：待实施  
> 基线：2026-09-01 当前工作区代码  
> 范围：Canonical Descriptor、Invocation Correlation、Introspection Query  
> 约束：需求描述与实现不一致时，以当前公开 API、模块边界和并发契约为准。

## 1. 阶段目标

本阶段把 Axiom 从“拥有运行时能力”推进为“能够被机器稳定理解、筛选和追踪的运行时”。

完成后，调用者应能仅依赖 Axiom 公开值类型回答：

```text
系统有哪些 Action？
某个 Action 接收什么参数并返回什么？
有哪些指定逻辑类型的 Resource？
有哪些处于指定状态的 Task？
某个 Task 由哪个 Action、request 创建？
某个 request 产生了哪些关联日志？
```

本阶段建立的事实来源将供 Python Binding、Agent Tool Adapter、MCP、RPC 和 CLI 转换使用；
这些 Adapter 不反向定义 Axiom 的核心模型。

## 2. 当前代码事实与需求修正

### 2.1 已经具备的能力

- `ModuleDescriptor` 已拥有规范 `namespace_name` 和有序字符串 `metadata`。
- `ActionDescriptor` 已拥有 `ActionId`、描述、参数、返回 `TypeDescriptor`、可选版本和 tags。
- `ParameterDescriptor` 已拥有名称、描述、必填标记、类型和可选默认值。
- `ResourceDescriptor` 已拥有稳定 `ResourceId` 和逻辑 `type`。
- `TaskDescriptor` 已拥有 `TaskId`、名称、状态、进度和可选错误。
- `InvocationContext` 已定义 `request_id`、`trace_id`、`caller` 和有序字符串
  `metadata`。
- `Runtime::invoke()` 已把 InvocationContext 传到内部 `IAction`，并在同一线程的日志作用域中
  自动附加 `request_id`、`trace_id`、`caller`、`module`、`action` 及非保留 metadata。
- `TaskRegistry` 已为 Task 执行线程日志附加 `task_id` 和 `task_name`。
- `IntrospectionService` 已提供 modules/actions/resources/tasks、单项 describe 和顺序采集的
  `RuntimeSnapshot`；所有结果均为独立拥有的值。
- Action Runtime、ResourceRegistry 和 TaskRegistry 已支持各自声明范围内的并发操作；
  `RuntimeSnapshot` 是按 module/action、resource、task 顺序采样的非原子观察。
- Introspection 已有 `actions(module)` 与 `resources(type)` 两个单字段筛选入口。

### 2.2 尚未满足的能力

- 公开的 typed Action 适配器当前忽略 `InvocationContext`。普通 `ModuleBuilder::add()` 注册的
  callable 无法读取上下文并显式传给新建 Task。
- Task 没有 origin 数据，提交 API 也没有承载弱来源的输入。
- Task 的执行线程只恢复 `task_id`、`task_name`，不会自动拥有创建它的 request、trace、
  caller、module 或 action 字段。
- `LogQuery` 只能按 level、category 和 limit 查询，不能直接按 request/trace/action/task
  精确关联。
- Introspection 没有 Action tags、Task state/origin 的强类型组合 Query。
- Descriptor 字段目前并不完全统一：Module 的显示语义藏在任意 metadata 中；Action 没有
  metadata；Resource 注册模型没有名称、描述或任意 metadata 的真实来源。
- `ActionDescriptor::tags` 目前没有校验、规范化或明确的匹配语义；Module metadata 的键和值
  也只被保存，未被 `validate(ModuleDescriptor)` 校验。

### 2.3 不按原需求字面实施的部分

“每一种 Descriptor 都包含 id/name/type/version/state/metadata”不是合适的公共契约。字段只应
出现在拥有真实语义和真实数据来源的对象上：Action 没有运行状态，Task 没有参数 schema，
Resource 当前也没有显示名称。不得用空字符串、RTTI、ID 文本拆分或 metadata 约定伪造字段。

`ModuleDescriptor::namespace_name` 是现有 Action namespace 的规范身份，不新增内容重复的
`id`。`ActionId::action()` 和 `ActionId::module()` 已能提供本地名称与所属 module，不在
`ActionDescriptor` 再保存可能失配的 `name` 或 `module` 字段。

## 3. 架构与所有权边界

保持现有依赖方向：

```text
foundation <- action -> logging
foundation <- resource
foundation <- async
task -> foundation / async / events / logging
introspection -> action / resource / task
```

必须保持以下边界：

- Descriptor 由对应状态拥有模块产生；Introspection 只复制、组合和筛选。
- Action 仍为同步调用，不拥有 Task，也不等待 Task。
- Task 不依赖 Action Runtime、Action Registry 或 `ActionId` 类型。
- Task origin 是不可解析所有权的弱关联；关联对象不存在或已被移除时，origin 仍可保留。
- InvocationContext 只承载调用身份和诊断 metadata，不提供服务、Registry、Logger 或取消能力。
- 不增加全局 `ServiceLocator`，也不通过进程级 thread-local 暗中定位 TaskRegistry。
- Logging 是旁路诊断；日志分配或 sink 失败不能改变 Action/Task 结果。
- Introspection 保持非拥有、无缓存、只读。Query 不改变 snapshot 的非原子语义。

## 4. Canonical Descriptor Model

### 4.1 稳定字段矩阵

| 模型 | 本阶段稳定公开字段 | 说明 |
| --- | --- | --- |
| `ModuleDescriptor` | `namespace_name`, `description`, `version`, `tags`, `metadata` | `namespace_name` 即规范身份；后三项为新增的显式发现字段。 |
| `ActionDescriptor` | `id`, `description`, `parameters`, `return_type`, `version`, `tags`, `metadata` | `metadata` 为新增；module/name 由 `ActionId` 无损获得。 |
| `ParameterDescriptor` | `name`, `description`, `required`, `type`, `default_value` | 保持现状。 |
| `TypeDescriptor` | `kind`, `nullable`, `description`, `element_type`, `fields`, `value_type` | 保持 Axiom `Value` 语义，不映射 JSON Schema 专属字段。 |
| `ResourceDescriptor` | `id`, `type` | 保持现状；没有真实注册数据前不增加空壳字段。 |
| `TaskDescriptor` | `id`, `name`, `state`, `progress`, `error`, `origin` | `origin` 为新增可选弱关联。 |

建议值模型：

```cpp
namespace axiom {

struct ModuleDescriptor {
    std::string namespace_name;
    std::string description;
    std::optional<std::string> version;
    std::vector<std::string> tags;
    std::map<std::string, std::string, std::less<>> metadata;
};

struct ActionDescriptor {
    ActionId id;
    std::string description;
    std::vector<ParameterDescriptor> parameters;
    TypeDescriptor return_type{};
    std::optional<std::string> version;
    std::vector<std::string> tags;
    std::map<std::string, std::string, std::less<>> metadata;
};

} // namespace axiom

namespace axiom::task {

struct TaskOrigin final {
    std::string request_id;
    std::string trace_id;
    std::string caller;
    std::string action_id;
    std::map<std::string, std::string, std::less<>> metadata;
};

struct TaskDescriptor final {
    TaskId id;
    std::string name;
    TaskState state{TaskState::Pending};
    Progress progress;
    std::optional<Error> error;
    std::optional<TaskOrigin> origin;
};

} // namespace axiom::task
```

`TaskOrigin::action_id` 使用规范文本而不是 `ActionId`，以避免 `task -> action` 依赖。它是由
提交方声明的关联值，TaskRegistry 不查询或证明 Action 存在。空字段表示该维度未知；完全没有
来源时使用 `std::nullopt`，避免“存在但所有字段为空”的状态。

### 4.2 metadata 和专用字段

- 可用于统一发现或筛选的字段必须成为专用强类型成员，不能只放在 metadata 中。
- metadata 是适配器或宿主的附加稳定字符串信息，不承载对象所有权或行为开关。
- `request_id`、`trace_id`、`caller`、`module`、`action`、`task_id`、`task_name`、`status`、
  `duration_ms` 是日志保留字段，调用者 metadata 不得覆盖。
- `origin_request_id`、`origin_trace_id`、`origin_action_id` 不作为 TaskDescriptor 的平铺重复
  成员；序列化 Adapter 可从 `origin` 映射成协议所需形式。
- metadata 继续使用有序 `std::map`，保证确定性复制和输出；本阶段不把值扩大为任意 `Value`。

### 4.3 校验和确定性

- Module namespace、ActionId、Parameter name、ResourceId/TaskId 沿用现有规范。
- 非空 version 规则沿用 Action；Module version 使用同一规则。
- tags 首版定义为区分大小写的精确字符串，注册时去重失败而不是静默去重；保持输入顺序用于
  展示，Query 不能依赖顺序。
- 空 tag 和重复 tag 返回 `ErrorCode::InvalidDescriptor`。是否进一步限制 tag 字符集，应由真实
  Adapter 需求驱动，本阶段不擅自规定协议格式。
- metadata 键必须非空；重复键已由 `std::map` 结构消除。值允许为空，表示宿主明确提供空值。
- Descriptor 注册后视为不可变事实；Introspection 返回深复制值，嵌套 `TypeDescriptor` 继续
  使用现有深复制 helper。
- modules/actions/resources/tasks 的排序继续使用现有规范身份顺序，新增字段不参与排序。

### 4.4 Resource 的明确延期项

当前 `ResourceRegistry::add(std::unique_ptr<T>)` 只获得对象和 `ResourceTraits<T>::type_name`。
因此 Resource 本阶段只稳定 `id` 与 `type`。若以后确实需要名称、描述、tags、version 或
metadata，应先在 Resource 模块设计一个拥有这些输入的最小注册值，再由 Registry 原样保存和
描述；不能在 Introspection 层补造。

## 5. Invocation Correlation

### 5.1 关联链路

目标链路为：

```text
request_id / trace_id / caller
              |
              v
       synchronous Action
        |              |
        v              v
   Action logs      TaskOrigin
                        |
                        v
                   Task logs
```

关联不表示生命周期所有权。一个 Action 可以不创建 Task，也可以创建多个 Task；Task 可在
Action 返回后继续运行和保留。

### 5.2 让 typed Action 可读取 InvocationContext

内部 `IAction` 已接收 InvocationContext，缺口只在 `TypedActionAdapter`。不要改变普通
`ModuleBuilder::add()` 的参数推导和 descriptor 结果。建议增加一个名称明确的上下文注册入口：

```cpp
builder.addContextual(
    "rebuild",
    "Starts a rebuild task",
    [&tasks, &executor](const InvocationContext& invocation, std::string target) {
        task::TaskOrigin origin{
            .request_id = invocation.request_id,
            .trace_id = invocation.trace_id,
            .caller = invocation.caller,
            .action_id = "index.rebuild",
            .metadata = invocation.metadata,
        };
        return tasks.submit(executor,
                            task::TaskSubmission{.name = "rebuild", .origin = origin},
                            [target = std::move(target)](task::TaskContext&) {
                                return rebuild(target);
                            });
    },
    param("target", "Target index"));
```

契约：

- InvocationContext 是注入参数，不出现在 `ActionDescriptor::parameters` 中。
- context 只在同步 callable 执行期间以 `const&` 有效；异步使用必须复制需要的值。
- `addContextual()` 和普通 `add()` 共用参数校验、Value 转换、返回转换和异常归一化逻辑，不能
  形成第二套 Action 实现。
- 不通过“识别 callable 第一个特殊类型”的隐式魔法改变现有 `add()`；显式入口使公开签名和
  descriptor 参数数量保持可预测。
- `action_id` 由注册 Action 的规范 ID 填入。实现应让 contextual adapter 持有该 ID，避免业务
  callable 手写字符串。最终 API 可把只读 `ActionInvocation` 视图注入 callable，其中包含
  `const InvocationContext&` 和 `const ActionId&`；不要把 Runtime 或 Logger 放入该视图。

推荐最终注入值：

```cpp
class ActionInvocation final {
public:
    [[nodiscard]] const ActionId& actionId() const noexcept;
    [[nodiscard]] const InvocationContext& context() const noexcept;
};
```

该类型由 Action 模块拥有，仅在同步调用栈内有效。业务代码再显式构造 TaskOrigin，Task 模块
无需依赖它。

### 5.3 Task 提交与 origin

保留现有 `submit(executor, std::string name, F&&)`，使已有调用方源码兼容，并让它委托到新的
提交值入口：

```cpp
struct TaskSubmission final {
    std::string name;
    std::optional<TaskOrigin> origin;
};

template <typename F>
auto submit(async::Executor& executor, TaskSubmission submission, F&& function);
```

TaskRegistry 在接受提交时把 origin 复制进 TaskControl 的初始 descriptor；Pending、Running、
progress 和终态通知中的 origin 必须完全相同。`describe()`、`list()`、TaskHandle 间接读取和
Introspection 均返回独立副本。

TaskOrigin 只记录提交时信息，不随外部 InvocationContext 或 Action 生命周期变化。Task 重试、
派生 Task 或跨线程再提交不会自动继承；调用者必须显式传递。这一规则避免隐藏的线程传播和
Task 对 Action Runtime 的依赖。

### 5.4 日志上下文传播

当前 Action 日志传播已经满足同线程业务日志关联，保留其字段覆盖规则。Task 新增 origin 后：

- TaskRegistry 创建 task logger 时绑定 `task_id`、`task_name` 以及非空 origin 字段。
- Task 执行线程的 `ScopedLogContext` 安装同一组字段，使使用同一个 LoggingService 创建的业务
  Logger 自动获得关联信息。
- 建议使用日志字段名 `request_id`、`trace_id`、`caller`、`action`；若 action 非空，同时从
  `module.action` 的规范文本安全提取 `module`。无法确认规范格式时只记录 `action`，不猜 module。
- Task origin metadata 的保留字段冲突处理与 Runtime 完全一致：先过滤调用者 metadata，再写入
  Axiom 权威字段。
- Task 自身日志应覆盖创建线程残留的 `task_id`/`task_name`，但不能让 origin metadata 覆盖
  `status` 等 Task Runtime 维护字段。
- 上下文不传播到 Task callable 自行创建的线程；与当前 LoggingService 的线程作用域契约一致。

### 5.5 关联日志查询

为了真正回答“某次 request 产生了哪些日志”，在现有 `LogQuery` 上增加有限的稳定字段筛选：

```cpp
struct LogQuery {
    LogLevel minimum_level{LogLevel::Trace};
    std::vector<std::string> category_prefixes;
    std::optional<std::string> request_id;
    std::optional<std::string> trace_id;
    std::optional<std::string> action_id;
    std::optional<std::string> task_id;
    std::size_t limit{0};
};
```

所有非空条件采用精确字符串匹配并同时满足；缺少对应字段的 LogRecord 不匹配。`limit` 仍在全部
条件过滤后保留最新 N 条，返回顺序仍为采集顺序。首版不加入任意 field predicate、metadata
表达式或 trace span 模型。

## 6. Introspection 强类型 Query

### 6.1 公共 Query 值

建议放在 `include/axiom/introspection/introspection_query.hpp`：

```cpp
namespace axiom::introspection {

struct ActionQuery final {
    std::optional<std::string> module;
    std::vector<std::string> tags;
};

struct ResourceQuery final {
    std::optional<std::string> type;
};

struct TaskQuery final {
    std::optional<task::TaskState> state;
    std::optional<std::string> origin_action_id;
    std::optional<std::string> origin_request_id;
};

} // namespace axiom::introspection
```

接口增量：

```cpp
[[nodiscard]] std::vector<ActionDescriptor> actions(const ActionQuery& query) const;
[[nodiscard]] Result<std::vector<resource::ResourceDescriptor>>
resources(const ResourceQuery& query) const;
[[nodiscard]] std::vector<task::TaskDescriptor> tasks(const TaskQuery& query) const;
```

保留现有无参入口和 `actions(std::string_view)`、`resources(std::string_view)`，由旧重载委托给
Query 实现。不要在同一版本删除或改变旧重载的错误行为。

### 6.2 匹配语义

- `ActionQuery::module`：与 `ActionId::module()` 区分大小写、完整精确匹配；无匹配返回空集合。
  为兼容当前 `actions(string_view)`，首版不新增非法 module 错误。
- `ActionQuery::tags`：all-of 语义；Action 必须包含 Query 中每个 tag。空 tags 不筛选；Query
  中重复 tag 不改变结果。
- `ResourceQuery::type`：复用当前 Resource 规范名验证；非法 type 返回
  `ErrorCode::InvalidArgument`，合法但无匹配返回成功的空集合。
- `TaskQuery::state`：与 descriptor state 枚举精确相等。
- origin 条件：Task 必须有 origin，且相应字段精确相等；未知关联返回空集合，不查询 Action
  Runtime 或日志系统。
- 一个 Query 中的所有非空条件采用 AND 语义。
- 过滤后保持来源列表原有顺序；不根据匹配数量、tag 或状态重新排序。

### 6.3 实现位置

Query 过滤属于 IntrospectionService：

- Action 从一次 `Runtime::discoverActions()` 的稳定有序快照复制并过滤。
- Resource 从一次 `ResourceRegistry::list()` 结果过滤，不读取 Registry 内部 Entry。
- Task 从一次 `TaskRegistry::list()` 结果过滤，不逐项再次 `describe()`。
- 不为首版增加索引、缓存或 generation。实际规模证明线性过滤不足后，再在数据拥有模块增加
  索引；不得让 Introspection 成为第二份状态所有者。

`RuntimeSnapshot` 保持“全部可发现对象”的含义，不接收 Query。调用者需要局部视图时使用三类
Query；不要制造字段间时间一致性的假象。

## 7. 推荐实施顺序

### 增量 1：Descriptor 契约稳定化

涉及：

- `include/axiom/action/module.hpp`
- `include/axiom/action/descriptor.hpp`
- `src/action/descriptor.cpp`
- `src/action/module_builder.cpp`
- `include/axiom/task/task_types.hpp`
- Introspection 的 deep-copy helper 和 descriptor/snapshot 测试

工作：

1. 增加明确适用的 Module/Action 字段、TaskOrigin 和校验。
2. 更新所有聚合初始化点，保证新增可选字段默认不改变现有行为。
3. 证明 Introspection 深复制新增 metadata/origin，来源变化不会修改已返回值。
4. 更新公共 Doxygen 和架构文档中的事实描述。

完成后运行 `checkflow fast`。

### 增量 2：Action 上下文可用性

涉及：

- `include/axiom/action/invocation_context.hpp`
- 新增或相邻定义的 `ActionInvocation`
- `include/axiom/action/module_builder.hpp`
- `include/axiom/action/detail/typed_action_adapter.hpp`
- `src/action/module_builder.cpp`
- Runtime/ModuleBuilder 测试

工作：

1. 增加显式 contextual 注册路径。
2. 复用现有转换和异常边界，不改变同步执行。
3. 验证注入参数不进入 descriptor，且每次并发调用得到对应的只读上下文。
4. 保证普通 `add()` 的编译期约束和行为不变。

完成后运行 `checkflow fast`。

### 增量 3：Task origin 与跨线程日志

涉及：

- `include/axiom/task/task_registry.hpp`
- `include/axiom/task/detail/task_control.hpp`
- `src/task/task_registry.cpp`
- `src/task/task_control.cpp`
- Task、Task concurrency、logging 测试

工作：

1. 增加 `TaskSubmission` 重载并保留旧入口。
2. 从 Pending 起固定 origin，贯穿所有 descriptor snapshot 和通知。
3. 把 origin 转成 Task logger 的保留关联字段并安装到执行线程。
4. 验证 TaskRegistry/Handle 析构、remove、取消和异常语义均不改变。

这是实质实现，完成后运行 `checkflow hardening`。

### 增量 4：强类型 Query 与日志查询

涉及：

- `include/axiom/introspection/introspection_query.hpp`
- `include/axiom/introspection/introspection_service.hpp`
- `src/introspection/introspection_service.cpp`
- `include/axiom/logging/log_query.hpp`
- `src/logging/log_collector.cpp`
- Introspection/logging 测试、umbrella header、安装消费者

工作：

1. 增加三类 Query 和确定的 AND/all-of/精确匹配语义。
2. 旧重载委托给 Query，保持返回类型、错误和排序兼容。
3. 增加有限的日志 correlation 查询字段。
4. 验证静态库、共享库和安装后消费者能只通过公开头使用新接口。

完成后运行 `checkflow fast`，交付前运行 `checkflow full`。

## 8. 测试清单

### Descriptor

- Module/Action 新字段注册后可完整发现，metadata 顺序确定且副本独立。
- 空 version、空 tag、重复 tag、空 metadata key 按最终契约被拒绝。
- Parameter 默认值、递归 TypeDescriptor 和当前错误路径回归不变。
- Resource 仍只暴露 id/type，不因本阶段产生推断字段。
- TaskOrigin 的无来源、部分来源、完整来源都能稳定复制。

### Invocation

- contextual Action 收到与 `Runtime::invoke()` 输入相同的 request/trace/caller/metadata。
- contextual 注入值包含权威 ActionId，且不出现在 Action 参数 descriptor 中。
- 两个并发 invocation 不串用上下文。
- 参数失败、业务 Error、标准异常、未知异常和无 Logger 行为保持当前结果。
- Action 同线程业务日志仍包含权威 module/action，metadata 不能覆盖保留字段。

### Task 与日志

- 旧 submit 产生 `origin == std::nullopt`，行为和源码兼容。
- 新 submit 从 Pending 到终态始终保留相同 origin；onChanged 也包含 origin。
- describe/list/Introspection 返回 origin 的独立副本。
- Action 创建的 Task 日志同时包含 request、trace、caller、module、action、task_id、task_name。
- 多个 Task 共享 request 时可按 request_id 查回全部关联日志；按 task_id 可缩小到单 Task。
- origin metadata 不能覆盖 Axiom 保留字段。
- Executor 拒绝、Pending 取消、Running 协作取消、失败、remove 和 Registry 析构契约不变。
- 并发测试用 barrier/latch/promise 控制交错，不依赖 sleep。

### Introspection Query

- Action module 精确匹配且不做前缀或大小写折叠。
- tags 使用 all-of，覆盖空条件、单 tag、多 tag、重复 query tag 和无匹配。
- Resource type 保持当前非法输入错误与合法空结果。
- Task state 覆盖五种 `TaskState`；origin action/request 分别及组合筛选。
- 多条件采用 AND，结果保持原始规范 ID 顺序。
- Query 期间来源并发变化时，每个返回 descriptor 仍满足来源自身一致性；不测试全局原子性。
- 旧 overload 与等价 Query 的结果和错误完全一致。

## 9. 验收映射

| 问题 | 公开入口 | 验收要点 |
| --- | --- | --- |
| 系统有哪些 Action？ | `introspection.actions()` | 返回完整、稳定排序的 ActionDescriptor 副本。 |
| Action 需要什么参数、返回什么？ | `describeAction(id)` | ParameterDescriptor 与 TypeDescriptor 完整可读。 |
| 有哪些 geometry Resource？ | `resources(ResourceQuery{.type = "geometry"})` | 只按公开 `type` 精确筛选。 |
| 有哪些 Running Task？ | `tasks(TaskQuery{.state = TaskState::Running})` | 只按公开 state 筛选。 |
| task:42 来自哪个 Action/request？ | `describeTask(id).origin` | origin 是稳定弱关联，不要求来源仍存在。 |
| request 产生了哪些日志？ | `LogCollector::query(LogQuery{.request_id = ...})` | Action 和派生 Task 日志均携带同一 request_id。 |

## 10. 明确不在本阶段实现

- OpenAI、MCP、JSON-RPC、JSON Schema 或任何供应商专属类型；
- Descriptor 的 JSON 序列化格式和协议字段别名；
- 任意 metadata/日志 fields 表达式、SQL/DSL、AND/OR tree；
- 全文搜索、相关度排序、分页 token、自定义 predicate ABI；
- Action 异步化、Action 对 Task 的所有权、Task 对 Action Runtime 的查询；
- 全局 ServiceLocator、跨 Registry 事务快照、持久 trace/span 系统；
- Resource 名称/描述/metadata，直到 Resource 注册模型拥有真实输入；
- 自动跨用户创建线程传播日志上下文；
- Python、Agent、MCP、RPC 或 CLI Adapter 本身。

## 11. 完成定义

本阶段只有在以下条件全部满足时才算完成：

- Canonical Descriptor 字段及适用范围由对应模块公开、校验和拥有；
- InvocationContext 可被需要它的 typed Action 显式读取，但不改变普通 Action 参数 schema；
- Task origin 是可选、不可变、可发现的弱关联，并自动进入 Task 执行日志；
- LogQuery 能按稳定 correlation 字段精确查询；
- Introspection 强类型 Query 只使用公开 Descriptor 字段，且保持原有排序、错误、生命周期与
  非原子 snapshot 契约；
- 新行为有确定性 GoogleTest 回归，公共 Doxygen、架构文档、umbrella header 和安装消费者同步；
- 每个增量通过 `checkflow fast`，实质实现通过 `checkflow hardening`，交付前
  `checkflow full` 通过；不得用 suppression、排除或降低规则通过门禁。

后续 Adapter 的唯一正确方向是：

```text
Axiom Descriptor / Query / Correlation
                  |
                  v
       Python / Agent / MCP / RPC / CLI
```

Adapter 可以转换字段和错误，但不能建立与 Axiom 并行的第二套运行时事实模型。
