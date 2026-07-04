#ifndef REGEX_DFA_H
#define REGEX_DFA_H

#include "nfa.h"

/* ========================================================================== */
/*  DFA 状态                                                                    */
/* ========================================================================== */

/**
 * 每个 DFA 状态对应 NFA 的一个状态集合（子集）。
 * transitions[] 覆盖 256 个字节值，-1 表示该输入没有转移。
 *
 * 设计说明：
 * - 每个 DFA 状态持有一张完整的 256 入口转移表（查表 O(1)），
 *   这是 DFA 匹配 O(n) 的保证（n = 输入长度）。
 * - 存储上用 flat 动态数组而非链表，构造阶段按 BFS 顺序增长，
 *   最终产出的 DFAMachine 可直接用于匹配，无需额外步骤。
 * - 字母表固定在单字节（0..255），不直接支持 Unicode；
 *   UTF-8 输入会按字节拆开匹配，对大部分 ASCII 模式仍有正确语义。
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
 *
 * 这是 NFA→DFA 转换的核心入口，实现了编译原理中经典的
 * 子集构造算法（Subset Construction Algorithm）。
 *
 * 算法步骤：
 *   1. 计算 ε-closure({nfa.start}) → DFA 状态 0
 *   2. BFS 遍历所有 DFA 状态，对当前状态 S 和每个字符 c∈[0,255]：
 *      a. move(S, c)  → 从 S 中任意 NFA 状态沿匹配 c 的边走一步
 *      b. ε-closure(move(S, c)) → 目标 DFA 状态
 *      c. 若目标状态是新的，追加到 DFA 状态数组末尾
 *      d. 记录 S --c--> target
 *   3. 若 DFA 状态中包含 NFA 的 accept 状态，则该 DFA 状态也是 accept
 *
 * 实现细节：
 * - 新状态总是追加到 dfa_states[] 末尾，线性遍历即可完成 BFS，
 *   无需单独的队列数据结构。
 * - DFA 状态去重通过比较底层 BoolSet（memcmp）完成，正确性依赖于
 *   ε-closure 的确定性（同一个输入集合总是展开成同一个闭包）。
 *
 * @param nfa     完整的 NFA 图（Thompson 构造产物，允许 ε 边）
 * @return        确定化的 DFA 机器。调用者负责调用 dfa_free() 释放。
 */
DFAMachine dfa_from_nfa(const NFAGraph *nfa);

/** 释放 DFA 机器（states 数组及每个状态的 transitions 数组） */
void dfa_free(DFAMachine *dfa);

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
