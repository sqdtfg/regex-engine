/**
 * test_capture.c — 捕获组功能单元测试
 *
 * 测试覆盖：
 *   - dfa_from_ast_with_groups  构建带捕获组信息的 DFA
 *   - dfa_match_captured        带捕获组的匹配
 *   - captured_match_free       释放捕获结果
 *   - dfa_capture_free          释放带捕获组的 DFA
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "capture.h"
#include "parser.h"
#include "nfa.h"
#include "dfa.h"

/* ========================================================================== */
/*  测试框架                                                                    */
/* ========================================================================== */

static int g_passes = 0;
static int g_failures = 0;
static int g_module_passes = 0;
static int g_module_failures = 0;

static void module_begin(const char *name) {
    printf("\n==================================================\n");
    printf("  %s\n", name);
    printf("--------------------------------------------------\n");
    g_module_passes = 0;
    g_module_failures = 0;
}

static void module_end(void) {
    printf("--------------------------------------------------\n");
    if (g_module_failures == 0) {
        printf("  结果：all passed (%d tests)\n", g_module_passes);
    } else {
        printf("  结果：passed %d, failed %d\n", g_module_passes, g_module_failures);
    }
}

static void check_pass(const char *desc) {
    printf("  PASS: %s\n", desc);
    g_passes++;
    g_module_passes++;
}

static void check_fail(const char *desc, const char *expected, const char *actual) {
    printf("  FAIL: %s — expected '%s', got '%s'\n", desc, expected, actual);
    g_failures++;
    g_module_failures++;
}

#define CHECK_INT_EQ(expected, actual, desc) \
    do { if ((int)(expected) != (int)(actual)) { \
        char e[32], a[32]; snprintf(e,sizeof(e),"%d",(expected)); \
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

/* ========================================================================== */
/*  辅助函数                                                                    */
/* ========================================================================== */

static DFAMachine build_with_groups(const char *pattern) {
    Parser parser;
    parser_init(&parser, pattern);
    ASTNode *ast = parser_parse(&parser);
    if (!ast) {
        DFAMachine dfa = {0};
        return dfa;
    }
    DFAMachine dfa = dfa_from_ast_with_groups(ast);
    ast_free(ast);
    return dfa;
}

/* ========================================================================== */
/*  测试：基础捕获组                                                            */
/* ========================================================================== */

static void test_simple_group(void) {
    DFAMachine dfa = build_with_groups("(abc)");
    CapturedMatch result;

    CHECK_TRUE(dfa.states != NULL, "group: DFA states not NULL");

    result = dfa_match_captured(&dfa, "xyzabc123");
    CHECK_TRUE(result.matched, "group: match succeeded");
    CHECK_SIZE_T_EQ(3, result.start, "group: full match start 3");
    CHECK_SIZE_T_EQ(6, result.end, "group: full match end 6");
    CHECK_SIZE_T_EQ(3, result.length, "group: full match length 3");
    CHECK_INT_EQ(1, result.group_count, "group: 1 capture group");

    if (result.groups) {
        CHECK_TRUE(result.groups[0].matched, "group: group 0 matched");
        CHECK_SIZE_T_EQ(3, result.groups[0].start, "group: group 0 start 3");
        CHECK_SIZE_T_EQ(6, result.groups[0].end, "group: group 0 end 6");

        CHECK_TRUE(result.groups[1].matched, "group: group 1 matched");
        CHECK_SIZE_T_EQ(3, result.groups[1].start, "group: group 1 start 3");
        CHECK_SIZE_T_EQ(6, result.groups[1].end, "group: group 1 end 6");
        CHECK_SIZE_T_EQ(3, result.groups[1].length, "group: group 1 length 3");
    }

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

static void test_no_group(void) {
    DFAMachine dfa = build_with_groups("abc");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "abc");
    CHECK_TRUE(result.matched, "no-group: match succeeded");
    CHECK_INT_EQ(0, result.group_count, "no-group: 0 capture groups");

    if (result.groups) {
        CHECK_TRUE(result.groups[0].matched, "no-group: group 0 matched");
    }

    captured_match_free(&result);
    dfa_free(&dfa);
}

static void test_group_with_alter(void) {
    DFAMachine dfa = build_with_groups("(cat|dog)");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "the cat sat");
    CHECK_TRUE(result.matched, "alter: match succeeded");
    CHECK_INT_EQ(1, result.group_count, "alter: 1 capture group");

    if (result.groups && result.groups[1].matched) {
        CHECK_SIZE_T_EQ(4, result.groups[1].start, "alter: cat start 4");
        CHECK_SIZE_T_EQ(7, result.groups[1].end, "alter: cat end 7");
    }

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

/* ========================================================================== */
/*  测试：嵌套与重复捕获组                                                      */
/* ========================================================================== */

static void test_nested_groups(void) {
    DFAMachine dfa = build_with_groups("((ab)(cd))");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "xxabcdzz");
    CHECK_TRUE(result.matched, "nested: match succeeded");
    CHECK_INT_EQ(3, result.group_count, "nested: 3 capture groups");

    if (result.groups) {
        /* Group 0: full match "abcd" */
        CHECK_TRUE(result.groups[0].matched, "nested: group 0 matched");
        CHECK_SIZE_T_EQ(2, result.groups[0].start, "nested: group 0 start 2");
        CHECK_SIZE_T_EQ(6, result.groups[0].end, "nested: group 0 end 6");

        /* Group 1: ((ab)(cd)) overall */
        CHECK_TRUE(result.groups[1].matched, "nested: group 1 matched");
        CHECK_SIZE_T_EQ(2, result.groups[1].start, "nested: group 1 start 2");
        CHECK_SIZE_T_EQ(6, result.groups[1].end, "nested: group 1 end 6");
    }

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

static void test_repeated_group(void) {
    DFAMachine dfa = build_with_groups("(a+)");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "xxxaaaayyy");
    CHECK_TRUE(result.matched, "repeated: match succeeded");

    if (result.groups && result.groups[1].matched) {
        CHECK_SIZE_T_EQ(3, result.groups[1].start, "repeated: a+ start 3");
        CHECK_SIZE_T_EQ(7, result.groups[1].end, "repeated: a+ end 7");
    }

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

static void test_group_no_match(void) {
    DFAMachine dfa = build_with_groups("(abc)");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "xyz");
    CHECK_FALSE(result.matched, "nomatch: not matched");
    CHECK_INT_EQ(0, result.group_count, "nomatch: group_count=0");

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

/* ========================================================================== */
/*  测试：量词与捕获组组合                                                      */
/* ========================================================================== */

static void test_star_group(void) {
    DFAMachine dfa = build_with_groups("(a*)");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "bbb");
    /* a* 可匹配空串 */
    if (result.matched && result.groups && result.groups[1].matched) {
        CHECK_SIZE_T_EQ(0, result.groups[1].length, "star: a* empty match length 0");
    }

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

static void test_optional_group(void) {
    DFAMachine dfa = build_with_groups("(ab)?c");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "xc");
    CHECK_TRUE(result.matched, "opt: 'c' matches (ab)?c");

    captured_match_free(&result);
    dfa_capture_free(&dfa);
}

/* ========================================================================== */
/*  测试：释放安全性                                                            */
/* ========================================================================== */

static void test_free_safety(void) {
    DFAMachine dfa = build_with_groups("(abc)");
    CapturedMatch result;

    result = dfa_match_captured(&dfa, "abc");
    captured_match_free(&result);
    check_pass("free: captured_match_free OK");

    dfa_capture_free(&dfa);
    check_pass("free: dfa_capture_free OK");
}

static void test_null_safety(void) {
    CapturedMatch result = {0};
    captured_match_free(NULL);
    check_pass("null: captured_match_free(NULL) safe");

    result = dfa_match_captured(NULL, "abc");
    CHECK_FALSE(result.matched, "null: dfa_match_captured(NULL) returns no match");
    check_pass("null: no crash");

    DFAMachine dfa = build_with_groups("(abc)");
    result = dfa_match_captured(&dfa, NULL);
    CHECK_FALSE(result.matched, "null: text=NULL returns no match");
    dfa_capture_free(&dfa);
}

/* ========================================================================== */
/*  测试：无捕获组时的 groups 指针                                              */
/* ========================================================================== */

static void test_groups_pointer(void) {
    DFAMachine dfa = build_with_groups("abc");
    CapturedMatch result = dfa_match_captured(&dfa, "abc");

    /* No capture groups: groups allocated with 1 element (group 0) */
    CHECK_TRUE(result.groups != NULL, "groups: groups[0] allocated even with 0 groups");
    CHECK_TRUE(result.groups[0].matched, "groups: group 0 always valid");

    captured_match_free(&result);
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
    printf("  Test capture groups unit test\n");
    printf("==================================================\n");

    /* ---- 基础捕获组 ---- */
    module_begin("Basic capture groups");
    test_simple_group();
    test_no_group();
    test_group_with_alter();
    module_end();

    /* ---- 嵌套与重复 ---- */
    module_begin("Nested & repeated groups");
    test_nested_groups();
    test_repeated_group();
    test_group_no_match();
    module_end();

    /* ---- 量词与捕获组组合 ---- */
    module_begin("Quantifiers & groups");
    test_star_group();
    test_optional_group();
    module_end();

    /* ---- 释放安全性 ---- */
    module_begin("Free safety");
    test_free_safety();
    test_null_safety();
    test_groups_pointer();
    module_end();

    /* ---- 总结果 ---- */
    printf("\n==================================================\n");
    if (g_failures == 0) {
        printf("  All tests passed!\n");
    } else {
        printf("  Some tests failed\n");
    }
    printf("  Total: %3d\n", g_passes + g_failures);
    printf("  Passed: %3d\n", g_passes);
    if (g_failures > 0) {
        printf("  Failed: %3d\n", g_failures);
    }
    printf("==================================================\n");

    return g_failures > 0 ? 1 : 0;
}
