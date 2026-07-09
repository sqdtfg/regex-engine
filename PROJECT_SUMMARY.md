# 正则表达式引擎 — 项目总结报告

> 基于 Thompson NFA → 子集构造 DFA → Hopcroft 最小化的正则表达式引擎
> 共计 **14 次迭代** (2026-07-05 ~ 07-09)，**16 次提交** 于 `history1` 分支

---

## 一、项目概况

| 维度 | 数值 |
|------|------|
| 源文件 | 10 `.c` + 9 `.h` |
| 测试文件 | 9 套单元测试 + 4 个工具 |
| 内部测试项 | **676+** (全部通过) |
| 编译警告 | **0** |
| 架构 | Tokenizer → Parser → NFA(Thompson) → DFA(子集构造) → Hopcroft最小化 → Matcher |

### 源文件清单

| 文件 | 功能 | 行数 |
|------|------|------|
| `src/tokenizer.c` | 词法分析 (正则→Token序列) | 278 |
| `src/parser.c` | 递归下降语法分析 (Token→AST) | ~420 |
| `src/nfa.c` | Thompson构造 (AST→ε-NFA) | ~690 |
| `src/dfa.c` | 子集构造法 (NFA→DFA) | ~400 |
| `src/hopcroft.c` | Hopcroft DFA最小化 | ~400 |
| `src/matcher.c` | DFA匹配器 (全串/子串/全局) | ~870 |
| `src/capture.c` | 捕获组支持 (子DFA方案) | ~410 |
| `src/api.c` | 高级API (compile/match/search) | ~225 |
| `src/posix_compat.c` | POSIX兼容层 (regcomp/regexec) | ~340 |
| `src/main.c` | 演示主程序 | ~177 |

---

## 二、验收指标达成情况

| 指标 | 目标 | 实际 | 状态 |
|------|------|------|------|
| **grep Spencer ERE 测试** | ≥90% | **94.6%** (221条, 计入预期失败) | ✅ |
| **DFA匹配 ≥ POSIX 80%** | ≥80% | **全部达标** (最低88.5%) | ✅ |
| **NFA→DFA 转换图** | 提供 | `DOT/` 5 组 DOT 图 + HTML 报告 | ✅ |
| **状态转移表** | 提供 | `DOT/performance_report.txt` | ✅ |
| **性能对比表格** | 提供 | `bench_all compare` 自动化 | ✅ |
| **内部测试** | 0 失败 | **676+ 项全通过** | ✅ |
| **0 warning** | 0 | **0** (GCC 14.2 -Wall -Wextra) | ✅ |

---

## 三、关键修复与改进

### 迭代1：Bug修复 + 锚定实现
- `dfa_match()` 最短匹配 → 最左最长匹配 (POSIX语义)
- `capture.c` `match_sub_dfa_greedy()` 增加 `out_start` 输出参数
- `posix_compat.c` 移除双重解析
- `regex_compile` 添加 `dfa_minimize` 调用
- 实现 `^` / `$` 锚定支持
- 合并 `dfa.h` / `matcher.h` 重复声明

### 迭代2-3：grep 测试兼容层 + ERE 修复
- Spencer `.tests` 格式测试运行器
- Tokenizer 裸 `{` 回退为字面字符 (ERE兼容)
- Parser 空括号 `()`、孤立 `)` 支持
- NFA 空 GROUP 用 ε 边

### 迭代5：嵌套括号回归修复
- `parse_atom` 不再将 `)` 当普通字符(仅 `parser_parse` 结尾容错)

### 迭代8：UTF-8 乱码修复 + POSIX 对比
- 全部文件 `SetConsoleOutputCP(CP_UTF8)`
- grep_test_runner 默认路径 + ERE 模式
- CMake 一键目标: `run_tests` / `run_grep_tests` / `run_benchmark` 等

### 迭代9：编译警告清零 (7→0)
- posix_compat.c `%s` 截断 → `%.240s`
- test_benchmark.c `snprintf` 重叠 → `memmove`
- test_posix_compat.c `%d`→`%lld`, 未使用变量删除

### 迭代11：POSIX 字符类 + 回溯NFA对比
- `dfa.c` bracket_matches 添加 `[[:alpha:]]` `[[:digit:]]` 等8种
- `posix_system_bench.c` DFA vs 回溯NFA(标准POSIX算法) 对比

### 迭代13：PCRE 原生 POSIX 对比 + BRE 测试
- 使用系统 `libpcreposix-0.dll` 的原生 `regcomp`/`regexec`/`regfree`
- 通过手动链接避免命名冲突
- BRE 测试支持 (26.6%，BRE语法 `\(` `\{` `\1` 不支持)

### 迭代14：HTML 全中文 + ERE 94.6%
- `dot_gen.c` HTML 报告所有字段改为中文
- 状态转移表清晰展示 `字符 → 目标状态`

### 迭代15：合并重复性能测试
- 删除 `posix_benchmark.c` / `posix_compare.c` / `test_benchmark.c`
- 合并为 `bench_all.c` (统一入口)

---

## 四、性能数据

### DFA 匹配吞吐量 (MB/s)

| 模式 | 1KB | 100KB | 1000KB | 状态数 |
|------|-----|-------|--------|--------|
| `a*` | 6,007 | 12,897 | 14,004 | 1 |
| `a+` | 3,680 | 11,595 | 12,754 | 2 |
| `[a-z]+` | 4,382 | 12,636 | 13,976 | 2 |
| `\d{3}-\d{4}` | 138 | 142 | 143 | 9 |
| `\w+@\w+\.\w+` | 54 | 54 | 54 | 6 |

### 编译管道 (μs, 10000次平均)

| 模式 | 词法 | 解析 | NFA→DFA | 最小化 |
|------|------|------|----------|--------|
| `a` | 0.0 | 0.2 | 28 | 84 |
| `\d{3}-\d{4}` | 0.2 | 0.8 | 178 | 440 |
| `\w+@\w+\.\w+` | 0.2 | 1.3 | 177 | 348 |

### DFA vs POSIX 等效对比

| 模式 | DFA (MB/s) | POSIX等效 (MB/s) | 比值 |
|------|-----------|-----------------|------|
| `a*` | 12,081 | 11,514 | 104.9% |
| `[a-z]+` | 11,933 | 12,564 | 95.0% |
| `\d{3}-\d{4}` | 127 | 144 | 88.5% |

---

## 五、NFA→DFA 状态压缩比

| 正则表达式 | NFA | DFA(前) | DFA(后) | 压缩比 |
|-----------|-----|---------|---------|--------|
| `a(b|c)*d` | 12 | 5 | 3 | 4.0x |
| `(a|b)*abb` | 14 | 5 | 4 | 3.5x |
| `\d{3}-\d{4}` | 16 | 9 | 9 | 1.8x |
| `\w+@\w+\.\w+` | 16 | 6 | 6 | 2.7x |
| `(ab|cd|ef)+` | 18 | 7 | 5 | 3.6x |
| `a?b?c?` | 12 | 4 | 2 | 6.0x |

---

## 六、已知限制

| 问题 | 影响范围 | 说明 |
|------|----------|------|
| POSIX 字符类无效名不报错 | 5个 ERE 测试 | `[[:alph:]]` `[[:notdef:]]` 等应编译失败但被接受 |
| `$` 锚定含字面 `$` 误判 | 2个 ERE 测试 | `a$` on `a$` 应不匹配但匹配了 `a` |
| `{` 回退不完整 | 5个 ERE 测试 | `a{1a}` `a{,2}` 等裸 `{` 处理不完善 |
| BRE 语法不支持 | bre.tests (64条) | `\(` `\)` `\{` `\}` 分组/量词、`\1` 反向引用 |
| `b{1000000000}` 崩溃 | 超大数量词 | 已加 >100000 防御但 `{m}` 格式仍可构造 |
| POSIX `REG_ICASE` 不支持 | 忽略大小写 | 标志被接受但不生效 |

---

## 七、log/ 归档说明

本文件是对 `log/` 目录下 **29 个** 日志文件的精炼总结，覆盖了从 2026-07-05 到 07-09 的全部开发过程。

log/ 目录结构：
- `summary-20260705-*.md` ~ `summary-20260706-*.md`：早期逐小时/跨夜进度 (17个)
- `summary-audit-*.md`：代码审计报告 (2个)
- `summary-20260709-iter*.md`：迭代开发日志 (10个，iter1~iter10)

以上日志文件均已归档，本 `PROJECT_SUMMARY.md` 为最终交付文档。
