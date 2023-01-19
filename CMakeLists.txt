cmake_minimum_required(VERSION 3.24)
project(Producer)

set(CMAKE_CXX_STANDARD 20)
add_library(catch catch/catch_amalgamated.cpp)
target_compile_definitions(catch PUBLIC CATCH_CONFIG_FAST_COMPILE)
target_link_options(catch PUBLIC -fuse-ld=lld)

add_subdirectory(fmt)

include_directories(catch)

#set(CMAKE_CXX_FLAGS "-Og")

add_executable(Producer main.cpp)
target_link_libraries(Producer catch fmt::fmt)
