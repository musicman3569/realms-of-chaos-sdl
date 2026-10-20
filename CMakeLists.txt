cmake_minimum_required(VERSION 3.21)
project(roc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

find_package(SDL2 REQUIRED CONFIG)

add_executable(roc
    src/main.c
    src/archive.c
    src/gfx.c
    src/level.c
    src/engine.c
    src/setup.c
    src/behave.c
    src/png.c
)

if(TARGET SDL2::SDL2main)
    target_link_libraries(roc PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(roc PRIVATE SDL2::SDL2)

if(MSVC)
    target_compile_options(roc PRIVATE /W4)
else()
    target_compile_options(roc PRIVATE -Wall -Wextra -Wpedantic)
    if(MINGW)
        target_link_options(roc PRIVATE -static-libgcc)
    endif()
endif()

# Put SDL2.dll next to the executable so it runs outside the build shell.
if(WIN32)
    add_custom_command(TARGET roc POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_RUNTIME_DLLS:roc> $<TARGET_FILE_DIR:roc>
        COMMAND_EXPAND_LISTS)
endif()
