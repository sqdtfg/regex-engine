#ifndef REGEX_DFA_H
#define REGEX_DFA_H

#include <stdio.h>
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
    int has_anchor_start;       /* 模式以 ^ 开头（仅全串/行首匹配） */
    int has_anchor_end;         /* 模式以 $ 结尾（仅全串/行尾匹配） */
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
 */
DFAMachine dfa_from_nfa(const NFAGraph *nfa);

/** 释放 DFA 机器占用的所有内存（幂等） */
void dfa_free(DFAMachine *dfa);

/* ---- 匹配操作 ---- */

/** DFA 精确匹配：整个输入字符串 */
MatchResult dfa_match_full(const DFAMachine *dfa, const char *input);

/** DFA 子串匹配：查找输入中第一个匹配的子串（最左最长，POSIX 语义） */
MatchResult dfa_match(const DFAMachine *dfa, const char *input);

/** DFA 全局匹配：查找输入中所有匹配的子串 */
int dfa_match_all(const DFAMachine *dfa, const char *input,
                  MatchResult *results, int max_results);

/* ---- 可视化 / 调试 ---- */

/** 打印 DFA 状态转移表到 stdout（调试用） */
void dfa_dump(const DFAMachine *dfa);

/** 输出 DFA 为 Graphviz DOT 格式（可视化调试用） */
void dfa_dump_dot(const DFAMachine *dfa, FILE *fp);

/** 输出 DFA 为 Graphviz DOT 文件 */
int dfa_dump_dot_file(const DFAMachine *dfa, const char *filepath);

#endif /* REGEX_DFA_H */
