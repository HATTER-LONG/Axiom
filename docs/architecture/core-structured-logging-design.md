# Axiom Core 结构化 Logging 子系统设计

> 状态：实施中（基础 logging 与首期 Sink 已交付）
> 范围：在 `Axiom::Core` 中引入 `axiom::core::logging`

## 1. 目标与边界

在现有 `Axiom::Core` 内新增 `axiom::core::logging`，形成 `base <- logging <- action`
的依赖方向。Logging 可以复用 `Value`，但不得依赖 Runtime、Qt、UI、插件或 Agent。

每个宿主创建一个 `LoggingService`，各模块从同一服务取得带独立 category 的 Logger，例如
`runtime`、`geometry` 与 `python`。服务统一完成过滤，并将记录扇出到全部匹配的 Sink。

不提供进程级全局 Logger。默认构造的 Logger 是安全的 no-op，因此现有 Runtime 的默认行为
与无日志输出保持不变。

第一阶段已经交付 Console、Callback 和内存 Collector。文件及轮转文件通过同一 `ILogSink`
扩展，不在首期实现。

## 2. 分层与目录

```text
应用 / 未来适配器
          |
          v
     Axiom::Core
   base <- logging <- action
```

建议目录如下：

```text
src/core/include/axiom/core/
├── core.hpp
├── base/
├── logging/
│   ├── {log_level,log_record,log_filter,log_query,log_sink}.hpp
│   ├── {logger,logging_service,console_sink,callback_sink,log_collector}.hpp
│   └── detail/
└── action/
src/core/src/
├── logging/
└── action/
```

更新 Core umbrella header、安装导出、架构规则和架构文档。架构规则必须禁止 Logging 包含
action、应用、Qt 或 Agent 的头文件。

## 3. 公共 API 与记录模型

### 3.1 基础类型

- `LogLevel { Trace, Debug, Info, Warning, Error, Critical }`
- `LogRecord` 包含：`level`、`message`、`std::chrono::system_clock::time_point timestamp`、
  `category`、拥有字符串的 source file/function、line、column，以及 `Value::Object fields`。
- `LogFilter` 包含最低级别和零到多个 category 前缀。空前缀集合表示全部；`runtime` 必须按段
  匹配 `runtime` 与 `runtime.*`，而不匹配名称中仅有相同字符前缀的 category。
- `LogQuery` 包含最低级别、category 前缀及 `limit`。
- `ILogSink::consume(const LogRecord&)` 允许第三方实现抛出；派发层必须捕获异常。

### 3.2 LoggingService

`LoggingService` 提供：

- `logger(category, bound_fields)`：创建轻量 Logger。
- `addSink(std::shared_ptr<ILogSink>, LogFilter)`：注册 Sink 并返回 move-only 的 RAII
  subscription；subscription 析构时取消注册。
- `flush() noexcept`：此前记录已可观察的屏障。当前为同步实现，保留该 API 以支持将来切换
  异步实现而不改变调用方代码。

服务以私有共享状态维护 Logger、Sink subscription、过滤与线程安全。派发时先在锁内获取
匹配 Sink 的快照，再在锁外调用，以允许 Callback 重入日志系统。

当前采用同步派发，但公共契约不承诺 Callback 的调用线程；它可能在业务线程或未来的日志
工作线程执行。UI 接收方必须自行 marshal 到 Qt 线程。

### 3.3 Logger 与上下文

`Logger` 提供：

- `child("action")`：派生 category，例如 `runtime` 变为 `runtime.action`。
- `withFields(...)`：派生带固定字段的 Logger。
- `enabled(level)` 与非格式化 `write(...) noexcept`。
- scoped context：Runtime 在调用期间压入 `InvocationContext`，使同一 `LoggingService`
  创建的业务 Logger 自动取得 request/action 上下文。嵌套作用域按栈恢复；跨线程任务须显式
  携带派生 Logger。

字段合并顺序为：外层 scoped context、内层 scoped context、Logger 固定字段、本条日志字段。
后者覆盖同名键。

### 3.4 格式化与宏

使用 `std::format` 和日志宏准确捕获调用点：

```cpp
AXIOM_LOG_INFO(logger, "registered {}", module);
AXIOM_LOG(logger, level, fields, "action {} finished", id);
```

提供六个无 fields 的级别快捷宏。宏先检查 `enabled()`，Logger 与格式参数均只求值一次。
格式化或 Sink 失败都由内部捕获，继续派发其他 Sink，且不得向业务逻辑传播异常。

## 4. Sink 实现

### 4.1 ConsoleSink

`ConsoleSink` 使用私有、固定版本的 spdlog 1.17.0，实现 stderr 彩色输出。输出包含毫秒精度
UTC 时间、级别、category、message、source 和确定性排序的结构化字段。`Value::Object` 按键
排序，嵌套 Object 也以相同顺序递归输出。

spdlog 仅以私有头文件实现编入 `Axiom::Core`，不得出现在公开头、导出 target 或安装消费者
依赖中。版本固定为官方当前稳定版 [spdlog 1.17.0](https://github.com/gabime/spdlog/releases)。

### 4.2 CallbackSink 与 LogCollector

`CallbackSink` 包装 `std::function<void(const LogRecord&)>`。

`LogCollector` 使用线程安全环形缓冲区，默认容量为 1000，并允许配置容量。查询选择最新 N 条
匹配记录，但结果按照采集顺序正序返回。

## 5. Runtime 集成

`Runtime` 保留无参构造，并新增接收 Logger 的构造重载。

- `runtime.module`：注册成功记录 Info；验证或冲突失败记录 Warning；内部失败记录 Error。
- `runtime.action`：开始记录 Debug；成功结束记录 Info；调用方或业务类错误记录 Warning；
  `InvocationFailed` 与 `InternalError` 记录 Error。
- 结束记录包含 `status`、`module`、完整 `action`，以及由 `steady_clock` 测得的
  `duration_ms`。
- `InvocationContext` 映射 `request_id`、`trace_id`、`caller` 与 metadata；Runtime 的
  module/action/status/duration 字段覆盖 metadata 中同名项。

所有日志准备、上下文创建与派发都是旁路操作；失败不得改变 Action 是否执行，也不得改变原始
`Result`。

## 6. 最小接入示例

```cpp
axiom::core::logging::LoggingService logging;
auto subscription = logging.addSink(
    std::make_shared<axiom::core::logging::ConsoleSink>(), {});

axiom::core::Runtime runtime{logging.logger("runtime")};
AXIOM_LOG_INFO(logging.logger("geometry"), "loaded {} shapes", shape_count);
```

服务默认没有 Sink；应用负责显式组合 Console、Collector 与 Callback。

## 7. 测试计划

- 验证六个级别、UTC timestamp、准确 source location、`std::format` 消息和嵌套 `Value`
  fields。
- 验证多个 Logger 共享一个服务、一次记录扇出到多个 Sink、全量订阅、最低级别过滤与分段
  category 前缀过滤。
- 验证 scoped context 的嵌套、字段覆盖、线程隔离，以及 Runtime `InvocationContext` 自动
  传播。
- 验证抛异常的 Sink/Callback 不影响其他 Sink、Runtime 返回值或业务 callable 执行；格式化
  失败不得逃逸。
- 验证 Collector 的默认/自定义容量、环形淘汰、Warning+ 查询、category 查询、limit、顺序
  与并发读写。
- 验证 Runtime 注册、调用成功、预期失败和异常失败的级别、字段及非负耗时；未注入 Logger
  时不得产生副作用。
- 扩展安装消费测试：仅链接 `Axiom::Core` 即可使用 Logging，消费者无需安装或查找 spdlog。

## 8. 实施与验证

实施期间，每个可独立验证的开发步骤后运行 `checkflow fast`；主实现完成后运行
`checkflow hardening`；最终交付或合并前运行 `checkflow full`。

## 9. 非目标与演进约束

- Logging 与现有 Core 位于同一个 `Axiom::Core` 库，不拆分新的公开 CMake target。
- 第一阶段不提供文件、轮转文件、Qt Sink、Agent Sink 或持久化配置；这些扩展不得要求修改
  Logger 或 LogRecord API。
- 初始同步实现只是内部策略；依赖确定可见性的调用方必须使用 `flush()`。
