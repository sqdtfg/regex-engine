#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <locale.h>
#endif
#include <stdlib.h>
#include "parser.h"

/* ========================================================================== */
/*  测试框架                                                                    */
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

/* ========================================================================== */
/*  AST 验证辅助函数                                                            */
/* ========================================================================== */

/** 检查节点类型 */
static int check_node_type(const ASTNode *node, ASTNodeType expected,
                            const char *desc) {
    if (!node) {
        check_fail(desc, ast_type_name(expected), "NULL");
        return 0;
    }
    if (node->type != expected) {
        check_fail(desc, ast_type_name(expected), ast_type_name(node->type));
        return 0;
    }
    check_pass(desc);
    return 1;
}

/** 解析并返回根节点（失败则返回 NULL） */
static ASTNode *do_parse(const char *input) {
    Parser parser;
    parser_init(&parser, input);
    return parser_parse(&parser);
}

/** 解析失败应该返回 NULL，且设置错误码 */
static void check_parse_fails(const char *input, const char *desc) {
    Parser parser;
    parser_init(&parser, input);
    ASTNode *root = parser_parse(&parser);
    if (root != NULL) {
        check_fail(desc, "解析失败（NULL）", "解析成功（非 NULL）");
        ast_free(root);
    } else {
        check_pass(desc);
    }
}

/* ========================================================================== */
/*  测试用例                                                                    */
/* ========================================================================== */

/* ---- 简单原子 ---- */

static void test_parse_single_char(void) {
    ASTNode *root = do_parse("a");
    if (check_node_type(root, AST_CHAR, "单个字符 — 类型为普通字符")) {
        if (root->ch == 'a') check_pass("单个字符 — 值为 'a'");
        else {
            char act[8]; snprintf(act, sizeof(act), "'%c'", root->ch);
            check_fail("单个字符 — 值为 'a'", "'a'", act);
        }
    }
    ast_free(root);
}

static void test_parse_dot(void) {
    ASTNode *root = do_parse(".");
    check_node_type(root, AST_DOT, "点号 — 类型为任意字符");
    ast_free(root);
}

static void test_parse_escape(void) {
    ASTNode *root = do_parse("\\d");
    check_node_type(root, AST_ESCAPE, "\\d — 类型为转义序列");
    ast_free(root);

    root = do_parse("\\w");
    check_node_type(root, AST_ESCAPE, "\\w — 类型为转义序列");
    ast_free(root);

    root = do_parse("\\.");
    if (check_node_type(root, AST_CHAR, "\\. — 转义元字符为普通字符")) {
        if (root->ch == '.') check_pass("\\. — 值为 '.'");
    }
    ast_free(root);
}

static void test_parse_bracket(void) {
    ASTNode *root = do_parse("[abc]");
    if (check_node_type(root, AST_BRACKET, "[abc] — 类型为字符集合")) {
        if (root->bracket.len == 3 && strncmp(root->bracket.str, "abc", 3) == 0)
            check_pass("[abc] — 内容为 \"abc\"");
        else
            check_fail("[abc] — 内容为 \"abc\"", "\"abc\"", root->bracket.str);
    }
    ast_free(root);
}

/* ---- 量词 ---- */

static void test_parse_star(void) {
    ASTNode *root = do_parse("a*");
    if (check_node_type(root, AST_STAR, "a* — 根为星号量词")) {
        check_node_type(root->left, AST_CHAR, "a* — 子节点为普通字符");
    }
    ast_free(root);
}

static void test_parse_plus(void) {
    ASTNode *root = do_parse("a+");
    if (check_node_type(root, AST_PLUS, "a+ — 根为加号量词")) {
        check_node_type(root->left, AST_CHAR, "a+ — 子节点为普通字符");
    }
    ast_free(root);
}

static void test_parse_question(void) {
    ASTNode *root = do_parse("a?");
    if (check_node_type(root, AST_QUESTION, "a? — 根为问号量词")) {
        check_node_type(root->left, AST_CHAR, "a? — 子节点为普通字符");
    }
    ast_free(root);
}

static void test_parse_curly_exact(void) {
    ASTNode *root = do_parse("a{3}");
    if (check_node_type(root, AST_CURLY, "a{3} — 根为范围量词")) {
        check_node_type(root->left, AST_CHAR, "a{3} — 子节点为普通字符");
        if (root->quant_min == 3 && root->quant_max == 3)
            check_pass("a{3} — min=3, max=3");
        else {
            char buf[64]; snprintf(buf, sizeof(buf), "min=%d,max=%d", root->quant_min, root->quant_max);
            check_fail("a{3} — min=3, max=3", "min=3,max=3", buf);
        }
    }
    ast_free(root);
}

static void test_parse_curly_range(void) {
    ASTNode *root = do_parse("a{2,5}");
    if (check_node_type(root, AST_CURLY, "a{2,5} — 根为范围量词")) {
        if (root->quant_min == 2 && root->quant_max == 5)
            check_pass("a{2,5} — min=2, max=5");
    }
    ast_free(root);
}

/* ---- 连接 ---- */

static void test_parse_concat_two(void) {
    ASTNode *root = do_parse("ab");
    if (check_node_type(root, AST_CONCAT, "ab — 根为连接")) {
        check_node_type(root->left,  AST_CHAR, "ab — 左子为普通字符");
        check_node_type(root->right, AST_CHAR, "ab — 右子为普通字符");
        if (root->left->ch  == 'a') check_pass("ab — 左子值为 'a'");
        if (root->right->ch == 'b') check_pass("ab — 右子值为 'b'");
    }
    ast_free(root);
}

static void test_parse_concat_many(void) {
    /* abc → CONCAT(CONCAT(a,b),c) */
    ASTNode *root = do_parse("abc");
    if (check_node_type(root, AST_CONCAT, "abc — 根为连接")) {
        ASTNode *l = root->left;
        ASTNode *r = root->right;
        /* l 应该是 CONCAT(a,b), r 是 c */
        if (l->type == AST_CONCAT) {
            check_pass("abc — 左子为连接（左结合）");
            if (r->ch == 'c') check_pass("abc — 右子为 'c'");
        } else {
            check_fail("abc — 左结合结构", "CONCAT", ast_type_name(l->type));
        }
    }
    ast_free(root);
}

/* ---- 并集 ---- */

static void test_parse_alter_simple(void) {
    ASTNode *root = do_parse("a|b");
    if (check_node_type(root, AST_ALTER, "a|b — 根为并集")) {
        check_node_type(root->left,  AST_CHAR, "a|b — 左子为 'a'");
        check_node_type(root->right, AST_CHAR, "a|b — 右子为 'b'");
    }
    ast_free(root);
}

static void test_parse_alter_left_assoc(void) {
    /* a|b|c → ALTER(ALTER(a,b), c) */
    ASTNode *root = do_parse("a|b|c");
    if (check_node_type(root, AST_ALTER, "a|b|c — 根为并集")) {
        ASTNode *l = root->left;
        ASTNode *r = root->right;
        if (l->type == AST_ALTER)
            check_pass("a|b|c — 左结合，左子为并集");
        else
            check_fail("a|b|c — 左结合", "并集", ast_type_name(l->type));
        if (r->type == AST_CHAR && r->ch == 'c')
            check_pass("a|b|c — 右子为 'c'");
    }
    ast_free(root);
}

/* ---- 括号 / 捕获组 ---- */

static void test_parse_group(void) {
    ASTNode *root = do_parse("(a)");
    if (check_node_type(root, AST_GROUP, "(a) — 根为捕获组")) {
        check_node_type(root->left, AST_CHAR, "(a) — 内部为普通字符");
    }
    ast_free(root);
}

static void test_parse_group_alter(void) {
    ASTNode *root = do_parse("(a|b)");
    if (check_node_type(root, AST_GROUP, "(a|b) — 根为捕获组")) {
        check_node_type(root->left, AST_ALTER, "(a|b) — 内部为并集");
    }
    ast_free(root);
}

static void test_parse_group_nested(void) {
    ASTNode *root = do_parse("((a))");
    if (check_node_type(root, AST_GROUP, "((a)) — 根为捕获组")) {
        ASTNode *inner = root->left;
        if (inner->type == AST_GROUP) {
            check_pass("((a)) — 内部为捕获组");
            check_node_type(inner->left, AST_CHAR, "((a)) — 最内层为普通字符");
        } else {
            check_fail("((a)) — 内部为捕获组", "捕获组", ast_type_name(inner->type));
        }
    }
    ast_free(root);
}

/* ---- 优先级验证 ---- */

static void test_precedence_quant_over_concat(void) {
    /* ab* → CONCAT(a, STAR(b))  不是 STAR(CONCAT(a,b)) */
    ASTNode *root = do_parse("ab*");
    if (check_node_type(root, AST_CONCAT, "ab* — 根为连接（量词优先于连接）")) {
        ASTNode *r = root->right;
        if (r->type == AST_STAR)
            check_pass("ab* — 右子为星号（*只绑定b）");
        else
            check_fail("ab* — 右子为星号", "星号量词", ast_type_name(r->type));
    }
    ast_free(root);
}

static void test_precedence_concat_over_alter(void) {
    /* ab|cde → ALTER(CONCAT(a,b), CONCAT(c,CONCAT(d,e))) */
    ASTNode *root = do_parse("ab|cde");
    if (check_node_type(root, AST_ALTER, "ab|cde — 根为并集（连接优先于并集）")) {
        if (root->left->type  == AST_CONCAT) check_pass("ab|cde — 左子为连接");
        if (root->right->type == AST_CONCAT) check_pass("ab|cde — 右子为连接");
    }
    ast_free(root);
}

static void test_precedence_group_overrides(void) {
    /* (ab)* → STAR(GROUP(CONCAT(a,b))) */
    ASTNode *root = do_parse("(ab)*");
    if (check_node_type(root, AST_STAR, "(ab)* — 根为星号（括号提升优先级）")) {
        ASTNode *grp = root->left;
        if (check_node_type(grp, AST_GROUP, "(ab)* — 子节点为捕获组")) {
            check_node_type(grp->left, AST_CONCAT, "(ab)* — 内部为连接");
        }
    }
    ast_free(root);
}

/* ---- 复杂组合 ---- */

static void test_complex_1(void) {
    /* a(b|c)*d — 经典例子 */
    ASTNode *root = do_parse("a(b|c)*d");
    if (!root) { check_fail("a(b|c)*d", "解析成功", "NULL"); return; }
    if (check_node_type(root, AST_CONCAT, "a(b|c)*d — 根为连接")) {
        /* 左结合: CONCAT(CONCAT(a, STAR(GROUP(ALTER(b,c)))), d) */
        ASTNode *l = root->left;   /* CONCAT(a, STAR(...)) */
        ASTNode *r = root->right;  /* d */
        if (r->type == AST_CHAR && r->ch == 'd')
            check_pass("a(b|c)*d — 最后是 'd'");
        if (l->type == AST_CONCAT) {
            check_pass("a(b|c)*d — 左部为连接");
            ASTNode *ll = l->left;   /* a */
            ASTNode *lr = l->right;  /* STAR(GROUP(ALTER(b,c))) */
            if (ll->ch == 'a') check_pass("a(b|c)*d — 首个为 'a'");
            if (lr->type == AST_STAR) {
                check_pass("a(b|c)*d — 中间为星号");
                ASTNode *grp = lr->left;
                if (grp->type == AST_GROUP) {
                    check_pass("a(b|c)*d — 星号子为捕获组");
                    check_node_type(grp->left, AST_ALTER, "a(b|c)*d — 捕获组内部为并集");
                }
            }
        }
    }
    ast_free(root);
}

static void test_complex_2(void) {
    /* \d{2,4}[a-z]? */
    ASTNode *root = do_parse("\\d{2,4}[a-z]?");
    if (!root) { check_fail("\\d{2,4}[a-z]?", "解析成功", "NULL"); return; }
    check_node_type(root, AST_CONCAT, "\\d{2,4}[a-z]? — 根为连接");
    ast_free(root);
}

/* ---- 错误情况 ---- */

static void test_error_empty(void) {
    check_parse_fails("", "空输入 → 解析失败");
}

static void test_error_unclosed_paren(void) {
    check_parse_fails("(a", "未闭合括号 → 解析失败");
}

static void test_error_extra_paren(void) {
    check_parse_fails("a)", "多余右括号 → 解析失败");
}

static void test_error_pipe_at_start(void) {
    /* | 开头：左边链为空，应该报错 */
    Parser parser;
    parser_init(&parser, "|a");
    ASTNode *root = parser_parse(&parser);
    /* 依实现：parse_regex 调用 parse_chain，chain 至少一个 factor，
       | 不是 factor 开头，所以 chain 返回 NULL，regex 返回 NULL */
    if (root == NULL) check_pass("| 开头 → 解析失败");
    else { check_fail("| 开头 → 解析失败", "NULL", "非 NULL"); ast_free(root); }
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
    printf("║     语法解析器 单元测试              ║\n");
    printf("╚══════════════════════════════════════╝\n");

    /* ---- 原子解析 ---- */
    module_begin("原子解析");
    test_parse_single_char();
    test_parse_dot();
    test_parse_escape();
    test_parse_bracket();
    module_end();

    /* ---- 量词 ---- */
    module_begin("量词");
    test_parse_star();
    test_parse_plus();
    test_parse_question();
    test_parse_curly_exact();
    test_parse_curly_range();
    module_end();

    /* ---- 连接 ---- */
    module_begin("连接（并置）");
    test_parse_concat_two();
    test_parse_concat_many();
    module_end();

    /* ---- 并集 ---- */
    module_begin("并集 (|)");
    test_parse_alter_simple();
    test_parse_alter_left_assoc();
    module_end();

    /* ---- 捕获组 ---- */
    module_begin("捕获组（括号）");
    test_parse_group();
    test_parse_group_alter();
    test_parse_group_nested();
    module_end();

    /* ---- 优先级 ---- */
    module_begin("优先级验证");
    test_precedence_quant_over_concat();
    test_precedence_concat_over_alter();
    test_precedence_group_overrides();
    module_end();

    /* ---- 复杂组合 ---- */
    module_begin("复杂组合");
    test_complex_1();
    test_complex_2();
    module_end();

    /* ---- 错误情况 ---- */
    module_begin("错误处理");
    test_error_empty();
    test_error_unclosed_paren();
    test_error_extra_paren();
    test_error_pipe_at_start();
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
