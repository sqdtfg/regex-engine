# ==============================================================================
# 正则表达式引擎（NFA/DFA 双模式）
# ==============================================================================
# 课程：AI辅助C语言实践 · 题目09 · 两人小组
#
# 项目结构：
#   include/         头文件（dfa.h, nfa.h, parser.h, tokenizer.h, hopcroft.h, ...）
#   src/             源文件
#     dfa.c          子集构造法（NFA → DFA）
#     nfa.c          Thompson 构造法（AST → NFA）
#     parser.c       递归下降语法解析器
#     tokenizer.c    词法分析器（NFA-state → token）
#     hopcroft.c     Hopcroft DFA 最小化
#     matcher.c      DFA 匹配器（精确/子串/全局）
#     capture.c      捕获组支持
#     main.c         演示主程序
#   tests/           单元测试
#     test_tokenizer.c / test_parser.c / test_nfa.c /
#     test_dfa.c / test_hopcroft.c
#
# 构建步骤（Windows PowerShell）：
#   cd regex-engine
#   mkdir build && cd build
#   cmake .. -G "MinGW Makefiles"
#   cmake --build .
#
# 运行：
#   cmake --build . --target run_tests  # 一键运行全部测试
#   ./bin/regex_engine                  # 交互式演示
#   ./bin/regex_engine "a*b" "aaab"     # 命令行参数模式
#   ./bin/regex_hopcroft_test           # 最小化单元测试
# ==============================================================================

cmake_minimum_required(VERSION 3.16)
project(RegexEngine
    VERSION     1.0.0
    LANGUAGES   C
    DESCRIPTION "NFA/DFA dual-mode regular expression engine"
)

# ---------- 编译选项 ----------
set(CMAKE_C_STANDARD          11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS        OFF)

# Close MSVC warnings C4996 (POSIX compatibility layer) and C4819 (code page)
if(MSVC)
    add_compile_options(/W4 /wd4996 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY  ${CMAKE_BINARY_DIR}/lib)

# Windows: 强制 UTF-8 编码，解决控制台乱码
if(MINGW OR MSVC)
    add_compile_definitions(_UNICODE UNICODE)
endif()

# ---------- 核心源文件（仅用于静态库） ----------
file(GLOB_RECURSE CORE_SOURCES "src/*.c")
file(GLOB_RECURSE HEADERS "include/*.h")

# ---------- 静态库：core（核心库） ----------
add_library(regex_core STATIC ${CORE_SOURCES})
target_include_directories(regex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------- 可执行程序 ----------

# 2) regex_engine — 主程序（仅用 src/main.c）
add_executable(regex_engine src/main.c)
target_link_libraries(regex_engine PRIVATE regex_core)

# 3) regex_tokenizer_test — 词法分析器测试
add_executable(regex_tokenizer_test tests/test_tokenizer.c)
target_link_libraries(regex_tokenizer_test PRIVATE regex_core)

# 4) regex_parser_test — 语法解析器测试
add_executable(regex_parser_test tests/test_parser.c)
target_link_libraries(regex_parser_test PRIVATE regex_core)

# 5) regex_nfa_test — NFA Thompson 构造测试
add_executable(regex_nfa_test tests/test_nfa.c)
target_link_libraries(regex_nfa_test PRIVATE regex_core)

# 6) regex_dfa_test — DFA 子集构造 + 匹配测试
add_executable(regex_dfa_test tests/test_dfa.c)
target_link_libraries(regex_dfa_test PRIVATE regex_core)

# 7) regex_hopcroft_test — Hopcroft DFA 最小化测试
add_executable(regex_hopcroft_test tests/test_hopcroft.c)
target_link_libraries(regex_hopcroft_test PRIVATE regex_core)

# 8) regex_matcher_test — 匹配器测试
add_executable(regex_matcher_test tests/test_matcher.c)
target_link_libraries(regex_matcher_test PRIVATE regex_core)

# 9) regex_capture_test — 捕获组测试
add_executable(regex_capture_test tests/test_capture.c)
target_link_libraries(regex_capture_test PRIVATE regex_core)

# 10) regex_api_test — 高级 API 测试
add_executable(regex_api_test tests/test_api.c)
target_link_libraries(regex_api_test PRIVATE regex_core)

# 11) regex_posix_test — POSIX 兼容层测试
add_executable(regex_posix_test tests/test_posix_compat.c)
target_link_libraries(regex_posix_test PRIVATE regex_core)

# 12) benchmark — 性能基准测试（含 POSIX regex.h 对比）
add_executable(benchmark tests/benchmark.c)
target_link_libraries(benchmark PRIVATE regex_core ${CMAKE_DL_LIBS})
if(MINGW)
    # MinGW gnurx: regex.h lives in /opt/include, lib in /opt/lib
    target_include_directories(benchmark PRIVATE "D:/mingw64/opt/include")
    target_link_directories(benchmark PRIVATE "D:/mingw64/opt/lib")
    target_link_libraries(benchmark PRIVATE gnurx)
endif()

# 13) regex_grep_test — Grep 测试集适配
add_executable(regex_grep_test tests/test_grep.c)
target_link_libraries(regex_grep_test PRIVATE regex_core)

# 14) test_runner — 统一测试运行框架
add_executable(test_runner tests/test_runner.c)
target_link_libraries(test_runner PRIVATE regex_core)

# 15) gnu_grep_test — GNU grep 官方测试运行器
add_executable(gnu_grep_test tests/gnu_grep_test.c)
target_link_libraries(gnu_grep_test PRIVATE regex_core)

# ---------- 安装 ----------
install(TARGETS regex_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(TARGETS regex_engine regex_tokenizer_test regex_parser_test regex_nfa_test regex_dfa_test regex_hopcroft_test regex_matcher_test regex_capture_test regex_api_test regex_posix_test benchmark regex_grep_test test_runner gnu_grep_test
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ---------- 自定义目标：一键测试 ----------
add_custom_target(run_tests
    COMMAND $<TARGET_FILE:regex_tokenizer_test>
    COMMAND $<TARGET_FILE:regex_parser_test>
    COMMAND $<TARGET_FILE:regex_nfa_test>
    COMMAND $<TARGET_FILE:regex_dfa_test>
    COMMAND $<TARGET_FILE:regex_hopcroft_test>
    COMMAND $<TARGET_FILE:regex_matcher_test>
    COMMAND $<TARGET_FILE:regex_capture_test>
    COMMAND $<TARGET_FILE:regex_api_test>
    COMMAND $<TARGET_FILE:regex_posix_test>
    DEPENDS regex_tokenizer_test regex_parser_test regex_nfa_test regex_dfa_test regex_hopcroft_test regex_matcher_test regex_capture_test regex_api_test regex_posix_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running test suite..."
)

# ---------- 自定义目标：运行测试框架 ----------
add_custom_target(run_test_runner
    COMMAND $<TARGET_FILE:test_runner>
    DEPENDS test_runner regex_tokenizer_test regex_parser_test regex_nfa_test regex_dfa_test regex_hopcroft_test regex_matcher_test regex_capture_test regex_api_test regex_posix_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running complete test suite via test_runner..."
)

# ---------- 自定义目标：运行 GNU grep 官方测试 ----------
add_custom_target(run_gnu_grep_tests
    COMMAND $<TARGET_FILE:gnu_grep_test>
        ${CMAKE_SOURCE_DIR}/tests/gnu_grep/bre.tests
        ${CMAKE_SOURCE_DIR}/tests/gnu_grep/ere.tests
        ${CMAKE_SOURCE_DIR}/tests/gnu_grep/spencer1.tests
    DEPENDS gnu_grep_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running GNU grep official test suite..."
)

# ---------- 信息输出 ----------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  Regex Engine (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "========================================")
message(STATUS "")

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()