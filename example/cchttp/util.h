

#pragma once
#define _GNU_SOURCE /* To get defns of NI_MAXSERV and NI_MAXHOST */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>

#include <ts/ts.h>

#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#include <ifaddrs.h>
#ifdef __unix__
    #include <linux/if_link.h>
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#ifndef NI_MAXSERV
#define NI_MAXSERV 32
#endif


#define CHECK(X)                                         \
  {                                                      \
    const TSReturnCode r = X;                            \
    assert(r == TS_SUCCESS);                             \
  }

#define CHECKNULL(x)  assert((x) != NULL)


#define debug(fmt, args...)                                                             \
  do {                                                                                  \
    TSDebug(PLUGIN_NAME, "DEBUG: [%s:%d] [%s] " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
  } while (0)

#define info(fmt, args...)              \
  do {                                  \
    TSDebug(PLUGIN_NAME, "INFO: " fmt, ##args); \
  } while (0)

#define warning(fmt, args...)              \
  do {                                     \
    TSDebug(PLUGIN_NAME, "WARNING: " fmt, ##args); \
  } while (0)

#define error(fmt, args...)                                                             \
  do {                                                                                  \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args);      \
    TSDebug(PLUGIN_NAME, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
  } while (0)

#define fatal(fmt, args...)                                                             \
  do {                                                                                  \
    TSError("[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args);      \
    TSDebug(PLUGIN_NAME, "[%s:%d] [%s] ERROR: " fmt, __FILE__, __LINE__, __FUNCTION__, ##args); \
    exit(-1);                                                                           \
  } while (0)



int get_my_ip(int *ips);
int is_my_ip(int ip, int *ips, int n_ips);
void printbits(int64_t x);
char *int64_to_bitstring_static(int64_t x);
void print_reader(const char *plugin_name, TSIOBufferReader reader);