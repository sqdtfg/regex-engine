#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "parser.h"
#include "nfa.h"
#include "dfa.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    /* 默认测试输入 */
    const char *pattern = (argc >= 2) ? argv[1] : "a|bc*";

    printf("╔══════════════════════════════════════╗\n");
    printf("║     正则表达式引擎演示                ║\n");
    printf("╚══════════════════════════════════════╝\n\n");
    printf("输入: %s\n\n", pattern);

    /* ---- AST ---- */
    Parser parser;
    parser_init(&parser, pattern);

    ASTNode *root = parser_parse(&parser);
    if (!root) {
        printf("解析失败: %s\n", parser.error_msg);
        return 1;
    }

    printf("语法树:\n");
    ast_print(root);
    printf("\n");

    /* ---- NFA ---- */
    NFAGraph nfa = nfa_from_ast(root);
    if (!nfa.start) {
        printf("NFA 生成失败\n");
        ast_free(root);
        return 1;
    }
    nfa_dump(&nfa);

    // /* ---- DFA ---- */
    // DFAMachine dfa = dfa_from_nfa(&nfa);
    // if (dfa.states) {
    //     dfa_dump(&dfa);

    //     /* 尝试匹配输入字符串 */
    //     const char *input = (argc >= 3) ? argv[2] : "bc";
    //     MatchResult r = dfa_match(&dfa, input);
    //     printf("\n匹配测试: pattern=\"%s\" input=\"%s\"\n", pattern, input);
    //     if (r.matched) {
    //         printf("  ✓ 匹配成功! start=%zu end=%zu length=%zu\n",
    //                r.start, r.end, r.length);
    //         printf("  匹配内容: \"%.*s\"\n", (int)r.length, input + r.start);
    //     } else {
    //         printf("  ✗ 未匹配\n");
    //     }

    //     dfa_free(&dfa);
    // }

    nfa_free(&nfa);
    ast_free(root);
    return 0;
}
