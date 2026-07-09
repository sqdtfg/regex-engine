# 迭代6 总结 — 性能基准 + NFA→DFA 转换图 + 状态转移表

## 修改的问题

1. **DFA 性能基准测试** (posix_benchmark.c)
   - 测量 5 种典型模式在 1KB～1000KB 输入下的匹配吞吐量
   - 测量编译管道（词法→解析→NFA→DFA→最小化）各阶段耗时
   - 所有输出使用 UTF-8 中文

2. **NFA→DFA 转换图和状态转移表** (dot_gen.c)
   - 自动生成 5 组示例的 NFA/DFA DOT 文件
   - 文本格式状态转移表（最小化前后对比）
   - 性能对比报告 (performance_report.txt)

3. **CMake 一键目标**
   - `run_tests` — 运行 9 套单元测试
   - `run_grep_tests` — 运行 Spencer ERE 测试
   - `run_benchmark` — 运行性能基准
   - `run_dotgen` — 生成 NFA/DFA 图和表
   - `run_perf_compare` — DFA vs POSIX 性能对比

## 解决的问题

- 不再需要手动传参数运行测试/基准
- NFA→DFA 转换图可直接在 Graphviz 查看
- 状态转移表可以提交到文档

## 下一步计划

- 迭代7：注释全部改为中文 + 修复乱码 + POSIX 对比基准
