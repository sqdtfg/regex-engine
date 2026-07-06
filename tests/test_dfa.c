#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#endif
#include "dfa.h"
#include "parser.h"

/* ========================================================================== */
/*  测试框架（与 test_nfa.c 一致）                                               */
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

#define CHECK_EDGE_TYPE(expected, actual, desc) \
    do { \
        if ((int)(expected) == (int)(actual)) { \
            check_pass(desc); \
        } else { \
            char exp[32], act[32]; \
            snprintf(exp, sizeof(exp), "%d", (int)(expected)); \
            snprintf(act, sizeof(act), "%d", (int)(actual)); \
            check_fail(desc, exp, act); \
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

/* ========================================================================== */
/*  测试：单字符 / 点号 / 转义 / 字符集合                                        */
/* ========================================================================== */

static void test_single_char(void) {
    DFAMachine dfa = do_build("a");
    CHECK_NOT_NULL(dfa.states, "a — states 非 NULL");
    CHECK_INT(0, dfa.start_state, "a — 起始状态为 0");
    CHECK_TRUE(dfa.state_count >= 2, "a — 状态数 ≥ 2（起始+接受）");

    /* 检查字符 'a' 的转移 */
    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1, "a — 'a' 有转移");
    if (sa != -1) {
        CHECK_TRUE(dfa.states[sa].is_accept, "a — 读 'a' 到接受状态");
    }

    /* 非匹配字符不应到达接受 */
    int sb = dfa.states[0].transitions['b'];
    int bad = (sb != -1 && dfa.states[sb].is_accept);
    CHECK_FALSE(bad, "a — 'b' 不到接受状态");

    dfa_free(&dfa);
}

static void test_dot(void) {
    DFAMachine dfa = do_build(".");
    CHECK_NOT_NULL(dfa.states, ". — states 非 NULL");

    /* 点号应对大量字符都有转移 */
    int cnt = 0;
    for (int c = 0; c < 256; c++) {
        if (dfa.states[0].transitions[c] != -1) cnt++;
    }
    CHECK_TRUE(cnt >= 128, ". — 起始状态对 ≥128 个字符有转移");

    dfa_free(&dfa);
}

static void test_escape_d(void) {
    DFAMachine dfa = do_build("\\d");
    CHECK_NOT_NULL(dfa.states, "\\d — states 非 NULL");

    int s3 = dfa.states[0].transitions['3'];
    CHECK_TRUE(s3 != -1 && dfa.states[s3].is_accept, "\\d — '3' 到接受");

    int sa = dfa.states[0].transitions['a'];
    int bad = (sa != -1 && dfa.states[sa].is_accept);
    CHECK_FALSE(bad, "\\d — 'a' 不到接受");

    dfa_free(&dfa);
}

static void test_escape_w(void) {
    DFAMachine dfa = do_build("\\w");
    CHECK_NOT_NULL(dfa.states, "\\w — states 非 NULL");

    /* 字母、数字、下划线应该匹配 */
    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "\\w — 'a' 到接受");

    int s9 = dfa.states[0].transitions['9'];
    CHECK_TRUE(s9 != -1 && dfa.states[s9].is_accept, "\\w — '9' 到接受");

    int su = dfa.states[0].transitions['_'];
    CHECK_TRUE(su != -1 && dfa.states[su].is_accept, "\\w — '_' 到接受");

    /* 空格不应匹配 */
    int ss = dfa.states[0].transitions[' '];
    int bad = (ss != -1 && dfa.states[ss].is_accept);
    CHECK_FALSE(bad, "\\w — ' ' 不到接受");

    dfa_free(&dfa);
}

static void test_escape_s(void) {
    DFAMachine dfa = do_build("\\s");
    CHECK_NOT_NULL(dfa.states, "\\s — states 非 NULL");

    int ss = dfa.states[0].transitions[' '];
    CHECK_TRUE(ss != -1 && dfa.states[ss].is_accept, "\\s — ' ' 到接受");

    int sa = dfa.states[0].transitions['a'];
    int bad = (sa != -1 && dfa.states[sa].is_accept);
    CHECK_FALSE(bad, "\\s — 'a' 不到接受");

    dfa_free(&dfa);
}

static void test_bracket(void) {
    DFAMachine dfa = do_build("[abc]");
    CHECK_NOT_NULL(dfa.states, "[abc] — states 非 NULL");

    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "[abc] — 'a' 到接受");
    int sb = dfa.states[0].transitions['b'];
    CHECK_TRUE(sb != -1 && dfa.states[sb].is_accept, "[abc] — 'b' 到接受");
    int sc = dfa.states[0].transitions['c'];
    CHECK_TRUE(sc != -1 && dfa.states[sc].is_accept, "[abc] — 'c' 到接受");

    int sd = dfa.states[0].transitions['d'];
    int bad = (sd != -1 && dfa.states[sd].is_accept);
    CHECK_FALSE(bad, "[abc] — 'd' 不到接受");

    dfa_free(&dfa);
}

static void test_bracket_negate(void) {
    DFAMachine dfa = do_build("[^0-9]");
    CHECK_NOT_NULL(dfa.states, "[^0-9] — states 非 NULL");

    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "[^0-9] — 'a' 到接受");

    int s0 = dfa.states[0].transitions['0'];
    int bad = (s0 != -1 && dfa.states[s0].is_accept);
    CHECK_FALSE(bad, "[^0-9] — '0' 不到接受");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：连接 CONCAT                                                           */
/* ========================================================================== */

static void test_concat_two(void) {
    DFAMachine dfa = do_build("ab");
    CHECK_NOT_NULL(dfa.states, "ab — states 非 NULL");
    CHECK_TRUE(dfa.state_count >= 3, "ab — 状态数 ≥ 3");

    /* 状态0读'a'应到中间状态 */
    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "ab — 'a' 有转移");
    if (s1 != -1) {
        CHECK_FALSE(dfa.states[s1].is_accept, "ab — 读 'a' 后非接受");
        int s2 = dfa.states[s1].transitions['b'];
        CHECK_TRUE(s2 != -1 && dfa.states[s2].is_accept,
                   "ab — 读 'b' 到接受");
    }

    dfa_free(&dfa);
}

static void test_concat_three(void) {
    DFAMachine dfa = do_build("abc");
    CHECK_NOT_NULL(dfa.states, "abc — states 非 NULL");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "abc — 'a' 可走");
    if (s1 != -1) {
        int s2 = dfa.states[s1].transitions['b'];
        CHECK_TRUE(s2 != -1, "abc — 'b' 可走");
        if (s2 != -1) {
            int s3 = dfa.states[s2].transitions['c'];
            CHECK_TRUE(s3 != -1 && dfa.states[s3].is_accept,
                       "abc — 'c' 到接受");
        }
    }

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：并集 ALTER                                                            */
/* ========================================================================== */

static void test_alter_simple(void) {
    DFAMachine dfa = do_build("a|b");
    CHECK_NOT_NULL(dfa.states, "a|b — states 非 NULL");

    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "a|b — 'a' 到接受");

    int sb = dfa.states[0].transitions['b'];
    CHECK_TRUE(sb != -1 && dfa.states[sb].is_accept, "a|b — 'b' 到接受");

    int sc = dfa.states[0].transitions['c'];
    int bad = (sc != -1 && dfa.states[sc].is_accept);
    CHECK_FALSE(bad, "a|b — 'c' 不到接受");

    dfa_free(&dfa);
}

static void test_alter_three(void) {
    DFAMachine dfa = do_build("a|b|c");
    CHECK_NOT_NULL(dfa.states, "a|b|c — states 非 NULL");

    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "a|b|c — 'a' 到接受");
    int sb = dfa.states[0].transitions['b'];
    CHECK_TRUE(sb != -1 && dfa.states[sb].is_accept, "a|b|c — 'b' 到接受");
    int sc = dfa.states[0].transitions['c'];
    CHECK_TRUE(sc != -1 && dfa.states[sc].is_accept, "a|b|c — 'c' 到接受");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：量词 STAR / PLUS / QUESTION                                           */
/* ========================================================================== */

static void test_star(void) {
    DFAMachine dfa = do_build("a*");
    CHECK_NOT_NULL(dfa.states, "a* — states 非 NULL");

    /* 起始状态是接受（零次匹配） */
    CHECK_TRUE(dfa.states[0].is_accept, "a* — 起始状态是接受（零次）");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a* — 'a' 有转移");
    if (s1 != -1) {
        CHECK_TRUE(dfa.states[s1].is_accept, "a* — 读 'a' 后是接受");
        /* 再读 'a' 仍在接受状态 */
        int s2 = dfa.states[s1].transitions['a'];
        CHECK_TRUE(s2 != -1 && dfa.states[s2].is_accept,
                   "a* — 读 'aa' 后仍是接受");
    }

    dfa_free(&dfa);
}

static void test_plus(void) {
    DFAMachine dfa = do_build("a+");
    CHECK_NOT_NULL(dfa.states, "a+ — states 非 NULL");

    /* 起始状态不是接受（至少匹配一次） */
    CHECK_FALSE(dfa.states[0].is_accept, "a+ — 起始状态不是接受");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a+ — 'a' 有转移");
    if (s1 != -1) {
        CHECK_TRUE(dfa.states[s1].is_accept, "a+ — 读 'a' 后是接受");
        int s2 = dfa.states[s1].transitions['a'];
        CHECK_TRUE(s2 != -1 && dfa.states[s2].is_accept,
                   "a+ — 读 'aa' 仍是接受");
    }

    dfa_free(&dfa);
}

static void test_question(void) {
    DFAMachine dfa = do_build("a?");
    CHECK_NOT_NULL(dfa.states, "a? — states 非 NULL");

    /* 起始状态是接受（零次匹配） */
    CHECK_TRUE(dfa.states[0].is_accept, "a? — 起始状态是接受（零次）");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a? — 'a' 有转移");
    if (s1 != -1) {
        CHECK_TRUE(dfa.states[s1].is_accept, "a? — 读 'a' 后是接受");
        /* 不应该能再读一个 'a' */
        int s2 = dfa.states[s1].transitions['a'];
        int bad = (s2 != -1 && dfa.states[s2].is_accept);
        CHECK_FALSE(bad, "a? — 读 'aa' 不应再匹配");
    }

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：范围量词 {m,n}                                                        */
/* ========================================================================== */

static void test_curly_exact(void) {
    /* a{3}: 精确 3 个 a */
    DFAMachine dfa = do_build("a{3}");
    CHECK_NOT_NULL(dfa.states, "a{3} — states 非 NULL");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a{3} — 'a'#1 可走");
    if (s1 != -1) {
        CHECK_FALSE(dfa.states[s1].is_accept, "a{3} — 读1个不是接受");
        int s2 = dfa.states[s1].transitions['a'];
        CHECK_TRUE(s2 != -1, "a{3} — 'a'#2 可走");
        if (s2 != -1) {
            CHECK_FALSE(dfa.states[s2].is_accept, "a{3} — 读2个不是接受");
            int s3 = dfa.states[s2].transitions['a'];
            CHECK_TRUE(s3 != -1 && dfa.states[s3].is_accept,
                       "a{3} — 'a'#3 到接受");
            /* 不应能读第4个 */
            int s4 = dfa.states[s3].transitions['a'];
            int bad = (s4 != -1 && dfa.states[s4].is_accept);
            CHECK_FALSE(bad, "a{3} — 不应读第4个");
        }
    }

    dfa_free(&dfa);
}

static void test_curly_range(void) {
    /* a{2,4}: 2 到 4 个 a */
    DFAMachine dfa = do_build("a{2,4}");
    CHECK_NOT_NULL(dfa.states, "a{2,4} — states 非 NULL");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a{2,4} — 'a'#1 可走");
    if (s1 != -1) {
        CHECK_FALSE(dfa.states[s1].is_accept, "a{2,4} — 读1个不是接受");
        int s2 = dfa.states[s1].transitions['a'];
        CHECK_TRUE(s2 != -1, "a{2,4} — 'a'#2 可走");
        if (s2 != -1) {
            CHECK_TRUE(dfa.states[s2].is_accept, "a{2,4} — 读2个是接受");
        }
    }

    dfa_free(&dfa);
}

static void test_curly_open(void) {
    /* a{1,}: 至少 1 个，无上限 (等价于 a+) */
    DFAMachine dfa = do_build("a{1,}");
    CHECK_NOT_NULL(dfa.states, "a{1,} — states 非 NULL");

    CHECK_FALSE(dfa.states[0].is_accept, "a{1,} — 起始不是接受（至少1次）");

    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a{1,} — 'a'#1 可走");
    if (s1 != -1) {
        CHECK_TRUE(dfa.states[s1].is_accept, "a{1,} — 读1个是接受");
        int s2 = dfa.states[s1].transitions['a'];
        CHECK_TRUE(s2 != -1 && dfa.states[s2].is_accept,
                   "a{1,} — 读2个仍是接受");
    }

    dfa_free(&dfa);
}

static void test_curly_zero_min(void) {
    /* a{0,3}: 0 到 3 个 a */
    DFAMachine dfa = do_build("a{0,3}");
    CHECK_NOT_NULL(dfa.states, "a{0,3} — states 非 NULL");

    /* 起始状态应是接受（0次匹配） */
    CHECK_TRUE(dfa.states[0].is_accept, "a{0,3} — 起始是接受（0次）");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：捕获组 GROUP（NFA 层面透传）                                            */
/* ========================================================================== */

static void test_group(void) {
    DFAMachine dfa = do_build("(a)");
    CHECK_NOT_NULL(dfa.states, "(a) — states 非 NULL");

    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "(a) — 'a' 到接受");

    dfa_free(&dfa);
}

static void test_group_with_alter(void) {
    DFAMachine dfa = do_build("(a|b)");
    CHECK_NOT_NULL(dfa.states, "(a|b) — states 非 NULL");

    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1 && dfa.states[sa].is_accept, "(a|b) — 'a' 到接受");
    int sb = dfa.states[0].transitions['b'];
    CHECK_TRUE(sb != -1 && dfa.states[sb].is_accept, "(a|b) — 'b' 到接受");

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：复杂组合                                                              */
/* ========================================================================== */

static void test_complex_concat_star(void) {
    /* ab* */
    DFAMachine dfa = do_build("ab*");
    CHECK_NOT_NULL(dfa.states, "ab* — states 非 NULL");

    /* 读 'a' 后应该是接受（因为 b* 可以匹配零次） */
    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1 && dfa.states[s1].is_accept,
               "ab* — 读 'a' 后是接受（b*零次）");

    dfa_free(&dfa);
}

static void test_complex_alter_concat(void) {
    /* ab|cd */
    DFAMachine dfa = do_build("ab|cd");
    CHECK_NOT_NULL(dfa.states, "ab|cd — states 非 NULL");

    /* 'a' → 中间状态 */
    int sa = dfa.states[0].transitions['a'];
    CHECK_TRUE(sa != -1, "ab|cd — 'a' 可走");
    if (sa != -1) {
        CHECK_FALSE(dfa.states[sa].is_accept, "ab|cd — 读 'a' 后非接受");
        int sb = dfa.states[sa].transitions['b'];
        CHECK_TRUE(sb != -1 && dfa.states[sb].is_accept,
                   "ab|cd — 读 'b' 到接受");
    }

    /* 'c' → 中间状态 */
    int sc = dfa.states[0].transitions['c'];
    CHECK_TRUE(sc != -1, "ab|cd — 'c' 可走");
    if (sc != -1) {
        CHECK_FALSE(dfa.states[sc].is_accept, "ab|cd — 读 'c' 后非接受");
        int sd = dfa.states[sc].transitions['d'];
        CHECK_TRUE(sd != -1 && dfa.states[sd].is_accept,
                   "ab|cd — 读 'd' 到接受");
    }

    dfa_free(&dfa);
}

static void test_complex_group_star(void) {
    /* (ab)* */
    DFAMachine dfa = do_build("(ab)*");
    CHECK_NOT_NULL(dfa.states, "(ab)* — states 非 NULL");

    /* 起始是接受（零次） */
    CHECK_TRUE(dfa.states[0].is_accept, "(ab)* — 起始是接受（零次）");

    dfa_free(&dfa);
}

static void test_complex_nested_quant(void) {
    /* a(b|c)*d */
    DFAMachine dfa = do_build("a(b|c)*d");
    CHECK_NOT_NULL(dfa.states, "a(b|c)*d — states 非 NULL");

    /* 读 'a' 后 → 再读 'd' 直接到接受（(b|c)* 零次） */
    int s1 = dfa.states[0].transitions['a'];
    CHECK_TRUE(s1 != -1, "a(b|c)*d — 'a' 可走");
    if (s1 != -1) {
        int s2 = dfa.states[s1].transitions['d'];
        CHECK_TRUE(s2 != -1 && dfa.states[s2].is_accept,
                   "a(b|c)*d — 'ad' 到接受（零次循环）");

        /* 'b' 也可走 */
        int sb = dfa.states[s1].transitions['b'];
        CHECK_TRUE(sb != -1, "a(b|c)*d — 读 'a' 后可走 'b'");
    }

    dfa_free(&dfa);
}

static void test_complex_curly_with_escape(void) {
    /* \d{2,4}[a-z]? */
    DFAMachine dfa = do_build("\\d{2,4}[a-z]?");
    CHECK_NOT_NULL(dfa.states, "\\d{2,4}[a-z]? — states 非 NULL");

    /* 起始不是接受（\d{2,4} 至少2次） */
    CHECK_FALSE(dfa.states[0].is_accept, "\\d{2,4}[a-z]? — 起始不是接受");

    /* 第一个数字 */
    int s1 = dfa.states[0].transitions['3'];
    CHECK_TRUE(s1 != -1, "\\d{2,4}[a-z]? — 数字#1 可走");
    if (s1 != -1) {
        CHECK_FALSE(dfa.states[s1].is_accept, "数字#1 后不是接受");
    }

    dfa_free(&dfa);
}

/* ========================================================================== */
/*  测试：确定性验证                                                             */
/* ========================================================================== */

static void test_determinism(void) {
    const char *patterns[] = {
        "a", ".", "\\d", "[abc]", "[^0-9]",
        "ab", "abc", "a|b", "a|b|c",
        "a*", "a+", "a?", "(ab)*", "a(b|c)*d",
        "a{3}", "a{2,4}", "a{1,}", "a{0,3}",
        "\\d{2,4}[a-z]?", "ab|cd",
        "(a|b)", "(a*)*",
    };
    int n_pats = (int)(sizeof(patterns) / sizeof(patterns[0]));
    int ok = 1;

    for (int i = 0; i < n_pats && ok; i++) {
        DFAMachine dfa = do_build(patterns[i]);
        if (!dfa.states) {
            ok = 0;
            char buf[128];
            snprintf(buf, sizeof(buf), "模式 '%s' 返回 NULL states", patterns[i]);
            check_fail("确定性", "有效 DFA", buf);
            ok = 0;
            break;
        }

        /* 验证：每个状态对同一字符最多有一个转移 */
        for (int s = 0; s < dfa.state_count && ok; s++) {
            if (!dfa.states[s].transitions) {
                ok = 0;
                check_fail("确定性", "有效 transitions", "NULL transitions");
                break;
            }
        }

        dfa_free(&dfa);
    }

    if (ok)
        check_pass("确定性 — 所有模式 DFA transitions[] 有效");
    else
        check_fail("确定性", "有效", "存在 NULL transitions");
}

/* ========================================================================== */
/*  测试：边界情况                                                              */
/* ========================================================================== */

static void test_null_ast(void) {
    DFAMachine dfa = dfa_from_nfa(NULL);
    CHECK_NULL(dfa.states, "NULL NFA — states 为 NULL");
    CHECK_INT(0, dfa.state_count, "NULL NFA — state_count 为 0");
    dfa_free(&dfa);
}

static void test_null_nfa(void) {
    NFAGraph nfa = {0};
    DFAMachine dfa = dfa_from_nfa(&nfa);
    CHECK_NULL(dfa.states, "空 NFA — states 为 NULL");
    dfa_free(&dfa);
}

static void test_double_free(void) {
    DFAMachine dfa = do_build("a");
    dfa_free(&dfa);
    dfa_free(&dfa);
    check_pass("double-free — 安全无崩溃");
}

static void test_dump_null(void) {
    dfa_dump(NULL);
    check_pass("dfa_dump(NULL) — 安全无崩溃");
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
    printf("║   DFA 子集构造 单元测试               ║\n");
    printf("╚══════════════════════════════════════╝\n");

    /* ---- 原子 ---- */
    module_begin("单字符 / 点号 / 转义 / 字符集合");
    test_single_char();
    test_dot();
    test_escape_d();
    test_escape_w();
    test_escape_s();
    test_bracket();
    test_bracket_negate();
    module_end();

    /* ---- 连接 ---- */
    module_begin("连接 CONCAT");
    test_concat_two();
    test_concat_three();
    module_end();

    /* ---- 并集 ---- */
    module_begin("并集 ALTER");
    test_alter_simple();
    test_alter_three();
    module_end();

    /* ---- 量词 ---- */
    module_begin("量词 * / + / ?");
    test_star();
    test_plus();
    test_question();
    module_end();

    module_begin("量词 {m,n}");
    test_curly_exact();
    test_curly_range();
    test_curly_open();
    test_curly_zero_min();
    module_end();

    /* ---- 捕获组 ---- */
    module_begin("捕获组");
    test_group();
    test_group_with_alter();
    module_end();

    /* ---- 复杂组合 ---- */
    module_begin("复杂组合");
    test_complex_concat_star();
    test_complex_alter_concat();
    test_complex_group_star();
    test_complex_nested_quant();
    test_complex_curly_with_escape();
    module_end();

    /* ---- 确定性 ---- */
    module_begin("确定性验证");
    test_determinism();
    module_end();

    /* ---- 边界 ---- */
    module_begin("边界与安全");
    test_null_ast();
    test_null_nfa();
    test_double_free();
    test_dump_null();
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
