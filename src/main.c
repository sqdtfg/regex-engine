/**
 * main.c — 正则表达式引擎答辩演示主程序
 *
 * 用法:
 *   regex_engine "<模式>" "<文本>"    完整编译流水线 + 匹配, 自动生成DOT文件
 *   regex_engine                      运行内置4个示例演示
 *
 * 流水线: Tokenizer → Parser → AST → NFA → DFA(子集构造) → Hopcroft → 匹配
 * DOT文件输出: 项目根目录 DOT/ 下
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <time.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"
#include "api.h"
#include "matcher.h"

/* ========================================================================== */
/*  计时                                                                        */
/* ========================================================================== */

#ifdef _WIN32
static double freq = 0.0;
static void t_init(void) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); freq = (double)f.QuadPart; }
static double t_now(void) { LARGE_INTEGER c; QueryPerformanceCounter(&c); return (double)c.QuadPart / freq; }
#else
static void t_init(void) {}
static double t_now(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9; }
#endif
#define US(s,e) (((e)-(s))*1e6)

/* ========================================================================== */
/*  安全文件名                                                                   */
/* ========================================================================== */

static void safe_name(const char *s, char *out, int max) {
    int j = 0;
    static const char *hex = "0123456789abcdef";
    for (const char *p = s; *p && j < max - 4; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') {
            out[j++] = *p;
        } else {
            out[j++] = '_';
            out[j++] = hex[((unsigned char)*p) >> 4];
            out[j++] = hex[((unsigned char)*p) & 0xf];
        }
    }
    out[j] = '\0';
}

/* ========================================================================== */
/*  横幅                                                                        */
/* ========================================================================== */

static void banner(void) {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║     正则表达式引擎 — 完整编译流水线演示                   ║\n");
    printf("║     Tokenizer → Parser → NFA → DFA → Minimize → Match    ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
}

static void phase(int n, const char *name) {
    printf("\n  ▸ 阶段 %d — %s\n", n, name);
}

static void timing(double us) {
    if (us < 1000.0)      printf("     [耗时] %.1f μs\n", us);
    else if (us < 1e6)    printf("     [耗时] %.2f ms\n", us / 1000.0);
    else                  printf("     [耗时] %.3f s\n", us / 1e6);
}

/* ========================================================================== */
/*  完整流水线 (pattern + text)                                                  */
/* ========================================================================== */

static int run_pipeline(const char *pattern, const char *text) {
    double t0, t1, t_start;
    Parser parser;
    ASTNode *root = NULL;
    NFAGraph nfa = {0};
    DFAMachine dfa = {0};

    t_start = t_now();
    banner();
    printf("\n  正则: \"%s\"\n", pattern);
    if (text && text[0]) printf("  文本: \"%s\"\n", text);

    /* 确保 DOT 输出目录存在 (项目根目录) */
    MKDIR("DOT");

    char safe[128];
    safe_name(pattern, safe, sizeof(safe));

    /* ---- 阶段1: 词法分析 + 语法分析 → AST ---- */
    phase(1, "词法分析 & 语法分析 → AST");
    t0 = t_now();
    parser_init(&parser, pattern);
    root = parser_parse(&parser);
    if (!root) { printf("     [错误] %s\n", parser.error_msg); return 1; }
    t1 = t_now();
    ast_print(root);
    timing(US(t0, t1));

    /* ---- 阶段2: Thompson NFA ---- */
    phase(2, "Thompson 构造 → ε-NFA");
    t0 = t_now();
    nfa = nfa_from_ast(root);
    t1 = t_now();
    if (!nfa.start) { printf("     [错误] NFA 构造失败\n"); ast_free(root); return 1; }
    printf("     NFA 状态数: %d (起始=%d, 接受=%d)\n",
           nfa.state_count, nfa.start->id, nfa.end->id);
    if (nfa.has_anchor_start) printf("     锚定: ^\n");
    if (nfa.has_anchor_end)   printf("     锚定: $\n");
    timing(US(t0, t1));

    /* NFA DOT */
    {
        char path[256]; snprintf(path, sizeof(path), "DOT/nfa_%s.dot", safe);
        if (nfa_dump_dot_file(&nfa, path) == 0)
            printf("     [DOT] %s\n", path);
    }

    /* ---- 阶段3: 子集构造 DFA ---- */
    phase(3, "子集构造 → DFA");
    t0 = t_now();
    dfa = dfa_from_nfa(&nfa);
    t1 = t_now();
    if (!dfa.states) { printf("     [错误] DFA 构造失败\n"); nfa_free(&nfa); ast_free(root); return 1; }
    printf("     最小化前 DFA 状态数: %d\n", dfa.state_count);
    timing(US(t0, t1));

    /* DFA 最小化前 DOT */
    {
        char path[256]; snprintf(path, sizeof(path), "DOT/dfa_before_%s.dot", safe);
        if (dfa_dump_dot_file(&dfa, path) == 0)
            printf("     [DOT] %s\n", path);
    }

    printf("\n     ── DFA 转移表 (最小化前) ──\n");
    dfa_dump(&dfa);

    /* ---- 阶段4: Hopcroft 最小化 ---- */
    phase(4, "Hopcroft 算法 → DFA 最小化");
    int before = dfa.state_count;
    t0 = t_now();
    dfa_minimize(&dfa);
    t1 = t_now();
    int after = dfa.state_count;
    printf("     %d → %d 状态", before, after);
    if (before > after) printf("  (压缩 %.1f%%)", (1.0 - (double)after/before)*100.0);
    printf("\n");
    timing(US(t0, t1));

    /* DFA 最小化后 DOT */
    {
        char path[256]; snprintf(path, sizeof(path), "DOT/dfa_min_%s.dot", safe);
        if (dfa_dump_dot_file(&dfa, path) == 0)
            printf("     [DOT] %s\n", path);
    }

    printf("\n     ── DFA 转移表 (最小化后) ──\n");
    dfa_dump(&dfa);

    /* ---- 阶段5: 匹配 ---- */
    if (text && text[0]) {
        phase(5, "DFA 匹配 → 结果");
        t0 = t_now();
        MatchResult mr = dfa_match_full(&dfa, text);
        if (!mr.matched) mr = dfa_match(&dfa, text);
        t1 = t_now();
        if (mr.matched) {
            printf("     [匹配] \"%.*s\" [%zu, %zu) 长度=%zu\n",
                   (int)mr.length, text + mr.start, mr.start, mr.end, mr.length);
        } else {
            printf("     [未匹配] \"%s\" 中未找到 \"%s\"\n", text, pattern);
        }
        timing(US(t0, t1));

        /* findAll */
        int cnt = 0;
        MatchResult results[64];
        t0 = t_now();
        cnt = dfa_match_all(&dfa, text, results, 64);
        t1 = t_now();
        if (cnt > 0) {
            printf("\n     findAll: %d 个匹配 (%.1f μs)\n", cnt, US(t0, t1));
            for (int i = 0; i < cnt && i < 8; i++)
                printf("       [%d] \"%.*s\" [%zu,%zu)\n", i+1,
                       (int)results[i].length, text + results[i].start,
                       results[i].start, results[i].end);
        }
    }

    /* 汇总 */
    double total = t_now();
    printf("\n  ╔══════════════════════════════════════════════╗\n");
    printf("  ║  流水线总耗时: %-31.2f ms ║\n", (total - t_start) * 1000.0);
    printf("  ╚══════════════════════════════════════════════╝\n\n");

    dfa_free(&dfa);
    nfa_free(&nfa);
    ast_free(root);
    return 0;
}

/* ========================================================================== */
/*  内置演示 (无参数模式)                                                        */
/* ========================================================================== */

typedef struct {
    const char *title, *pattern, *text, *desc;
} Demo;

static void run_demos(void) {
    Demo d[] = {
        {"基础连接与子串搜索", "abc", "xabcyabcz",
         "普通字符序列 abc, 在文本中搜索子串匹配"},
        {"交替与Kleene闭包", "a|bc*", "bccc",
         "分支(a)或(b后零或多个c), 展示NFA分支+ε转移"},
        {"字符类与量词组合", "[a-zA-Z]+[0-9]?", "Hello42World",
         "字母+可选数字, 展示字符集合区间与findAll多匹配"},
        {"锚定与全串匹配", "^The.*end\\.$", "The quick fox reached the end.",
         "^行首+.*任意序列+$行尾, 展示零宽度断言"},
    };
    int n = sizeof(d) / sizeof(d[0]);

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║    正则表达式引擎 — 内置示例演示                          ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");

    MKDIR("DOT");

    for (int i = 0; i < n; i++) {
        printf("\n┌─ 示例 %d ──────────────────────────────────────────────┐\n", i+1);
        printf("│  %-54s │\n", d[i].title);
        printf("│  模式: %-48s │\n", d[i].pattern);
        printf("│  文本: %-48s │\n", d[i].text);
        printf("│  %-54s │\n", d[i].desc);
        printf("└─────────────────────────────────────────────────────────┘\n");

        double t0 = t_now();
        regex_t *prog = regex_compile(d[i].pattern, REGEX_FLAG_NONE);
        double t1 = t_now();
        if (!prog) { printf("     [错误] 编译失败\n"); continue; }
        printf("     编译: %.2f ms | DFA状态: %d\n",
               US(t0, t1) / 1000.0, prog->dfa.state_count);

        /* 生成 DOT 文件 */
        {
            char safe[128]; safe_name(d[i].pattern, safe, sizeof(safe));

            /* 重建 DFA 用于 DOT */
            Parser p; parser_init(&p, d[i].pattern);
            ASTNode *a = parser_parse(&p);
            NFAGraph nf = nfa_from_ast(a);
            DFAMachine df = dfa_from_nfa(&nf);
            {
                char path[256]; snprintf(path, sizeof(path), "DOT/nfa_%s.dot", safe);
                nfa_dump_dot_file(&nf, path);
            }
            {
                char path[256]; snprintf(path, sizeof(path), "DOT/dfa_before_%s.dot", safe);
                dfa_dump_dot_file(&df, path);
            }
            dfa_minimize(&df);
            {
                char path[256]; snprintf(path, sizeof(path), "DOT/dfa_min_%s.dot", safe);
                dfa_dump_dot_file(&df, path);
            }
            dfa_free(&df); nfa_free(&nf); ast_free(a);
            printf("     [DOT] DOT/nfa_%s.dot, DOT/dfa_before_%s.dot, DOT/dfa_min_%s.dot\n",
                   safe, safe, safe);
        }

        /* 匹配 */
        double t2 = t_now();
        MatchResult mr; int ok = regex_match(prog, d[i].text, &mr);
        if (!ok) ok = regex_search(prog, d[i].text, &mr);
        double t3 = t_now();
        if (ok) printf("     匹配: \"%.*s\" [%zu,%zu) | %.1f μs\n",
                       (int)mr.length, d[i].text + mr.start, mr.start, mr.end, US(t2, t3));
        else printf("     匹配: 未匹配 | %.1f μs\n", US(t2, t3));

        /* findAll */
        int cnt; double t4 = t_now();
        MatchResult *all = regex_findall(prog, d[i].text, &cnt);
        double t5 = t_now();
        if (all && cnt > 0) {
            printf("     findAll: %d个 | %.1f μs\n", cnt, US(t4, t5));
            regex_findall_free(all);
        }

        regex_free(prog);
    }

    printf("\n  ╔══════════════════════════════════════════════════════════╗\n");
    printf("  ║  演示完成。DOT文件在 DOT/ 目录下。                        ║\n");
    printf("  ║  regex_engine \"<模式>\" \"<文本>\" 查看完整流水线         ║\n");
    printf("  ╚══════════════════════════════════════════════════════════╝\n\n");
}

/* ========================================================================== */
/*  main                                                                        */
/* ========================================================================== */

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    t_init();

    if (argc >= 3)
        return run_pipeline(argv[1], argv[2]);
    else if (argc == 2)
        return run_pipeline(argv[1], "");
    else {
        run_demos();
        return 0;
    }
}
