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

/* ========================================================================== */
/*  dfa_dump_dot — Graphviz DOT 输出                                           */
/* ========================================================================== */

/** 将字符转为 DOT 安全的标签文本 */
static const char *char_dot_label(int c) {
    static char buf[8];
    if (c >= 32 && c <= 126) {
        if (c == '"')  return "\\\"";
        if (c == '\\') return "\\\\";
        snprintf(buf, sizeof(buf), "%c", c);
        return buf;
    }
    /* 特殊空白字符 */
    switch (c) {
    case '\n': return "\\\\n";
    case '\r': return "\\\\r";
    case '\t': return "\\\\t";
    case ' ':  return "SP";
    }
    /* 其他控制字符：十六进制 */
    snprintf(buf, sizeof(buf), "0x%02x", c);
    return buf;
}

/**
 * 将 DFA 的状态转移表以 Graphviz DOT 格式输出到指定文件。
 *
 * 输出规约：
 * - 有向图 rankdir=LR（从左到右）
 * - 接受状态：双圈 (shape=doublecircle)，普通状态：圆圈 (shape=circle)
 * - 起始状态由不可见节点 (shape=point) 的边标记
 * - 边标签合并连续字符为区间（如 "a-c"、"0-9"），避免 256 条独立边爆炸
 * - -1 转移不画边（隐式死状态）
 *
 * 用法：将输出粘贴到 https://viz-js.com 或 VS Code Graphviz 扩展可直接查看。
 *
 * @param dfa  DFA 机器
 * @param fp   输出文件（可为 stdout）
 */
void dfa_dump_dot(const DFAMachine *dfa, FILE *fp) {
    if (!dfa || !dfa->states) {
        fprintf(fp, "// (null DFA)\n");
        return;
    }

    fprintf(fp, "digraph DFA {\n");
    fprintf(fp, "    rankdir=LR;\n");
    fprintf(fp, "    node [shape=circle];\n\n");

    /* 不可见起始节点 */
    fprintf(fp, "    start [shape=point];\n");

    /* 输出每个状态 */
    for (int i = 0; i < dfa->state_count; i++) {
        const DFAState *s = &dfa->states[i];
        const char *shape = s->is_accept ? "doublecircle" : "circle";
        fprintf(fp, "    S%d [shape=%s, label=\"%d\"];\n",
                s->id, shape, s->id);
    }

    fprintf(fp, "\n    /* start edge */\n");
    fprintf(fp, "    start -> S%d;\n", dfa->start_state);

    /* 对每个状态，按目标状态分组并合并连续字符区间 */
    fprintf(fp, "\n    /* transitions (character ranges merged) */\n");
    for (int s = 0; s < dfa->state_count; s++) {
        const DFAState *st = &dfa->states[s];

        /* 记录每个字符的目标 state */
        int targets[256];
        for (int c = 0; c < 256; c++) {
            targets[c] = st->transitions[c];
        }

        /* 对每个曾经出现的目标 state 输出边 */
        int done[256] = {0};
        for (int c = 0; c < 256; c++) {
            int t = targets[c];
            if (t == -1 || done[c]) continue;

            /* 找到连续区间 [c, hi] */
            int hi = c;
            while (hi + 1 < 256 && targets[hi + 1] == t && !done[hi + 1]) {
                hi++;
            }

            /* 标记已处理 */
            for (int j = c; j <= hi; j++) done[j] = 1;

            /* 生成标签 */
            if (c == hi) {
                /* 单字符 */
                fprintf(fp, "    S%d -> S%d [label=\"%s\"];\n",
                        s, t, char_dot_label(c));
            } else if (c + 1 == hi) {
                /* 两个字符，用逗号连 */
                fprintf(fp, "    S%d -> S%d [label=\"%s,%s\"];\n",
                        s, t, char_dot_label(c), char_dot_label(hi));
            } else {
                /* 三个及以上连续字符，用区间 */
                fprintf(fp, "    S%d -> S%d [label=\"%s-%s\"];\n",
                        s, t, char_dot_label(c), char_dot_label(hi));
            }
        }
    }

    fprintf(fp, "}\n");
}

int dfa_dump_dot_file(const DFAMachine *dfa, const char *filepath) {
    FILE *fp = fopen(filepath, "w");
    if (!fp) return -1;
    dfa_dump_dot(dfa, fp);
    fclose(fp);
    return 0;
}
