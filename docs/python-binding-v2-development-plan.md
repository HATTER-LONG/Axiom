# Axiom Python Binding v2 后续开发计划

## 1. 结论

当前仓库已经完成 Python Binding 所依赖的大部分 Core 能力：

- `axiom::command::CommandDispatcher` 已提供九个封闭命令；
- `Value`、`Result<T>`、`Error`、`InvocationContext` 已构成语言无关动态边界；
- Descriptor、Error、Task 和 Snapshot 已在 Command 内转换为 `Value`；
- 静态库、共享库、安装后 CMake 消费者、导出符号和覆盖率完整性已有质量门；
- Core 当前不依赖 Python、pybind11 或其他协议实现。

因此，后续工作不是重新实现大纲中的 Command 层，也不是把 Runtime、Registry 或
Descriptor 对象图绑定给 Python。最小且正确的增量是：

```text
Application-owned Runtime / ResourceRegistry / TaskRegistry
                         |
                 revocable HostBridge
                         |
                  CommandDispatcher
                         |
              _axiom Python extension
                         |
                 axiom Python facade
```

首个必须解决的设计问题是宿主生命周期，而不是 Value 转换。当前
`CommandDispatcher` 对三个来源只持有非拥有引用，并明确要求来源比 Dispatcher 活得更久。
因此禁止把任意地址、裸指针或未经验证的 capsule 直接保存到 Python 对象中。推荐由独立的
Python Adapter 提供可撤销的 `HostBridge`/lease：应用一次性绑定同一组来源；Python Host
只持有受控 state；应用关闭 bridge 后，新调用稳定失败，进行中的调用先完成，随后销毁
Dispatcher。这个 bridge 不拥有或延长 Core 来源的生命周期，应用仍必须按声明顺序保证
bridge 先于来源析构。

## 2. 当前代码基线

### 2.1 已完成，不应重复开发

| 大纲能力 | 当前代码事实 | 后续处理 |
| --- | --- | --- |
| 九个 Command | `command_methods.hpp` 与 `CommandDispatcher` 已完整实现 | Python 只调用 `dispatch()` |
| Command 严格校验 | 已固定未知 method、缺失字段、未知字段、类型错误的顺序与 path | 复用并补 schema 契约测试 |
| InvocationContext 传播 | `dispatch(..., context)` 已原样传到 `Runtime::invoke()`，且有测试 | 只实现 Python 到 C++ 的字段转换 |
| Descriptor/Snapshot 编码 | `src/command/descriptor_conversion.*` 已完成 | 不在 Python 重复编码 |
| Runtime 异常归一化 | Runtime 已把 Action 标准/未知异常归一为 `Result` 错误 | Binding 仅处理边界自身抛出的异常 |
| 共享库能力 | `BUILD_SHARED_LIBS=ON`、导出宏、shared preset、安装消费者已存在 | 扩展 shared + extension 场景 |
| Core 安装包 | `Axiom::Axiom` 已导出，安装后消费者已验证 | 新增 wheel/import 消费者，不替换现有测试 |
| 质量流程 | `fast`、`hardening`、`full` 已存在 | 将 C++ bridge、Python 测试和 wheel 验证正式纳入 |

### 2.2 仍需完成

1. 移除 `src/command/command_validation.cpp` 对公开路径
   `axiom/action/detail/value_converter.hpp` 的依赖；
2. 把现有编码测试提升为独立、完整的 Command schema contract tests；
3. 定义 Python Adapter 的宿主接入、撤销、并发和 interpreter teardown 契约；
4. 实现严格的 `Python object <-> Value` 转换和 `Error -> AxiomError` 映射；
5. 实现最小 `_axiom` 扩展与纯 Python facade；
6. 建立 wheel、安装、重定位、共享库部署和 Python import 验证；
7. 将新增 C++/Python 源码纳入架构、静态分析、格式、测试和覆盖率门禁。

## 3. 对原开发大纲的修正

### 3.1 Phase 1 只剩两项增量

原大纲把 Command 技术债、Command schema 和 InvocationContext 当成待实现能力。按当前代码：

- `command -> action/detail` 依赖仍真实存在，必须清理；
- schema 已有实现和较多单元测试，但缺少一个集中、明确的契约夹具；
- InvocationContext 已实现并有端到端 C++ 测试，不应重新设计 Core API。

### 3.2 Shared Library 不是从零开始

当前项目默认构建静态库，但 `quality-shared` 已验证 DLL/SO、公开导出和安装消费者。
Python v2 应保留静态 C++ 构建能力，同时规定：

- `AXIOM_BUILD_PYTHON=ON` 时必须使用共享 `Axiom::Axiom`；
- 配置为 Python + static Core 时 CMake 直接给出清晰错误；
- `_axiom.pyd`/`_axiom.so` 只链接共享 `Axiom::Axiom`，不得把 Core 源码或静态 archive
  再编译/链接进扩展；
- wheel 内必须部署与扩展匹配的 Axiom DLL/SO，并设置正确的 Windows DLL 搜索方式或
  ELF/macOS loader-relative runtime path；不能依赖 build tree 或全局安装碰巧存在同名库。

### 3.3 `_attach()` 不是公共“万能指针入口”

当前代码没有 Application aggregate、共享所有权句柄或可检测裸对象析构的机制。仅检查 capsule
名称不能证明其中三个来源仍存活。推荐契约如下：

- Python 用户不直接调用 `_attach()`；
- C++ embedding 侧创建 adapter-owned `HostBridge`，构造时同时接收 Runtime、ResourceRegistry
  和 TaskRegistry，内部一次性构造对应 Dispatcher；
- 只有扩展自身创建的私有 `_HostHandle` 能附加到 Python `Host`；不支持任意 capsule、整数地址、
  `None`、单独来源对象或来源拼接；
- `_HostHandle` 持有 bridge state 的共享控制块，但不拥有 Core 来源；
- `HostBridge::close()` 和析构将 state 标为 closed，阻止新 dispatch，等待已有 dispatch lease
  退出，再销毁 Dispatcher；
- Python Host 在 closed/expired 状态上调用任何 API都抛出明确的 `AxiomHostClosedError`；
- 应用必须保证 `HostBridge` 在三个来源之前关闭/析构，且来源析构不与 dispatch 并发。

若首版没有真实的嵌入式 Application 调用方可以验证这套 C++ 接入 API，则不能用测试专用 capsule
替代设计完成度。应先交付一个最小 embedding consumer，再继续 facade。

### 3.4 Python 不拥有 Runtime

为方便测试而在 `_axiom` 中默认构造 Runtime/Registry/TaskRegistry，会改变原大纲明确排除的
所有权语义。生产 API 不提供默认 `Host()` 或 `create_runtime()`。测试夹具可以在测试扩展或
C++ integration executable 中拥有来源，但不得进入安装包的用户 API。

## 4. 目标目录与目标边界

建议结构：

```text
include/axiom/python/
    host_bridge.hpp          # 独立 Adapter 的 C++ embedding 契约，不进入 axiom.hpp
src/python/
    host_bridge.cpp          # 可撤销 state、dispatch lease、关闭同步
    module.cpp               # 唯一 pybind11 模块入口
    value_conversion.hpp
    value_conversion.cpp
    error_translation.hpp
    error_translation.cpp
python/
    pyproject.toml
    src/axiom/
        __init__.py
        _host.py
        actions.py
        resources.py
        tasks.py
        errors.py
        py.typed
    tests/
        ...
tests/python_embedding/
    ...                      # 真实 shared Core + extension + interpreter 消费者
```

若 `host_bridge.hpp` 必须包含 CPython 或 pybind11 类型，应把它留在单独的
`Axiom::PythonHost` target，而不是安装为 `Axiom::Axiom` 的公共头。首选设计是公共 embedding
契约只暴露 Adapter 自有类型，CPython/pybind11 细节留在 `.cpp`。

CMake target 方向必须保持：

```text
Axiom::Axiom <- Axiom::PythonHost <- _axiom
pybind11 ---------------------------^
Python facade ----------------------> _axiom
```

`Axiom::Axiom` 不链接 PythonHost 或 pybind11；`Axiom::PythonHost` 不访问任何模块的 private/detail
头，只使用公开 Command/Foundation 接口。

## 5. 分阶段实施计划

### Phase 0：冻结支持矩阵与验收夹具

目标：先确定能被持续验证的产品边界。

工作：

1. 明确首版 CPython 版本、Windows/Linux 架构和编译器 ABI 组合；macOS 若不在当前项目支持范围，
   明确列为后续而不是默认为已支持；
2. 增加 `AXIOM_BUILD_PYTHON`（默认 `OFF`），不改变现有 C++ 用户的默认构建；
3. Python 开启时使用 CMake `FindPython` 的 Interpreter/Development.Module 目标和
   `pybind11_add_module`；
4. 建立一个最小 embedding test：应用创建三个来源与 HostBridge，Python 只执行
   `system.snapshot`；
5. 配置阶段验证 Python ABI、构建配置和 shared Core 一致。

验收：Windows Release 的 `DLL + PYD + embedding consumer` 跑通；Linux 对应 job 同步建立，
不能只在本机静态测试中模拟。

补充（收口阶段落实）：

- **平台化 preset**：`quality-python`/`quality-wheel` 不得硬编码任何编译器；
  由当前平台工具链决定（Linux 默认 GNU driver，Windows 由 MSVC/clang-cl
  开发环境提供），避免 clang-cl 在 Linux 上连编译器探测都无法通过。
- **解释器版本选择**：CMake 在 `AXIOM_BUILD_PYTHON=ON` 时强制支持矩阵
  （Linux 3.12；Windows 3.12/3.13；其他平台配置报错），默认使用
  `uv sync --project python --python 3.12` 建立的固定 `python/.venv`，可用
  `Python_EXECUTABLE` 覆盖；工具版本锁定在 `python/uv.lock`。

### Phase 1：清理 Command 私有依赖并冻结 schema

目标：先让 Adapter 所依赖的动态协议成为稳定事实。

工作：

1. 将 Value path 拼接和通用 type-mismatch 构造移到 Foundation 的窄小 detail owner，或在
   Command 内保留小型私有实现；
2. 删除 Command 对 `axiom/action/detail/value_converter.hpp` 的 include；
3. 在架构规则中显式禁止 `src/command/` 包含 `axiom/action/detail/`；
4. 把九个命令的参数 schema、完整成功输出、可选字段省略规则、所有枚举字符串和 Error 四字段
   编码集中到 contract tests；
5. 保留现有验证顺序、排序语义和 Snapshot 非原子契约。

验收：`checkflow fast` 通过；contract tests 对整个对象做相等比较，避免只断言少数字段。

### Phase 2：实现可撤销 HostBridge

目标：在任何 Python 转换前证明生命周期边界安全。

工作：

1. 设计 adapter-owned shared state，包含 Dispatcher、closed 状态、活动调用计数与必要同步；
2. 构造时原子接收完整来源组合，不提供 partial attach；
3. dispatch 获取短期 lease；关闭阻止新 lease，并等待已有 lease 结束；
4. close 幂等；析构不传播异常；关闭后不再接触来源；
5. 私有 `_HostHandle` 校验模块身份、state 非空且未关闭；
6. 禁止 Python 对象直接访问 Runtime、Registry、IntrospectionService 或 Dispatcher 地址。

测试：空/错误 handle、错误 Python 类型、重复 attach、重复 close、closed state、并发
dispatch/close、多 Host、Python GC、bridge 先关闭、来源按合法顺序销毁。

验收：所有非法状态在 attach 或调用入口变成确定异常；ASan/UBSan 下无 use-after-free；
**异常路径同样释放 lease**——dispatch 内部使用 RAII `DispatchLease`，Action 抛出
`std::bad_alloc` 或任何异常后，另一线程的 `close()` 必须在确定的同步超时内完成，
不允许任何依赖 sleep 的时序验证。

### Phase 3：实现动态值转换

目标：转换层只负责 Python object 与 `Value` 的一一映射。

规则：

- `None/bool/int/float/str/list/dict[str, ...]` 对应现有七种 `Value`；
- 先判断 `bool` 再判断 `int`，避免 Python 的 `bool` 被当成整数；
- Python `int` 必须落入 `int64_t`，否则抛带当前 path 的转换错误，不截断；
- dict key 必须是实际 `str`，不调用 `str(key)`；
- 不接受 tuple、set、bytes、Decimal、PathLike、自定义 mapping/sequence 或任意 `__int__`/
  `__float__` 隐式转换；
- 所有嵌套错误使用与 Core 一致的对象/数组 path 语法；
- 检测递归容器或设置明确深度上限，避免自引用 list/dict 导致栈溢出；
- C++ 转 Python 返回全新 list/dict，不暴露 `Value` 内部引用。

测试覆盖七种类型、空容器、Unicode、`INT64_MIN/MAX`、两侧越界、非字符串 key、不支持类型、
多层 path、自引用容器和任意合法嵌套 round-trip。

验收：转换代码不做 Command/Action 业务校验；失败不会调用 Dispatcher。

### Phase 4：Error 与异常边界

目标：Core 失败完整且稳定地映射为 Python 异常。

工作：

1. 提供 `AxiomError`，实例属性为 `code: str`、`message: str`、`path: str | None`、
   `details: object | None`；
2. `code` 使用 Command 已定义的小写稳定字符串，不在 Python 重建独立 enum 权威来源；
3. 可按需要提供 `AxiomHostClosedError` 和 `AxiomConversionError` 子类，但 Core
   `Result<Value>` 的业务失败统一映射为 `AxiomError`；
4. `std::bad_alloc` 映射 `MemoryError`；Binding 参数错误映射 `TypeError`/`ValueError`；
   其他标准或未知 C++ 异常在模块顶层截获并映射为不泄露实现细节的 `RuntimeError`；
5. 不把 Core `Result` failure 再包装成成功 dict，也不吞掉 `details`。

验收：UnknownCommand、MissingArgument、UnknownArgument、TypeMismatch、InvalidArgument、NotFound、
Action business failure 和 invocation failure 的四字段均有 Python 断言。

### Phase 5：最小 `_axiom` dispatch Binding

目标：C++ 扩展只有一个稳定能力入口。

建议内部形态：

```python
_Host.dispatch(method: str, params: dict[str, object], context: dict[str, object] | None = None)
```

要求：

- `method` 只接受 `str`，`params` 只接受 dict；缺省 params 是否允许必须固定，首版建议要求显式
  dict 以保持 Command 边界清楚；
- context 只接受 `request_id`、`trace_id`、`caller`、`metadata`；未知字段、错误类型、非字符串
  metadata key/value 在调用 Core 前拒绝，并给出 context path；
- 不自动生成或改写 request/trace/caller；缺省值沿用 `InvocationContext{}`；
- 只有 Action 执行阶段在 Core 契约允许时释放 GIL；Python object 转换、异常创建和 Host state
  操作期间持有 GIL；
- 释放 GIL 前把所有 Python 数据转换为拥有型 C++ 值，恢复 GIL 后再创建 Python 结果；
- 不绑定 Runtime、Registry、Descriptor、TaskHandle 或 `Value` 类。

验收：九个命令均从 Python 经过同一 dispatch；上下文字段到测试 Action 完整一致。

### Phase 6：纯 Python facade 与类型信息

目标：在不扩大 C++ ABI 的前提下提供易用 API。

工作：

1. `Host.actions/resources/tasks` 返回只读 facade；`Host.snapshot()` 直接 dispatch；
2. facade 只组装 method/params/context，不复制校验、过滤、descriptor 或 Error 语义；
3. 导出 `AxiomError`、Host 和必要的 `TypedDict`/Protocol 类型；包含 `py.typed`；
4. 首版严格限制为 list/describe/invoke、list/describe、list/describe/cancel、snapshot；
5. 不增加 asyncio、Python Action 注册、Task submission/result、Resource native binding。

验收：公共 API 测试与直接 dispatch 测试使用同一底层 fixture；`python -m pytest` 与静态类型检查
均通过。

### Phase 7：打包、安装与重定位

目标：从干净环境安装后可直接使用，而不是只能从源码/build tree import。

工作：

1. 使用标准 `pyproject.toml`，CMake 项目采用 `scikit-build-core` backend；不新增命令式
   `setup.py`；
2. CMake 安装 `_axiom` 到 `axiom` package，并把匹配的 Axiom shared library 部署到 wheel；
3. 修正 Windows DLL 搜索与 Unix loader-relative rpath；
4. 构建 wheel，在全新 venv 中安装，移动/清理原 build tree 后执行 import 与九命令 smoke test；
5. 检查 wheel 不包含源目录绝对路径、私有头、pybind11 headers、测试夹具或重复 Core 库；
6. 明确 wheel 是 CPython/平台/架构相关制品；发布矩阵之外从源码构建的行为单独记录。

验收：`python -c "import axiom"`、embedding consumer 和 facade integration tests 均只依赖已安装
wheel；Windows 与 Linux 分别验证。

补充（收口阶段落实）：

- **临时 wheel consumer**：验证只写临时目录与临时 venv，不向执行 CTest 的
  解释器、全局环境或 `python/dist-wheel` 安装；构建中的 CMake wheel build
  tree 在 import 前被移出原位；消费者现场编译，源码头文件可用，但链接与
  运行时库必须解析到 venv site-packages 中 wheel 安装的
  `Axiom`/`PythonHost` 共享库（Linux 以 `ldd` 断言）。

### Phase 8：质量门与文档收口

目标：Python Adapter 成为正式工程组成，而不是例外。

工作：

1. architecture 规则增加 Core 禁止依赖 `axiom/python`、PythonHost、Python/pybind11 头；Python
   Adapter 禁止 include 各模块 private/detail；
2. C++ bridge/converter 进入 clang-format、clang-tidy、cppcheck、复杂度和 LLVM coverage inventory；
3. Python 源码进入 formatter/linter/type-check/pytest/coverage；
4. `fast` 运行聚焦的 C++ 与 Python 测试；`full` 增加 shared、wheel、安装、重定位和 import；
   `hardening` 覆盖 HostBridge 生命周期与并发；
5. mutation testing 是否覆盖 CPython glue 需基于工具实测。若暂不适用，只排除不可变的绑定样板，
   转换和 HostBridge 逻辑不得排除，并在质量文档记录依据；
6. 更新 README、architecture、Python usage/build/install、宿主接入和 lifetime 文档。

验收：`checkflow fast`、`checkflow hardening`、`checkflow full` 实际运行并通过；不得通过降低现有
90% coverage、70% mutation 阈值或新增宽泛排除来达成。

补充（收口阶段落实）：

- **目标专用编译数据库**：`full` 的普通 clang-tidy/cppcheck 只检查
  `quality-full` 实际编译的目标（不含 `src/python/**`、
  `include/axiom/python/**`、`tests/python_embedding/**`）；绑定源码的
  clang-tidy/cppcheck 在 `quality-wheel` 成功构建后，用该构建的
  `compile_commands.json` 执行，保证 pybind11 include 路径真实存在，
  不再出现 “pybind11/pybind11.h file not found” 之类的伪缺失。
- **Python 测试与 Core 门禁拆分**：`quality-python`/`quality-wheel` 只构建
  Adapter 与 embedding consumer，不下载 googletest、不编译 `axiom_test`。
  `checkflow fast` 在 Core 覆盖率之后执行 `uv sync --project python --frozen`，
  再用 `ctest -R '^axiom\\.python_'` 跑 embedding/ruff/mypy/facade；HostBridge
  的 C++ 生命周期测试留在 `quality-fast`/`hardening`/`full`。

## 6. 测试矩阵

| 层次 | 重点 | 必须验证的失败路径 |
| --- | --- | --- |
| Command contract | 九命令参数与输出 schema | enum/string、optional omission、Error details、非法内部 shape |
| HostBridge C++ | close、lease、并发与析构顺序 | empty/wrong/closed handle、dispatch 与 close 竞争 |
| Value converter | 七种类型与嵌套 | int64 越界、非字符串 key、自引用、不支持类型、精确 path |
| Extension | dispatch、context、异常翻译 | 未知 context 字段、Core failure、bad_alloc/标准/未知异常 |
| Facade | 方法到 Command 的精确映射 | 不增加字段、不改写 context、不吞 Error |
| Embedding | 真实 Application 来源接入 | bridge 关闭后仍持有 Python Host、interpreter teardown |
| Wheel consumer | 安装、重定位、import、shared loader | build tree 删除、缺失/错误 DLL/SO、Debug/Release 混用 |

所有生命周期并发测试使用 latch/barrier 等确定性同步，不使用 sleep 猜时序。

## 7. 提交拆分建议

每个提交保持可独立验证，推荐顺序：

1. `command` 私有依赖清理 + architecture rule + schema contract tests；
2. Python CMake option + 最小 shared embedding fixture；
3. HostBridge/lease + 生命周期测试；
4. Value converter + 边界测试；
5. Error translation + `_Host.dispatch()`；
6. 九命令 Python integration + InvocationContext 测试；
7. Python facade、类型信息与用户测试；
8. wheel/install/relocation/import 验证；
9. CheckFlow、coverage integrity 和文档收口。

不要把 Host 生命周期、转换、facade 和 packaging 堆进一个不可审查的大提交；也不要为每个
Python facade 文件创建新的 C++ 公共接口。

## 8. 风险与停止条件

- **真实宿主接入不明确**：没有生产侧创建/关闭 bridge 的调用路径时，停止在 Phase 0，不用测试
  capsule 假装完成 attach。
- **单进程双 Core**：发现 host 和 extension 各自静态链接 Axiom 时立即阻断构建，不接受“测试看似
  能跑”。
- **来源可能先于 bridge 析构**：这是当前非拥有 Core API 无法自行检测的宿主违约；必须通过
  RAII 声明顺序、显式 close 和 embedding tests 消除，不能承诺任意裸对象失效都可检测。
- **interpreter finalization**：不得在无 GIL 或 Python 已 finalizing 时创建 Python 对象、回调 Python
  或依赖 pybind11 静态析构顺序；若无法证明安全，bridge teardown 只执行纯 C++ 关闭。
- **wheel 动态库冲突**：必须验证 wheel 内库的定位和唯一性，避免加载系统中另一版本 Axiom。
- **跨平台范围膨胀**：先完成明确支持矩阵；新增 macOS、abi3、PyPy、Conda 特殊发布规则应独立
  评估。

## 9. 最终完成标准

Python Binding v2 只有在以下条件全部满足时才完成：

1. Python 能通过 facade 和直接 dispatch 使用现有九个 Command；
2. 所有能力只经过 `CommandDispatcher`，Python 不接触 Registry/Runtime 私有状态；
3. Python 与 `Value` 的转换严格、可递归、可定位错误且无危险隐式转换；
4. Core Error 的 code/message/path/details 完整保留；
5. InvocationContext 四字段原样到达 Action；
6. attach 只接受 adapter 创建的完整、有效 handle，closed/expired state 确定失败；
7. Host close、GC、并发 dispatch、Application shutdown 和 interpreter teardown 无 UB；
8. host 与 extension 在同一进程只使用一份 Axiom shared library；
9. Windows DLL + PYD 与 Linux SO + extension 的真实消费者通过；
10. 安装 wheel 并移除 build-tree 依赖后仍可 import 和执行；
11. Core 不依赖 Python/pybind11，Adapter 不依赖 Core private/detail；
12. 新增实现进入现有质量、覆盖率和安装门禁，所有要求的 Gate 实际通过。

## 10. 实施依据

- 仓库当前公共契约：`include/axiom/command/command_dispatcher.hpp`、
  `include/axiom/action/invocation_context.hpp`、`include/axiom/foundation/value.hpp`；
- 仓库当前构建与质量事实：`CMakeLists.txt`、`CMakePresets.json`、`checkflow.json`、
  `cmake/verify_coverage.cmake`；
- Python 包使用标准 `pyproject.toml` build-system/project metadata；
- CMake 扩展使用现代 FindPython 与 `pybind11_add_module`；
- CMake 原生扩展打包采用专门支持 CMake 的 build backend（推荐 `scikit-build-core`）。

