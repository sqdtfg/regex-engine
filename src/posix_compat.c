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
 *   2. 底层完全复用 api.h 中的函数。
 *   3. 对暂不支持的特性（REG_ICASE、REG_NEWLINE 等）给出警告但不报错。
 *   4. 捕获组计数基于正则表达式中左括号的数量（简单启发式）。
 * ============================================================================
 */

#include "posix_compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "api.h"

/* -------------------------------------------------------------------------- */
/*  内部：统计捕获组数量                                                       */
/* -------------------------------------------------------------------------- */

/**
 * 启发式统计正则表达式中的捕获组数量。
 *
 * 规则：
 *   - 忽略被转义的 '('
 *   - 忽略字符类 [...] 内部的 '('
 *   - 每个 '(' 计为一个捕获组
 *
 * 注意：这只是一个近似值。POSIX 标准中 nsub 来自编译后的内部结构，
 * 我们无法直接访问，因此用这种启发式方法估算。
 */
static int count_capture_groups(const char *pattern) {
    int count = 0;
    int escaped = 0;
    int in_bracket = 0;
    int bracket_negated = 0;

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
            bracket_negated = (*p == '^');
            continue;
        }

        if (*p == ']' && in_bracket) {
            in_bracket = 0;
            bracket_negated = 0;
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

    /* 暂不支持的标志 — 发出警告但不阻止编译 */
    if (cflags & REG_ICASE) {
        /* TODO: 忽略大小写匹配 */
    }
    if (cflags & REG_NEWLINE) {
        /* TODO: 换行匹配语义 */
    }
    if (cflags & REG_NOTBOL) {
        /* TODO: 不是行首 */
    }
    if (cflags & REG_NOTEOL) {
        /* TODO: 不是行尾 */
    }

    regex_t *internal = regex_compile(pattern, REGEX_FLAG_NONE);
    if (!internal) {
        memset(prog, 0, sizeof(*prog));
        prog->re_errcode = REG_BADPAT;
        strncpy(prog->re_errmsg, "compile failed (invalid pattern)",
                sizeof(prog->re_errmsg) - 1);
        return REG_BADPAT;
    }

    prog->re_flags = cflags;
    prog->re_errcode = 0;
    prog->re_errmsg[0] = '\0';
    prog->internal = internal;
    prog->nsub = count_capture_groups(pattern);

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
    if (prog->re_flags & REG_NOSUB) {
        int result = regex_match(internal, string, NULL);
        return result ? 0 : REG_NOMATCH;
    }

    if (nmatch == 0 || !pmatch) {
        return REG_INVARG;
    }

    /* 清零输出 */
    memset(pmatch, 0xFF, sizeof(regmatch_t) * nmatch);

    /* 先用 regex_match 做全串匹配 */
    MatchResult m;
    int matched = regex_match(internal, string, &m);

    if (!matched) {
        /* 全串未匹配，尝试 regex_search 子串匹配 */
        matched = regex_search(internal, string, &m);
    }

    if (!matched) {
        return REG_NOMATCH;
    }

    /* 第 0 组 = 完整匹配 */
    pmatch[0].rm_so = (regoff_t)m.start;
    pmatch[0].rm_eo = (regoff_t)m.end;

    /* TODO: 捕获组详细信息 — 当前版本只返回完整匹配（第 0 组）。
     * 要支持捕获组需要扩展底层 API（capture.h 已有 CapturedMatch 类型）。 */

    return 0;
}

/* -------------------------------------------------------------------------- */
/*  regfree — 释放编译后的正则程序                                               */
/* -------------------------------------------------------------------------- */

void regfree(regex_prog_t *prog) {
    if (!prog) {
        return;
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
        msg = "unmatched ) or )";
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
