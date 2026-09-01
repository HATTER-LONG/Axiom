# Axiom Introspection / Discovery 设计

## 1. 目的与结论

本文定义 Axiom 首版统一只读运行时观察层。它面向 UI、Python、自动化系统和
Agent Adapter，回答以下问题：

- 当前注册了哪些 Module 和 Action，Action 的调用契约是什么；
- 当前注册了哪些 Resource，它们的稳定身份和逻辑类型是什么；
- 当前保留了哪些 Task，它们的状态、进度和失败信息是什么；
- 顺序读取上述来源时，Axiom 的可观察状态是什么。

结合当前项目，原始需求需要作四点修正：

1. `axiom::Runtime` 已经提供 `discoverModules()`、`discoverActions()`、
   `findModule()` 和 `findAction()`，Action 侧不应再建立 Registry 或 Descriptor。
2. `task::TaskRegistry` 已经提供 `list()`、`describe()` 和完整的
   `TaskDescriptor`，Task 侧只需转发只读查询。
3. `resource::ResourceRegistry` 当前只有 `add / resolve / remove / contains`，也没有
   Resource 名称或元数据模型。MVP 必须先补充最小只读查询；不能凭空生成名称和元数据。
4. 当前没有拥有 Action、Resource 和 Task 的“应用总 Runtime”。统一快照只能聚合由宿主
   创建并保证生命周期的三个现有对象；首版不是跨 Registry 的事务快照。

建议新增 `axiom::introspection::IntrospectionService`，它非拥有地引用现有
`Runtime`、`ResourceRegistry` 和 `TaskRegistry`，不保存描述数据、不暴露内部 Registry，
也不提供任何修改操作。

## 2. 当前架构事实

现有依赖关系为：

```text
foundation <- action -> logging
foundation <- resource
foundation <- async
task -> foundation / async / events / logging

introspection -> action / resource / task
```

`introspection` 是新的顶层只读聚合模块。现有模块不依赖它，应用和后续 Adapter 可以依赖
它。该方向应加入 `quality/architecture_rules.json`，禁止 `foundation`、`action`、
`resource`、`task` 等底层模块反向包含 `axiom/introspection/`。

### 2.1 Action 的现状

- `ModuleDescriptor` 已包含规范 namespace 和字符串 metadata。
- `ActionDescriptor` 已包含 `ActionId`、描述、按调用顺序排列的参数、返回类型、版本和 tags。
- `ParameterDescriptor` 已包含名称、描述、是否必填、`TypeDescriptor` 和默认值。
- discovery 结果按规范 Module namespace 或完整 Action ID 升序排列。
- Descriptor 由 `Runtime` 拥有，当前查询返回只读引用，生命周期持续到 Runtime 析构。
- `Runtime` 当前明确声明为非线程安全：注册、发现和调用不能并发。

因此 Action Introspection 已经存在，统一服务只负责复制其结果并提供统一入口。

### 2.2 Resource 的现状

- `ResourceId` 是 `<type>:<serial>` 形式的稳定身份值，不代表对象仍然存在。
- `ResourceTraits<T>::type_name` 是稳定逻辑类型名；Registry 内部也保留该名字与精确 C++
  类型的绑定。
- Registry 不保留显示名称、描述或任意 metadata。
- Handle 只保存 ID，`ResourceRef<T>` 只提供带 keepalive 的类型化访问。
- 当前契约要求宿主串行化同一个 Registry 的全部操作和析构。

因此首版 `ResourceDescriptor` 只能诚实地包含 `id` 和 `type`。显示名称和 metadata 需要未来
先扩展资源注册输入及其所有权契约，再原样暴露给 Introspection；不得从 RTTI、对象地址或
序号猜测。

### 2.3 Task 的现状

- `TaskDescriptor` 已包含 ID、名称、状态、进度和可选 `Error`。
- `TaskRegistry::describe()` 返回单个一致的值快照；`list()` 返回按 Task ID 排序的独立值。
- `list()` 中每项各自一致，但不承诺所有 Task 来自同一个时刻。
- Task 查询、提交、取消和移除已经支持并发。
- Registry 只列出仍保留的 Task；终态 Task 经显式 `remove()` 后不再被发现。

统一服务不读取类型化 Task 结果，因为 `TaskDescriptor` 没有统一动态结果模型，原始需求也只
要求状态、进度与错误。

## 3. 范围

### 3.1 MVP 包含

- 列出 Module 和 Action；
- 按 Module 过滤 Action；
- 按 ID 描述 Action；
- 列出 Resource，按逻辑类型过滤 Resource，按 ID 描述 Resource；
- 列出 Task，按 ID 描述 Task；
- 生成包含以上四类值的 `RuntimeSnapshot`；
- 确定性排序、明确的 NotFound 错误和并发读取契约。

### 3.2 MVP 不包含

- 注册 Module/Action、创建或解析 Resource、提交/取消/移除 Task；
- JSON、Python、MCP、Qt、HTTP 或其他序列化/绑定接口；
- 查询语言、全文搜索、权限、缓存、历史状态或远程 Discovery；
- 日志聚合、Task 类型化结果、Resource 内容读取；
- 事件订阅或将 snapshot 与 events 自动拼接；
- 跨 Action、Resource、Task 的全局事务或线性一致性。

## 4. 公共模型

新增头文件建议为：

```text
include/axiom/introspection/resource_descriptor.hpp
include/axiom/introspection/runtime_snapshot.hpp
include/axiom/introspection/introspection_service.hpp
```

除 Resource 外直接复用现有 Descriptor，不建立 `IntrospectedAction`、
`IntrospectedTask` 等平行模型。

```cpp
namespace axiom::resource {

/** @brief 当前 Resource 注册记录的只读值快照。 */
struct ResourceDescriptor final {
    ResourceId id;
    std::string type;
};

} // namespace axiom::resource

namespace axiom::introspection {

/** @brief 顺序采集各运行时来源得到的只读值快照。 */
struct RuntimeSnapshot final {
    std::vector<ModuleDescriptor> modules;
    std::vector<ActionDescriptor> actions;
    std::vector<resource::ResourceDescriptor> resources;
    std::vector<task::TaskDescriptor> tasks;
};

} // namespace axiom::introspection
```

`ResourceDescriptor::type` 取 Registry 注册时保存的逻辑类型名，并与 `id` 的 type 部分一致。
即使当前可由 ID 文本解析，也应由 Resource 模块构造 Descriptor，避免 Introspection 复制
ResourceId 语法和 Registry 不变量。

所有返回给统一服务调用者的集合和 Descriptor 都是自有值，不返回
`reference_wrapper`。这使 Adapter 无需理解底层对象地址和引用有效期，并允许快照在下一次
Registry 变化后继续独立存在。它不是状态缓存：每次调用仍直接从真实来源复制当前值。

## 5. IntrospectionService 接口

建议的最小接口如下：

```cpp
namespace axiom::introspection {

class AXIOM_API IntrospectionService final {
public:
    IntrospectionService(const Runtime& actions,
                         const resource::ResourceRegistry& resources,
                         const task::TaskRegistry& tasks) noexcept;
    ~IntrospectionService() noexcept;

    IntrospectionService(const IntrospectionService&) = delete;
    IntrospectionService& operator=(const IntrospectionService&) = delete;
    IntrospectionService(IntrospectionService&&) = delete;
    IntrospectionService& operator=(IntrospectionService&&) = delete;

    [[nodiscard]] std::vector<ModuleDescriptor> modules() const;
    [[nodiscard]] std::vector<ActionDescriptor> actions() const;
    [[nodiscard]] std::vector<ActionDescriptor>
    actions(std::string_view module_namespace) const;
    [[nodiscard]] Result<ActionDescriptor> describeAction(const ActionId& id) const;

    [[nodiscard]] std::vector<resource::ResourceDescriptor> resources() const;
    [[nodiscard]] Result<std::vector<resource::ResourceDescriptor>>
    resources(std::string_view type) const;
    [[nodiscard]] Result<resource::ResourceDescriptor>
    describeResource(const resource::ResourceId& id) const;

    [[nodiscard]] std::vector<task::TaskDescriptor> tasks() const;
    [[nodiscard]] Result<task::TaskDescriptor>
    describeTask(const task::TaskId& id) const;

    [[nodiscard]] RuntimeSnapshot snapshot() const;
};

} // namespace axiom::introspection
```

接口约束：

- 构造函数不接管三个来源。它们必须在 Service 析构前存活；它们的析构不能与 Service
  查询并发。
- 三个来源全部必需。MVP 不增加 nullable source、动态 attach/detach 或“部分 snapshot”
  配置面；需要部分能力的调用者可直接使用现有子系统查询。
- `actions(module)` 只按 `ActionId::module()` 精确匹配规范 namespace，不做模糊搜索。
- `resources(type)` 只按规范逻辑类型精确匹配；实现复用 Resource 的规范名校验并返回
  `Result`，不私自容忍非法输入。
- `describe*()` 保留来源的 `ErrorCode::NotFound`，不建立 Introspection 专用错误枚举。
- 返回顺序稳定：Module 按 namespace，Action 按完整 ID，Resource 按完整 ID，Task 按 ID。
- 分配或复制失败沿用项目惯例抛出 `std::bad_alloc`，不伪装为查询业务错误。

为保持公共接口小，MVP 不增加通用字符串 ID、variant descriptor、分页对象、查询 builder 或
回调访问器。

## 6. ResourceRegistry 的最小前置能力

Introspection 不得成为 Resource 内部状态的第二个拥有者，也不应通过 `friend` 读取
`ResourceRegistry::Impl`。应由 Resource 模块在自己的边界提供最小只读能力：

```cpp
namespace axiom::resource {

struct ResourceDescriptor final {
    ResourceId id;
    std::string type;
};

class ResourceRegistry {
public:
    [[nodiscard]] Result<ResourceDescriptor> describe(const ResourceId& id) const;
    [[nodiscard]] std::vector<ResourceDescriptor> list() const;
    // 原有 add / resolve / remove / contains 保持不变。
};

} // namespace axiom::resource
```

`axiom::resource` 拥有 `ResourceDescriptor`，`IntrospectionService` 直接复用它，避免底层
模块返回上层类型。

`list()` 必须复制 ID 和逻辑类型，不返回对象、`shared_ptr`、RTTI `type_info`、内部 Entry 或
可解析的 `ResourceRef`。被移除的注册不再出现；仍由外部 `ResourceRef` keepalive 的对象也不
出现，因为 Discovery 描述的是 Registry 成员关系而不是对象存活。

这是对 Resource 公共 API 的**增量只读扩展**，不会改变已有四个操作的签名或 Resource
所有权语义。没有这项扩展，就无法在遵守 Single Source of Truth 和模块边界的同时完成
Resource Discovery。

## 7. 并发与一致性

### 7.1 必须先修正的来源契约

仅给 `IntrospectionService` 加 mutex 不能使查询线程安全：应用仍可绕过 Service 调用
`Runtime::registerModule()`、`Runtime::invoke()` 或 `ResourceRegistry` 修改方法。要满足原始
MVP 的线程安全目标，锁必须位于各自状态的拥有模块：

- Action Registry 使用读写同步：Module 注册取得独占锁；invoke、find 和 discover 取得
  共享锁。锁内允许定位并保留不可变 Action 实现所需的稳定访问，但不得把新的粗粒度锁带入
  用户 callable 或 logging sink。具体方案需要先验证 Action 实现的生命周期，再决定是在
  调用期间持有共享锁，还是复制稳定的内部调用句柄。
- ResourceRegistry 使用内部同步保护 entries 和 bindings；`add/remove/resolve/contains/list/
  describe` 支持并发。和现有 Task 规则一致，不在 Registry 锁内销毁用户 Resource；remove
  先移出 keepalive，再在解锁后释放。
- TaskRegistry 保持现有并发契约，不新增外层锁。

这是现有行为契约的增强，不改变已有调用签名。如果本阶段不实施这两项来源级同步，则文档和
API 必须诚实降级为“调用方保证 Action 与 Resource 查询不和其他操作并发”，不能把 Service
标记为线程安全。

### 7.2 单项与列表一致性

- `describeAction()`、`describeResource()` 和 `describeTask()` 各自返回一次来源查询所得的一致
  值。
- 每个 `modules/actions/resources/tasks` 调用返回一次来源级列表；集合排序确定。
- 对 Task 沿用已有语义：列表中每项一致，但多个 Task 可能来自略有不同的时刻。
- 查询返回后，真实 Registry 可以立即变化；返回的值不随之变化。

### 7.3 RuntimeSnapshot 不是全局原子快照

`snapshot()` 按固定顺序读取 Module/Action、Resource、Task。它不会同时锁住三个 Registry，
原因是这会引入跨模块锁顺序、延长用户状态锁持有时间，并提高死锁和调用方阻塞风险。

因此 `RuntimeSnapshot` 的准确契约是：

> 一组在 `snapshot()` 调用期间顺序采集的来源级只读值；每个来源遵守自身列表一致性，但不
> 承诺所有字段对应同一个全局时刻。

首版不添加 timestamp 或 generation。当前三个来源没有共同 clock/generation，添加一个时间
值也不能证明原子性。若未来确有全局一致快照需求，应先引入拥有全部运行时状态和写入协调的
更高层 Runtime，而不是在 Introspection 内伪造事务。

## 8. 与 Events 和 Logging 的关系

Introspection 与 Events 保持正交：

```text
snapshot()          回答“现在可观察到什么”
TaskRegistry event  回答“Task 发生了什么变化”
```

典型 Adapter 可以先读取 snapshot，再订阅已有 Task 事件，但首版没有 Module/Resource 事件，
也没有用于消除 snapshot/subscribe 间竞态的 generation 或 replay 协议。因此不能承诺无遗漏的
通用增量同步。需要该保证时，应由各状态拥有模块先设计带序号的事件契约。

Logging 继续独立。`LogCollector / LogQuery` 回答“最近发生了什么、为何失败”；Task 的当前失败
信息仍来自 `TaskDescriptor::error`。MVP 不把日志记录塞入 RuntimeSnapshot，也不让
Introspection 依赖 logging。

## 9. 内部实现与所有权

- Service 只保存三个非拥有指针或引用，协调逻辑放在 `.cpp`；不复制 Registry，不注册回调。
- `modules()` 和 `actions()` 将 Runtime 返回的只读引用深复制成现有 Descriptor 值；
  `TypeDescriptor` 的递归所有权必须沿用其现有复制语义并由测试覆盖。
- `actions(module)` 可从已排序 Action 列表稳定过滤，不需要 Action Registry 新增专用索引。
- Resource 过滤同样从 `ResourceRegistry::list()` 结果稳定过滤；不公开 binding map。
- Task 方法直接调用 `TaskRegistry::list/describe`。
- `snapshot()` 复用上述私有复制 helper，避免一次调用中因公共方法层层构造不必要的临时值；
  但不保留结果供下一次调用复用。
- Service 不调用任何 mutation API，不需要 `const_cast` 或友元访问。

公开头中的每个类型和方法必须使用 Doxygen 说明返回值、排序、生命周期、并发及非原子快照
契约。新增导出符号使用 `AXIOM_API`，加入 `<axiom/axiom.hpp>` umbrella header，并验证静态、
共享和安装包消费者。

## 10. 错误语义

| 操作 | 情况 | 结果 |
|---|---|---|
| `describeAction(id)` | Action 不存在 | `ErrorCode::NotFound` |
| `describeResource(id)` | 已移除、外部 Registry 或未知 ID | `ErrorCode::NotFound` |
| `describeTask(id)` | 已移除或未知 Task | `ErrorCode::NotFound` |
| type/module 精确过滤 | 合法但无匹配项 | 成功的空 vector |
| Resource type 过滤 | 非规范 type | `ErrorCode::InvalidArgument` |
| 任意复制查询 | 内存分配失败 | 抛出 `std::bad_alloc` |

描述查询不应把“在查询后立即被移除”视为错误：返回的是查询成功时复制出的独立值。

## 11. 实施顺序

1. **Resource 描述能力**：在 Resource 模块增加其拥有的 `ResourceDescriptor`、`describe()` 和
   `list()`，保持确定性顺序、移除语义及无对象泄漏。
2. **来源并发契约**：分别在 Action 和 Resource 所有模块内完成同步与生命周期设计，补充并发
   回归测试并更新现有 Doxygen/architecture 文档。
3. **统一值模型**：增加 `RuntimeSnapshot`，只组合现有 `ModuleDescriptor`、
   `ActionDescriptor`、`resource::ResourceDescriptor` 和 `task::TaskDescriptor`。
4. **聚合服务**：实现最小非拥有 Service、过滤和 describe 转发，验证 NotFound、排序、深复制
   与来源析构前置条件。
5. **集成**：更新 umbrella header、CMake、架构规则、安装消费者和共享库导出验证。

每个可验证增量运行 `checkflow fast`；实质实现后运行 `checkflow hardening`；交付前运行
`checkflow full`。不得用 suppression、排除实现文件或降低规则来通过门禁。

## 12. 测试设计

### Resource

- 多类型、多实例按完整 ID 确定性排序；
- `describe()` 返回正确 ID/type，未知、已移除及外部 Registry ID 返回 NotFound；
- list/describe 不延长 Resource 对象生命周期；外部 ResourceRef 存活时移除后仍不可发现；
- 并发 add/resolve/remove/contains/list/describe 无数据竞争，用户析构在锁外运行并可按既有契约
  安全释放相关外部状态；
- 同一逻辑名的 type binding 规则在并发改造后不变。

### Action

- Service 复制出的 Module/Action 与 Runtime 现有 discovery 完全一致且顺序稳定；
- 按 Module 精确过滤，不接受前缀或大小写折叠；
- 嵌套 `TypeDescriptor` 深复制，Service 返回值在后续调用后仍有效；
- 注册、查询和调用的并发契约按最终来源实现验证；测试 callable 重入边界，避免锁内执行用户
  析构或日志 sink。

### Task 与 Snapshot

- Task 描述完整保留状态、进度、消息和 Error；移除后为 NotFound；
- Snapshot 空状态、混合状态和确定性顺序；
- 在 Task 变化期间重复 snapshot，每个 TaskDescriptor 始终满足现有状态不变量；
- 明确测试而非隐藏非原子语义：不同来源可在采集间变化，snapshot 仍是有效值集合且不崩溃；
- Service 析构不影响三个来源，三个来源在 Service 存活期间不得先析构；
- 安装后的独立消费者只通过公共头创建 Service 并读取 snapshot；静态和共享构建均通过。

并发测试使用 latch、promise 或条件变量控制交错，不依赖 sleep。

## 13. 验收标准

完成 MVP 后，公共 API 能以现有 Axiom 类型回答：

- `modules()` / `actions()`：Axiom 当前公开哪些同步能力；
- `describeAction()`：某个 Action 的参数、默认值、返回类型和描述是什么；
- `resources()` / `describeResource()`：Registry 当前拥有哪些身份及其逻辑类型；
- `tasks()` / `describeTask()`：Registry 当前保留哪些 Task，它们运行到哪里、是否失败；
- `snapshot()`：一次调用期间顺序观察到的上述完整值集合。

验收不应声称 Resource 具有当前模型没有保存的名称或 metadata，不应声称 snapshot 是全局原子
状态，也不应声称仅靠聚合 Service 能修复来源对象的并发限制。满足线程安全验收的前提是
Action 与 Resource 的状态拥有模块完成第 7 节所述同步增强。

## 14. 后续扩展边界

稳定后可在更高层增加 JSON、Python、MCP 或 UI Adapter。Adapter 负责字段命名、序列化、
schema 映射和协议错误转换，不反向污染基础模型。若未来 Resource 注册流程增加明确的显示名称、
描述或 metadata，`resource::ResourceDescriptor` 可在真实来源拥有这些字段后作兼容扩展；若需要
无遗漏的 snapshot + events 同步，则先在各 Registry 引入 generation/replay 契约。

一句话定义：

> Axiom Introspection / Discovery 是一个非拥有、无缓存的只读聚合层；它复用 Action 与 Task
> 的现有描述模型，并由 Resource 模块补齐最小描述能力，以稳定值快照向 Adapter 暴露当前
> Runtime 可观察状态，同时不伪造跨 Registry 原子性或底层不存在的元数据。
