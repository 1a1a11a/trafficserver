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

#include "util.h"
#include "Protocol.h"
#include "TxnSM.h"
#include "tscore/ink_defs.h"
#include <math.h>

/* global variable */
TSTextLogObject protocol_plugin_log;

/* static variable */
EcPeer ec_peers[MAX_EC_NODES];
int EC_n;
int EC_k;
int EC_x;
int n_peers;
int current_node_EC_index;

// static int get_my_ip(int *ips);
// static int is_my_ip(int ip, int *ips, int n_ips);
static int load_ec_peer(int argc, const char **argv, EcPeer *ec_peers, int *myips, int n_myips);
static int EcHTTPHandler(TSCont contp, TSEvent event, void *edata);

/* load ec nodes from command line arguments,
 * return the number of peers, which should be argc-2 (one is programName, one is current node)
 * assume all ip are AF_INET
 **/
static int
load_ec_peer(int argc, const char **argv, EcPeer *ec_peers, int *myips, int n_myips)
{
  //  EcPeer* ec_peers = TSmalloc(sizeof(EcPeer) * MAX_EC_NODES);
  struct sockaddr_in ip_addr;
  memset(&ip_addr, 0, sizeof(ip_addr));
  ip_addr.sin_family      = AF_INET;
  ip_addr.sin_addr.s_addr = htonl(0);    /* Should be in network byte order */
  ip_addr.sin_port        = htons(8080); /* Should be in network byte order */

  int new_ip, ip_oct0, ip_oct1, ip_oct2, ip_oct3, port, ec_peer_pos = 0;
  for (int i = 1; i < argc; i++) {
    sscanf(argv[i], "%d.%d.%d.%d:%d", &ip_oct0, &ip_oct1, &ip_oct2, &ip_oct3, &port);
    new_ip = (ip_oct0 << 24) | (ip_oct1 << 16) | (ip_oct2 << 8) | (ip_oct3);
    if (is_my_ip(new_ip, myips, n_myips)) {
      current_node_EC_index = i - 1;
      continue;
    }

    // OK, this one is a peer, let's save it
    TSDebug(PLUGIN_NAME, "load peer %s", argv[i]);
    ip_addr.sin_addr.s_addr = htonl(new_ip);
    ip_addr.sin_port        = htons(port);

    memcpy(&(ec_peers[ec_peer_pos].addr), &ip_addr, sizeof(ip_addr));
    ec_peers[ec_peer_pos].ip       = new_ip;
    ec_peers[ec_peer_pos].port     = port;
    ec_peers[ec_peer_pos].addr_str = TSstrdup(argv[i]);
    ec_peers[ec_peer_pos].index    = ec_peer_pos;
    ec_peer_pos++;
  }
  return ec_peer_pos;
}

static int
EcHTTPHandler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp     = (TSHttpTxn)edata;
  TxnData *txn_data = (TxnData *)TSContDataGet(contp);
  // TSCont main_contp;
  TSMutex pmutex;

  switch (event) {
  // TS_HTTP_POST_REMAP_HOOK
  case TS_EVENT_HTTP_POST_REMAP:
    TSDebug(PLUGIN_NAME, "handler get TS_EVENT_HTTP_POST_REMAP");
    TSDebug(PLUGIN_NAME, "current alive server connections %d", TSHttpCurrentServerConnectionsGet());

    pmutex = (TSMutex)TSMutexCreate();
    // txn_sm = (TSCont)TxnSMCreate(pmutex, edata);
    setup_ec(contp, pmutex, txnp);

    // TSMutexLockTry(pmutex); // TODO: why should it not check if we got the lock??
    // TSContCall(txn_sm, 0, txn);
    // TSMutexUnlock(pmutex);
    // TSHttpTxnHookAdd(txnp, TS_HTTP_SEND_RESPONSE_HDR_HOOK, contp);

    /* if we need to go through codding */ 
    // TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, contp);
    // TSHttpHookAdd(TS_HTTP_READ_RESPONSE_HDR_HOOK, contp);
    // TSHttpHookAdd(TS_HTTP_RESPONSE_TRANSFORM_HOOK, contp);

    break;
)
  case EC_EVENT_K_BACK:
    TSDebug(PLUGIN_NAME, "K+X-1 resp back! available peers %d", txn_data->available_peers);
    // check k event
    int64_t available_peers = 0, final_size = 0;
    int current_peer_available = false;
    for (int i = 0; i < EC_n; i++) {
      current_peer_available = __sync_and_and_fetch(&(txn_data->available_peers), (1 << i));
      // current_peer_available = txn_data->available_peers & (1 << i);
      // if (txn_data->peer_resp_buf[i] != NULL) {

      if (current_peer_available) {
        available_peers++;
        final_size += strlen(txn_data->peer_resp_buf[i]);
        TSDebug(PLUGIN_NAME, "response %d %s", i, txn_data->peer_resp_buf[i]);
      }
    }
    TSAssert(available_peers >= EC_k - 1);
    TSDebug(PLUGIN_NAME, "K %d, response size sum is %" PRId64, EC_k, final_size);

    txn_data->final_resp    = TSmalloc(sizeof(char) * final_size);
    txn_data->final_resp[0] = '\0';
    for (int i = 0; i < EC_n; i++) {
      if (txn_data->peer_resp_buf[i] != NULL) {
        TSstrlcat(txn_data->final_resp, txn_data->peer_resp_buf[i], final_size + 1);
      }
    }
    TSDebug(PLUGIN_NAME, "final resp %s", txn_data->final_resp);
    TSDebug(PLUGIN_NAME, "current alive server connections %d", TSHttpCurrentServerConnectionsGet());
    txn_data->status = EC_STATUS_PEER_RESP_READY;
    break;
  // TS_HTTP_RESPONSE_TRANSFORM_HOOK
  case TS_EVENT_IMMEDIATE:
  case TS_EVENT_VCONN_WRITE_READY:
  case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    TSDebug(PLUGIN_NAME,
            "handler get TS_EVENT_IMMEDIATE (%d) or TS_EVENT_VCONN_WRITE_READY (%d) TS_EVENT_HTTP_SEND_RESPONSE_HDR (%d) %d",
            TS_EVENT_IMMEDIATE, TS_EVENT_VCONN_WRITE_READY, TS_EVENT_HTTP_SEND_RESPONSE_HDR, event);
    if (txn_data->status == EC_STATUS_PEER_RESP_READY) {
      // DO RS
      ;
    } else {
      TSDebug(PLUGIN_NAME, "wait for peer response");
      TSContSchedule(contp, 1, TS_THREAD_POOL_DEFAULT);
    }
    break;

    if (txn_data && txn_data->status == EC_STATUS_ALL_ERADY) {
      // prepare response
      ;
    } else
      return 0;
  case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_READ_RESPONSE_HDR");
    TSVConn vconn = TSTransformCreate(NULL, txnp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, vconn);
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* When our output connection says that it has finished
       reading all the data we've written to it then we should
       shutdown the write portion of its connection to
       indicate that we don't want to hear about it anymore. */
    TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    cleanup_main_contp(contp);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

  case TS_EVENT_ERROR:
  default:
    TSDebug(PLUGIN_NAME, "Unknown event %d in EcHTTPHandler, POST_REMAP is %d", event, TS_EVENT_HTTP_POST_REMAP);
    TSError("[%s] unknown event %d in EcHTTPHandler, POST_REMAP is %d", PLUGIN_NAME, event, TS_EVENT_HTTP_POST_REMAP);
    break;
  }

  // TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static int HttpSessionHandler(){

  switch (event)
  {
  case TS_HTTP_SSN_START:
    /* start TCP connection to each peer */
    break;

  case TS_HTTP_SSN_CLOSE_HOOK:
    /* close TCP connection to each peer */
  
    break; 
   default:
    break;
  }



  TSHttpSsnReenable(ssnp, event); 
  return TS_SUCCESS; 
}



void
TSPluginInit(int argc, const char *argv[])
{
  TSPluginRegistrationInfo info;
  int n_myips = 0;
  int myips[MAX_IP_PER_NODE];

  info.plugin_name   = PLUGIN_NAME;
  info.vendor_name   = "TheSys Group";
  info.support_email = "peter.waynechina@gmail.com";

  if (TSPluginRegister(&info) != TS_SUCCESS) {
    TSError("[%s] Plugin registration failed", PLUGIN_NAME);

    goto error;
  }

  TSDebug(PLUGIN_NAME, "total %d EC nodes\n", argc - 1);

  if (argc <= 1) {
    TSError("Usage: %s.so node0IP:node0Port node1IP:node1Port ...", argv[0]);
    goto error;
  }

  n_myips = get_my_ip(myips);
  if (n_myips <= 0) {
    TSError("[%s] failed to load my ip", PLUGIN_NAME);
    goto error;
  }
  TSDebug(PLUGIN_NAME, "load %d my ips", n_myips);

  n_peers = load_ec_peer(argc, argv, ec_peers, myips, n_myips);
  if (n_peers != argc - 2) {
    TSError("[%s] failed to load peers, I have %d ip, there are %d peers, %d inputs", PLUGIN_NAME, n_myips, n_peers, argc - 1);
    goto error;
  }

  /* create customized log */
  if (TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &protocol_plugin_log) != TS_SUCCESS) {
    TSError("[%s] Failed to create log", PLUGIN_NAME);
  }

  /* format of the log entries, for caching_status, 1 for HIT and 0 for MISS */
  if (TSTextLogObjectWrite(protocol_plugin_log, "timestamp filename servername caching_status\n\n") != TS_SUCCESS) {
    TSError("[%s] Failed to write into log", PLUGIN_NAME);
  }

  // Jason::Debug::Temp
  EC_k = n_peers;
  EC_n = n_peers;
  EC_x = 0;

  TSCont contp = TSContCreate(EcHTTPHandler, TSMutexCreate());
  TSCont ssn_contp = TSContCreate(HttpSessionHandler, TSMutexCreate());

  TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, contp);

  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, ssn_contp);
  TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, ssn_contp);

  TSDebug(PLUGIN_NAME, "initialization finish");

error:
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);
}
