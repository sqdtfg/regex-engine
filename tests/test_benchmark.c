/**
 * test_benchmark.c — 性能基准测试
 *
 * 测量各模块在不同规模输入下的执行时间。
 * 结果输出为 CSV 格式，便于导入电子表格分析。
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif
#include "api.h"
#include "matcher.h"
#include "capture.h"
#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"
#include "tokenizer.h"

/* ========================================================================== */
/*  计时工具                                                                    */
/* ========================================================================== */

#ifdef _WIN32
static double elapsed_ms(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart * 1000.0;
}
#else
static double elapsed_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

/* ========================================================================== */
/*  测试数据结构                                                                */
/* ========================================================================== */

typedef struct {
    const char *category;
    const char *name;
    double time_ms;      /* 平均耗时（毫秒） */
    size_t input_size;   /* 输入大小 */
    int iterations;      /* 迭代次数 */
    double ops_per_sec;  /* 每秒操作数（字符/秒） */
} BenchResult;

static BenchResult results[128];
static int result_count = 0;

static void record_result(const char *category, const char *name,
                          double time_ms, size_t input_size, int iterations) {
    if (result_count >= 128) return;
    BenchResult *r = &results[result_count++];
    r->category = category;
    r->name = name;
    r->time_ms = time_ms;
    r->input_size = input_size;
    r->iterations = iterations;
    r->ops_per_sec = (iterations * input_size) / (time_ms / 1000.0);
}

/* ========================================================================== */
/*  辅助：生成测试文本                                                          */
/* ========================================================================== */

static char *generate_text(size_t len, char fill) {
    char *text = (char *)malloc(len + 1);
    if (text) {
        memset(text, fill, len);
        text[len] = '\0';
    }
    return text;
}

/* ========================================================================== */
/*  1. Tokenizer 基准                                                           */
/* ========================================================================== */

static void bench_tokenizer(void) {
    const char *patterns[] = {
        "a", "abc", "a*b+", "[0-9]+", "\\d{3}-\\d{4}",
        "(abc|def)+", "\\w+@\\w+\\.\\w+", NULL
    };

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 100000;
        for (int j = 0; j < iters; j++) {
            Parser parser;
            parser_init(&parser, patterns[i]);
            ASTNode *ast = parser_parse(&parser);
            if (ast) ast_free(ast);
        }
        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("Tokenizer: %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("tokenizer", patterns[i], ms, strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  2. NFA 构建基准                                                             */
/* ========================================================================== */

static void bench_nfa(void) {
    const char *patterns[] = {
        "a", "abc", "a*b+", "[0-9]+", "\\d{3}-\\d{4}",
        "(abc|def)+", "a(b|c)*d", NULL
    };

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 50000;
        for (int j = 0; j < iters; j++) {
            Parser parser;
            parser_init(&parser, patterns[i]);
            ASTNode *ast = parser_parse(&parser);
            if (ast) {
                NFAGraph nfa = nfa_from_ast(ast);
                ast_free(ast);
                nfa_free(&nfa);
            }
        }
        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("NFA:       %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("nfa", patterns[i], ms, strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  3. DFA 构建基准                                                             */
/* ========================================================================== */

static void bench_dfa(void) {
    const char *patterns[] = {
        "a", "abc", "a*b+", "[0-9]+", "\\d{3}-\\d{4}",
        "(abc|def)+", "a(b|c)*d", "([a-z]+\\.?)+" , NULL
    };

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 20000;
        for (int j = 0; j < iters; j++) {
            Parser parser;
            parser_init(&parser, patterns[i]);
            ASTNode *ast = parser_parse(&parser);
            if (ast) {
                NFAGraph nfa = nfa_from_ast(ast);
                DFAMachine dfa = dfa_from_nfa(&nfa);
                ast_free(ast);
                nfa_free(&nfa);
                dfa_free(&dfa);
            }
        }
        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("DFA:       %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("dfa", patterns[i], ms, strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  4. DFA 最小化基准                                                           */
/* ========================================================================== */

static void bench_hopcroft(void) {
    const char *patterns[] = {
        "a*b+", "([a-z]+)+", "(abc|def|ghi)+",
        "\\d{2,4}[a-z]{1,3}", NULL
    };

    for (int i = 0; patterns[i]; i++) {
        Parser parser;
        parser_init(&parser, patterns[i]);
        ASTNode *ast = parser_parse(&parser);
        if (!ast) continue;

        NFAGraph nfa = nfa_from_ast(ast);
        ast_free(ast);
        if (!nfa.start) { nfa_free(&nfa); continue; }

        DFAMachine dfa = dfa_from_nfa(&nfa);
        nfa_free(&nfa);
        if (!dfa.states) { dfa_free(&dfa); continue; }

        int orig_states = dfa.state_count;

        double t0 = elapsed_ms();
        int iters = 10000;
        for (int j = 0; j < iters; j++) {
            /* 重新构建 DFA 以确保每次最小化都是独立的 */
            Parser parser2;
            parser_init(&parser2, patterns[i]);
            ASTNode *ast2 = parser_parse(&parser2);
            if (!ast2) break;
            NFAGraph nfa2 = nfa_from_ast(ast2);
            ast_free(ast2);
            if (!nfa2.start) { nfa_free(&nfa2); break; }
            DFAMachine dfa2 = dfa_from_nfa(&nfa2);
            nfa_free(&nfa2);
            if (!dfa2.states) { dfa_free(&dfa2); break; }

            dfa_minimize(&dfa2);
            dfa_free(&dfa2);
        }
        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("Hopcroft:  %-25s  %8.3f ms  (%d iters, %d states)\n",
               patterns[i], ms, iters, orig_states);

        record_result("hopcroft", patterns[i], ms, orig_states, iters);
        dfa_free(&dfa);
    }
}

/* ========================================================================== */
/*  5. 匹配性能基准                                                             */
/* ========================================================================== */

static void bench_match(void) {
    struct {
        const char *pattern;
        const char *text;
        size_t text_len;
        const char *label;
    } tests[] = {
        { "a*",          "aaaaaaaaaa",       10,    "short match (star)" },
        { "a+",          "aaaaaaaaaa",       10,    "short match (plus)" },
        { "[a-z]+",      "abcdefghijklmnopqrstuvwxyz", 26, "medium match" },
        { "\\d{3}-\\d{4}", "123-4567",       8,     "digit pattern" },
        { "\\w+@\\w+\\.\\w+", "user@example.com", 16, "email pattern" },
        { "(abc)+",      "abcabcabcabcabc",  15,    "group repeat" },
        { NULL, NULL, 0, NULL },
    };

    /* 生成不同长度的测试文本 */
    size_t text_sizes[] = { 100, 1000, 10000, 100000 };
    const char *labels[] = { "100B", "1KB", "10KB", "100KB" };

    for (int ti = 0; tests[ti].pattern; ti++) {
        for (int si = 0; si < 4; si++) {
            size_t len = text_sizes[si];
            char *text = generate_text(len, 'a' + (ti % 26));
            if (!text) continue;

            /* 确保文本包含匹配内容 */
            const char *pat = tests[ti].pattern;
            if (strstr(pat, "\\d")) {
                memset(text, '0', len);
            } else if (strstr(pat, "@")) {
                /* 用 memmove 替代 snprintf 重叠写入，消除 -Wrestrict 警告 */
                memmove(text + 16, text, len - 16 > 16 ? 16 : len - 16);
                memcpy(text, "user@example.com", 16);
            }

            regex_t *prog = regex_compile(pat, REGEX_FLAG_NONE);
            if (!prog) { free(text); continue; }

            double t0 = elapsed_ms();
            int iters = 10000;
            if (len >= 10000) iters = 1000;
            if (len >= 100000) iters = 100;

            MatchResult r;
            for (int j = 0; j < iters; j++) {
                regex_search(prog, text, &r);
            }
            double t1 = elapsed_ms();
            double ms = t1 - t0;

            printf("Match:     %-20s + %s  %8.3f ms  (%d iters)\n",
                   tests[ti].label, labels[si], ms, iters);

            record_result("match", tests[ti].label, ms, len, iters);

            regex_free(prog);
            free(text);
        }
    }
}

/* ========================================================================== */
/*  6. 捕获组匹配基准                                                           */
/* ========================================================================== */

static void bench_capture(void) {
    const char *patterns[] = {
        "(abc)", "(abc)(def)", "(a+)", "((ab)(cd))", NULL
    };

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 10000;
        const char *text = "xxxabcyyy";

        Parser parser;
        parser_init(&parser, patterns[i]);
        ASTNode *ast = parser_parse(&parser);
        if (!ast) continue;

        DFAMachine dfa = dfa_from_ast_with_groups(ast);
        ast_free(ast);
        if (!dfa.states) { dfa_free(&dfa); continue; }

        for (int j = 0; j < iters; j++) {
            CapturedMatch cm = dfa_match_captured(&dfa, text);
            captured_match_free(&cm);
        }
        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("Capture:   %-20s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("capture", patterns[i], ms, strlen(text), iters);

        dfa_capture_free(&dfa);
    }
}

/* ========================================================================== */
/*  7. 全局匹配基准                                                             */
/* ========================================================================== */

static void bench_findall(void) {
    const char *patterns[] = { "a", "[aeiou]", "\\d", NULL };
    size_t text_sizes[] = { 1000, 10000, 100000 };
    const char *labels[] = { "1KB", "10KB", "100KB" };

    for (int pi = 0; patterns[pi]; pi++) {
        for (int si = 0; si < 3; si++) {
            size_t len = text_sizes[si];
            char *text = generate_text(len, 'a');
            if (!text) continue;

            regex_t *prog = regex_compile(patterns[pi], REGEX_FLAG_NONE);
            if (!prog) { free(text); continue; }

            double t0 = elapsed_ms();
            int iters = si < 2 ? 1000 : 100;

            for (int j = 0; j < iters; j++) {
                int count;
                MatchResult *results = regex_findall(prog, text, &count);
                if (results) regex_findall_free(results);
            }
            double t1 = elapsed_ms();
            double ms = t1 - t0;

            printf("FindAll:   %-10s + %s  %8.3f ms  (%d iters)\n",
                   patterns[pi], labels[si], ms, iters);
            record_result("findall", patterns[pi], ms, len, iters);

            regex_free(prog);
            free(text);
        }
    }
}

/* ========================================================================== */
/*  输出 CSV 摘要                                                              */
/* ========================================================================== */

static void output_csv(void) {
    printf("\n=== CSV Summary ===\n");
    printf("category,name,time_ms,input_size,iterations,ops_per_sec\n");
    for (int i = 0; i < result_count; i++) {
        printf("%s,%s,%.3f,%zu,%d,%.0f\n",
               results[i].category, results[i].name,
               results[i].time_ms, results[i].input_size,
               results[i].iterations, results[i].ops_per_sec);
    }
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

    printf("==================================================\n");
    printf("  正则引擎 性能基准测试\n");
    printf("==================================================\n\n");

    printf("--- Tokenizer ---\n");
    bench_tokenizer();

    printf("\n--- NFA Construction ---\n");
    bench_nfa();

    printf("\n--- DFA Construction ---\n");
    bench_dfa();

    printf("\n--- Hopcroft Minimization ---\n");
    bench_hopcroft();

    printf("\n--- Matching ---\n");
    bench_match();

    printf("\n--- Capture Groups ---\n");
    bench_capture();

    printf("\n--- FindAll ---\n");
    bench_findall();

    output_csv();

    printf("\n==================================================\n");
    printf("  总计 %d 个基准测试项\n", result_count);
    printf("==================================================\n");

    return 0;
}
