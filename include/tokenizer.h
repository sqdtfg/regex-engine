#ifndef REGEX_TOKENIZER_H
#define REGEX_TOKENIZER_H

#include <stddef.h>

/* ========================================================================== */
/*  Token 类型定义                                                             */
/* ========================================================================== */

/** 转义序列对应的特殊字符 */
typedef enum {
    ESCAPE_DIGIT = 0,   /* \d  数字 */
    ESCAPE_NON_DIGIT,   /* \D  非数字 */
    ESCAPE_WORD,        /* \w  单词字符 */
    ESCAPE_NON_WORD,    /* \W  非单词字符 */
    ESCAPE_SPACE,       /* \s  空白字符 */
    ESCAPE_NON_SPACE,   /* \S  非空白字符 */
} EscapeSeq;

/** Token 种类 */
typedef enum {
    TOK_CHAR,           /* 普通字符 */
    TOK_ESCAPE,         /* 转义序列 */
    TOK_DOT,            /* '.' */
    TOK_BRACKET,        /* '[...]' */
    TOK_STAR,           /* '*' */
    TOK_PLUS,           /* '+' */
    TOK_QUESTION,       /* '?' */
    TOK_CURLY,          /* '{m,n}' */
    TOK_LPAREN,         /* '(' */
    TOK_RPAREN,         /* ')' */
    TOK_PIPE,           /* '|' */
    TOK_CARET,          /* '^' */
    TOK_DOLLAR,         /* '$' */
    TOK_EOF,            /* 输入结束 */
    TOK_ERROR,          /* 词法错误 */
} RegexTokenType;

/** Token 结构体 */
typedef struct {
    RegexTokenType type;
    union {
        char ch;
        EscapeSeq esc;
        struct {
            int min;
            int max;
        } curly;
        struct {
            char *str;
            size_t len;
        } bracket;
    };
    size_t pos;
} Token;

/* ========================================================================== */
/*  Tokenizer 状态                                                            */
/* ========================================================================== */

typedef struct {
    const char *input;
    size_t length;
    size_t pos;
    int error_code;
    char error_msg[128];
} Tokenizer;

/* ========================================================================== */
/*  公共 API                                                                  */
/* ========================================================================== */

/** 初始化 Tokenizer */
void tokenizer_init(Tokenizer *tok, const char *input);

/**
 * 获取下一个 Token
 * @return 下一个 Token
 */
Token tokenizer_next(Tokenizer *tok);

/** 将 Token 转换为可读字符串（调试用） */
const char *token_to_string(Token tok);

#endif /* REGEX_TOKENIZER_H */