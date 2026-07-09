/**
 * dot_gen.c — 生成单文件 HTML 诊断报告
 *
 * 对一组典型正则表达式穿过完整管道，输出一个自包含的 HTML 文件，
 * 包含清晰的网格表格，每个模式展示:
 *   (1) 正则表达式模式
 *   (2) NFA 状态数
 *   (3) DFA 最小化前：状态数 + 状态转移表
 *   (4) DFA 最小化后：状态数 + 状态转移表
 *
 * 用法（CMake）：
 *   cmake --build . --target run_dotgen
 * 输出：
 *   build/DOT/report.html（可在任意浏览器中打开）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "parser.h"
#include "nfa.h"
#include "dfa.h"
#include "hopcroft.h"

/* ========================================================================== */
/*  字符区间标签格式化（紧凑、HTML 安全）                                        */
/* ========================================================================== */

/** 将单个字符转为 HTML 安全字符串 */
static int html_escape_char(char *buf, size_t bufsz, int c) {
    switch (c) {
    case '&':  return snprintf(buf, bufsz, "&amp;");
    case '<':  return snprintf(buf, bufsz, "&lt;");
    case '>':  return snprintf(buf, bufsz, "&gt;");
    case '\'': return snprintf(buf, bufsz, "&#39;");
    case '"':  return snprintf(buf, bufsz, "&quot;");
    default:   return snprintf(buf, bufsz, "%c", c);
    }
}

/**
 * 将字符区间 [lo..hi] 格式化为紧凑标签写入 buf。
 * 返回写入的字节数（不含 NUL）。
 *
 *   - 单个可打印字符 → 'a'
 *   - 单个不可打印字符 → 0x09
 *   - 可打印范围 → a-z
 *   - 不可打印范围 → 0x00-0x1F
 */
static int format_char_range(char *buf, size_t bufsz, int lo, int hi) {
    if (lo == hi) {
        if (lo >= 32 && lo <= 126) {
            return html_escape_char(buf, bufsz, lo);
        } else {
            /* 识别常见控制字符，给出助记名 */
            switch (lo) {
            case '\t': return snprintf(buf, bufsz, "\\t");
            case '\n': return snprintf(buf, bufsz, "\\n");
            case '\r': return snprintf(buf, bufsz, "\\r");
            default:   return snprintf(buf, bufsz, "0x%02X", lo);
            }
        }
    } else {
        /* 区间 */
        if (lo >= 32 && lo <= 126 && hi >= 32 && hi <= 126) {
            char lo_c, hi_c;
            switch (lo) { case '<': lo_c = 0; break; case '>': lo_c = 0; break;
                          case '&': lo_c = 0; break; default: lo_c = (char)lo; }
            switch (hi) { case '<': hi_c = 0; break; case '>': hi_c = 0; break;
                          case '&': hi_c = 0; break; default: hi_c = (char)hi; }
            if (lo_c && hi_c) return snprintf(buf, bufsz, "%c-%c", lo_c, hi_c);
        }
        return snprintf(buf, bufsz, "0x%02X-0x%02X", lo, hi);
    }
}

/**
 * 构建紧凑的逗号分隔标签，描述从 DFA 状态转移到 target 的全部字符。
 * 合并连续字符为区间（如 a-z、0-9），减少视觉噪音。
 * 输出已做 HTML 转义。返回 buf 指针以便链式使用。
 */
static const char *transition_label(const int *trans, int target,
                                     char *buf, size_t bufsz) {
    if (bufsz < 4) return "";
    int pos = 0;
    int ranges = 0;

    for (int c = 0; c < 256; ) {
        if (trans[c] != target) { c++; continue; }
        int hi = c;
        while (hi + 1 < 256 && trans[hi + 1] == target) hi++;

        if (ranges > 0) {
            if (pos < (int)bufsz - 2) buf[pos++] = ',';
        }

        char tmp[32];
        int n = format_char_range(tmp, sizeof(tmp), c, hi);
        if (pos + n < (int)bufsz - 1) {
            memcpy(buf + pos, tmp, (size_t)n);
            pos += n;
        }
        ranges++;
        c = hi + 1;
    }

    buf[pos] = '\0';
    return (ranges == 0) ? "-" : buf;
}

/* ========================================================================== */
/*  HTML 输出辅助                                                               */
/* ========================================================================== */

/** 输出 HTML 头部（含嵌入式 CSS） */
static void emit_html_header(FILE *fp) {
    fputs(
"<!DOCTYPE html>\n"
"<html lang=\"zh\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>Regex Engine &mdash; Compilation Report</title>\n"
"<style>\n"
"  * { box-sizing: border-box; margin: 0; padding: 0; }\n"
"  body {\n"
"    font-family: 'Segoe UI', system-ui, -apple-system, sans-serif;\n"
"    background: #f4f5f7; color: #1a1a2e; padding: 2rem;\n"
"    max-width: 1100px; margin: 0 auto;\n"
"  }\n"
"  h1 { font-size: 1.5rem; margin-bottom: 0.2rem; }\n"
"  .sub { color: #666; margin-bottom: 2rem; font-size: 0.9rem; }\n"
"  h2 {\n"
"    font-size: 1.15rem; margin: 2.2rem 0 0.75rem;\n"
"    padding: 0.45rem 0.7rem; background: #e2e4e9;\n"
"    border-left: 4px solid #3a7bd5; border-radius: 0 4px 4px 0;\n"
"  }\n"
"  h3 {\n"
"    font-size: 0.95rem; margin: 1.4rem 0 0.55rem;\n"
"    color: #444; font-weight: 600;\n"
"  }\n"
"\n"
"  /* ---- 汇总表 ---- */\n"
"  .summary { margin: 1rem 0 1.5rem; }\n"
"  table {\n"
"    width: 100%; border-collapse: collapse; margin-bottom: 1.2rem;\n"
"    background: #fff; border-radius: 6px;\n"
"    overflow: hidden; box-shadow: 0 1px 5px rgba(0,0,0,0.07);\n"
"    font-size: 0.88rem;\n"
"  }\n"
"  th {\n"
"    background: #3a3f51; color: #fff; padding: 0.5rem 0.65rem;\n"
"    text-align: left; font-weight: 600; letter-spacing: 0.02em;\n"
"  }\n"
"  td { padding: 0.45rem 0.65rem; border-bottom: 1px solid #eee; vertical-align: top; }\n"
"  tr:last-child td { border-bottom: none; }\n"
"  .summary tr:hover td { background: #f0f4ff; }\n"
"\n"
"  /* 列宽 */\n"
"  th.w-st, td.w-st { width: 4.5rem; text-align: center; font-weight: 700; }\n"
"  th.w-ac, td.w-ac { width: 4rem; text-align: center; }\n"
"  td.tx {\n"
"    font-family: 'Cascadia Code', 'Cascadia Mono', 'Consolas', 'Fira Code', monospace;\n"
"    font-size: 0.83rem; line-height: 1.55; word-break: keep-all;\n"
"  }\n"
"  td.tx b { color: #3a7bd5; font-weight: 700; }\n"
"\n"
"  td.ac-y { color: #1b7a1b; font-size: 1.05rem; }\n"
"  td.ac-n { color: #bbb; }\n"
"\n"
"  /* ---- 徽章 ---- */\n"
"  .badge {\n"
"    display: inline-block; padding: 0.1rem 0.5rem; border-radius: 3px;\n"
"    font-weight: 700; font-size: 0.78rem;\n"
"  }\n"
"  .badge-g { background: #e6f4ea; color: #1e7e34; }\n"
"  .badge-y { background: #fff3cd; color: #856404; }\n"
"  .badge-r { background: #fce4ec; color: #c62828; }\n"
"\n"
"  /* ---- 状态数统计卡 ---- */\n"
"  .card-row { display: flex; gap: 1rem; margin: 0.6rem 0 1.2rem; flex-wrap: wrap; }\n"
"  .card {\n"
"    background: #fff; border-radius: 6px; padding: 0.65rem 1rem;\n"
"    box-shadow: 0 1px 4px rgba(0,0,0,0.07);\n"
"    min-width: 125px; text-align: center;\n"
"  }\n"
"  .card-num { font-size: 1.4rem; font-weight: 700; color: #2c3e50; }\n"
"  .card-lbl { font-size: 0.73rem; color: #888; margin-top: 0.12rem; text-transform: uppercase; letter-spacing: 0.04em; }\n"
"\n"
"  /* ---- 模式名 ---- */\n"
"  code.re {\n"
"    background: #eef1f7; padding: 0.15rem 0.4rem; border-radius: 3px;\n"
"    font-size: 1rem; color: #3a3f51;\n"
"  }\n"
"\n"
"  /* ---- 响应式 ---- */\n"
"  @media (max-width: 700px) {\n"
"    body { padding: 0.8rem; }\n"
"    .card-row { flex-direction: column; }\n"
"    .card { min-width: unset; }\n"
"  }\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<h1>&#9881; Regex Engine &mdash; Compilation Report</h1>\n"
"<p class=\"sub\">"
"Thompson NFA &rarr; Subset-Construction DFA &rarr; Hopcroft Minimization"
"</p>\n"
    , fp);
}

/** 输出 HTML 尾部 */
static void emit_html_footer(FILE *fp) {
    fputs("</body>\n</html>\n", fp);
}

/**
 * 输出汇总对比表（所有模式横向对比）。
 * 在第二趟输出详情之前调用。
 */
static void emit_summary_table(FILE *fp, int n, const char **pats,
                                const int *nfa, const int *dfab, const int *dfaa) {
    fputs("<h2>&#128202; Summary</h2>\n"
          "<table class=\"summary\">\n"
          "<thead><tr>"
          "<th>#</th><th>Pattern</th>"
          "<th>NFA States</th>"
          "<th>DFA Before</th>"
          "<th>DFA After</th>"
          "<th>Reduction</th>"
          "</tr></thead>\n<tbody>\n", fp);

    for (int i = 0; i < n; i++) {
        double pct = (dfab[i] > 0) ? (1.0 - (double)dfaa[i] / dfab[i]) * 100.0 : 0.0;
        const char *cls;
        if (pct >= 30.0)      cls = "badge badge-g";
        else if (pct >= 10.0) cls = "badge badge-y";
        else                  cls = "badge badge-r";

        fprintf(fp,
            "<tr>"
            "<td>%d</td>"
            "<td><code class=\"re\">%s</code></td>"
            "<td class=\"w-st\">%d</td>"
            "<td class=\"w-st\">%d</td>"
            "<td class=\"w-st\">%d</td>"
            "<td class=\"w-st\"><span class=\"%s\">%.0f%%</span></td>"
            "</tr>\n",
            i + 1, pats[i], nfa[i], dfab[i], dfaa[i], cls, pct);
    }

    fputs("</tbody>\n</table>\n", fp);
}

/**
 * 输出一个 DFA 的状态转移表（HTML <table>）。
 *
 * 表头：State | Accept | Transitions
 * 每行显示一个状态的所有转移边，标签合并连续字符区间。
 * 转移格式：<b>S3</b> ← a-z,d-g（箭头左侧是目标状态，右侧是触发字符）
 */
static void emit_dfa_table(FILE *fp, const DFAMachine *dfa) {
    fputs("<table><thead><tr>"
          "<th class=\"w-st\">State</th>"
          "<th class=\"w-ac\">Accept</th>"
          "<th>Transitions</th>"
          "</tr></thead>\n<tbody>\n", fp);

    for (int i = 0; i < dfa->state_count; i++) {
        const DFAState *s = &dfa->states[i];

        fprintf(fp, "<tr><td class=\"w-st\">S%d</td>", s->id);
        fprintf(fp, "<td class=\"w-ac %s\">%s</td>",
                s->is_accept ? "ac-y" : "ac-n",
                s->is_accept ? "\xe2\x9c\x93" : "\xe2\x80\x94");

        /* 按目标状态分组，在每个 (source, target) 对内合并连续字符区间 */
        fputs("<td class=\"tx\">", fp);

        int seen[256] = {0};
        int first = 1;
        for (int c = 0; c < 256; c++) {
            int t = s->transitions[c];
            if (t == -1 || seen[t]) continue;
            seen[t] = 1;

            char label[600];
            transition_label(s->transitions, t, label, sizeof(label));

            if (!first) fputs("<br>", fp);
            first = 0;

            /* 输出格式：<目标> ← <标签>  */
            fprintf(fp, "<b>S%d</b> &larr; %s", t, label);
        }

        if (first) fputs("&mdash;", fp);
        fputs("</td></tr>\n", fp);
    }

    fputs("</tbody>\n</table>\n", fp);
}

/* ========================================================================== */
/*  单模式处理辅助：解析 → NFA → DFA → 收集计数                                 */
/* ========================================================================== */

/**
 * 对单个模式执行完整编译管道并返回统计信息。
 * 返回 0 = 成功，非 0 = 解析失败。
 */
static int process_pattern(const char *pat, int *nfa_out, int *dfab_out, int *dfaa_out) {
    Parser parser;
    parser_init(&parser, pat);
    ASTNode *ast = parser_parse(&parser);
    if (!ast) {
        *nfa_out = *dfab_out = *dfaa_out = 0;
        return -1;
    }

    NFAGraph nfa = nfa_from_ast(ast);
    *nfa_out = nfa.state_count;

    DFAMachine dfa = dfa_from_nfa(&nfa);
    *dfab_out = dfa.state_count;

    dfa_minimize(&dfa);
    *dfaa_out = dfa.state_count;

    dfa_free(&dfa);
    nfa_free(&nfa);
    ast_free(ast);
    return 0;
}

/* ========================================================================== */
/*  入口                                                                        */
/* ========================================================================== */

int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    /* ---- 输出路径 ---- */
    const char *dir = "DOT";
    if (argc > 1) dir = argv[1];

    char out_path[512];
    snprintf(out_path, sizeof(out_path), "%s/report.html", dir);

    /* 确保输出目录存在 */
    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s 2>/dev/null", dir);
        system(cmd);
    }

    /* ---- 测试模式 ---- */
    const char *patterns[] = {
        "a(b|c)*d",
        "(a|b)*abb",
        "\\d{3}-\\d{4}",
        "\\w+@\\w+\\.\\w+",
        "[a-z]+",
        "a+",
        "(ab|cd|ef)+",
        "a?b?c?",
        NULL
    };

    int n_pats = 0;
    while (patterns[n_pats]) n_pats++;

    /* ====================================================================== */
    /*  第一趟：收集汇总统计信息                                                */
    /* ====================================================================== */
    int nfa_counts[20], dfa_before[20], dfa_after[20];

    printf("=== 第一趟：收集统计信息 ===\n");
    for (int i = 0; i < n_pats; i++) {
        int rc = process_pattern(patterns[i],
                                  &nfa_counts[i], &dfa_before[i], &dfa_after[i]);
        printf("  [%d] %-20s  NFA=%-3d  DFA(before)=%-3d  DFA(after)=%-3d  %s\n",
               i + 1, patterns[i],
               nfa_counts[i], dfa_before[i], dfa_after[i],
               rc == 0 ? "OK" : "PARSE FAIL");
    }

    /* ====================================================================== */
    /*  打开输出文件                                                           */
    /* ====================================================================== */
    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        fprintf(stderr, "ERROR: Cannot open %s for writing\n", out_path);
        return 1;
    }

    emit_html_header(fp);

    /* 汇总对比表 */
    emit_summary_table(fp, n_pats, patterns,
                       nfa_counts, dfa_before, dfa_after);

    /* ====================================================================== */
    /*  第二趟：逐模式输出详情                                                 */
    /* ====================================================================== */
    printf("\n=== 第二趟：输出详情 ===\n");

    for (int i = 0; i < n_pats; i++) {
        const char *pat = patterns[i];

        /* 跳过在第一趟解析失败的模式 */
        if (nfa_counts[i] == 0 && dfa_before[i] == 0 && dfa_after[i] == 0) {
            fprintf(fp, "<h2>%s <span style=\"color:#c62828;font-weight:400\">"
                    "(parse failed)</span></h2>\n", pat);
            continue;
        }

        printf("  [%d] %s\n", i + 1, pat);

        /* 重建管道以获取 DFA 详情 */
        Parser parser;
        parser_init(&parser, pat);
        ASTNode *ast = parser_parse(&parser);

        NFAGraph nfa = nfa_from_ast(ast);
        DFAMachine dfa = dfa_from_nfa(&nfa);

        int nfa_st = nfa.state_count;
        int dfab_st = dfa.state_count;

        /* ---- 模式标题 ---- */
        fprintf(fp, "<h2>%s</h2>\n", pat);

        /* 状态数统计卡 */
        fprintf(fp,
            "<div class=\"card-row\">"
            "<div class=\"card\"><div class=\"card-num\">%d</div>"
            "<div class=\"card-lbl\">NFA states</div></div>"
            "<div class=\"card\"><div class=\"card-num\">%d</div>"
            "<div class=\"card-lbl\">DFA before minimize</div></div>"
            "<div class=\"card\"><div class=\"card-num\">%d</div>"
            "<div class=\"card-lbl\">DFA after minimize</div></div>"
            "</div>\n",
            nfa_st, dfab_st, dfa_after[i]);

        /* ---- DFA 最小化前转移表 ---- */
        fprintf(fp, "<h3>DFA Before Minimize &mdash; %d state%s</h3>\n",
                dfab_st, dfab_st == 1 ? "" : "s");
        emit_dfa_table(fp, &dfa);

        /* ---- DFA 最小化后转移表 ---- */
        dfa_minimize(&dfa);
        fprintf(fp, "<h3>DFA After Minimize &mdash; %d state%s</h3>\n",
                dfa_after[i], dfa_after[i] == 1 ? "" : "s");
        emit_dfa_table(fp, &dfa);

        dfa_free(&dfa);
        nfa_free(&nfa);
        ast_free(ast);
    }

    /* ---- 尾部 ---- */
    emit_html_footer(fp);
    fclose(fp);

    printf("\nReport written to: %s\n", out_path);
    return 0;
}
