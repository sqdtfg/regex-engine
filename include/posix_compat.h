#ifndef REGEX_POSIX_COMPAT_H
#define REGEX_POSIX_COMPAT_H

#include <stddef.h>

/* ========================================================================== */
/*  POSIX 正则表达式兼容层                                                       */
/*                                                                           */
/*  本文件提供一套与 POSIX 1003.1 <regex.h> 风格一致的 API，                   */
/*  底层复用本项目已有的 regex_compile / regex_match / regex_search /         */
/*  regex_findall 等函数。                                                     */
/*                                                                           */
/*  目标：                                                                     */
/*    - 对上层调用者提供熟悉的 regcomp/regexec/regfree/regerror 接口          */
/*    - 支持基本 POSIX 标志（REG_EXTENDED、REG_ICASE、REG_NOSUB、             */
/*      REG_NEWLINE、REG_NOTBOL、REG_NOTEOL）                                */
/*    - 捕获组信息通过 regmatch_t 数组传出                                     */
/* ========================================================================== */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  类型定义                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * 匹配范围。对标 POSIX regmatch_t：
 *   rm_so = start offset  (-1 = 未捕获)
 *   rm_eo = end offset    (-1 = 未捕获)
 */
typedef struct {
    regoff_t rm_so;   /* start offset */
    regoff_t rm_eo;   /* end offset */
} regmatch_t;

/**
 * 编译后的正则程序。内部封装 regex_t* 及标志。
 */
typedef struct {
    int          re_flags;    /* 编译时传入的标志 */
    char         re_errmsg[256]; /* 最近一次错误信息 */
    int          re_errcode;  /* 错误码 (0=OK) */
    void        *internal;    /* 内部 regex_t* (opaque) */
    int          nsub;        /* 捕获组数量（不含第 0 组） */
} regex_prog_t;

/* -------------------------------------------------------------------------- */
/*  错误码                                                                      */
/* -------------------------------------------------------------------------- */

/**
 * 模拟 POSIX 错误码命名空间。
 * 注意：本项目不使用系统 <regex.h>，因此不会与 REG_* 冲突。
 */
enum {
    REG_OK = 0,
    REG_BADPAT,       /* 模式无效（解析失败） */
    REG_ECOLLATE,     /* 不支持：字符类/排序 */
    REG_ECTYPE,       /* 不支持：未知字符类 */
    REG_EESCAPE,      /* 不支持：尾部转义 */
    REG_EPAREN,       /* 括号不匹配 */
    REG_EBRACK,       /* 方括号不匹配 */
    REG_ERANGE,       /* 范围无效 */
    REG_ESIZE,        /* 超出最大匹配数 */
    REG_ESPACE,       /* 内存不足 */
    REG_BADBR,        /* 不支持：{} 中无效重复数 */
    REG_INVARG,       /* 不支持：无效参数 */
    REG_NOMATCH       /* regexec 返回：未匹配 */
};

/* -------------------------------------------------------------------------- */
/*  标志位 (模拟 POSIX)                                                         */
/* -------------------------------------------------------------------------- */

enum {
    REG_EXTENDED  = 0x01,  /* 扩展正则（本项目默认支持） */
    REG_ICASE     = 0x02,  /* 忽略大小写（暂不实现） */
    REG_NOSUB     = 0x04,  /* 不返回匹配位置信息 */
    REG_NEWLINE   = 0x08,  /* 换行匹配（暂不实现） */
    REG_NOTBOL    = 0x10,  /* 输入不是行首（暂不实现） */
    REG_NOTEOL    = 0x20   /* 输入不是行尾（暂不实现） */
};

/* -------------------------------------------------------------------------- */
/*  公共 API                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * 编译正则表达式。
 *
 * @param prog    输出的程序对象（调用者提供，无需初始化）
 * @param pattern 正则表达式字符串
 * @param cflags  编译标志（REG_EXTENDED / REG_ICASE / REG_NOSUB / REG_NEWLINE）
 * @return        0 = 成功, 非 0 = 错误码（存于 prog->re_errcode）
 */
int regcomp(regex_prog_t *prog, const char *pattern, int cflags);

/**
 * 执行匹配。
 *
 * @param prog      已编译的正则程序
 * @param string    待匹配字符串
 * @param nmatch    regmatch_t 数组大小
 * @param pmatch    匹配结果数组（至少 nmatch 个 regmatch_t）。
 *                  若 cflags 含 REG_NOSUB，则忽略。
 * @param eflags    执行标志（REG_NOTBOL / REG_NOTEOL / REG_NEWLINE，暂不实现）
 * @return          0 = 匹配成功, REG_NOMATCH = 未匹配, 其他 = 错误码
 */
int regexec(const regex_prog_t *prog, const char *string,
            size_t nmatch, regmatch_t pmatch[], int eflags);

/**
 * 释放编译后的正则程序。
 */
void regfree(regex_prog_t *prog);

/**
 * 获取错误信息。
 *
 * @param preg    已编译的程序（用于读取错误上下文）
 * @param errcode 错误码
 * @param errbuf  输出缓冲
 * @param ebuffersize 缓冲大小
 * @return        errbuf 指针（方便链式调用）
 */
const char *regerror(int errcode, const regex_prog_t *preg,
                     char *errbuf, size_t ebuffersize);

#ifdef __cplusplus
}
#endif

#endif /* REGEX_POSIX_COMPAT_H */
