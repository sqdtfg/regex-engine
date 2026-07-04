#ifndef REGEX_DFA_H
#define REGEX_DFA_H

#include "nfa.h"

/* ========================================================================== */
/*  DFA 状态                                                                    */
/* ========================================================================== */

/**
 * DFA 状态节点。
 *
 * 每个 DFA 状态对应 NFA 的一个状态集合（子集构造法的产物）。
 * transitions[] 覆盖 256 个单字节输入值，-1 表示该输入无转移。
 *
 * 设计说明：
 * - 每个状态持有一张完整的 256 入口转移表（查表 O(1)），
 *   这是 DFA 匹配 O(n) 的保证（n = 输入长度）。
 * - 构造阶段沿 BFS 顺序将新状态追加到 flat 动态数组，
 *   产出的 DFAMachine 可直接用于匹配，无需额外构建步骤。
 * - 字母表固定在单字节（0..255），不直接支持 Unicode；
 *   UTF-8 输入按字节拆开匹配，对 ASCII 模式仍保持正确语义。
 */
typedef struct DFAState {
    int id;                     /* 状态编号（构造阶段等于数组索引） */
    int *transitions;           /* transitions[256]，-1 = 无转移 */
    int is_accept;              /* 是否为接受状态 */
} DFAState;

/* ========================================================================== */
/*  DFA 机器（编译结果）                                                        */
/* ========================================================================== */

typedef struct {
    DFAState *states;           /* 动态状态数组 */
    int state_count;            /* 状态总数 */
    int start_state;            /* 起始状态 id */
} DFAMachine;

/* ========================================================================== */
/*  匹配结果                                                                    */
/* ========================================================================== */

typedef struct {
    int matched;                /* 0 = 未匹配, 1 = 匹配成功 */
    size_t start;               /* 匹配起始位置（输入中的偏移） */
    size_t end;                 /* 匹配结束位置（不包含） */
    size_t length;              /* 匹配长度 = end - start */
} MatchResult;

/* ========================================================================== */
/*  API                                                                        */
/* ========================================================================== */

/**
 * 子集构造法：从 NFA 构建 DFA。
 *
 * 这是 NFA→DFA 转换的核心入口，实现编译原理中经典的
 * 子集构造算法（Subset Construction Algorithm）。
 *
 * 算法步骤：
 *   1. ε-closure({nfa.start}) → DFA 状态 0
 *   2. BFS 遍历所有 DFA 状态，对当前状态 S 和每个字符 c∈[0,255]：
 *      a. move(S, c)  → 沿匹配 c 的 NFA 边走一步
 *      b. ε-closure(move(S, c)) → 目标 DFA 状态
 *      c. 若目标状态是新的，追加到 DFA 状态数组末尾
 *      d. 记录转移 S --c--> target
 *   3. 若 DFA 状态中包含 NFA 的 accept 状态，则该 DFA 状态也是 accept
 *
 * 实现细节：
 * - 新状态总是追加到数组末尾，线性遍历即可完成 BFS，无需额外队列。
 * - 状态去重通过比较底层 BoolSet（memcmp）完成，正确性依赖于
 *   ε-closure 的确定性（同一个输入集合总是展开成同一个闭包）。
 *
 * @param nfa  完整的 NFA 图（Thompson 构造产物，允许 ε 边）
 * @return     确定化的 DFA 机器（调用者负责调用 dfa_free() 释放）
 */
DFAMachine dfa_from_nfa(const NFAGraph *nfa);

/**
 * 释放 DFA 机器占用的所有内存。
 * 包括每个状态的 transitions 数组以及 states 数组本身。
 * 重复释放同一个对象是安全的（幂等操作）。
 */
void dfa_free(DFAMachine *dfa);

/**
 * 用 DFA 在输入文本上执行子串匹配（第一个匹配）。
 *
 * 从输入起始位置出发，沿转移表逐字符前进。
 * 一旦到达接受状态即返回（最短匹配语义）。
 * 若当前位置无匹配则从下一位置重试。
 *
 * @param dfa     DFA 机器
 * @param input   待匹配的输入字符串（以 \0 结尾）
 * @return        匹配结果（matched=1 时 start/end/length 有效）
 */
MatchResult dfa_match(const DFAMachine *dfa, const char *input);

/**
 * 打印 DFA 的状态转移表（调试用）。
 * 仅输出有意义的转移（next_id != -1），可打印字符直接显示，
 * 不可打印字符以十六进制（0x%02x）表示。
 * 接受 NULL 参数（输出 "(null DFA)"）。
 */
void dfa_dump(const DFAMachine *dfa);

#endif /* REGEX_DFA_H */
