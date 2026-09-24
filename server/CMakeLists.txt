cmake_minimum_required(VERSION 3.28)
project(ac_server VERSION 0.1.0 LANGUAGES CXX)

# S01 §5.2 冻结编译开关：C++20、Release -O2、禁快速数学与隐式 FMA、警告即错误。
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AC_WERROR "把编译警告当错误（CI 恒为 ON）" ON)

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

add_executable(ac_server "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
target_link_libraries(ac_server PRIVATE ac_core)

# tests/ 下的每个 .cpp 都是 ac_tests 的翻译单元；入口 main() 只在 main_test.cpp，
# 后续计划的测试文件只写 AC_TEST 用例（§9 消费约定）。
file(GLOB AC_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
add_executable(ac_tests ${AC_TEST_SOURCES})
target_include_directories(ac_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
target_link_libraries(ac_tests PRIVATE ac_core)

enable_testing()
add_test(NAME unit COMMAND ac_tests)
