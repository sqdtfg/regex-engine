/**
 * bench_all.c — 所有性能测试统一入口
 *
 * 合并了以下文件的重复代码：
 *   - posix_benchmark.c (DFA匹配+编译管道)
 *   - posix_compare.c (DFA vs 未最小化DFA对比)
 *   - posix_system_bench.c (DFA vs 系统POSIX regcomp对比)
 *
 * 通过命令行参数选择运行模式：
 *   bench_all match    — DFA 匹配性能基准
 *   bench_all compile  — 编译管道性能基准
 *   bench_all compare  — DFA vs 未最小化DFA(POSIX等效)对比
 *   bench_all posix    — DFA vs 系统POSIX(PCRE)对比
 *   bench_all all      — 运行所有模式(默认)
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

#ifdef USE_SYS_POSIX
#include <sys/types.h>
#include <regex.h>
#endif

#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"
#include "api.h"

/* ========================================================================== */
/*  共享工具函数                                                               */
/* ========================================================================== */

/** 生成可打印 ASCII 测试文本 */
static char *make_text(size_t n, int seed) {
    char *t = (char *)malloc(n + 1);
    if (!t) return NULL;
    for (size_t i = 0; i < n; i++)
        t[i] = (char)(32 + ((i * 7 + seed * 13) % 95));
    t[n] = '\0';
    return t;
}

/** 在文本中间嵌入匹配点 (对 * 模式填满匹配字符) */
static void inject_match(char *t, size_t n, const char *pat) {
    size_t mid = n / 2;
    if (pat[0] && strchr(pat, '*') == pat) {
        for (size_t i = mid; i < n - 1; i++) t[i] = pat[0];
    } else {
        const char *word = "abc123hello456xyz";
        size_t wlen = strlen(word);
        if (mid + wlen < n) memcpy(t + mid, word, wlen);
    }
}

/** 编译模式到最小化 DFA (底层 API) */
static DFAMachine compile_dfa(const char *pattern) {
    Parser parser;
    parser_init(&parser, pattern);
    ASTNode *ast = parser_parse(&parser);
    if (!ast) return (DFAMachine){0};
    NFAGraph nfa = nfa_from_ast(ast);
    DFAMachine dfa = dfa_from_nfa(&nfa);
    nfa_free(&nfa);
    ast_free(ast);
    dfa_minimize(&dfa);
    return dfa;
}

/** 用底层 DFA 执行最左最长匹配（不经过 api.h） */
static void dfa_match_raw(const DFAMachine *dfa, const char *text, size_t text_len) {
    size_t limit = dfa->has_anchor_start ? 0 : text_len;
    for (size_t start = 0; start <= limit; start++) {
        int state = dfa->start_state;
        size_t pos = start;
        int found = 0;
        while (pos <= text_len) {
            if (dfa->states[state].is_accept) { found = 1; break; }
            if (pos == text_len) break;
            int nx = dfa->states[state].transitions[(unsigned char)text[pos]];
            if (nx == -1) break;
            state = nx; pos++;
        }
        if (found && (!dfa->has_anchor_end || pos == text_len)) return;
    }
}

/* ========================================================================== */
/*  1. DFA 匹配性能基准                                                         */
/* ========================================================================== */

static void bench_match(void) {
    struct { const char *pat; const char *label; } tests[] = {
        {"a*",               "a*              "},
        {"a+",               "a+              "},
        {"[a-z]+",           "[a-z]+          "},
        {"\\d{3}-\\d{4}",    "\\d{3}-\\d{4}   "},
        {"\\w+@\\w+\\.\\w+", "\\w+@\\w+\\.\\w+"},
    };
    int sizes[] = {1, 10, 100, 1000}; /* KB */

    printf("============================================================\n");
    printf("  DFA 匹配性能 (最小化后)\n");
    printf("============================================================\n");
    printf("%-22s %7s %10s %10s\n", "模式", "规模", "耗时(ms)", "MB/s");
    printf("%-22s %7s %10s %10s\n", "----", "----", "--------", "-----");

    for (int ti = 0; ti < 5; ti++) {
        regex_t *prog = regex_compile(tests[ti].pat, REGEX_FLAG_NONE);
        if (!prog) { printf("%-22s 编译失败\n", tests[ti].label); continue; }
        printf("  (状态数: %d)\n", prog->dfa.state_count);

        for (int si = 0; si < 4; si++) {
            int size_kb = sizes[si];
            size_t text_sz = (size_t)size_kb * 1024;
            char *text = make_text(text_sz, ti);
            if (!text) continue;
            inject_match(text, text_sz, tests[ti].pat);

            int iters = (size_kb >= 100) ? 1000 : 10000;
            if (size_kb >= 1000) iters = 100;

            double t0 = now_ms();
            MatchResult r;
            for (int j = 0; j < iters; j++) regex_search(prog, text, &r);
            double t1 = now_ms();
            double ms = t1 - t0;
            double mbps = (double)(iters * text_sz) / (ms / 1000.0) / (1024.0 * 1024.0);

            printf("%-22s %6dKB %10.3f %10.2f\n",
                   tests[ti].label, size_kb, ms, mbps);
        }
        regex_free(prog);
        printf("\n");
    }
}

/* ========================================================================== */
/*  2. 编译管道性能基准                                                         */
/* ========================================================================== */

static void bench_compile(void) {
    const char *patterns[] = {
        "a", "abc", "a*", "a|b", "[0-9]+",
        "\\d{3}-\\d{4}", "(abc|def)+", "\\w+@\\w+\\.\\w+", NULL
    };
    int iters = 10000;

    printf("============================================================\n");
    printf("  编译管道 (10000次平均, 单位 us)\n");
    printf("============================================================\n");
    printf("%-22s %8s %8s %8s %10s\n", "模式", "词法", "解析", "NFA→DFA", "最小化");
    printf("%-22s %8s %8s %8s %10s\n", "----", "----", "----", "-------", "------");

    for (int pi = 0; patterns[pi]; pi++) {
        const char *pat = patterns[pi];

        /* 词法 */
        double tok_us = 0;
        {   double t0 = now_ms();
            for (int j = 0; j < iters; j++) {
                Tokenizer tok; tokenizer_init(&tok, pat);
                while (tokenizer_next(&tok).type != TOK_EOF) {}
            }
            tok_us = (now_ms() - t0) / (double)iters * 1000.0;
        }

        /* 解析 */
        double parse_us = 0;
        {   double t0 = now_ms();
            for (int j = 0; j < iters; j++) {
                Parser p; parser_init(&p, pat);
                ASTNode *a = parser_parse(&p); ast_free(a);
            }
            parse_us = (now_ms() - t0) / (double)iters * 1000.0;
        }

        /* NFA→DFA + 最小化 */
        double dfa_us = 0, min_us = 0;
        {
            Parser p; parser_init(&p, pat); ASTNode *a = parser_parse(&p);
            NFAGraph nfa = nfa_from_ast(a);
            {   double t0 = now_ms();
                for (int j = 0; j < iters; j++) {
                    DFAMachine d = dfa_from_nfa(&nfa); dfa_free(&d);
                }
                dfa_us = (now_ms() - t0) / (double)iters * 1000.0;
            }
            /* 最小化 */
            {   DFAMachine d = dfa_from_nfa(&nfa);
                int orig = d.state_count;
                int act = iters; if (orig > 10) act = iters / 10; if (act < 10) act = 10;
                double t0 = now_ms();
                for (int j = 0; j < act; j++) {
                    DFAMachine c = dfa_from_nfa(&nfa); dfa_minimize(&c); dfa_free(&c);
                }
                min_us = (now_ms() - t0) / (double)act * 1000.0;
                dfa_free(&d);
            }
            nfa_free(&nfa); ast_free(a);
        }

        printf("%-22s %7.1fus %7.1fus %7.1fus %7.1fus\n",
               pat, tok_us, parse_us, dfa_us, min_us);
    }
}

/* ========================================================================== */
/*  3. DFA vs 未最小化(POSIX等效) 对比                                         */
/* ========================================================================== */

static void bench_compare(void) {
    struct { const char *pat; const char *label; } tests[] = {
        {"a*",               "a*              "},
        {"a+",               "a+              "},
        {"[a-z]+",           "[a-z]+          "},
        {"\\d{3}-\\d{4}",    "\\d{3}-\\d{4}   "},
        {"\\w+@\\w+\\.\\w+", "\\w+@\\w+\\.\\w+"},
    };

    printf("============================================================\n");
    printf("  DFA(最小化) vs DFA(未最小化, POSIX等效)\n");
    printf("============================================================\n");
    printf("%-22s %6s %10s %10s %7s\n", "模式", "规模", "DFA MB/s", "基线 MB/s", "比值");

    for (int ci = 0; ci < 5; ci++) {
        for (int si = 0; si < 2; si++) {
            int kb = (si == 0) ? 1 : 1000;
            size_t sz = (size_t)kb * 1024;
            int iters = (kb >= 1000) ? 100 : 10000;
            char *text = make_text(sz, ci * 7);
            if (!text) continue;
            inject_match(text, sz, tests[ci].pat);

            /* 最小化 DFA */
            int states;
            double dfa_mbps;
            {   DFAMachine d = compile_dfa(tests[ci].pat);
                if (!d.states) { dfa_mbps = -1; }
                else {
                    states = d.state_count;
                    double t0 = now_ms();
                    for (int j = 0; j < iters; j++) dfa_match_raw(&d, text, sz);
                    double t1 = now_ms();
                    dfa_mbps = (double)(iters * sz) / ((t1 - t0) / 1000.0) / (1024.0 * 1024.0);
                    dfa_free(&d);
                }
            }

            /* 未最小化 DFA (POSIX 等效) */
            double raw_mbps = -1;
            {   Parser p; parser_init(&p, tests[ci].pat); ASTNode *a = parser_parse(&p);
                NFAGraph n = nfa_from_ast(a); DFAMachine d = dfa_from_nfa(&n);
                nfa_free(&n); ast_free(a);
                if (d.states) {
                    int raw_st = d.state_count;
                    double t0 = now_ms();
                    for (int j = 0; j < iters; j++) dfa_match_raw(&d, text, sz);
                    double t1 = now_ms();
                    raw_mbps = (double)(iters * sz) / ((t1 - t0) / 1000.0) / (1024.0 * 1024.0);
                    dfa_free(&d);
                }
            }

            if (dfa_mbps > 0 && raw_mbps > 0) {
                double ratio = dfa_mbps / raw_mbps * 100.0;
                printf("%-22s %5dKB %8.2f  %8.2f  %6.1f%% %s\n",
                       tests[ci].label, kb, dfa_mbps, raw_mbps, ratio,
                       ratio >= 80.0 ? "✓" : "✗");
            }
            free(text);
        }
    }
}

/* ========================================================================== */
/*  4. DFA vs 系统 POSIX (PCRE) 对比 (条件编译)                                */
/* ========================================================================== */

#ifdef USE_SYS_POSIX
static void bench_sys_posix(void) {
    struct { const char *pat; const char *label; } tests[] = {
        {"a*", "a*"}, {"a+", "a+"}, {"[a-z]+", "[a-z]+"},
        {"[0-9]{3}-[0-9]{4}", "[0-9]{3}-[0-9]{4}"},
    };

    printf("============================================================\n");
    printf("  DFA vs 系统 POSIX regex.h (PCRE)\n");
    printf("============================================================\n");
    printf("%-22s %6s %10s %10s %7s\n", "模式", "KB", "DFA MB/s", "POSIX MB/s", "比值");

    for (int ci = 0; ci < 4; ci++) {
        for (int si = 0; si < 2; si++) {
            int kb = (si == 0) ? 1 : 1000;
            size_t sz = (size_t)kb * 1024;
            int iters = (kb >= 1000) ? 100 : 10000;
            char *text = make_text(sz, ci * 7);
            if (!text) continue;
            inject_match(text, sz, tests[ci].pat);

            double dfa_mbps;
            {   DFAMachine d = compile_dfa(tests[ci].pat);
                double t0 = now_ms();
                for (int j = 0; j < iters; j++) dfa_match_raw(&d, text, sz);
                double t1 = now_ms();
                dfa_mbps = (double)(iters * sz) / ((t1 - t0) / 1000.0) / (1024.0 * 1024.0);
                dfa_free(&d);
            }

            double sys_mbps = -1;
            {   regex_t r;  /* 系统 POSIX */
                if (regcomp(&r, tests[ci].pat, REG_EXTENDED) == 0) {
                    double t0 = now_ms();
                    regmatch_t pm[1];
                    for (int j = 0; j < iters; j++) regexec(&r, text, 1, pm, 0);
                    double t1 = now_ms();
                    sys_mbps = (double)(iters * sz) / ((t1 - t0) / 1000.0) / (1024.0 * 1024.0);
                    regfree(&r);
                }
            }

            if (dfa_mbps > 0 && sys_mbps > 0) {
                double ratio = dfa_mbps / sys_mbps * 100.0;
                printf("%-22s %5dKB %8.2f  %8.2f  %6.1f%% %s\n",
                       tests[ci].label, kb, dfa_mbps, sys_mbps, ratio,
                       ratio >= 80.0 ? "✓" : "✗");
            }
            free(text);
        }
    }
}
#endif

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
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  DFA 正则引擎 — 性能测试                                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    if (strcmp(mode, "all") == 0 || strcmp(mode, "match") == 0)
        bench_match();

    if (strcmp(mode, "all") == 0 || strcmp(mode, "compile") == 0)
        bench_compile();

    if (strcmp(mode, "all") == 0 || strcmp(mode, "compare") == 0)
        bench_compare();

#ifdef USE_SYS_POSIX
    if (strcmp(mode, "all") == 0 || strcmp(mode, "posix") == 0)
        bench_sys_posix();
#else
    if (strcmp(mode, "posix") == 0)
        printf("  [跳过] 系统 POSIX 对比未编译 (需 libpcreposix)\n");
#endif

    printf("\n============================================================\n");
    printf("  测试完成。\n");
    printf("============================================================\n");
    return 0;
}
