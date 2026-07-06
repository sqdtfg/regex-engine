#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#else
#include <sys/stat.h>
#endif
#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"
#include "api.h"
#include "posix_compat.h"

/**
 * 将正则表达式中的特殊字符转为安全文件名字符。
 * 输出仅含 [a-zA-Z0-9_-]，避免路径注入。
 */
static void make_safe_filename(const char *pattern, char *safe, int maxlen) {
    int j = 0;
    static const char *hex = "0123456789abcdef";
    for (const char *p = pattern; *p && j < maxlen - 4; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_') {
            safe[j++] = *p;
        } else {
            safe[j++] = '_';
            safe[j++] = hex[((unsigned char)*p) >> 4];
            safe[j++] = hex[((unsigned char)*p) & 0xf];
        }
    }
    safe[j] = '\0';
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    /* 默认测试输入 */
    // const char *pattern = (argc >= 2) ? argv[1] : "a|bc*";
    const char *pattern = (argc >= 2) ? argv[1] : "a|bc*|[^abc]|[a-zA-Z0-9]ab.x*y+z?";


    printf("╔══════════════════════════════════════╗\n");
    printf("║     正则表达式引擎演示                ║\n");
    printf("╚══════════════════════════════════════╝\n\n");
    printf("输入: %s\n\n", pattern);

    /* 生成 DOT 目录 */
#ifdef _WIN32
    _mkdir("DOT");
#else
    mkdir("DOT", 0755);
#endif
    char safe[128];
    make_safe_filename(pattern, safe, sizeof(safe));

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

    /* NFA DOT 文件 */
    {
        char dotpath[256];
        snprintf(dotpath, sizeof(dotpath), "DOT/nfa_%s.dot", safe);
        if (nfa_dump_dot_file(&nfa, dotpath) == 0)
            printf("  [DOT] 已生成 %s\n", dotpath);
        else
            printf("  [DOT] 生成 %s 失败\n", dotpath);
    }

    /* ---- DFA ---- */
    DFAMachine dfa = dfa_from_nfa(&nfa);
    if (dfa.states) {
        printf("\n最小化前 DFA 状态数: %d\n", dfa.state_count);
        dfa_dump(&dfa);

        /* 最小化前 DFA DOT 文件 */
        {
            char dotpath[256];
            snprintf(dotpath, sizeof(dotpath), "DOT/dfa_before_%s.dot", safe);
            if (dfa_dump_dot_file(&dfa, dotpath) == 0)
                printf("  [DOT] 已生成 %s\n", dotpath);
            else
                printf("  [DOT] 生成 %s 失败\n", dotpath);
        }

        dfa_minimize(&dfa);
        printf("\n最小化后 DFA 状态数: %d\n", dfa.state_count);
        dfa_dump(&dfa);

        /* 最小化后 DFA DOT 文件 */
        {
            char dotpath[256];
            snprintf(dotpath, sizeof(dotpath), "DOT/dfa_min_%s.dot", safe);
            if (dfa_dump_dot_file(&dfa, dotpath) == 0)
                printf("  [DOT] 已生成 %s\n", dotpath);
            else
                printf("  [DOT] 生成 %s 失败\n", dotpath);
        }

        /* ---- 使用高级 API 进行匹配 ---- */
        printf("\n========== 高级 API 测试 ==========\n");
        {
            regex_t *prog = regex_compile(pattern, REGEX_FLAG_NONE);
            if (prog) {
                const char *test_inputs[] = {"bc", "a", "123", "abc", "abbb", NULL};
                for (int ti = 0; test_inputs[ti]; ti++) {
                    const char *input = test_inputs[ti];
                    MatchResult r;
                    if (regex_match(prog, input, &r)) {
                        printf("  regex_match(\"%s\"): ✓ start=%zu end=%zu len=%zu\n",
                               input, r.start, r.end, r.length);
                    } else if (regex_search(prog, input, &r)) {
                        printf("  regex_search(\"%s\"): ✓ start=%zu end=%zu len=%zu\n",
                               input, r.start, r.end, r.length);
                    } else {
                        printf("  regex_(match|search)(\"%s\"): ✗ 未匹配\n", input);
                    }
                }

                /* 测试 POSIX 兼容层 */
                {
                    regex_prog_t rp;
                    if (regcomp(&rp, pattern, REG_EXTENDED) == REG_OK) {
                        regmatch_t pmatch[1];
                        const char *test_str = "bc";
                        int rc = regexec(&rp, test_str, 1, pmatch, 0);
                        if (rc == 0) {
                            printf("  regexec(\"%s\"): ✓ rm_so=%lld rm_eo=%lld\n",
                                   test_str, pmatch[0].rm_so, pmatch[0].rm_eo);
                        } else if (rc == REG_NOMATCH) {
                            printf("  regexec(\"%s\"): ✗ 未匹配\n", test_str);
                        } else {
                            char errbuf[256];
                            regerror(rc, &rp, errbuf, sizeof(errbuf));
                            printf("  regexec(\"%s\"): ✗ 错误: %s\n", test_str, errbuf);
                        }
                        regfree(&rp);
                    }
                }

                regex_free(prog);
            } else {
                printf("  编译失败: %s\n", regex_error(REGEX_ERR_PARSE));
            }
        }

        dfa_free(&dfa);
    }

    nfa_free(&nfa);
    ast_free(root);
    return 0;
}
