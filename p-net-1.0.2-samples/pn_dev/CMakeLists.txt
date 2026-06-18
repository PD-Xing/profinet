#********************************************************************
#        _       _         _
#  _ __ | |_  _ | |  __ _ | |__   ___
# | '__|| __|(_)| | / _` || '_ \ / __|
# | |   | |_  _ | || (_| || |_) |\__ \
# |_|    \__|(_)|_| \__,_||_.__/ |___/
#
# http://www.rt-labs.com
# Copyright 2017 rt-labs AB, Sweden.
# See LICENSE file in the project root for full license information.
#*******************************************************************/

if (NOT BUILD_FUZZ)
  set_target_properties (pn_dev
    PROPERTIES
    C_STANDARD 99
    )

  target_include_directories(pn_dev
    PRIVATE
    .
    )

  target_sources(pn_dev
    PRIVATE
    sampleapp_common.c
    app_utils.c
    app_log.c
    app_gsdml.c
    app_data.c
    )

  target_link_libraries (pn_dev PUBLIC pnet)

  install (TARGETS pn_dev DESTINATION bin)
endif()
