/*********************************************************************
 *        _       _         _
 *  _ __ | |_  _ | |  __ _ | |__   ___
 * | '__|| __|(_)| | / _` || '_ \ / __|
 * | |   | |_  _ | || (_| || |_) |\__ \
 * |_|    \__|(_)|_| \__,_||_.__/ |___/
 *
 * www.rt-labs.com
 * Copyright 2021 rt-labs AB, Sweden.
 *
 * This software is dual-licensed under GPLv3 and a commercial
 * license. See the file LICENSE.md distributed with this software for
 * full license information.
 ********************************************************************/

#include "app_data.h"
#include "app_utils.h"
#include "app_gsdml.h"
#include "app_log.h"
#include "sampleapp_common.h"
#include "osal.h"
#include "pnal.h"
#include <pnet_api.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* TCP bridge to motion controller */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>

#define PLC_BRIDGE_HOST     "127.0.0.1"
#define PLC_BRIDGE_PORT     31000

/* PLCFrame: 80-byte protocol shared with motion controller */
#pragma pack(push, 1)
typedef struct {
   uint8_t  bool_data[32];
   int16_t  int_data[8];
   float    real_data[8];
} PLCFrame;
#pragma pack(pop)

static int plc_sock_fd = -1;
static PLCFrame latest_robot_status;
static pthread_mutex_t status_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_t plc_thread_id;
static volatile bool plc_thread_running = true;

#define APP_DATA_DEFAULT_OUTPUT_DATA 0

/* Parameter data for digital submodules */
static uint32_t app_param_1 = 0;
static uint32_t app_param_2 = 0;
static uint32_t app_param_echo_gain = 1;

/* Digital submodule process data (1 byte = 8 bits for I1.0~I1.7 / Q1.0~Q1.7) */
static uint8_t inputdata[APP_GSDML_INPUT_DATA_DIGITAL_SIZE] = {0};
static uint8_t outputdata[APP_GSDML_OUTPUT_DATA_DIGITAL_SIZE] = {0};
static bool prev_button_state = false;

/* Echo submodule data */
static uint8_t echo_inputdata[APP_GSDML_INPUT_DATA_ECHO_SIZE] = {0};
static uint8_t echo_outputdata[APP_GSDML_OUTPUT_DATA_ECHO_SIZE] = {0};

/* Serial port for debug printing on OK3576 */
static FILE *serial_fp = NULL;
#define SERIAL_DEVICE "/dev/ttyFIQ0"

CC_PACKED_BEGIN
typedef struct CC_PACKED app_echo_data
{
   uint32_t echo_float_bytes;
   uint32_t echo_int;
} app_echo_data_t;
CC_PACKED_END
CC_STATIC_ASSERT (sizeof (app_echo_data_t) == APP_GSDML_INPUT_DATA_ECHO_SIZE);
CC_STATIC_ASSERT (sizeof (app_echo_data_t) == APP_GSDML_OUTPUT_DATA_ECHO_SIZE);

/**
 * Initialize serial port for debug output
 */
static int init_serial_port (void)
{
   char cmd[128];
   
   serial_fp = fopen (SERIAL_DEVICE, "w");
   if (serial_fp == NULL)
   {
      APP_LOG_WARNING ("Failed to open %s, using stdout for debug\n", SERIAL_DEVICE);
      serial_fp = stdout;
      return 0;
   }
   
   /* Configure serial port: 115200 8N1, raw mode */
   snprintf (cmd, sizeof (cmd), "stty -F %s 115200 raw -echo 2>/dev/null", SERIAL_DEVICE);
   system (cmd);
   
   APP_LOG_INFO ("Debug serial port %s initialized\n", SERIAL_DEVICE);
   return 0;
}

/**
 * TCP bridge thread: maintain persistent connection to motion controller.
 * The motion controller's PLCCommunicator uses a synchronous request-response
 * protocol: recv 80 bytes → send 80 bytes. We match this by doing send+recv
 * as a pair in app_data_set_output_data(), and cache the response here.
 *
 * This thread only does connection management (connect/reconnect).
 * Actual data I/O happens on the PROFINET callback thread.
 */
static void * plc_tcp_thread_fn (void * arg)
{
   (void)arg;
   struct sockaddr_in addr = {
      .sin_family = AF_INET,
      .sin_port = htons (PLC_BRIDGE_PORT),
   };
   inet_pton (AF_INET, PLC_BRIDGE_HOST, &addr.sin_addr);

   while (plc_thread_running)
   {
      int fd = socket (AF_INET, SOCK_STREAM, 0);
      if (fd < 0)
      {
         sleep (1);
         continue;
      }

      if (connect (fd, (struct sockaddr *)&addr, sizeof (addr)) != 0)
      {
         APP_LOG_WARNING (
            "TCP bridge: connect to %s:%d failed: %s\n",
            PLC_BRIDGE_HOST, PLC_BRIDGE_PORT, strerror (errno));
         close (fd);
         sleep (1);
         continue;
      }

      APP_LOG_INFO (
         "TCP bridge: connected to motion controller %s:%d\n",
         PLC_BRIDGE_HOST, PLC_BRIDGE_PORT);

      /* Publish the connected fd for the PROFINET callback to use */
      plc_sock_fd = fd;

      /* Just wait for disconnection. The motion controller will close
         the connection if it shuts down. We poll with a short sleep. */
      while (plc_thread_running && plc_sock_fd == fd)
      {
         sleep (1);
      }

      close (fd);
      if (plc_sock_fd == fd)
      {
         plc_sock_fd = -1;
      }
      APP_LOG_WARNING ("TCP bridge: disconnected, reconnecting...\n");
   }

   return NULL;
}

/**
 * Start TCP bridge thread. Called once at application startup.
 */
int app_plc_bridge_start (void)
{
   if (pthread_create (&plc_thread_id, NULL, plc_tcp_thread_fn, NULL) != 0)
   {
      APP_LOG_WARNING ("TCP bridge: failed to create thread\n");
      return -1;
   }
   return 0;
}

/**
 * Print I/O data to serial port ONLY when data has changed
 * Format: I1.0=x,I1.1=x,...,I1.7=x | Q1.0=x,Q1.1=x,...,Q1.7=x
 */
static void print_io_on_change (uint8_t input, uint8_t output)
{
   static uint8_t prev_input = 0xFF;
   static uint8_t prev_output = 0xFF;
   
   /* Skip if no change */
   if (input == prev_input && output == prev_output)
   {
      return;
   }
   
   fprintf (serial_fp,
            "I1.0=%d,I1.1=%d,I1.2=%d,I1.3=%d,I1.4=%d,I1.5=%d,I1.6=%d,I1.7=%d | "
            "Q1.0=%d,Q1.1=%d,Q1.2=%d,Q1.3=%d,Q1.4=%d,Q1.5=%d,Q1.6=%d,Q1.7=%d\n",
            (input >> 0) & 1, (input >> 1) & 1, (input >> 2) & 1, (input >> 3) & 1,
            (input >> 4) & 1, (input >> 5) & 1, (input >> 6) & 1, (input >> 7) & 1,
            (output >> 0) & 1, (output >> 1) & 1, (output >> 2) & 1, (output >> 3) & 1,
            (output >> 4) & 1, (output >> 5) & 1, (output >> 6) & 1, (output >> 7) & 1);
   fflush (serial_fp);
   
   prev_input = input;
   prev_output = output;
}

/**
 * Get input data (I1.0~I1.7) to send to PLC master
 * Called by p-net stack when it needs input data
 * 
 * Logic: I1.0 follows Q1.0 (echo), I1.1~I1.6 = 0, I1.7 = button state
 */
uint8_t * app_data_get_input_data (
   uint16_t slot_nbr,
   uint16_t subslot_nbr,
   uint32_t submodule_id,
   bool button_pressed,
   uint16_t * size,
   uint8_t * iops)
{
   if (size == NULL || iops == NULL)
   {
      return NULL;
   }

   /* PLC Frame Input: return latest robot status from TCP bridge */
   if (submodule_id == APP_GSDML_SUBMOD_ID_PLC_FRAME_IN)
   {
      static uint8_t status_buf[sizeof (PLCFrame)];

      pthread_mutex_lock (&status_mutex);
      memcpy (status_buf, &latest_robot_status, sizeof (PLCFrame));
      pthread_mutex_unlock (&status_mutex);

      *size = sizeof (PLCFrame);
      if (plc_sock_fd > 0)
      {
         *iops = PNET_IOXS_GOOD;
      }
      else
      {
         *iops = PNET_IOXS_BAD;
      }
      return status_buf;
   }

   if (submodule_id == APP_GSDML_SUBMOD_ID_DIGITAL_IN ||
       submodule_id == APP_GSDML_SUBMOD_ID_DIGITAL_IN_OUT)
   {
      /* Build input byte:
       * - bit0 (I1.0) = Q1.0 (echo from PLC output)
       * - bit1~bit6 (I1.1~I1.6) = 0
       * - bit7 (I1.7) = button state
       */
      prev_button_state = button_pressed;
      
      uint8_t new_input = 0;
      
      /* I1.0 follows Q1.0 */
      if (outputdata[0] & 0x01)
      {
         new_input |= 0x01;
      }
      
      /* I1.7 = button state */
      if (button_pressed)
      {
         new_input |= 0x80;
      }

      *size = APP_GSDML_INPUT_DATA_DIGITAL_SIZE;
      *iops = PNET_IOXS_GOOD;
      
      /* Print to serial only when input data changes */
      if (new_input != inputdata[0])
      {
         inputdata[0] = new_input;
         print_io_on_change (inputdata[0], outputdata[0]);
      }
      
      return inputdata;
   }

   if (submodule_id == APP_GSDML_SUBMOD_ID_ECHO)
   {
      app_echo_data_t *p_in = (app_echo_data_t *)echo_inputdata;
      app_echo_data_t *p_out = (app_echo_data_t *)echo_outputdata;
      float inputfloat, outputfloat;
      uint32_t host_in, host_out;

      /* Integer echo */
      p_in->echo_int = CC_TO_BE32 (
         CC_FROM_BE32 (p_out->echo_int) * CC_FROM_BE32 (app_param_echo_gain));

      /* Float echo */
      host_out = CC_FROM_BE32 (p_out->echo_float_bytes);
      memcpy (&outputfloat, &host_out, sizeof (outputfloat));
      inputfloat = outputfloat * CC_FROM_BE32 (app_param_echo_gain);
      memcpy (&host_in, &inputfloat, sizeof (inputfloat));
      p_in->echo_float_bytes = CC_TO_BE32 (host_in);

      *size = APP_GSDML_INPUT_DATA_ECHO_SIZE;
      *iops = PNET_IOXS_GOOD;
      return echo_inputdata;
   }

   return NULL;
}

/**
 * Set output data (Q1.0~Q1.7) received from PLC master
 * Called by p-net stack when output data arrives from master
 */
int app_data_set_output_data (
   uint16_t slot_nbr,
   uint16_t subslot_nbr,
   uint32_t submodule_id,
   uint8_t * data,
   uint16_t size)
{
   if (data == NULL)
   {
      return -1;
   }

   /* PLC Frame Output: forward to motion controller via TCP.
      Motion controller's PLCCommunicator protocol is synchronous:
      recv request → send response. We do send+recv as a pair. */
   if (submodule_id == APP_GSDML_SUBMOD_ID_PLC_FRAME_OUT)
   {
      if (size < sizeof (PLCFrame))
      {
         return -1;
      }

      if (plc_sock_fd > 0)
      {
         PLCFrame tx;
         memcpy (&tx, data, sizeof (PLCFrame));

         if (send (plc_sock_fd, &tx, sizeof (PLCFrame), MSG_NOSIGNAL) < 0)
         {
            APP_LOG_WARNING ("TCP bridge: send failed: %s\n", strerror (errno));
            close (plc_sock_fd);
            plc_sock_fd = -1;
            return -1;
         }

         /* Read back the response (robot status) */
         PLCFrame rx;
         ssize_t n = recv (plc_sock_fd, &rx, sizeof (PLCFrame), MSG_WAITALL);
         if (n == (ssize_t)sizeof (PLCFrame))
         {
            pthread_mutex_lock (&status_mutex);
            latest_robot_status = rx;
            pthread_mutex_unlock (&status_mutex);
         }
         else
         {
            APP_LOG_WARNING ("TCP bridge: recv failed: %s\n",
                             (n < 0) ? strerror (errno) : "short read");
            close (plc_sock_fd);
            plc_sock_fd = -1;
         }
      }
      return 0;
   }

   if (submodule_id == APP_GSDML_SUBMOD_ID_DIGITAL_OUT ||
       submodule_id == APP_GSDML_SUBMOD_ID_DIGITAL_IN_OUT)
   {
      if (size == APP_GSDML_OUTPUT_DATA_DIGITAL_SIZE)
      {
         memcpy (outputdata, data, size);

         /* Bit7 controls LED */
         app_set_led (APP_DATA_LED_ID, (outputdata[0] & 0x80) != 0);

         /* Print to serial only when I/O data changes */
         print_io_on_change (inputdata[0], outputdata[0]);

         return 0;
      }
   }
   else if (submodule_id == APP_GSDML_SUBMOD_ID_ECHO)
   {
      if (size == APP_GSDML_OUTPUT_DATA_ECHO_SIZE)
      {
         memcpy (echo_outputdata, data, size);
         return 0;
      }
   }

   return -1;
}

/**
 * Set default outputs when not connected to PLC
 */
int app_data_set_default_outputs (void)
{
   outputdata[0] = APP_DATA_DEFAULT_OUTPUT_DATA;
   app_set_led (APP_DATA_LED_ID, false);
   return 0;
}

/**
 * Application initialization - called once at startup
 */
void app_data_init (void)
{
   init_serial_port ();
   APP_LOG_INFO ("OK3576 Profinet device: I1.0~I1.7 input, Q1.0~Q1.7 output\n");
}

int app_data_write_parameter (
   uint16_t slot_nbr,
   uint16_t subslot_nbr,
   uint32_t submodule_id,
   uint32_t index,
   const uint8_t * data,
   uint16_t length)
{
   const app_gsdml_param_t *par_cfg;

   par_cfg = app_gsdml_get_parameter_cfg (submodule_id, index);
   if (par_cfg == NULL)
   {
      APP_LOG_WARNING ("Unsupported parameter write. Submodule: %u Index: %u\n",
                       (unsigned)submodule_id, (unsigned)index);
      return -1;
   }

   if (length != par_cfg->length)
   {
      APP_LOG_WARNING ("Unsupported length. Index: %u Length: %u Expected: %u\n",
                       (unsigned)index, (unsigned)length, par_cfg->length);
      return -1;
   }

   if (index == APP_GSDML_PARAMETER_1_IDX)
      memcpy (&app_param_1, data, length);
   else if (index == APP_GSDML_PARAMETER_2_IDX)
      memcpy (&app_param_2, data, length);
   else if (index == APP_GSDML_PARAMETER_ECHO_IDX)
      memcpy (&app_param_echo_gain, data, length);

   APP_LOG_DEBUG ("Writing parameter \"%s\"\n", par_cfg->name);
   return 0;
}

int app_data_read_parameter (
   uint16_t slot_nbr,
   uint16_t subslot_nbr,
   uint32_t submodule_id,
   uint32_t index,
   uint8_t ** data,
   uint16_t * length)
{
   const app_gsdml_param_t *par_cfg;

   par_cfg = app_gsdml_get_parameter_cfg (submodule_id, index);
   if (par_cfg == NULL)
   {
      APP_LOG_WARNING ("Unsupported parameter read. Submodule: %u Index: %u\n",
                       (unsigned)submodule_id, (unsigned)index);
      return -1;
   }

   if (*length < par_cfg->length)
   {
      APP_LOG_WARNING ("Buffer too small. Index: %u Need: %u Have: %u\n",
                       (unsigned)index, par_cfg->length, (unsigned)*length);
      return -1;
   }

   if (index == APP_GSDML_PARAMETER_1_IDX)
   {
      *data = (uint8_t *)&app_param_1;
      *length = sizeof (app_param_1);
   }
   else if (index == APP_GSDML_PARAMETER_2_IDX)
   {
      *data = (uint8_t *)&app_param_2;
      *length = sizeof (app_param_2);
   }
   else if (index == APP_GSDML_PARAMETER_ECHO_IDX)
   {
      *data = (uint8_t *)&app_param_echo_gain;
      *length = sizeof (app_param_echo_gain);
   }

   return 0;
}
