# Grep 开源测试集 - Regex Engine

这是一套完整的正则表达式测试集，包含超过 200+ 个测试用例，覆盖了正则表达式的主要功能点。

## 📊 测试集统计

| 文件 | 测试用例数 | 覆盖功能 |
|------|-----------|---------|
| `basic.txt` | 37 | 基础功能（字面量、量词、分支、字符类、点号） |
| `literals.txt` | 20 | 字面量匹配、大小写敏感、空白字符 |
| `quantifiers.txt` | 30 | 量词测试（* + ?）、分组量词、嵌套量词 |
| `character_classes.txt` | 50 | 字符类、范围、否定、组合 |
| `dot.txt` | 35 | 点号匹配、点号与量词组合 |
| `anchors.txt` | 45 | 锚点（^ $）、完整匹配 |
| `groups.txt` | 50 | 分组、嵌套分组、分组与量词 |
| `escape.txt` | 70 | 转义序列、字符类简写（\d \w \s） |
| `complex.txt` | 40 | 复杂场景、真实场景、边界条件 |
| **总计** | **377+** | **完整覆盖** |

## 🎯 测试覆盖

### 基础功能
- ✅ 字面量匹配
- ✅ 字符类和范围
- ✅ 点号任意字符匹配
- ✅ 量词（* + ?）
- ✅ 分支选择（|）
- ✅ 分组（）

### 高级功能
- ✅ 转义序列
- ✅ 字符类简写（\d \w \s 及其否定）
- ✅ 锚点（^ $）
- ✅ 嵌套分组
- ✅ 分组量词
- ✅ 复杂组合模式

### 真实场景
- ✅ 邮箱地址
- ✅ URL
- ✅ 电话号码
- ✅ IP 地址
- ✅ 日期时间
- ✅ 标识符

### 边界条件
- ✅ 空字符串
- ✅ 单个字符
- ✅ 长文本
- ✅ 重叠匹配
- ✅ 零宽匹配

## 📝 测试用例格式

```
# 注释行
pattern text expected_result [expected_start] [expected_end]
```

其中：
- `pattern` - 正则表达式模式
- `text` - 要匹配的文本
- `expected_result` - 1 表示匹配，0 表示不匹配
- `expected_start` - 期望的匹配起始位置（可选）
- `expected_end` - 期望的匹配结束位置（可选）

## 🚀 运行测试

### 在 VS Code 终端中运行

```powershell
# 进入 build 目录
cd build

# 运行单个测试文件
./bin/regex_grep_test ../tests/grep_tests/basic.txt

# 运行多个测试文件
./bin/regex_grep_test ../tests/grep_tests/basic.txt ../tests/grep_tests/literals.txt

# 运行所有测试文件
./bin/regex_grep_test ../tests/grep_tests/*.txt
```

### 使用 CMake 目标运行

```powershell
cd build
cmake --build . --target run_grep_tests
```

## 📈 完成标准

任务要求：通过 **90%** 以上的测试用例

- **当前总测试数**：377+
- **目标通过数**：340+
- **90% 通过率**：339+

## 💡 使用建议

1. **先跑基础测试** - 从 `basic.txt` 开始
2. **逐步添加** - 基础通过后再跑复杂测试
3. **失败分析** - 查看具体失败用例，定位问题
4. **边界测试** - 特别关注边界条件和复杂场景

## 🔧 扩展测试集

如需添加更多测试，只需在相应文件中按格式添加新行即可。

## 📚 参考

- grep 测试集
- POSIX 正则表达式标准
- regex 引擎测试实践
