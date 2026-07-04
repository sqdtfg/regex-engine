#ifndef REGEX_MATCHER_H
#define REGEX_MATCHER_H

#include <stdio.h>
#include "dfa.h"

/* ========================================================================== */
/*  公共 API                                                                  */
/* ========================================================================== */

/**
 * DFA 精确匹配：从起始状态出发，逐个字符沿转移表行走。
 * 如果所有字符都消耗完后停在接受状态，返回匹配成功。
 *
 * @param dfa   编译好的 DFA 机器（不可变）
 * @param input 待匹配的输入字符串
 * @return      匹配结果（matched=1 表示整个输入匹配成功）
 */
MatchResult dfa_match_full(const DFAMachine *dfa, const char *input);

/**
 * DFA 子串匹配：查找输入中第一个匹配的子串。
 *
 * @param dfa   编译好的 DFA 机器（不可变）
 * @param input 待匹配的输入字符串
 * @return      匹配结果（matched=1 表示找到匹配）
 */
MatchResult dfa_match(const DFAMachine *dfa, const char *input);

/**
 * DFA 全局匹配：查找输入中所有匹配的子串。
 *
 * @param dfa         编译好的 DFA 机器（不可变）
 * @param input       待匹配的输入字符串
 * @param results     匹配结果数组（输出）
 * @param max_results 结果数组的最大容量
 * @return            实际匹配到的数量
 */
int dfa_match_all(const DFAMachine *dfa, const char *input, MatchResult *results, int max_results);

/**
 * 将 DFA 的状态转移表以人类可读的方式打印到 stdout。
 * 仅打印有意义的转移（next_id != -1），便于理解 DFA 结构。
 *
 * @param dfa  DFA 机器（可为 NULL）
 */
void dfa_dump(const DFAMachine *dfa);

/**
 * 将 DFA 的状态转移表输出为 Graphviz DOT 格式（可视化调试用）。
 *
 * 输出规约：
 * - 有向图 digraph DFA { rankdir=LR; ... }
 * - 接受状态：双圈 (shape=doublecircle)，普通状态：圆圈 (shape=circle)
 * - 起始状态由不可见节点 (shape=point) 的边标记
 * - 边标签合并连续字符为区间，避免 256 条边输出爆炸
 * - -1 转移不画边
 *
 * @param dfa  DFA 机器（可为 NULL）
 * @param fp   输出文件（可为 stdout）
 */
void dfa_dump_dot(const DFAMachine *dfa, FILE *fp);

#endif /* REGEX_MATCHER_H */
