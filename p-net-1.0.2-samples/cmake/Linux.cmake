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
  ports/linux
  )

target_sources(pn_dev
  PRIVATE
  ports/linux/sampleapp_main.c
  )

target_compile_options(pn_dev
  PRIVATE
  -Wall
  -Wextra
  -Werror
  -Wno-unused-parameter
  -ffunction-sections
  -fdata-sections
  )

target_link_options(pn_dev
   PRIVATE
   -Wl,--gc-sections
)

target_link_libraries(pn_dev PUBLIC osal pthread rt)
