#include "matcher.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ========================================================================== */
/*  DFA 匹配器实现                                                              */
/*                                                                            */
/*  核心思路：DFA 就是一个状态机 —— 从起始状态出发，                            */
/*  逐字符查 transitions[] 表，走到接受状态即匹配成功。                          */
/*  每个字符只读一次，永不回溯，时间复杂度 O(n)。                               */
/* ========================================================================== */

/* ========================================================================== */
/*  内部辅助：字符区间标签生成（文本/DOT 共用）                                 */
/* ========================================================================== */

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
/*  内部辅助：字符区间标签生成（文本/DOT 共用）                                 */
/* ========================================================================== */

/**
 * 将字符区间 [lo..hi] 转为人类可读标签。
 * 可打印 ASCII → 使用 'a'-'z' 格式
 * 控制字符 → 使用十六进制 0xNN
 * DOT 格式：可打印字符用原字符，不可打印用 0xNN
 */
static void format_range_label(FILE *fp, int lo, int hi, int is_dot) {
    if (is_dot) {
        if (lo == hi) {
            /* 单字符：可打印用原字符，不可打印用 0xNN */
            if (lo >= 32 && lo <= 126) {
                /* 转义 DOT 特殊字符 */
                if (lo == '"') fputs("\\\"", fp);
                else if (lo == '\\') fputs("\\\\", fp);
                else fputc(lo, fp);
            } else {
                char buf[8];
                snprintf(buf, sizeof(buf), "0x%02x", lo);
                fputs(buf, fp);
            }
        } else {
            /* 区间：两端都可打印 → 'a-z' 格式 */
            if (lo >= 32 && lo <= 126 && hi >= 32 && hi <= 126) {
                fputc(lo, fp);
                fputc('-', fp);
                fputc(hi, fp);
            } else {
                /* 跨可打印/不可打印边界 → 两端都用 0xNN 保持一致 */
                char buf_lo[8], buf_hi[8];
                snprintf(buf_lo, sizeof(buf_lo), "0x%02x", lo);
                snprintf(buf_hi, sizeof(buf_hi), "0x%02x", hi);
                fputs(buf_lo, fp);
                fputc('-', fp);
                fputs(buf_hi, fp);
            }
        }
        return;
    }

    /* 文本格式（dfa_dump） */
    if (lo == hi) {
        if (lo >= 32 && lo < 127)
            printf("'%c'", lo);
        else
            printf("0x%02x", lo);
    } else if (lo + 1 == hi) {
        if (lo >= 32 && lo < 127 && hi >= 32 && hi < 127)
            printf("'%c','%c'", lo, hi);
        else
            printf("0x%02x,0x%02x", lo, hi);
    } else {
        if (lo >= 32 && lo < 127 && hi >= 32 && hi < 127)
            printf("'%c'-'%c'", lo, hi);
        else
            printf("0x%02x-0x%02x", lo, hi);
    }
}

/**
 * 收集所有转移到 target 的字符为一个位掩码数组（256 bits）。
 * 返回分配的掩码指针（调用者 free），若 target 不存在则返回 NULL。
 */
static unsigned char *collect_target_bytes(const int *transitions, int target) {
    unsigned char *mask = calloc(32, 1);  /* 256 bits = 32 bytes */
    if (!mask) return NULL;
    for (int c = 0; c < 256; c++) {
        if (transitions[c] == target) {
            mask[c >> 3] |= (1 << (c & 7));
        }
    }
    return mask;
}

/**
 * 尝试将一组非语义化的转移识别为字符集合模式（如 [abc]、[^abc]、[a-z]）。
 * 返回语义化标签（静态缓冲区），无法识别则返回 NULL。
 *
 * 识别策略（启发式，按优先级从高到低）：
 *   1. 精确匹配常见范围（a-z、A-Z、0-9）
 *   2. 精确匹配小集合（≤ 5 个离散可打印字符 → [abcde]）
 *   3. 精确匹配小补集（≤ 5 个离散可打印字符排除 → [^abcde]）
 *   4. 连续区间 → 用 'a'-'z' 格式
 *   5. 多区间但 ≤ 3 个 → 尝试用补集描述
 *   6. 兜底：逗号分隔各区间
 */
static const char *complex_class_label(const int *transitions, int target) {
    static char buf[128];

    /* ---- 策略 1：精确匹配常见范围（a-z、A-Z、0-9） ---- */
    {
        /* a-z (0x61-0x7A) */
        {
            int match = 1;
            for (int c = 0; c < 256; c++) {
                int expected = ((c >= 0x61 && c <= 0x7A) ? target : -1);
                if (transitions[c] != expected) { match = 0; break; }
            }
            if (match) return "a-z";
        }
        /* A-Z (0x41-0x5A) */
        {
            int match = 1;
            for (int c = 0; c < 256; c++) {
                int expected = ((c >= 0x41 && c <= 0x5A) ? target : -1);
                if (transitions[c] != expected) { match = 0; break; }
            }
            if (match) return "A-Z";
        }
        /* 0-9 (0x30-0x39) */
        {
            int match = 1;
            for (int c = 0; c < 256; c++) {
                int expected = ((c >= 0x30 && c <= 0x39) ? target : -1);
                if (transitions[c] != expected) { match = 0; break; }
            }
            if (match) return "0-9";
        }
    }

    /* 收集目标字节位掩码 */
    unsigned char *mask = collect_target_bytes(transitions, target);
    if (!mask) return NULL;

    /* ---- 提取区间列表 ---- */
    int intervals[128];  /* 每两项为一对 [start, end] */
    int interval_count = 0;
    int in_range = 0;
    int range_start = 0;

    for (int c = 0; c < 256; c++) {
        int is_target = (mask[c >> 3] & (1 << (c & 7)));
        if (is_target && !in_range) {
            in_range = 1;
            range_start = c;
        } else if (!is_target && in_range) {
            intervals[interval_count * 2] = range_start;
            intervals[interval_count * 2 + 1] = c - 1;
            interval_count++;
            in_range = 0;
        }
    }
    if (in_range) {
        intervals[interval_count * 2] = range_start;
        intervals[interval_count * 2 + 1] = 255;
        interval_count++;
    }

    free(mask);

    if (interval_count == 0) return NULL;

    /* ---- 策略 2：小集合（≤ 5 个离散可打印字符）→ [abcde] ---- */
    if (interval_count <= 5) {
        int printable_only = 1;
        int char_count = 0;
        int pos = 0;
        for (int i = 0; i < interval_count && pos < 120; i++) {
            int lo = intervals[i * 2], hi = intervals[i * 2 + 1];
            if (lo < 32 || lo > 126 || hi < 32 || hi > 126) {
                printable_only = 0;
                break;
            }
            for (int c = lo; c <= hi && pos < 120; c++) {
                buf[pos++] = (char)c;
                char_count++;
            }
        }
        if (printable_only && char_count > 0 && char_count <= 5) {
            buf[pos] = '\0';
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%.*s", (int)pos, buf);
            snprintf(buf, sizeof(buf), "[%s]", tmp);
            return buf;
        }
    }

    /* ---- 策略 3：≤ 3 个区间 → 尝试补集描述 ---- */
    if (interval_count <= 3) {
        /* 检查是否所有非目标字节都是 -1（死状态）或可打印 */
        int all_non_target_is_dead_or_printable = 1;
        int excl_pos = 0;
        char excl_chars[16] = {0};

        for (int c = 0; c < 256; c++) {
            if (transitions[c] != target && transitions[c] != -1) {
                all_non_target_is_dead_or_printable = 0;
                break;
            }
            if (transitions[c] == target) continue;
            if (c >= 32 && c <= 126 && excl_pos < 15) {
                excl_chars[excl_pos++] = (char)c;
            }
        }

        if (all_non_target_is_dead_or_printable && excl_pos > 0 && excl_pos <= 5) {
            excl_chars[excl_pos] = '\0';
            snprintf(buf, sizeof(buf), "[^%s]", excl_chars);
            return buf;
        }
    }

    /* ---- 策略 4：单个连续区间 → 'a-z' 格式 ---- */
    if (interval_count == 1) {
        int lo = intervals[0], hi = intervals[1];
        if (lo >= 32 && lo <= 126 && hi >= 32 && hi <= 126) {
            snprintf(buf, sizeof(buf), "%c-%c", lo, hi);
            return buf;
        }
        /* 跨边界的单区间 → 用 hex 格式 */
        snprintf(buf, sizeof(buf), "0x%02x-0x%02x", lo, hi);
        return buf;
    }

    /* ---- 策略 4b：hex 字符类检测 [0-9A-Fa-f]（必须在通用策略之前） ---- */
    {
        /* 检查区间是否恰好是 hex 子集 */
        int has_09 = 0, has_AF = 0, has_af = 0;
        int other_intervals = 0;

        for (int i = 0; i < interval_count && i < 10; i++) {
            int lo = intervals[i * 2], hi = intervals[i * 2 + 1];
            if (lo < 32 || lo > 126 || hi < 32 || hi > 126) {
                other_intervals++;
                continue;
            }
            /* 检查是否是 hex 子集（精确匹配：0-9 必须是完整区间） */
            if (lo == 0x30 && hi == 0x39) {
                has_09 = 1;
            } else if (lo >= 0x41 && hi <= 0x46) {
                has_AF = 1;
            } else if (lo >= 0x61 && hi <= 0x66) {
                has_af = 1;
            } else {
                other_intervals++;
            }
        }

        if (other_intervals == 0 && (has_09 || has_AF || has_af)) {
            /* 是 hex 子集，输出紧凑形式 */
            int pos = 0;
            buf[pos++] = '[';
            int first_group = 1;

            if (has_09) {
                buf[pos++] = '0'; buf[pos++] = '-'; buf[pos++] = '9';
                first_group = 0;
            }
            if (has_AF) {
                if (!first_group) buf[pos++] = '-';
                buf[pos++] = 'A'; buf[pos++] = '-'; buf[pos++] = 'F';
                first_group = 0;
            }
            if (has_af) {
                if (!first_group) buf[pos++] = '-';
                buf[pos++] = 'a'; buf[pos++] = '-'; buf[pos++] = 'f';
                first_group = 0;
            }
            buf[pos++] = ']';
            buf[pos] = '\0';
            return buf;
        }
    }

    /* ---- 策略 4c：区间模式推断 — 尝试合并为标准字符类描述 ---- */
    {
        /* 检查区间是否为数字+大写字母+小写字母的某种组合 */
        int has_digits = 0, has_upper = 0, has_lower = 0;
        int other_intervals = 0;
        int digit_intervals = 0, upper_intervals = 0, lower_intervals = 0;
        /* 记录每个类别的总覆盖范围 */
        int digit_lo = 256, digit_hi = -1;
        int upper_lo = 256, upper_hi = -1;
        int lower_lo = 256, lower_hi = -1;

        for (int i = 0; i < interval_count && i < 10; i++) {
            int lo = intervals[i * 2], hi = intervals[i * 2 + 1];
            if (lo < 32 || lo > 126 || hi < 32 || hi > 126) {
                other_intervals++;
                continue;
            }
            /* 判断区间属于哪个类别 */
            int digit = (lo >= 0x30 && hi <= 0x39);
            int upper = (lo >= 0x41 && hi <= 0x5A);
            int lower = (lo >= 0x61 && hi <= 0x7A);
            if (digit) {
                has_digits = 1;
                digit_intervals++;
                if (lo < digit_lo) digit_lo = lo;
                if (hi > digit_hi) digit_hi = hi;
            } else if (upper) {
                has_upper = 1;
                upper_intervals++;
                if (lo < upper_lo) upper_lo = lo;
                if (hi > upper_hi) upper_hi = hi;
            } else if (lower) {
                has_lower = 1;
                lower_intervals++;
                if (lo < lower_lo) lower_lo = lo;
                if (hi > lower_hi) lower_hi = hi;
            } else {
                other_intervals++;
            }
        }

        if (other_intervals == 0 && interval_count <= 4) {
            /* 检查每个类别是否恰好填满完整范围且区间数量为 1 */
            int digits_complete = has_digits && digit_intervals == 1 &&
                (digit_lo == 0x30 && digit_hi == 0x39);
            int upper_complete = has_upper && upper_intervals == 1 &&
                (upper_lo == 0x41 && upper_hi == 0x5A);
            int lower_complete = has_lower && lower_intervals == 1 &&
                (lower_lo == 0x61 && lower_hi == 0x7A);

            /* 只有当所有区间恰好填满完整类别时才输出紧凑形式 */
            if (digits_complete && upper_complete && lower_complete) {
                /* 三者都有 → [0-9-A-Z-a-z] */
                buf[0] = '['; buf[1] = '0'; buf[2] = '-'; buf[3] = '9';
                buf[4] = '-'; buf[5] = 'A'; buf[6] = '-'; buf[7] = 'Z';
                buf[8] = '-'; buf[9] = 'a'; buf[10] = '-'; buf[11] = 'z';
                buf[12] = ']'; buf[13] = '\0';
                return buf;
            } else if (digits_complete && upper_complete) {
                /* 数字+大写 → [0-9-A-Z] */
                int pos = 0;
                buf[pos++] = '[';
                buf[pos++] = '0'; buf[pos++] = '-'; buf[pos++] = '9';
                buf[pos++] = '-';
                buf[pos++] = 'A'; buf[pos++] = '-'; buf[pos++] = 'Z';
                buf[pos++] = ']'; buf[pos] = '\0';
                return buf;
            } else if (digits_complete && lower_complete) {
                /* 数字+小写 → [0-9-a-z] */
                int pos = 0;
                buf[pos++] = '[';
                buf[pos++] = '0'; buf[pos++] = '-'; buf[pos++] = '9';
                buf[pos++] = '-';
                buf[pos++] = 'a'; buf[pos++] = '-'; buf[pos++] = 'z';
                buf[pos++] = ']'; buf[pos] = '\0';
                return buf;
            } else if (upper_complete && lower_complete) {
                /* 大写+小写 → [A-Z-a-z] */
                int pos = 0;
                buf[pos++] = '[';
                buf[pos++] = 'A'; buf[pos++] = '-'; buf[pos++] = 'Z';
                buf[pos++] = '-';
                buf[pos++] = 'a'; buf[pos++] = '-'; buf[pos++] = 'z';
                buf[pos++] = ']'; buf[pos] = '\0';
                return buf;
            }
            /* 不完全匹配 → 走策略 5 逗号分隔 */
        }
    }

    /* ---- 策略 5：多区间 → 逗号分隔 ---- */
    if (interval_count <= 10) {
        int pos = 0;
        for (int i = 0; i < interval_count && pos < 120; i++) {
            int lo = intervals[i * 2], hi = intervals[i * 2 + 1];
            if (i > 0) {
                if (pos < 127) buf[pos++] = ',';
            }
            if (lo == hi) {
                if (lo >= 32 && lo <= 126) {
                    buf[pos++] = (char)lo;
                } else {
                    pos += snprintf(buf + pos, 127 - pos, "0x%02x", lo);
                }
            } else if (lo >= 32 && lo <= 126 && hi >= 32 && hi <= 126) {
                buf[pos++] = (char)lo;
                buf[pos++] = '-';
                buf[pos++] = (char)hi;
            } else {
                pos += snprintf(buf + pos, 127 - pos, "0x%02x-0x%02x", lo, hi);
            }
        }
        buf[pos] = '\0';
        return buf;
    }

    /* ---- 兜底：无法语义化，返回 NULL ---- */
    return NULL;
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

            /* 先尝试语义化识别整个转移（如 .、\d、\w 等） */
            const char *semantic = semantic_range_label(targets, t);
            if (semantic) {
                if (!has_any) {
                    printf("  转移: ");
                    has_any = 1;
                } else {
                    printf("        ");
                }
                printf("%s->%d\n", semantic, t);
                continue;
            }

            /* 再尝试字符集合语义化（如 [abc]、[^abc]、a-z） */
            const char *complex = complex_class_label(targets, t);
            if (complex) {
                if (!has_any) {
                    printf("  转移: ");
                    has_any = 1;
                } else {
                    printf("        ");
                }
                printf("%s->%d\n", complex, t);
                continue;
            }

            /* 否则：收集所有转移到 t 的连续区间 */
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

                format_range_label(NULL, c2, hi, 0);
                printf("->%d\n", t);
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

            /* 再尝试字符集合语义化（如 [abc]、[^abc]、a-z） */
            const char *complex = complex_class_label(st->transitions, t);
            if (complex) {
                fprintf(fp, "    S%d -> S%d [label=\"%s\"];\n", s, t, complex);
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

                format_range_label(fp, c2, hi, 1);
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
