# 迭代11 总结 — ERE 91% + POSIX字符类 + 回溯NFA对比

## 修改的问题

1. **parser.c — `)` 孤括号兼容**
   - `parse_atom` 中添加 TOK_RPAREN → 普通字符 ')' 
   - 修复 `)` 独立出现时报错的问题（#10, #218, #219 → 已解决）

2. **dfa.c — POSIX 字符类支持**
   - `bracket_matches` 识别 `[[:alpha:]]` `[[:digit:]]` `[[:lower:]]` `[[:upper:]]` `[[:xdigit:]]` `[[:alnum:]]` `[[:punct:]]` `[[:space:]]`
   - 注意：posix_compat 路径（Grep compat layer通过 regcomp → dfa_from_ast_with_groups）使用同一个 dfa.c 的 bracket_matches

3. **posix_system_bench.c — 回溯NFA对比**
   - 实现标准回溯NFA匹配器（与传统 POSIX regex.h 内部同算法）
   - DFA vs 回溯NFA 直接性能对比
   - 结论：全部 5 种模式达标

## ERE Spencer 测试结果

| 指标 | 迭代10 | 迭代11 |
|------|--------|--------|
| 总计 | 221 | 221 |
| 通过 | 161 | 164 |
| 失败 | 23 | 20 |
| 预期失败 | 37 | 37 |
| 通过率(计预期失败) | 89.6% | **91.0%** ✅ |

### 剩余 20 个失败分类

| 类别 | 数量 | 案例 |
|------|------|------|
| `$` 锚定误判 | 2 | #56 `a$` on `a$` — 输入含字面$ |
| `{` 回退不完整 | 5 | #76 `a{1a}` #77 `a{,2}` #78 `a{,}` #79 `a{}` #80 `a{1,*}` |
| `[^]b]c` bracket | 1 | #114 `a[^]b]c` — `]` 在 `^` 后应为字面 |
| POSIX 字符类 | 8 | #133-142 （lexer tokenizer 未在 posix_compat 路径工作） |
| POSIX 字符类匹配 | 4 | #143-146 `[[:digit:]]` 等 — bracket 内容 `[[:alpha:]]` 中间的 `]` 被 tokenizer 提前闭合 |

## POSIX 字符类根因分析

`[[:alpha:]]` 在 tokenizer 的 `parse_bracket` 中：
- 遇到 `:` 后的第一个 `]` 就被当作闭合符
- bracket 内容只收集到 `[[:alpha:` 为止
- 后面的 `]]` 被当作外部字面字符

**修复方向**: 在 tokenizer 中识别 `[[:` 前缀，在遇到 `:]]` 之前不闭合 bracket

## 下步

- 修复 tokenizer bracket 解析 POSIX 字符类
- 修复 `$` 锚定和 `{` 回退
- 重写 dot_gen 和 main.c
