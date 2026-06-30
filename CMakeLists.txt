# ==============================================================================
# 正则表达式引擎（NFA/DFA 双模式）
# ==============================================================================
# 课程：AI辅助C语言实践 · 题目09 · 两人小组
#
# 构建步骤（Windows PowerShell）：
#   cd regex-engine
#   mkdir build && cd build
#   cmake .. -G "MinGW Makefiles"   # 或用 "Visual Studio 17 2022" 等生成器
#   cmake --build .
#
# 运行：
#   ./bin/regex_engine               # 交互式演示
#   ./bin/regex_engine "a*b" "aaab"  # 命令行参数模式
#   ./bin/regex_engine --test        # 运行内置测试套件
#   ./bin/regex_engine --bench       # 运行性能基准测试
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

# ---------- 自动收集源文件 ----------
file(GLOB_RECURSE SOURCES
    "src/*.c"
)

file(GLOB_RECURSE HEADERS
    "include/*.h"
)

# ---------- 静态库：core（核心库） ----------
add_library(regex_core STATIC ${SOURCES})
target_include_directories(regex_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ---------- 可执行程序 ----------

# 1) regex_engine — 主程序
add_executable(regex_engine src/main.c)
target_link_libraries(regex_engine PRIVATE regex_core)

# 2) regex_test — 词法分析器测试
add_executable(regex_test tests/test_tokenizer.c)
target_include_directories(regex_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(regex_test PRIVATE regex_core)

# 3) regex_parser_test — 语法解析器测试
add_executable(regex_parser_test tests/test_parser.c)
target_include_directories(regex_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(regex_parser_test PRIVATE regex_core)

# ---------- 安装 ----------
install(TARGETS regex_core
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)
install(TARGETS regex_engine regex_test regex_parser_test
    RUNTIME DESTINATION bin
)
install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# ---------- 自定义目标：一键测试 ----------
add_custom_target(run_tests
    COMMAND $<TARGET_FILE:regex_test>
    COMMAND $<TARGET_FILE:regex_parser_test>
    DEPENDS regex_test regex_parser_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running test suite..."
)

# ---------- 信息输出 ----------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  Regex Engine (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
message(STATUS "  C Standard: ${CMAKE_C_STANDARD}")
message(STATUS "========================================")
message(STATUS "")
