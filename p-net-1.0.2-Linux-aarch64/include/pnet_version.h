/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2018 rt-labs AB, Sweden.
 *
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 ********************************************************************/

#ifndef PNET_VERSION_H
#define PNET_VERSION_H

#define PNET_GIT_REVISION "v1.0.2"

#if !defined(PNET_VERSION_BUILD) && defined(PNET_GIT_REVISION)
#define PNET_VERSION_BUILD PNET_GIT_REVISION
#endif

/* clang-format-off */

#define PNET_VERSION_MAJOR 1
#define PNET_VERSION_MINOR 0
#define PNET_VERSION_PATCH 2

#if defined(PNET_VERSION_BUILD)
#define PNET_VERSION \
   "1.0.2+" PNET_VERSION_BUILD
#else
#define PNET_VERSION \
   "1.0.2"
#endif

/* clang-format-on */

#endif /* PNET_VERSION_H */
