# Axiom Task Runtime 首版开发计划

## 1. 目标与架构定位

在现有 `Axiom::Axiom` 库内新增 `axiom::task` 子系统，实现任务身份、状态、进度、协作取消、结果查询、日志关联和轻量通知。

按当前代码调整需求：

- 直接使用 `async::Executor::submit()`，不新增线程池、Scheduler 功能或 Executor 抽象。
- 依赖方向为 `task → foundation / async / events / logging`；底层模块不得反向依赖 task。
- 复用 `axiom::Result<T>`、`Error`、`logging::Logger` 和 `events::Signal`，导出符号使用 `AXIOM_API`。
- `Task<T>` 作为内部执行状态，不新增公开 Task 类。
- 不修改 Action 调用接口或 ABI，不实现 UI、Python、Agent 适配、动态结果序列化，以及原需求排除的调度扩展。

本文是开发计划存档，尚未开始实现。实施时从 `main` 创建 `codex/task-runtime`；创建前重新检查工作区，保留任何新增的无关改动。

## 2. 公共接口与行为契约

### 身份、上下文和进度

- `TaskId` 提供复制、比较、hash、`str()` 和 `parse()`；采用与 ResourceId 一致的规范文本风格：`task:<非零序号>`。不同 Registry 共用模块内序号分配，不复用已分配 ID；不承诺跨进程或重启后的唯一性。
- `TaskState` 使用 `Pending / Running / Completed / Failed / Cancelled`。
- `CancellationToken` 是可复制的只读取消标志，允许跨线程检查，不提供线程终止能力。
- `TaskContext` 提供 `id()`、`cancellation()`、`reportProgress()`；Context 不可复制或移动，仅在任务函数调用期间有效，进度只能由该任务执行线程报告。
- `Progress` 保留 `double value` 与自有 `std::string message`。初始值为 `0.0`；接受有限的 `[0,1]` 数值，允许进度回退，不合法输入抛出 `std::invalid_argument`。成功完成时设为 `1.0`；失败或取消保留最后进度。首版不加入不确定进度模式。

### 提交与观察

核心接口确定为：

```cpp
// F 接受 TaskContext&，返回 axiom::Result<T>
tasks.submit(executor, name, function)
    -> axiom::Result<TaskHandle<T>>;

handle.id();
handle.state();
handle.progress();
handle.cancel();
handle.result() -> std::optional<axiom::Result<T>>;

tasks.describe(id) -> axiom::Result<TaskDescriptor>;
tasks.list() -> std::vector<TaskDescriptor>;
tasks.cancel(id) -> axiom::Result<void>;
tasks.remove(id) -> axiom::Result<void>;
```

- `submit()` 从 callable 返回类型推导 `T`，支持只可移动的 callable；首版结果支持可复制值类型及 `void`。
- `TaskHandle<T>` 可复制，持有任务状态；不提供空默认句柄，缺省持有使用 `optional`。析构句柄不会取消任务。
- `result()` 不等待任务完成：未完成返回空，终态返回独立结果副本，可重复读取。读取和复制结果不会持有执行用户代码的内部锁。
- `TaskDescriptor` 包含需求中的 ID、名称、状态、进度和可选错误，是一次一致的值快照。`list()` 按 ID 文本排序；每项一致，但不承诺所有任务在同一时刻的全局快照。
- Registry 强持有已提交任务，包括终态任务。`remove()` 仅允许移除终态；活跃任务返回 `InvalidArgument`，未知 ID 返回 `NotFound`。移除后已有句柄仍然有效。
- 接受提交后，任务可以在 `submit()` 返回前开始或完成。关闭的 Executor 返回提交失败，不留下 Registry 条目，也不发出生命周期通知；分配失败允许抛出 `std::bad_alloc`。

### 取消与失败

采用已确认的“尊重任务返回值”语义：

- Pending 取消允许直接进入 Cancelled，Executor 排队包装器随后执行时跳过业务函数。
- Running 取消只设置标志，不立即结束任务。返回成功仍进入 Completed；返回取消错误进入 Cancelled；其他错误进入 Failed。
- 在 `ErrorCode` 末尾追加 `Cancelled`，保持现有枚举数值不变；取消由 `Result<T>::failure(Error{...})` 表达，不新增另一套结果类型。
- 任务返回的业务 Error 原样保留。逃逸的标准异常映射为 `InvocationFailed`，非标准异常映射为 `InternalError`，不把未经筛选的异常文本暴露到公共错误。
- 重复取消和终态取消均为无副作用操作；Registry 对存在的任务返回成功，未知 ID 返回 `NotFound`。
- 所有终态不可逆；状态、结果及错误在同一次受保护更新中提交。

## 3. 内部实现与集成

### 所有权和并发

- Registry 索引、任务状态、类型化结果分别由内部实现管理；模板头只承担 callable 适配与类型化访问，协调逻辑放入实现文件。
- Registry 查询、提交、取消、移除，以及句柄观察与取消支持并发。对象析构、句柄赋值或移动与该对象上的操作需要调用方同步。
- Registry 析构释放注册关系，不取消任务、不等待线程。已被 Executor 接受的工作和外部句柄继续持有必要状态，不能捕获裸 Registry 指针。
- Executor 生命周期继续遵循原契约；Task 不调用其 `close()`。业务 callable 捕获的外部引用仍由调用者保证生命周期。
- 不在 Registry 或任务状态锁内调用业务函数、通知回调、日志 sink，或释放可能执行用户析构函数的对象。

### 事件通知

- 提供一个 `TaskRegistry::onChanged(callback)` 订阅入口，返回现有 Signal 的 RAII Subscription；回调接收只读 `TaskDescriptor` 快照，不公开可变 Signal 或 `emit()`。
- 发出 Running、进度和终态通知；Pending 通过查询可见，不单独发出创建事件，也不回放历史。
- Task 内部按单个任务的状态更新顺序排队并串行发布通知；不同任务允许并发通知，不保证回调线程固定。
- 通知发生在状态提交之后。回调可重入查询、取消和解除订阅；回调收到的是变更快照，查询可能已看到更新状态。
- 在订阅适配层隔离每个回调的异常，记录诊断并继续其他订阅者，不改变现有 Signal 的异常传播契约。
- Registry 析构结束订阅入口；已取得的通知快照可能继续执行，不等待回调。调用方需保证回调捕获对象的生命周期。

### 日志与 Resource

- Registry 提供默认构造和接受 `logging::Logger` 的构造方式，与现有 Runtime 一致；默认日志为 no-op。
- 在业务函数实际执行的线程上建立 RAII 日志上下文，附加 `task_id`、`task_name`；发出开始、完成、失败、取消及通知异常诊断。
- 自动关联限于同一 LoggingService、同一执行线程，沿用现有字段覆盖规则；不自动传播至业务自行启动的线程。
- Task 对 `Value` 和 `resource::Handle<T>` 仅作为普通结果处理，无须依赖 Resource 实现。返回 Resource Handle 不延长资源注册寿命。
- 同步补齐新增错误码在现有日志分类代码中的处理，但不改变 Action 的调用方式。

## 4. 开发顺序与验证

1. **分支与基础模型**：检查工作区、创建分支，加入身份、状态、进度、取消错误及对应测试。
2. **执行与结果**：实现提交、内部共享状态、上下文、句柄、成功／失败／取消路径和异常边界。
3. **Registry 与生命周期**：完成查询、终态保留、显式清理、并发操作和独立于 Registry 的任务存活。
4. **通知与日志**：完成有序通知、异常隔离、回调重入及执行线程日志关联。
5. **对外交付**：更新 umbrella header、CMake 源文件与测试注册、架构规则、Doxygen 和架构文档；加入长任务示例及安装包消费者测试。

每个可验证增量运行 `checkflow fast`；实质实现完成后运行 `checkflow hardening`，交付前运行 `checkflow full`。不降低现有覆盖率、复杂度或变异测试门槛。

测试使用 GoogleTest，通过 promise、条件变量或 latch 控制交错，避免依赖 sleep：

- `int`、`void`、`Value`、Resource Handle 结果；未完成空结果及终态重复读取。
- 业务失败、标准与非标准异常、已关闭 Executor、只可移动 callable。
- Pending 取消跳过函数、Running 协作取消、忽略取消后成功、重复取消和完成竞争。
- 进度边界、NaN／无穷、消息复制、成功归一化及并发快照。
- 多 Registry ID 唯一、ID 解析、未知查询、活跃任务移除失败、终态移除和 Registry 析构后的句柄有效性。
- 单任务通知顺序、回调异常隔离、回调重入、解除订阅与跨任务并发。
- 日志关联、上下文恢复、线程复用不串号，以及日志服务先析构后的安全性。
- 安装后的独立消费者通过公共头实例化 Task 模板，并验证静态库、共享库及必要导出符号。

## 5. 验收与环境限制

验收示例使用项目实际语法返回 `Result<int>::success(42)`，能够完成提交、观察进度、请求取消、读取终态结果和按 ID 清理；无需修改 Action Runtime。

规划阶段已运行的 `checkflow doctor` 是工具可用性检查，不代表测试或质量门禁已经通过。当前 Windows 环境不支持 Mull，须在支持 Mull 的 Linux 环境补跑 hardening；Windows、Linux、macOS 的静态／共享构建与安装消费者验证分别记录结果，不能将未执行的平台检查标记为通过。
