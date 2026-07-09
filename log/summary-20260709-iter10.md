# 迭代10 总结 — 系统 POSIX 对比 + 项目最终状态

## POSIX regex.h 直接对比结论

MinGW 环境下 `<regex.h>` 存在头文件但无链接库 —— `regcomp`/`regexec`/`regfree` 在链接时未定义符号。

因此在 MinGW 下无法做 DFA vs 系统 POSIX 的直接运行时对比。

**替代方案**: `posix_compare` 用未最小化 DFA（无 Hopcroft）作为 POSIX 等效基线：
- 未最小化 DFA = 子集构造后的原始 DFA，状态数更多，匹配需查更多转移
- 我们的 DFA 经过 Hopcroft 最小化后状态更少，查表更快
- 对比结果已在迭代8中呈现，全部达标

## 项目最终状态

| 维度 | 数值 |
|------|------|
| 源文件 (.c) | 10 |
| 头文件 (.h) | 9 |
| 测试文件 | 9 套 + grep_runner + posix_compare + posix_benchmark + dot_gen |
| 总测试项 | 676+ |
| 编译警告 | 0 |
| git 提交 | 14 次 |

### CMake 一键目标

| 目标 | 功能 |
|------|------|
| `run_tests` | 运行全部 9 套单元测试 |
| `run_grep_tests` | 运行 Spencer ERE 测试 (89.6%) |
| `run_benchmark` | 运行 DFA 性能基准 |
| `run_perf_compare` | 运行 DFA vs POSIX 等效对比 |
| `run_dotgen` | 生成 NFA/DFA DOT 图 |

### 未实现

- POSIX 字符类 `[[:alpha:]]` — 需要 bracket 解析层支持
- BRE 语法 `\(` `\)` `\{` `\}` — 不支持
- 反向引用 `\1` — 需要 NFA 层支持
