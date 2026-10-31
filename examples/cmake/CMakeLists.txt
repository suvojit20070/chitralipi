# A separate project that uses the installed Chitralipi.
#   cmake -S . -B build && cmake --build build && ./build/hello
cmake_minimum_required(VERSION 3.16)
project(chitralipi_hello C)

find_package(chitralipi 1 REQUIRED)

add_executable(hello ../c/hello.c)
target_link_libraries(hello PRIVATE chitralipi::chitralipi)
