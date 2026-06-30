#ifndef REGEX_PARSER_H
#define REGEX_PARSER_H

#include "tokenizer.h"

/* ========================================================================== */
/*  AST 节点类型                                                               */
/* ========================================================================== */

/** AST 节点种类 */
typedef enum {
    AST_CHAR,           /* 普通字符        — 叶子 */
    AST_DOT,            /* 任意字符 .      — 叶子 */
    AST_ESCAPE,         /* 转义序列 \d\w\s — 叶子 */
    AST_BRACKET,        /* 字符集合 [abc]  — 叶子 */
    AST_CONCAT,         /* 连接 a b        — 二叉树 */
    AST_ALTER,          /* 并集 a|b        — 二叉树 */
    AST_STAR,           /* 星号 a*         — 一元 */
    AST_PLUS,           /* 加号 a+         — 一元 */
    AST_QUESTION,       /* 问号 a?         — 一元 */
    AST_CURLY,          /* 量词 a{m,n}     — 一元 */
    AST_GROUP,          /* 括号 (a)        — 一元（捕获组） */
} ASTNodeType;

/* 前向声明 */
typedef struct ASTNode ASTNode;

/** AST 节点 */
struct ASTNode {
    ASTNodeType type;           /* 节点类型 */

    /* ---- 叶子节点数据 ---- */
    char ch;                    /* AST_CHAR 的字符值 */
    EscapeSeq esc;              /* AST_ESCAPE 的转义类型 */
    struct {
        char *str;
        size_t len;
    } bracket;                  /* AST_BRACKET 的字符集合内容 */

    /* ---- 量词参数 ---- */
    int quant_min;              /* AST_CURLY 的最小重复次数 */
    int quant_max;              /* AST_CURLY 的最大重复次数 (-1 表示无上限) */

    /* ---- 二叉树结构 ---- */
    ASTNode *left;              /* 左子节点 */
    ASTNode *right;             /* 右子节点 */

    /* ---- 源位置（用于错误报告） ---- */
    size_t pos;
};

/* ========================================================================== */
/*  解析器状态                                                                  */
/* ========================================================================== */

typedef struct {
    Tokenizer tokenizer;        /* 词法分析器 */
    Token current;              /* 当前 lookahead token */
    int error_code;             /* 0 = 正常, 非0 = 出错 */
    char error_msg[256];        /* 错误信息 */
} Parser;

/* ========================================================================== */
/*  公共 API                                                                   */
/* ========================================================================== */

/** 初始化解析器 */
void parser_init(Parser *parser, const char *input);

/** 解析整个正则表达式，返回 AST 根节点（调用者负责释放） */
ASTNode *parser_parse(Parser *parser);

/** 释放 AST 树 */
void ast_free(ASTNode *node);

/** 将 AST 节点类型转为中文名称（调试用） */
const char *ast_type_name(ASTNodeType type);

/** 以二叉树形式打印 AST */
void ast_print(const ASTNode *root);

#endif /* REGEX_PARSER_H */
