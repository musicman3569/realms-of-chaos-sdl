# CMake toolchain file for the Miyoo Mini (Plus): a Cortex-A7 (SigmaStar SSD202D) with NEON,
# hard-float, through the toolchain in /opt/miyoomini-toolchain (see Dockerfile).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(MIYOO_TOOLCHAIN /opt/miyoomini-toolchain/usr)
set(CMAKE_C_COMPILER ${MIYOO_TOOLCHAIN}/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_STRIP ${MIYOO_TOOLCHAIN}/bin/arm-linux-gnueabihf-strip)
set(CMAKE_SYSROOT ${MIYOO_TOOLCHAIN}/arm-linux-gnueabihf/sysroot)

set(CMAKE_C_FLAGS_INIT "-marm -mcpu=cortex-a7 -mtune=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard")
# SDL2 needs the vendor's libcam_os_wrapper.so, which only the handheld has (in /config/lib), and
# libneonarmmiyoo.so, which comes with it
set(CMAKE_EXE_LINKER_FLAGS_INIT "-Wl,--allow-shlib-undefined -Wl,-rpath-link,/opt/sdl2-miyoo/lib")

set(CMAKE_FIND_ROOT_PATH /opt/sdl2-miyoo)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
