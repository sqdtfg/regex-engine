#include "matcher.h"
#include <stdio.h>
#include <string.h>

/* ========================================================================== */
/*  DFA 匹配器实现                                                              */
/*                                                                            */
/*  核心思路：DFA 就是一个状态机 —— 从起始状态出发，                            */
/*  逐字符查 transitions[] 表，走到接受状态即匹配成功。                          */
/*  每个字符只读一次，永不回溯，时间复杂度 O(n)。                               */
/* ========================================================================== */

/* -------------------------------------------------------------------------- */
/*  内部辅助：将 char 映射为 0..255 的索引                                      */
/* -------------------------------------------------------------------------- */

static inline int char_to_index(char c) {
    return (unsigned char)c;
}

/* ========================================================================== */
/*  dfa_match_full — 精确匹配整个输入字符串                                      */
/* ========================================================================== */

MatchResult dfa_match_full(const DFAMachine *dfa, const char *input) {
    MatchResult result = {0};

    if (!dfa || !input) {
        return result;
    }

    int current_state = dfa->start_state;
    const char *p = input;

    while (*p != '\0') {
        int idx = char_to_index(*p);
        int next_state = dfa->states[current_state].transitions[idx];
        if (next_state == -1) {
            break;
        }
        current_state = next_state;
        p++;
    }

    result.start = 0;
    result.end = (size_t)(p - input);
    result.matched = dfa->states[current_state].is_accept && *p == '\0';
    if (result.matched) {
        result.length = result.end;
    }

    return result;
}

/* ========================================================================== */
/*  dfa_match — 匹配输入中的第一个子串                                              */
/* ========================================================================== */

MatchResult dfa_match(const DFAMachine *dfa, const char *input) {
    MatchResult result = {0};

    if (!dfa || !input) {
        return result;
    }

    size_t input_len = strlen(input);

    for (size_t start = 0; start <= input_len; start++) {
        int current_state = dfa->start_state;
        size_t pos = start;

        while (pos <= input_len) {
            if (dfa->states[current_state].is_accept) {
                result.matched = 1;
                result.start = start;
                result.end = pos;
                result.length = pos - start;
                return result;
            }

            if (pos == input_len) {
                break;
            }

            int idx = char_to_index(input[pos]);
            int next_state = dfa->states[current_state].transitions[idx];
            if (next_state == -1) {
                break;
            }

            current_state = next_state;
            pos++;
        }
    }

    return result;
}

/* ========================================================================== */
/*  dfa_match_all — 查找所有匹配项（最多匹配                                      */
/* ========================================================================== */

int dfa_match_all(const DFAMachine *dfa, const char *input, MatchResult *results, int max_results) {
    int count = 0;

    if (!dfa || !input || !results || max_results <= 0) {
        return 0;
    }

    size_t input_len = strlen(input);
    size_t pos = 0;

    while (pos <= input_len && count < max_results) {
        int current_state = dfa->start_state;
        size_t best_end = 0;
        int found = 0;
        size_t i;

        for (i = pos; i <= input_len; i++) {
            if (dfa->states[current_state].is_accept) {
                best_end = i;
                found = 1;
            }

            if (i == input_len) {
                break;
            }

            int idx = char_to_index(input[i]);
            int next_state = dfa->states[current_state].transitions[idx];
            if (next_state == -1) {
                break;
            }

            current_state = next_state;
        }

        if (found) {
            results[count].matched = 1;
            results[count].start = pos;
            results[count].end = best_end;
            results[count].length = best_end - pos;
            count++;

            pos = (best_end > pos) ? best_end : pos + 1;
        } else {
            pos++;
        }
    }

    return count;
}

/* ========================================================================== */
/*  dfa_dump — 打印 DFA 状态转移表（调试用）                                   */
/* ========================================================================== */

void dfa_dump(const DFAMachine *dfa) {
    if (!dfa) {
        printf("(null DFA)\n");
        return;
    }

    printf("========== DFA 状态转移表 ==========\n");
    printf("起始状态: %d\n", dfa->start_state);
    printf("状态总数: %d\n", dfa->state_count);
    printf("------------------------------------\n");

    for (int i = 0; i < dfa->state_count; i++) {
        const DFAState *state = &dfa->states[i];
        printf("状态 %d %s\n",
               state->id,
               state->is_accept ? "(接受)" : "");

        int has_any = 0;
        for (int c = 0; c < 256; c++) {
            if (state->transitions[c] != -1) {
                if (!has_any) {
                    printf("  转移: ");
                    has_any = 1;
                }
                printf("[%c(0x%02x)->%d] ",
                       (c >= 32 && c < 127) ? c : '.',
                       c,
                       state->transitions[c]);
            }
        }
        if (has_any) {
            printf("\n");
        } else {
            printf("  (无转移)\n");
        }
    }

    printf("====================================\n");
}
