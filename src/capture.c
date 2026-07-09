#include "capture.h"
#include "nfa.h"
#include "parser.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/*  设计概要                                                                      */
/*                                                                            */
/*  捕获组追踪的核心思路：                                                       */
/*                                                                            */
/*  1. dfa_from_ast_with_groups(ast_root):                                      */
/*     - 遍历 AST 统计 AST_GROUP 节点数量，为每组分配编号（从 1 开始）。          */
/*     - 调用 nfa_from_ast(ast_root) 构建完整 NFA。                              */
/*     - 调用 dfa_from_nfa(nfa) 构建 DFA。                                       */
/*     - 分配 CaptureData（含组数量、子 AST 指针），                             */
/*       注册到全局映射表（key = DFA.states 指针）。                             */
/*     - 返回 DFA（按值）。                                                      */
/*                                                                            */
/*  2. dfa_match_captured(dfa, input):                                          */
/*     - 通过 DFA.states 指针查全局映射表获得 CaptureData。                      */
/*     - 内联最左最长匹配找到完整匹配区间。                                       */
/*     - 遍历 CaptureData 中记录的每个子 AST，                                  */
/*       用子 AST 构建独立 DFA，在匹配区间内做最长匹配（match_sub_dfa_greedy）。  */
/*     - 填充 CapturedMatch.groups[]。                                          */
/*                                                                            */
/*  3. 为什么用子 DFA 而不是 NFA 模拟？                                          */
/*     - NFAGraph 只暴露 start/end 指针，无状态数组，无法高效做 epsilon-closure。 */
/*     - 子 DFA 复用已有的 dfa_from_nfa + dfa_match，代码简洁。                   */
/* ========================================================================== */

/* ========================================================================== */
/*  内部数据结构                                                                  */
/* ========================================================================== */

/**
 * 捕获组在构建期的元数据。
 * 在 DFA 构建时收集，匹配时使用。
 */
typedef struct {
    int id;                 /* 组编号（从 1 开始） */
    size_t pattern_pos;     /* 组在 pattern 中的起始偏移（'(' 的位置） */
    ASTNode *sub_ast;       /* 组内部的子 AST（AST_GROUP.left）— 克隆副本 */
} GroupMeta;

/**
 * 附加在 DFA 上的捕获组数据。
 * 通过全局映射表与 DFAMachine.states 关联。
 */
typedef struct {
    int group_count;                    /* 捕获组总数（不含第 0 组） */
    GroupMeta *groups;                  /* groups[1..group_count] */
} CaptureData;

/* ========================================================================== */
/*  全局映射表：DFA.states 指针 → CaptureData                                    */
/* ========================================================================== */

#define CAPTURE_MAP_CAPACITY 256

typedef struct {
    const void *dfa_states_ptr;
    CaptureData *capdata;
} CaptureMapEntry;

static CaptureMapEntry capture_map[CAPTURE_MAP_CAPACITY];
static int capture_map_used = 0;

/** 在映射表中查找 CaptureData */
static CaptureData *capture_map_find(const void *ptr) {
    for (int i = 0; i < capture_map_used; i++) {
        if (capture_map[i].dfa_states_ptr == ptr) {
            return capture_map[i].capdata;
        }
    }
    return NULL;
}

/** 在映射表中注册新条目 */
static void capture_map_insert(const void *ptr, CaptureData *capdata) {
    if (capture_map_used >= CAPTURE_MAP_CAPACITY) return;
    capture_map[capture_map_used].dfa_states_ptr = ptr;
    capture_map[capture_map_used].capdata = capdata;
    capture_map_used++;
}

/** 从映射表中移除指定条目的 CaptureData */
static void capture_map_remove(const void *ptr) {
    for (int i = 0; i < capture_map_used; i++) {
        if (capture_map[i].dfa_states_ptr == ptr) {
            capture_map_used--;
            for (int j = i; j < capture_map_used; j++) {
                capture_map[j] = capture_map[j + 1];
            }
            break;
        }
    }
}

/* ========================================================================== */
/*  AST 遍历：统计捕获组数量，收集组元数据                                         */
/* ========================================================================== */

/**
 * 递归遍历 AST，统计捕获组数量。
 * 每个 AST_GROUP 节点计为一个捕获组。
 */
static int count_groups_recursive(const ASTNode *node) {
    if (!node) return 0;

    int count = (node->type == AST_GROUP) ? 1 : 0;
    count += count_groups_recursive(node->left);
    count += count_groups_recursive(node->right);
    return count;
}

/**
 * 递归遍历 AST，为每个 AST_GROUP 收集元数据。
 *
 * @param node        当前 AST 节点
 * @param next_id     下一个可用的组编号（从 1 开始）
 * @param metas_out   输出数组，Caller 分配好大小为 group_count + 1
 * @return            下一个可用的组编号
 */
static int collect_group_metas_recursive(const ASTNode *node, int next_id,
                                          GroupMeta *metas_out) {
    if (!node) return next_id;

    if (node->type == AST_GROUP) {
        metas_out[next_id].id = next_id;
        metas_out[next_id].sub_ast = ast_clone(node->left);
        metas_out[next_id].pattern_pos = node->pos;
        next_id++;
        /* 继续遍历组内部（可能有嵌套组） */
        next_id = collect_group_metas_recursive(node->left, next_id, metas_out);
        /* 组节点处理完毕，不继续遍历兄弟（组本身是叶子式的包装） */
        return next_id;
    }

    /* 非 GROUP 节点，继续递归左右子树 */
    next_id = collect_group_metas_recursive(node->left, next_id, metas_out);
    next_id = collect_group_metas_recursive(node->right, next_id, metas_out);
    return next_id;
}

/* ========================================================================== */
/*  子组匹配：用独立 DFA 在文本区间内定位捕获组                                    */
/* ========================================================================== */

/**
 * 从子 AST 构建独立 DFA，在文本区间内做最长匹配（贪婪），POSIX 语义。
 *
 * 策略：从区间内每个起始位置尝试驱动子 DFA，记录全局最长匹配的位置。
 * 这确保像 (a+)(b+) 中 b+ 子组能在 "aaabbb" 的 "bbb" 部分正确匹配，
 * 即使从区间起点开始 b+ 无法匹配。
 *
 * @param sub_ast     子表达式的 AST
 * @param text        完整输入文本
 * @param text_start  搜索区间的起始偏移（相对于 text）
 * @param text_len    搜索区间的长度
 * @param out_start   输出: 匹配在 text 中的绝对起始偏移（仅当返回值 > 0 时有效）
 * @return            匹配长度（0 = 未匹配）
 */
static size_t match_sub_dfa_greedy(ASTNode *sub_ast,
                                    const char *text,
                                    size_t text_start,
                                    size_t text_len,
                                    size_t *out_start)
{
    if (out_start) *out_start = text_start;
    if (!sub_ast || text_len == 0) return 0;

    /* 从子 AST 构建 NFA */
    NFAGraph sub_nfa = nfa_from_ast(sub_ast);
    if (sub_nfa.state_count <= 0) {
        return 0;
    }

    /* 从 NFA 构建 DFA */
    DFAMachine sub_dfa = dfa_from_nfa(&sub_nfa);
    nfa_free(&sub_nfa);

    if (sub_dfa.states == NULL) {
        return 0;
    }

    /* 在区间 [text_start, text_start + text_len) 内做最长匹配 */
    const char *text_base = text + text_start;
    size_t global_best_len = 0;
    size_t global_best_start = 0;

    /* 对区间内每个起始位置，扫描到区间末尾，记录最长匹配 */
    for (size_t s = 0; s < text_len; s++) {
        int current_state = sub_dfa.start_state;
        size_t pos = s;
        size_t last_accept = s;

        /* 检查起始状态是否为接受状态（如空匹配 a*） */
        if (sub_dfa.states[current_state].is_accept) {
            last_accept = s;
        }

        while (pos < text_len) {
            if (sub_dfa.states[current_state].is_accept && (pos - s) > (last_accept - s)) {
                last_accept = pos;
            }

            int idx = (unsigned char)text_base[pos];
            int next_state = sub_dfa.states[current_state].transitions[idx];
            if (next_state == -1) {
                break;
            }

            current_state = next_state;
            pos++;
        }

        /* 检查最终位置是否是接受状态 */
        if (sub_dfa.states[current_state].is_accept && (pos - s) > (last_accept - s)) {
            last_accept = pos;
        }

        size_t len = last_accept - s;
        if (len > global_best_len) {
            global_best_len = len;
            global_best_start = s;
        }
    }

    dfa_free(&sub_dfa);

    if (global_best_len > 0 && out_start) {
        *out_start = text_start + global_best_start;
    }
    return global_best_len;
}

/* ========================================================================== */
/*  dfa_from_ast_with_groups — 实现                                               */
/* ========================================================================== */

DFAMachine dfa_from_ast_with_groups(const ASTNode *ast_root) {
    if (!ast_root) return (DFAMachine){0};

    /* 1. 统计捕获组数量 */
    int group_count = count_groups_recursive(ast_root);

    /* 2. 构建 NFA → DFA */
    NFAGraph nfa = nfa_from_ast(ast_root);
    if (nfa.state_count <= 0) {
        return (DFAMachine){0};
    }

    DFAMachine dfa = dfa_from_nfa(&nfa);
    nfa_free(&nfa);

    if (dfa.states == NULL) {
        return dfa;
    }

    /* 3. 如果有捕获组，分配 CaptureData 并注册到映射表 */
    if (group_count > 0) {
        size_t metas_size = (size_t)(group_count + 1) * sizeof(GroupMeta);
        size_t capdata_size = sizeof(CaptureData) + metas_size;

        CaptureData *capdata = (CaptureData *)calloc(1, capdata_size);
        if (!capdata) {
            dfa_free(&dfa);
            return (DFAMachine){0};
        }

        capdata->group_count = group_count;
        capdata->groups = (GroupMeta *)((char *)capdata + sizeof(CaptureData));

        /* 收集每个组的元数据 */
        collect_group_metas_recursive(ast_root, 1, capdata->groups);

        /* 注册到全局映射表，key = DFA states 数组的指针 */
        capture_map_insert(dfa.states, capdata);
    }

    return dfa;
}

/* ========================================================================== */
/*  dfa_capture_free — 实现                                                       */
/* ========================================================================== */

void dfa_capture_free(DFAMachine *dfa) {
    if (!dfa || !dfa->states) return;

    /* 从映射表中查找并释放 CaptureData */
    CaptureData *capdata = capture_map_find(dfa->states);
    if (capdata) {
        /* 释放每个组的子 AST 克隆 */
        if (capdata->groups) {
            for (int i = 1; i <= capdata->group_count; i++) {
            ast_free(capdata->groups[i].sub_ast);
            }
        }
        capture_map_remove(dfa->states);
        free(capdata);
    }

    /* 释放 DFA */
    dfa_free(dfa);
}

/* ========================================================================== */
/*  dfa_match_captured — 实现                                                     */
/* ========================================================================== */

CapturedMatch dfa_match_captured(const DFAMachine *dfa, const char *input) {
    CapturedMatch result = {0};

    if (!dfa || !input || !dfa->states) {
        return result;
    }

    /* 1. 通过 DFA.states 指针查找 CaptureData */
    CaptureData *capdata = capture_map_find(dfa->states);
    int group_count = capdata ? capdata->group_count : 0;

    /* 2. 用基础 DFA 匹配找到完整匹配区间（最左最长匹配） */
    MatchResult full_match = {0};
    {
        size_t input_len = strlen(input);
        size_t best_end = 0;
        int found = 0;
        size_t m_start;

        for (m_start = 0; m_start <= input_len; m_start++) {
            int state = dfa->start_state;
            size_t pos = m_start;

            while (pos <= input_len) {
                if (dfa->states[state].is_accept) {
                    best_end = pos;
                    found = 1;
                }
                if (pos == input_len) break;
                int idx = (unsigned char)input[pos];
                int next = dfa->states[state].transitions[idx];
                if (next == -1) break;
                state = next;
                pos++;
            }
            if (found) break;  /* 找到从 m_start 开始的最长匹配 */
        }

        if (!found) {
            return result;
        }

        full_match.matched = 1;
        full_match.start = m_start;
        full_match.end = best_end;
        full_match.length = best_end - m_start;
    }
    if (!full_match.matched) {
        return result;
    }

    /* 3. 填充完整匹配信息（第 0 组） */
    result.matched = 1;
    result.start = full_match.start;
    result.end = full_match.end;
    result.length = full_match.length;
    result.group_count = group_count;

    /* 4. 分配捕获组数组（含第 0 组 = 完整匹配） */
    size_t total_groups = (size_t)group_count + 1;
    result.groups = (CaptureGroup *)calloc(total_groups, sizeof(CaptureGroup));
    if (!result.groups) {
        return result;
    }

    /* 第 0 组 = 完整匹配 */
    result.groups[0].matched = 1;
    result.groups[0].start = full_match.start;
    result.groups[0].end = full_match.end;
    result.groups[0].length = full_match.length;

    /* 5. 对每个捕获组，用子 DFA 在匹配区间内定位 */
    if (capdata && capdata->groups) {
        for (int i = 1; i <= group_count; i++) {
            GroupMeta *meta = &capdata->groups[i];
            if (!meta->sub_ast) continue;

            /* 在完整匹配的输入区间内做子组匹配 */
            size_t sub_len = full_match.length;
            if (sub_len == 0) continue;

            /* 子表达式在区间内的匹配（返回长度和绝对起始位置） */
            size_t sub_start = 0;
            size_t sub_len_matched = match_sub_dfa_greedy(
                meta->sub_ast,
                input,
                full_match.start,
                sub_len,
                &sub_start
            );

            if (sub_len_matched > 0) {
                result.groups[i].matched = 1;
                result.groups[i].start = sub_start;
                result.groups[i].end = sub_start + sub_len_matched;
                result.groups[i].length = sub_len_matched;
            }
        }
    }

    return result;
}

/* ========================================================================== */
/*  captured_match_free — 实现                                                    */
/* ========================================================================== */

void captured_match_free(CapturedMatch *match) {
    if (!match) return;
    free(match->groups);
    match->groups = NULL;
    match->group_count = 0;
}
