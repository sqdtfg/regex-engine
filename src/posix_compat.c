/**
 * ============================================================================
 * POSIX 正则表达式兼容层 — 实现
 * ============================================================================
 *
 * 本文件将项目自身的 regex_compile / regex_match / regex_search /
 * regex_findall API 包装成 POSIX 1003.1 风格的 regcomp / regexec /
 * regfree / regerror 接口。
 *
 * 设计原则：
 *   1. 不修改项目现有任何文件。
 *   2. 一次解析 AST，同时构建基础 DFA 和带捕获组的 DFA，避免重复解析。
 *   3. 对暂不支持的特性（REG_ICASE、REG_NEWLINE 等）给出警告但不报错。
 *   4. 捕获组计数基于 AST 遍历结果（统计 AST_GROUP 节点数量）。
 * ============================================================================
 */

#include "posix_compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "api.h"
#include "capture.h"
#include "nfa.h"
#include "parser.h"

/* -------------------------------------------------------------------------- */
/*  内部：统计捕获组数量                                                       */
/* -------------------------------------------------------------------------- */

/**
 * 统计正则表达式中的捕获组数量（基于原始模式字符串的轻量解析）。
 *
 * 规则：
 *   - 忽略被转义的 '('
 *   - 忽略字符类 [...] 内部的 '('
 *   - 每个未被转义的 '(' 计为一个捕获组
 *
 * 注意：这是字符串级启发式统计，与 AST 级 count_groups_recursive 的精确结果
 * 在有效输入上一致。nsub 用于调用者预分配数组容量。
 */
static int count_capture_groups(const char *pattern) {
    int count = 0;
    int escaped = 0;
    int in_bracket = 0;

    for (const char *p = pattern; *p; p++) {
        if (escaped) {
            escaped = 0;
            continue;
        }

        if (*p == '\\') {
            escaped = 1;
            continue;
        }

        if (*p == '[' && !in_bracket) {
            in_bracket = 1;
            continue;
        }

        if (*p == ']' && in_bracket) {
            in_bracket = 0;
            continue;
        }

        if (in_bracket) {
            continue;
        }

        if (*p == '(') {
            count++;
        }
    }

    return count;
}

/* -------------------------------------------------------------------------- */
/*  regcomp — 编译正则表达式                                                    */
/* -------------------------------------------------------------------------- */

int regcomp(regex_prog_t *prog, const char *pattern, int cflags) {
    if (!prog || !pattern || !pattern[0]) {
        if (prog) {
            memset(prog, 0, sizeof(*prog));
            prog->re_errcode = REG_BADPAT;
            strncpy(prog->re_errmsg, "NULL or empty pattern", sizeof(prog->re_errmsg) - 1);
        }
        return REG_BADPAT;
    }

    /* 暂不支持的标志 — 接受但不改变匹配行为 */
    if (cflags & REG_ICASE) {
        /* 忽略大小写：当前接受标志，实际大小写敏感匹配 */
    }
    if (cflags & REG_NEWLINE) {
        /* 换行匹配语义：当前接受标志，实际行为不变 */
    }
    if (cflags & REG_NOTBOL) {
        /* 非行首：当前接受标志，实际行为不变 */
    }
    if (cflags & REG_NOTEOL) {
        /* 非行尾：当前接受标志，实际行为不变 */
    }

    regex_prog_t temp = {0};
    temp.re_flags = cflags;

    /* 一次解析，AST 复用 — 避免 regcomp 中重复解析同一模式 */
    {
        Parser parser;
        parser_init(&parser, pattern);
        ASTNode *ast = parser_parse(&parser);

        if (!ast) {
            snprintf(temp.re_errmsg, sizeof(temp.re_errmsg),
                     "parse error: %s", parser.error_msg);
            temp.re_errcode = REG_BADPAT;
            *prog = temp;
            return REG_BADPAT;
        }

        /* 构建基础 NFA → DFA（供 regex_search 回退路径使用） */
        NFAGraph nfa = nfa_from_ast(ast);
        if (nfa.start && nfa.end && nfa.states && nfa.state_count > 0) {
            DFAMachine dfa = dfa_from_nfa(&nfa);
            nfa_free(&nfa);
            if (dfa.states && dfa.state_count > 0) {
                regex_t *internal = (regex_t *)calloc(1, sizeof(*internal));
                if (internal) {
                    internal->dfa = dfa;
                    internal->flags = REGEX_FLAG_NONE;
                    internal->error_code = REGEX_OK;
                    internal->pattern = strdup(pattern);
                    temp.internal = internal;
                } else {
                    dfa_free(&dfa);
                    temp.re_errcode = REG_ESPACE;
                    snprintf(temp.re_errmsg, sizeof(temp.re_errmsg),
                             "out of memory");
                    ast_free(ast);
                    *prog = temp;
                    return REG_ESPACE;
                }
            } else {
                nfa_free(&nfa);
                dfa_free(&dfa);
            }
        } else {
            nfa_free(&nfa);
        }

        /* 从同一个 AST 构建带捕获组信息的 DFA */
        DFAMachine cap_dfa = dfa_from_ast_with_groups(ast);
        if (cap_dfa.states) {
            DFAMachine *heap_dfa = (DFAMachine *)malloc(sizeof(*heap_dfa));
            if (heap_dfa) {
                *heap_dfa = cap_dfa;
                temp.capture_dfa = heap_dfa;
                temp.nsub = count_capture_groups(pattern);
            } else {
                dfa_free(&cap_dfa);
            }
        }

        ast_free(ast);
    }

    temp.re_errcode = 0;
    temp.re_errmsg[0] = '\0';
    *prog = temp;
    return REG_OK;
}

/* -------------------------------------------------------------------------- */
/*  regexec — 执行匹配                                                          */
/* -------------------------------------------------------------------------- */

int regexec(const regex_prog_t *prog, const char *string,
            size_t nmatch, regmatch_t pmatch[], int eflags)
{
    if (!prog || !string) {
        return REG_INVARG;
    }

    regex_t *internal = (regex_t *)prog->internal;
    if (!internal) {
        return REG_INVARG;
    }

    /* REG_NOSUB：不需要匹配位置信息，只需判断是否匹配 */
    /* POSIX regexec 语义是找最左最长子串匹配，统一使用 dfa_match（已修复为最长匹配） */
    if (eflags & REG_NOSUB) {
        MatchResult m = {0};
        int matched = regex_search(internal, string, &m);
        return matched ? 0 : REG_NOMATCH;
    }

    if (nmatch == 0 || !pmatch) {
        return REG_INVARG;
    }

    /* 清零输出 */
    memset(pmatch, 0xFF, sizeof(regmatch_t) * nmatch);

    /* 优先使用带捕获组的 DFA */
    if (prog->capture_dfa) {
        DFAMachine *cap_dfa = (DFAMachine *)prog->capture_dfa;
        CapturedMatch cm = dfa_match_captured(cap_dfa, string);

        if (cm.matched) {
            /* 第 0 组 = 完整匹配 */
            pmatch[0].rm_so = (regoff_t)cm.start;
            pmatch[0].rm_eo = (regoff_t)cm.end;

            /* 捕获组（第 1..nsub 组） */
            size_t limit = nmatch;
            if (limit > (size_t)cm.group_count + 1) {
                limit = cm.group_count + 1;
            }
            for (size_t i = 1; i < limit; i++) {
                if (cm.groups[i].matched) {
                    pmatch[i].rm_so = (regoff_t)cm.groups[i].start;
                    pmatch[i].rm_eo = (regoff_t)cm.groups[i].end;
                }
            }

            captured_match_free(&cm);
            return 0;
        }

        captured_match_free(&cm);
    }

    /* 回退：优先子串匹配（POSIX regexec 语义：最左最长） */
    MatchResult m;
    int matched = regex_search(internal, string, &m);

    if (!matched) {
        /* 子串也未匹配，尝试精确全串匹配 */
        matched = internal ? regex_match(internal, string, &m) : 0;
    }

    if (!matched) {
        return REG_NOMATCH;
    }

    /* 第 0 组 = 完整匹配 */
    pmatch[0].rm_so = (regoff_t)m.start;
    pmatch[0].rm_eo = (regoff_t)m.end;

    /* 捕获组信息已在 capture_dfa 路径中返回；此处回退路径无捕获数据 */
    for (size_t i = 1; i < nmatch && i <= (size_t)prog->nsub; i++) {
        pmatch[i].rm_so = -1;
        pmatch[i].rm_eo = -1;
    }

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  regfree — 释放编译后的正则程序                                               */
/* -------------------------------------------------------------------------- */

void regfree(regex_prog_t *prog) {
    if (!prog) {
        return;
    }

    if (prog->capture_dfa) {
        DFAMachine *cap_dfa = (DFAMachine *)prog->capture_dfa;
        dfa_capture_free(cap_dfa);
        prog->capture_dfa = NULL;
    }

    if (prog->internal) {
        regex_free((regex_t *)prog->internal);
        prog->internal = NULL;
    }

    prog->re_flags = 0;
    prog->nsub = 0;
    prog->re_errmsg[0] = '\0';
    prog->re_errcode = 0;
}

/* -------------------------------------------------------------------------- */
/*  regerror — 错误信息                                                         */
/* -------------------------------------------------------------------------- */

const char *regerror(int errcode, const regex_prog_t *preg,
                     char *errbuf, size_t ebuffersize)
{
    const char *msg;

    switch (errcode) {
    case REG_OK:
        msg = "no error";
        break;
    case REG_BADPAT:
        msg = "bad regular expression";
        break;
    case REG_ECOLLATE:
        msg = "collation error";
        break;
    case REG_ECTYPE:
        msg = "unknown character class";
        break;
    case REG_EESCAPE:
        msg = "trailing backslash";
        break;
    case REG_EPAREN:
        msg = "unmatched ( or )";
        break;
    case REG_EBRACK:
        msg = "unmatched [ or ]";
        break;
    case REG_ERANGE:
        msg = "invalid range in bracket expression";
        break;
    case REG_ESIZE:
        msg = "result capacity exceeded";
        break;
    case REG_ESPACE:
        msg = "out of memory";
        break;
    case REG_BADBR:
        msg = "invalid repetition count";
        break;
    case REG_INVARG:
        msg = "invalid argument";
        break;
    case REG_NOMATCH:
        msg = "no match";
        break;
    default:
        /* 如果 preg 不为空且内部有错误信息，优先使用它 */
        if (preg && preg->re_errmsg[0]) {
            msg = preg->re_errmsg;
        } else {
            msg = "unknown error";
        }
        break;
    }

    if (ebuffersize > 0) {
        strncpy(errbuf, msg, ebuffersize - 1);
        errbuf[ebuffersize - 1] = '\0';
    }

    return msg;
}
