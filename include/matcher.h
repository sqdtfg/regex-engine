#ifndef REGEX_MATCHER_H
#define REGEX_MATCHER_H

#include "dfa.h"

/* ========================================================================== */
/*  匹配器兼容层                                                                */
/*                                                                            */
/*  所有匹配函数声明（dfa_match、dfa_match_full、dfa_match_all）均已合并到       */
/*  dfa.h。本头文件为兼容存根，仅 #include "dfa.h" 以保持现有调用者编译通过。    */
/*                                                                            */
/*  新代码请直接 #include "dfa.h"。                                             */
/* ========================================================================== */

#endif /* REGEX_MATCHER_H */
