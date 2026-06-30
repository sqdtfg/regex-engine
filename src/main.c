#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "parser.h"

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    /* 默认测试输入 */
    const char *pattern = (argc >= 2) ? argv[1] : "a|bc*";

    printf("╔══════════════════════════════════════╗\n");
    printf("║     正则表达式解析示例               ║\n");
    printf("╚══════════════════════════════════════╝\n\n");
    printf("输入: %s\n\n", pattern);

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

    ast_free(root);
    return 0;
}
