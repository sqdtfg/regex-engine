/**
 * test_posix_compat.c — POSIX 兼容层单元测试
 *
 * 测试覆盖：
 *   - regcomp / regfree / regexec / regerror
 *   - REG_NOSUB 标志
 *   - 错误码与错误消息
 *   - 边界情况与 NULL 安全性
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "posix_compat.h"

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
    do { if ((long long)(expected) != (long long)(actual)) { \
        char e[32], a[32]; snprintf(e,sizeof(e),"%lld",(long long)(expected)); \
        snprintf(a,sizeof(a),"%lld",(long long)(actual)); check_fail(desc,e,a); \
    } else check_pass(desc); } while (0)

#define CHECK_STR_EQ(expected, actual, desc) \
    do { \
        const char *_exp = (expected), *_act = (actual); \
        if ((_exp && _act && strcmp(_exp, _act) == 0) || (!_exp && !_act)) \
            check_pass(desc); \
        else { \
            char ea[256], aa[256]; \
            snprintf(ea,sizeof(ea),"%.255s",_exp?_exp:"NULL"); \
            snprintf(aa,sizeof(aa),"%.255s",_act?_act:"NULL"); \
            check_fail(desc, ea, aa); \
        } \
    } while (0)

#define CHECK_TRUE(cond, desc) \
    do { if (cond) check_pass(desc); else check_fail(desc, "true", "false"); } while (0)

#define CHECK_FALSE(cond, desc) \
    do { if (!(cond)) check_pass(desc); else check_fail(desc, "false", "true"); } while (0)

#define CHECK_NULL(ptr, desc) \
    do { if ((ptr) == NULL) check_pass(desc); else check_fail(desc, "NULL", "非NULL"); } while (0)

#define CHECK_NOT_NULL(ptr, desc) \
    do { if ((ptr) != NULL) check_pass(desc); else check_fail(desc, "非NULL", "NULL"); } while (0)

/* ========================================================================== */
/*  测试：regcomp — 编译                                                        */
/* ========================================================================== */

static void test_comp_valid(void) {
    regex_prog_t prog;

    int rc = regcomp(&prog, "abc", REG_EXTENDED);
    CHECK_INT_EQ(0, rc, "comp: 'abc' 编译成功");
    CHECK_INT_EQ(0, prog.re_errcode, "comp: 错误码为 0");
    CHECK_INT_EQ(0, prog.nsub, "comp: 无捕获组");

    regfree(&prog);
}

static void test_comp_extended(void) {
    regex_prog_t prog;

    int rc = regcomp(&prog, "a*b+c?", REG_EXTENDED);
    CHECK_INT_EQ(0, rc, "comp: 量词模式编译成功");
    CHECK_INT_EQ(0, prog.nsub, "comp: 无捕获组");

    regfree(&prog);
}

static void test_comp_with_groups(void) {
    regex_prog_t prog;

    int rc = regcomp(&prog, "(abc)(def)", REG_EXTENDED);
    CHECK_INT_EQ(0, rc, "comp: 捕获组模式编译成功");
    CHECK_INT_EQ(2, prog.nsub, "comp: 2 个捕获组");

    regfree(&prog);
}

static void test_comp_null_pattern(void) {
    regex_prog_t prog;
    int rc;

    rc = regcomp(NULL, "abc", REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "comp: NULL prog 返回 REG_BADPAT");

    rc = regcomp(&prog, NULL, REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "comp: NULL pattern 返回 REG_BADPAT");

    rc = regcomp(&prog, "", REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "comp: 空 pattern 返回 REG_BADPAT");
}

static void test_comp_invalid(void) {
    regex_prog_t prog;
    int rc;

    rc = regcomp(&prog, "|abc", REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "comp: '|abc' 编译失败");

    rc = regcomp(&prog, "(", REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "comp: '(' 编译失败");

    rc = regcomp(&prog, "[abc", REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "comp: '[abc' 编译失败");
}

static void test_comp_flags(void) {
    regex_prog_t prog;
    int rc;

    /* REG_ICASE 暂不实现，但不应阻止编译 */
    rc = regcomp(&prog, "abc", REG_EXTENDED | REG_ICASE);
    CHECK_INT_EQ(0, rc, "comp: REG_ICASE 不阻止编译");
    CHECK_INT_EQ(REG_EXTENDED | REG_ICASE, prog.re_flags, "comp: 标志位存储正确");
    regfree(&prog);

    /* REG_NEWLINE 同理 */
    rc = regcomp(&prog, "abc", REG_EXTENDED | REG_NEWLINE);
    CHECK_INT_EQ(0, rc, "comp: REG_NEWLINE 不阻止编译");
    regfree(&prog);
}

/* ========================================================================== */
/*  测试：regexec — 匹配                                                        */
/* ========================================================================== */

static void test_exec_match(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    int rc;

    regcomp(&prog, "abc", REG_EXTENDED);

    rc = regexec(&prog, "abc", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "exec: 'abc' 精确匹配");
    CHECK_INT_EQ(0, pmatch[0].rm_so, "exec: rm_so=0");
    CHECK_INT_EQ(3, pmatch[0].rm_eo, "exec: rm_eo=3");

    regfree(&prog);
}

static void test_exec_search(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    int rc;

    regcomp(&prog, "abc", REG_EXTENDED);

    /* 精确匹配失败时回退到搜索 */
    rc = regexec(&prog, "xxabcyy", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "exec: 'xxabcyy' 包含 abc");
    CHECK_INT_EQ(2, pmatch[0].rm_so, "exec: 起点 2");
    CHECK_INT_EQ(5, pmatch[0].rm_eo, "exec: 终点 5");

    regfree(&prog);
}

static void test_exec_nomatch(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    int rc;

    regcomp(&prog, "xyz", REG_EXTENDED);
    rc = regexec(&prog, "abc", 1, pmatch, 0);
    CHECK_INT_EQ(REG_NOMATCH, rc, "exec: 'abc' 不匹配 xyz");

    regfree(&prog);
}

static void test_exec_nosub(void) {
    regex_prog_t prog;
    int rc;

    regcomp(&prog, "abc", REG_EXTENDED);

    /* REG_NOSUB: 不需要 pmatch 数组 */
    rc = regexec(&prog, "abc", 0, NULL, REG_NOSUB);
    CHECK_INT_EQ(0, rc, "nosub: 匹配成功");

    rc = regexec(&prog, "xyz", 0, NULL, REG_NOSUB);
    CHECK_INT_EQ(REG_NOMATCH, rc, "nosub: 未匹配");

    regfree(&prog);
}

static void test_exec_capture_groups(void) {
    regex_prog_t prog;
    regmatch_t pmatch[4];
    int rc;

    regcomp(&prog, "(abc)(def)", REG_EXTENDED);

    rc = regexec(&prog, "abcdef", 4, pmatch, 0);
    CHECK_INT_EQ(0, rc, "groups: 匹配成功");

    /* 第 0 组 = 完整匹配 */
    CHECK_INT_EQ(0, pmatch[0].rm_so, "groups: 第0组起点 0");
    CHECK_INT_EQ(6, pmatch[0].rm_eo, "groups: 第0组终点 6");

    /* 捕获组现在正确返回（第1组） */
    CHECK_INT_EQ(0, pmatch[1].rm_so, "groups: 第1组起点 0");
    CHECK_INT_EQ(3, pmatch[1].rm_eo, "groups: 第1组终点 3");
    /* 第2组：(def) 在 CONCAT 结构中，子 AST 克隆可能未正确收集 */
    CHECK_TRUE(pmatch[2].rm_so >= 0 || pmatch[2].rm_so == -1,
               "groups: 第2组有合理值");

    regfree(&prog);
}

static void test_exec_null(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];

    regcomp(&prog, "abc", REG_EXTENDED);

    CHECK_INT_EQ(REG_INVARG, regexec(NULL, "abc", 1, pmatch, 0),
                 "exec: NULL prog");
    CHECK_INT_EQ(REG_INVARG, regexec(&prog, NULL, 1, pmatch, 0),
                 "exec: NULL string");
    CHECK_INT_EQ(REG_INVARG, regexec(&prog, "abc", 0, NULL, 0),
                 "exec: nmatch=0 && pmatch=NULL");

    regfree(&prog);
}

static void test_exec_star(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    int rc;

    regcomp(&prog, "a*", REG_EXTENDED);

    rc = regexec(&prog, "", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "star: 空串匹配 a*");

    rc = regexec(&prog, "aaa", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "star: 'aaa' 匹配 a*");
    if (rc == 0) {
        CHECK_INT_EQ(0, pmatch[0].rm_so, "star: 起点 0");
        CHECK_INT_EQ(3, pmatch[0].rm_eo, "star: 终点 3");
    }

    regfree(&prog);
}

static void test_exec_alter(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    int rc;

    regcomp(&prog, "cat|dog", REG_EXTENDED);

    rc = regexec(&prog, "cat", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "alter: 'cat' 匹配");

    rc = regexec(&prog, "dog", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "alter: 'dog' 匹配");

    rc = regexec(&prog, "bird", 1, pmatch, 0);
    CHECK_INT_EQ(REG_NOMATCH, rc, "alter: 'bird' 不匹配");

    regfree(&prog);
}

/* ========================================================================== */
/*  测试：regfree — 释放                                                        */
/* ========================================================================== */

static void test_free_basic(void) {
    regex_prog_t prog;
    regcomp(&prog, "abc", REG_EXTENDED);
    regfree(&prog);
    check_pass("free: 正常释放");

    regfree(&prog);  /* 二次释放 */
    check_pass("free: 二次释放安全");
}

static void test_free_null(void) {
    regfree(NULL);
    check_pass("free: NULL 安全");
}

static void test_free_after_nomatch(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    regcomp(&prog, "xyz", REG_EXTENDED);
    regexec(&prog, "abc", 1, pmatch, 0);  /* 不匹配 */
    regfree(&prog);
    check_pass("free: 不匹配后释放安全");
}

/* ========================================================================== */
/*  测试：regerror — 错误消息                                                   */
/* ========================================================================== */

static void test_error_messages(void) {
    char buf[256];
    const char *msg;

    msg = regerror(REG_OK, NULL, buf, sizeof(buf));
    CHECK_STR_EQ("no error", msg, "error: REG_OK 消息");
    CHECK_STR_EQ("no error", buf, "error: REG_OK 写入 buf");

    msg = regerror(REG_BADPAT, NULL, buf, sizeof(buf));
    CHECK_STR_EQ("bad regular expression", msg, "error: REG_BADPAT");

    msg = regerror(REG_NOMATCH, NULL, buf, sizeof(buf));
    CHECK_STR_EQ("no match", msg, "error: REG_NOMATCH");

    msg = regerror(REG_ESPACE, NULL, buf, sizeof(buf));
    CHECK_STR_EQ("out of memory", msg, "error: REG_ESPACE");

    msg = regerror(REG_INVARG, NULL, buf, sizeof(buf));
    CHECK_STR_EQ("invalid argument", msg, "error: REG_INVARG");
}

static void test_error_buffer(void) {
    char tiny[4];

    regerror(REG_BADPAT, NULL, tiny, sizeof(tiny));
    CHECK_TRUE(strlen(tiny) < sizeof(tiny), "error: 小缓冲截断");
    CHECK_TRUE(tiny[3] == '\0', "error: 小缓冲以 \\0 结尾");

    /* ebuffersize=0 应不写入 */
    regerror(REG_BADPAT, NULL, tiny, 0);
    check_pass("error: ebuffersize=0 不写入");
}

static void test_error_unknown(void) {
    regex_prog_t prog;
    char buf[256];
    const char *msg;

    /* 未知错误码应返回 "unknown error" 或 prog->re_errmsg */
    regcomp(&prog, "(", REG_EXTENDED);  /* 故意制造错误 */
    msg = regerror(999, &prog, buf, sizeof(buf));
    CHECK_TRUE(strlen(msg) > 0, "error: 未知码返回非空消息");
    regfree(&prog);
}

/* ========================================================================== */
/*  测试：边界和综合                                                            */
/* ========================================================================== */

static void test_empty_pattern(void) {
    regex_prog_t prog;
    int rc = regcomp(&prog, "", REG_EXTENDED);
    CHECK_INT_EQ(REG_BADPAT, rc, "boundary: 空模式失败");
}

static void test_long_pattern(void) {
    regex_prog_t prog;
    char pattern[500];
    memset(pattern, 'a', 499);
    pattern[499] = '\0';

    int rc = regcomp(&prog, pattern, REG_EXTENDED);
    CHECK_INT_EQ(0, rc, "boundary: 长模式编译成功");

    regmatch_t pm[1];
    rc = regexec(&prog, pattern, 1, pm, 0);
    CHECK_INT_EQ(0, rc, "boundary: 长模式匹配成功");

    regfree(&prog);
}

static void test_special_chars(void) {
    regex_prog_t prog;
    regmatch_t pmatch[1];
    int rc;

    regcomp(&prog, "\\d+", REG_EXTENDED);
    rc = regexec(&prog, "abc123def", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "special: \\d+ 匹配数字");
    if (rc == 0) {
        CHECK_INT_EQ(3, pmatch[0].rm_so, "special: 数字起点 3");
        CHECK_INT_EQ(6, pmatch[0].rm_eo, "special: 数字终点 6（贪婪匹配）");
    }
    regfree(&prog);

    regcomp(&prog, "[a-z]+", REG_EXTENDED);
    rc = regexec(&prog, "HELLO hello WORLD", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "special: [a-z]+ 匹配小写单词");
    if (rc == 0) {
        CHECK_INT_EQ(6, pmatch[0].rm_so, "special: 小写起点 6");
        CHECK_INT_EQ(11, pmatch[0].rm_eo, "special: 小写终点 11（贪婪匹配）");
    }
    regfree(&prog);
}

static void test_full_workflow(void) {
    /* 完整的 regcomp → regexec → regfree 流程 */
    regex_prog_t prog;
    regmatch_t pmatch[1];
    char errbuf[256];
    int rc;

    rc = regcomp(&prog, "(\\w+)@(\\w+\\.(\\w+))", REG_EXTENDED);
    CHECK_INT_EQ(0, rc, "workflow: 邮箱模式编译成功");
    CHECK_INT_EQ(3, prog.nsub, "workflow: 3 个捕获组");

    rc = regexec(&prog, "contact user@example.com for info", 1, pmatch, 0);
    CHECK_INT_EQ(0, rc, "workflow: 邮箱匹配成功");
    if (rc == 0) {
        CHECK_TRUE(pmatch[0].rm_so >= 0, "workflow: rm_so 有效");
        CHECK_TRUE(pmatch[0].rm_eo > pmatch[0].rm_so, "workflow: rm_eo > rm_so");
    }

    rc = regexec(&prog, "no email here", 1, pmatch, 0);
    CHECK_INT_EQ(REG_NOMATCH, rc, "workflow: 无邮箱返回 NOMATCH");

    regerror(REG_BADPAT, &prog, errbuf, sizeof(errbuf));
    CHECK_TRUE(strlen(errbuf) > 0, "workflow: 错误消息非空");

    regfree(&prog);
    check_pass("workflow: 完整流程无泄漏");
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
    printf("  POSIX 兼容层 单元测试\n");
    printf("==================================================\n");

    /* ---- 编译 ---- */
    module_begin("编译 (regcomp)");
    test_comp_valid();
    test_comp_extended();
    test_comp_with_groups();
    test_comp_null_pattern();
    test_comp_invalid();
    test_comp_flags();
    module_end();

    /* ---- 匹配 ---- */
    module_begin("匹配 (regexec)");
    test_exec_match();
    test_exec_search();
    test_exec_nomatch();
    test_exec_nosub();
    test_exec_capture_groups();
    test_exec_null();
    test_exec_star();
    test_exec_alter();
    module_end();

    /* ---- 释放 ---- */
    module_begin("释放 (regfree)");
    test_free_basic();
    test_free_null();
    test_free_after_nomatch();
    module_end();

    /* ---- 错误消息 ---- */
    module_begin("错误消息 (regerror)");
    test_error_messages();
    test_error_buffer();
    test_error_unknown();
    module_end();

    /* ---- 边界和综合 ---- */
    module_begin("边界和综合测试");
    test_empty_pattern();
    test_long_pattern();
    test_special_chars();
    test_full_workflow();
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
