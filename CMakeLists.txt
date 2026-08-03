cmake_minimum_required(VERSION 3.16)
project(tls_test CXX)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_BUILD_RPATH "${CMAKE_BINARY_DIR}")

add_library(foo SHARED libfoo.cpp)

add_executable(dynamic_app main_dynamic.cpp)
target_link_libraries(dynamic_app PRIVATE foo pthread)


add_executable(dlopen_app main_dlopen.cpp)
target_link_libraries(dlopen_app PRIVATE pthread)

if(APPLE)
    # dlopen lives in libSystem; there is no libdl to link.
    # A bare leaf name is not resolved via rpath or cwd by dyld, so give
    # dlopen() a @loader_path-relative path instead.
    target_compile_definitions(dlopen_app PRIVATE
                               LIBFOO_NAME="@loader_path/libfoo.dylib")
else()
    target_link_libraries(dlopen_app PRIVATE dl)
    target_compile_definitions(dlopen_app PRIVATE LIBFOO_NAME="libfoo.so")
endif()
