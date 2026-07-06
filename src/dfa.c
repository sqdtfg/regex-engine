#include "dfa.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ========================================================================== */
/*  DFA 子集构造法实现                                                         */
/*                                                                            */
/*  算法核心：                                                                 */
/*   1. NFA 起始状态集的 ε-闭包 = DFA 状态 0                                   */
/*   2. BFS 遍历所有 DFA 状态，对每个状态 S 和每个字符 c：                       */
/*      a. move(S, c) → 从 S 中任意 NFA 状态出发，沿匹配 c 的边到达的状态集     */
/*      b. ε-closure(move(S, c)) → 目标 DFA 状态                              */
/*      c. 若目标状态是新的，分配新 DFA 状态 ID                                  */
/*      d. 记录转移 S --c--> target_id                                        */
/*   3. 若 DFA 状态中包含 NFA 接受状态，则该 DFA 状态也是接受状态                 */
/*                                                                            */
/*  因为新状态总是追加到数组末尾，BFS 用线性遍历 dfa_states 即可，无需独立队列。   */
/* ========================================================================== */

/* ========================================================================== */
/*  内部辅助：字符匹配                                                         */
/* ========================================================================== */

/** 检查单个字符是否匹配 bracket 中的字符集合（支持范围和否定） */
static int bracket_matches(const char *bstr, size_t blen, char c) {
    if (!bstr || blen == 0) return 0;

    int negate = 0;
    size_t i = 0;

    if (bstr[0] == '^') {
        negate = 1;
        i = 1;
    }

    int matched = 0;
    for (; i < blen; i++) {
        if (i + 2 < blen && bstr[i + 1] == '-') {
            /* 范围：例如 a-z, 0-9 */
            if ((unsigned char)c >= (unsigned char)bstr[i] &&
                (unsigned char)c <= (unsigned char)bstr[i + 2]) {
                matched = 1;
                break;
            }
            i += 2;  /* 跳过范围表达式 */
        } else {
            if (c == bstr[i]) {
                matched = 1;
                break;
            }
        }
    }

    return negate ? !matched : matched;
}

/** 检查一条 NFA 出边是否匹配给定字符 */
static int edge_matches(NFAEdgeType type, char ch, EscapeSeq esc,
                         const char *bstr, size_t blen, char input_char) {
    switch (type) {
    case NFA_EDGE_CHAR:
        return ch == input_char;

    case NFA_EDGE_DOT:
        return 1;  /* 匹配任意字符 */

    case NFA_EDGE_ESCAPE:
        switch (esc) {
        case ESCAPE_DIGIT:
            return input_char >= '0' && input_char <= '9';
        case ESCAPE_NON_DIGIT:
            return !(input_char >= '0' && input_char <= '9');
        case ESCAPE_WORD: {
            char c = input_char;
            return (c >= 'a' && c <= 'z') ||
                   (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') ||
                   c == '_';
        }
        case ESCAPE_NON_WORD: {
            char c = input_char;
            return !((c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z') ||
                     (c >= '0' && c <= '9') ||
                     c == '_');
        }
        case ESCAPE_SPACE:
            return input_char == ' '  || input_char == '\t' ||
                   input_char == '\n' || input_char == '\r' ||
                   input_char == '\f' || input_char == '\v';
        case ESCAPE_NON_SPACE:
            return !(input_char == ' '  || input_char == '\t' ||
                     input_char == '\n' || input_char == '\r' ||
                     input_char == '\f' || input_char == '\v');
        default:
            return 0;
        }

    case NFA_EDGE_BRACKET:
        return bracket_matches(bstr, blen, input_char);

    default:
        return 0;
    }
}

/* ========================================================================== */
/*  内部辅助：布尔集合（BoolSet）                                               */
/* ========================================================================== */

typedef bool *BoolSet;

static BoolSet bool_set_new(int size) {
    return (BoolSet)calloc((size_t)size, sizeof(bool));
}

static void bool_set_free(BoolSet set) {
    free(set);
}

static bool bool_set_eq(BoolSet a, BoolSet b, int size) {
    return memcmp(a, b, (size_t)size * sizeof(bool)) == 0;
}

/* ========================================================================== */
/*  内部辅助：ε-闭包                                                           */
/* ========================================================================== */

/**
 * 对给定的 NFA 状态集合，计算其 ε-闭包（原地修改）。
 *
 * ε-闭包 = 从集合中任一状态出发，仅沿 ε 边可以到达的所有状态的并集。
 * 使用显式栈做 DFS，避免递归栈溢出。
 *
 * 一个 NFA 状态最多有两条 ε 出边（Thompson 构造保证），经过 BFS/DFS 处理后
 * 所有可达的 ε 路径均被遍历。
 *
 * 在图论上，ε-转移构成一个有向图，本函数计算从集合 S 出发的 ε-传递闭包。
 *
 * @param set        输入：起始状态集合；输出：起始集合 ∪ ε-可达集合
 * @param states     NFA 状态数组（按 id 索引）
 * @param nfa_count  NFA 状态总数
 */
static void epsilon_closure(BoolSet set, NFAState **states, int nfa_count) {
    /* 显式栈 — 最坏情况下所有状态都压入一次 */
    int *stack = (int *)malloc((size_t)nfa_count * sizeof(int));
    if (!stack) return;  /* 分配失败：保守处理，不扩展闭包 */
    int top = 0;

    /* 将所有当前在集合中的状态压入栈 */
    for (int i = 0; i < nfa_count; i++) {
        if (set[i]) stack[top++] = i;
    }

    while (top > 0) {
        int id = stack[--top];
        NFAState *s = states[id];

        /* 沿 edge1 的 ε 边转移 */
        if (s->edge1_type == NFA_EDGE_EPSILON && s->edge1_next) {
            int nid = s->edge1_next->id;
            if (!set[nid]) {
                set[nid] = true;
                stack[top++] = nid;
            }
        }

        /* 沿 edge2 的 ε 边转移 */
        if (s->edge2_type == NFA_EDGE_EPSILON && s->edge2_next) {
            int nid = s->edge2_next->id;
            if (!set[nid]) {
                set[nid] = true;
                stack[top++] = nid;
            }
        }
    }

    free(stack);
}

/* ========================================================================== */
/*  dfa_from_nfa — 子集构造法                                                  */
/* ========================================================================== */

DFAMachine dfa_from_nfa(const NFAGraph *nfa) {
    DFAMachine dfa = {0};

    if (!nfa || !nfa->start || !nfa->end || !nfa->states || nfa->state_count <= 0) {
        return dfa;
    }

    int nfa_count = nfa->state_count;

    /* ---- 动态数组 ---- */
    int dfa_cap = 16;
    DFAState *dfa_states = (DFAState *)malloc((size_t)dfa_cap * sizeof(DFAState));
    BoolSet  *dfa_sets   = (BoolSet  *)malloc((size_t)dfa_cap * sizeof(BoolSet));
    if (!dfa_states || !dfa_sets) {
        free(dfa_states);
        free(dfa_sets);
        return dfa;  /* dfa 已被初始化为 {0} */
    }
    int dfa_count = 0;

    /* ---- 构造起始 DFA 状态：ε-closure({nfa.start}) ---- */
    BoolSet start_set = bool_set_new(nfa_count);
    if (!start_set) {
        free(dfa_sets);
        free(dfa_states);
        return dfa;  /* dfa 已被初始化为 {0} */
    }
    start_set[nfa->start->id] = true;
    epsilon_closure(start_set, nfa->states, nfa_count);

    dfa_sets[0] = start_set;
    dfa_states[0].id = 0;
    dfa_states[0].transitions = (int *)malloc(256 * sizeof(int));
    if (!dfa_states[0].transitions) {
        bool_set_free(start_set);
        free(dfa_sets);
        free(dfa_states);
        return dfa;
    }
    for (int i = 0; i < 256; i++) {
        dfa_states[0].transitions[i] = -1;
    }
    dfa_states[0].is_accept = start_set[nfa->end->id] ? 1 : 0;
    dfa_count = 1;

    /* ---- 主循环：线性遍历 dfa_states（BFS，新状态追加到末尾） ---- */
    int cur = 0;
    while (cur < dfa_count) {
        BoolSet cur_set = dfa_sets[cur];

        for (int c = 0; c < 256; c++) {
            char ch = (char)c;

            /* move: 从 cur_set 中每个 NFA 状态沿匹配 ch 的边走一步 */
            BoolSet next_set = bool_set_new(nfa_count);
            if (!next_set) continue;  /* 分配失败：跳过此字符 */
            int has_any = 0;

            for (int i = 0; i < nfa_count; i++) {
                if (!cur_set[i]) continue;
                NFAState *s = nfa->states[i];

                /* 检查 edge1（非 ε 边）。
                 * 注意 edge1_next 和 edge2_next 是互斥的两条出边，
                 * 需要分别检查，不可遗漏其中一条。 */
                if (s->edge1_next && s->edge1_type != NFA_EDGE_EPSILON &&
                    edge_matches(s->edge1_type, s->edge1_char, s->edge1_esc,
                                 s->edge1_bracket.str, s->edge1_bracket.len, ch)) {
                    next_set[s->edge1_next->id] = true;
                    has_any = 1;
                }

                /* 检查 edge2（非 ε 边） */
                if (s->edge2_next && s->edge2_type != NFA_EDGE_EPSILON &&
                    edge_matches(s->edge2_type, s->edge2_char, s->edge2_esc,
                                 s->edge2_bracket.str, s->edge2_bracket.len, ch)) {
                    next_set[s->edge2_next->id] = true;
                    has_any = 1;
                }
            }

            if (!has_any) {
                bool_set_free(next_set);
                continue;
            }

            /* ε-closure 原地扩展 */
            epsilon_closure(next_set, nfa->states, nfa_count);

            /* 查找已存在的等价 DFA 状态（去重） */
            int target_id = -1;
            for (int i = 0; i < dfa_count; i++) {
                if (bool_set_eq(dfa_sets[i], next_set, nfa_count)) {
                    target_id = i;
                    break;
                }
            }

            if (target_id == -1) {
                /* 新 DFA 状态：扩容 + 追加 */
                if (dfa_count >= dfa_cap) {
                    dfa_cap *= 2;
                    DFAState *tmp_states = (DFAState *)realloc(
                        dfa_states, (size_t)dfa_cap * sizeof(DFAState));
                    if (!tmp_states) {
                        bool_set_free(next_set);
                        goto oom;
                    }
                    dfa_states = tmp_states;

                    BoolSet *tmp_sets = (BoolSet *)realloc(
                        dfa_sets, (size_t)dfa_cap * sizeof(BoolSet));
                    if (!tmp_sets) {
                        bool_set_free(next_set);
                        goto oom;
                    }
                    dfa_sets = tmp_sets;
                }

                target_id = dfa_count;
                dfa_sets[target_id] = next_set;  /* 移交所有权 */
                dfa_states[target_id].id = target_id;
                dfa_states[target_id].transitions = (int *)malloc(256 * sizeof(int));
                if (!dfa_states[target_id].transitions) {
                    /* next_set 所有权已移交，在 oom 中统一释放 */
                    goto oom;
                }
                for (int i = 0; i < 256; i++) {
                    dfa_states[target_id].transitions[i] = -1;
                }
                dfa_states[target_id].is_accept = next_set[nfa->end->id] ? 1 : 0;
                dfa_count++;
            } else {
                bool_set_free(next_set);
            }

            dfa_states[cur].transitions[c] = target_id;
        }

        cur++;
    }

    /* ---- 打包返回 ---- */
    dfa.states      = dfa_states;
    dfa.state_count = dfa_count;
    dfa.start_state = 0;

    /* ---- 释放临时集合 ---- */
oom:
    for (int i = 0; i < dfa_count; i++) {
        bool_set_free(dfa_sets[i]);
    }
    free(dfa_sets);

    return dfa;
}

/* ========================================================================== */
/*  dfa_free — 释放 DFA 机器                                                  */
/* ========================================================================== */

void dfa_free(DFAMachine *dfa) {
    if (!dfa || !dfa->states) return;

    for (int i = 0; i < dfa->state_count; i++) {
        free(dfa->states[i].transitions);
    }
    free(dfa->states);

    dfa->states      = NULL;
    dfa->state_count = 0;
    dfa->start_state = 0;
}
