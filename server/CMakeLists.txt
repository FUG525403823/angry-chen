cmake_minimum_required(VERSION 3.28)
project(ac_server VERSION 0.1.0 LANGUAGES CXX)

# S01 §5.2 冻结编译开关：C++20、Release -O2、禁快速数学与隐式 FMA、警告即错误。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AC_WERROR "把编译警告当错误（CI 恒为 ON）" ON)

# S15 §3/§5：发布版号由构建注入。它同时是 --version 行与 /metrics 的 ac_server_version 标签的唯一来源。
set(AC_SERVER_VERSION "0.1.0" CACHE STRING "服务端语义化版本（MAJOR.MINOR.PATCH，无前缀）")

if(MSVC)
  add_compile_options(/W4 /permissive- /fp:strict)
  if(AC_WERROR)
    add_compile_options(/WX)
  endif()
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi" CACHE STRING "" FORCE)
else()
  add_compile_options(-Wall -Wextra -ffp-contract=off -fno-fast-math)
  if(AC_WERROR)
    add_compile_options(-Werror)
  endif()
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
  set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g" CACHE STRING "" FORCE)
endif()

# ac_core = src/ 下全部实现文件（main.cpp 归 ac_server）；后续计划的 sim/net/ai/combat/room/persist 自动纳入。
file(GLOB_RECURSE AC_CORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM AC_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

add_library(ac_core STATIC ${AC_CORE_SOURCES})
target_include_directories(ac_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(ac_core PUBLIC AC_SERVER_VERSION="${AC_SERVER_VERSION}")

# S04 §5.7：WinSock2 需要显式链接 ws2_32（POSIX 侧无需额外库）。
if(WIN32)
  target_link_libraries(ac_core PUBLIC ws2_32)
endif()

add_executable(ac_server "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(ac_server PRIVATE ac_core)

# S15 §5 目录布局：安装出来就叫 angry-chen-server（/opt/angry-chen/bin/ 下的唯一可执行文件）。
# 用 install(PROGRAMS ...) 而不是 set_target_properties(OUTPUT_NAME)：后者连构建产物一起改名，
# 会打断 build.ps1 的产物断言与计划 §6-1 的 `server/build/ac_server --version`。
install(PROGRAMS $<TARGET_FILE:ac_server> DESTINATION bin RENAME angry-chen-server)

# S14 §3：压测与门禁工具（tools/ 在 src/ 之外，不会被 ac_core 的 glob 吸进去）。
foreach(AC_TOOL IN ITEMS bot bench gate)
  add_executable(ac_${AC_TOOL} "${CMAKE_CURRENT_SOURCE_DIR}/tools/${AC_TOOL}.cpp")
  target_link_libraries(ac_${AC_TOOL} PRIVATE ac_core)
endforeach()
if(WIN32)
  # gate 采样 RSS 需要 psapi（GetProcessMemoryInfo）。
  target_link_libraries(ac_gate PRIVATE psapi)
endif()

# tests/ 下的每个 .cpp 都是 ac_tests 的翻译单元；入口 main() 只在 main_test.cpp，
# 后续计划的测试文件只写 AC_TEST 用例（§9 消费约定）。
file(GLOB AC_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
add_executable(ac_tests ${AC_TEST_SOURCES})
target_include_directories(ac_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(ac_tests PRIVATE ac_core)

# fixture 目录用绝对路径注入：ctest 的工作目录是构建目录，相对路径找不到 server/tests/fixtures（S03 §5.5）。
target_compile_definitions(ac_tests PRIVATE "AC_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures\"")
# S07 §5.5：对拍向量在 docs/evidence/fixtures（仓库根的 docs，不在 server/ 下），同样注入绝对路径。
target_compile_definitions(ac_tests PRIVATE "AC_EVIDENCE_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/../docs/evidence/fixtures\"")

enable_testing()
add_test(NAME unit COMMAND ac_tests)
