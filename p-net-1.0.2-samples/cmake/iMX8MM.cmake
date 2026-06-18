#********************************************************************
#        _       _         _
#  _ __ | |_  _ | |  __ _ | |__   ___
# | '__|| __|(_)| | / _` || '_ \ / __|
# | |   | |_  _ | || (_| || |_) |\__ \
# |_|    \__|(_)|_| \__,_||_.__/ |___/
#
# www.rt-labs.com
# Copyright 2021 rt-labs AB, Sweden.
# Copyright 2023 NXP
#
# This software is dual-licensed under GPLv3 and a commercial
# license. See the file LICENSE.md distributed with this software for
# full license information.
#*******************************************************************/

target_include_directories(pn_dev
  PRIVATE
  ports/iMX8M
  )

target_sources(pn_dev
  PRIVATE
  ports/iMX8M/sampleapp_main.c
  ports/iMX8M/sampleapp_imx8mmevk.c
  )

target_link_libraries(pn_dev
  PRIVATE
  mcuxsdk-bsp
  )

generate_bin(pn_dev)
