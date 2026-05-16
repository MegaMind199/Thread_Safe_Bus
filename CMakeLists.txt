cmake_minimum_required(VERSION 3.20)
project(ThreadSafeBus C)

set(CMAKE_C_STANDARD 11)

add_executable(app
    main.c
    Src/thread_safe_bus.c
    Inc/thread_safe_bus.h
    Inc/status.h
)

target_include_directories(app PRIVATE Inc)