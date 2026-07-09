#include "tokenizer.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

/* ========================================================================== */
/*  内部辅助函数                                                                */
/* ========================================================================== */

/** 跳过一个或多个空白字符 */
static void skip_whitespace(Tokenizer *tok) {
    while (tok->pos < tok->length && isspace((unsigned char)tok->input[tok->pos])) {
        tok->pos++;
    }
}

/** 查看当前位置字符，不前进 */
static char peek(const Tokenizer *tok) {
    return (tok->pos < tok->length) ? tok->input[tok->pos] : '\0';
}

/** 消费当前位置字符，前进一位 */
static char consume(Tokenizer *tok) {
    return (tok->pos < tok->length) ? tok->input[tok->pos++] : '\0';
}

/** 报告词法错误 */
static Token make_error(Tokenizer *tok, const char *msg) {
    Token t = { .type = TOK_ERROR, .pos = tok->pos };
    tok->error_code = 1;
    strncpy(tok->error_msg, msg, sizeof(tok->error_msg) - 1);
    tok->error_msg[sizeof(tok->error_msg) - 1] = '\0';
    return t;
}

/* ========================================================================== */
/*  转义序列解析                                                                */
/* ========================================================================== */

/**
 * 解析 \d \w \s 等转义序列。
 * 调用前提：当前位置已在 '\' 之后的第一个字符上。
 *
 * 处理规则：
 *   - 已知字符类 (\d \w \s \D \W \S) → TOK_ESCAPE + 对应 EscapeSeq
 *   - 已知元字符 (\. \* \? \+ \( \) \[ \] \| \^ \$) → TOK_CHAR（字面意义）
 *   - 其他未知转义 → 当作普通字符，返回 TOK_CHAR
 */
static Token parse_escape(Tokenizer *tok) {
    Token t = { .type = TOK_CHAR, .pos = tok->pos };

    char c = consume(tok);  // 消费 '\' 后的第一个字符

    switch (c) {
        case 'd': t.type = TOK_ESCAPE; t.esc = ESCAPE_DIGIT;          break;
        case 'D': t.type = TOK_ESCAPE; t.esc = ESCAPE_NON_DIGIT;      break;
        case 'w': t.type = TOK_ESCAPE; t.esc = ESCAPE_WORD;           break;
        case 'W': t.type = TOK_ESCAPE; t.esc = ESCAPE_NON_WORD;       break;
        case 's': t.type = TOK_ESCAPE; t.esc = ESCAPE_SPACE;          break;
        case 'S': t.type = TOK_ESCAPE; t.esc = ESCAPE_NON_SPACE;      break;

        /* 其余所有情况（含元字符转义 \. \* 等、未知转义 \x）→ TOK_CHAR */
        default:
            t.ch = c;
            break;
    }

    if (c == '\0') {
        return make_error(tok, "转义序列不完整");
    }

    return t;
}

/* ========================================================================== */
/*  字符集合解析 [...]                                                          */
/* ========================================================================== */

/**
 * 解析 [... ] 字符集合。
 * 调用前提：当前位置已在 '[' 上（调用者已消费 '[' 并记录了起始 pos）。
 *
 * 特殊规则：
 *   - 集合开头紧跟的 ']' 视为普通字符（如 [^]abc]）
 *   - 集合内 '-' 范围标记原样保留，由 DFA 匹配时的 bracket_matches() 处理
 *   - 集合内 '\' 暂按字面字符处理
 *
 * 返回值：t.bracket.str 指向 malloc 分配的字符串，调用者负责 free。
 */
static Token parse_bracket(Tokenizer *tok) {
    // pos 回退到 '['
    Token t = { .type = TOK_BRACKET, .pos = tok->pos - 1 };  

    /* 收集括号内的原始内容 */
    size_t capacity = 16;
    size_t len = 0;
    char *buf = calloc(capacity, 1);
    if (!buf) {
        return make_error(tok, "内存分配失败");
    }

    char c;
    int first = 1;  /* 标记是否为集合内的第一个字符 */
    while ((c = consume(tok)) != '\0') {
        /* '[' 后紧跟的第一个 ']' 是普通字符，不是闭合符 */
        if (c == ']' && !first) {
            break;
        }
        first = 0;

        /* 扩容 */
        if (len + 1 >= capacity) {
            capacity *= 2;
            char *tmp = realloc(buf, capacity);
            if (!tmp) {
                free(buf);
                return make_error(tok, "内存分配失败");
            }
            buf = tmp;
        }
        buf[len++] = c;
    }

    if (c == '\0') {
        /* 未找到闭合 ']' */
        free(buf);
        return make_error(tok, "未闭合的字符集合");
    }

    /* 成功：将收集的字符复制到 t.bracket.str */
    t.bracket.str = buf;   /* 直接移交所有权 */
    t.bracket.len = len;
    return t;
}

/* ========================================================================== */
/*  量词解析 {m,n}                                                             */
/* ========================================================================== */

/**
 * 解析 {m,n} 量词。
 * 调用前提：当前位置已在 '{' 上（调用者已消费 '{' 并记录了起始 pos）。
 *
 * 支持格式：
 *   - {m}     → min==max==m
 *   - {m,}    → min==m,   max==-1（上界无限）
 *   - {m,n}   → min==m,   max==n
 */
static Token parse_curly(Tokenizer *tok) {
    Token t = { .type = TOK_CURLY, .pos = tok->pos - 1 };  // pos 回退到 '{'

    /* 解析 min */
    long min_val = 0;
    int has_digit = 0;
    char c;
    while ((c = consume(tok)) != '\0') {
        if (c >= '0' && c <= '9') {
            /* 防止整数溢出 — 量词值 > 100000 视为拒绝 */
            if (min_val > 100000) {
                return make_error(tok, "量词值过大");
            }
            min_val = min_val * 10 + (c - '0');
            has_digit = 1;
        } else {
            break;
        }
    }

    if (!has_digit) {
        /* ERE 兼容：裸 { 后不是数字 → 回退为字面字符 */
        t.type = TOK_CHAR;
        t.ch = '{';
        t.pos = tok->pos - 1;
        return t;
    }

    t.curly.min = (int)min_val;

    /* 遇到 ',' → 解析 max */
    if (c == ',') {
        long max_val = 0;
        has_digit = 0;
        while ((c = consume(tok)) != '\0') {
            if (c >= '0' && c <= '9') {
                if (max_val > 100000) {
                    return make_error(tok, "量词值过大");
                }
                max_val = max_val * 10 + (c - '0');
                has_digit = 1;
            } else {
                break;
            }
        }

        if (has_digit) {
            t.curly.max = (int)max_val;
        } else {
            t.curly.max = -1;  /* {m,} 上界无限 */
        }
    } else {
        t.curly.max = t.curly.min;  /* {m} 上下界相同 */
    }

    /* 必须遇到 '}' */
    if (c != '}') {
        /* ERE 兼容：未闭合的 { → 回退为字面字符。
         * 但如果看到了数字且 c 是字母或逗号后有字母，按 ERE 语义
         * 这不是合法量词 → 回退为普通字符。 */
        t.type = TOK_CHAR;
        t.ch = '{';
        t.pos = tok->pos - 1;
        return t;
    }

    return t;
}

/* ========================================================================== */
/*  公共 API 实现                                                               */
/* ========================================================================== */

void tokenizer_init(Tokenizer *tok, const char *input) {
    memset(tok, 0, sizeof(*tok));
    tok->input = input;
    tok->length = input ? strlen(input) : 0;
    tok->pos = 0;
    tok->error_code = 0;
    tok->error_msg[0] = '\0';
}

Token tokenizer_next(Tokenizer *tok) {
    Token t = { .type = TOK_EOF, .pos = tok->pos };

    /* 已有错误 → 持续返回错误 */
    if (tok->error_code) {
        return make_error(tok, "tokenizer 处于错误状态");
    }

    /* 输入结束 */
    if (tok->pos >= tok->length) {
        return t;
    }

    skip_whitespace(tok);
    if (tok->pos >= tok->length) {
        return t;
    }

    char c = peek(tok);
    t.pos = tok->pos;

    switch (c) {
        case '(': t.type = TOK_LPAREN;     consume(tok); break;
        case ')': t.type = TOK_RPAREN;     consume(tok); break;
        case '*': t.type = TOK_STAR;       consume(tok); break;
        case '+': t.type = TOK_PLUS;       consume(tok); break;
        case '?': t.type = TOK_QUESTION;   consume(tok); break;
        case '|': t.type = TOK_PIPE;       consume(tok); break;
        case '^': t.type = TOK_CARET;      consume(tok); break;
        case '$': t.type = TOK_DOLLAR;     consume(tok); break;
        case '.': t.type = TOK_DOT;        consume(tok); break;

        case '\\': consume(tok); t = parse_escape(tok); break;
        case '[':  consume(tok); t = parse_bracket(tok); break;
        case '{':  consume(tok); t = parse_curly(tok);   break;

        default:
            t.type = TOK_CHAR;
            t.ch = consume(tok);
            break;
    }

    return t;
}

const char *token_to_string(Token tok) {
    switch (tok.type) {
        case TOK_CHAR:          return "普通字符";
        case TOK_ESCAPE:        return "转义序列";
        case TOK_DOT:           return "点号 .";
        case TOK_BRACKET:       return "字符集合 [...]";
        case TOK_STAR:          return "星号 *";
        case TOK_PLUS:          return "加号 +";
        case TOK_QUESTION:      return "问号 ?";
        case TOK_CURLY:         return "量词 {m,n}";
        case TOK_LPAREN:        return "左括号 (";
        case TOK_RPAREN:        return "右括号 )";
        case TOK_PIPE:          return "竖线 |";
        case TOK_CARET:         return "脱字符 ^";
        case TOK_DOLLAR:        return "美元符 $";
        case TOK_EOF:           return "输入结束";
        case TOK_ERROR:         return "词法错误";
        default:                return "未知类型";
    }
}