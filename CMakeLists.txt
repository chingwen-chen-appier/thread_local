cmake_minimum_required(VERSION 3.16)
project(tls_test CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}")

add_library(foo SHARED libfoo.cpp)

add_executable(dynamic_app main_dynamic.cpp)
target_link_libraries(dynamic_app PRIVATE foo pthread)


add_executable(dlopen_app main_dlopen.cpp)
target_link_libraries(dlopen_app PRIVATE dl pthread)
