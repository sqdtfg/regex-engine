/**
 * posix_system_bench.c — DFA引擎 vs 系统POSIX(PCRE) regex.h 性能直接对比
 *
 * 直接用 <regex.h> 调用系统 regcomp/regexec/regfree (libpcreposix)。
 * 为避免 struct regex_t 命名冲突, 不使用 api.h，只用底层 parser/nfa/dfa/hopcroft。
 *
 * 注意: build/bin/ 目录下需要有 libpcreposix-0.dll 和 libpcre-1.dll
 *
 * 用法（CMake）：
 *   cmake --build . --target run_posix_cmp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ============ 系统 POSIX regex.h ============ */
#include <sys/types.h>
#include <regex.h>

/* ============ 项目底层引擎（不用 api.h）============ */
#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"

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

static void inject_match(char *t, size_t n, const char *pat) {
    size_t mid = n / 2;
    if (strstr(pat, "*") == pat) {
        for (size_t i = mid; i < n - 1; i++) t[i] = pat[0];
        return;
    }
    const char *word = "abc123hello456xyz";
    size_t wlen = strlen(word);
    if (mid + wlen < n) memcpy(t + mid, word, wlen);
}

/* ========================================================================== */
/*  项目 DFA 引擎匹配 (底层 API)                                                */
/* ========================================================================== */

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

static double bench_our_dfa(const char *pattern, const char *text, size_t text_len,
                              int iters, int *out_states) {
    DFAMachine dfa = compile_dfa(pattern);
    if (!dfa.states) return -1.0;
    *out_states = dfa.state_count;

    double t0 = now_ms();
    for (int k = 0; k < iters; k++) {
        size_t ilen = text_len;
        size_t limit = dfa.has_anchor_start ? 0 : ilen;
        for (size_t start = 0; start <= limit; start++) {
            int state = dfa.start_state;
            size_t pos = start;
            int found = 0;
            while (pos <= ilen) {
                if (dfa.states[state].is_accept) { found = 1; break; }
                if (pos == ilen) break;
                int nx = dfa.states[state].transitions[(unsigned char)text[pos]];
                if (nx == -1) break;
                state = nx; pos++;
            }
            if (found) {
                if (dfa.has_anchor_end && pos != ilen) continue;
                break;
            }
        }
    }
    double t1 = now_ms();
    dfa_free(&dfa);
    return (double)(iters * text_len) / ((t1 - t0) / 1000.0) / (1024.0 * 1024.0);
}

/* ========================================================================== */
/*  系统 POSIX regex.h (PCRE libpcreposix-0.dll) 匹配                          */
/* ========================================================================== */

static double bench_sys_posix(const char *pattern, const char *text, size_t text_len,
                                int iters) {
    regex_t preg;           /* 这里用的是系统 <regex.h> 的 regex_t */
    int rc = regcomp(&preg, pattern, REG_EXTENDED);
    if (rc != 0) return -1.0;

    double t0 = now_ms();
    regmatch_t pm[1];
    for (int i = 0; i < iters; i++)
        regexec(&preg, text, 1, pm, 0);
    double t1 = now_ms();
    regfree(&preg);

    return (double)(iters * text_len) / ((t1 - t0) / 1000.0) / (1024.0 * 1024.0);
}

/* ========================================================================== */
/*  入口                                                                        */
/* ========================================================================== */

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    struct {
        const char *pattern;
        const char *label;
    } cases[] = {
        {"a*",               "a*              "},
        {"a+",               "a+              "},
        {"[a-z]+",           "[a-z]+          "},
        {"[0-9]{3}-[0-9]{4}","[0-9]{3}-[0-9]{4}"},
    };
    int ncases = 4;

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  DFA 引擎 vs 系统 POSIX regex.h (PCRE) 性能直接对比           ║\n");
    printf("║  系统使用 libpcreposix-0.dll 的 regcomp/regexec/regfree      ║\n");
    printf("║  验收标准: 我们的 DFA ≥ 系统 POSIX 的 80%%                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");

    printf("%-18s %6s %7s %10s %10s %7s %5s\n",
           "模式", "KB", "迭代", "DFA(MB/s)", "POSIX(MB/s)", "比值", "判定");
    printf("%-18s %6s %7s %10s %10s %7s %5s\n",
           "----", "--", "----", "---------", "----------", "----", "----");

    int sizes[] = {1, 100, 1000};
    int all_pass = 1;

    for (int ci = 0; ci < ncases; ci++) {
        for (int si = 0; si < 3; si++) {
            int size_kb = sizes[si];
            size_t text_sz = (size_t)size_kb * 1024;
            int iters = (size_kb >= 1000) ? 100 : (size_kb >= 100 ? 1000 : 10000);

            char *text = make_text(text_sz, ci * 7 + si);
            if (!text) continue;
            inject_match(text, text_sz, cases[ci].pattern);

            int states = 0;
            double dfa_mbps = bench_our_dfa(cases[ci].pattern, text, text_sz, iters, &states);
            double sys_mbps = bench_sys_posix(cases[ci].pattern, text, text_sz, iters);

            if (dfa_mbps > 0 && sys_mbps > 0) {
                double ratio = dfa_mbps / sys_mbps * 100.0;
                const char *ok = (ratio >= 80.0) ? " ✓" : " ✗";
                if (ratio < 80.0) all_pass = 0;
                printf("%-18s %5d %6d %8.2f   %8.2f   %6.1f%% %s\n",
                       cases[ci].label, size_kb, iters, dfa_mbps, sys_mbps, ratio, ok);
            } else {
                printf("%-18s %5d %6d %8s   %8s   %6s\n",
                       cases[ci].label, size_kb, iters, "N/A", "N/A", "-");
            }
            free(text);
        }
        if (ci < ncases - 1) printf("\n");
    }

    printf("\n============================================================\n");
    if (all_pass)
        printf("  结论: 所有模式 DFA ≥ 系统 POSIX 的 80%%, 全部达标 ✓\n");
    else
        printf("  结论: 部分模式未达标\n");
    printf("============================================================\n");
    return all_pass ? 0 : 1;
}
