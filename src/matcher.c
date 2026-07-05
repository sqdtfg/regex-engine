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

/* ========================================================================== */
/*  内部辅助：DOT 标签生成                                                     */
/* ========================================================================== */

/**
 * 将单个字符转为 DOT 标签安全的文本。
 * 双缓冲区支持同语句内多次调用。
 *
 * 处理规则：
 * - 控制字符 → 转义序列 (\n, \r, \t)
 * - 可打印 ASCII → 原字符（自动转义 \ 和 "）
 * - 扩展 ASCII → 十六进制 0xx
 */
static const char *char_dot_label(int c) {
    static char buf[2][16];
    static int  idx = 0;
    char *b = buf[idx];
    idx = (idx + 1) % 2;

    switch (c) {
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    case ' ':  return "SP";
    case '\\': return "\\\\";
    case '"':  return "\\\"";
    }

    if (c >= 32 && c <= 126) {
        b[0] = (char)c;
        b[1] = '\0';
        return b;
    }

    snprintf(b, 16, "0x%02x", (unsigned char)c);
    return b;
}

/**
 * 判断一组字符转移是否匹配某个语义类别（转义序列）。
 * 返回类别名（静态缓冲区），若不属于任何已知类别则返回 NULL。
 *
 * 支持的类别：
 *   .   → 所有 256 个字节
 *   \d  → 0-9
 *   \D  → 非 0-9
 *   \w  → a-zA-Z0-9_
 *   \W  → 非 \w
 *   \s  → 空白字符（空格、\t、\n、\r、\f、\v）
 *   \S  → 非 \s
 *
 * 此函数用于在 DOT 输出中将原始字节区间还原为语义化标签，
 * 使可视化结果更易读。
 */
static const char *semantic_range_label(const int *transitions, int target) {
    /* ---- . （DOT）：所有 256 个字节都转移到同一目标 ---- */
    {
        int all_same = 1;
        for (int c = 0; c < 256; c++) {
            if (transitions[c] != target) { all_same = 0; break; }
        }
        if (all_same) return ".";
    }

    /* ---- \d ：0x30-0x39（'0'-'9'） ---- */
    {
        int match = 1;
        for (int c = 0; c < 256; c++) {
            int expected = ((c >= 0x30 && c <= 0x39) ? target : -1);
            if (transitions[c] != expected) { match = 0; break; }
        }
        if (match) return "\\d";
    }

    /* ---- \D ：非 0x30-0x39 转移到 target，0x30-0x39 转移到其他 ---- */
    {
        int match = 1;
        int other_target = -2;  /* -2 = 尚未见过其他目标 */
        for (int c = 0; c < 256; c++) {
            if (c >= 0x30 && c <= 0x39) {
                if (transitions[c] == target) { match = 0; break; }
                if (other_target == -2) other_target = transitions[c];
                else if (transitions[c] != other_target) { match = 0; break; }
            } else {
                if (transitions[c] != target) { match = 0; break; }
            }
        }
        if (match && other_target != -2) return "\\D";
    }

    /* ---- \w ：a-z A-Z 0-9 _ ---- */
    {
        int match = 1;
        for (int c = 0; c < 256; c++) {
            int is_word = ((c >= 0x61 && c <= 0x7A) ||
                           (c >= 0x41 && c <= 0x5A) ||
                           (c >= 0x30 && c <= 0x39) ||
                           (c == 0x5F));
            int expected = (is_word ? target : -1);
            if (transitions[c] != expected) { match = 0; break; }
        }
        if (match) return "\\w";
    }

    /* ---- \W ：非 \w ---- */
    {
        int match = 1;
        int other_target = -2;  /* -2 = 尚未见过其他目标 */
        for (int c = 0; c < 256; c++) {
            int is_word = ((c >= 0x61 && c <= 0x7A) ||
                           (c >= 0x41 && c <= 0x5A) ||
                           (c >= 0x30 && c <= 0x39) ||
                           (c == 0x5F));
            if (is_word) {
                if (transitions[c] == target) { match = 0; break; }
                if (other_target == -2) other_target = transitions[c];
                else if (transitions[c] != other_target) { match = 0; break; }
            } else {
                if (transitions[c] != target) { match = 0; break; }
            }
        }
        if (match && other_target != -2) return "\\W";
    }

    /* ---- \s ：空格(0x20) \t(0x09) \n(0x0A) \r(0x0D) \f(0x0C) \v(0x0B) ---- */
    {
        int match = 1;
        for (int c = 0; c < 256; c++) {
            int is_space = (c == 0x20 || c == 0x09 || c == 0x0A ||
                            c == 0x0D || c == 0x0C || c == 0x0B);
            int expected = (is_space ? target : -1);
            if (transitions[c] != expected) { match = 0; break; }
        }
        if (match) return "\\s";
    }

    /* ---- \S ：非 \s ---- */
    {
        int match = 1;
        int other_target = -2;  /* -2 = 尚未见过其他目标 */
        for (int c = 0; c < 256; c++) {
            int is_space = (c == 0x20 || c == 0x09 || c == 0x0A ||
                            c == 0x0D || c == 0x0C || c == 0x0B);
            if (is_space) {
                if (transitions[c] == target) { match = 0; break; }
                if (other_target == -2) other_target = transitions[c];
                else if (transitions[c] != other_target) { match = 0; break; }
            } else {
                if (transitions[c] != target) { match = 0; break; }
            }
        }
        if (match && other_target != -2) return "\\S";
    }

    return NULL;
}

/* ========================================================================== */
/*  内部辅助：将 char 映射为 0..255 的索引                                      */
/* ========================================================================== */

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

        /* 按目标状态分组后合并连续字符区间输出。
         * 同一个 (source, target) 对的所有区间只打印一行。 */
        int targets[256];
        for (int c = 0; c < 256; c++) {
            targets[c] = state->transitions[c];
        }

        int t_done[256] = {0};
        int has_any = 0;
        for (int c = 0; c < 256; c++) {
            int t = targets[c];
            if (t == -1 || t_done[t]) continue;
            t_done[t] = 1;

            /* 收集所有转移到 t 的连续区间 */
            int done_char[256] = {0};
            for (int c2 = 0; c2 < 256; c2++) {
                if (targets[c2] != t || done_char[c2]) continue;

                int hi = c2;
                while (hi + 1 < 256 && targets[hi + 1] == t && !done_char[hi + 1]) {
                    hi++;
                }
                for (int j = c2; j <= hi; j++) done_char[j] = 1;

                if (!has_any) {
                    printf("  转移: ");
                    has_any = 1;
                } else {
                    printf("        ");
                }

                if (c2 == hi) {
                    /* 单字符 */
                    if (c2 >= 32 && c2 < 127)
                        printf("'%c'->%d\n", c2, t);
                    else
                        printf("0x%02x->%d\n", c2, t);
                } else if (c2 + 1 == hi) {
                    /* 两个字符 */
                    if (c2 >= 32 && c2 < 127 && hi >= 32 && hi < 127)
                        printf("'%c','%c'->%d\n", c2, hi, t);
                    else
                        printf("0x%02x,0x%02x->%d\n", c2, hi, t);
                } else {
                    /* 区间 */
                    if (c2 >= 32 && c2 < 127 && hi >= 32 && hi < 127)
                        printf("'%c'-'%c'->%d\n", c2, hi, t);
                    else
                        printf("0x%02x-0x%02x->%d\n", c2, hi, t);
                }
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

/**
 * 将 DFA 的状态转移表以 Graphviz DOT 格式输出到指定文件。
 *
 * 输出规约：
 * - 有向图 rankdir=LR（从左到右）
 * - 接受状态：双圈 (shape=doublecircle)，普通状态：圆圈 (shape=circle)
 * - 起始状态由不可见节点 (shape=point) 的边标记
 * - 边标签合并连续字符为区间（如 "a-c"、"0-9"），避免 256 条独立边爆炸
 * - -1 转移不画边（隐式死状态）
 * - 语义化标签：.（任意字符）、\d/\D/\w/\W/\s/\S 等自动识别
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

    /* 对每个状态，按目标状态分组后合并连续字符区间。
     * 同一个 (source, target) 对只出一条边，标签尝试语义化识别。 */
    fprintf(fp, "\n    /* transitions (character ranges merged per target) */\n");
    for (int s = 0; s < dfa->state_count; s++) {
        const DFAState *st = &dfa->states[s];

        /* 收集每个目标状态的所有字符（比特集） */
        int done_targets[256] = {0};
        for (int c = 0; c < 256; c++) {
            int t = st->transitions[c];
            if (t == -1 || done_targets[t]) continue;
            done_targets[t] = 1;

            /* 先尝试语义化识别整个转移（如 .、\d、\w 等） */
            const char *semantic = semantic_range_label(st->transitions, t);
            if (semantic) {
                fprintf(fp, "    S%d -> S%d [label=\"%s\"];\n", s, t, semantic);
                continue;
            }

            /* 否则：从 [0,255] 中挑出所有转移到 t 的字符，合并为连续区间 */
            int done_char[256] = {0};
            int first = 1;

            fprintf(fp, "    S%d -> S%d [label=\"", s, t);

            for (int c2 = 0; c2 < 256; c2++) {
                if (st->transitions[c2] != t || done_char[c2]) continue;

                int hi = c2;
                while (hi + 1 < 256 && st->transitions[hi + 1] == t && !done_char[hi + 1]) {
                    hi++;
                }
                for (int j = c2; j <= hi; j++) done_char[j] = 1;

                if (!first) fputc(',', fp);
                first = 0;

                const char *cl = char_dot_label(c2);
                const char *ch = char_dot_label(hi);

                if (c2 == hi) {
                    fputs(cl, fp);
                } else {
                    fprintf(fp, "%s-%s", cl, ch);
                }
            }

            fprintf(fp, "\"];\n");
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
