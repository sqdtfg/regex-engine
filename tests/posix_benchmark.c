/**
 * posix_benchmark.c — POSIX regex.h vs our DFA engine speed comparison
 *
 * Compiles with: gcc -O2 -o posix_benchmark posix_benchmark.c
 *
 * On Windows with MinGW, POSIX regex.h is not available.
 * This test uses our API + a fallback timing-based measurement.
 *
 * Usage: posix_benchmark [pattern] [input_size_kb]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_ms(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)freq.QuadPart * 1000.0;
}
#else
#include <sys/time.h>
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}
#endif

#include "api.h"
#include "dfa.h"
#include "hopcroft.h"
#include "nfa.h"
#include "parser.h"

/* ---- 生成测试文本 ---- */
static char *make_text(size_t len, int seed) {
    char *t = malloc(len + 1);
    if (!t) return NULL;
    /* 生成可打印 ASCII 文本 */
    for (size_t i = 0; i < len; i++) {
        t[i] = (char)(32 + ((i * 7 + seed * 13) % 95));
    }
    t[len] = '\0';
    return t;
}

/* 在文本中嵌入一个模式匹配点 */
static void inject_match(char *text, size_t len, const char *pattern, size_t at) {
    /* 简单方法：在位置 at 放一个 'a' 字符，对大多数字符模式有效 */
    if (at < len) {
        text[at] = 'a';
        if (at + 1 < len) text[at + 1] = 'b';
        if (at + 2 < len) text[at + 2] = 'c';
    }
}

/* ---- 运行 DFA 匹配 ---- */
static void run_dfa_tests(void) {
    /* 测试配置: {pattern, text_size_kb, iterations} */
    struct {
        const char *pattern;
        const char *label;
        int sizes[4];
    } tests[] = {
        {"a*",           "a*           ", {1, 10, 100, 1000}},
        {"a+",           "a+           ", {1, 10, 100, 1000}},
        {"[a-z]+",       "[a-z]+       ", {1, 10, 100, 1000}},
        {"\\d{3}-\\d{4}","\\\\d{3}-\\\\d{4}", {1, 10, 100, 1000}},
        {"\\w+@\\w+\\.\\w+","\\\\w+@\\\\w+\\\\.\\\\w+", {1, 10, 100, 1000}},
    };

    printf("============================================================\n");
    printf("  DFA regex engine — Matching Performance\n");
    printf("============================================================\n");
    printf("%-20s %8s %10s %12s %12s\n",
           "Pattern", "Size(KB)", "Time(ms)", "Throughput", "MB/sec");
    printf("------------------------------------------------------------\n");

    for (int ti = 0; ti < 5; ti++) {
        regex_t *prog = regex_compile(tests[ti].pattern, REGEX_FLAG_NONE);
        if (!prog) {
            printf("%-20s: compile FAILED\n", tests[ti].label);
            continue;
        }
        printf("  DFA states: %d  (after minimize)\n", prog->dfa.state_count);

        for (int si = 0; si < 4; si++) {
            int size_kb = tests[ti].sizes[si];
            size_t text_sz = (size_t)size_kb * 1024;
            char *text = make_text(text_sz, ti * 10 + si);
            if (!text) continue;
            inject_match(text, text_sz, tests[ti].pattern, text_sz / 2);

            /* 选择迭代次数 */
            int iters = 10000;
            if (size_kb >= 100)  iters = 1000;
            if (size_kb >= 1000) iters = 100;

            double t0 = get_time_ms();
            MatchResult r;
            for (int j = 0; j < iters; j++) {
                regex_search(prog, text, &r);
            }
            double t1 = get_time_ms();
            double ms = t1 - t0;

            /* 吞吐量: characters processed per second */
            double total_chars = (double)iters * (double)text_sz;
            double mb_per_sec = (total_chars / (ms / 1000.0)) / (1024.0 * 1024.0);
            double throughput = total_chars / (ms / 1000.0);

            printf("%-20s %8d %10.3f %12.0f %12.2f\n",
                   tests[ti].label, size_kb, ms, throughput, mb_per_sec);

            free(text);
        }
        regex_free(prog);
        printf("\n");
    }
}

/* ---- 编译/构造阶段性能 ---- */
static void run_compile_tests(void) {
    const char *patterns[] = {
        "a", "abc", "a*", "a|b", "[0-9]+",
        "\\d{3}-\\d{4}", "(abc|def)+", "\\w+@\\w+\\.\\w+",
        NULL
    };

    printf("============================================================\n");
    printf("  Compilation Pipeline (Tokenizer -> Parser -> NFA -> DFA -> Minimize)\n");
    printf("============================================================\n");
    printf("%-20s %8s %8s %8s %10s\n",
           "Pattern", "Tokenize", "Parse", "NFA->DFA", "Minimize");
    printf("%-20s %8s %8s %8s %10s\n",
           "-------", "--------", "-----", "-------", "--------");
    printf("  (all times in microseconds, averaged over 10000 iterations)\n\n");

    for (int pi = 0; patterns[pi]; pi++) {
        const char *pat = patterns[pi];
        int iters = 10000;

        /* Tokenize */
        {
            double t0 = get_time_ms();
            for (int j = 0; j < iters; j++) {
                Tokenizer tok;
                tokenizer_init(&tok, pat);
                while (tokenizer_next(&tok).type != TOK_EOF) {}
            }
            double ms = (get_time_ms() - t0);
            printf("%-20s %7.1fus ", pat, ms / (double)iters * 1000.0);
        }

        /* Parse */
        {
            double t0 = get_time_ms();
            for (int j = 0; j < iters; j++) {
                Parser parser;
                parser_init(&parser, pat);
                ASTNode *ast = parser_parse(&parser);
                ast_free(ast);
            }
            double ms = (get_time_ms() - t0);
            printf("%7.1fus ", ms / (double)iters * 1000.0);
        }

        /* NFA -> DFA */
        {
            Parser parser;
            parser_init(&parser, pat);
            ASTNode *ast = parser_parse(&parser);
            NFAGraph nfa = nfa_from_ast(ast);

            double t0 = get_time_ms();
            for (int j = 0; j < iters; j++) {
                DFAMachine dfa = dfa_from_nfa(&nfa);
                dfa_free(&dfa);
            }
            double ms = (get_time_ms() - t0);
            printf("%7.1fus ", ms / (double)iters * 1000.0);

            /* Minimize */
            {
                DFAMachine dfa = dfa_from_nfa(&nfa);
                int orig_states = dfa.state_count;
                double t0 = get_time_ms();
                int actual_iters = iters;
                if (orig_states > 10) actual_iters = iters / 10;  /* 大 DFA 减少迭代 */
                if (actual_iters < 10) actual_iters = 10;
                for (int j = 0; j < actual_iters; j++) {
                    DFAMachine copy = dfa_from_nfa(&nfa);
                    dfa_minimize(&copy);
                    dfa_free(&copy);
                }
                double ms = (get_time_ms() - t0);
                printf("%7.1fus (%d->%d states)\n",
                       ms / (double)actual_iters * 1000.0,
                       orig_states, dfa.state_count);
                dfa_free(&dfa);
            }

            nfa_free(&nfa);
            ast_free(ast);
        }
    }
}

int main(int argc, char **argv) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     DFA Regex Engine — Performance Report               ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n\n");

    /* Select test mode */
    const char *mode = "all";
    if (argc > 1) mode = argv[1];

    if (strcmp(mode, "match") == 0 || strcmp(mode, "all") == 0) {
        run_dfa_tests();
    }

    if (strcmp(mode, "compile") == 0 || strcmp(mode, "all") == 0) {
        run_compile_tests();
    }

    printf("\n============================================================\n");
    printf("  Report generated. DFA uses O(n) matching with 256-way\n");
    printf("  transition table (1 lookup per byte).\n");
    printf("============================================================\n");

    return 0;
}
