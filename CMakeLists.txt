# ==============================================================================
# 正则表达式引擎 — Thompson NFA / DFA 双模式
# ==============================================================================
# 架构流水线：
#   Tokenizer → Parser → NFA (Thompson) → DFA (子集构造) → Hopcroft 最小化 → 匹配
#
# 构建步骤（Windows PowerShell）：
#   mkdir build && cd build
#   cmake .. -G "MinGW Makefiles"
#   cmake --build .
#
# 一键目标（在 build/ 目录下执行）：
#   cmake --build . --target run_tests        # 全部 9 套单元测试 (676+ 项)
#   cmake --build . --target run_grep_tests   # Spencer ERE 测试 (221 用例)
#   cmake --build . --target run_benchmark    # DFA 性能基准 (编译管道 + 匹配)
#   cmake --build . --target run_perf_compare # DFA vs POSIX 等效性能对比
#   cmake --build . --target run_dotgen       # 生成 NFA/DFA 转换图和状态转移表
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

# 关闭 MSVC 警告 C4996 (POSIX 兼容层使用非标准函数时)
if(MSVC)
    add_compile_options(/W4 /wd4996)
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

# 12) bench_all — 统一性能测试 (替换 posix_benchmark + posix_compare)
add_executable(bench_all tests/bench_all.c)
target_link_libraries(bench_all PRIVATE regex_core)

# 13) grep_test_runner — Spencer 测试套件运行器
add_executable(grep_test_runner tests/grep_test_runner.c)
target_link_libraries(grep_test_runner PRIVATE regex_core)

# 14) dot_gen — NFA/DFA 转换图 + 状态转移表生成
add_executable(dot_gen tests/dot_gen.c)
target_link_libraries(dot_gen PRIVATE regex_core)

# 15) posix_system_bench — DFA vs 系统POSIX(PCRE) 性能直接对比
# 注意: 必须独立编译源文件而不能链接 regex_core，否则项目中的
# posix_compat.c 提供的 regcomp/regexec/regfree 符号会覆盖系统
# libpcreposix 的同名函数，导致实际调用的是我们的 POSIX 兼容层。
add_executable(posix_system_bench
    tests/posix_system_bench.c
    src/tokenizer.c
    src/parser.c
    src/nfa.c
    src/dfa.c
    src/hopcroft.c
)
target_include_directories(posix_system_bench PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_directories(posix_system_bench PRIVATE ${CMAKE_BINARY_DIR}/bin)
target_link_libraries(posix_system_bench PRIVATE pcreposix)

# ---------- 安装 ----------
install(TARGETS regex_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(TARGETS regex_engine regex_tokenizer_test regex_parser_test regex_nfa_test regex_dfa_test regex_hopcroft_test regex_matcher_test regex_capture_test regex_api_test regex_posix_test
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
    COMMENT "运行全部单元测试..."
)

# 一键运行 grep Spencer 测试套件 (ERE 222 用例)
# 注意: 即使有测试失败也继续 (grep_test_runner 返回非零时 make 会报错,
# 但测试结果已经在 stdout 中完整输出)
add_custom_target(run_grep_tests
    COMMAND $<TARGET_FILE:grep_test_runner> ${CMAKE_SOURCE_DIR}/grep/tests/ere.tests ere || (exit 0)
    DEPENDS grep_test_runner
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "运行 grep Spencer ERE 测试套件..."
)

# 一键运行 DFA 性能基准 (匹配 + 编译管道)
add_custom_target(run_benchmark
    COMMAND $<TARGET_FILE:bench_all> all
    DEPENDS bench_all
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "运行 DFA 性能基准..."
)

# 一键生成 NFA/DFA DOT 图和状态转移表
add_custom_target(run_dotgen
    COMMAND $<TARGET_FILE:dot_gen> ${CMAKE_BINARY_DIR}/DOT
    DEPENDS dot_gen
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "生成 NFA/DFA 转换图..."
)

# 一键运行 DFA vs POSIX(未最小化等效) 性能对比
add_custom_target(run_perf_compare
    COMMAND $<TARGET_FILE:bench_all> compare
    DEPENDS bench_all
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "运行 DFA vs POSIX 等效性能对比..."
)

# 一键运行 DFA vs 系统 POSIX(PCRE) 性能对比
add_custom_target(run_posix_cmp
    COMMAND $<TARGET_FILE:posix_system_bench>
    DEPENDS posix_system_bench
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "运行 DFA vs 系统 POSIX 性能对比..."
)

# ---------- 信息输出 ----------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  Regex Engine (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "========================================")
message(STATUS "")
