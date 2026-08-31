# Task Runtime 交付记录

本次实现依据 [开发计划](task-runtime-plan.md) 和 `agile-delivery` 技能。
开发分支为 `codex/task-runtime`，原始基线为 `a72e326`。

## 范围与任务边界

1. 核心实现：拥有 `axiom::task` 的身份、执行状态、结果、Registry、通知和日志。
   验收以计划中的公共契约和确定性 GoogleTest 为准，不修改 Action 调用方式，
   不增加线程池或调度抽象。由一个 5.6 terra 实现 agent 在隔离 worktree 完成，
   各增量必须经过 `fast` 后提交。剩余阶段另设一个 5.6 terra 测试任务，
   仅在独立文件补公开并发与生命周期回归，与现有代码质量修复隔离。
2. 交付集成：依赖核心公共接口，补充文档、长任务示例、安装消费者和必要构建修复。
   保持现有 `Axiom::Axiom` 安装目标、静态默认值和平台边界；由 Primary 完成。
3. 独立 review：依赖已提交的集成结果及通过的 `hardening`。
   只检查明确提交范围；语义修复后重新验证。

## 验证记录

| 检查 | 状态 | 说明 |
| --- | --- | --- |
| 原始基线 `fast` | 通过 | 行 93.4%、区域 94.6%、分支 90.4%。 |
| CMake 嵌入契约 | 通过 | 宿主分析器配置保留，不添加应用或测试目标。 |
| 最终集成 `fast` | 通过 | 已整合 `codex/task-old-tests` 与 `codex/task-runtime-tests`；行 93.3%、区域 94.7%、分支 90.3%。 |
| 最终集成 `hardening` | 通过（Windows） | ASan 单测通过。Mull 在 Windows 上跳过，不能作为变异测试通过。 |
| 独立 review | 待执行 | 必须先通过完整 `hardening`。 |
| 最终 `full` | 待执行 | 包含 Windows 静态/共享库与安装消费者。 |
| Windows 静态安装消费者 | 通过 | 包含取消后立即读取结果、Resource Handle 结果寿命检查。 |
| Windows 共享安装消费者 | 通过 | 同样的消费者已在 DLL 安装包通过；全部共享单测仍待最终门禁。 |
| Linux / macOS 构建与消费者 | 未执行 | 当前只有 Windows 环境。 |

Windows 的 CheckFlow doctor 明确报告 Mull 不受支持。本机未安装 WSL Linux
发行版；doctor 的可用性结果不能作为 mutation 测试通过的证据。未通过全部必需
门禁前，不将本记录视为完成验收。

早期报告保存在 `build-quality/reports/task-integration-full.json`、
`task-integration-hardening.json` 与 `task-repair-1-full.json`。这些是修复诊断，
不能替代最终验证。旧 worktree 已在提交集成后移除，必要报告另行保留。
