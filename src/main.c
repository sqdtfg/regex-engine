/**
 * ============================================================================
 * 正则表达式引擎 — 答辩演示主程序
 * ============================================================================
 *
 * 用法:
 *   regex_engine "<模式>" "<文本>"    逐步展示完整编译流水线 (带计时)
 *   regex_engine "<模式>"             仅展示编译流水线 (文本为空)
 *   regex_engine                      运行内置示例演示 (4 个精选案例)
 *
 * 流水线: Tokenizer → Parser → AST → NFA (Thompson) → DFA (子集构造)
 *         → Hopcroft 最小化 → DFA 匹配
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif

#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"
#include "api.h"

/* ========================================================================== */
/*  高精度计时 (秒为单位，展示时转为 μs / ms)                                    */
/* ========================================================================== */

#ifdef _WIN32
static double timer_freq = 0.0;

static void timer_init(void) {
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    timer_freq = (double)f.QuadPart;
}

static double timer_now(void) {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / timer_freq;
}
#else
static void timer_init(void) { /* no-op */ }

static double timer_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}
#endif

/** 返回两个时间戳之间的微秒数 */
#define ELAPSED_US(start, end) (((end) - (start)) * 1e6)

/* ========================================================================== */
/*  输出辅助                                                                    */
/* ========================================================================== */

static void print_phase(int num, const char *name) {
    printf("\n  ▸ 阶段 %d — %s\n", num, name);
}

static void print_timing(double us) {
    if (us < 1000.0)
        printf("     [计时] %.1f μs\n", us);
    else if (us < 1000000.0)
        printf("     [计时] %.2f ms\n", us / 1000.0);
    else
        printf("     [计时] %.3f s\n", us / 1000000.0);
}

/* ========================================================================== */
/*  横幅                                                                        */
/* ========================================================================== */

static void print_banner(void) {
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║       正则表达式引擎 — 完整编译流水线演示                 ║\n");
    printf("║       Tokenizer -> Parser -> NFA -> DFA -> Minimize -> Match  ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
}

/* ========================================================================== */
/*  逐阶段流水线 (pattern + text 参数模式)                                      */
/*  手动逐步调用每个阶段，展示中间结果和分步计时                                  */
/* ========================================================================== */

static int run_pipeline(const char *pattern, const char *text) {
    double t0, t1, t_start;
    Parser parser;
    ASTNode *root = NULL;
    NFAGraph nfa = {0};
    DFAMachine dfa = {0};

    t_start = timer_now();

    print_banner();
    printf("\n  正则表达式: \"%s\"\n", pattern);
    printf("  待匹配文本: \"%s\"\n", text);

    /* ------------------------------------------------------------------ */
    /*  阶段 1: 词法分析 + 语法分析 → AST                                   */
    /* ------------------------------------------------------------------ */
    print_phase(1, "词法分析 & 语法分析 → 抽象语法树 (AST)");
    t0 = timer_now();
    parser_init(&parser, pattern);
    root = parser_parse(&parser);
    if (!root) {
        printf("     [错误] 解析失败: %s\n", parser.error_msg);
        return 1;
    }
    t1 = timer_now();
    printf("     AST 语法树:\n");
    ast_print(root);
    print_timing(ELAPSED_US(t0, t1));

    /* ------------------------------------------------------------------ */
    /*  阶段 2: Thompson 构造 → NFA                                        */
    /* ------------------------------------------------------------------ */
    print_phase(2, "Thompson 构造法 → 非确定性有限自动机 (ε-NFA)");
    t0 = timer_now();
    nfa = nfa_from_ast(root);
    t1 = timer_now();
    if (!nfa.start) {
        printf("     [错误] NFA 构造失败\n");
        ast_free(root);
        return 1;
    }
    printf("     NFA 状态总数: %d\n", nfa.state_count);
    printf("     起始状态: %d    接受状态: %d\n",
           nfa.start ? nfa.start->id : -1,
           nfa.end   ? nfa.end->id   : -1);
    if (nfa.has_anchor_start) printf("     锚定: ^ (行首)\n");
    if (nfa.has_anchor_end)   printf("     锚定: $ (行尾)\n");
    print_timing(ELAPSED_US(t0, t1));

    /* ------------------------------------------------------------------ */
    /*  阶段 3: 子集构造 → DFA                                              */
    /* ------------------------------------------------------------------ */
    print_phase(3, "子集构造法 (Subset Construction) → DFA");
    t0 = timer_now();
    dfa = dfa_from_nfa(&nfa);
    t1 = timer_now();
    if (!dfa.states || dfa.state_count <= 0) {
        printf("     [错误] DFA 构造失败\n");
        nfa_free(&nfa);
        ast_free(root);
        return 1;
    }
    printf("     最小化前 DFA 状态数: %d\n", dfa.state_count);
    print_timing(ELAPSED_US(t0, t1));

    /* ---- 打印最小化前 DFA 转移表 ---- */
    printf("\n     ── 最小化前 DFA 状态转移表 ──\n");
    dfa_dump(&dfa);

    /* ------------------------------------------------------------------ */
    /*  阶段 4: Hopcroft 最小化                                             */
    /* ------------------------------------------------------------------ */
    print_phase(4, "Hopcroft 算法 → DFA 最小化");
    int before_min = dfa.state_count;
    t0 = timer_now();
    dfa_minimize(&dfa);
    t1 = timer_now();
    int after_min = dfa.state_count;
    printf("     最小化前: %d 个状态\n", before_min);
    printf("     最小化后: %d 个状态\n", after_min);
    if (before_min > after_min) {
        double ratio = (1.0 - (double)after_min / before_min) * 100.0;
        printf("     状态压缩率: %.1f%%  (%d → %d)\n", ratio, before_min, after_min);
    } else {
        printf("     已是最小 DFA，无需压缩\n");
    }
    print_timing(ELAPSED_US(t0, t1));

    /* ---- 打印最小化后 DFA 转移表 ---- */
    printf("\n     ── 最小化后 DFA 状态转移表 ──\n");
    dfa_dump(&dfa);

    /* ------------------------------------------------------------------ */
    /*  阶段 5: DFA 匹配                                                    */
    /* ------------------------------------------------------------------ */
    print_phase(5, "DFA 单遍扫描匹配 → 结果");
    t0 = timer_now();
    /* 优先全串匹配 (anchored)，失败则子串搜索 */
    MatchResult mr = dfa_match_full(&dfa, text);
    if (!mr.matched) {
        mr = dfa_match(&dfa, text);
    }
    t1 = timer_now();

    if (mr.matched) {
        printf("     [结果] 匹配成功\n");
        printf("     匹配文本: \"%.*s\"\n", (int)mr.length, text + mr.start);
        printf("     匹配位置: [%zu, %zu)  匹配长度: %zu\n",
               mr.start, mr.end, mr.length);
    } else {
        printf("     [结果] 未匹配\n");
        printf("     文本 \"%s\" 中未找到匹配模式 \"%s\" 的子串\n", text, pattern);
    }
    print_timing(ELAPSED_US(t0, t1));

    /* ---- 额外: findall 展示 ---- */
    {
        int count = 0;
        MatchResult results[64];
        t0 = timer_now();
        count = dfa_match_all(&dfa, text, results, 64);
        t1 = timer_now();
        if (count > 0) {
            printf("\n     findAll 全局搜索: 共 %d 个匹配 (%.1f μs)\n",
                   count, ELAPSED_US(t0, t1));
            for (int i = 0; i < count && i < 8; i++) {
                printf("       [%d] [%zu,%zu) = \"%.*s\"\n",
                       i + 1, results[i].start, results[i].end,
                       (int)results[i].length, text + results[i].start);
            }
            if (count > 8) printf("       ... (共 %d 个)\n", count);
        }
    }

    /* ------------------------------------------------------------------ */
    /*  汇总                                                                 */
    /* ------------------------------------------------------------------ */
    double t_total = timer_now();
    printf("\n  ╔══════════════════════════════════════════════╗\n");
    printf("  ║  流水线总耗时: %-31.2f ms ║\n",
           (t_total - t_start) * 1000.0);
    printf("  ╚══════════════════════════════════════════════╝\n\n");

    /* 清理 */
    dfa_free(&dfa);
    nfa_free(&nfa);
    ast_free(root);
    return 0;
}

/* ========================================================================== */
/*  内置示例演示 (无参数模式)                                                    */
/*  4 个精选案例，覆盖核心功能，使用 regex_compile 高级 API                       */
/* ========================================================================== */

typedef struct {
    const char *title;
    const char *pattern;
    const char *text;
    const char *description;
} DemoExample;

static void run_builtin_demo(void) {
    DemoExample examples[] = {
        {
            "基础连接与子串搜索",
            "abc",
            "xabcyabcz",
            "普通字符序列 abc，在文本中搜索子串匹配 — 验证最基础的 DFA 匹配"
        },
        {
            "交替与 Kleene 闭包",
            "a|bc*",
            "bccc",
            "分支 (a) 或 (b 后零或多个 c) — 展示 NFA 分支 + ε 转移, Thompson 构造核心"
        },
        {
            "字符类与量词组合",
            "[a-zA-Z]+[0-9]?",
            "Hello42World",
            "一个或多个字母 + 可选数字 — 展示字符集合区间、量词嵌套与 findAll 多匹配"
        },
        {
            "锚定与全串匹配",
            "^The.*end\\.$",
            "The quick fox reached the end.",
            "^ 行首 + .* 任意序列 + 行尾 $ — 展示零宽度断言与全串锚定匹配"
        },
    };
    int n = sizeof(examples) / sizeof(examples[0]);

    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║    正则表达式引擎 — 内置示例演示                          ║\n");
    printf("║    使用 regex_compile 高级 API (编译+最小化+匹配)         ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");

    for (int i = 0; i < n; i++) {
        DemoExample *ex = &examples[i];

        /* ---- 示例标题 ---- */
        printf("\n┌─ 示例 %d ──────────────────────────────────────────────┐\n", i + 1);
        printf("│  %-54s │\n", ex->title);
        printf("│  模式: %-48s │\n", ex->pattern);
        printf("│  文本: %-48s │\n", ex->text);
        printf("│  %-54s │\n", ex->description);
        printf("└─────────────────────────────────────────────────────────┘\n");

        /* ---- 编译 ---- */
        double t0 = timer_now();
        regex_t *prog = regex_compile(ex->pattern, REGEX_FLAG_NONE);
        double t1 = timer_now();

        if (!prog) {
            printf("     [错误] 编译失败: %s\n", regex_error(REGEX_ERR_PARSE));
            continue;
        }
        printf("     编译: %.2f ms  |  DFA 状态数: %d\n",
               ELAPSED_US(t0, t1) / 1000.0, prog->dfa.state_count);

        /* ---- 匹配 (全串优先, 再子串) ---- */
        double t2 = timer_now();
        MatchResult mr;
        int matched = regex_match(prog, ex->text, &mr);
        if (!matched) matched = regex_search(prog, ex->text, &mr);
        double t3 = timer_now();

        if (matched) {
            printf("     匹配: \"%.*s\"  [%zu, %zu)  |  %.1f μs\n",
                   (int)mr.length, ex->text + mr.start,
                   mr.start, mr.end,
                   ELAPSED_US(t2, t3));
        } else {
            printf("     匹配: 未匹配  |  %.1f μs\n", ELAPSED_US(t2, t3));
        }

        /* ---- findAll ---- */
        int count = 0;
        double t4 = timer_now();
        MatchResult *all = regex_findall(prog, ex->text, &count);
        double t5 = timer_now();

        if (all && count > 0) {
            printf("     findAll: %d 个匹配  |  %.1f μs\n",
                   count, ELAPSED_US(t4, t5));
            for (int j = 0; j < count && j < 6; j++) {
                printf("       [%d] \"%.*s\"  [%zu, %zu)\n",
                       j + 1,
                       (int)all[j].length, ex->text + all[j].start,
                       all[j].start, all[j].end);
            }
            if (count > 6) printf("       ... 及其他 %d 个匹配\n", count - 6);
            regex_findall_free(all);
        }

        regex_free(prog);
    }

    /* ---- 收尾 ---- */
    printf("\n  ╔══════════════════════════════════════════════════════╗\n");
    printf("  ║  演示完成。使用以下命令查看完整编译流水线:             ║\n");
    printf("  ║    regex_engine \"<模式>\" \"<文本>\"                   ║\n");
    printf("  ╚══════════════════════════════════════════════════════╝\n\n");
}

/* ========================================================================== */
/*  main                                                                        */
/* ========================================================================== */

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    timer_init();

    if (argc >= 3) {
        /* 完整流水线: regex_engine "<pattern>" "<text>" */
        return run_pipeline(argv[1], argv[2]);
    } else if (argc == 2) {
        /* 仅 pattern: 文本为空，展示编译流水线 */
        printf("  提示: 提供第二个参数作为匹配文本可查看匹配结果\n");
        printf("  用法: regex_engine \"<模式>\" \"<文本>\"\n\n");
        return run_pipeline(argv[1], "");
    } else {
        /* 无参数: 内置示例 */
        run_builtin_demo();
        return 0;
    }
}
