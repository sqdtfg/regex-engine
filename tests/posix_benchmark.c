/**
 * posix_benchmark.c — DFA 引擎性能基准测试（纯项目侧，不依赖系统 POSIX）
 *
 * 测量 DFA 匹配（100KB 输入）和编译管道各阶段的吞吐量。
 *
 * 用法（CMake）：
 *   cmake --build . --target run_benchmark
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static double now_ms(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart * 1000.0;
}
#else
#include <sys/time.h>
static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

#include "api.h"
#include "dfa.h"
#include "hopcroft.h"

/* ========================================================================== */
/*  辅助函数                                                                    */
/* ========================================================================== */

static char *make_text(size_t n, int seed) {
    char *t = (char *)malloc(n + 1);
    if (!t) return NULL;
    for (size_t i = 0; i < n; i++)
        t[i] = (char)(32 + ((i * 7 + seed * 13) % 95));
    t[n] = '\0';
    return t;
}

static void inject_match(char *text, size_t n) {
    size_t mid = n / 2;
    const char *word = "abc123hello456xyz";
    size_t wlen = strlen(word);
    if (mid + wlen < n) memcpy(text + mid, word, wlen);
}

/* ========================================================================== */
/*  DFA 匹配性能                                                                */
/* ========================================================================== */

static void run_match_bench(void) {
    struct {
        const char *pattern;
        const char *label;
    } tests[] = {
        {"a*",               "a*              "},
        {"a+",               "a+              "},
        {"[a-z]+",           "[a-z]+          "},
        {"\\d{3}-\\d{4}",    "\\d{3}-\\d{4}   "},
        {"\\w+@\\w+\\.\\w+", "\\w+@\\w+\\.\\w+"},
    };
    int sizes[] = {1, 10, 100, 1000};  /* KB */

    printf("============================================================\n");
    printf("  DFA 匹配性能 (最小化后)\n");
    printf("============================================================\n");
    printf("%-22s %7s %10s %12s %10s\n", "模式", "规模", "耗时(ms)", "吞吐量(ch/s)", "MB/s");
    printf("%-22s %7s %10s %12s %10s\n", "----", "----", "--------", "-----------", "----");

    for (int ti = 0; ti < 5; ti++) {
        regex_t *prog = regex_compile(tests[ti].pattern, REGEX_FLAG_NONE);
        if (!prog) { printf("%-22s 编译失败\n", tests[ti].label); continue; }
        printf("  (最小化后 DFA 状态数: %d)\n", prog->dfa.state_count);

        for (int si = 0; si < 4; si++) {
            int size_kb = sizes[si];
            size_t text_sz = (size_t)size_kb * 1024;
            char *text = make_text(text_sz, ti);
            if (!text) continue;
            inject_match(text, text_sz);

            int iters = (size_kb >= 100) ? 1000 : 10000;
            if (size_kb >= 1000) iters = 100;

            double t0 = now_ms();
            MatchResult r;
            for (int j = 0; j < iters; j++) regex_search(prog, text, &r);
            double t1 = now_ms();
            double ms = t1 - t0;

            double chars_per_sec = (double)iters * (double)text_sz / (ms / 1000.0);
            double mbps = chars_per_sec / (1024.0 * 1024.0);

            printf("%-22s %6dKB %10.3f %12.0f %10.2f\n",
                   tests[ti].label, size_kb, ms, chars_per_sec, mbps);
        }
        regex_free(prog);
        printf("\n");
    }
}

/* ========================================================================== */
/*  编译管道性能                                                                */
/* ========================================================================== */

static void run_compile_bench(void) {
    const char *patterns[] = {
        "a", "abc", "a*", "a|b", "[0-9]+",
        "\\d{3}-\\d{4}", "(abc|def)+", "\\w+@\\w+\\.\\w+",
        NULL
    };
    int iters = 10000;

    printf("============================================================\n");
    printf("  编译管道 (Tokenizer→解析→NFA→DFA→最小化)\n");
    printf("  (10000 次迭代平均值, 单位 us)\n");
    printf("============================================================\n");
    printf("%-22s %8s %8s %8s %10s\n", "模式", "词法", "解析", "NFA→DFA", "最小化");
    printf("%-22s %8s %8s %8s %10s\n", "----", "----", "----", "-------", "------");

    for (int pi = 0; patterns[pi]; pi++) {
        const char *pat = patterns[pi];

        /* 词法分析 */
        double tok_us = 0;
        {
            double t0 = now_ms();
            for (int j = 0; j < iters; j++) {
                Tokenizer tok;
                tokenizer_init(&tok, pat);
                while (tokenizer_next(&tok).type != TOK_EOF) {}
            }
            tok_us = (now_ms() - t0) / (double)iters * 1000.0;
        }

        /* 语法解析 */
        double parse_us = 0;
        {
            double t0 = now_ms();
            for (int j = 0; j < iters; j++) {
                Parser parser;
                parser_init(&parser, pat);
                ASTNode *ast = parser_parse(&parser);
                ast_free(ast);
            }
            parse_us = (now_ms() - t0) / (double)iters * 1000.0;
        }

        /* NFA→DFA */
        double dfa_us = 0;
        {
            Parser parser;
            parser_init(&parser, pat);
            ASTNode *ast = parser_parse(&parser);
            NFAGraph nfa = nfa_from_ast(ast);

            double t0 = now_ms();
            for (int j = 0; j < iters; j++) {
                DFAMachine dfa = dfa_from_nfa(&nfa);
                dfa_free(&dfa);
            }
            dfa_us = (now_ms() - t0) / (double)iters * 1000.0;

            /* 最小化 */
            DFAMachine dfa = dfa_from_nfa(&nfa);
            int orig = dfa.state_count;
            int actual = iters;
            if (orig > 10) actual = iters / 10;
            if (actual < 10) actual = 10;
            double t0_m = now_ms();
            for (int j = 0; j < actual; j++) {
                DFAMachine copy = dfa_from_nfa(&nfa);
                dfa_minimize(&copy);
                dfa_free(&copy);
            }
            double min_us = (now_ms() - t0_m) / (double)actual * 1000.0;
            dfa_free(&dfa);
            nfa_free(&nfa);
            ast_free(ast);

            printf("%-22s %7.1fus %7.1fus %7.1fus %7.1fus (%d→%d)\n",
                   pat, tok_us, parse_us, dfa_us, min_us, orig, orig);
        }
    }
}

/* ========================================================================== */
/*  入口                                                                        */
/* ========================================================================== */

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const char *mode = (argc > 1) ? argv[1] : "all";

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     DFA 正则引擎 — 性能报告                               ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    if (strcmp(mode, "all") == 0 || strcmp(mode, "match") == 0)
        run_match_bench();

    if (strcmp(mode, "all") == 0 || strcmp(mode, "compile") == 0)
        run_compile_bench();

    printf("============================================================\n");
    printf("  报告结束。DFA 使用 256路查表转移, O(n) 匹配, 永不回溯。\n");
    printf("============================================================\n");
    return 0;
}
