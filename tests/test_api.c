/**
 * test_api.c — 高级 API 单元测试
 *
 * 测试覆盖：
 *   - regex_compile / regex_free
 *   - regex_match / regex_search
 *   - regex_findall
 *   - regex_error
 *   - 错误处理路径
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "api.h"

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
        char e[32], a[32]; snprintf(e,sizeof(e),"%d",(expected)); \
        snprintf(a,sizeof(a),"%d",(actual)); check_fail(desc,e,a); \
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

#define CHECK_NULL(ptr, desc) \
    do { if ((ptr) == NULL) check_pass(desc); else check_fail(desc, "NULL", "非NULL"); } while (0)

#define CHECK_NOT_NULL(ptr, desc) \
    do { if ((ptr) != NULL) check_pass(desc); else check_fail(desc, "非NULL", "NULL"); } while (0)

/* ========================================================================== */
/*  测试：regex_compile / regex_free                                            */
/* ========================================================================== */

static void test_compile_valid(void) {
    regex_t *prog;

    prog = regex_compile("abc", REGEX_FLAG_NONE);
    CHECK_NOT_NULL(prog, "compile: 简单模式成功");
    if (prog) {
        CHECK_INT_EQ(0, prog->error_code, "compile: 错误码为 0");
        CHECK_TRUE(strcmp(prog->pattern, "abc") == 0, "compile: pattern 存储正确");
        regex_free(prog);
    }

    prog = regex_compile("a*b+c?", REGEX_FLAG_NONE);
    CHECK_NOT_NULL(prog, "compile: 量词模式成功");
    if (prog) regex_free(prog);

    prog = regex_compile("(abc|def)+", REGEX_FLAG_NONE);
    CHECK_NOT_NULL(prog, "compile: 捕获组+量词成功");
    if (prog) regex_free(prog);
}

static void test_compile_null(void) {
    regex_t *prog = regex_compile(NULL, REGEX_FLAG_NONE);
    CHECK_NULL(prog, "compile: NULL pattern 返回 NULL");
}

static void test_compile_invalid(void) {
    regex_t *prog;

    prog = regex_compile("|abc", REGEX_FLAG_NONE);
    CHECK_NULL(prog, "compile: 非法模式返回 NULL");

    prog = regex_compile("(", REGEX_FLAG_NONE);
    CHECK_NULL(prog, "compile: 未闭合括号返回 NULL");

    prog = regex_compile("[abc", REGEX_FLAG_NONE);
    CHECK_NULL(prog, "compile: 未闭合字符组返回 NULL");

    prog = regex_compile("", REGEX_FLAG_NONE);
    CHECK_NULL(prog, "compile: 空模式返回 NULL");
}

static void test_free_safety(void) {
    regex_t *prog = regex_compile("a", REGEX_FLAG_NONE);
    regex_free(prog);
    regex_free(NULL);
    check_pass("free: NULL 安全");
}

/* ========================================================================== */
/*  测试：regex_match — 精确匹配                                                */
/* ========================================================================== */

static void test_match_exact(void) {
    regex_t *prog = regex_compile("abc", REGEX_FLAG_NONE);
    MatchResult r;
    int ret;

    ret = regex_match(prog, "abc", &r);
    CHECK_INT_EQ(1, ret, "match: 'abc' 匹配 'abc'");
    if (ret) {
        CHECK_SIZE_T_EQ(0, r.start, "match: 起点 0");
        CHECK_SIZE_T_EQ(3, r.end, "match: 终点 3");
        CHECK_SIZE_T_EQ(3, r.length, "match: 长度 3");
    }

    ret = regex_match(prog, "abcd", &r);
    CHECK_INT_EQ(0, ret, "match: 'abcd' 不匹配");

    ret = regex_match(prog, "ab", &r);
    CHECK_INT_EQ(0, ret, "match: 'ab' 不匹配");

    regex_free(prog);
}

static void test_match_star(void) {
    regex_t *prog = regex_compile("a*", REGEX_FLAG_NONE);
    MatchResult r;

    CHECK_INT_EQ(1, regex_match(prog, "", &r), "a*: 空串匹配");
    CHECK_INT_EQ(1, regex_match(prog, "aaa", &r), "a*: 'aaa' 匹配");
    CHECK_INT_EQ(0, regex_match(prog, "aab", &r), "a*: 'aab' 不匹配");

    regex_free(prog);
}

static void test_match_null(void) {
    regex_t *prog = regex_compile("a", REGEX_FLAG_NONE);
    MatchResult r;

    CHECK_INT_EQ(0, regex_match(NULL, "a", &r), "match: NULL prog 返回 0");
    CHECK_INT_EQ(0, regex_match(prog, NULL, &r), "match: NULL text 返回 0");

    regex_free(prog);
}

/* ========================================================================== */
/*  测试：regex_search — 子串匹配                                               */
/* ========================================================================== */

static void test_search_basic(void) {
    regex_t *prog = regex_compile("abc", REGEX_FLAG_NONE);
    MatchResult r;
    int ret;

    ret = regex_search(prog, "xxabcyy", &r);
    CHECK_INT_EQ(1, ret, "search: 'xxabcyy' 包含 abc");
    if (ret) {
        CHECK_SIZE_T_EQ(2, r.start, "search: 起点 2");
        CHECK_SIZE_T_EQ(5, r.end, "search: 终点 5");
        CHECK_SIZE_T_EQ(3, r.length, "search: 长度 3");
    }

    ret = regex_search(prog, "xyz", &r);
    CHECK_INT_EQ(0, ret, "search: 'xyz' 不含 abc");

    regex_free(prog);
}

static void test_search_star(void) {
    regex_t *prog = regex_compile("a+", REGEX_FLAG_NONE);
    MatchResult r;

    CHECK_INT_EQ(1, regex_search(prog, "bbbaaa", &r), "a+: 'bbbaaa' 含 a+");
    if (r.matched) {
        CHECK_SIZE_T_EQ(3, r.start, "a+: 起点 3");
    }

    CHECK_INT_EQ(0, regex_search(prog, "bbb", &r), "a+: 'bbb' 不含 a+");

    regex_free(prog);
}

static void test_search_empty(void) {
    regex_t *prog = regex_compile("a*", REGEX_FLAG_NONE);
    MatchResult r;

    /* a* 可匹配空串，在 "bbb" 中应从位置 0 匹配长度为 0 */
    CHECK_INT_EQ(1, regex_search(prog, "bbb", &r), "a*: 'bbb' 含 a*（空）");

    regex_free(prog);
}

/* ========================================================================== */
/*  测试：regex_findall — 全局匹配                                              */
/* ========================================================================== */

static void test_findall_basic(void) {
    regex_t *prog = regex_compile("a", REGEX_FLAG_NONE);
    int count;
    MatchResult *results;

    results = regex_findall(prog, "abracadabra", &count);
    CHECK_NOT_NULL(results, "findall: 返回非 NULL");
    CHECK_INT_EQ(5, count, "findall: 5 个 'a'");

    if (results) {
        CHECK_SIZE_T_EQ(0, results[0].start, "findall: 第1个在 0");
        CHECK_SIZE_T_EQ(3, results[1].start, "findall: 第2个在 3");
        CHECK_SIZE_T_EQ(10, results[4].start, "findall: 第5个在 10");
    }
    regex_findall_free(results);
    regex_free(prog);
}

static void test_findall_no_match(void) {
    regex_t *prog = regex_compile("xyz", REGEX_FLAG_NONE);
    int count;
    MatchResult *results = regex_findall(prog, "abc", &count);

    CHECK_NULL(results, "findall: 无匹配返回 NULL");
    CHECK_INT_EQ(0, count, "findall: count=0");

    regex_free(prog);
}

static void test_findall_null(void) {
    int count;
    regex_t *prog_a;

    CHECK_NULL(regex_findall(NULL, "abc", &count), "findall: NULL prog");

    prog_a = regex_compile("a", REGEX_FLAG_NONE);
    CHECK_NULL(regex_findall(prog_a, NULL, &count), "findall: NULL text");
    regex_free(prog_a);

    prog_a = regex_compile("a", REGEX_FLAG_NONE);
    CHECK_NULL(regex_findall(prog_a, "abc", NULL), "findall: NULL count");
    regex_free(prog_a);
}

static void test_findall_multiple(void) {
    regex_t *prog = regex_compile("[aeiou]", REGEX_FLAG_NONE);
    int count;
    MatchResult *results;

    results = regex_findall(prog, "hello world", &count);
    CHECK_NOT_NULL(results, "findall: 元音匹配");
    CHECK_INT_EQ(3, count, "findall: 'hello world' 有 3 个元音");
    regex_findall_free(results);
    regex_free(prog);
}

/* ========================================================================== */
/*  测试：regex_error — 错误消息                                                */
/* ========================================================================== */

static void test_error_messages(void) {
    CHECK_TRUE(strlen(regex_error(REGEX_OK)) > 0, "error: REGEX_OK 有消息");
    CHECK_TRUE(strlen(regex_error(REGEX_ERR_NULL_ARGUMENT)) > 0, "error: NULL_ARGUMENT 有消息");
    CHECK_TRUE(strlen(regex_error(REGEX_ERR_NO_MEMORY)) > 0, "error: NO_MEMORY 有消息");
    CHECK_TRUE(strlen(regex_error(REGEX_ERR_PARSE)) > 0, "error: PARSE 有消息");
    CHECK_TRUE(strlen(regex_error(REGEX_ERR_NFA_BUILD)) > 0, "error: NFA_BUILD 有消息");
    CHECK_TRUE(strlen(regex_error(REGEX_ERR_DFA_BUILD)) > 0, "error: DFA_BUILD 有消息");
    CHECK_TRUE(strlen(regex_error(REGEX_ERR_TOO_MANY_MATCHES)) > 0, "error: TOO_MANY 有消息");
    CHECK_TRUE(strlen(regex_error(999)) > 0, "error: 未知码有消息");
}

static void test_error_in_program(void) {
    regex_t *prog = regex_compile("|invalid", REGEX_FLAG_NONE);
    CHECK_NULL(prog, "error: 非法模式编译失败");
    /* prog 为 NULL，无法检查 error_msg */
}

/* ========================================================================== */
/*  测试：复杂模式                                                              */
/* ========================================================================== */

static void test_complex_patterns(void) {
    regex_t *prog;
    MatchResult r;
    int ret;

    /* 邮箱模式（简化版）：word@word.word */
    prog = regex_compile("\\w+@\\w+\\.\\w+", REGEX_FLAG_NONE);
    CHECK_NOT_NULL(prog, "complex: 邮箱模式编译成功");
    if (prog) {
        ret = regex_search(prog, "user@example.com", &r);
        CHECK_INT_EQ(1, ret, "complex: 邮箱匹配");
        if (ret) {
            CHECK_SIZE_T_EQ(0, r.start, "complex: 起点 0");
            CHECK_SIZE_T_EQ(14, r.end, "complex: 终点 14");
        }
        regex_free(prog);
    }

    /* 数字序列 */
    prog = regex_compile("\\d{3}-\\d{4}", REGEX_FLAG_NONE);
    CHECK_NOT_NULL(prog, "complex: 电话号码模式编译成功");
    if (prog) {
        ret = regex_search(prog, "call 123-4567 now", &r);
        CHECK_INT_EQ(1, ret, "complex: 电话匹配");
        if (ret) {
            CHECK_SIZE_T_EQ(5, r.start, "complex: 电话起点 5");
            CHECK_SIZE_T_EQ(13, r.end, "complex: 电话终点 13");
        }
        regex_free(prog);
    }

    /* 单词边界模拟：[a-z]+ */
    prog = regex_compile("[a-z]+", REGEX_FLAG_NONE);
    CHECK_NOT_NULL(prog, "complex: 单词模式编译成功");
    if (prog) {
        ret = regex_match(prog, "hello", &r);
        CHECK_INT_EQ(1, ret, "complex: hello 匹配");
        regex_free(prog);
    }
}

/* ========================================================================== */
/*  测试：匹配结果结构完整性                                                    */
/* ========================================================================== */

static void test_result_struct(void) {
    regex_t *prog = regex_compile("abc", REGEX_FLAG_NONE);
    MatchResult r = {0};

    /* 匹配成功时所有字段应有效 */
    regex_match(prog, "abc", &r);
    CHECK_TRUE(r.matched, "struct: matched=1");
    CHECK_SIZE_T_EQ(0, r.start, "struct: start=0");
    CHECK_SIZE_T_EQ(3, r.end, "struct: end=3");
    CHECK_SIZE_T_EQ(3, r.length, "struct: length=3");

    /* 匹配失败时应清零 */
    r.matched = 999;  /* 故意污染 */
    regex_match(prog, "xyz", &r);
    CHECK_INT_EQ(0, r.matched, "struct: 失败时 matched=0");

    regex_free(prog);
}

/* ========================================================================== */
/*  主函数                                                                      */
/* ========================================================================== */

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    printf("==================================================\n");
    printf("  高级 API 单元测试\n");
    printf("==================================================\n");

    /* ---- 编译/释放 ---- */
    module_begin("编译与释放 (compile/free)");
    test_compile_valid();
    test_compile_null();
    test_compile_invalid();
    test_free_safety();
    module_end();

    /* ---- 精确匹配 ---- */
    module_begin("精确匹配 (regex_match)");
    test_match_exact();
    test_match_star();
    test_match_null();
    module_end();

    /* ---- 子串匹配 ---- */
    module_begin("子串匹配 (regex_search)");
    test_search_basic();
    test_search_star();
    test_search_empty();
    module_end();

    /* ---- 全局匹配 ---- */
    module_begin("全局匹配 (regex_findall)");
    test_findall_basic();
    test_findall_no_match();
    test_findall_null();
    test_findall_multiple();
    module_end();

    /* ---- 错误消息 ---- */
    module_begin("错误消息 (regex_error)");
    test_error_messages();
    test_error_in_program();
    module_end();

    /* ---- 复杂模式 ---- */
    module_begin("复杂模式");
    test_complex_patterns();
    module_end();

    /* ---- 结果结构 ---- */
    module_begin("匹配结果结构完整性");
    test_result_struct();
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
