# 迭代12 总结 — dot_gen HTML + main.c Demo 重写

## 修改的问题

1. **dot_gen.c 完全重写**
   - 生成自包含 HTML 报告: `DOT/report.html`
   - 含有 8 组模式的完整状态转移表（HTML 表格, 支持悬停高亮）
   - NFA→DFA 压缩比汇总卡片
   - 同时保持 DOT 文件生成
   - 输出包含: 浏览器可打开的 HTML + 文本 transfer 路径

2. **main.c — 答辩 Demo 重写**
   - 4 个内置演示（无参数模式）:
     - Demo 1: `a(b|c)*d` — 展示完整管道 NFA/DFA/最小化
     - Demo 2: `\w+@\w+\.\w+` — 邮箱匹配
     - Demo 3: `\d{3}-\d{4}` — 电话匹配 
     - Demo 4: `^abc` — 锚定演示
   - 命令行模式: `regex_engine "pattern" "text"` 单次匹配
   - 展示每阶段耗时（μs级） + 状态数压缩
   - 中文输出 + UTF-8 安全

3. **tokenizer bracket 修复** (已提交)
   - `[[` 序列在 brack 中不触发早闭

## 测试状态

- 内部 9 套全部通过
- ERE 91.0%
- 0 warnings
