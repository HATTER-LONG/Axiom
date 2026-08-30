# Axiom Core Resource / Handle 需求设计

> 状态：待实现；本文确定 MVP 契约，不代表当前 API 已提供。
> 范围：在 `Axiom::Core` 中新增独立的 `axiom::core::resource` 子系统。

## 1. 目标与仓库现状

为不适合直接放进 `Value` 的复杂 C++ 对象提供统一资源管理，例如 `Document / Mesh /
Shape / Scene`。对象留在 C++ 侧，调用方通过稳定 ID 和类型安全句柄引用它们。

当前仓库已有 `base`、`action` 和 `logging`，尚无 Resource 子系统：

- `Value` 与 `TypeDescriptor` 仅覆盖标量、Array 和 Object，不支持资源类型。
- `Result<T>` 支持承载 move-only 值；`ErrorCode` 已有 `InvalidArgument`、`NotFound`、
  `TypeMismatch` 等类别，可以直接复用，无须新增一套错误机制。
- `action/detail/Registry` 管理模块和 Action，不承担领域对象生命周期；不扩展或复用它
  来保存资源。当前注册入口是 `ModuleBuilder::add()`，没有 `module.action()`。

归属模块为 `resource`，仅依赖 `base` 和标准库；领域类型及其 traits 由业务模块定义。
MVP 不修改 `Value`、`TypeDescriptor`、Action 转换器或 Runtime 的公开接口和现有行为。
现有架构概览见 [Core 设计](core-framework-phase-1.md)，其中历史描述与源码不一致时以
源码为准，例如 Logging 已实现。

## 2. 核心模型与范围

```text
C++ Object → ResourceRegistry → ResourceId → Handle<T>
                  ↑                              |
                  └──────── resolve<T>() ─────────┘
                                ↓
                          ResourceRef<T> → 访问对象
```

生命周期原则：Registry 管理资源的注册与持有；Handle 只标识资源；ResourceRef 在一次
访问期间保活对象。移除注册与对象物理析构是两个不同事件。

MVP 包含 ID、类型映射、句柄、访问引用，以及注册、解析、移除、存在性查询。不包含
全局 Registry、自动缓存、持久化、资源枚举、依赖图、级联删除、事件、异步加载、权限、
网络协议、Python/UI 绑定或插件卸载。没有当前需求的接口不提前加入。

## 3. 主要类型与最小接口

以下为拟定的调用接口，省略 Doxygen、导出宏和私有成员，并非可直接编译的完整头文件。

| 类型 | 契约 |
| --- | --- |
| `ResourceId` | 拥有规范 ID 数据；支持复制、相等与全序比较、`std::hash`、`str()` 和 `parse()`。不存对象地址或所有权。 |
| `ResourceTraits<T>` | 通过 `static constexpr std::string_view type_name` 提供稳定逻辑类型名，例如 `Shape → "shape"`。 |
| `Handle<T>` | 只持有一个 `ResourceId`；可复制、可比较、可 hash；以 `explicit Handle(ResourceId)` 构造，`id()` 返回只读 ID。无解引用、隐式跨类型转换或 Registry 指针。 |
| `ResourceRef<T>` | resolve 成功后取得的 move-only RAII 访问对象；提供 `operator-> / operator*`，内部保活资源，不暴露共享所有权接口。 |
| `ResourceRegistry` | 宿主显式创建，不可复制或移动；管理所有注册记录及类型校验，公开操作仅为下列四项。 |

```cpp
template <typename T>
Result<Handle<T>> add(std::unique_ptr<T> object);

template <typename T>
Result<ResourceRef<T>> resolve(const Handle<T>& handle) const;

bool remove(const ResourceId& id);
bool contains(const ResourceId& id) const;
```

`ResourceId::parse(std::string_view)` 返回 `Result<ResourceId>`；`str()` 返回在 ID 存活且
未修改期间有效的 `std::string_view`。`Handle::id()` 返回相同生命周期约束的 const 引用。
ID 和 Handle 不提供默认空值；可选资源由调用方使用 `std::optional<Handle<T>>` 表达。
移动后的 ID、Handle 和 ResourceRef 仅允许析构或重新赋值，不允许读取或解引用。

`T` 在 MVP 中必须是完整、非 cv 限定、非数组的对象类型，具有不抛异常的析构函数和有效
traits。不支持按基类查询、类型转换、`Handle<const T>` 或自定义 deleter；需要这些能力
时先由领域包装类型承接。类型别名视为同一 C++ 类型。

### ResourceId 与类型校验

- 规范文本为 `<type>:<serial>`，例如 `shape:42`。type 满足 `[a-z][a-z0-9_]*`；serial 是
  非零 `uint64_t` 的十进制形式，不允许前导零、符号、空白或溢出。解析非法文本返回
  `InvalidArgument`。比较按规范文本字典序，hash 与相等一致，不承诺 hash 跨进程稳定。
- 稳定性范围是同一进程中同一 Core 实例的运行期：序号由 Core 实现内部统一分配，在
  多个 Registry、删除及 Registry 重建之间均不复用；计数耗尽返回 `InternalError`，
  不回绕。失败注册允许消耗序号，因此不保证连续。不同独立加载的 Core 副本不互操作。
- ID 不因对象内容变化而变化；移除后旧 ID 永久失效。`parse()` 只验证语法，不保证资源
  存在，也不提供跨进程、重启后的身份恢复；第二阶段传输必须增加会话/宿主范围约束。
- traits 未定义或名字不合法时在编译期拒绝。名字不得来自 RTTI 的显示名称或编译器签名。
  Registry 同时校验逻辑类型名和内部精确 C++ 类型身份，绝不仅按字符串强制转换指针。
- 同一 Registry 内一个逻辑名只能对应一种 C++ 类型，该绑定保持到 Registry 销毁，即使
  此类型的资源已全部移除。不同 C++ 类型重复占用逻辑名时，`add` 返回 `TypeMismatch`。
  跨 Registry 的逻辑命名一致性由业务模块负责，MVP 不建立全局类型注册表。

`Handle<T>` 的类型参数限制 C++ 调用方式，但从 ID 构造 Handle 不证明运行时类型正确。
最终有效性与类型安全始终由 Registry 的 `resolve` 保证。

## 4. 生命周期、失败与线程契约

| 操作或事件 | 可观察行为 |
| --- | --- |
| `add<T>(object)` | 接收 `unique_ptr<T>` 的所有权，成功返回新 Handle；空指针返回 `InvalidArgument`。同内容对象可分别注册，ID 不同。 |
| `add` 失败 | Registry 中现有资源与类型绑定不变。按值传入的所有权已经转移，失败时新对象被释放，不承诺退还给调用方；分配失败可抛 `std::bad_alloc`。 |
| `resolve<T>(handle)` | 先查完整 ID；缺失、已移除或属于另一 Registry 时返回 `NotFound`；命中后逻辑名或精确 C++ 类型不匹配返回 `TypeMismatch`；成功取得非空 ResourceRef。 |
| `contains(id)` | 仅报告当前 Registry 是否仍注册该完整 ID，不证明某个 `T` 可解析，也不保活对象。 |
| `remove(id)` | 首次移除返回 `true`；不存在或重复移除返回 `false`，无其他副作用。不级联移除其他资源。 |
| 移除后 | `contains` 为 false，后续 resolve 为 `NotFound`；已有 ResourceRef 仍可访问同一对象。无引用时立即析构，否则最后一个 ResourceRef 释放时析构一次。 |
| Registry 销毁 | 撤销全部注册并释放其持有；已有 ResourceRef 可继续访问，Handle 可继续作为 ID 值保存。资源间析构顺序未指定。 |

ResourceRef 不是裸借用：它通过内部共享保活避免 `remove()` 或 Registry 析构造成悬空。
Registry 是注册所有者，但不能宣称其销毁必定立即析构所有对象。对象不会被 resolve 复制，
同一资源的多个访问引用观察到同一对象；const Registry 的 resolve 仍可取得可变访问。

ResourceRef 不可默认构造，成功结果之外没有可访问的空引用。`operator-> / operator*`
返回的指针或引用只允许在对应 ResourceRef 有效期间使用；不能长期缓存或传过动态边界。
引用应限制在一次业务访问内，不应存入资源或全局状态，避免互相保活形成环。框架不强制
超时回收，也不执行循环引用收集。

MVP 不承诺线程安全：同一 Registry 的所有操作和析构均须由宿主串行化；不同 Registry
可以独立使用，内部 ID 分配须防止并发重复。ResourceRef 的保活不代表对象线程安全，
并发读写与析构线程要求由领域类型及宿主负责。对象析构发生在最后一个持有者释放它的
线程，不保证 UI 线程；不允许析构回调重入正在移除或销毁的所属 Registry。

错误复用 `base/Result` 与 `ErrorCode`；可带规范 ID、期望/实际逻辑名，不暴露地址、RTTI
字符串或实现布局。没有 Action 参数上下文时不伪造 `Error.path`。内存分配等异常直接
传播，不在 Resource 层转成 `InvocationFailed`，也不静默吞掉失败。

## 5. 目录与实现边界

```text
src/core/include/axiom/core/resource/
├── resource_id.hpp
├── resource_traits.hpp
├── handle.hpp
├── resource_ref.hpp
├── resource_registry.hpp
└── detail/
    └── resource_entry.hpp
src/core/src/resource/             # ID 分配、Registry 存储等非模板实现
tests/resource_test.cpp
```

模板只承接类型约束与有类型的访问，查找、身份分配、类型绑定、事务提交和生命周期策略
由 Registry 私有实现统一负责。`detail/resource_entry.hpp` 承载必要的类型擦除与保活，
其内部类型不是稳定 API；不得把 `std::any`、`shared_ptr<void>`、可变容器或原始存储指针
暴露给调用方，也不要求业务类继承统一 Resource 基类。

实现时更新 `core.hpp`、`src/core/CMakeLists.txt`、测试清单及架构规则：resource 不得
依赖 action、logging、应用或外部适配器，base 不得反向依赖 resource。复用 Result 所
带来的既有 Value 依赖允许存在，但不意味着向 Value 增加 Resource 分支。

保持 `Axiom::Core` 单一导出目标，非模板公共符号使用 `AXIOM_CORE_API`；公开头包含
`@file` / `@brief` 和完整契约。精确类型身份必须验证从安装消费者注册自定义类型、再在
共享库边界解析的情况，不能依赖某个翻译单元局部静态对象的地址偶然相同。MVP 不承诺
动态插件之间的类型身份或卸载安全。不得引入 Qt、OCC、Boost 或平台专用公开类型。

## 6. MVP 使用示意

```cpp
struct Shape {
    double x = 0.0;
};

template <>
struct axiom::core::resource::ResourceTraits<Shape> {
    static constexpr std::string_view type_name = "shape";
};

// 以下位于业务函数内，返回类型为 axiom::core::Result<void>。
axiom::core::resource::ResourceRegistry resources;
auto added = resources.add(std::make_unique<Shape>());
if (!added) {
    return axiom::core::Result<void>::failure(added.error());
}
auto handle = added.value();
auto resolved = resources.resolve(handle);
if (!resolved) {
    return axiom::core::Result<void>::failure(resolved.error());
}
resolved.value()->x += 10.0;
resources.remove(handle.id());       // 撤销注册；resolved 仍保活 Shape
return axiom::core::Result<void>::success();
```

## 7. 实施顺序与验收

按三个可验证增量实施，每步执行 `checkflow fast`：

1. ID、traits 与 Handle：验证规范文本往返、非法输入、比较/hash、一致性及类型约束。
2. Registry 与 ResourceRef：实现所有权转移、类型校验、访问保活和完整失败契约。
3. 安装与架构收口：公开头、导出符号、依赖规则、安装消费者与契约文档同步完成。

GoogleTest 验收至少覆盖：

- 注册和修改不可复制对象；多个 resolve 指向同一对象；Handle 复制不延长生命周期。
- 空输入、未知 ID、跨 Registry ID、错误类型 Handle、同逻辑名不同 C++ 类型；失败不
  破坏已有条目；移除全部对象后仍拒绝不同类型抢占逻辑名。
- 重复 remove、移除后 resolve、仍持有 ResourceRef 时移除/销毁 Registry、最后引用释放
  后恰好一次析构，以及失败注册时输入对象的释放。
- 多 Registry、Registry 重建、删除再注册及不同 Registry 并发分配均不复用 ID；序号
  溢出通过私有可测试边界验证，不增加公开计数器或测试专用开关。
- 缺失/非法 traits 和跨类型 Handle 转换在编译期拒绝；move-only 约束及生命周期测试
  不解引用已移出或悬空对象。
- 默认静态库与 `-DBUILD_SHARED_LIBS=ON` 下的已安装包消费者，含业务自定义类型、
  ID/hash、注册、解析和释放；Windows、Linux、macOS 的构建与导出行为一致。

实质实现完成后执行 `checkflow hardening`，交付前执行 `checkflow full`。现有 Value、
Runtime、Logging 和安装测试不得退化，不降低覆盖率、静态分析或变异测试规则。
本次仅交付需求文档，不创建上述源码或提前开放接口。

## 8. 第二阶段：Value 与 Action 接入

```text
带会话范围的外部引用 ↔ ResourceId ↔ Value ↔ Handle<T> → Action 参数
```

第二阶段再确定 Value 表达、资源类型描述、输入/返回转换、默认参数规则与错误 path，
以及宿主向调用提供 Registry 的方式。若选择 Value 原生资源类型，必须先安排基础 ID
类型的分层归属，避免 `base ↔ resource` 循环依赖；不能简单在 Value 中包含 Registry。
Runtime 是否自动校验资源存在、访问引用覆盖多长调用范围，也必须显式设计。

目标调用按当前接口风格表达如下，**MVP 不支持编译此注册**：

```cpp
module.add("translate", "平移形状",
           [&resources](Handle<Shape> shape, double x) -> Result<void> {
               auto ref = resources.resolve(shape);
               if (!ref) return Result<void>::failure(ref.error());
               ref.value()->translate(x);
               return Result<void>::success();
           },
           param("shape"), param("x"));
```

此处 Shape 是提供 `translate()` 的领域类型；捕获的 Registry 必须覆盖 callable 的使用
期。大纲中的 `module.action(...)` 表达同一意图，不因此新增与 `add()` 重复的注册入口。

后续 Python、UI、RPC 和 Agent 仅交换受会话约束的资源标识；ID 不是授权令牌，不能因为
持有或猜中 ID 就允许访问。权限、断连清理和跨进程映射属于适配器与宿主策略，不由 MVP
隐式承担。领域对象应通过稳定包装接口隐藏第三方实现，Handle 本身不能消除领域 API 的泄漏。

> 一句话定义：Resource / Handle 用稳定 ID 和类型安全句柄管理复杂 C++ 对象，使 Action、
> Python、UI、RPC 和 Agent 可以引用真实对象，而不通过动态边界暴露原始指针或具体实现类型。
