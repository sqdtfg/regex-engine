#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ========================================================================== */
/*  内部辅助                                                                    */
/* ========================================================================== */

/** 创建一棵新 AST 节点（叶子，无子节点） */
static ASTNode *ast_node_new(ASTNodeType type) {
    ASTNode *node = calloc(1, sizeof(*node));
    if (!node) return NULL;
    node->type = type;
    return node;
}

/* ========================================================================== */
/*  解析器内部                                                                  */
/* ========================================================================== */

/** 查看当前 token，不消费 */
static Token peek(Parser *p) {
    return p->current;
}

/** 消费当前 token，请求下一个 */
static Token advance(Parser *p) {
    Token old = p->current;
    p->current = tokenizer_next(&p->tokenizer);
    return old;
}

/** 如果当前 token 匹配预期类型则消费并返回 1，否则返回 0 不报错 */
static int match(Parser *p, RegexTokenType type) {
    if (peek(p).type == type) {
        advance(p);
        return 1;
    }
    return 0;
}

/** 消费指定类型的 token；不匹配则设置错误 */
static Token expect(Parser *p, RegexTokenType type, const char *what) {
    if (peek(p).type == type) {
        return advance(p);
    }
    p->error_code = 1;
    snprintf(p->error_msg, sizeof(p->error_msg),
             "期望 %s，但遇到了 %s", what, token_to_string(peek(p)));
    return peek(p);
}

/* ---- 前向声明（处理相互递归） ---- */
static ASTNode *parse_regex(Parser *p);
static ASTNode *parse_chain(Parser *p);
static ASTNode *parse_factor(Parser *p);
static ASTNode *parse_atom(Parser *p);

/* ========================================================================== */
/*  parse_atom — 解析原子（最高优先级）                                         */
/*  对应文法：                                                                  */
/*    atom → '(' regex ')'                                                    */
/*         | '[' ... ']'   (TOK_BRACKET)                                     */
/*         | '\d' ...       (TOK_ESCAPE)                                      */
/*         | '.'             (TOK_DOT)                                        */
/*         | 普通字符         (TOK_CHAR)                                       */
/* ========================================================================== */

static ASTNode *parse_atom(Parser *p) {
    if (p->error_code) return NULL;

    Token tok = peek(p);

    /* ---- 普通字符 ---- */
    if (tok.type == TOK_CHAR) {
        advance(p);
        ASTNode *node = ast_node_new(AST_CHAR);
        node->ch = tok.ch;
        node->pos = tok.pos;
        return node;
    }

    /* ---- 任意字符 . ---- */
    if (tok.type == TOK_DOT) {
        advance(p);
        ASTNode *node = ast_node_new(AST_DOT);
        node->pos = tok.pos;
        return node;
    }

    /* ---- 转义序列 \d \w \s ---- */
    if (tok.type == TOK_ESCAPE) {
        advance(p);
        ASTNode *node = ast_node_new(AST_ESCAPE);
        node->esc = tok.esc;
        node->pos = tok.pos;
        return node;
    }

    /* ---- 字符集合 [...] ---- */
    if (tok.type == TOK_BRACKET) {
        advance(p);
        ASTNode *node = ast_node_new(AST_BRACKET);
        node->bracket.str = tok.bracket.str;   /* 移交所有权 */
        node->bracket.len = tok.bracket.len;
        node->pos = tok.pos;
        return node;
    }

    /* ---- 行首锚定 ^ ---- */
    if (tok.type == TOK_CARET) {
        advance(p);
        ASTNode *node = ast_node_new(AST_ANCHOR_START);
        node->pos = tok.pos;
        return node;
    }

    /* ---- 行尾锚定 $ ---- */
    if (tok.type == TOK_DOLLAR) {
        advance(p);
        ASTNode *node = ast_node_new(AST_ANCHOR_END);
        node->pos = tok.pos;
        return node;
    }

    /* ---- 括号 '(' regex ')' ---- */
    if (tok.type == TOK_LPAREN) {
        advance(p);                                 /* 消费 '(' */
        ASTNode *inner = parse_regex(p);            /* 递归解析内部 */
        if (p->error_code) { ast_free(inner); return NULL; }
        expect(p, TOK_RPAREN, "右括号 ')'");        /* 消费 '）' */
        if (p->error_code) { ast_free(inner); return NULL; }

        /* 包装为捕获组 */
        ASTNode *group = ast_node_new(AST_GROUP);
        group->left = inner;
        group->pos = tok.pos;
        return group;
    }

    /* ---- 意外的 token ---- */
    p->error_code = 1;
    snprintf(p->error_msg, sizeof(p->error_msg),
             "不期望的 token: %s", token_to_string(tok));
    return NULL;
}

/* ========================================================================== */
/*  parse_factor — 解析因子 atom[quantifier]?                                  */
/*  对应文法：                                                                  */
/*    factor → atom ('*' | '+' | '?' | '{m,n}')?                              */
/*                                                                             */
/*  量词优先级最高 — 它只绑定紧邻的左操作数 atom                                     */
/* ========================================================================== */

static ASTNode *parse_factor(Parser *p) {
    if (p->error_code) return NULL;

    ASTNode *node = parse_atom(p);          /* 先解析原子 */
    if (!node || p->error_code) return NULL;

    Token tok = peek(p);

    /* ---- 检查量词 ---- */
    switch (tok.type) {
        case TOK_STAR:
            advance(p);
            {
                ASTNode *star = ast_node_new(AST_STAR);
                star->left = node;
                star->pos = tok.pos;
                return star;
            }

        case TOK_PLUS:
            advance(p);
            {
                ASTNode *plus = ast_node_new(AST_PLUS);
                plus->left = node;
                plus->pos = tok.pos;
                return plus;
            }

        case TOK_QUESTION:
            advance(p);
            {
                ASTNode *q = ast_node_new(AST_QUESTION);
                q->left = node;
                q->pos = tok.pos;
                return q;
            }

        case TOK_CURLY:
            advance(p);
            {
                ASTNode *curly = ast_node_new(AST_CURLY);
                curly->left = node;
                curly->quant_min = tok.curly.min;
                curly->quant_max = tok.curly.max;
                curly->pos = tok.pos;
                return curly;
            }

        default:
            break;      /* 无量词 */
    }

    return node;
}

/* ========================================================================== */
/*  parse_chain — 解析连接序列（中等优先级）                                     */
/*  对应文法：                                                                  */
/*    chain → factor+                                                        */
/*                                                                             */
/*  连接是并排放置，没有任何分隔符。连续多个因子 = 连接。                              */
/*  当 lookahead 是一个原子的开头时，继续拼接。                                    */
/*                                                                             */
/*  原子开头 = { TOK_CHAR, TOK_DOT, TOK_ESCAPE, TOK_BRACKET, TOK_LPAREN, TOK_CARET, TOK_DOLLAR }       */
/* ========================================================================== */

static ASTNode *parse_chain(Parser *p) {
    if (p->error_code) return NULL;

    ASTNode *left = parse_factor(p);        /* 至少一个因子 */
    if (!left || p->error_code) return NULL;

    while (!p->error_code) {
        RegexTokenType t = peek(p).type;

        /* 判断下一个 token 是不是因子的开头 */
        int is_atom_start = (t == TOK_CHAR || t == TOK_DOT ||
                             t == TOK_ESCAPE || t == TOK_BRACKET ||
                             t == TOK_LPAREN || t == TOK_CARET ||
                             t == TOK_DOLLAR);

        if (!is_atom_start) break;          /* 不是因子开头 → 连接结束 */

        ASTNode *right = parse_factor(p);   /* 解析下一个因子 */
        if (!right || p->error_code) break;

        /* 创建 CONCAT 节点，左结合 */
        ASTNode *concat = ast_node_new(AST_CONCAT);
        concat->left = left;
        concat->right = right;
        concat->pos = left->pos;
        left = concat;
    }

    return left;
}

/* ========================================================================== */
/*  parse_regex — 解析完整正则表达式（最低优先级，处理并集）                        */
/*  对应文法：                                                                  */
/*    regex → chain ('|' chain)*                                            */
/*                                                                             */
/*  先切按 '|' 切分，每段内部交给 parse_chain 做连接                              */
/* ========================================================================== */

static ASTNode *parse_regex(Parser *p) {
    if (p->error_code) return NULL;

    ASTNode *left = parse_chain(p);         /* 左侧第一个分支 */
    if (!left || p->error_code) return NULL;

    while (match(p, TOK_PIPE)) {            /* 遇到 '|' */
        ASTNode *right = parse_chain(p);    /* 解析右侧分支 */
        if (!right || p->error_code) {
            ast_free(left);
            return NULL;
        }

        /* 创建 ALTER 节点 */
        ASTNode *alt = ast_node_new(AST_ALTER);
        alt->left = left;
        alt->right = right;
        alt->pos = left->pos;
        left = alt;
    }

    return left;
}

/* ========================================================================== */
/*  公共 API                                                                    */
/* ========================================================================== */

void parser_init(Parser *parser, const char *input) {
    memset(parser, 0, sizeof(*parser));
    tokenizer_init(&parser->tokenizer, input);
    parser->current = tokenizer_next(&parser->tokenizer);
}

ASTNode *parser_parse(Parser *parser) {
    ASTNode *root = parse_regex(parser);

    /* 解析完成后，应该遇到 EOF；否则有多余字符 */
    if (!parser->error_code && peek(parser).type != TOK_EOF) {
        parser->error_code = 1;
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "正则表达式在第 %zu 个字符处有多余内容", peek(parser).pos);
        ast_free(root);
        return NULL;
    }

    /* 如果输入就是空的，返回 NULL */
    if (!parser->error_code && root == NULL) {
        parser->error_code = 1;
        snprintf(parser->error_msg, sizeof(parser->error_msg),
                 "正则表达式为空");
    }

    return root;
}

void ast_free(ASTNode *node) {
    if (!node) return;
    ast_free(node->left);
    ast_free(node->right);
    if (node->type == AST_BRACKET) {
        free(node->bracket.str);
    }
    free(node);
}

/** 递归克隆 AST 节点（深拷贝） */
ASTNode *ast_clone(const ASTNode *node) {
    if (!node) return NULL;

    ASTNode *clone = calloc(1, sizeof(*clone));
    if (!clone) return NULL;

    *clone = *node;  /* 浅拷贝所有字段 */

    if (node->type == AST_BRACKET && node->bracket.str) {
        clone->bracket.str = strdup(node->bracket.str);
        if (!clone->bracket.str) {
            free(clone);
            return NULL;
        }
    }

    clone->left = ast_clone(node->left);
    clone->right = ast_clone(node->right);

    return clone;
}

const char *ast_type_name(ASTNodeType type) {
    switch (type) {
        case AST_CHAR:      return "普通字符";
        case AST_DOT:       return "任意字符";
        case AST_ESCAPE:    return "转义序列";
        case AST_BRACKET:   return "字符集合";
        case AST_CONCAT:    return "连接";
        case AST_ALTER:     return "并集";
        case AST_STAR:      return "星号量词";
        case AST_PLUS:      return "加号量词";
        case AST_QUESTION:  return "问号量词";
        case AST_CURLY:     return "范围量词";
        case AST_GROUP:     return "捕获组";
        case AST_ANCHOR_START: return "行首锚定";
        case AST_ANCHOR_END:   return "行尾锚定";
        default:            return "未知节点";
    }
}

/* ========================================================================== */
/*  AST 二叉树打印（右子树在上 → 根 → 左子树在下）                               */
/* ========================================================================== */

static void print_data(const ASTNode *node) {
    switch (node->type) {
        case AST_CHAR:      printf(" '%c'", node->ch); break;
        case AST_DOT:       printf(" .");              break;
        case AST_ESCAPE:    printf(" %s", (const char *[]){"\\d","\\D","\\w","\\W","\\s","\\S"}[node->esc]); break;
        case AST_BRACKET:   printf(" [%.*s]", (int)node->bracket.len, node->bracket.str); break;
        case AST_CURLY:
            if      (node->quant_max == -1) printf(" {%d,}", node->quant_min);
            else if (node->quant_min == node->quant_max) printf(" {%d}", node->quant_min);
            else    printf(" {%d,%d}", node->quant_min, node->quant_max);
            break;
        case AST_ANCHOR_START: printf(" ^"); break;
        case AST_ANCHOR_END:   printf(" $"); break;
        default: break;
    }
}

static void print_tree(const ASTNode *node, const char *pfx, int is_left) {
    if (!node) return;

    char next[512];
    snprintf(next, sizeof(next), "%s%s   ", pfx, is_left ? "│" : " ");

    /* 1. 右子树（上方） */
    print_tree(node->right, next, 0);

    /* 2. 当前节点 */
    printf("%s%s── %s", pfx, is_left ? "└" : "┌", ast_type_name(node->type));
    print_data(node);
    printf("\n");

    /* 3. 左子树（下方） */
    print_tree(node->left, next, 1);
}

void ast_print(const ASTNode *root) {
    if (!root) { printf("(空树)\n"); return; }

    print_tree(root->right, "", 0);
    printf("%s", ast_type_name(root->type));
    print_data(root);
    printf("\n");
    print_tree(root->left, "", 1);
}

