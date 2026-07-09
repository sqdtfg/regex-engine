/**
 * dot_gen.c — 生成单文件 HTML 诊断报告（全中文）
 *
 * 对典型正则表达式集，输出一个自包含的 HTML 文件，包含：
 *   (1) 正则表达式模式名
 *   (2) NFA 状态数
 *   (3) DFA 最小化前：状态数 + 状态转移表
 *   (4) DFA 最小化后：状态数 + 状态转移表
 *   (5) 汇总对比表（所有模式横向对比）
 *
 * 用法（CMake）：
 *   cmake --build . --target run_dotgen
 * 输出：
 *   build/DOT/report.html（浏览器可直接打开）
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

/* 安全文件名 */
static void safe_name(const char *s, char *out, int max) {
    int j = 0;
    static const char *hex = "0123456789abcdef";
    for (const char *p = s; *p && j < max - 4; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')
            out[j++] = *p;
        else { out[j++] = '_'; out[j++] = hex[((unsigned char)*p)>>4]; out[j++] = hex[((unsigned char)*p)&0xf]; }
    }
    out[j] = '\0';
}

/* ========================================================================== */
/*  字符区间标签格式化（紧凑、HTML 安全）                                        */
/* ========================================================================== */

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

static int format_char_range(char *buf, size_t bufsz, int lo, int hi) {
    if (lo == hi) {
        if (lo >= 32 && lo <= 126) {
            return html_escape_char(buf, bufsz, lo);
        } else {
            switch (lo) {
            case '\t': return snprintf(buf, bufsz, "\\t");
            case '\n': return snprintf(buf, bufsz, "\\n");
            case '\r': return snprintf(buf, bufsz, "\\r");
            default:   return snprintf(buf, bufsz, "0x%02X", lo);
            }
        }
    } else {
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
/*  HTML 输出（全中文）                                                         */
/* ========================================================================== */

static void emit_html_header(FILE *fp) {
    fputs(
"<!DOCTYPE html>\n"
"<html lang=\"zh\">\n"
"<head>\n"
"<meta charset=\"UTF-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"<title>正则引擎 &mdash; 编译报告</title>\n"
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
"  .card-lbl { font-size: 0.73rem; color: #888; margin-top: 0.12rem; }\n"
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
"<h1>&#9881; 正则引擎 &mdash; 编译报告</h1>\n"
"<p class=\"sub\">"
"Thompson NFA &rarr; 子集构造 DFA &rarr; Hopcroft 最小化"
"</p>\n"
    , fp);
}

static void emit_html_footer(FILE *fp) {
    fputs("</body>\n</html>\n", fp);
}

static void emit_summary_table(FILE *fp, int n, const char **pats,
                                const int *nfa, const int *dfab, const int *dfaa) {
    fputs("<h2>&#128202; 状态压缩汇总</h2>\n"
          "<table class=\"summary\">\n"
          "<thead><tr>"
          "<th>编号</th><th>正则表达式</th>"
          "<th>NFA 状态数</th>"
          "<th>DFA(最小化前)</th>"
          "<th>DFA(最小化后)</th>"
          "<th>压缩比</th>"
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

static void emit_dfa_table(FILE *fp, const DFAMachine *dfa) {
    fputs("<table><thead><tr>"
          "<th class=\"w-st\">状态</th>"
          "<th class=\"w-ac\">接受?</th>"
          "<th>转移</th>"
          "</tr></thead>\n<tbody>\n", fp);

    for (int i = 0; i < dfa->state_count; i++) {
        const DFAState *s = &dfa->states[i];

        fprintf(fp, "<tr><td class=\"w-st\">S%d</td>", s->id);
        fprintf(fp, "<td class=\"w-ac %s\">%s</td>",
                s->is_accept ? "ac-y" : "ac-n",
                s->is_accept ? "是" : "否");

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

            fprintf(fp, "%s &rarr; <b>S%d</b>", label, t);
        }

        if (first) fputs("&mdash;", fp);
        fputs("</td></tr>\n", fp);
    }

    fputs("</tbody>\n</table>\n", fp);
}

/* ========================================================================== */
/*  单模式处理                                                                  */
/* ========================================================================== */

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

    const char *dir = "DOT";
    if (argc > 1) dir = argv[1];

    char out_path[512];
    snprintf(out_path, sizeof(out_path), "%s/report.html", dir);

    {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "mkdir -p %s 2>/dev/null", dir);
        system(cmd);
    }

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

    int nfa_counts[20], dfa_before[20], dfa_after[20];

    printf("=== 第一趟：收集统计信息 ===\n");
    for (int i = 0; i < n_pats; i++) {
        int rc = process_pattern(patterns[i],
                                  &nfa_counts[i], &dfa_before[i], &dfa_after[i]);
        printf("  [%d] %-20s  NFA=%-3d  DFA(前)=%-3d  DFA(后)=%-3d  %s\n",
               i + 1, patterns[i],
               nfa_counts[i], dfa_before[i], dfa_after[i],
               rc == 0 ? "OK" : "解析失败");
    }

    FILE *fp = fopen(out_path, "w");
    if (!fp) {
        fprintf(stderr, "无法写入: %s\n", out_path);
        return 1;
    }

    emit_html_header(fp);
    emit_summary_table(fp, n_pats, patterns,
                       nfa_counts, dfa_before, dfa_after);

    printf("\n=== 第二趟：输出详情 ===\n");

    for (int i = 0; i < n_pats; i++) {
        const char *pat = patterns[i];

        if (nfa_counts[i] == 0 && dfa_before[i] == 0 && dfa_after[i] == 0) {
            fprintf(fp, "<h2>%s <span style=\"color:#c62828;font-weight:400\">"
                    "(解析失败)</span></h2>\n", pat);
            continue;
        }

        printf("  [%d] %s\n", i + 1, pat);

        Parser parser;
        parser_init(&parser, pat);
        ASTNode *ast = parser_parse(&parser);

        NFAGraph nfa = nfa_from_ast(ast);
        DFAMachine dfa = dfa_from_nfa(&nfa);

        int nfa_st = nfa.state_count;
        int dfab_st = dfa.state_count;

        fprintf(fp, "<h2>%s</h2>\n", pat);

        fprintf(fp,
            "<div class=\"card-row\">"
            "<div class=\"card\"><div class=\"card-num\">%d</div>"
            "<div class=\"card-lbl\">NFA 状态数</div></div>"
            "<div class=\"card\"><div class=\"card-num\">%d</div>"
            "<div class=\"card-lbl\">DFA 最小化前状态数</div></div>"
            "<div class=\"card\"><div class=\"card-num\">%d</div>"
            "<div class=\"card-lbl\">DFA 最小化后状态数</div></div>"
            "</div>\n",
            nfa_st, dfab_st, dfa_after[i]);

        fprintf(fp, "<h3>DFA 最小化前 &mdash; %d 个状态</h3>\n", dfab_st);
        emit_dfa_table(fp, &dfa);

        dfa_minimize(&dfa);
        fprintf(fp, "<h3>DFA 最小化后 &mdash; %d 个状态</h3>\n", dfa_after[i]);
        emit_dfa_table(fp, &dfa);

        /* 生成 DOT 文件 */
        {
            char s[128]; safe_name(pat, s, sizeof(s));
            char p[512];
            /* NFA DOT */
            {   Parser p2; parser_init(&p2, pat); ASTNode *a2 = parser_parse(&p2);
                NFAGraph n2 = nfa_from_ast(a2);
                snprintf(p, sizeof(p), "DOT/nfa_%s.dot", s);
                nfa_dump_dot_file(&n2, p);
                nfa_free(&n2); ast_free(a2);
            }
            /* DFA 最小化前 */
            {   Parser p2; parser_init(&p2, pat); ASTNode *a2 = parser_parse(&p2);
                NFAGraph n2 = nfa_from_ast(a2); DFAMachine d2 = dfa_from_nfa(&n2);
                snprintf(p, sizeof(p), "DOT/dfa_before_%s.dot", s);
                dfa_dump_dot_file(&d2, p);
                nfa_free(&n2); ast_free(a2);
            }
            /* DFA 最小化后 */
            {   Parser p2; parser_init(&p2, pat); ASTNode *a2 = parser_parse(&p2);
                NFAGraph n2 = nfa_from_ast(a2); DFAMachine d2 = dfa_from_nfa(&n2);
                dfa_minimize(&d2);
                snprintf(p, sizeof(p), "DOT/dfa_min_%s.dot", s);
                dfa_dump_dot_file(&d2, p);
                dfa_free(&d2); nfa_free(&n2); ast_free(a2);
            }
        }

        dfa_free(&dfa);
        nfa_free(&nfa);
        ast_free(ast);
    }

    emit_html_footer(fp);
    fclose(fp);

    printf("\n报告已写入: %s\n", out_path);
    return 0;
}
