cmake_minimum_required (VERSION 3.16)
project(sandbox)

find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED) # https://stackoverflow.com/a/29316084/868014
pkg_check_modules(SDL2 REQUIRED sdl2)

pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(GST REQUIRED gstreamer-1.0)
pkg_search_module(GST_BASE REQUIRED gstreamer-plugins-base-1.0)

add_executable(sandbox sandbox.cpp)
set_target_properties(sandbox PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)

target_link_libraries(sandbox Threads::Threads)

target_compile_options(sandbox PRIVATE ${SDL2_CFLAGS_OTHER})
target_include_directories(sandbox PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(sandbox ${SDL2_LIBRARIES})

target_include_directories(sandbox PRIVATE ${GST_INCLUDE_DIRS} ${GST_BASE_INCLUDE_DIRS} "/usr/lib/arm-linux-gnueabihf/gstreamer-1.0/include")
target_link_directories(sandbox PRIVATE ${GST_LIBRARY_DIRS} ${GST_BASE_LIBRARY_DIRS})
target_link_libraries(sandbox glib-2.0 gobject-2.0 gstreamer-1.0 gstbase-1.0 gstvideo-1.0)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT sandbox)

# \gst-build\gst-plugins-base\tests\examples\gl\generic\cube\main.cpp

add_executable(cube main.cpp)

target_link_libraries(cube Threads::Threads)

target_compile_options(cube PRIVATE ${SDL2_CFLAGS_OTHER})
target_include_directories(cube PRIVATE ${SDL2_INCLUDE_DIRS})
target_link_libraries(cube ${SDL2_LIBRARIES})

target_link_libraries(cube GL)

target_include_directories(cube PRIVATE ${GST_INCLUDE_DIRS} ${GST_BASE_INCLUDE_DIRS} "/usr/lib/arm-linux-gnueabihf/gstreamer-1.0/include")
target_link_directories(cube PRIVATE ${GST_LIBRARY_DIRS} ${GST_BASE_LIBRARY_DIRS})
target_link_libraries(cube glib-2.0 gobject-2.0 gstreamer-1.0 gstbase-1.0 gstvideo-1.0 gstgl-1.0)
