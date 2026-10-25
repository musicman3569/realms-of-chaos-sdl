# SDL2::SDL2 for the Miyoo build: the prebuilt sdl2_miyoo library and its headers, as the
# Dockerfile lays them out in /opt/sdl2-miyoo (the release ships no CMake files of its own).
get_filename_component(_sdl2_prefix "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

if(NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 SHARED IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${_sdl2_prefix}/lib/libSDL2-2.0.so.0"
        IMPORTED_SONAME "libSDL2-2.0.so.0"
        INTERFACE_INCLUDE_DIRECTORIES "${_sdl2_prefix}/include/SDL2"
        INTERFACE_COMPILE_DEFINITIONS "_REENTRANT")
endif()

set(SDL2_FOUND TRUE)
set(SDL2_VERSION 2.0.20)
