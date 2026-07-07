/**
 * test_grep.c — Grep 测试集适配
 *
 * 功能：
 *   - 解析测试用例文件
 *   - 执行正则匹配测试
 *   - 验证结果并生成报告
 *
 * 测试用例文件格式：
 *   # 注释行
 *   pattern text expected_result [start] [end]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "api.h"

/* ========================================================================== */
/*  测试用例结构                                                               */
/* ========================================================================== */

typedef struct {
    char *pattern;
    char *text;
    int expected_match;
    size_t expected_start;
    size_t expected_end;
    int line_num;
} GrepTestCase;

typedef struct {
    GrepTestCase *cases;
    int count;
    int capacity;
} GrepTestSuite;

/* ========================================================================== */
/*  测试统计                                                                   */
/* ========================================================================== */

static int g_total = 0;
static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

/* ========================================================================== */
/*  工具函数                                                                   */
/* ========================================================================== */

static char *trim(char *str) {
    char *end;

    while (isspace((unsigned char)*str)) str++;

    if (*str == 0)
        return str;

    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;

    end[1] = '\0';
    return str;
}

static void init_suite(GrepTestSuite *suite) {
    suite->capacity = 64;
    suite->count = 0;
    suite->cases = (GrepTestCase *)malloc(sizeof(GrepTestCase) * suite->capacity);
}

static void free_suite(GrepTestSuite *suite) {
    if (!suite) return;
    for (int i = 0; i < suite->count; i++) {
        free(suite->cases[i].pattern);
        free(suite->cases[i].text);
    }
    free(suite->cases);
    suite->cases = NULL;
    suite->count = 0;
    suite->capacity = 0;
}

static void add_test_case(GrepTestSuite *suite, const char *pattern, const char *text,
                          int expected_match, size_t expected_start, size_t expected_end,
                          int line_num) {
    if (suite->count >= suite->capacity) {
        suite->capacity *= 2;
        suite->cases = (GrepTestCase *)realloc(suite->cases, sizeof(GrepTestCase) * suite->capacity);
    }

    GrepTestCase *tc = &suite->cases[suite->count];
    tc->pattern = strdup(pattern);
    tc->text = strdup(text);
    tc->expected_match = expected_match;
    tc->expected_start = expected_start;
    tc->expected_end = expected_end;
    tc->line_num = line_num;
    suite->count++;
}

/* ========================================================================== */
/*  解析测试用例文件                                                           */
/* ========================================================================== */

static int parse_test_file(const char *filename, GrepTestSuite *suite) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        printf("  ERROR: 无法打开文件 %s\n", filename);
        return -1;
    }

    char line[1024];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char *trimmed = trim(line);

        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }

        char pattern[256];
        char text[256];
        int expected_match;
        size_t expected_start = 0;
        size_t expected_end = 0;

        int num_fields = sscanf(trimmed, "%255s %255s %d %zu %zu",
                                pattern, text, &expected_match, &expected_start, &expected_end);

        if (num_fields >= 3) {
            add_test_case(suite, pattern, text, expected_match, expected_start, expected_end, line_num);
        } else {
            printf("  WARNING: 第 %d 行格式错误，跳过\n", line_num);
        }
    }

    fclose(fp);
    return suite->count;
}

/* ========================================================================== */
/*  执行单个测试用例                                                           */
/* ========================================================================== */

static int run_test_case(const GrepTestCase *tc) {
    regex_t *prog = regex_compile(tc->pattern, REGEX_FLAG_NONE);
    if (!prog) {
        printf("  FAIL: 第 %d 行 - 编译失败: %s\n", tc->line_num, tc->pattern);
        g_failed++;
        return 0;
    }

    MatchResult result;
    int matched = regex_search(prog, tc->text, &result);

    int pass = 1;
    if (matched != tc->expected_match) {
        pass = 0;
    } else if (matched) {
        if (result.start != tc->expected_start || result.end != tc->expected_end) {
            pass = 0;
        }
    }

    if (pass) {
        g_passed++;
        printf("  PASS: 第 %d 行 - /%s/ 在 \"%s\"\n", tc->line_num, tc->pattern, tc->text);
    } else {
        g_failed++;
        if (matched) {
            printf("  FAIL: 第 %d 行 - /%s/ 在 \"%s\"\n", tc->line_num, tc->pattern, tc->text);
            printf("        期望: %s [%zu-%zu]\n",
                   tc->expected_match ? "匹配" : "不匹配",
                   tc->expected_start, tc->expected_end);
            printf("        实际: 匹配 [%zu-%zu]\n", result.start, result.end);
        } else {
            printf("  FAIL: 第 %d 行 - /%s/ 在 \"%s\"\n", tc->line_num, tc->pattern, tc->text);
            printf("        期望: %s [%zu-%zu]\n",
                   tc->expected_match ? "匹配" : "不匹配",
                   tc->expected_start, tc->expected_end);
            printf("        实际: 不匹配\n");
        }
    }

    regex_free(prog);
    g_total++;
    return pass;
}

/* ========================================================================== */
/*  执行测试套件                                                               */
/* ========================================================================== */

static void run_test_suite(const char *filename) {
    printf("\n==================================================\n");
    printf("  测试文件: %s\n", filename);
    printf("==================================================\n");

    GrepTestSuite suite;
    init_suite(&suite);

    int num_tests = parse_test_file(filename, &suite);
    if (num_tests <= 0) {
        printf("  没有测试用例\n");
        free_suite(&suite);
        return;
    }

    printf("  加载了 %d 个测试用例\n", num_tests);

    for (int i = 0; i < suite.count; i++) {
        run_test_case(&suite.cases[i]);
    }

    free_suite(&suite);
}

/* ========================================================================== */
/*  主函数                                                                     */
/* ========================================================================== */

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    printf("==================================================\n");
    printf("  Grep 测试集适配\n");
    printf("==================================================\n");

    if (argc < 2) {
        printf("\n用法: %s <test_file1> [test_file2] ...\n", argv[0]);
        printf("示例: %s tests/grep_tests/basic.txt\n", argv[0]);
        return 1;
    }

    for (int i = 1; i < argc; i++) {
        run_test_suite(argv[i]);
    }

    printf("\n==================================================\n");
    printf("  测试总结\n");
    printf("==================================================\n");
    printf("  总计:   %d 个测试\n", g_total);
    printf("  通过:   %d 个\n", g_passed);
    printf("  失败:   %d 个\n", g_failed);
    printf("  跳过:   %d 个\n", g_skipped);
    printf("  通过率: %.1f%%\n", g_total > 0 ? (float)g_passed / g_total * 100 : 0.0);
    printf("==================================================\n");

    return g_failed > 0 ? 1 : 0;
}
