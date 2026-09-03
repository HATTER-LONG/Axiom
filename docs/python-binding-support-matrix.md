# Axiom Python Binding v2 支持矩阵

本文冻结 Python Binding v2 首个版本的平台、解释器与 ABI 边界。矩阵之外的
组合不在首版验收范围内，扩展需单独评估（见
`docs/python-binding-v2-development-plan.md` 第 8 节风险）。

## 1. 首版支持矩阵

| 平台 | 架构 | CPython | C++ 编译器 / ABI | Core 链接 | 产物 |
| --- | --- | --- | --- | --- | --- |
| Windows | x86_64 | 3.12、3.13 | clang-cl（MSVC ABI）或 MSVC，Release | 共享 `Axiom::Axiom` | `Axiom.dll` + `_axiom.cp31X-win_amd64.pyd` |
| Linux | x86_64 | 3.12 | Clang 或 GCC（libstdc++ ABI），Release | 共享 `Axiom::Axiom` | `libAxiom.so` + `_axiom.cpython-312-x86_64-linux-gnu.so` |

规则：

- host 应用程序与 `_axiom` 扩展在同一进程内必须链接同一份共享
  `Axiom::Axiom`；扩展不得再次编译或静态链接 Core 源码/archive。
- 整个构建（Core、PythonHost、扩展、embedding consumer）必须使用同一
  C++ 编译器 ABI。Windows 上 CPython 官方发行版使用 MSVC ABI，因此
  GNU-driver clang 构建的扩展不可加载。
- `_axiom` 是 CPython/平台/架构相关制品；它不提供 abi3、PyPy 或 Conda
  兼容性。
- **编译器选择由平台工具链决定**：`quality-python`/`quality-wheel` preset
  不硬编码任何编译器。Linux 使用默认 CMake 编译器（GNU driver 的 Clang 或
  GCC）；Windows 必须在 MSVC Developer Prompt 或显式 clang-cl 环境中配置，
  由环境变量或 `-DCMAKE_CXX_COMPILER` 指定。
- **解释器契约由 CMake 强制**：`AXIOM_BUILD_PYTHON=ON` 时，Linux 仅接受
  CPython 3.12，Windows 接受 3.12/3.13，其他平台或版本在配置阶段直接报错。
  默认优先使用 `uv sync --project python --python 3.12 --frozen` 建立的固定
  `python/.venv`（工具版本锁定在 `python/uv.lock`）；也可用
  `Python_EXECUTABLE` 显式指向任意受支持解释器。未找到该环境、或缓存仍指向
  Windows Store/`pythoncore` 解释器时，配置改用 venv 并丢弃过期的
  FindPython Development 缓存，不会静默链接错误的 `python3XX.dll`。

## 2. 明确不支持

- **macOS**：不在首版支持范围，后续单独评估。
- **abi3 稳定 ABI、PyPy、Conda 专有打包**：独立评估，不与本矩阵合并。
- **静态 Core + Python**：`AXIOM_BUILD_PYTHON=ON` 时 CMake 直接报错。
- **Debug/Release 混用**：wheel 内的 Core 共享库必须与扩展同一配置。

## 3. 验证现状

- Windows x64（CPython 3.12/3.13，clang-cl Release）：本机验证（早期
  clang-cl preset 时期执行；preset 平台化改造后未重新执行，重新验证前不
  作为当前 preset 行为的依据）。
- Linux x64（CPython 3.12，`python/.venv` 固定环境）：本机实际执行并
  通过：`quality-python` 与 `quality-wheel` 的 configure/build/ctest
  （含隔离式 wheel 消费者）、`checkflow fast`、`checkflow hardening`、
  `checkflow full`，以及以 Python 编译数据库为准的 `src/python`、
  `include/axiom/python`、`tests/python_embedding` clang-tidy/cppcheck。
- 仓库暂无 CI；上表只记录已实际执行的验证，未跑的矩阵组合不得宣称通过。

## 4. 覆盖率清单边界

pybind11 绑定源码（`src/python/module.cpp`、`value_conversion.cpp`、
`error_translation.cpp`）只在 `AXIOM_BUILD_PYTHON` 构建中编译。静态覆盖率
构建通过 `verify_coverage.cmake` 的显式清单排除这些源，且排除项必须指向
真实存在的 `src/` 文件；其余全部源码仍必须出现在覆盖率清单中。转换与
HostBridge 逻辑进入覆盖率的正式方案在质量收口阶段按工具实测处理，不通过
沉默排除掩盖缺口。

Mull 变异测试在 Windows 上被 CheckFlow 工具层整体判定不支持并跳过（工具
平台限制，不是项目排除规则）。`host_bridge.cpp` 已进入 mutation 构建并
落在 `mull.yml` 的 includePaths 内，因此在支持 Mull 的平台（Linux）上
`checkflow hardening` 会对 HostBridge 实现产生变异并要求其测试通过；绑定
转换源因不在 mutation 编译集中而豁免。Linux 上的 `checkflow hardening`
（含 Mull）已实际执行并通过。