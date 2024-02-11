cmake_minimum_required(VERSION 3.21)
project(cs_log C)

set(CMAKE_C_STANDARD 23)
find_package(SQLite3)

add_library(cs_log STATIC
        src/cs_log.c
        src/utils.c
        src/csl.h
)
target_compile_options(cs_log PRIVATE -Wall -Wpedantic -Werror)
target_include_directories(cs_log PUBLIC src)

add_executable(log_printer src/log_printer.c)
target_compile_options(log_printer PUBLIC -Wall -Wpedantic -Werror)
target_link_libraries(log_printer PUBLIC cs_log)

if(SQLite3_FOUND)
    target_link_libraries(log_printer PUBLIC SQLite::SQLite3 ${CMAKE_DL_LIBS})
    target_compile_definitions(log_printer PUBLIC SQLITE_AVAILABLE)
endif()

add_executable(example examples/example.c)
target_link_libraries(example PUBLIC cs_log)
