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

cmake_minimum_required (VERSION 3.14)

# p-net library options
set(PNET_MAX_AR 16 CACHE STRING "Number of connections")
set(PNET_MAX_API 1 CACHE STRING "Number of Application Processes")
set(PNET_MAX_CR 2 CACHE STRING "Per AR. 1 input and 1 output")
set(PNET_MAX_SLOTS 4 CACHE STRING "Per API. Should be > 1 to allow at least one I/O module")
set(PNET_MAX_SUBSLOTS 4 CACHE STRING "Per slot (3 needed for DAP)")
set(PNET_MAX_DFP_IOCR 0 CACHE STRING "Allowed values are 0 (zero) or 2")
set(PNET_MAX_PHYSICAL_PORTS 1 CACHE STRING "Max number of physical ports")
set(PNET_MAX_LOG_BOOK_ENTRIES 10 CACHE STRING "Max log book entries")
set(PNET_MAX_ALARMS 2 CACHE STRING "Per AR and queue")
set(PNET_MAX_ALARM_PAYLOAD_DATA_SIZE 1460 CACHE STRING "Max size of alarm payload")
set(PNET_MAX_DIAG_ITEMS 100 CACHE STRING "Total, per device")
set(PNET_MAX_DIAG_MANUF_DATA_SIZE 50 CACHE STRING "Max size of manufacturer specific diagnosis data")
set(PNET_MAX_AR_VENDOR_BLOCKS 0 CACHE STRING "Must be > 0")
set(PNET_MAX_AR_VENDOR_BLOCK_DATA_LENGTH 0 CACHE STRING "Max AR vendor block data length")
set(PNET_MAX_MAN_SPECIFIC_FAST_STARTUP_DATA_LENGTH 0 CACHE STRING "or 512 (bytes)")
set(PNET_MAX_SESSION_BUFFER_SIZE 1500 CACHE STRING "Max fragmented RPC request/response length")
set(PNET_MAX_DIRECTORYPATH_SIZE 256 CACHE STRING "Max directory path size")
set(PNET_MAX_FILENAME_SIZE 256 CACHE STRING "Max filename size")
set(PNET_MAX_PORT_DESCRIPTION_SIZE 256 CACHE STRING "Max port description size")
set(PNET_MAX_MC_CR 0 CACHE STRING "Per AR")

set(PNET_OPTION_FAST_STARTUP 0 CACHE BOOL "Enable fast startup")
set(PNET_OPTION_PARAMETER_SERVER 0 CACHE BOOL "Enable parameter server")
set(PNET_OPTION_IR 0 CACHE BOOL "Enable IR")
set(PNET_OPTION_SR 0 CACHE BOOL "Enable SR")
set(PNET_OPTION_REDUNDANCY 0 CACHE BOOL "Enable redundancy")
set(PNET_OPTION_AR_VENDOR_BLOCKS 0 CACHE BOOL "Enable AR vendor blocks")
set(PNET_OPTION_RS 0 CACHE BOOL "Enable RS")
set(PNET_OPTION_MC_CR 0 CACHE BOOL "Enable MC CR")
set(PNET_OPTION_SRL 0 CACHE BOOL "Enable SRL")
set(PNET_OPTION_SNMP 1 CACHE BOOL "Enable SNMP")
set(PNET_OPTION_DRIVER_ENABLE 0 CACHE BOOL "Enable driver")

set(PNET_USE_ATOMICS 1 CACHE BOOL "Use atomic operations")

set(LOG_LEVEL INFO CACHE STRING "Log level")
set(PF_ETH_LOG OFF CACHE STRING "PF ETH log")
set(PF_LLDP_LOG OFF CACHE STRING "PF LLDP log")
set(PF_SNMP_LOG OFF CACHE STRING "PF SNMP log")
set(PF_CPM_LOG OFF CACHE STRING "PF CPM log")
set(PF_PPM_LOG OFF CACHE STRING "PF PPM log")
set(PF_DCP_LOG OFF CACHE STRING "PF DCP log")
set(PF_RPC_LOG OFF CACHE STRING "PF RPC log")
set(PF_ALARM_LOG OFF CACHE STRING "PF ALARM log")
set(PF_AL_BUF_LOG OFF CACHE STRING "PF AL BUF log")
set(PF_PNAL_LOG OFF CACHE STRING "PF PNAL log")
set(PNET_LOG OFF CACHE STRING "PNET log")

# Generate version numbers
set(PNET_VERSION_MAJOR 1)
set(PNET_VERSION_MINOR 0)
set(PNET_VERSION_PATCH 2)

# Get git revision
execute_process(
  COMMAND git rev-parse --short HEAD
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  OUTPUT_VARIABLE PNET_GIT_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)

configure_file (
  ${CMAKE_CURRENT_SOURCE_DIR}/pnet_options.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/pnet_options.h
  )

configure_file (
  ${CMAKE_CURRENT_SOURCE_DIR}/pnet_version.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/pnet_version.h
  )

# Collect source files
file(GLOB PNET_COMMON_SOURCES src/common/*.c)
file(GLOB PNET_DEVICE_SOURCES src/device/*.c)

# Linux port sources
file(GLOB PNET_PORT_SOURCES src/ports/linux/*.c)

add_library(pnet
  ${PNET_COMMON_SOURCES}
  ${PNET_DEVICE_SOURCES}
  ${PNET_PORT_SOURCES}
  )

target_compile_features(pnet
  PUBLIC
  c_std_99
  )

target_include_directories(pnet
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ports/linux>
  )

target_link_libraries(pnet
  PUBLIC
  osal
  )

target_compile_options(pnet
  PRIVATE
  -Wall
  -Wextra
  -Wno-unused-parameter
  -ffunction-sections
  -fdata-sections
  )
