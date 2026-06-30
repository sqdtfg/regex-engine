#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#endif
#include <stdlib.h>
#include "tokenizer.h"

/* ========================================================================== */
/*  测试框架                                                                    */
/* ========================================================================== */

static int g_passes = 0;
static int g_failures = 0;
static int g_module_passes = 0;
static int g_module_failures = 0;

/** 开始一个测试模块 */
static void module_begin(const char *name) {
    printf("\n══════════════════════════════════════\n");
    printf("  %s\n", name);
    printf("──────────────────────────────────────\n");
    g_module_passes = 0;
    g_module_failures = 0;
}

/** 结束一个测试模块 */
static void module_end(void) {
    printf("──────────────────────────────────────\n");
    if (g_module_failures == 0) {
        printf("  结果：全部通过 (%d 项)\n", g_module_passes);
    } else {
        printf("  结果：通过 %d 项，失败 %d 项\n", g_module_passes, g_module_failures);
    }
}

/** 记录单项测试通过 */
static void check_pass(const char *desc) {
    printf("  ✓ %s\n", desc);
    g_passes++;
    g_module_passes++;
}

/** 记录单项测试失败 */
static void check_fail(const char *desc, const char *expected, const char *actual) {
    printf("  ✗ %s —— 期望「%s」，实际「%s」\n", desc, expected, actual);
    g_failures++;
    g_module_failures++;
}

/* ========================================================================== */
/*  断言宏（只用于内部判断，不直接输出）                                         */
/* ========================================================================== */

#define CHECK_INT(expected, actual, desc) \
    do { \
        if ((int)(expected) != (int)(actual)) { \
            char exp[32], act[32]; \
            snprintf(exp, sizeof(exp), "%d", (int)(expected)); \
            snprintf(act, sizeof(act), "%d", (int)(actual)); \
            check_fail(desc, exp, act); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

#define CHECK_SIZE_T(expected, actual, desc) \
    do { \
        if ((size_t)(expected) != (size_t)(actual)) { \
            char exp[32], act[32]; \
            snprintf(exp, sizeof(exp), "%zu", (size_t)(expected)); \
            snprintf(act, sizeof(act), "%zu", (size_t)(actual)); \
            check_fail(desc, exp, act); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

#define CHECK_CHAR(expected, actual, desc) \
    do { \
        if ((expected) != (actual)) { \
            char exp[8], act[8]; \
            snprintf(exp, sizeof(exp), "'%c'", (expected)); \
            snprintf(act, sizeof(act), "'%c'", (actual)); \
            check_fail(desc, exp, act); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

#define CHECK_STR(expected, actual, desc) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            check_fail(desc, expected, actual); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

#define CHECK_BUF(expected, actual, n, desc) \
    do { \
        if (strncmp((expected), (actual), (n)) != 0 || strlen(actual) != (size_t)(n)) { \
            check_fail(desc, expected, actual); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

#define CHECK_TOKEN(e_type, tok, desc) \
    do { \
        RegexTokenType _expected = (e_type); \
        RegexTokenType _actual   = (tok).type; \
        if (_expected != _actual) { \
            Token _exp_tok = (Token){ .type = _expected }; \
            Token _act_tok = (tok); \
            check_fail(desc, token_to_string(_exp_tok), token_to_string(_act_tok)); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

/* ========================================================================== */
/*  测试用例                                                                    */
/* ========================================================================== */

/* --- 初始化 --- */
static void test_init_empty(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "");
    CHECK_SIZE_T(0, tok.length, "空字符串 — 长度为 0");
    CHECK_SIZE_T(0, tok.pos, "空字符串 — 位置为 0");
    CHECK_INT(0, tok.error_code, "空字符串 — 错误码为 0");
}

static void test_init_null(void) {
    Tokenizer tok;
    tokenizer_init(&tok, NULL);
    CHECK_SIZE_T(0, tok.length, "NULL 输入 — 长度为 0");
    CHECK_SIZE_T(0, tok.pos, "NULL 输入 — 位置为 0");
}

static void test_init_basic(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "abc");
    CHECK_SIZE_T(3, tok.length, "普通字符串 — 长度为 3");
    CHECK_SIZE_T(0, tok.pos, "普通字符串 — 位置为 0");
}

/* --- EOF --- */
static void test_eof(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_EOF, t, "空输入直接返回结束标记");
}

static void test_eof_after_spaces(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "   ");
    tokenizer_next(&tok);
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_EOF, t, "纯空格输入返回结束标记");
}

/* --- 单字符 --- */
static void test_char_token(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "a");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "字符 'a' — 类型为普通字符");
    CHECK_CHAR('a', t.ch, "字符 'a' — 值为 'a'");
    CHECK_SIZE_T(0, t.pos, "字符 'a' — 位置为 0");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_EOF, t, "字符 'a' 之后 — 返回结束标记");
}

static void test_special_chars(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "()*+?|^.");

    CHECK_TOKEN(TOK_LPAREN,  tokenizer_next(&tok), "'(' → 左括号");
    CHECK_TOKEN(TOK_RPAREN,  tokenizer_next(&tok), "')' → 右括号");
    CHECK_TOKEN(TOK_STAR,    tokenizer_next(&tok), "'*' → 星号");
    CHECK_TOKEN(TOK_PLUS,    tokenizer_next(&tok), "'+' → 加号");
    CHECK_TOKEN(TOK_QUESTION, tokenizer_next(&tok), "'?' → 问号");
    CHECK_TOKEN(TOK_PIPE,    tokenizer_next(&tok), "'|' → 竖线");
    CHECK_TOKEN(TOK_CARET,   tokenizer_next(&tok), "'^' → 脱字符");
    CHECK_TOKEN(TOK_DOT,     tokenizer_next(&tok), "'.' → 点号");
    CHECK_TOKEN(TOK_EOF,     tokenizer_next(&tok), "结尾 → 结束标记");
}

static void test_multiple_chars(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "abc");

    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "连续字符 — 第 1 个类型为普通字符");
    CHECK_CHAR('a', t.ch, "连续字符 — 第 1 个值为 'a'");
    CHECK_SIZE_T(0, t.pos, "连续字符 — 第 1 个位置为 0");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "连续字符 — 第 2 个类型为普通字符");
    CHECK_CHAR('b', t.ch, "连续字符 — 第 2 个值为 'b'");
    CHECK_SIZE_T(1, t.pos, "连续字符 — 第 2 个位置为 1");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "连续字符 — 第 3 个类型为普通字符");
    CHECK_CHAR('c', t.ch, "连续字符 — 第 3 个值为 'c'");
    CHECK_SIZE_T(2, t.pos, "连续字符 — 第 3 个位置为 2");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_EOF, t, "连续字符后 — 返回结束标记");
}

/* --- 空白跳过 --- */
static void test_skip_leading_spaces(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "  ab");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "跳过前导空格 — 类型为普通字符");
    CHECK_CHAR('a', t.ch, "跳过前导空格 — 值为 'a'");
    CHECK_SIZE_T(2, t.pos, "跳过前导空格 — 位置为 2（跳过了 2 个空格）");
}

static void test_skip_spaces_between(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "a  b");
    tokenizer_next(&tok);
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "跳过中间空格 — 类型为普通字符");
    CHECK_CHAR('b', t.ch, "跳过中间空格 — 值为 'b'");
    CHECK_SIZE_T(3, t.pos, "跳过中间空格 — 位置为 3");
}

/* --- 转义序列 --- */
static void test_escape_digit(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\d");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ESCAPE, t, "\\d → 转义序列");
    CHECK_INT(ESCAPE_DIGIT, t.esc, "\\d → 数字转义");
}

static void test_escape_word(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\w");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ESCAPE, t, "\\w → 转义序列");
    CHECK_INT(ESCAPE_WORD, t.esc, "\\w → 单词字符转义");
}

static void test_escape_space(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\s");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ESCAPE, t, "\\s → 转义序列");
    CHECK_INT(ESCAPE_SPACE, t.esc, "\\s → 空白字符转义");
}

static void test_escape_uppercase(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\D\\W\\S");

    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ESCAPE, t, "\\D → 转义序列");
    CHECK_INT(ESCAPE_NON_DIGIT, t.esc, "\\D → 非数字转义");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ESCAPE, t, "\\W → 转义序列");
    CHECK_INT(ESCAPE_NON_WORD, t.esc, "\\W → 非单词字符转义");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ESCAPE, t, "\\S → 转义序列");
    CHECK_INT(ESCAPE_NON_SPACE, t.esc, "\\S → 非空白字符转义");
}

static void test_escape_dot(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\.");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "\\. → 普通字符（转义元字符）");
    CHECK_CHAR('.', t.ch, "\\. → 值为 '.'");
}

static void test_escape_backslash(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\\\");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CHAR, t, "\\\\ → 普通字符（转义反斜杠）");
    CHECK_CHAR('\\', t.ch, "\\\\ → 值为 '\\'");
}

static void test_escape_incomplete(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ERROR, t, "末尾 \\ → 词法错误");
    CHECK_INT(1, tok.error_code, "末尾 \\ → 错误码已设置");
}

/* --- 字符集合 --- */
static void test_bracket_simple(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "[abc]");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_BRACKET, t, "[abc] → 字符集合");
    CHECK_SIZE_T(3, t.bracket.len, "[abc] → 内容长度为 3");
    CHECK_BUF("abc", t.bracket.str, 3, "[abc] → 内容为 \"abc\"");
    free(t.bracket.str);
}

static void test_bracket_first_close(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "[]abc]");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_BRACKET, t, "[]abc] → 字符集合（首字符 ']' 为字面量）");
    CHECK_SIZE_T(4, t.bracket.len, "[]abc] → 内容长度为 4");
    CHECK_BUF("]abc", t.bracket.str, 4, "[]abc] → 内容为 \"]abc\"");
    free(t.bracket.str);
}

static void test_bracket_negation(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "[^abc]");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_BRACKET, t, "[^abc] → 字符集合（含否定符）");
    CHECK_SIZE_T(4, t.bracket.len, "[^abc] → 内容长度为 4（含 '^'）");
    free(t.bracket.str);
}

static void test_bracket_unclosed(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "[abc");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ERROR, t, "[abc（未闭合）→ 词法错误");
}

/* --- 量词 --- */
static void test_curly_exact(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "{3}");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CURLY, t, "{3} → 量词");
    CHECK_INT(3, t.curly.min, "{3} → 最小值为 3");
    CHECK_INT(3, t.curly.max, "{3} → 最大值为 3");
}

static void test_curly_range(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "{2,5}");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CURLY, t, "{2,5} → 量词");
    CHECK_INT(2, t.curly.min, "{2,5} → 最小值为 2");
    CHECK_INT(5, t.curly.max, "{2,5} → 最大值为 5");
}

static void test_curly_open(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "{3,}");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_CURLY, t, "{3,} → 量词（无上限）");
    CHECK_INT(3, t.curly.min, "{3,} → 最小值为 3");
    CHECK_INT(-1, t.curly.max, "{3,} → 最大值为 -1 表示无上限");
}

static void test_curly_unclosed(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "{3");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ERROR, t, "{3（未闭合）→ 词法错误");
}

static void test_curly_no_digit(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "{a}");
    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ERROR, t, "{a}（非数字）→ 词法错误");
}

/* --- token_to_string --- */
static void test_token_to_string(void) {
    Token t;
    t = (Token){ .type = TOK_CHAR };
    CHECK_STR("普通字符", token_to_string(t), "普通字符 → 中文名");

    t = (Token){ .type = TOK_EOF };
    CHECK_STR("输入结束", token_to_string(t), "EOF → 中文名");

    t = (Token){ .type = TOK_ERROR };
    CHECK_STR("词法错误", token_to_string(t), "ERROR → 中文名");

    t = (Token){ .type = (RegexTokenType)-1 };
    CHECK_STR("未知类型", token_to_string(t), "非法类型 → 中文名");
}

/* --- 错误恢复 --- */
static void test_error_continues(void) {
    Tokenizer tok;
    tokenizer_init(&tok, "\\");

    Token t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ERROR, t, "第 1 次调用 → 返回错误");

    t = tokenizer_next(&tok);
    CHECK_TOKEN(TOK_ERROR, t, "第 2 次调用 → 持续返回错误");
}

/* ========================================================================== */
/*  主函数                                                                      */
/* ========================================================================== */

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
#endif

    printf("╔══════════════════════════════════════╗\n");
    printf("║     词法分析器 单元测试              ║\n");
    printf("╚══════════════════════════════════════╝\n");

    /* ---- 初始化 ---- */
    module_begin("初始化");
    test_init_empty();
    test_init_null();
    test_init_basic();
    module_end();

    /* ---- 结束标记 ---- */
    module_begin("结束标记 (EOF)");
    test_eof();
    test_eof_after_spaces();
    module_end();

    /* ---- 单字符 ---- */
    module_begin("单字符解析");
    test_char_token();
    test_special_chars();
    test_multiple_chars();
    module_end();

    /* ---- 空白跳过 ---- */
    module_begin("空白字符跳过");
    test_skip_leading_spaces();
    test_skip_spaces_between();
    module_end();

    /* ---- 转义序列 ---- */
    module_begin("转义序列");
    test_escape_digit();
    test_escape_word();
    test_escape_space();
    test_escape_uppercase();
    test_escape_dot();
    test_escape_backslash();
    test_escape_incomplete();
    module_end();

    /* ---- 字符集合 ---- */
    module_begin("字符集合 [...]");
    test_bracket_simple();
    test_bracket_first_close();
    test_bracket_negation();
    test_bracket_unclosed();
    module_end();

    /* ---- 量词 ---- */
    module_begin("量词 {m,n}");
    test_curly_exact();
    test_curly_range();
    test_curly_open();
    test_curly_unclosed();
    test_curly_no_digit();
    module_end();

    /* ---- token_to_string ---- */
    module_begin("Token 转字符串");
    test_token_to_string();
    module_end();

    /* ---- 错误恢复 ---- */
    module_begin("错误状态恢复");
    test_error_continues();
    module_end();

    /* ---- 总结果 ---- */
    printf("\n╔══════════════════════════════════════╗\n");
    if (g_failures == 0) {
        printf("║  测试全部通过！                      ║\n");
    } else {
        printf("║  测试存在失败                        ║\n");
    }
    printf("║  总计：%3d 项                        ║\n", g_passes + g_failures);
    printf("║  通过：%3d 项                        ║\n", g_passes);
    if (g_failures > 0) {
        printf("║  失败：%3d 项                        ║\n", g_failures);
    }
    printf("╚══════════════════════════════════════╝\n");

    return g_failures > 0 ? 1 : 0;
}
