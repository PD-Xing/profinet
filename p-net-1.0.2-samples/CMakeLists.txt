#********************************************************************
#        _       _         _
#  _ __ | |_  _ | |  __ _ | |__   ___
# | '__|| __|(_)| | / _` || '_ \ / __|
# | |   | |_  _ | || (_| || |_) |\__ \
# |_|    \__|(_)|_| \__,_||_.__/ |___/
#
# www.rt-labs.com
# Copyright 2018 rt-labs AB, Sweden.
#
# This software is dual-licensed under GPLv3 and a commercial
# license. See the file LICENSE.md distributed with this software for
# full license information.
#*******************************************************************/

cmake_minimum_required (VERSION 3.28)
project(PNET_SAMPLES VERSION 1.0.2)

# Default to release build with debug info
if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
    "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)

message(STATUS "Building for ${CMAKE_SYSTEM_NAME} on ${CMAKE_SYSTEM_PROCESSOR}")

# Use pre-built libraries from p-net-1.0.2-Linux-aarch64
set(PNET_SDK_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../p-net-1.0.2-Linux-aarch64)

# Add imported library targets
add_library(pnet STATIC IMPORTED)
set_target_properties(pnet PROPERTIES
  IMPORTED_LOCATION ${PNET_SDK_PATH}/lib/libpnet.a
  INTERFACE_INCLUDE_DIRECTORIES "${PNET_SDK_PATH}/include"
  INTERFACE_LINK_LIBRARIES "osal;pthread;rt"
)

add_library(osal STATIC IMPORTED)
set_target_properties(osal PROPERTIES
  IMPORTED_LOCATION ${PNET_SDK_PATH}/lib/libosal.a
  INTERFACE_INCLUDE_DIRECTORIES "${PNET_SDK_PATH}/include"
)

# We cannot build fuzz-test executables and pn_dev at the same time, as this
# will create multiple definitions of main, as libFuzzer creates its own
# implementation of main
if (NOT BUILD_FUZZ)
# Add platform-dependent targets early, so they can be configured by
# platform
  add_executable(pn_dev "")
  set_target_properties(pn_dev PROPERTIES RUNTIME_OUTPUT_DIRECTORY "pn_dev")

  # Platform configuration
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/${CMAKE_SYSTEM_NAME}.cmake)

  add_subdirectory(pn_dev)
endif()
