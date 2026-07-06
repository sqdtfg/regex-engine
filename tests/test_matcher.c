/**
 * test_matcher.c — DFA 匹配器单元测试
 *
 * 测试覆盖：
 *   - dfa_match_full   (精确匹配整个输入)
 *   - dfa_match        (子串匹配)
 *   - dfa_match_all    (全局匹配)
 *   - dfa_dump / dfa_dump_dot (可视化输出)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "dfa.h"
#include "matcher.h"
#include "parser.h"

/* ========================================================================== */
/*  测试框架                                                                    */
/* ========================================================================== */

static int g_passes = 0;
static int g_failures = 0;
static int g_module_passes = 0;
static int g_module_failures = 0;

static void module_begin(const char *name) {
    printf("\n");
    printf("==================================================\n");
    printf("  %s\n", name);
    printf("--------------------------------------------------\n");
    g_module_passes = 0;
    g_module_failures = 0;
}

static void module_end(void) {
    printf("--------------------------------------------------\n");
    if (g_module_failures == 0) {
        printf("  结果：全部通过 (%d 项)\n", g_module_passes);
    } else {
        printf("  结果：通过 %d 项，失败 %d 项\n", g_module_passes, g_module_failures);
    }
}

static void check_pass(const char *desc) {
    printf("  PASS: %s\n", desc);
    g_passes++;
    g_module_passes++;
}

static void check_fail(const char *desc, const char *expected, const char *actual) {
    printf("  FAIL: %s — 期望「%s」，实际「%s」\n", desc, expected, actual);
    g_failures++;
    g_module_failures++;
}

#define CHECK_INT_EQ(expected, actual, desc) \
    do { if ((int)(expected) != (int)(actual)) { \
        char e[32], a[32]; snprintf(e,sizeof(e),"%d",(int)(expected)); \
        snprintf(a,sizeof(a),"%d",(int)(actual)); check_fail(desc,e,a); \
    } else check_pass(desc); } while (0)

#define CHECK_SIZE_T_EQ(expected, actual, desc) \
    do { if ((size_t)(expected) != (size_t)(actual)) { \
        char e[32], a[32]; snprintf(e,sizeof(e),"%zu",(size_t)(expected)); \
        snprintf(a,sizeof(a),"%zu",(size_t)(actual)); check_fail(desc,e,a); \
    } else check_pass(desc); } while (0)

#define CHECK_TRUE(cond, desc) \
    do { if (cond) check_pass(desc); else check_fail(desc, "true", "false"); } while (0)

#define CHECK_FALSE(cond, desc) \
    do { if (!(cond)) check_pass(desc); else check_fail(desc, "false", "true"); } while (0)

#define CHECK_RESULT(exp_matched, exp_start, exp_end, exp_length, res, desc) \
    do { \
        int _ok = 1; \
        if ((res).matched != (exp_matched)) { \
            char e[32], a[32]; snprintf(e,sizeof(e),"%d",(exp_matched)); \
            snprintf(a,sizeof(a),"%d",(res).matched); \
            check_fail(desc "_matched", e, a); _ok = 0; \
        } \
        if ((size_t)(exp_start) != (res).start) { \
            char e[32], a[32]; snprintf(e,sizeof(e),"%zu",(size_t)(exp_start)); \
            snprintf(a,sizeof(a),"%zu",(res).start); \
            check_fail(desc "_start", e, a); _ok = 0; \
        } \
        if ((size_t)(exp_end) != (res).end) { \
            char e[32], a[32]; snprintf(e,sizeof(e),"%zu",(size_t)(exp_end)); \
            snprintf(a,sizeof(a),"%zu",(res).end); \
            check_fail(desc "_end", e, a); _ok = 0; \
        } \
        if ((size_t)(exp_length) != (res).length) { \
            char e[32], a[32]; snprintf(e,sizeof(e),"%zu",(size_t)(exp_length)); \
            snprintf(a,sizeof(a),"%zu",(res).length); \
            check_fail(desc "_length", e, a); _ok = 0; \
        } \
        if (_ok) check_pass(desc); \
    } while (0)

/* ========================================================================== */
/*  辅助函数：pattern → DFA                                                    */
/* ========================================================================== */

static DFAMachine build_dfa(const char *pattern) {
    Parser parser;
    parser_init(&parser, pattern);
    ASTNode *ast = parser_parse(&parser);
    if (!ast) {
        printf("  ! 解析失败: %s\n", parser.error_msg);
        DFAMachine dfa = {0};
        return dfa;
    }
    NFAGraph nfa = nfa_from_ast(ast);
    ast_free(ast);
    if (!nfa.start) {
        DFAMachine dfa = {0};
        return dfa;
    }
    DFAMachine dfa = dfa_from_nfa(&nfa);
    nfa_free(&nfa);
    return dfa;
}

/* ========================================================================== */
/*  测试：dfa_match_full — 精确匹配整个输入                                     */
/* ========================================================================== */

static void test_full_exact_match(void) {
    DFAMachine dfa = build_dfa("abc");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "abc");
    CHECK_RESULT(1, 0, 3, 3, result, "full: \"abc\" 匹配 \"abc\"");

    result = dfa_match_full(&dfa, "abcd");
    CHECK_FALSE(result.matched, "full: \"abcd\" 不匹配 \"abc\"");

    result = dfa_match_full(&dfa, "ab");
    CHECK_FALSE(result.matched, "full: \"ab\" 不匹配 \"abc\"");

    result = dfa_match_full(&dfa, "");
    CHECK_FALSE(result.matched, "full: 空串不匹配 \"abc\"");

    dfa_free(&dfa);
}

static void test_full_star(void) {
    DFAMachine dfa = build_dfa("a*");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "");
    CHECK_RESULT(1, 0, 0, 0, result, "a*: 空串匹配");

    result = dfa_match_full(&dfa, "a");
    CHECK_RESULT(1, 0, 1, 1, result, "a*: \"a\" 匹配");

    result = dfa_match_full(&dfa, "aaaa");
    CHECK_RESULT(1, 0, 4, 4, result, "a*: \"aaaa\" 匹配");

    result = dfa_match_full(&dfa, "b");
    CHECK_FALSE(result.matched, "a*: \"b\" 不匹配");

    result = dfa_match_full(&dfa, "aaab");
    CHECK_FALSE(result.matched, "a*: \"aaab\" 不匹配");

    dfa_free(&dfa);
}

static void test_full_plus(void) {
    DFAMachine dfa = build_dfa("a+");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "a");
    CHECK_RESULT(1, 0, 1, 1, result, "a+: \"a\" 匹配");

    result = dfa_match_full(&dfa, "aaa");
    CHECK_RESULT(1, 0, 3, 3, result, "a+: \"aaa\" 匹配");

    result = dfa_match_full(&dfa, "");
    CHECK_FALSE(result.matched, "a+: 空串不匹配");

    dfa_free(&dfa);
}

static void test_full_dot(void) {
    DFAMachine dfa = build_dfa("a.c");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "abc");
    CHECK_RESULT(1, 0, 3, 3, result, ". : \"abc\" 匹配");

    result = dfa_match_full(&dfa, "aXc");
    CHECK_RESULT(1, 0, 3, 3, result, ". : \"aXc\" 匹配");

    result = dfa_match_full(&dfa, "ac");
    CHECK_FALSE(result.matched, ". : \"ac\" 不匹配（缺1字符）");

    dfa_free(&dfa);
}

static void test_full_digit(void) {
    DFAMachine dfa = build_dfa("\\d\\d");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "42");
    CHECK_RESULT(1, 0, 2, 2, result, "\\d\\d: \"42\" 匹配");

    result = dfa_match_full(&dfa, "ab");
    CHECK_FALSE(result.matched, "\\d\\d: \"ab\" 不匹配");

    result = dfa_match_full(&dfa, "4");
    CHECK_FALSE(result.matched, "\\d\\d: \"4\" 不匹配");

    dfa_free(&dfa);
}

static void test_full_alter(void) {
    DFAMachine dfa = build_dfa("cat|dog");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "cat");
    CHECK_RESULT(1, 0, 3, 3, result, "cat|dog: \"cat\" 匹配");

    result = dfa_match_full(&dfa, "dog");
    CHECK_RESULT(1, 0, 3, 3, result, "cat|dog: \"dog\" 匹配");

    result = dfa_match_full(&dfa, "cats");
    CHECK_FALSE(result.matched, "cat|dog: \"cats\" 不匹配");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：dfa_match — 子串匹配                                                  */
/* ========================================================================== */

static void test_substring_basic(void) {
    DFAMachine dfa = build_dfa("abc");
    MatchResult result = {0};

    result = dfa_match(&dfa, "xxabcyy");
    CHECK_RESULT(1, 2, 5, 3, result, "sub: \"xxabcyy\" 匹配 abc");

    result = dfa_match(&dfa, "abcdef");
    CHECK_RESULT(1, 0, 3, 3, result, "sub: \"abcdef\" 匹配 abc");

    result = dfa_match(&dfa, "def");
    CHECK_RESULT(0, 0, 0, 0, result, "sub: \"def\" 不匹配 abc");

    dfa_free(&dfa);
}

static void test_substring_star(void) {
    DFAMachine dfa = build_dfa("a*");
    MatchResult result = {0};

    result = dfa_match(&dfa, "bbb");
    /* a* 可匹配空串，应从位置 0 匹配长度为 0 */
    CHECK_TRUE(result.matched, "a*: \"bbb\" 应匹配空串");

    result = dfa_match(&dfa, "baac");
    CHECK_TRUE(result.matched, "a*: \"baac\" 应匹配 aa");

    dfa_free(&dfa);
}

static void test_substring_first_occurrence(void) {
    DFAMachine dfa = build_dfa("cat");
    MatchResult result = {0};

    result = dfa_match(&dfa, "the cat sat on the mat");
    CHECK_RESULT(1, 4, 7, 3, result, "sub: 第一个 'cat' 在位置 4");

    dfa_free(&dfa);
}

static void test_substring_anchored_like(void) {
    /* ^ 锚定暂不支持，用 abc 模拟 */
    DFAMachine dfa = build_dfa("abc");
    MatchResult result = {0};

    result = dfa_match(&dfa, "abc");
    CHECK_RESULT(1, 0, 3, 3, result, "sub: \"abc\" 精确匹配");

    result = dfa_match(&dfa, "xabc");
    CHECK_RESULT(1, 1, 4, 3, result, "sub: \"xabc\" 匹配 abc 在位置 1");

    dfa_free(&dfa);
}

static void test_substring_empty_input(void) {
    DFAMachine dfa = build_dfa("a");
    MatchResult result = {0};

    result = dfa_match(&dfa, "");
    CHECK_RESULT(0, 0, 0, 0, result, "sub: 空输入不匹配");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：dfa_match_all — 全局匹配                                              */
/* ========================================================================== */

static void test_global_basic(void) {
    DFAMachine dfa = build_dfa("a");
    MatchResult results[64];
    int count;

    count = dfa_match_all(&dfa, "abracadabra", results, 64);
    CHECK_INT_EQ(5, count, "global: 'abracadabra' 有 5 个 'a'");
    if (count >= 5) {
        CHECK_SIZE_T_EQ(0, results[0].start, "global: 第1个 'a' 在 0");
        CHECK_SIZE_T_EQ(1, results[0].end, "global: 第1个 'a' 结束于 1");
        CHECK_SIZE_T_EQ(3, results[1].start, "global: 第2个 'a' 在 3");
        CHECK_SIZE_T_EQ(7, results[3].start, "global: 第4个 'a' 在 7");
        CHECK_SIZE_T_EQ(10, results[4].start, "global: 第5个 'a' 在 10");
    }

    dfa_free(&dfa);
}

static void test_global_multiple(void) {
    DFAMachine dfa = build_dfa("an");
    MatchResult results[64];
    int count;

    count = dfa_match_all(&dfa, "banana", results, 64);
    CHECK_INT_EQ(2, count, "global: 'banana' 有 2 个 'an'");
    if (count >= 2) {
        CHECK_SIZE_T_EQ(1, results[0].start, "global: 第1个 'an' 在 1");
        CHECK_SIZE_T_EQ(3, results[1].start, "global: 第2个 'an' 在 3");
    }

    dfa_free(&dfa);
}

static void test_global_overlap_skip(void) {
    /* 全局匹配应向前推进，避免无限循环 */
    DFAMachine dfa = build_dfa("aa");
    MatchResult results[64];
    int count;

    count = dfa_match_all(&dfa, "aaaa", results, 64);
    /* 贪婪最长匹配后从 best_end 或 pos+1 推进 */
    CHECK_INT_EQ(2, count, "global: 'aaaa' 有 2 个重叠 'aa'");

    dfa_free(&dfa);
}

static void test_global_no_match(void) {
    DFAMachine dfa = build_dfa("xyz");
    MatchResult results[64];
    int count;

    count = dfa_match_all(&dfa, "abc", results, 64);
    CHECK_INT_EQ(0, count, "global: 'abc' 无 'xyz'");

    dfa_free(&dfa);
}

static void test_global_capacity(void) {
    DFAMachine dfa = build_dfa("a");
    MatchResult results[4];
    int count;

    count = dfa_match_all(&dfa, "aaaaa", results, 4);
    CHECK_INT_EQ(4, count, "global: 容量 4 限制返回 4");

    dfa_free(&dfa);
}

static void test_global_null_args(void) {
    DFAMachine dfa = build_dfa("a");
    int count;

    count = dfa_match_all(NULL, "abc", NULL, 10);
    CHECK_INT_EQ(0, count, "global: NULL dfa 返回 0");

    count = dfa_match_all(&dfa, NULL, NULL, 10);
    CHECK_INT_EQ(0, count, "global: NULL text 返回 0");

    count = dfa_match_all(&dfa, "abc", NULL, 10);
    CHECK_INT_EQ(0, count, "global: NULL results 返回 0");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：dfa_dump 可视化输出                                                   */
/* ========================================================================== */

static void test_dump_output(void) {
    DFAMachine dfa = build_dfa("a|b");
    printf("  --- dfa_dump 输出 ---\n");
    dfa_dump(&dfa);
    printf("  --- dfa_dump 结束 ---\n");
    check_pass("dump: a|b 无崩溃输出");
    dfa_free(&dfa);
}

static void test_dump_dot_output(void) {
    DFAMachine dfa = build_dfa("a|b");
    /* 输出到 stdout 验证不崩溃 */
    printf("  --- DOT 输出 ---\n");
    dfa_dump_dot(&dfa, stdout);
    printf("  --- DOT 结束 ---\n");
    check_pass("dot: a|b 无崩溃输出");
    dfa_free(&dfa);
}

static void test_dump_null(void) {
    dfa_dump(NULL);
    check_pass("dump: NULL 无崩溃");

    FILE *fp = tmpfile();
    if (fp) {
        dfa_dump_dot(NULL, fp);
        fclose(fp);
        check_pass("dot: NULL 无崩溃");
    }
}

/* ========================================================================== */
/*  测试：边界和安全性                                                          */
/* ========================================================================== */

static void test_edge_cases(void) {
    /* 单字符 DFA */
    DFAMachine dfa = build_dfa("a");
    MatchResult r = {0};

    r = dfa_match_full(&dfa, "a");
    CHECK_RESULT(1, 0, 1, 1, r, "edge: 单字符精确匹配");

    r = dfa_match_full(&dfa, "A");
    CHECK_RESULT(0, 0, 0, 0, r, "edge: 大小写敏感");

    dfa_free(&dfa);

    /* 空正则 "" */
    /* parser 对空字符串返回 NULL，此处略过 */

    /* 长输入 */
    dfa = build_dfa("a");
    char long_text[1000];
    memset(long_text, 'a', 999);
    long_text[999] = '\0';
    r = dfa_match_full(&dfa, long_text);
    CHECK_FALSE(r.matched, "edge: 全 'a' 长串不匹配（需要 a*）");

    dfa_free(&dfa);
}

static void test_special_chars(void) {
    DFAMachine dfa = build_dfa("[^a-z]");
    MatchResult result = {0};

    result = dfa_match_full(&dfa, "1");
    CHECK_RESULT(1, 0, 1, 1, result, "special: [^a-z] 匹配 '1'");

    result = dfa_match_full(&dfa, "a");
    CHECK_RESULT(0, 0, 0, 0, result, "special: [^a-z] 不匹配 'a'");

    result = dfa_match_full(&dfa, "\n");
    CHECK_RESULT(1, 0, 1, 1, result, "special: [^a-z] 匹配 '\\n'");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  主函数                                                                      */
/* ========================================================================== */

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("==================================================\n");
    printf("  DFA 匹配器 单元测试\n");
    printf("==================================================\n");

    /* ---- 精确匹配 ---- */
    module_begin("精确匹配 (dfa_match_full)");
    test_full_exact_match();
    test_full_star();
    test_full_plus();
    test_full_dot();
    test_full_digit();
    test_full_alter();
    module_end();

    /* ---- 子串匹配 ---- */
    module_begin("子串匹配 (dfa_match)");
    test_substring_basic();
    test_substring_star();
    test_substring_first_occurrence();
    test_substring_anchored_like();
    test_substring_empty_input();
    module_end();

    /* ---- 全局匹配 ---- */
    module_begin("全局匹配 (dfa_match_all)");
    test_global_basic();
    test_global_multiple();
    test_global_overlap_skip();
    test_global_no_match();
    test_global_capacity();
    test_global_null_args();
    module_end();

    /* ---- 可视化输出 ---- */
    module_begin("可视化输出 (dump)");
    test_dump_output();
    test_dump_dot_output();
    test_dump_null();
    module_end();

    /* ---- 边界和安全性 ---- */
    module_begin("边界和安全性");
    test_edge_cases();
    test_special_chars();
    module_end();

    /* ---- 总结果 ---- */
    printf("\n==================================================\n");
    if (g_failures == 0) {
        printf("  测试全部通过！\n");
    } else {
        printf("  测试存在失败\n");
    }
    printf("  总计: %3d 项\n", g_passes + g_failures);
    printf("  通过: %3d 项\n", g_passes);
    if (g_failures > 0) {
        printf("  失败: %3d 项\n", g_failures);
    }
    printf("==================================================\n");

    return g_failures > 0 ? 1 : 0;
}
