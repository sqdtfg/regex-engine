/**
 * grep_test_runner — 使用 Spencer ERE/BRE .tests 格式，通过 POSIX 兼容层验证正则引擎。
 *
 * Spencer 测试格式: status@regex@input[@comment]
 *   status: 0 = 应匹配, 1 = 应不匹配, 2 = 应编译失败
 *
 * 编译模式: bre → regcomp(0),  ere → regcomp(REG_EXTENDED)
 *
 * CMake 自定义目标 "run_grep_tests" 可直接调用，无需手动传参。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "posix_compat.h"

#define MAX_LINE 4096

typedef struct {
    const char *file;
    int total;
    int passed;
    int failed;
    int skipped;
    int xfail;  /* 预期失败（标记为 @TO CORRECT 的行） */
} Stats;

static int is_xfail(const char *comment) {
    return strstr(comment, "TO CORRECT") != NULL;
}

static int is_botch(const char *comment) {
    return strstr(comment, "BOTCH") != NULL;
}

static int run_one_test(const char *status_str, const char *regex,
                         const char *input, const char *comment,
                         int eflags, int is_ere, Stats *s) {
    int expected_status = atoi(status_str);
    int xf = is_xfail(comment);
    int botch = is_botch(comment);

    s->total++;

    /* 编译 */
    regex_prog_t prog;
    int cflags = is_ere ? REG_EXTENDED : 0;
    int rc = regcomp(&prog, regex, cflags);

    /* status=2 表示应编译失败 */
    if (expected_status == 2) {
        if (rc != 0) { s->passed++; return 1; }
        if (xf) { s->xfail++; return 1; }
        if (botch) { s->skipped++; regfree(&prog); return 1; }
        printf("  FAIL #%d: '%s' — 应编译失败但成功了\n", s->total, regex);
        s->failed++;
        regfree(&prog);
        return 0;
    }

    if (rc != 0) {
        if (xf) { s->xfail++; return 1; }
        printf("  FAIL #%d: '%s' — 编译失败: %s\n", s->total, regex, prog.re_errmsg);
        s->failed++;
        return 0;
    }

    /* 执行匹配 */
    regmatch_t pmatch[10];
    int exec_rc = regexec(&prog, input, 10, pmatch, eflags);

    if (expected_status == 0) {
        if (exec_rc == 0) { s->passed++; }
        else {
            if (xf) { s->xfail++; regfree(&prog); return 1; }
            printf("  FAIL #%d: '%s' on '%s' — 应匹配但未匹配\n", s->total, regex, input);
            s->failed++;
        }
    } else {
        if (exec_rc == REG_NOMATCH) { s->passed++; }
        else {
            if (xf) { s->xfail++; regfree(&prog); return 1; }
            printf("  FAIL #%d: '%s' on '%s' — 应不匹配但匹配了\n", s->total, regex, input);
            s->failed++;
        }
    }

    regfree(&prog);
    return 1;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    /* 默认路径和模式，可通过命令行覆盖 */
    const char *filepath = "../grep/tests/ere.tests";
    const char *mode_str = "ere";

    if (argc >= 2) filepath = argv[1];
    if (argc >= 3) mode_str = argv[2];

    int is_ere = (strcmp(mode_str, "ere") == 0);
    int eflags = 0;

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "无法打开测试文件: %s\n", filepath);
        return 1;
    }

    Stats s = { .file = filepath };
    char line[MAX_LINE];

    printf("============================================================\n");
    printf("  Spencer %s 测试: %s\n", is_ere ? "ERE" : "BRE", filepath);
    printf("============================================================\n\n");

    while (fgets(line, sizeof(line), fp)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }

        char *status_str = line;
        char *regex = strchr(line, '@');
        if (!regex) continue;
        *regex++ = '\0';

        char *input = strchr(regex, '@');
        if (!input) continue;
        *input++ = '\0';

        char *comment = strchr(input, '@');
        if (comment) *comment++ = '\0';
        else comment = "";

        run_one_test(status_str, regex, input, comment, eflags, is_ere, &s);
    }

    fclose(fp);

    printf("\n============================================================\n");
    printf("  %s 测试结果\n", is_ere ? "ERE" : "BRE");
    printf("  总计: %d, 通过: %d, 失败: %d, 跳过: %d, 预期失败: %d\n",
           s.total, s.passed, s.failed, s.skipped, s.xfail);
    if (s.total > 0) {
        int base = s.total - s.skipped;
        if (base > 0) {
            double rate = 100.0 * (s.passed + s.xfail) / (double)base;
            double strict = 100.0 * s.passed / (double)base;
            printf("  通过率(计预期失败): %.1f%%\n", rate);
            printf("  严格通过率: %.1f%%\n", strict);
        }
    }
    printf("============================================================\n");

    return s.failed > 0 ? 1 : 0;
}
