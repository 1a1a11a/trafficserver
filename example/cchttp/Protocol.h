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
#define _GNU_SOURCE      /* To get defns of NI_MAXSERV and NI_MAXHOST */
#define MY_DEBUG_LEVEL 4 // 0-5

#include <unistd.h>
#include <math.h>
#include <inttypes.h>
#include <arpa/inet.h>
#include <sys/time.h>

#include <ts/ts.h>
// #include "net.h"

// #include <queue>

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

// #define EC_STATUS_BEGIN 0
// #define EC_STATUS_PEER_RESP_READY 1
// // #define EC_STATUS_RESP_READY 2
// // #define EC_STATUS_ALL_READY 3
// #define TXN_WAIT_FOR_CLEAN 2


typedef enum _EC_status{
  EC_STATUS_BEGIN, 
  EC_STATUS_PEER_RESP_READY, 
  TXN_WAIT_FOR_CLEAN
}EcStatus; 


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

  TSMutex transform_mtx; 
  TSCont transform_contp; 
  // int64_t ssn_id;
  // int64_t txn_id;
  char ssn_txn_id[16];



  volatile int16_t n_available_peers; // __builtin_popcount
  volatile int64_t ready_peers;       // use the bit of a 64-bit integer to represent peers

  char **peer_resp_buf;
  char *final_resp;

  EcStatus status;

  char *request_path_component;
  char *request_string;
  TSIOBuffer request_buffer;
  TSIOBufferReader *request_buffer_readers;

  // response transform
  TSVIO output_vio;
  TSIOBuffer output_buffer;
  TSIOBufferReader output_reader;

  // stat
  bool local_hit;

  // timer
  int64_t txn_start_ts;
  int64_t post_remap_ts; 
  int64_t local_finish_ts;
  int64_t *chunk_arrival_ts;
  int64_t decoding_start_ts;
  int64_t decoding_finish_ts;
  int64_t response_begin_ts;
  int64_t txn_finish_ts;

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

  // save partial of previous response to find Content-Length
  char concat_response[65];

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

typedef enum _timeType {
  TXN_START       ,
  POST_REMAP, 
  LOCAL_FINISH    ,
  CHUNK_ARRIVAL   ,
  DECODING_START  ,
  DECODING_FINISH ,
  RESPONSE_BEGIN  ,
  TXN_FINISH      
} TimeType;

static inline void
record_time(TSTextLogObject log_obj, TxnData *txn_data, TimeType time_type, void *other_data)
{
  struct timeval tv;
  gettimeofday(&tv, NULL);
  unsigned long time_in_micro = 1000000 * tv.tv_sec + tv.tv_usec;

  switch (time_type) {
  case TXN_START:
    txn_data->txn_start_ts = time_in_micro;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s TxnStart %ld", txn_data->ssn_txn_id, time_in_micro);
    break;
  case POST_REMAP:
    txn_data->post_remap_ts = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s PostRemap %ld", txn_data->ssn_txn_id, txn_data->post_remap_ts);
    break;
  case LOCAL_FINISH:
    txn_data->local_finish_ts = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s LocalFinish %ld", txn_data->ssn_txn_id, txn_data->local_finish_ts);
    break;
  case CHUNK_ARRIVAL:
    ;
    int64_t chunk_id                     = (int64_t)(other_data);
    txn_data->chunk_arrival_ts[chunk_id] = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s ChunkArrival chunk %ld: %ld", txn_data->ssn_txn_id, chunk_id,
                         txn_data->chunk_arrival_ts[chunk_id]);
    break;
  case DECODING_START:
    txn_data->decoding_start_ts = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s DecodingStart %ld", txn_data->ssn_txn_id, txn_data->decoding_start_ts);
    break;
  case DECODING_FINISH:
    txn_data->decoding_finish_ts = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s DecodingFinish %ld", txn_data->ssn_txn_id, txn_data->decoding_finish_ts);
    break;
  case RESPONSE_BEGIN:
    txn_data->response_begin_ts = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s RespBegin %ld", txn_data->ssn_txn_id, txn_data->response_begin_ts);
    break;
  case TXN_FINISH:
    txn_data->txn_finish_ts = time_in_micro - txn_data->txn_start_ts;
    TSTextLogObjectWrite(log_obj, "Ssn-Txn %s TxnFinish %ld", txn_data->ssn_txn_id, txn_data->txn_finish_ts);
    break;
    default:
    TSAssert(false); 
    break; 
  }
}
