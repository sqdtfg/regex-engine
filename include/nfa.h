#ifndef REGEX_NFA_H
#define REGEX_NFA_H

#include <stdio.h>
#include "parser.h"

/* ========================================================================== */
/*  NFA 状态                                                                    */
/* ========================================================================== */

/** 转移边的类型 */
typedef enum {
    NFA_EDGE_EPSILON,       /* ε 边 — 不消耗输入即可转移 */
    NFA_EDGE_CHAR,          /* 匹配单个字符 (ch) */
    NFA_EDGE_DOT,           /* 匹配任意单个字符 */
    NFA_EDGE_ESCAPE,        /* 匹配转义序列 (\d \w \s 等，用 esc 字段) */
    NFA_EDGE_BRACKET,       /* 匹配字符集合 ([abc] / [^0-9]) */
} NFAEdgeType;

/** NFA 状态节点 */
typedef struct NFAState {
    int id;                     /* 状态编号（全局唯一，调试用） */

    /* ---- 两条出边 ---- */
    NFAEdgeType edge1_type;     /* 第一条出边的类型 */
    char  edge1_char;           /* NFA_EDGE_CHAR 时，匹配的字符 */
    EscapeSeq edge1_esc;        /* NFA_EDGE_ESCAPE 时，转义类型 */
    struct {
        char *str;              /* NFA_EDGE_BRACKET 时，字符集合内容 */
        size_t len;
    } edge1_bracket;
    struct NFAState *edge1_next; /* 第一条出边指向的状态 */

    NFAEdgeType edge2_type;     /* 第二条出边的类型（ε-NFA 最多两条出边） */
    char  edge2_char;
    EscapeSeq edge2_esc;
    struct {
        char *str;
        size_t len;
    } edge2_bracket;
    struct NFAState *edge2_next;

} NFAState;

/* ========================================================================== */
/*  NFA 片段（Thompson 构造的中间产物）                                         */
/* ========================================================================== */

/**
 * 每个 AST 节点被转换成一段带 "进口" 和 "出口" 的 NFA 子图。
 * 调用者通过组合这些片段来构建完整 NFA。
 */
typedef struct {
    NFAState *start;    /* 进口状态 */
    NFAState *end;      /* 出口状态（只有一个） */
} NFAFragment;

/* ========================================================================== */
/*  NFA 图（完整 NFA）                                                          */
/* ========================================================================== */

typedef struct {
    NFAState *start;        /* 起始状态 */
    NFAState *end;          /* 接受状态 */
    int state_count;        /* 状态总数 */
    NFAState **states;      /* 所有状态的指针数组（用于释放） */
} NFAGraph;

/* ========================================================================== */
/*  API                                                                        */
/* ========================================================================== */

/**
 * Thompson 构造：从 AST 根节点构建 NFA。
 * @param ast_root  AST 根节点（由 parser_parse 产出）
 * @return         完整的 NFA 图。调用者负责调用 nfa_free() 释放。
 */
NFAGraph nfa_from_ast(const ASTNode *ast_root);

/** 释放 NFA 图的所有状态和边数据 */
void nfa_free(NFAGraph *nfa);

/** 打印 NFA 图（调试用，输出状态和转移关系） */
void nfa_dump(const NFAGraph *nfa);

/** 将 NFA 图输出为 Graphviz DOT 格式（可视化调试用） */
void nfa_dump_dot(const NFAGraph *nfa, FILE *fp);

/** 将 NFA 图输出为 Graphviz DOT 文件 */
int nfa_dump_dot_file(const NFAGraph *nfa, const char *filepath);

#endif /* REGEX_NFA_H */
