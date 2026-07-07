/**
 * ============================================================================
 * 正则引擎 性能基准测试 (Benchmark)
 * ============================================================================
 *
 * 项目背景：
 *   正则表达式引擎是文本处理工具（grep/sed）和编程语言运行时的核心组件。
 *   本项目从零实现支持完整正则语法的引擎，经历「正则 -> NFA -> DFA ->
 *   最小化 DFA」的完整转换链，并与 POSIX regex.h 进行性能对比。
 *   AI 大模型可辅助推导 Thompson 构造规则和子集构造算法。
 *
 * 测试维度：
 *   1. 各模块内部性能 — Tokenizer / NFA / DFA / Hopcroft / Matcher
 *   2. POSIX 兼容层性能 — regcomp / regexec / regfree
 *   3. 自研引擎 vs POSIX regex.h 对比 — 相同模式下的编译与匹配速度
 *   4. 不同输入规模下的缩放行为 — 100B / 1KB / 10KB / 100KB
 *
 * 编译方式：
 *   有 POSIX regex.h 的系统：
 *     gcc -O2 -o benchmark benchmark.c -lregex
 *   无 POSIX regex.h 的系统（仅测试自研引擎）：
 *     gcc -O2 -o benchmark benchmark.c
 *
 * 输出格式：
 *   - 终端输出：人类可读的性能报告
 *   - CSV 导出：结果可导入 Excel / Python 进行可视化
 *
 * 依赖：
 *   - 本项目 api.h / posix_compat.h / capture.h 等
 *   - 可选：系统 <regex.h>（POSIX 对比）
 * ============================================================================
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/time.h>
#endif

/* 自研引擎头文件 */
#include "api.h"
#include "matcher.h"
#include "capture.h"
#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"
#include "tokenizer.h"
#include "posix_compat.h"

/* ========================================================================== */
/*  POSIX regex.h 条件编译支持                                                  */
/* ========================================================================== */

/**
 * 检测平台是否提供 POSIX regex.h。
 * Linux/glibc: <regex.h>
 * macOS/BSD:   <regex.h>
 * MinGW:       通常不提供，需手动链接 libregex
 */
#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__OpenBSD__) || defined(__NetBSD__)
#define HAS_POSIX_REGEX 1
#include <regex.h>
#else
/* 尝试包含 MinGW 或其他平台的 regex.h */
#ifdef __MINGW32__
/* MinGW 可能需要手动安装 libregex */
#include <regex.h>
#define HAS_POSIX_REGEX 1
#else
#define HAS_POSIX_REGEX 0
#endif
#endif

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
/*  测试结果数据结构                                                            */
/* ========================================================================== */

typedef struct {
    const char *category;    /* 测试类别 */
    const char *name;        /* 测试名称 */
    double time_ms;          /* 平均耗时（毫秒） */
    size_t input_size;       /* 输入大小（字节） */
    int iterations;          /* 迭代次数 */
    double ops_per_sec;      /* 每秒操作数（字符/秒 或 次/秒） */
} BenchResult;

static BenchResult results[512];
static int result_count = 0;

static void record_result(const char *category, const char *name,
                          double time_ms, size_t input_size, int iterations) {
    if (result_count >= 512) return;
    BenchResult *r = &results[result_count++];
    r->category = category;
    r->name = name;
    r->time_ms = time_ms;
    r->input_size = input_size;
    r->iterations = iterations;
    if (time_ms > 0) {
        r->ops_per_sec = (iterations * input_size) / (time_ms / 1000.0);
    } else {
        r->ops_per_sec = 0;
    }
}

/* ========================================================================== */
/*  辅助工具                                                                    */
/* ========================================================================== */

/** 生成重复字符的测试文本 */
static char *generate_text(size_t len, char fill) {
    char *text = (char *)malloc(len + 1);
    if (text) {
        memset(text, fill, len);
        text[len] = '\0';
    }
    return text;
}

/** 生成包含数字的测试文本 */
static char *generate_digit_text(size_t len) {
    char *text = (char *)malloc(len + 1);
    if (text) {
        for (size_t i = 0; i < len; i++) {
            text[i] = '0' + (i % 10);
        }
        text[len] = '\0';
    }
    return text;
}

/** 生成包含混合内容的测试文本（模拟真实日志） */
static char *generate_log_text(size_t len) {
    const char *templates[] = {
        "user@example.com logged in from 192.168.1.1",
        "ERROR: connection timeout after 30s",
        "DEBUG: processing request #12345",
        "INFO: file size=1024 bytes, path=/tmp/data.csv",
        "WARN: disk usage at 85%, threshold=90%",
    };
    int n_templates = 5;
    char *text = (char *)malloc(len + 1);
    if (!text) return NULL;

    size_t pos = 0;
    size_t idx = 0;
    while (pos < len) {
        const char *tmpl = templates[idx % n_templates];
        size_t tmpl_len = strlen(tmpl);
        size_t copy_len = tmpl_len;
        if (pos + copy_len > len) copy_len = len - pos;
        memcpy(text + pos, tmpl, copy_len);
        pos += copy_len;
        idx++;
    }
    text[len] = '\0';
    return text;
}

/* ========================================================================== */
/*  1. Tokenizer 基准测试                                                       */
/* ========================================================================== */

static void bench_tokenizer(void) {
    const char *patterns[] = {
        "a",
        "abc",
        "a*b+",
        "[0-9]+",
        "\\d{3}-\\d{4}",
        "(abc|def)+",
        "\\w+@\\w+\\.\\w+",
        "([a-z]+\\.?)+",
        NULL
    };

    printf("\n=== 1. Tokenizer 性能 ===\n");

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 200000;

        for (int j = 0; j < iters; j++) {
            Tokenizer tok;
            tokenizer_init(&tok, patterns[i]);
            Token t;
            do {
                t = tokenizer_next(&tok);
            } while (t.type != TOK_EOF && t.type != TOK_ERROR);
        }

        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("  %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("tokenizer", patterns[i], ms,
                      strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  2. NFA 构建基准测试（Thompson 构造）                                        */
/* ========================================================================== */

static void bench_nfa(void) {
    const char *patterns[] = {
        "a",
        "abc",
        "a*b+",
        "[0-9]+",
        "\\d{3}-\\d{4}",
        "(abc|def)+",
        "a(b|c)*d",
        NULL
    };

    printf("\n=== 2. NFA 构建性能 (Thompson 构造) ===\n");

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 100000;

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

        printf("  %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("nfa", patterns[i], ms,
                      strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  3. DFA 构建基准测试（子集构造法）                                           */
/* ========================================================================== */

static void bench_dfa(void) {
    const char *patterns[] = {
        "a",
        "abc",
        "a*b+",
        "[0-9]+",
        "\\d{3}-\\d{4}",
        "(abc|def)+",
        "a(b|c)*d",
        "([a-z]+\\.?)+",
        NULL
    };

    printf("\n=== 3. DFA 构建性能 (子集构造法) ===\n");

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 50000;

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

        printf("  %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("dfa", patterns[i], ms,
                      strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  4. Hopcroft 最小化基准测试                                                  */
/* ========================================================================== */

static void bench_hopcroft(void) {
    const char *patterns[] = {
        "a*b+",
        "([a-z]+)+",
        "(abc|def|ghi)+",
        "\\d{2,4}[a-z]{1,3}",
        NULL
    };

    printf("\n=== 4. Hopcroft 最小化性能 ===\n");

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
        int iters = 20000;

        for (int j = 0; j < iters; j++) {
            /* 每次重新构建以确保独立的 DFA 实例 */
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

        printf("  %-25s  %8.3f ms  (%d iters, %d states -> %d)\n",
               patterns[i], ms, iters, orig_states,
               dfa.state_count);

        record_result("hopcroft", patterns[i], ms,
                      orig_states, iters);
        dfa_free(&dfa);
    }
}

/* ========================================================================== */
/*  5. 匹配性能基准测试（不同输入规模）                                         */
/* ========================================================================== */

static void bench_match(void) {
    struct {
        const char *pattern;
        const char *text;
        size_t text_len;
        const char *label;
    } tests[] = {
        { "a*",          NULL, 0, "star (零次或多次)" },
        { "a+",          NULL, 0, "plus (一次或多次)" },
        { "[a-z]+",      NULL, 0, "char class (字符集合)" },
        { "\\d{3}-\\d{4}", NULL, 0, "digit pattern (数字模式)" },
        { "\\w+@\\w+\\.\\w+", NULL, 0, "email pattern (邮箱模式)" },
        { "(abc)+",      NULL, 0, "group repeat (组重复)" },
        { "^abc$",       NULL, 0, "anchors (锚定)" },
        { NULL, NULL, 0, NULL },
    };

    /* 不同长度的测试文本 */
    struct {
        size_t len;
        const char *label;
    } sizes[] = {
        { 100,    "100B" },
        { 1000,   "1KB" },
        { 10000,  "10KB" },
        { 100000, "100KB" },
    };

    printf("\n=== 5. 匹配性能 (不同输入规模) ===\n");

    for (int ti = 0; tests[ti].pattern; ti++) {
        for (int si = 0; si < 4; si++) {
            size_t len = sizes[si].len;

            /* 生成合适的测试文本 */
            char *text = NULL;
            if (strstr(tests[ti].pattern, "\\d") ||
                strstr(tests[ti].pattern, "\\D")) {
                text = generate_digit_text(len);
            } else if (strstr(tests[ti].pattern, "@")) {
                text = generate_log_text(len);
            } else if (strstr(tests[ti].pattern, "[a-z]")) {
                text = generate_text(len, 'a' + (ti % 26));
            } else {
                text = generate_text(len, 'a');
            }
            if (!text) continue;

            /* 编译正则 */
            regex_t *prog = regex_compile(tests[ti].pattern, REGEX_FLAG_NONE);
            if (!prog) { free(text); continue; }

            /* 预热：确保 DFA 已构建 */
            MatchResult warmup = {0};
            regex_search(prog, text, &warmup);

            /* 正式测试 */
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

            printf("  %-25s + %-6s  %8.3f ms  (%d iters)\n",
                   tests[ti].label, sizes[si].label, ms, iters);

            record_result("match", tests[ti].pattern, ms, len, iters);

            regex_free(prog);
            free(text);
        }
    }
}

/* ========================================================================== */
/*  6. 捕获组匹配基准测试                                                       */
/* ========================================================================== */

static void bench_capture(void) {
    const char *patterns[] = {
        "(abc)",
        "(abc)(def)",
        "(a+)",
        "((ab)(cd))",
        "(\\w+@\\w+)",
        NULL
    };

    printf("\n=== 6. 捕获组匹配性能 ===\n");

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 20000;
        const char *text = "xxxabcyyy";

        Parser parser;
        parser_init(&parser, patterns[i]);
        ASTNode *ast = parser_parse(&parser);
        if (!ast) continue;

        DFAMachine dfa = dfa_from_ast_with_groups(ast);
        ast_free(ast);
        if (!dfa.states) continue;

        for (int j = 0; j < iters; j++) {
            CapturedMatch cm = dfa_match_captured(&dfa, text);
            captured_match_free(&cm);
        }

        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("  %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("capture", patterns[i], ms,
                      strlen(text), iters);

        dfa_capture_free(&dfa);
    }
}

/* ========================================================================== */
/*  7. 全局匹配基准测试（findall）                                              */
/* ========================================================================== */

static void bench_findall(void) {
    const char *patterns[] = {
        "a",
        "[aeiou]",
        "\\d",
        "\\w+",
        NULL
    };

    size_t text_sizes[] = { 1000, 10000, 100000 };
    const char *labels[] = { "1KB", "10KB", "100KB" };

    printf("\n=== 7. 全局匹配性能 (findall) ===\n");

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
                MatchResult *res = regex_findall(prog, text, &count);
                if (res) regex_findall_free(res);
            }
            double t1 = elapsed_ms();
            double ms = t1 - t0;

            printf("  %-10s + %-6s  %8.3f ms  (%d iters)\n",
                   patterns[pi], labels[si], ms, iters);
            record_result("findall", patterns[pi], ms, len, iters);

            regex_free(prog);
            free(text);
        }
    }
}

/* ========================================================================== */
/*  8. POSIX 兼容层基准测试                                                     */
/* ========================================================================== */

static void bench_posix_compat(void) {
    const char *patterns[] = {
        "abc",
        "a*b+",
        "[0-9]+",
        "\\d{3}-\\d{4}",
        "(abc|def)+",
        "\\w+@\\w+\\.\\w+",
        NULL
    };

    const char *texts[] = {
        "abc",
        "aaaabbb",
        "123456789",
        "123-4567",
        "abcdef",
        "user@example.com",
        NULL
    };

    printf("\n=== 8. POSIX 兼容层性能 (regcomp/regexec/regfree) ===\n");

    for (int i = 0; patterns[i]; i++) {
        /* regcomp 性能 */
        double t0 = elapsed_ms();
        int iters = 50000;

        for (int j = 0; j < iters; j++) {
            regex_prog_t prog;
            int rc = regcomp(&prog, patterns[i], REG_EXTENDED);
            if (rc == 0) {
                regfree(&prog);
            }
        }
        double t1 = elapsed_ms();
        double ms_comp = t1 - t0;

        printf("  regcomp: %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms_comp, iters);
        record_result("posix_regcomp", patterns[i], ms_comp,
                      strlen(patterns[i]), iters);

        /* regexec 性能 */
        if (texts[i]) {
            regex_prog_t prog;
            int rc = regcomp(&prog, patterns[i], REG_EXTENDED);
            if (rc != 0) continue;

            t0 = elapsed_ms();
            iters = 50000;

            for (int j = 0; j < iters; j++) {
                regmatch_t pmatch[1];
                regexec(&prog, texts[i], 1, pmatch, 0);
            }

            t1 = elapsed_ms();
            double ms_exec = t1 - t0;

            printf("  regexec: %-25s  %8.3f ms  (%d iters)\n",
                   patterns[i], ms_exec, iters);
            record_result("posix_regexec", patterns[i], ms_exec,
                          strlen(texts[i]), iters);

            regfree(&prog);
        }
    }
}

/* ========================================================================== */
/*  9. 自研引擎 vs POSIX regex.h 对比测试                                       */
/* ========================================================================== */

#if HAS_POSIX_REGEX
static void bench_posix_comparison(void) {
    struct {
        const char *pattern;
        const char *text;
        const char *label;
    } tests[] = {
        { "abc",          "hello abc world",        "literal match" },
        { "a*b",          "aaab",                   "star quantifier" },
        { "[0-9]+",       "test 12345 end",         "digit class" },
        { "\\w+@\\w+\\.\\w+", "user@example.com",   "email pattern" },
        { "(abc)+",       "abcabcabc",              "group repeat" },
        { "^hello",       "hello world",            "start anchor" },
        { "world$",       "hello world",            "end anchor" },
        { NULL, NULL, NULL },
    };

    /* 不同输入规模 */
    size_t sizes[] = { 100, 1000, 10000, 100000 };
    const char *size_labels[] = { "100B", "1KB", "10KB", "100KB" };

    printf("\n=== 9. 自研引擎 vs POSIX regex.h 对比 ===\n");

    for (int ti = 0; tests[ti].pattern; ti++) {
        for (int si = 0; si < 4; si++) {
            size_t len = sizes[si];
            char *text = generate_text(len, 'a' + (ti % 26));
            if (!text) continue;

            /* ---- 自研引擎 ---- */
            {
                regex_t *prog = regex_compile(tests[ti].pattern, REGEX_FLAG_NONE);
                if (prog) {
                    double t0 = elapsed_ms();
                    int iters = 10000;
                    if (len >= 10000) iters = 1000;
                    if (len >= 100000) iters = 100;

                    MatchResult r;
                    for (int j = 0; j < iters; j++) {
                        regex_search(prog, text, &r);
                    }
                    double t1 = elapsed_ms();
                    double ours_ms = t1 - t0;

                    printf("  [%s] %-20s + %-6s  自研: %8.3f ms  POSIX: ",
                           tests[ti].label, tests[ti].pattern, size_labels[si],
                           ours_ms);
                    fflush(stdout);

                    /* ---- POSIX regex.h ---- */
                    regex_t posix_prog;
                    if (regcomp(&posix_prog, tests[ti].pattern, REG_EXTENDED) == 0) {
                        t0 = elapsed_ms();
                        for (int j = 0; j < iters; j++) {
                            regmatch_t pmatch[1];
                            regexec(&posix_prog, text, 1, pmatch, 0);
                        }
                        t1 = elapsed_ms();
                        double posix_ms = t1 - t0;

                        printf("%8.3f ms", posix_ms);

                        /* 计算加速比 */
                        if (posix_ms > 0) {
                            double speedup = ours_ms / posix_ms;
                            printf("  (%.2fx)", speedup);
                        }
                        regfree(&posix_prog);
                    } else {
                        printf("POSIX编译失败");
                    }

                    printf("\n");
                    regex_free(prog);
                }
            }

            free(text);
        }
    }
}
#else
static void bench_posix_comparison(void) {
    printf("\n=== 9. 自研引擎 vs POSIX regex.h 对比 ===\n");
    printf("  [跳过] 当前平台未提供 POSIX regex.h\n");
}
#endif

/* ========================================================================== */
/*  10. 编译流水线整体性能基准测试                                               */
/* ========================================================================== */

static void bench_full_pipeline(void) {
    const char *patterns[] = {
        "abc",
        "a*b+",
        "[0-9]+",
        "\\d{3}-\\d{4}",
        "(abc|def)+",
        "\\w+@\\w+\\.\\w+",
        "([a-z]+\\.?)+",
        NULL
    };

    printf("\n=== 10. 完整编译流水线性能 ===\n");
    printf("  (regex_compile = tokenizer + parser + nfa + dfa + hopcroft)\n");

    for (int i = 0; patterns[i]; i++) {
        double t0 = elapsed_ms();
        int iters = 10000;

        for (int j = 0; j < iters; j++) {
            regex_t *prog = regex_compile(patterns[i], REGEX_FLAG_NONE);
            if (prog) regex_free(prog);
        }

        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("  %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("pipeline", patterns[i], ms,
                      strlen(patterns[i]), iters);
    }
}

/* ========================================================================== */
/*  11. 内存分配与释放基准测试                                                  */
/* ========================================================================== */

static void bench_memory(void) {
    const char *patterns[] = {
        "a*b+",
        "(abc|def)+",
        "\\w+@\\w+\\.\\w+",
        NULL
    };

    printf("\n=== 11. 内存分配与释放基准 ===\n");

    for (int i = 0; patterns[i]; i++) {
        /* 编译 + 匹配 + 释放 的完整生命周期 */
        double t0 = elapsed_ms();
        int iters = 5000;
        const char *text = "hello abc123 world user@test.com";

        for (int j = 0; j < iters; j++) {
            regex_t *prog = regex_compile(patterns[i], REGEX_FLAG_NONE);
            if (prog) {
                MatchResult r;
                regex_search(prog, text, &r);
                regex_free(prog);
            }
        }

        double t1 = elapsed_ms();
        double ms = t1 - t0;

        printf("  %-25s  %8.3f ms  (%d iters)\n",
               patterns[i], ms, iters);
        record_result("memory", patterns[i], ms,
                      strlen(text), iters);
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
    printf("==================================================\n");
    printf("\n项目背景：\n");
    printf("  正则表达式引擎是文本处理工具（grep/sed）和编程语言运行时的\n");
    printf("  核心组件。本项目从零实现支持完整正则语法的引擎，经历「正则\n");
    printf("  -> NFA -> DFA -> 最小化 DFA」的完整转换链，并与 POSIX\n");
    printf("  regex.h 进行性能对比。\n");
    printf("==================================================\n");

    /* 1. Tokenizer */
    bench_tokenizer();

    /* 2. NFA Construction */
    bench_nfa();

    /* 3. DFA Construction */
    bench_dfa();

    /* 4. Hopcroft Minimization */
    bench_hopcroft();

    /* 5. Matching (different input sizes) */
    bench_match();

    /* 6. Capture Groups */
    bench_capture();

    /* 7. FindAll */
    bench_findall();

    /* 8. POSIX Compat Layer */
    bench_posix_compat();

    /* 9. Self-built Engine vs POSIX regex.h */
    bench_posix_comparison();

    /* 10. Full Pipeline */
    bench_full_pipeline();

    /* 11. Memory Allocation */
    bench_memory();

    /* Output CSV summary */
    output_csv();

    printf("\n==================================================\n");
    printf("  总计 %d 个基准测试项\n", result_count);
    printf("==================================================\n");

    return 0;
}
