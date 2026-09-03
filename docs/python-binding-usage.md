# Axiom Python Binding v2 使用说明

本文描述宿主应用程序接入、Python API、构建、打包与安装约束。支持矩阵见
`docs/python-binding-support-matrix.md`；开发计划与验收标准见
`docs/python-binding-v2-development-plan.md`。

## 1. 宿主生命周期契约

Python 不拥有 Runtime、ResourceRegistry 或 TaskRegistry。宿主应用程序
（C++ embedding）一次性构造 `axiom::python::HostBridge`，通过 `attach()`
获得唯一的 `HostHandle` 交给 Python：

```cpp
Runtime runtime;                                    // 宿主拥有来源
resource::ResourceRegistry resources;
task::TaskRegistry tasks;
{
    axiom::python::HostBridge bridge{runtime, resources, tasks};
    // bridge.attach() 通过 pybind cast 传给 Python：
    // axiom.Host._attach(handle) 返回 facade Host
    // ...
    bridge.close();                                 // 可早于 Python 对象销毁
}                                                   // 来源仍存活
```

规则：

- **一次性原子绑定**：构造同时接收三个来源，没有 partial attach；不接受
  地址、裸指针、外部 capsule、`None` 或来源拼接。
- **Python 只拿到私有 `_HostHandle`**：它共享 bridge 的 control block，但不
  拥有 Core 来源，也访问不到 Runtime、Registry、IntrospectionService 或
  Dispatcher 地址。
- **close 幂等**：正常调用返回 `true`，关闭后新 dispatch 稳定抛
  `AxiomHostClosedError`；已在进行的 dispatch 先执行完，随后内部
  CommandDispatcher 销毁，之后不再接触来源。如果 Action 在同一 bridge 的
  dispatch 内调用 `close()`，它返回 `false` 而不等待自身 lease；调用方必须在
  dispatch 返回后再关闭。每次 dispatch 持有内部 RAII lease：无论正常返回还是
  异常退出，lease 都归还活动计数。
- **生命周期义务**：来源必须活到 `close()`（或 bridge 析构）完成，且来源
  析构不与任何 dispatch 并发。handle 可以在 bridge 关闭后继续存在，但只能
  稳定返回 `AxiomHostClosedError`，不再接触来源；这是非拥有式 Core API 的
  宿主声明义务，bridge 无法自行检测来源死亡。
- **interpreter finalization**：应用必须在 Python finalize 之前或之后用纯
  C++ 路径执行 close；任何 Python 对象创建/回调都不得发生在解释器已
  finalizing 或已 finalize 之后。

## 2. Python API

```python
from axiom import Host, AxiomError, AxiomHostClosedError, AxiomConversionError

host = Host._attach(handle)              # 仅宿主应用调用
host.dispatch(method, params, context)   # 唯一稳定入口
host.actions.list(module=..., tags=...)
host.actions.describe("ns.action")
host.actions.invoke("ns.action", {...}, context={...})
host.resources.list(type=...)
host.resources.describe("widget:1")
host.tasks.list(state=..., origin_action=..., origin_request=...)
host.tasks.describe("task:1")
host.tasks.cancel("task:1")
host.snapshot()
```

约束：

- `method` 必须是实际 `str`，`params` 必须是实际 `dict`；缺省不提供 params 的
  隐式构造。
- `context` 只接受 `request_id`、`trace_id`、`caller`、`metadata`
  （`dict[str, str]`）；未知字段、错误类型、非字符串 metadata 键值在进入
  Core 之前以 `AxiomConversionError` 拒绝并给出 `context.*` path。
- Python 精确的 `None/bool/int/float/str/list/dict[str, ...]` 一一映射七种 `Value`；
  `bool` 先于 `int` 判断；`int` 必须落入 `int64_t`；dict key 必须是真实
  `str`；不接受 tuple/set/bytes/Decimal/path-like/自定义 mapping 与隐式
  `__int__`/`__float__`；嵌套失败携带 Core 一致的 path 语法；自引用容器由
  深度上限截断。
- Core `Result` 业务失败统一为 `AxiomError`，字段为
  `code/message/path/details`；`code` 使用 Command 公开契约
  `axiom::command::errorCodeName()` 定义的小写稳定字符串（如
  `type_mismatch`），不吞 `details`。
- facade 只组装 method/params/context；不重复校验、descriptor 或 Error
  语义。

## 3. 构建

### 3.1 准备固定的开发环境

Python 门禁不依赖用户全局解释器，也不使用 Windows Store 解释器。用
[uv](https://docs.astral.sh/uv/) 同步锁定的开发环境（默认 CPython 3.12）：

```sh
uv sync --project python --python 3.12 --frozen
# 或：
python tools/create_python_venv.py
```

`uv` 会按 `python/.python-version` 与 `python/uv.lock` 创建
`python/.venv`，并安装 `pyproject.toml` `[dependency-groups] dev` 中的
pybind11、pytest、ruff、mypy、scikit-build-core 与 ninja。CMake 在
`AXIOM_BUILD_PYTHON=ON` 且未显式设置 `Python_EXECUTABLE` 时必须使用该
环境；缺失时配置直接失败，而不会回退到系统 Python。解释器一旦更换，CMake
会丢弃过期的 FindPython Development 缓存，避免仍链接旧的 `python3XX.dll`。
Windows 上 embedding 测试通过 `AXIOM_PYTHON_HOME`（`sys.base_prefix`）与
`PYTHONPATH`（venv `purelib`）初始化解释器，且不会把 `python3XX.dll` 复制到
宿主可执行文件旁边。离线环境需提前准备该 venv 以及 CPM 依赖缓存
（`~/.cache/CPM` 或 `CPM_SOURCE_CACHE`）。

### 3.2 配置与构建

```sh
cmake --preset quality-python        # 共享 Core + Axiom::PythonHost + _axiom
cmake --build --preset quality-python
ctest --preset quality-python --output-on-failure
```

- preset 不硬编码编译器：**Linux** 使用默认 CMake 编译器（GNU driver 的
  Clang 或 GCC）；**Windows** 在 MSVC Developer Prompt 或 clang-cl 环境中
  配置（必要时显式 `-DCMAKE_CXX_COMPILER=clang-cl`），保证 MSVC ABI 与
  CPython 官方发行版一致。
- 解释器解析顺序：显式 `Python_EXECUTABLE` > `python/.venv`（uv sync
  的固定环境）。未找到 venv 时配置失败。CMake 强制支持矩阵：Linux 仅
  CPython 3.12，Windows 3.12/3.13，其他组合在配置阶段报错。解释器变更时
  会清空 FindPython 的 Development 缓存，避免 Windows 上仍部署旧的
  `python3XX.dll`。
- `checkflow fast` 先跑 `quality-fast` 的 Core GoogleTest，再 `uv sync`
  并构建 `quality-python`；该 preset 不编译 googletest/`axiom_test`，CTest
  只匹配 `axiom.python_*`（embedding、ruff/mypy、facade）。
- `AXIOM_BUILD_PYTHON=ON` 要求 `BUILD_SHARED_LIBS=ON`（静态组合直接
  CMake 报错），并要求无 coverage/sanitizer/mutation instrumentation。
- `_axiom` 只链接共享 `Axiom::Axiom` 与 `Axiom::PythonHost`；不会再次编译
  Core 源码或静态 archive；host 与 extension 在进程内共享同一份 Core。
- pybind11 优先从所选解释器可导入的安装（`pybind11.get_cmake_dir()`）解析，
  网络可达时回退到 CPM 下载（版本与 `python/pyproject.toml` 的 dev 组保持一致）。
- Visual Studio 构建使用 `/utf-8`，并采用 DLL 导出异常继承 std::logic_error
  的标准 C4275 处置。

## 4. Wheel 打包、安装与重定位

wheel 使用标准 `pyproject.toml` 与 scikit-build-core（无 setup.py）：

```sh
python -m pip wheel python/ -w <wheelhouse>
python -m pip install <wheelhouse>/axiom-*.whl
```

- wheel 内 `_axiom` 安装在 `axiom/` 包目录，匹配的
  `Axiom.dll`（Windows）/`libAxiom.so`（Linux，`$ORIGIN` rpath）与其相邻；
  Windows 下 CPython 3.8+ 的模块导入 DLL 搜索保证不会加载系统中另一版本
  Core。
- `quality-wheel` 的 `axiom.python_wheel` 测试（`tools/verify_python_wheel.py`）
  执行隔离式验证，全部写入随测试销毁的临时目录：
  - 在临时 wheelhouse 构建 wheel（临时 scikit-build 目录，不写
    `python/dist-wheel`），并检查制品：扩展、带 SOVERSION 的共享
    `libaxiom`/`libaxiom_python_host`（Windows 为 DLL/导入库）、无源码/
    头文件/测试/静态 archive/重复 Core；wheel 的 CPython 与平台 tag 必须
    与支持矩阵一致；
  - 安装到临时 venv，不向执行测试的解释器或任何全局环境安装；
  - 在 import 前把 CMake wheel build tree 移出原位，消除任何隐式回退；
  - 现场编译独立消费者（`tests/python_embedding/wheel_consumer`）：使用
    源码头文件，但链接与运行时库全部来自已安装 wheel 的包目录（Linux 用
    RPATH，Windows 用包目录 DLL 搜索路径），并用 `ldd` 断言解析结果落在
    wheel 包目录内；
  - 消费者驱动 dispatch、closed-host 与 facade 三套 pytest，全部只依赖
    已安装 wheel。
- wheel 是 CPython/平台/架构相关制品（如 `cp312-linux_x86_64`、
  `cp313-win_amd64`）；矩阵之外的组合从源码构建，行为单独评估。

## 5. 异常与 GIL

- 转换、异常创建、Host state 操作与当前的 Command dispatch 均持有 GIL。
  `CommandDispatcher` 尚未公开只执行 Action 的边界，因此 adapter 不会在
  schema 校验或 Host lease 期间释放 GIL。
- `std::bad_alloc` 映射 `MemoryError`；绑定参数错误映射 TypeError/ValueError
  （`AxiomConversionError` 是 `ValueError` 子类）；其他标准或未知 C++ 异常在
  模块顶层截获为不泄露实现细节的 `RuntimeError`。

## 6. 质量门

- `checkflow fast`：C++ 门后先 `uv sync --project python --frozen`，再构建
  `quality-python` 并只运行 `axiom.python_*`（embedding、ruff、mypy、
  facade 90% 覆盖率）。Core GoogleTest 不在此步骤重复。
- `checkflow full`：普通 clang-tidy/cppcheck 只扫描 `quality-full` 实际编译
  的 Core、应用与常规测试源码（不含 `src/python/**`、
  `include/axiom/python/**`、`tests/python_embedding/**`）；追加
  `quality-wheel` 构建/测试（含隔离式 wheel 验证），并随后用
  `build-quality/wheel` 的编译数据库对绑定源码与 embedding consumer 执行
  cppcheck、clang-tidy（`warnings_as_errors: "*"`），保证 pybind11 include
  路径真实存在。
- `checkflow hardening`：HostBridge 生命周期与并发测试在 ASan/UBSan 下运行；
  包含 Core 将 Action 异常归一为 Result 后 close 仍及时完成，以及 lease
  持有期间意外异常展开后 close 不卡死的回归。测试专用 fault seam 仅在
  `AXIOM_ENABLE_TEST_SEAMS=ON` 的测试构建中编译（fast/full/hardening），
  不进入安装包或 wheel。Mull 对 HostBridge 实现产生变异并要求覆盖，
  pybind11 绑定样板因不在 mutation 编译集中而豁免，依据记录在
  `docs/python-binding-support-matrix.md`。
- 静态覆盖率清单经由 `verify_coverage.cmake` 的显式清单排除仅在 Python
  构建编译的绑定源码；其余源码仍必须全部出现在覆盖率清单中。
