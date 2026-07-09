#include "api.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "matcher.h"
#include "nfa.h"
#include "parser.h"
#include "hopcroft.h"

static char *regex_strdup(const char *s) {
    size_t len;
    char *copy;

    if (!s) {
        return NULL;
    }

    len = strlen(s);
    copy = (char *)malloc(len + 1);
    if (!copy) {
        return NULL;
    }

    memcpy(copy, s, len + 1);
    return copy;
}

static void regex_set_error(regex_t *prog, int code, const char *message) {
    if (!prog) {
        return;
    }

    prog->error_code = code;
    if (message) {
        snprintf(prog->error_msg, sizeof(prog->error_msg), "%s", message);
    } else {
        snprintf(prog->error_msg, sizeof(prog->error_msg), "%s", regex_error(code));
    }
}

static void clear_result(MatchResult *result) {
    if (result) {
        memset(result, 0, sizeof(*result));
    }
}

regex_t *regex_compile(const char *pattern, int flags) {
    regex_t *prog;
    Parser parser;
    ASTNode *ast = NULL;
    NFAGraph nfa = {0};

    if (!pattern) {
        return NULL;
    }

    prog = (regex_t *)calloc(1, sizeof(*prog));
    if (!prog) {
        return NULL;
    }

    prog->flags = flags;
    prog->pattern = regex_strdup(pattern);
    if (!prog->pattern) {
        regex_set_error(prog, REGEX_ERR_NO_MEMORY, NULL);
        regex_free(prog);
        return NULL;
    }

    parser_init(&parser, pattern);
    ast = parser_parse(&parser);
    if (!ast) {
        regex_set_error(prog, REGEX_ERR_PARSE, parser.error_msg);
        regex_free(prog);
        return NULL;
    }

    nfa = nfa_from_ast(ast);
    ast_free(ast);
    ast = NULL;

    if (!nfa.start || !nfa.end || !nfa.states) {
        regex_set_error(prog, REGEX_ERR_NFA_BUILD, NULL);
        nfa_free(&nfa);
        regex_free(prog);
        return NULL;
    }

    prog->dfa = dfa_from_nfa(&nfa);
    nfa_free(&nfa);

    if (!prog->dfa.states || prog->dfa.state_count <= 0) {
        regex_set_error(prog, REGEX_ERR_DFA_BUILD, NULL);
        regex_free(prog);
        return NULL;
    }

    /* Hopcroft 最小化 — 减少状态数，提升后续匹配速度 */
    dfa_minimize(&prog->dfa);

    regex_set_error(prog, REGEX_OK, NULL);
    return prog;
}

int regex_match(regex_t *prog, const char *text, MatchResult *result) {
    MatchResult local;

    clear_result(result);
    if (!prog || !prog->dfa.states || !text) {
        if (prog) {
            regex_set_error(prog, REGEX_ERR_NULL_ARGUMENT, NULL);
        }
        return 0;
    }

    local = dfa_match_full(&prog->dfa, text);
    if (result) {
        *result = local;
    }

    regex_set_error(prog, REGEX_OK, NULL);
    return local.matched;
}

int regex_search(regex_t *prog, const char *text, MatchResult *result) {
    MatchResult local;

    clear_result(result);
    if (!prog || !prog->dfa.states || !text) {
        if (prog) {
            regex_set_error(prog, REGEX_ERR_NULL_ARGUMENT, NULL);
        }
        return 0;
    }

    local = dfa_match(&prog->dfa, text);
    if (result) {
        *result = local;
    }

    regex_set_error(prog, REGEX_OK, NULL);
    return local.matched;
}

MatchResult *regex_findall(regex_t *prog, const char *text, int *count) {
    size_t text_len;
    size_t capacity;
    MatchResult *matches;
    int found;

    if (count) {
        *count = 0;
    }

    if (!prog || !prog->dfa.states || !text || !count) {
        if (prog) {
            regex_set_error(prog, REGEX_ERR_NULL_ARGUMENT, NULL);
        }
        return NULL;
    }

    text_len = strlen(text);
    capacity = text_len + 1;
    if (capacity > (size_t)INT_MAX) {
        regex_set_error(prog, REGEX_ERR_TOO_MANY_MATCHES, NULL);
        return NULL;
    }

    matches = (MatchResult *)calloc(capacity, sizeof(*matches));
    if (!matches) {
        regex_set_error(prog, REGEX_ERR_NO_MEMORY, NULL);
        return NULL;
    }

    found = dfa_match_all(&prog->dfa, text, matches, (int)capacity);
    if (found <= 0) {
        free(matches);
        regex_set_error(prog, REGEX_OK, NULL);
        return NULL;
    }

    *count = found;
    regex_set_error(prog, REGEX_OK, NULL);
    return matches;
}

const char *regex_error(int err_code) {
    switch (err_code) {
    case REGEX_OK:
        return "no error";
    case REGEX_ERR_NULL_ARGUMENT:
        return "null argument";
    case REGEX_ERR_NO_MEMORY:
        return "out of memory";
    case REGEX_ERR_PARSE:
        return "parse error";
    case REGEX_ERR_NFA_BUILD:
        return "failed to build NFA";
    case REGEX_ERR_DFA_BUILD:
        return "failed to build DFA";
    case REGEX_ERR_TOO_MANY_MATCHES:
        return "too many matches";
    default:
        return "unknown regex error";
    }
}

void regex_free(regex_t *prog) {
    if (!prog) {
        return;
    }

    dfa_free(&prog->dfa);
    free(prog->pattern);
    prog->pattern = NULL;
    free(prog);
}

void regex_findall_free(MatchResult *matches) {
    free(matches);
}
