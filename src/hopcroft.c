#include "hopcroft.h"
#include <stdlib.h>
#include <string.h>

/* ========================================================================== */
/*  Hopcroft DFA 最小化                                                        */
/*                                                                            */
/*  算法思路（Hopcroft, 1971）：                                               */
/*   1. 引入物理死状态，把所有 -1 转移补全为指向死状态（简化分区逻辑）           */
/*   2. 构建反向边表：对每个状态 s 和字符 c，记录 (s-c→t) 即 s 是 t 关于 c 的前驱 */
/*   3. 初始分区：接受态 / 非接受态 / 死状态                                    */
/*   4. 选较小的块入工作队列 W                                                  */
/*   5. 对 W 中每个块 A 和每个符号 c：                                          */
/*      a. 找到所有转移到 A 中状态的前驱（preimage）                             */
/*      b. 对每个包含这些前驱的块 B，按"是否在 preimage 中"分裂                  */
/*      c. 将较小的新块加入 W（确保每次只处理较小的那一半）                       */
/*   6. 每个等价类 → 一个最小 DFA 状态，死状态块 → -1                           */
/*   7. 释放旧内存，原地替换 DFA 机器                                          */
/* ========================================================================== */

/* ---- 反向边链表节点 ---- */
typedef struct RevNode {
    int from;               /* 源状态 id */
    struct RevNode *next;   /* 同 (sym, to) 的下一个前驱 */
} RevNode;

/* ---- 动态 int 数组（表示等价类中的状态集合） ---- */
typedef struct {
    int *data;
    int count;
    int cap;
} IntVec;

/* ---- 等价类（块） --- 在分区细化过程中是动态变化的 ---- */
typedef struct {
    int *states;    /* 块中的状态 id 列表 */
    int count;      /* 当前块大小 */
} Block;

static IntVec int_vec_new(int cap) {
    IntVec v = {0};
    v.data = (int *)malloc((size_t)cap * sizeof(int));
    v.cap  = v.data ? cap : 0;
    v.count = 0;
    return v;
}

static void int_vec_push(IntVec *v, int val) {
    if (v->count >= v->cap) {
        v->cap = (v->cap == 0) ? 16 : v->cap * 2;
        v->data = (int *)realloc(v->data, (size_t)v->cap * sizeof(int));
    }
    if (v->data) v->data[v->count++] = val;
}

static void int_vec_free(IntVec *v) {
    free(v->data);
    v->data  = NULL;
    v->count = 0;
    v->cap   = 0;
}

/**
 * 深拷贝 DFA 状态，并追加一个物理死状态。
 * 死状态对 256 个字符全部自环，对所有状态的 -1 转移补全为指向死状态。
 *
 * @param states      原始 DFA 状态数组
 * @param state_count 原始状态数
 * @param out_count   输出：新状态数 = state_count + 1
 * @param dead_id     输出：死状态的 id
 * @return  新的 DFAState 数组（调用者负责释放每个状态的 transitions 和数组本身）
 */
static DFAState *dfa_copy_with_dead(const DFAState *states, int state_count,
                                     int *out_count, int *dead_id) {
    int n = state_count;
    int new_n = n + 1;
    *dead_id = n;
    *out_count = new_n;

    DFAState *new_states = (DFAState *)malloc((size_t)new_n * sizeof(DFAState));
    if (!new_states) return NULL;

    /* 拷贝原有状态 */
    for (int i = 0; i < n; i++) {
        new_states[i].id = i;
        new_states[i].is_accept = states[i].is_accept;
        new_states[i].transitions = (int *)malloc(256 * sizeof(int));
        if (!new_states[i].transitions) {
            for (int j = 0; j < i; j++) free(new_states[j].transitions);
            free(new_states);
            return NULL;
        }
        for (int c = 0; c < 256; c++) {
            int t = states[i].transitions[c];
            new_states[i].transitions[c] = (t == -1) ? *dead_id : t;
        }
    }

    /* 创建死状态 */
    new_states[n].id = n;
    new_states[n].is_accept = 0;
    new_states[n].transitions = (int *)malloc(256 * sizeof(int));
    if (!new_states[n].transitions) {
        for (int j = 0; j < n; j++) free(new_states[j].transitions);
        free(new_states);
        return NULL;
    }
    for (int c = 0; c < 256; c++) {
        new_states[n].transitions[c] = n;  /* 自环 */
    }

    return new_states;
}

/**
 * 构建反向边表。
 * rev[c * state_count + t] 是所有通过字符 c 转移到状态 t 的源状态链表。
 *
 * @param states      DFA 状态数组
 * @param state_count 状态总数
 * @return  反向边表（rev[node_count] 数组，node_count = 256 * state_count），
 *          调用者负责释放每个链表节点和数组本身。
 */
static RevNode **build_rev_lists(const DFAState *states, int state_count) {
    int n = state_count;
    int node_count = 256 * n;
    RevNode **rev = (RevNode **)calloc((size_t)node_count, sizeof(RevNode *));
    if (!rev) return NULL;

    for (int s = 0; s < n; s++) {
        for (int c = 0; c < 256; c++) {
            int t = states[s].transitions[c];
            RevNode *node = (RevNode *)malloc(sizeof(RevNode));
            if (!node) {
                for (int i = 0; i < node_count; i++) {
                    RevNode *p = rev[i];
                    while (p) { RevNode *next = p->next; free(p); p = next; }
                }
                free(rev);
                return NULL;
            }
            node->from = s;
            node->next = rev[c * n + t];
            rev[c * n + t] = node;
        }
    }
    return rev;
}

/** 释放反向边表 */
static void free_rev_lists(RevNode **rev, int state_count) {
    int node_count = 256 * state_count;
    for (int i = 0; i < node_count; i++) {
        RevNode *p = rev[i];
        while (p) { RevNode *next = p->next; free(p); p = next; }
    }
    free(rev);
}

/* ========================================================================== */
/*  dfa_minimize — 公共入口                                                   */
/* ========================================================================== */

void dfa_minimize(DFAMachine *dfa) {
    if (!dfa || !dfa->states || dfa->state_count <= 1) return;

    int old_n = dfa->state_count;
    int n, dead_id;
    DFAState *work = dfa_copy_with_dead(dfa->states, old_n, &n, &dead_id);
    if (!work) return;

    RevNode **rev = build_rev_lists(work, n);
    if (!rev) {
        for (int i = 0; i < n; i++) free(work[i].transitions);
        free(work);
        return;
    }

    /* ==== 初始化分区 ==== */
    int *block_of = (int *)malloc((size_t)n * sizeof(int));
    IntVec *blocks_vec = NULL;
    int block_count = 0;
    int blocks_cap = 3;  /* 初始容量：非接受 / 接受 / 死状态 */
    if (!block_of) goto oom;

    blocks_vec = (IntVec *)calloc((size_t)blocks_cap, sizeof(IntVec));
    if (!blocks_vec) goto oom;

    {
        int nacc = 0, acc = 0;
        for (int i = 0; i < n; i++) {
            if (i == dead_id) continue;
            if (work[i].is_accept) acc++; else nacc++;
        }

        /* 块 0：非接受态（不含死状态） */
        if (nacc > 0) {
            blocks_vec[0] = int_vec_new(nacc);
            for (int i = 0; i < n; i++) {
                if (i != dead_id && !work[i].is_accept) {
                    int_vec_push(&blocks_vec[0], i);
                    block_of[i] = 0;
                }
            }
            block_count = 1;
        }

        /* 块 k：接受态 */
        if (acc > 0) {
            int bid = block_count;
            blocks_vec[bid] = int_vec_new(acc);
            for (int i = 0; i < n; i++) {
                if (i != dead_id && work[i].is_accept) {
                    int_vec_push(&blocks_vec[bid], i);
                    block_of[i] = bid;
                }
            }
            block_count++;
        }

        /* 死状态单独一块 */
        {
            int bid = block_count;
            blocks_vec[bid] = int_vec_new(1);
            int_vec_push(&blocks_vec[bid], dead_id);
            block_of[dead_id] = bid;
            block_count++;
        }
    }

    /* ==== 工作队列（环形缓冲区，可动态扩容） ==== */
    int w_cap = block_count + 256;
    int *W = (int *)malloc((size_t)w_cap * sizeof(int));
    int wh = 0, wt = 0;
    if (!W) goto oom;
    /* 把较小的块入队（跳过死状态块） */
    for (int b = 0; b < block_count - 1; b++) {
        W[wt++] = b;
    }

    /* ==== 预分配临时数组 ==== */
    int *in_preimage = (int *)calloc((size_t)n, sizeof(int));
    IntVec preimage = int_vec_new(256);
    if (!in_preimage) goto oom;

    /* ==== 主循环 ==== */
    while (wh < wt) {
        int A = W[wh++];

        for (int c = 0; c < 256; c++) {
            /* 收集 preimage：所有通过 c 转移到块 A 中状态的前驱 */
            preimage.count = 0;
            for (int i = 0; i < blocks_vec[A].count; i++) {
                int t = blocks_vec[A].data[i];
                for (RevNode *rn = rev[c * n + t]; rn; rn = rn->next) {
                    int src = rn->from;
                    if (!in_preimage[src]) {
                        in_preimage[src] = 1;
                        int_vec_push(&preimage, src);
                    }
                }
            }

            /* 对每个与 preimage 有交集的块 B，执行分裂 */
            int block_count_snapshot = block_count;
            for (int B = 0; B < block_count_snapshot; B++) {
                if (blocks_vec[B].count == 0) continue;

                int split_count = 0;
                for (int i = 0; i < blocks_vec[B].count; i++) {
                    if (in_preimage[blocks_vec[B].data[i]]) split_count++;
                }
                if (split_count == 0 || split_count == blocks_vec[B].count) continue;

                /* 分裂 B 为 B1 = B∩preimage, B2 = B\preimage */
                int B1 = B;                          /* 复用原块 */

                /* 确保 blocks_vec 容量足够 */
                if (block_count >= blocks_cap) {
                    blocks_cap *= 2;
                    IntVec *tmp = (IntVec *)realloc(blocks_vec,
                        (size_t)blocks_cap * sizeof(IntVec));
                    if (!tmp) goto oom;
                    blocks_vec = tmp;
                    memset(blocks_vec + block_count, 0,
                           (size_t)(blocks_cap - block_count) * sizeof(IntVec));
                }
                int B2 = block_count++;              /* 新建块 */

                IntVec old_B = blocks_vec[B];
                blocks_vec[B1] = int_vec_new(split_count);
                blocks_vec[B2] = int_vec_new(old_B.count - split_count);

                for (int i = 0; i < old_B.count; i++) {
                    int s = old_B.data[i];
                    if (in_preimage[s]) {
                        int_vec_push(&blocks_vec[B1], s);
                        block_of[s] = B1;
                    } else {
                        int_vec_push(&blocks_vec[B2], s);
                        block_of[s] = B2;
                    }
                }
                int_vec_free(&old_B);

                /* 将较小的块加入工作队列 */
                if (wt >= w_cap) {
                    w_cap *= 2;
                    int *tmp_w = (int *)realloc(W, (size_t)w_cap * sizeof(int));
                    if (!tmp_w) goto oom;
                    W = tmp_w;
                }
                if (blocks_vec[B1].count <= blocks_vec[B2].count) {
                    W[wt++] = B1;
                } else {
                    W[wt++] = B2;
                }
            }

            /* 清理 preimage 标记 */
            for (int i = 0; i < preimage.count; i++) {
                in_preimage[preimage.data[i]] = 0;
            }
        }
    }

    /* ==== 重建最小 DFA ==== */
    {
        int dead_block = block_of[dead_id];

        int new_count = 0;
        for (int b = 0; b < block_count; b++) {
            if (b != dead_block && blocks_vec[b].count > 0) new_count++;
        }

        int *block_to_new = (int *)malloc((size_t)block_count * sizeof(int));
        if (!block_to_new) goto oom;

        int new_id = 0;
        for (int b = 0; b < block_count; b++) {
            if (b == dead_block || blocks_vec[b].count == 0) {
                block_to_new[b] = -1;
            } else {
                block_to_new[b] = new_id++;
            }
        }

        DFAState *new_states = (DFAState *)malloc((size_t)new_count * sizeof(DFAState));
        if (!new_states) { free(block_to_new); goto oom; }

        for (int b1 = 0; b1 < block_count; b1++) {
            int ns = block_to_new[b1];
            if (ns == -1) continue;

            int rep = blocks_vec[b1].data[0];
            new_states[ns].id = ns;
            new_states[ns].is_accept = work[rep].is_accept;
            new_states[ns].transitions = (int *)malloc(256 * sizeof(int));
            if (!new_states[ns].transitions) {
                for (int j = 0; j < ns; j++) free(new_states[j].transitions);
                free(new_states);
                free(block_to_new);
                goto oom;
            }

            for (int c = 0; c < 256; c++) {
                int t = work[rep].transitions[c];
                int tb = block_of[t];
                new_states[ns].transitions[c] = block_to_new[tb];  /* 死块 → -1 */
            }
        }

        int new_start = block_to_new[block_of[dfa->start_state]];

        for (int i = 0; i < old_n; i++) {
            free(dfa->states[i].transitions);
        }
        free(dfa->states);

        dfa->states      = new_states;
        dfa->state_count = new_count;
        dfa->start_state = new_start;

        free(block_to_new);
    }

    /* ==== 清理 ==== */
oom:
    if (work) {
        for (int i = 0; i < n; i++) free(work[i].transitions);
        free(work);
    }
    if (rev) free_rev_lists(rev, n);
    free(block_of);
    if (blocks_vec) {
        for (int b = 0; b < block_count; b++) int_vec_free(&blocks_vec[b]);
        free(blocks_vec);
    }
    free(W);
    free(in_preimage);
    int_vec_free(&preimage);
}
