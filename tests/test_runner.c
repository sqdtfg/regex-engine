/**
 * test_runner.c — 统一测试运行框架
 *
 * 功能：
 *   - 运行所有单元测试
 *   - 统计汇总测试结果
 *   - 显示详细的测试报告
 *
 * 使用：
 *   ./bin/test_runner              # 运行所有测试
 *   ./bin/test_runner tokenizer    # 只运行词法分析器测试
 *   ./bin/test_runner parser       # 只运行语法解析器测试
 *   ./bin/test_runner nfa          # 只运行 NFA 构造测试
 *   ./bin/test_runner dfa          # 只运行 DFA 构造测试
 *   ./bin/test_runner hopcroft     # 只运行 Hopcroft 最小化测试
 *   ./bin/test_runner matcher      # 只运行匹配器测试
 *   ./bin/test_runner capture      # 只运行捕获组测试
 *   ./bin/test_runner api          # 只运行 API 测试
 *   ./bin/test_runner posix        # 只运行 POSIX 兼容层测试
 *   ./bin/test_runner grep         # 只运行 Grep 测试集
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#ifdef _WIN32
#include <windows.h>
#endif

/* ========================================================================== */
/*  测试类型枚举                                                               */
/* ========================================================================== */

typedef enum {
    TEST_ALL,
    TEST_TOKENIZER,
    TEST_PARSER,
    TEST_NFA,
    TEST_DFA,
    TEST_HOPCROFT,
    TEST_MATCHER,
    TEST_CAPTURE,
    TEST_API,
    TEST_POSIX,
    TEST_GREP
} TestType;

/* ========================================================================== */
/*  测试结果结构                                                               */
/* ========================================================================== */

typedef struct {
    const char *name;
    const char *description;
    const char *executable;
    bool passed;
    int exit_code;
} TestResult;

/* ========================================================================== */
/*  测试列表                                                                   */
/* ========================================================================== */

static TestResult tests[] = {
    {
        "tokenizer",
        "词法分析器测试",
        "regex_tokenizer_test",
        false,
        0
    },
    {
        "parser",
        "语法解析器测试",
        "regex_parser_test",
        false,
        0
    },
    {
        "nfa",
        "NFA Thompson 构造测试",
        "regex_nfa_test",
        false,
        0
    },
    {
        "dfa",
        "DFA 子集构造测试",
        "regex_dfa_test",
        false,
        0
    },
    {
        "hopcroft",
        "Hopcroft DFA 最小化测试",
        "regex_hopcroft_test",
        false,
        0
    },
    {
        "matcher",
        "DFA 匹配器测试",
        "regex_matcher_test",
        false,
        0
    },
    {
        "capture",
        "捕获组测试",
        "regex_capture_test",
        false,
        0
    },
    {
        "api",
        "高级 API 测试",
        "regex_api_test",
        false,
        0
    },
    {
        "posix",
        "POSIX 兼容层测试",
        "regex_posix_test",
        false,
        0
    },
    {
        "grep",
        "Grep 测试集适配",
        "regex_grep_test",
        false,
        0
    }
};

static const int TEST_COUNT = sizeof(tests) / sizeof(tests[0]);

/* ========================================================================== */
/*  全局统计                                                                   */
/* ========================================================================== */

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

/* ========================================================================== */
/*  工具函数：构建可执行文件路径                                               */
/* ========================================================================== */

static char* get_executable_path(const char *executable) {
#ifdef _WIN32
    static char path[1024];
    snprintf(path, sizeof(path), "bin\\%s.exe", executable);
    return path;
#else
    static char path[1024];
    snprintf(path, sizeof(path), "bin/%s", executable);
    return path;
#endif
}

/* ========================================================================== */
/*  工具函数：执行单个测试                                                     */
/* ========================================================================== */

static int run_single_test(TestResult *test) {
    const char *exe_path = get_executable_path(test->executable);

    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║  正在运行: %-40s║\n", test->name);
    printf("║  描述: %-46s║\n", test->description);
    printf("╠═══════════════════════════════════════════════════════════╣\n");

#ifdef _WIN32
    /* Windows 系统：使用 system() 执行 */
    int ret = system(exe_path);
    test->exit_code = ret;
    test->passed = (ret == 0);
#else
    /* Linux/macOS 系统：使用 system() 执行 */
    int ret = system(exe_path);
    test->exit_code = WEXITSTATUS(ret);
    test->passed = (test->exit_code == 0);
#endif

    printf("╠═══════════════════════════════════════════════════════════╣\n");
    if (test->passed) {
        printf("║  结果: ✅ 通过 (退出码: %d)                            ║\n", test->exit_code);
        passed_tests++;
    } else {
        printf("║  结果: ❌ 失败 (退出码: %d)                            ║\n", test->exit_code);
        failed_tests++;
    }
    printf("╚═══════════════════════════════════════════════════════════╝\n");

    total_tests++;

    return test->passed ? 0 : 1;
}

/* ========================================================================== */
/*  工具函数：运行所有测试                                                     */
/* ========================================================================== */

static void run_all_tests(void) {
    for (int i = 0; i < TEST_COUNT; i++) {
        run_single_test(&tests[i]);
    }
}

/* ========================================================================== */
/*  工具函数：运行指定测试                                                     */
/* ========================================================================== */

static void run_test_by_name(const char *name) {
    for (int i = 0; i < TEST_COUNT; i++) {
        if (strcmp(tests[i].name, name) == 0) {
            run_single_test(&tests[i]);
            return;
        }
    }

    printf("\n");
    printf("⚠️  未知测试: %s\n", name);
    printf("可用的测试:\n");
    for (int i = 0; i < TEST_COUNT; i++) {
        printf("  - %s (%s)\n", tests[i].name, tests[i].description);
    }
    printf("\n");
}

/* ========================================================================== */
/*  工具函数：显示总结报告                                                     */
/* ========================================================================== */

static void print_summary(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                        测试总结报告                                 ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                   ║\n");

    for (int i = 0; i < TEST_COUNT; i++) {
        const char *status = tests[i].passed ? "✅ 通过" : "❌ 失败";
        printf("║  %-12s: %-30s: %-10s║\n",
               tests[i].name,
               tests[i].description,
               status);
    }

    printf("║                                                                   ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  总测试数: %3d                                                  ║\n", total_tests);
    printf("║  通过: %5d                                                      ║\n", passed_tests);
    printf("║  失败: %5d                                                      ║\n", failed_tests);

    float pass_rate = total_tests > 0 ? (float)passed_tests / total_tests * 100 : 0.0f;
    printf("║  通过率: %6.1f%%                                                ║\n", pass_rate);

    const char *msg;
    if (total_tests == 0) {
        msg = "ℹ️  没有运行任何测试";
    } else if (pass_rate == 100.0f) {
        msg = "🎉 全部通过！";
    } else if (pass_rate >= 90.0f) {
        msg = "✅ 表现优秀！";
    } else if (pass_rate >= 70.0f) {
        msg = "⚠️  基本通过";
    } else {
        msg = "❌ 需要修复";
    }

    printf("║  评价: %-50s║\n", msg);
    printf("║                                                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* ========================================================================== */
/*  工具函数：显示帮助信息                                                     */
/* ========================================================================== */

static void print_help(const char *prog_name) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════════════╗\n");
    printf("║                     Regex Engine 测试运行器                         ║\n");
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║  使用: %-55s║\n", prog_name);
    printf("╠═══════════════════════════════════════════════════════════════════╣\n");
    printf("║                                                                   ║\n");
    printf("║  选项:                                                            ║\n");
    printf("║    (无参数)              运行所有测试                             ║\n");
    printf("║    tokenizer             只运行词法分析器测试                     ║\n");
    printf("║    parser                只运行语法解析器测试                     ║\n");
    printf("║    nfa                   只运行 NFA 构造测试                      ║\n");
    printf("║    dfa                   只运行 DFA 构造测试                      ║\n");
    printf("║    hopcroft              只运行 Hopcroft 最小化测试               ║\n");
    printf("║    matcher               只运行匹配器测试                         ║\n");
    printf("║    capture               只运行捕获组测试                         ║\n");
    printf("║    api                   只运行 API 测试                          ║\n");
    printf("║    posix                 只运行 POSIX 兼容层测试                  ║\n");
    printf("║    grep                  只运行 Grep 测试集                      ║\n");
    printf("║    -h, --help            显示此帮助信息                           ║\n");
    printf("║                                                                   ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

/* ========================================================================== */
/*  主函数                                                                     */
/* ========================================================================== */

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    if (argc == 1) {
        /* 无参数：运行所有测试 */
        printf("\n");
        printf("╔═══════════════════════════════════════════════════════════════════╗\n");
        printf("║                 正在运行所有单元测试...                             ║\n");
        printf("╚═══════════════════════════════════════════════════════════════════╝\n");

        run_all_tests();
        print_summary();

    } else if (argc == 2) {
        /* 单个参数：运行指定测试或显示帮助 */
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            print_help(argv[0]);
        } else {
            run_test_by_name(argv[1]);
            print_summary();
        }

    } else {
        /* 多个参数：显示帮助 */
        printf("\n⚠️  无效的参数数量\n");
        print_help(argv[0]);
        return 1;
    }

    return failed_tests > 0 ? 1 : 0;
}
