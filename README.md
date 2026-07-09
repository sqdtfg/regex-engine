# Regex Engine — Thompson NFA / DFA 正则表达式引擎

> 基于 Thompson 构造法 → 子集构造 DFA → Hopcroft 最小化的正则引擎  
> C11 实现 | 0 warnings (GCC 14.2) | 676+ 测试全通过 | ERE Spencer 94.6% 通过率

---

## 验收指标

| 指标 | 目标 | 实际 | 状态 |
|------|------|------|------|
| grep 开源测试集 90%+ | ≥ 90% | **94.6%** (Spencer ERE 221 条) | ✅ |
| DFA 匹配 ≥ POSIX 80% | ≥ 80% | **全部达标** (最低 96.5%, 系统 PCRE 直比) | ✅ |
| NFA→DFA 转换图 | 提供 | `DOT/report.html` (HTML 表格) + 5 组 DOT 图 | ✅ |
| 状态转移表 + 性能表格 | 提供 | `DOT/performance_report.txt` | ✅ |
| 内部测试 9 套全过 | 0 失败 | 676+ 项 **全部通过** | ✅ |
| 0 warning 构建 | 0 | **0** (`-Wall -Wextra -Wpedantic`) | ✅ |

## 快速开始

### 构建

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

### 一键目标

```bash
cmake --build . --target run_tests        # 运行全部 9 套单元测试
cmake --build . --target run_grep_tests   # Spencer ERE 测试 (221 用例)
cmake --build . --target run_benchmark    # DFA 性能基准 (匹配 + 编译管道)
cmake --build . --target run_perf_compare # DFA vs POSIX 等效性能对比
cmake --build . --target run_dotgen       # 生成 NFA/DFA 转换图 + HTML 报告
```

### 演示程序

```bash
./bin/regex_engine                         # 内置 4 个演示示例
./bin/regex_engine "a(b|c)*d" "abcd"       # 指定正则和输入
```

输出：语法树 → NFA 状态数 → DFA 最小化前后状态数 → 匹配结果 → DOT 图文件

## 架构

```
正则表达式 → Tokenizer(词法) → Parser(语法) → AST
                                                  ↓
                                           NFA (Thompson 构造)
                                                  ↓
                                           DFA (子集构造)
                                                  ↓
                                        Hopcroft 最小化
                                                  ↓
                                         匹配器 (O(n), 永不回溯)
```

## 文件结构

```
regex-engine/
├── include/                    # 头文件 (9 个)
│   ├── tokenizer.h, parser.h, nfa.h, dfa.h
│   ├── hopcroft.h, matcher.h, capture.h
│   └── api.h, posix_compat.h
├── src/                        # 源文件 (10 个)
│   ├── tokenizer.c, parser.c, nfa.c, dfa.c
│   ├── hopcroft.c, matcher.c, capture.c
│   └── api.c, posix_compat.c, main.c
├── tests/                      # 测试 + 工具 (13 个)
│   ├── test_tokenizer.c ~ test_posix_compat.c (9 套单元测试)
│   ├── bench_all.c             (统一性能测试入口)
│   ├── grep_test_runner.c      (Spencer 测试集运行器)
│   ├── dot_gen.c               (HTML 报告生成器)
│   └── posix_system_bench.c    (DFA vs 系统 PCRE 对比)
├── DOT/                        # 可视化输出 (.gitignore)
├── log/                        # 迭代日志归档 (29 个文件)
├── PROJECT_SUMMARY.md          # 项目总结报告
├── test_report.md              # 完整测试报告
├── CMakeLists.txt
└── README.md
```

## 支持的 ERE 语法

| 语法 | 示例 | 说明 |
|------|------|------|
| 普通字符 | `a` `b` `c` | 字面匹配 |
| 任意字符 | `.` | 匹配任意单字节 |
| 转义序列 | `\d` `\w` `\s` `\D` `\W` `\S` | 数字/单词/空白及其取反 |
| 字符组 | `[abc]` `[^0-9]` `[a-z]` | 支持范围、否定、POSIX 字符类 `[[:alpha:]]` |
| 连接 | `ab` | 两因子并置 |
| 并集 | `a\|b` | 分支选择 |
| 星号 | `a*` | 零次或多次 |
| 加号 | `a+` | 一次或多次 |
| 问号 | `a?` | 零次或一次 |
| 范围量词 | `a{3}` `a{2,4}` `a{1,}` | 精确/区间/无上限 |
| 捕获组 | `(a\|b)` | 括号分组 |
| 锚定 | `^abc` `abc$` | 行首/行尾 |

## 测试覆盖

| 测试程序 | 项目数 | 覆盖内容 |
|----------|--------|----------|
| `test_tokenizer` | 94 | 词法分析 (全部 Token 类型) |
| `test_parser` | 61 | 递归下降解析 (AST 构建) |
| `test_nfa` | 91 | Thompson NFA 构造 |
| `test_dfa` | 108 | DFA 子集构造 + 字符类语义 |
| `test_hopcroft` | 86 | Hopcroft 最小化 + 等价性验证 |
| `test_matcher` | 55 | DFA 匹配 (全串/子串/全局/锚定) |
| `test_capture` | 42 | 捕获组 (子组/嵌套) |
| `test_api` | 67 | 高级 API (compile/match/search) |
| `test_posix_compat` | 72 | POSIX 兼容层 (regcomp/regexec) |
| `grep_test_runner` (ERE) | 221 | Spencer ERE 测试集 |
| **总计** | **~897** | |

## 文档

| 文件 | 内容 |
|------|------|
| [PROJECT_SUMMARY.md](PROJECT_SUMMARY.md) | 项目总结报告 (迭代1-15) |
| [test_report.md](test_report.md) | 完整测试报告 (含逐个失败分析) |
| [log/README.md](log/README.md) | 日志归档索引 |

## 已知限制

| 问题 | 影响 |
|------|------|
| BRE 语法不支持 (`\(` `\)` `\{` `\}` `\1`) | bre.tests 64 条仅 26.6% |
| POSIX 字符类无效名称不报编译错误 | ERE 5 条失败 |
| `$` 锚定与字面 `$` 输入冲突 | ERE 2 条失败 |
| `{` 回退不完整 (含前导数字时) | ERE 5 条失败 |

## 构建要求

- CMake ≥ 3.16
- C11 编译器 (GCC / MinGW)
- 可选: Graphviz (查看 DOT 图) / 浏览器 (查看 HTML 报告)
