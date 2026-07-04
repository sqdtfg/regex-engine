#ifndef REGEX_DFA_H
#define REGEX_DFA_H

#include "nfa.h"

/* ========================================================================== */
/*  DFA 状态                                                                    */
/* ========================================================================== */

/**
 * 每个 DFA 状态对应 NFA 的一个状态集合（子集）。
 * transitions[] 覆盖 256 个字节值，-1 表示该输入没有转移。
 */
typedef struct DFAState {
    int id;                     /* 状态编号 */
    int *transitions;           /* transitions[256]，每个指向下一个 DFA 状态 id，-1 = 无转移 */
    int is_accept;              /* 是否为接受状态 */
} DFAState;

/* ========================================================================== */
/*  DFA 机器                                                                    */
/* ========================================================================== */

typedef struct {
    DFAState *states;           /* 动态数组 */
    int state_count;            /* 状态总数 */
    int start_state;            /* 起始状态 id */
} DFAMachine;

/* ========================================================================== */
/*  匹配结果                                                                    */
/* ========================================================================== */

typedef struct {
    int matched;                /* 0 = 未匹配, 1 = 匹配成功 */
    size_t start;               /* 匹配的起始位置（在输入中的偏移） */
    size_t end;                 /* 匹配的结束位置（不包含） */
    size_t length;              /* 匹配长度 = end - start */
} MatchResult;

/* ========================================================================== */
/*  API                                                                        */
/* ========================================================================== */

/**
 * 子集构造法：从 NFA 构建 DFA。
 * @param nfa     完整的 NFA 图
 * @return        确定化的 DFA 机器。调用者负责调用 dfa_free() 释放。
 */
DFAMachine dfa_from_nfa(const NFAGraph *nfa);

/** 释放 DFA 机器 */
void dfa_free(DFAMachine *dfa);

/**
 * Hopcroft DFA 最小化。
 * 将 DFA 压缩到最小等价形式（合并等价状态），原地修改原 DFA 机器。
 * 算法复杂度 O(k n log n)，其中 k = 256（字母表大小），n = 状态数。
 *
 * @param dfa  待最小化的 DFA 机器（将原地替换 states 数组）
 */
void dfa_minimize(DFAMachine *dfa);

/**
 * 用 DFA 在输入文本上执行匹配。
 * @param dfa     DFA 机器
 * @param input   待匹配的输入字符串
 * @return        匹配结果（matched=1 时 start/end/length 有效）
 */
MatchResult dfa_match(const DFAMachine *dfa, const char *input);

/** 打印 DFA 的状态转移表（调试用） */
void dfa_dump(const DFAMachine *dfa);

#endif /* REGEX_DFA_H */
