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


target_include_directories(pn_dev
  PRIVATE
  ports/rt-kernel
  )

if (EXISTS ports/rt-kernel/sampleapp_${BSP}.c)
  set(BSP_SOURCE sampleapp_${BSP}.c)
else()
  set(BSP_SOURCE sampleapp_bsp.c)
endif()

target_sources(pn_dev
  PRIVATE
  ports/rt-kernel/sampleapp_main.c
  ports/rt-kernel/${BSP_SOURCE}
  )

target_compile_options(pn_dev
  PRIVATE
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  )
