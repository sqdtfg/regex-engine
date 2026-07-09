# 迭代1 总结 — Bug修复 + 代码清理 + 锚定实现

## 修改的问题

1. **test_api.c 期望值错误**：`\w+@\w+\.\w+` 匹配 `user@example.com` 长度是 16 不是 14

2. **capture.c `match_sub_dfa_greedy()` 返回丢失起始位置**：
   - 原函数只返回长度，调用者假设所有捕获组从 `full_match.start` 开始
   - 修复：增加 `out_start` 输出参数，返回值包含匹配的绝对起始偏移
   - 修复：增加起始状态接受检查（空匹配如 a*）

3. **posix_compat.c 双重解析**：
   - `regcomp()` 先调用 `regex_compile()` 解析一遍，再手动 `parser_parse()` 解析第二遍
   - 修复：只解析一次 AST，同时构建基础 DFA 和捕获组 DFA

4. **regex_compile 缺少 `dfa_minimize`**：
   - 生产 API 路径不调最小化，导致匹配时使用未最小化的 DFA（状态更多）
   - 修复：在 `regex_compile` 中构建 DFA 后调用 `dfa_minimize`

5. **`^`/`$` 锚定未实现**：
   - Tokenizer 产生 TOK_CARET/TOK_DOLLAR 但 Parser 忽略
   - 修复：Parser 添加 `AST_ANCHOR_START`/`AST_ANCHOR_END`
   - NFA 层面用 ε 边透传
   - `dfa_match()` 中：`^` → 限从 pos=0 开始，`$` → 仅接受匹配到末尾的
   - 锚定标记通过 `NFAGraph` → `DFAMachine` 传递

6. **dfa.h / matcher.h 重复声明**：所有函数声明合并到 dfa.h，matcher.h 变为兼容层

## 解决的问题

- capture 组位置现在正确输出各组的绝对起始偏移
- 模式只解析一次，减少不必要的 AST/NFA/DFA 构建
- 生产路径 DFA 经过最小化，匹配速度更快
- `^`/`$` 锚定功能可用
- 头文件声明唯一，不再重复

## 测试状态

全部 9 个测试套件通过（共计 ~676 项），无回归。

## 下一步计划

- 迭代2：构建 grep 测试兼容层，测量当前通过率
- 迭代3：性能基准测试 + NFA→DFA 转换图
