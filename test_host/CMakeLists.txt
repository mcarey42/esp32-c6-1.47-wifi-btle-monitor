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

include_directories(
    ../components/wlogger_types/include
    ../components/wlogger_csv/include
)

add_library(wlogger_csv_host STATIC ../components/wlogger_csv/wlogger_csv.c)
target_include_directories(wlogger_csv_host PUBLIC
    ../components/wlogger_types/include
    ../components/wlogger_csv/include
    host_shims)

add_executable(test_csv test_csv.c)
target_link_libraries(test_csv unity wlogger_csv_host)
add_test(NAME csv COMMAND test_csv)

include_directories(../components/wlogger_parsers/include)
add_library(wlogger_parsers_host STATIC ../components/wlogger_parsers/wlogger_parsers.c)
target_include_directories(wlogger_parsers_host PUBLIC
    ../components/wlogger_parsers/include host_shims)

add_executable(test_parsers test_parsers.c)
target_link_libraries(test_parsers unity wlogger_parsers_host)
add_test(NAME parsers COMMAND test_parsers)
