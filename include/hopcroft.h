#ifndef REGEX_HOPCROFT_H
#define REGEX_HOPCROFT_H

#include "dfa.h"

/**
 * Hopcroft DFA 最小化。
 * 将 DFA 压缩到最小等价形式（合并等价状态），原地修改原 DFA 机器。
 * 算法复杂度 O(k n log n)，其中 k = 256（字母表大小），n = 状态数。
 *
 * @param dfa  待最小化的 DFA 机器（将原地替换 states 数组）
 */
void dfa_minimize(DFAMachine *dfa);

#endif /* REGEX_HOPCROFT_H */
