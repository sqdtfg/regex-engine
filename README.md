# Regex Engine — 正则表达式引擎

Thompson 构造 + 子集构造 + Hopcroft 最小化的 C11 正则引擎，支持 NFA/DFA 双模式匹配。

## 核心算法

| 阶段       | 算法            | 说明                                                    |
| ---------- | --------------- | ------------------------------------------------------- |
| 词法分析   | 手动词法扫描    | 支持转义`\d\w\s`、字符组 `[a-z]`、量词 `{m,n}` 等 |
| 语法解析   | 递归下降        | 生成 AST，正确处理器词 > 连接 > 并集优先级              |
| NFA 构造   | Thompson 构造法 | 从 AST 递归构建 epsilon-NFA，每个状态最多两条出边       |
| DFA 构造   | 子集构造法      | epsilon-closure + move + 去重，BFS 线性遍历             |
| DFA 最小化 | Hopcroft 算法   | O(kn log n)，合并等价状态                               |
| 匹配       | DFA 状态机      | O(n) 单字符匹配，永不回溯                               |
| 可视化     | Graphviz DOT    | 自动生成 NFA/DFA 状态转移图到`DOT/` 目录              |

## 项目结构

```
regex-engine/
├── include/                # 头文件
│   ├── tokenizer.h         #   词法分析器
│   ├── parser.h            #   递归下降解析器 + AST
│   ├── nfa.h               #   NFA 状态 / Thompson 构造
│   ├── dfa.h               #   DFA 状态 / 子集构造 / 匹配器
│   ├── hopcroft.h          #   Hopcroft DFA 最小化
│   ├── matcher.h           #   DFA 匹配器 (精确/子串/全局)
│   └── capture.h           #   捕获组追踪
├── src/                    # 源文件
│   ├── tokenizer.c
│   ├── parser.c
│   ├── nfa.c
│   ├── dfa.c
│   ├── hopcroft.c
│   ├── matcher.c
│   ├── capture.c
│   └── main.c              #   演示入口
├── tests/                  # 单元测试
│   ├── test_tokenizer.c    #   82 项
│   ├── test_parser.c       #   61 项
│   ├── test_nfa.c          #   91 项
│   ├── test_dfa.c          #  108 项
│   └── test_hopcroft.c     #   69 项 (共 411 项)
├── DOT/                    # DOT 可视化输出 (.gitignore)
├── CMakeLists.txt
└── .gitignore
```

## 快速开始

### 构建

```bash
mkdir build && cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

### 运行演示

```bash
./bin/regex_engine "a|bc*"          # 默认输入 "bc"
./bin/regex_engine "a*"   "aaa"     # 指定输入
```

输出：AST 树、NFA 图、DFA 转移表（最小化前后）、匹配结果、DOT 文件。

### 运行全部测试

```bash
cmake --build . --target run_tests
```

### 查看状态图

运行 demo 后在 `DOT/` 目录下找到 `.dot` 文件，粘贴到 [viz-js.com](https://viz-js.com) 即可查看。

## 支持的语法

| 语法     | 示例                                      | 说明                    |
| -------- | ----------------------------------------- | ----------------------- |
| 普通字符 | `a` `b` `c`                         | 字面匹配                |
| 任意字符 | `.`                                     | 匹配任意单字节          |
| 转义序列 | `\d` `\w` `\s` `\D` `\W` `\S` | 数字/单词/空白 及其取反 |
| 字符组   | `[abc]` `[^0-9]` `[a-z]`            | 支持范围与否定          |
| 连接     | `ab`                                    | 两因子并置              |
| 并集     | `a\|b`                                   | 分支选择                |
| 星号     | `a*`                                    | 零次或多次              |
| 加号     | `a+`                                    | 一次或多次              |
| 问号     | `a?`                                    | 零次或一次              |
| 范围量词 | `a{3}` `a{2,4}` `a{1,}`             | 精确/区间/无上限        |
| 捕获组   | `(a\|b)`                                 | 括号分组                |

## 构建要求

- CMake >= 3.16
- C11 编译器 (GCC / MinGW / MSVC)
- 警告级别: `-Wall -Wextra`
