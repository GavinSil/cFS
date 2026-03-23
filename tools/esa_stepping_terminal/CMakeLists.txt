cmake_minimum_required(VERSION 3.5)
project(esa_stepping_terminal C)

add_executable(esa_stepping_terminal
    src/main.c
    src/repl.c
    src/uds_client.c
)

target_compile_options(esa_stepping_terminal PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS esa_stepping_terminal DESTINATION host)
