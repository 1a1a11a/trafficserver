/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#pragma once
#define _GNU_SOURCE /* To get defns of NI_MAXSERV and NI_MAXHOST */
#define MY_DEBUG_LEVEL 4      // 0-5

#include <unistd.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <limits.h>
// #include <string.h>
// #include <assert.h>
// #include <time.h>
// #include <sys/time.h>
// #include <sys/types.h>

#include <math.h>
#include <inttypes.h>
#include <arpa/inet.h>

#include <ts/ts.h>
// #include "net.h"

// #include <queue>

// #include <unistd.h>
// #include <errno.h>
// #include <netdb.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #include <arpa/inet.h>

// #include <ifaddrs.h>
// #include <linux/if_link.h>

#define PLUGIN_NAME "echttp"

#define MAX_SERVER_NAME_LENGTH 1024
#define MAX_FILE_NAME_LENGTH 1024
// #define MAX_REQUEST_STRING 1024

/* MAX_SERVER_NAME_LENGTH + MAX_FILE_NAME_LENGTH + strlen("\n\n") */
#define MAX_REQUEST_LENGTH 2050

#define MAX_EC_NODES 32
#define MAX_IP_PER_NODE 8

#define EC_BUFFER_SIZE 16777216 // 16 MB

// #define EC_EVENT_K_BACK 102400

#define EC_STATUS_BEGIN 0
#define EC_STATUS_PEER_RESP_READY 1
// #define EC_STATUS_RESP_READY 2
// #define EC_STATUS_ALL_READY 3
#define TXN_WAIT_FOR_CLEAN 2 


typedef struct _EcPeer {
  char *addr_str;
  int ip;
  int port;
  struct sockaddr_in addr;
  int index;
} EcPeer;


/*
// this is used for echttp, however, this is not implemented at this time 
class EcConnectionPool
{
public:
  EcConnectionPool(int n_peer, EcPeer *peers, int n_conn_per_peer = 200);

  TSVConn get_one_connection();
  int put_one_connection();

  int check_connection_pool();
  int repair_connection_pool();

private:
  TSMutex mtx;
  queue<TSVConn> connection_pool;
  int _build_connection_pool();

  static int vccon_handler(TSCont contp, TSEvent event, void *data);

}
*/

struct _TxnData; 
struct _PeerConnData; 
typedef struct _SsnData {
  // TSMutex mtx;
  // TSVConn *vconns;
  // TSCont *contps;
  struct _PeerConnData *pcds; 
  volatile int n_connected_peers;
  struct _TxnData *current_txn_data;

} SsnData;


typedef struct _TxnData {
  TSHttpTxn txnp;
  TSCont contp; 

  // TSMutex mtx;
  // TSAction pending_action;
  // TxnSMHandler current_handler;

  volatile int16_t n_available_peers; // __builtin_popcount
  volatile int64_t ready_peers;     // use the bit of a 64-bit integer to represent peers
  
  // TSIOBufferReader *peer_resp_readers; 
  char **peer_resp_buf;
  char *final_resp;

  int status;

  char *request_path_component;
  char *request_string;
  TSIOBuffer request_buffer;
  TSIOBufferReader *request_buffer_readers;

  // response transform
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;

  int64_t osize; 
  TSIOBuffer my_temp_buffer; 
  TSIOBufferReader my_temp_reader; 

} TxnData;

typedef struct _PeerConnData {
  TSVConn vconn;
  TSCont contp;
  TSVIO read_vio;
  TSVIO write_vio;
  // TSIOBuffer request_buffer;

  // needs to be initialized at txn start 
  TSIOBuffer response_buffer;
  TSIOBufferReader response_buffer_reader;

  bool content_has_began;
  int64_t content_length; 
  int64_t read_in_length; 

  // char *request_string;
  // char *request, *response;

  EcPeer *peer;
  // TxnData *txn_data;

  // TSCont main_contp, contp;

} PeerConnData;