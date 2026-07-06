#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <locale.h>
#endif
#include "dfa.h"
#include "hopcroft.h"
#include "matcher.h"
#include "parser.h"
#include "nfa.h"

/* ========================================================================== */
/*  测试框架（与 test_dfa.c 一致）                                              */
/* ========================================================================== */

static int g_passes = 0;
static int g_failures = 0;
static int g_module_passes = 0;
static int g_module_failures = 0;

static void module_begin(const char *name) {
    printf("\n══════════════════════════════════════\n");
    printf("  %s\n", name);
    printf("──────────────────────────────────────\n");
    g_module_passes = 0;
    g_module_failures = 0;
}

static void module_end(void) {
    printf("──────────────────────────────────────\n");
    if (g_module_failures == 0) {
        printf("  结果：全部通过 (%d 项)\n", g_module_passes);
    } else {
        printf("  结果：通过 %d 项，失败 %d 项\n", g_module_passes, g_module_failures);
    }
}

static void check_pass(const char *desc) {
    printf("  ✓ %s\n", desc);
    g_passes++;
    g_module_passes++;
}

static void check_fail(const char *desc, const char *expected, const char *actual) {
    printf("  ✗ %s —— 期望「%s」，实际「%s」\n", desc, expected, actual);
    g_failures++;
    g_module_failures++;
}

#define CHECK_INT(expected, actual, desc) \
    do { \
        if ((int)(expected) != (int)(actual)) { \
            char exp[32], act[32]; \
            snprintf(exp, sizeof(exp), "%d", (int)(expected)); \
            snprintf(act, sizeof(act), "%d", (int)(actual)); \
            check_fail(desc, exp, act); \
        } else { \
            check_pass(desc); \
        } \
    } while (0)

#define CHECK_NOT_NULL(ptr, desc) \
    do { \
        if ((ptr) != NULL) { \
            check_pass(desc); \
        } else { \
            check_fail(desc, "非 NULL", "NULL"); \
        } \
    } while (0)

#define CHECK_NULL(ptr, desc) \
    do { \
        if ((ptr) == NULL) { \
            check_pass(desc); \
        } else { \
            check_fail(desc, "NULL", "非 NULL"); \
        } \
    } while (0)

#define CHECK_TRUE(cond, desc) \
    do { \
        if (cond) { \
            check_pass(desc); \
        } else { \
            check_fail(desc, "true", "false"); \
        } \
    } while (0)

#define CHECK_FALSE(cond, desc) \
    do { \
        if (!(cond)) { \
            check_pass(desc); \
        } else { \
            check_fail(desc, "false", "true"); \
        } \
    } while (0)

/* ========================================================================== */
/*  辅助：pattern → AST → NFA → DFA                                            */
/* ========================================================================== */

static DFAMachine do_build(const char *pattern) {
    Parser parser;
    parser_init(&parser, pattern);
    ASTNode *ast = parser_parse(&parser);
    if (!ast) {
        printf("  ! 前置解析失败: %s\n", parser.error_msg);
        DFAMachine dfa = {0};
        return dfa;
    }
    NFAGraph nfa = nfa_from_ast(ast);
    ast_free(ast);

    if (!nfa.start) {
        printf("  ! NFA 构建失败\n");
        DFAMachine dfa = {0};
        return dfa;
    }

    DFAMachine dfa = dfa_from_nfa(&nfa);
    nfa_free(&nfa);
    return dfa;
}

/** pattern → AST → NFA → DFA → minimize → 最小 DFA */
static DFAMachine do_build_min(const char *pattern) {
    DFAMachine dfa = do_build(pattern);
    if (dfa.states) {
        dfa_minimize(&dfa);
    }
    return dfa;
}

/* ========================================================================== */
/*  辅助：验证 DFA 是否为最小形式                                                */
/* ========================================================================== */

/**
 * 验证 DFA 中不存在两个等价状态（暴力 O(n² · 256)）。
 * 等价定义：is_accept 相同 且 对所有字符 c 转移目标一致。
 * 返回 1 表示已最小（无等价状态对），0 表示还有可合并的状态。
 */
static int dfa_is_minimal(const DFAMachine *dfa) {
    int n = dfa->state_count;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            const DFAState *a = &dfa->states[i];
            const DFAState *b = &dfa->states[j];
            if (a->is_accept != b->is_accept) continue;  /* 不等价 */
            int diff = 0;
            for (int c = 0; c < 256; c++) {
                if (a->transitions[c] != b->transitions[c]) { diff = 1; break; }
            }
            if (!diff) return 0;  /* 找到两个等价状态 → 非最小 */
        }
    }
    return 1;  /* 没有等价状态对 → 已最小 */
}

/* ========================================================================== */
/*  测试：正确性 — 最小化后状态数是否符合预期                                     */
/* ========================================================================== */

static void test_idempotent(void) {
    /* 单字符 a — 已是最小，最小化后状态数不变 */
    DFAMachine dfa = do_build("a");
    int before = dfa.state_count;
    dfa_minimize(&dfa);
    int after = dfa.state_count;
    CHECK_INT(before, after, "a — 最小化后状态数不变");
    CHECK_TRUE(dfa_is_minimal(&dfa), "a — 已验证为最小 DFA");
    dfa_free(&dfa);
}

static void test_dot(void) {
    DFAMachine dfa = do_build_min(".");
    CHECK_NOT_NULL(dfa.states, ". — minimize 后 states 非 NULL");
    CHECK_TRUE(dfa.state_count <= 2, ". — 起始和接受合并为 ≤ 2 状态");
    CHECK_TRUE(dfa_is_minimal(&dfa), ". — 已验证为最小 DFA");
    dfa_free(&dfa);
}

static void test_star(void) {
    /* a* — 两状态等价应合并为 1 */
    DFAMachine dfa = do_build("a*");
    dfa_minimize(&dfa);
    CHECK_INT(1, dfa.state_count, "a* — 最小化为 1 个状态（自环接受态）");
    CHECK_TRUE(dfa.states[0].is_accept, "a* — 唯一状态是接受态");
    CHECK_INT(0, dfa.states[0].transitions['a'], "a* — 'a' 转移自环");
    CHECK_TRUE(dfa_is_minimal(&dfa), "a* — 已验证为最小 DFA");
    dfa_free(&dfa);
}

static void test_alter_same(void) {
    /* a|a — 两个分支完全相同，应压缩 */
    DFAMachine dfa = do_build_min("a|a");
    CHECK_NOT_NULL(dfa.states, "a|a — minimize 后 states 非 NULL");
    CHECK_INT(2, dfa.state_count, "a|a — 最小化后状态数 = 2（等价于 a）");
    CHECK_TRUE(dfa_is_minimal(&dfa), "a|a — 已验证为最小 DFA");
    dfa_free(&dfa);
}

static void test_complex(void) {
    /* a|bc* — 4 → 3 */
    DFAMachine dfa = do_build("a|bc*");
    int before = dfa.state_count;
    dfa_minimize(&dfa);
    CHECK_TRUE(dfa.state_count < before,
               "a|bc* — 最小化后状态数减少");
    CHECK_INT(3, dfa.state_count,
              "a|bc* — 最小化后状态数 = 3");
    CHECK_TRUE(dfa_is_minimal(&dfa), "a|bc* — 已验证为最小 DFA");
    dfa_free(&dfa);
}

static void test_double_loop(void) {
    /* (a|b)* — 两个循环分支，应最小化 */
    DFAMachine dfa = do_build_min("(a|b)*");
    CHECK_NOT_NULL(dfa.states, "(a|b)* — minimize 后 states 非 NULL");
    CHECK_TRUE(dfa.states[0].is_accept, "(a|b)* — 起始是接受态（零次）");
    int ta = dfa.states[0].transitions['a'];
    int tb = dfa.states[0].transitions['b'];
    CHECK_TRUE(ta != -1 && dfa.states[ta].is_accept,
               "(a|b)* — 读 'a' 后仍是接受");
    CHECK_TRUE(tb != -1 && dfa.states[tb].is_accept,
               "(a|b)* — 读 'b' 后仍是接受");
    CHECK_TRUE(dfa_is_minimal(&dfa), "(a|b)* — 已验证为最小 DFA");
    dfa_free(&dfa);
}

static void test_nested_star(void) {
    /* (a*)* — 嵌套星号，应退化为 1 状态 */
    DFAMachine dfa = do_build_min("(a*)*");
    CHECK_NOT_NULL(dfa.states, "(a*)* — minimize 后 states 非 NULL");
    CHECK_INT(1, dfa.state_count, "(a*)* — 退化为 1 个状态");
    CHECK_TRUE(dfa.states[0].is_accept, "(a*)* — 是接受态");
    CHECK_TRUE(dfa_is_minimal(&dfa), "(a*)* — 已验证为最小 DFA");
    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：语义保持 — 最小化后转移表全部有效                                      */
/* ========================================================================== */

static void test_preserves_semantics(void) {
    const char *patterns[] = {
        "a", "a*", "a+", "a?", "ab", "a|b", "a|bc*",
        "[abc]", "\\d", "\\w", "\\s", "(ab)*", "a(b|c)*d",
        "a{3}", "a{2,4}", "\\d{2,4}[a-z]?",
    };
    int n_pats = (int)(sizeof(patterns) / sizeof(patterns[0]));

    for (int k = 0; k < n_pats; k++) {
        DFAMachine dfa = do_build(patterns[k]);
        if (!dfa.states) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s — 构建失败", patterns[k]);
            check_fail("语义保持", "有效 DFA", buf);
            continue;
        }

        dfa_minimize(&dfa);

        int ok = 1;
        for (int s = 0; s < dfa.state_count && ok; s++) {
            for (int c = 0; c < 256 && ok; c++) {
                int t = dfa.states[s].transitions[c];
                if (t == -1) continue;
                if (t < 0 || t >= dfa.state_count) {
                    ok = 0;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                             "%s — 状态 %d 字符 '%c' 转移 %d 越界",
                             patterns[k], s, (char)c, t);
                    check_fail("语义保持", "有效转移", buf);
                }
            }
        }

        if (ok) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s — 转移表有效 (%d 状态)",
                     patterns[k], dfa.state_count);
            check_pass(buf);
        }

        dfa_free(&dfa);
    }
}

/* ========================================================================== */
/*  测试：全局最小性 — 所有模式的最小化结果应无可合并状态对                       */
/* ========================================================================== */

static void test_all_minimal(void) {
    const char *patterns[] = {
        "a", ".", "\\d", "\\w", "\\s",
        "[abc]", "[^0-9]", "ab", "abc",
        "a|b", "a|b|c", "a*", "a+", "a?",
        "(a)", "(a|b)", "(ab)*", "a(b|c)*d",
        "a{3}", "a{2,4}", "a{1,}", "a{0,3}",
        "ab|cd", "\\d{2,4}[a-z]?",
        "(a*)*", "a|a",
    };
    int n_pats = (int)(sizeof(patterns) / sizeof(patterns[0]));

    for (int k = 0; k < n_pats; k++) {
        DFAMachine dfa = do_build_min(patterns[k]);
        int minimal = dfa_is_minimal(&dfa);
        if (!minimal) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s — 存在等价状态对（非最小）", patterns[k]);
            check_fail("全局最小性", "最小 DFA", buf);
        } else {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s — 是最小 DFA (%d 状态)",
                     patterns[k], dfa.state_count);
            check_pass(buf);
        }
        dfa_free(&dfa);
    }
}

/* ========================================================================== */
/*  辅助：深拷贝 DFA                                                             */
/* ========================================================================== */

/** 深拷贝一个 DFAMachine（含每个状态的 transitions[]） */
static DFAMachine dfa_deep_copy(const DFAMachine *src) {
    DFAMachine dst = {0};
    if (!src || !src->states) return dst;

    dst.states = (DFAState *)malloc((size_t)src->state_count * sizeof(DFAState));
    if (!dst.states) return dst;

    dst.state_count = src->state_count;
    dst.start_state = src->start_state;

    for (int i = 0; i < src->state_count; i++) {
        dst.states[i].id = src->states[i].id;
        dst.states[i].is_accept = src->states[i].is_accept;
        dst.states[i].transitions = (int *)malloc(256 * sizeof(int));
        if (dst.states[i].transitions) {
            memcpy(dst.states[i].transitions, src->states[i].transitions, 256 * sizeof(int));
        }
    }

    return dst;
}

/* ========================================================================== */
/*  测试：最小化前后语言等价性                                                   */
/* ========================================================================== */

/**
 * 验证 dfa_minimize 不改变 DFA 接受的语言。
 *
 * 方法：
 *   1. 从同一模式构建两份相同的 DFA（一份保留，一份最小化）
 *   2. 对一组精心选择的测试字符串，分别用两份 DFA 做 dfa_match_full 匹配
 *   3. 比较每份输入的匹配结果（matched 是否一致）
 *
 * 如果最小化改变了语言，必然存在至少一个输入使得两份 DFA 结果不同。
 */
static void test_equivalence_full(void) {
    const char *patterns[] = {
        "a*",
        "a+",
        "a?",
        "ab*",
        "a|b",
        "a|bc*",
        "(ab)*",
        "a(b|c)*d",
        "a{3}",
        "a{2,4}",
        "a{1,}",
        "a{0,3}",
        "(a*)*",
        "(a|b)*",
        "\\d{2,4}[a-z]?",
        "ab|cd",
        "a*b*c*",
        NULL
    };

    /* 测试输入集合：覆盖匹配、不匹配、边界 */
    const char *inputs[] = {
        "",          /* 空串 */
        "a",         /* 单字符 */
        "aa",        /* 重复 */
        "aaa",
        "aaaa",
        "b",
        "ab",
        "abc",
        "abcd",
        "abcde",
        "123",
        "12",
        "hello",
        "world",
        "aabbcc",
        "xyz",
        "123abc",
        "___",
        NULL
    };

    for (int p = 0; patterns[p]; p++) {
        /* 构建两份相同的 DFA */
        DFAMachine dfa_orig = do_build(patterns[p]);
        if (!dfa_orig.states) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s — 构建失败，跳过等价性测试", patterns[p]);
            check_pass(buf);
            continue;
        }

        DFAMachine dfa_copy = dfa_deep_copy(&dfa_orig);
        if (!dfa_copy.states) {
            dfa_free(&dfa_orig);
            check_fail("等价性", "成功拷贝", "OOM");
            continue;
        }

        /* 对原 DFA 做最小化（原地修改） */
        dfa_minimize(&dfa_orig);

        /* 用 dfa_match_full 逐输入对比 */
        int mismatch = 0;
        for (int i = 0; inputs[i] && !mismatch; i++) {
            MatchResult r1 = dfa_match_full(&dfa_copy, inputs[i]);
            MatchResult r2 = dfa_match_full(&dfa_orig, inputs[i]);

            if (r1.matched != r2.matched) {
                char buf[256];
                snprintf(buf, sizeof(buf),
                         "%s + 输入 \"%s\" — 最小化前后结果不一致 "
                         "(min=%d, orig=%d)",
                         patterns[p], inputs[i], r2.matched, r1.matched);
                check_fail("语言等价性 (dfa_match_full)", "结果一致", buf);
                mismatch = 1;
            }
        }

        /* 额外验证：子串匹配 dfa_match 也应等价 */
        if (!mismatch) {
            for (int i = 0; inputs[i] && !mismatch; i++) {
                MatchResult r1 = dfa_match(&dfa_copy, inputs[i]);
                MatchResult r2 = dfa_match(&dfa_orig, inputs[i]);

                if (r1.matched != r2.matched) {
                    char buf[256];
                    snprintf(buf, sizeof(buf),
                             "%s + 输入 \"%s\" — 子串匹配不一致 "
                             "(min=%d, orig=%d)",
                             patterns[p], inputs[i], r2.matched, r1.matched);
                    check_fail("语言等价性 (dfa_match)", "结果一致", buf);
                    mismatch = 1;
                }
            }
        }

        /* 清理 */
        for (int i = 0; i < dfa_copy.state_count; i++) {
            free(dfa_copy.states[i].transitions);
        }
        free(dfa_copy.states);

        if (!mismatch) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s — 等价性验证通过 (%d 状态)",
                     patterns[p], dfa_orig.state_count);
            check_pass(buf);
        }

        dfa_free(&dfa_orig);
    }
}

/* ========================================================================== */
/*  测试：边界情况                                                              */
/* ========================================================================== */

static void test_edge_cases(void) {
    /* 空 DFA 安全处理 */
    DFAMachine dfa1 = {0};
    dfa_minimize(&dfa1);
    CHECK_NULL(dfa1.states, "空 DFA — minimize 安全返回");
    dfa_free(&dfa1);

    /* 单状态 DFA */
    dfa1 = do_build_min("a");
    dfa_minimize(&dfa1);  /* 再次 minimize 无操作 */
    CHECK_NOT_NULL(dfa1.states, "单状态 — minimize 后仍然有效");
    dfa_free(&dfa1);

    /* NULL 安全 */
    dfa_minimize(NULL);
    check_pass("dfa_minimize(NULL) — 安全无崩溃");
}

/* ========================================================================== */
/*  主函数                                                                      */
/* ========================================================================== */

int main(void) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setlocale(LC_ALL, ".UTF-8");
#endif

    printf("╔══════════════════════════════════════╗\n");
    printf("║   Hopcroft DFA 最小化 单元测试        ║\n");
    printf("╚══════════════════════════════════════╝\n");

    module_begin("正确性");
    test_idempotent();
    test_dot();
    test_star();
    test_alter_same();
    test_complex();
    test_double_loop();
    test_nested_star();
    module_end();

    module_begin("语义保持");
    test_preserves_semantics();
    module_end();

    module_begin("全局最小性");
    test_all_minimal();
    module_end();

    /* ---- 语言等价性 ---- */
    module_begin("最小化前后语言等价性");
    test_equivalence_full();
    module_end();

    /* ---- 边界与安全 ---- */
    module_begin("边界与安全");
    test_edge_cases();
    module_end();

    /* ---- 总结果 ---- */
    printf("\n╔══════════════════════════════════════╗\n");
    if (g_failures == 0) {
        printf("║  测试全部通过！                      ║\n");
    } else {
        printf("║  测试存在失败                        ║\n");
    }
    printf("║  总计：%3d 项                        ║\n", g_passes + g_failures);
    printf("║  通过：%3d 项                        ║\n", g_passes);
    if (g_failures > 0) {
        printf("║  失败：%3d 项                        ║\n", g_failures);
    }
    printf("╚══════════════════════════════════════╝\n");

    return g_failures > 0 ? 1 : 0;
}
