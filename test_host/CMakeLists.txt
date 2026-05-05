cmake_minimum_required(VERSION 3.16)
project(wlogger_host_tests C)
enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -O0 -g")

include_directories(unity host_shims)
add_library(unity STATIC unity/unity.c)

add_executable(test_smoke test_smoke.c)
target_link_libraries(test_smoke unity)
add_test(NAME smoke COMMAND test_smoke)
