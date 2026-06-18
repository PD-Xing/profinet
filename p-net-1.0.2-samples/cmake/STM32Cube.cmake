#********************************************************************
#        _       _         _
#  _ __ | |_  _ | |  __ _ | |__   ___
# | '__|| __|(_)| | / _` || '_ \ / __|
# | |   | |_  _ | || (_| || |_) |\__ \
# |_|    \__|(_)|_| \__,_||_.__/ |___/
#
# www.rt-labs.com
# Copyright 2021 rt-labs AB, Sweden.
#
# This software is dual-licensed under GPLv3 and a commercial
# license. See the file LICENSE.md distributed with this software for
# full license information.
#*******************************************************************/

target_include_directories(pn_dev
  PRIVATE
  ports/STM32Cube
  )

if (EXISTS ports/STM32Cube/sampleapp_${BOARD}.c)
  set(BOARD_SOURCE sampleapp_${BOARD}.c)
else()
  set(BOARD_SOURCE sampleapp_board.c)
endif()

target_sources(pn_dev
  PRIVATE
  ports/STM32Cube/sampleapp_main.c
  ports/STM32Cube/${BOARD_SOURCE}
  )

target_compile_options(pn_dev
  PRIVATE
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  )

target_link_libraries(pn_dev
  PRIVATE
  cube-bsp
  )

generate_bin(pn_dev)
