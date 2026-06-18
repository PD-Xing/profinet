# CMake toolchain file for OK3576 (aarch64)
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Cross compiler path - use local toolchain
set(TOOLCHAIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../toolchain/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu)

set(CMAKE_C_COMPILER   ${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/bin/aarch64-none-linux-gnu-g++)

# Sysroot - use the buildroot output or SDK sysroot
set(CMAKE_FIND_ROOT_PATH ${TOOLCHAIN_PATH}/aarch64-none-linux-gnu)

# Search for programs in the build host directories
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# Search for libraries and headers in the target directories
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
