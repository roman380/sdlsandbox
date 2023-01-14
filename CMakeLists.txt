cmake_minimum_required (VERSION 3.16)
project(sandbox)

find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED) # https://stackoverflow.com/a/29316084/868014
pkg_check_modules(SDL2 REQUIRED sdl2)

add_executable(sandbox sandbox.cpp)
set_target_properties(sandbox PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)

target_link_libraries(sandbox Threads::Threads)

target_compile_options(sandbox PRIVATE ${SDL2_CFLAGS_OTHER})
target_include_directories(sandbox PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(sandbox ${SDL2_LIBRARIES})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)
