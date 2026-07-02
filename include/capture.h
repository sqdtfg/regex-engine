#ifndef REGEX_CAPTURE_H
#define REGEX_CAPTURE_H

#include "dfa.h"
#include <stddef.h>

/* ========================================================================== */
/*  捕获组信息                                                                  */
/* ========================================================================== */

/**
 * 单个捕获组的位置信息。
 * matched = 1 表示该组在本次匹配中成功捕获，start/end 有效。
 * matched = 0 表示该组未参与匹配（例如 | 的未走分支、量词为 0 次）。
 */
typedef struct {
    int matched;            /* 0 = 未捕获, 1 = 成功捕获 */
    size_t start;           /* 捕获内容在输入中的起始偏移 */
    size_t end;             /* 捕获内容的结束偏移（不包含） */
    size_t length;          /* 捕获长度 = end - start */
} CaptureGroup;

/* ========================================================================== */
/*  带捕获组的匹配结果                                                          */
/* ========================================================================== */

/**
 * 扩展的匹配结果，包含完整匹配信息和所有捕获组的位置。
 * group_count 由 dfa_from_ast_with_groups() 设置，调用者无需手动管理。
 */
typedef struct {
    int matched;                /* 0 = 未匹配, 1 = 匹配成功 */
    size_t start;               /* 完整匹配的起始位置 */
    size_t end;                 /* 完整匹配的结束位置 */
    size_t length;              /* 完整匹配长度 */
    int group_count;            /* 捕获组总数（不含第 0 组） */
    CaptureGroup *groups;       /* groups[0..group_count]，groups[0] = 第 0 组（完整匹配） */
} CapturedMatch;

/* ========================================================================== */
/*  从 AST 构建带捕获组信息的 DFA                                               */
/* ========================================================================== */

/**
 * 从 AST 根节点构建 DFA，同时统计捕获组数量并将组边界信息嵌入 DFA 状态。
 *
 * 工作流程：
 *   1. 遍历 AST，为每个 AST_GROUP 分配一个递增的捕获组编号（从 1 开始）。
 *   2. 构建 NFA（Thompson 构造），在每个组的入口/出口状态打上组号标记。
 *   3. 子集构造法构建 DFA，每个 DFA 状态继承其 NFA 子集中的组边界标记。
 *   4. 匹配时，记录每个组首次进入和最后退出的输入位置。
 *
 * @param ast_root  AST 根节点（由 parser_parse 产出，不会被修改或释放）
 * @return          带捕获组信息的 DFA 机器。调用者负责调用 dfa_capture_free() 释放。
 */
DFAMachine dfa_from_ast_with_groups(const ASTNode *ast_root);

/**
 * 释放 dfa_from_ast_with_groups() 返回的 DFA（包括内部捕获组数据）。
 */
void dfa_capture_free(DFAMachine *dfa);

/* ========================================================================== */
/*  带捕获组的匹配 API                                                         */
/* ========================================================================== */

/**
 * 用带捕获组的 DFA 匹配输入字符串中的第一个子串。
 * @param dfa       带捕获组信息的 DFA
 * @param input     待匹配的输入字符串
 * @return          捕获匹配结果。调用者负责调用 captured_match_free() 释放 groups。
 */
CapturedMatch dfa_match_captured(const DFAMachine *dfa, const char *input);

/**
 * 释放 CapturedMatch.groups 数组（如果已分配）。
 */
void captured_match_free(CapturedMatch *match);

#endif /* REGEX_CAPTURE_H */
