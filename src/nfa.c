#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfa.h"

#ifdef _WIN32
#include <windows.h>
#endif

/* ========================================================================== */
/*  Thompson NFA 构造                                                          */
/*                                                                            */
/*  本文件实现正则表达式 → ε-NFA 的 Thompson 构造算法：                         */
/*   1. 每个 AST 节点被递归转换为一个 NFAFragment（带入口/出口的 NFA 子图）     */
/*   2. 所有状态由 StateVec 统一管理，以便后续释放和遍历                         */
/*   3. 每个 NFAState 最多有两条出边（edge1 / edge2），这是 Thompson ε-NFA     */
/*      的核心不变量                                                        */
/*                                                                            */
/*  核心思路：                                                                  */
/*    - 原子节点（CHAR / DOT / ESCAPE / BRACKET）：两状态 + 一条匹配边         */
/*    - 复合节点（CONCAT / ALTER / 量词）：组合子片段的入口和出口，              */
/*      必要时新建分裂状态（ε 分叉）和汇聚状态（ε 汇合）                        */
/* ========================================================================== */

/* ========================================================================== */
/*  内部辅助：动态状态数组（State Vector）                                      */
/* ========================================================================== */

/** 初始容量 — 足够容纳大多数简单的 NFA 而无需扩容 */
#define NFA_STATE_CAP 64

typedef struct {
    NFAState **data;        /* 状态指针数组 */
    int count;              /* 当前已存入的状态数 */
    int capacity;           /* 当前容量 */
} StateVec;

/** 初始化状态数组 */
static void vec_init(StateVec *v) {
    v->data     = malloc(NFA_STATE_CAP * sizeof(NFAState *));
    if (!v->data) {
        v->count    = 0;
        v->capacity = 0;
        return;
    }
    v->count    = 0;
    v->capacity = NFA_STATE_CAP;
}

/**
 * 将状态加入数组末尾。
 * 容量不足时自动翻倍扩容。
 */
static void vec_push(StateVec *v, NFAState *s) {
    if (v->count >= v->capacity) {
        v->capacity *= 2;
        v->data = realloc(v->data, v->capacity * sizeof(NFAState *));
    }
    v->data[v->count++] = s;
}

/* ========================================================================== */
/*  内部辅助：状态创建                                                          */
/* ========================================================================== */

/**
 * 创建新状态，分配全局唯一 id（以 vec 的当前大小为编号）。
 * calloc 确保未使用的边字段为零（next=NULL）。
 */
static NFAState *state_new(int id) {
    NFAState *s = calloc(1, sizeof(NFAState));
    s->id = id;
    return s;
}

/* ========================================================================== */
/*  Thompson 构造核心：AST → NFAFragment 递归                                    */
/* ========================================================================== */

static NFAFragment build_frag(const ASTNode *node, StateVec *vec);

/* ---- 原子节点：两状态 + 一条匹配边 ---- */

/**
 * 构造原子片断。
 * 所有原子（普通字符 / 点号 / 转义 / 字符集合）结构相同：
 *   [start] --- match ---> [end]
 * 不同处仅在于边上的匹配条件（edge1_type 和对应的数据字段）。
 */
static NFAFragment build_atom(NFAEdgeType etype, const ASTNode *node,
                               StateVec *vec) {
    NFAState *s = state_new(vec->count);  vec_push(vec, s);
    NFAState *e = state_new(vec->count);  vec_push(vec, e);

    /* 设置起始状态的第一条出边 */
    s->edge1_type = etype;
    s->edge1_next = e;

    switch (etype) {
    case NFA_EDGE_CHAR:
        s->edge1_char = node->ch;
        break;                                          /* ← 必须 break，否则跌落 NFA_EDGE_DOT */
    case NFA_EDGE_DOT:
        /* 点号不附加数据 — edge1_type 本身就编码了"任意字符"语义 */
        break;
    case NFA_EDGE_ESCAPE:
        s->edge1_esc = node->esc;
        break;
    case NFA_EDGE_BRACKET:
        /* 字符集合的字符串需要独立拷贝 — AST 释放时 bracket.str 也会被释放 */
        if (node->bracket.str && node->bracket.len) {
            s->edge1_bracket.str = malloc(node->bracket.len);
            memcpy(s->edge1_bracket.str, node->bracket.str, node->bracket.len);
            s->edge1_bracket.len = node->bracket.len;
        }
        break;
    default:
        break;
    }

    NFAFragment f = { s, e };
    return f;
}

/* ---- 递归构造 ---- */

/**
 * 递归遍历 AST，对每种节点类型执行对应的 Thompson 构造规则。
 *
 * @param node  当前 AST 节点（不能为 NULL）
 * @param vec   状态数组 — 新状态通过 state_new/vec_push 加入
 * @return      构造完成的 NFAFragment（入口 + 出口）
 */
static NFAFragment build_frag(const ASTNode *node, StateVec *vec) {
    if (!node) { NFAFragment f = { NULL, NULL }; return f; }

    switch (node->type) {

    /* ============================================================== */
    /*  叶子节点 — 全部委托给 build_atom                                 */
    /* ============================================================== */
    case AST_CHAR:    return build_atom(NFA_EDGE_CHAR,    node, vec);
    case AST_DOT:     return build_atom(NFA_EDGE_DOT,     node, vec);
    case AST_ESCAPE:  return build_atom(NFA_EDGE_ESCAPE,  node, vec);
    case AST_BRACKET: return build_atom(NFA_EDGE_BRACKET, node, vec);

    /* ============================================================== */
    /*  连接 A B                                                        */
    /*                                                                  */
    /*  结构：A.start … A.end --ε--> B.start … B.end                     */
    /*  直接用 ε 边焊接 A 的出口和 B 的入口，组合进口出口。                 */
    /*  不引入新状态 — 这是唯一不增加状态数的复合操作。                       */
    /* ============================================================== */
    case AST_CONCAT: {
        NFAFragment a = build_frag(node->left,  vec);
        NFAFragment b = build_frag(node->right, vec);

        /* A.end 的第一条出边目前是空的（原子出口无出边），直接占用 */
        a.end->edge1_type = NFA_EDGE_EPSILON;
        a.end->edge1_next = b.start;

        NFAFragment f = { a.start, b.end };
        return f;
    }

    /* ============================================================== */
    /*  并集 A|B                                                        */
    /*                                                                  */
    /*  结构（+1 状态数）:                                               */
    /*                ┌─ A.start … A.end ─┐                             */
    /*  new_start ─ε→                      →ε─ new_end                   */
    /*                └─ B.start … B.end ─┘                             */
    /*                                                                  */
    /*  new_start 是一个分裂状态：两条 ε 出边分别进入 A 和 B。              */
    /*  A.end 和 B.end 分别 ε 汇聚到 new_end。                            */
    /* ============================================================== */
    case AST_ALTER: {
        NFAFragment a = build_frag(node->left,  vec);
        NFAFragment b = build_frag(node->right, vec);

        NFAState *ns = state_new(vec->count);  vec_push(vec, ns);
        NFAState *ne = state_new(vec->count);  vec_push(vec, ne);

        ns->edge1_type = NFA_EDGE_EPSILON;  ns->edge1_next = a.start;
        ns->edge2_type = NFA_EDGE_EPSILON;  ns->edge2_next = b.start;

        a.end->edge1_type = NFA_EDGE_EPSILON;  a.end->edge1_next = ne;
        b.end->edge1_type = NFA_EDGE_EPSILON;  b.end->edge1_next = ne;

        NFAFragment f = { ns, ne };
        return f;
    }

    /* ============================================================== */
    /*  星号 A*（零次或多次）                                            */
    /*                                                                  */
    /*  结构（+2 状态数）:                                               */
    /*                                                                  */
    /*  new_start ─ε→ A.start … A.end ─ε→ new_end                       */
    /*      │                          ↑                                */
    /*      └──────── ε ──────────────┘  (bypass — 跳过整个子图)          */
    /*                  A.end ─ε→ A.start  (loop — 重复匹配)              */
    /*                                                                  */
    /*  new_start 的 edge1 进入子图，edge2 直接绕到 new_end（零次匹配）。   */
    /*  A.end 的 edge1 回到 A.start（循环），edge2 到 new_end（退出）。    */
    /* ============================================================== */
    case AST_STAR: {
        NFAFragment a = build_frag(node->left, vec);

        NFAState *ns = state_new(vec->count);  vec_push(vec, ns);
        NFAState *ne = state_new(vec->count);  vec_push(vec, ne);

        ns->edge1_type = NFA_EDGE_EPSILON;  ns->edge1_next = a.start;
        ns->edge2_type = NFA_EDGE_EPSILON;  ns->edge2_next = ne;

        a.end->edge1_type = NFA_EDGE_EPSILON;  a.end->edge1_next = a.start;
        a.end->edge2_type = NFA_EDGE_EPSILON;  a.end->edge2_next = ne;

        NFAFragment f = { ns, ne };
        return f;
    }

    /* ============================================================== */
    /*  加号 A+（至少一次）                                              */
    /*                                                                  */
    /*  结构（+2 状态数）:                                               */
    /*                                                                  */
    /*  new_start ─ε→ A.start … A.end ─ε→ new_end                       */
    /*                     │            ↑                                */
    /*                     └── ε ──────┘  (loop — 重复匹配，但不绕过)      */
    /*                                                                  */
    /*  与星号的区别：new_start 没有 bypass ε 边 → 必须至少走一次子图。    */
    /* ============================================================== */
    case AST_PLUS: {
        NFAFragment a = build_frag(node->left, vec);

        NFAState *ns = state_new(vec->count);  vec_push(vec, ns);
        NFAState *ne = state_new(vec->count);  vec_push(vec, ne);

        ns->edge1_type = NFA_EDGE_EPSILON;  ns->edge1_next = a.start;

        a.end->edge1_type = NFA_EDGE_EPSILON;  a.end->edge1_next = a.start;
        a.end->edge2_type = NFA_EDGE_EPSILON;  a.end->edge2_next = ne;

        NFAFragment f = { ns, ne };
        return f;
    }

    /* ============================================================== */
    /*  问号 A?（零次或一次）                                            */
    /*                                                                  */
    /*  结构（+2 状态数）:                                               */
    /*                                                                  */
    /*  new_start ─ε→ A.start … A.end ─ε→ new_end                       */
    /*      │                          ↑                                */
    /*      └──────── ε ──────────────┘  (bypass — 跳过整个子图)          */
    /*                                                                  */
    /*  与星号的区别：A.end 没有回到 A.start 的回边 → 只能匹配零次或一次。 */
    /* ============================================================== */
    case AST_QUESTION: {
        NFAFragment a = build_frag(node->left, vec);

        NFAState *ns = state_new(vec->count);  vec_push(vec, ns);
        NFAState *ne = state_new(vec->count);  vec_push(vec, ne);

        ns->edge1_type = NFA_EDGE_EPSILON;  ns->edge1_next = a.start;
        ns->edge2_type = NFA_EDGE_EPSILON;  ns->edge2_next = ne;

        a.end->edge1_type = NFA_EDGE_EPSILON;  a.end->edge1_next = ne;

        NFAFragment f = { ns, ne };
        return f;
    }

    /* ============================================================== */
    /*  范围量词 A{m,n}                                                  */
    /*                                                                  */
    /*  构造策略：                                                        */
    /*   1. 串联 m 个 A 的副本（强制匹配次数）                             */
    /*   2. 若 n == -1（无上限），尾部追加一个 A*                         */
    /*   3. 若 n > m，串联 (n-m) 个可选 A 副本 — 每个可选副本通过          */
    /*      ε 边的绕行实现"可以跳过"的语义                                */
    /*                                                                  */
    /*  可选副本结构：                                                    */
    /*      cur.end ─ε→ copy.start … copy.end                            */
    /*              ─ε─────────────────→  (edge2 直接跳到 copy.end)       */
    /* ============================================================== */
    case AST_CURLY: {
        int m = node->quant_min;
        int n = node->quant_max;

        /* 防御：规范化边界值 */
        if (m < 0)  m = 0;
        if (n < -1) n = -1;

        /* 第 1 步：构造 m 个强制副本的串联链 */
        NFAFragment cur;
        if (m == 0) {
            /* {0,n} — 零个强制副本，用一对 ε 状态占位 */
            NFAState *s = state_new(vec->count);  vec_push(vec, s);
            NFAState *e = state_new(vec->count);  vec_push(vec, e);
            s->edge1_type = NFA_EDGE_EPSILON;  s->edge1_next = e;
            cur.start = s;
            cur.end   = e;
        } else {
            cur = build_frag(node->left, vec);          /* 第 1 个副本 */
            for (int i = 1; i < m; i++) {
                NFAFragment copy = build_frag(node->left, vec);
                cur.end->edge1_type = NFA_EDGE_EPSILON;
                cur.end->edge1_next = copy.start;
                cur.end = copy.end;
            }
        }

        /* 第 2 步：处理剩余部分 */
        if (n == -1) {
            /* 无上限 {m,} — 尾部追加 A* */
            NFAFragment star = build_frag(node->left, vec);

            NFAState *ss = state_new(vec->count);  vec_push(vec, ss);
            NFAState *se = state_new(vec->count);  vec_push(vec, se);

            ss->edge1_type = NFA_EDGE_EPSILON;  ss->edge1_next = star.start;
            ss->edge2_type = NFA_EDGE_EPSILON;  ss->edge2_next = se;

            star.end->edge1_type = NFA_EDGE_EPSILON;  star.end->edge1_next = star.start;
            star.end->edge2_type = NFA_EDGE_EPSILON;  star.end->edge2_next = se;

            cur.end->edge1_type = NFA_EDGE_EPSILON;
            cur.end->edge1_next = ss;
            cur.end = se;
        } else {
            /* 有限上限 {m,n} — 追加 (n-m) 个可选副本 */
            int extra = n - m;
            for (int i = 0; i < extra; i++) {
                NFAFragment copy = build_frag(node->left, vec);

                /* edge1: 走入这个可选副本    edge2: 跳过这个可选副本     */
                cur.end->edge1_type = NFA_EDGE_EPSILON;
                cur.end->edge1_next = copy.start;
                cur.end->edge2_type = NFA_EDGE_EPSILON;
                cur.end->edge2_next = copy.end;

                cur.end = copy.end;
            }
        }

        return cur;
    }

    /* ============================================================== */
    /*  捕获组 (a) — 直接透传子表达式                                    */
    /*                                                                  */
    /*  NFA 层面直接递归左子节点，不引入额外状态和边。                      */
    /*  捕获语义由 capture.c 在 AST/DFA 层面独立处理。                     */
    /* ============================================================== */
    case AST_GROUP:
        /* 空 GROUP（left=NULL）对应 () — 零宽度，用 ε 边透传 */
        if (!node->left) {
            NFAState *s = state_new(vec->count);  vec_push(vec, s);
            NFAState *e = state_new(vec->count);  vec_push(vec, e);
            s->edge1_type = NFA_EDGE_EPSILON;
            s->edge1_next = e;
            NFAFragment f = { s, e };
            return f;
        }
        return build_frag(node->left, vec);

    /* ============================================================== */
    /*  锚定 ^ / $ — 零宽度断言，用一条 ε 边实现                       */
    /*                                                                  */
    /*  NFA 层面用 ε 边透传：锚点约束在 DFA 构造阶段通过 start/end  */
    /*  位置检查来实现，NFA 本身不做区分。                               */
    /* ============================================================== */
    case AST_ANCHOR_START:
    case AST_ANCHOR_END:
    {
        /* 零宽度断言，语义上等同于一条 ε 边 */
        NFAState *s = state_new(vec->count);  vec_push(vec, s);
        NFAState *e = state_new(vec->count);  vec_push(vec, e);
        s->edge1_type = NFA_EDGE_EPSILON;
        s->edge1_next = e;
        NFAFragment f = { s, e };
        return f;
    }

    /* ---- 防御：遇到未知类型返回空片段 ---- */
    default: {
        NFAFragment f = { NULL, NULL };
        return f;
    }
    }
}

/* ========================================================================== */
/*  公共 API                                                                   */
/* ========================================================================== */

/**
 * Thompson 构造：从 AST 根节点构建完整 NFA 图。
 *
 * 调用流程：
 *   1. 初始化状态数组
 *   2. 递归调用 build_frag 构造 NFA 子图
 *   3. 将向量中的数据打包为 NFAGraph 返回
 *
 * @param ast_root  由 parser_parse 产出的 AST 根节点
 * @return          完整的 NFA 图（含入口、出口、状态数组）。
 *                  当 ast_root 为 NULL 时返回全零结构。
 *                  调用者最终必须调用 nfa_free() 释放。
 */

/**
 * 递归遍历 AST，检测最左/最右叶是否为锚定节点。
 * CONCAT 的左结合性意味着锚定可能被套多层 CONCAT。
 */
static int ast_has_leftmost_anchor(const ASTNode *node) {
    if (!node) return 0;
    if (node->type == AST_ANCHOR_START) return 1;
    if (node->type == AST_CONCAT) return ast_has_leftmost_anchor(node->left);
    return 0;
}

static int ast_has_rightmost_anchor(const ASTNode *node) {
    if (!node) return 0;
    if (node->type == AST_ANCHOR_END) return 1;
    if (node->type == AST_CONCAT) return ast_has_rightmost_anchor(node->right);
    return 0;
}

NFAGraph nfa_from_ast(const ASTNode *ast_root) {
    NFAGraph nfa = {0};
    if (!ast_root) return nfa;

    /* 检测最左/最右锚定 */
    nfa.has_anchor_start = ast_has_leftmost_anchor(ast_root);
    nfa.has_anchor_end   = ast_has_rightmost_anchor(ast_root);

    StateVec vec;
    vec_init(&vec);

    NFAFragment frag = build_frag(ast_root, &vec);

    nfa.start       = frag.start;
    nfa.end         = frag.end;
    nfa.state_count = vec.count;
    nfa.states      = vec.data;      /* 移交所有权 */

    return nfa;
}

/**
 * 释放 NFA 图占用的所有内存。
 *
 * 遍历 states 数组：
 *   - 释放每条 bracket 边的字符串拷贝
 *   - 释放状态结构体本身
 *   - 最后释放 states 数组
 *
 * 行为：允许对空 NFA 或已释放的 NFA 重复调用（空操作）。
 */
void nfa_free(NFAGraph *nfa) {
    if (!nfa || !nfa->states) return;

    for (int i = 0; i < nfa->state_count; i++) {
        NFAState *s = nfa->states[i];
        if (!s) continue;

        /* 释放 bracket 边中动态分配的字符串 */
        if (s->edge1_type == NFA_EDGE_BRACKET && s->edge1_bracket.str)
            free(s->edge1_bracket.str);
        if (s->edge2_type == NFA_EDGE_BRACKET && s->edge2_bracket.str)
            free(s->edge2_bracket.str);

        free(s);
    }

    free(nfa->states);

    /* 防御性清零 — 防止 double-free */
    nfa->states      = NULL;
    nfa->state_count = 0;
    nfa->start       = NULL;
    nfa->end         = NULL;
}

/* ========================================================================== */
/*  nfa_dump — 调试打印                                                        */
/* ========================================================================== */

/** 将边类型转为可读字符串 */
static const char *etype_name(NFAEdgeType t) {
    switch (t) {
    case NFA_EDGE_EPSILON: return "ε";
    case NFA_EDGE_CHAR:    return "char";
    case NFA_EDGE_DOT:     return "dot";
    case NFA_EDGE_ESCAPE:  return "esc";
    case NFA_EDGE_BRACKET: return "bracket";
    default:               return "?";
    }
}

/**
 * 打印单条出边。
 * 若 next 为 NULL 则跳过（该边未使用）。
 */
static void dump_one_edge(const char *label, NFAEdgeType type,
                          char ch, EscapeSeq esc,
                          const char *bstr, size_t blen,
                          NFAState *next) {
    if (!next) return;

    printf("    %s: %s", label, etype_name(type));

    switch (type) {
    case NFA_EDGE_CHAR:
        printf(" '%c'", ch);
        break;
    case NFA_EDGE_ESCAPE: {
        static const char *names[] = {"\\d","\\D","\\w","\\W","\\s","\\S"};
        printf(" %s", names[esc]);
        break;
    }
    case NFA_EDGE_BRACKET:
        if (bstr && blen)
            printf(" [%.*s]", (int)blen, bstr);
        break;
    default:
        break;
    }

    printf(" -> %d\n", next->id);
}

/**
 * 以人类可读的格式将完整 NFA 图打印到 stdout。
 *
 * 输出：每个状态的编号、标记（start/accept）、以及所有出边信息。
 * 仅供调试使用。
 */
void nfa_dump(const NFAGraph *nfa) {
    if (!nfa || !nfa->states) {
        printf("(null NFA)\n");
        return;
    }

    printf("========== NFA ==========\n");
    printf("states: %d  start: %d  accept: %d\n",
           nfa->state_count,
           nfa->start ? nfa->start->id : -1,
           nfa->end   ? nfa->end->id   : -1);
    printf("--------------------------\n");

    for (int i = 0; i < nfa->state_count; i++) {
        NFAState *s = nfa->states[i];
        if (!s) continue;

        int is_s = (s == nfa->start);
        int is_e = (s == nfa->end);

        printf("[%d]%s%s\n", s->id,
               is_s ? " (start)" : "",
               is_e ? " (accept)" : "");

        dump_one_edge("e1", s->edge1_type, s->edge1_char, s->edge1_esc,
                      s->edge1_bracket.str, s->edge1_bracket.len,
                      s->edge1_next);
        dump_one_edge("e2", s->edge2_type, s->edge2_char, s->edge2_esc,
                      s->edge2_bracket.str, s->edge2_bracket.len,
                      s->edge2_next);
    }

    printf("==========================\n");
}

/* ========================================================================== */
/*  nfa_dump_dot — Graphviz DOT 输出                                           */
/* ========================================================================== */

/**
 * 构建 NFA 边的 DOT 标签字符串（不含引号，调用者负责写入 DOT 文件）。
 *
 * DOT 标签中的特殊字符（" \）在本函数内完成转义。
 *
 * @param type  边类型
 * @param ch    NFA_EDGE_CHAR 时的字符
 * @param esc   NFA_EDGE_ESCAPE 时的转义类型
 * @param bstr  NFA_EDGE_BRACKET 时的括号内容
 * @param blen  bstr 的长度
 * @param style 输出：边的 DOT style 属性前缀（ε 边为 "style=dashed, color=red, "，其余为空）
 * @param buf   输出：边长内容写入此缓冲区
 * @param bufsz buf 容量
 */
static void nfa_edge_dot_label(NFAEdgeType type, char ch, EscapeSeq esc,
                                const char *bstr, size_t blen,
                                const char **style, char *buf, size_t bufsz) {
    *style = "";
    switch (type) {
    case NFA_EDGE_EPSILON:
        *style = "style=dashed, color=red, ";
        snprintf(buf, bufsz, "ε");
        break;
    case NFA_EDGE_CHAR:
        if (ch == '"')       snprintf(buf, bufsz, "\\\"");
        else if (ch == '\\') snprintf(buf, bufsz, "\\\\");
        else                 snprintf(buf, bufsz, "%c", ch);
        break;
    case NFA_EDGE_DOT:
        snprintf(buf, bufsz, ".");
        break;
    case NFA_EDGE_ESCAPE: {
        static const char *names[] = {"\\\\d","\\\\D","\\\\w","\\\\W","\\\\s","\\\\S"};
        snprintf(buf, bufsz, "%s", names[esc]);
        break;
    }
    case NFA_EDGE_BRACKET: {
        /* 构建 [content]，同时对 DOT 特殊字符转义 */
        size_t pos = 0;
        if (pos < bufsz) buf[pos++] = '[';
        if (bstr) {
            for (size_t j = 0; j < blen && pos < bufsz - 2; j++) {
                if (bstr[j] == '"') {
                    if (pos < bufsz - 2) { buf[pos++] = '\\'; buf[pos++] = '"'; }
                } else if (bstr[j] == '\\') {
                    if (pos < bufsz - 2) { buf[pos++] = '\\'; buf[pos++] = '\\'; }
                } else {
                    buf[pos++] = bstr[j];
                }
            }
        }
        if (pos < bufsz) buf[pos++] = ']';
        buf[pos] = '\0';
        break;
    }
    default:
        snprintf(buf, bufsz, "?");
    }
}

/**
 * 将 NFA 图以 Graphviz DOT 格式输出到指定文件。
 *
 * 输出规约：
 * - 有向图 rankdir=LR（从左到右）
 * - 普通状态：圆圈 (shape=circle)
 * - 接受状态（end）：双圈 (shape=doublecircle)
 * - 起始状态：不可见节点 (shape=point)，用一条入边指向 NFA 的 start 状态
 * - ε 边：红色虚线，标签 "ε"
 * - 普通匹配边：实线，标签描述匹配条件
 *
 * 用法：将输出粘贴到 https://viz-js.com 或 VS Code Graphviz 扩展可直接查看。
 *
 * @param nfa  NFA 图
 * @param fp   输出文件（可为 stdout）
 */
void nfa_dump_dot(const NFAGraph *nfa, FILE *fp) {
    if (!nfa || !nfa->states) {
        fprintf(fp, "// (null NFA)\n");
        return;
    }

    fprintf(fp, "digraph NFA {\n");
    fprintf(fp, "    rankdir=LR;\n");
    fprintf(fp, "    node [shape=circle];\n\n");

    /* 不可见起始节点 */
    fprintf(fp, "    start [shape=point];\n");

    /* 输出每个状态 */
    for (int i = 0; i < nfa->state_count; i++) {
        NFAState *s = nfa->states[i];
        if (!s) continue;

        const char *shape = (s == nfa->end) ? "doublecircle" : "circle";
        fprintf(fp, "    S%d [shape=%s, label=\"%d\"];\n",
                s->id, shape, s->id);
    }

    fprintf(fp, "\n    /* start edge */\n");
    if (nfa->start) {
        fprintf(fp, "    start -> S%d;\n", nfa->start->id);
    }

    /* 输出所有出边 */
    fprintf(fp, "\n    /* transition edges */\n");
    for (int i = 0; i < nfa->state_count; i++) {
        NFAState *s = nfa->states[i];
        if (!s) continue;

        char label_buf[256];
        const char *style;

        /* edge1 */
        if (s->edge1_next) {
            nfa_edge_dot_label(s->edge1_type, s->edge1_char, s->edge1_esc,
                               s->edge1_bracket.str, s->edge1_bracket.len,
                               &style, label_buf, sizeof(label_buf));
            fprintf(fp, "    S%d -> S%d [%slabel=\"%s\"];\n",
                    s->id, s->edge1_next->id, style, label_buf);
        }

        /* edge2 */
        if (s->edge2_next) {
            nfa_edge_dot_label(s->edge2_type, s->edge2_char, s->edge2_esc,
                               s->edge2_bracket.str, s->edge2_bracket.len,
                               &style, label_buf, sizeof(label_buf));
            fprintf(fp, "    S%d -> S%d [%slabel=\"%s\"];\n",
                    s->id, s->edge2_next->id, style, label_buf);
        }
    }

    fprintf(fp, "}\n");
}

int nfa_dump_dot_file(const NFAGraph *nfa, const char *filepath) {
    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;
    nfa_dump_dot(nfa, fp);
    fclose(fp);
    return 0;
}
