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

#include "tscore/ink_defs.h"

#include "Protocol.h"
#include "util.h"
#include "net.h"
#include "ec.h"

/* global variable */
TSTextLogObject protocol_plugin_log;

/* static variable */
EcPeer ec_peers[MAX_EC_NODES];
int EC_n;
int EC_k;
int EC_x;
int n_peers;
int current_node_EC_index;
int ssn_data_ind;
int txn_data_ind;

TSCont global_ssn_contp;
TSCont global_txn_contp;

// static int get_my_ip(int *ips);
// static int is_my_ip(int ip, int *ips, int n_ips);
static int load_ec_peer(int argc, const char **argv, EcPeer *ec_peers, int *myips, int n_myips);
static int transaction_handler(TSCont contp, TSEvent event, void *edata);

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
transaction_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpTxn txnp = (TSHttpTxn)edata;
  // SsnData *ssn_data = (SsnData *)TSHttpSsnArgGet(ssnp, ssn_data_ind);
  TxnData *txn_data;
  // TSCont main_contp;
  // TSMutex pmutex;
  // if (event == EC_EVENT_K_BACK)
  //   TSDebug(PLUGIN_NAME, "transaction_handler: txn %" PRId64 " received EC_EVENT_K_BACK", TSHttpTxnIdGet(txnp));
  // else
  TSDebug(PLUGIN_NAME, "transaction_handler: txn %ld-%" PRId64 " received %s (%d)", TSHttpSsnIdGet(TSHttpTxnSsnGet(txnp)),
          TSHttpTxnIdGet(txnp), TSHttpEventNameLookup(event), event);

  switch (event) {
  case TS_EVENT_HTTP_TXN_START:;

    TSHttpTxnUntransformedRespCache(txnp, 1);
    TSHttpTxnTransformedRespCache(txnp, 0);
    setup_txn(txnp);
    txn_data        = TSHttpTxnArgGet(txnp, txn_data_ind);
    txn_data->contp = contp;
    // txn_data->transform_mtx2 = TST

    // this is not exactly the txn start due to creating TxnData, but it is really close
    record_time(protocol_plugin_log, txn_data, TXN_START, NULL);

    /* if we need to go through codding */
    // TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
    // TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_CLOSE_HOOK, contp);

    // TSCont txn_contp = TSContCreate(transaction_handler, TSMutexCreate());
    TSVConn vconn = TSTransformCreate(RS_resp_transform_handler, txnp);
    TSContDataSet(vconn, txn_data);
    txn_data->transform_contp = (TSCont)vconn;
    txn_data->txnp_mtx        = TSContMutexGet((TSCont)txnp);

    // TSHttpTxnHookAdd(txnp, TS_HTTP_POST_REMAP_HOOK, txn_contp);
    TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, vconn);
    // TSHttpTxnHookAdd(txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

    break;
  case TS_EVENT_HTTP_POST_REMAP:
    conn_peer(contp, txnp);
    txn_data = TSHttpTxnArgGet(txnp, txn_data_ind);
    record_time(protocol_plugin_log, txn_data, POST_REMAP, NULL);
    // TSHttpTxnUntransformedRespCache(txnp, 1);
    // TSHttpTxnTransformedRespCache(txnp, 0);
    // setup_txn(contp, txnp);
    // txn_data        = TSHttpTxnArgGet(txnp, txn_data_ind);
    // txn_data->contp = contp;

    // /* if we need to go through codding */
    // TSHttpSsn ssnp = TSHttpTxnSsnGet(txnp);
    // TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_CLOSE_HOOK, contp);

    // TSVConn vconn = TSTransformCreate(RS_resp_transform_handler, txnp);
    // TSContDataSet(vconn, txn_data);
    // TSHttpTxnHookAdd(txnp, TS_HTTP_RESPONSE_TRANSFORM_HOOK, vconn);

    // TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    break;

    // case EC_EVENT_K_BACK:
    //   TSAssert(false);
    //   txn_data = TSHttpTxnArgGet(txnp, txn_data_ind);
    //   TSDebug(PLUGIN_NAME, "transaction_handler: txn %" PRId64 "K+X-1 resp back! available peers %d %s", TSHttpTxnIdGet(txnp),
    //           txn_data->n_available_peers, int64_to_bitstring_static(txn_data->ready_peers));
    //   // check k event
    //   int64_t available_peers = 0, final_size = 0;
    //   int current_peer_available = 0;
    //   for (int i = 0; i < EC_n; i++) {
    //     // current_peer_available = __sync_and_and_fetch(&(txn_data->ready_peers), (1 << i));
    //     current_peer_available = txn_data->ready_peers & (1 << i);
    //     // current_peer_available = txn_data->n_available_peers & (1 << i);
    //     // if (txn_data->peer_resp_buf[i] != NULL) {

    //     if (current_peer_available > 0) {
    //       available_peers++;
    //       final_size += strlen(txn_data->peer_resp_buf[i]);
    //       TSDebug(PLUGIN_NAME, "transaction_handler: txn %" PRId64 "response %d %s", TSHttpTxnIdGet(txnp), i,
    //               txn_data->peer_resp_buf[i]);
    //     }
    //   }
    //   TSAssert(available_peers >= EC_k - 1);
    //   TSDebug(PLUGIN_NAME, "K %d X %d, response size sum is %" PRId64, EC_k, EC_x, final_size);

    //   txn_data->final_resp    = TSmalloc(sizeof(char) * final_size);
    //   txn_data->final_resp[0] = '\0';
    //   for (int i = 0; i < EC_n; i++) {
    //     if (txn_data->peer_resp_buf[i] != NULL) {
    //       TSstrlcat(txn_data->final_resp, txn_data->peer_resp_buf[i], final_size + 1);
    //     }
    //   }
    //   TSDebug(PLUGIN_NAME, "final resp %s", txn_data->final_resp);
    //   txn_data->status = EC_STATUS_PEER_RESP_READY;
    //   break;
    // case TS_EVENT_IMMEDIATE:
    // case TS_EVENT_VCONN_WRITE_READY:
    // case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
    //   TSAssert(false);
    //   break;
    // case TS_EVENT_HTTP_READ_RESPONSE_HDR:
    //   TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_READ_RESPONSE_HDR");
    //   break;
    // case TS_EVENT_VCONN_WRITE_COMPLETE:
    //   /* When our output connection says that it has finished
    //      reading all the data we've written to it then we should
    //      shutdown the write portion of its connection to
    //      indicate that we don't want to hear about it anymore. */
    //   TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    //   break;

  case TS_EVENT_HTTP_TXN_CLOSE:
    txn_data = TSHttpTxnArgGet(txnp, txn_data_ind);
    if (txn_data) {
      // drop everything since txn is going to close
      clean_txn(contp, txnp);
      TSHttpTxnArgSet(txnp, txn_data_ind, NULL);
      TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_TXN_CLOSE: txn %ld, finish cleaning", TSHttpTxnIdGet(txnp));

      TSDebug(PLUGIN_NAME,
              "*********************************************************************************************************"
              "**********************************************************************************");
      TSDebug(PLUGIN_NAME,
              "*********************************************************************************************************"
              "**********************************************************************************\n\n\n\n\n\n");
      record_time(protocol_plugin_log, txn_data, TXN_FINISH, NULL);
      // if (contp != global_txn_contp) {
      //   TSDebug(PLUGIN_NAME, "TS_EVENT_HTTP_TXN_CLOSE: txn %ld, destroy contp", TSHttpTxnIdGet(txnp));
      //   TSContDestroy(contp);
      // }
    }
    /* this contp is created globally, so don't destroy */
    // TSContDestroy(contp);
    break;

    // if (txn_data->n_available_peers < EC_k + EC_x) {
    //   // we need to wait for all peer response back
    //   TSDebug(PLUGIN_NAME, "transaction_handler: txn %ld wait for all peer response", TSHttpTxnIdGet(txnp));
    //   txn_data->status = TXN_WAIT_FOR_CLEAN;
    //   TSContSchedule(contp, 80, TS_THREAD_POOL_DEFAULT);
    // } else {
    //   clean_txn(contp, txnp);
    //   // TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    // }
    // break;
  // will not be used
  // case 2: // EVENT_INTERVAL when called by TS_Cont_Schedule
  //   txn_data = TSHttpTxnArgGet(txnp, txn_data_ind);
  //   if (txn_data->status == TXN_WAIT_FOR_CLEAN) {
  //     if (txn_data->n_available_peers < EC_k + EC_x) {
  //       // we need to wait for all peer response back
  //       TSDebug(PLUGIN_NAME, "transaction_handler: txn %ld wait for all peer response", TSHttpTxnIdGet(txnp));
  //       TSContSchedule(contp, 200, TS_THREAD_POOL_DEFAULT);
  //     } else {
  //       clean_txn(contp, txnp);
  //     }

  //   } else {
  //     TSAssert(false);
  //   }
  //   break;
  case TS_EVENT_ERROR:
  default:
    TSDebug(PLUGIN_NAME, "Unknown event %d in transaction_handler, POST_REMAP is %d", event, TS_EVENT_HTTP_POST_REMAP);
    TSError("[%s] unknown event %d in transaction_handler, POST_REMAP is %d", PLUGIN_NAME, event, TS_EVENT_HTTP_POST_REMAP);
    break;
  }

  // this is necessary!!! even for TS_EVENT_HTTP_TXN_CLOSE, without it, session won't close
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return 0;
}

static TSReturnCode
session_handler(TSCont contp, TSEvent event, void *edata)
{
  TSHttpSsn ssnp = (TSHttpSsn)edata;

  TSDebug(PLUGIN_NAME, "session_handler:  ssn %" PRId64 " receive %s (%d)", TSHttpSsnIdGet(ssnp), TSHttpEventNameLookup(event),
          event);
  switch (event) {
  case TS_EVENT_HTTP_SSN_START:

    setup_ssn(ssnp, n_peers);
    SsnData *ssn_data = TSHttpSsnArgGet(ssnp, ssn_data_ind);

    // this should have the lifetime of complete ssn
    TSCont txn_contp = TSContCreate(transaction_handler, TSMutexCreate());
    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, txn_contp);
    TSHttpSsnHookAdd(ssnp, TS_HTTP_POST_REMAP_HOOK, txn_contp);
    TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp);

    TSDebug(PLUGIN_NAME, "session_handler:  ssn %ld peer_connect is on the fly currently %d peers connected", TSHttpSsnIdGet(ssnp),
            ssn_data->n_connected_peers);
    break;

  case TS_EVENT_HTTP_SSN_CLOSE:
    clean_ssn(ssnp, n_peers);

    TSDebug(PLUGIN_NAME, "session_handler:  ssn %ld session closed", TSHttpSsnIdGet(ssnp));
    TSDebug(PLUGIN_NAME, "*********************************************************************************************************"
                         "**********************************************************************************");
    TSDebug(PLUGIN_NAME, "*********************************************************************************************************"
                         "**********************************************************************************");
    TSDebug(PLUGIN_NAME, "*********************************************************************************************************"
                         "**********************************************************************************");
    TSDebug(PLUGIN_NAME, "*********************************************************************************************************"
                         "**********************************************************************************");
    TSDebug(PLUGIN_NAME, "*********************************************************************************************************"
                         "**********************************************************************************");
    TSDebug(PLUGIN_NAME, "*********************************************************************************************************"
                         "**********************************************************************************");
    TSDebug(PLUGIN_NAME,
            "*********************************************************************************************************"
            "**********************************************************************************\n\n\n\n\n\n\n\n\n\n\n");

    break;

  default:
    break;
  }

  TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
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
  TSDebug(PLUGIN_NAME, "I have %d ips", n_myips);

  n_peers = load_ec_peer(argc, argv, ec_peers, myips, n_myips);
  if (n_peers != argc - 2) {
    TSError("[%s] failed to load peers, I have %d ip, there are %d peers, %d inputs", PLUGIN_NAME, n_myips, n_peers, argc - 1);
    goto error;
  }

  /* create customized log */
  if (TSTextLogObjectCreate(PLUGIN_NAME, TS_LOG_MODE_ADD_TIMESTAMP, &protocol_plugin_log) != TS_SUCCESS) {
    TSError("[%s] Failed to create log", PLUGIN_NAME);
  }

  CHECK(TSTextLogObjectWrite(protocol_plugin_log, "# myip "));
  for (int i = 0; i < n_myips; i++) {
    CHECK(TSTextLogObjectWrite(protocol_plugin_log, "# %s ", convert_ip_to_str(myips[i])));
  }
  CHECK(TSTextLogObjectWrite(protocol_plugin_log, "# peers "));
  for (int i = 0; i < n_peers; i++) {
    CHECK(TSTextLogObjectWrite(protocol_plugin_log, "# %s ", ec_peers[i].addr_str));
  }

  // Jason::Debug::Temp
  EC_k = n_peers;
  EC_n = n_peers + 1;
  EC_x = 1;

  global_ssn_contp = TSContCreate(session_handler, TSMutexCreate());
  global_txn_contp = TSContCreate(transaction_handler, TSMutexCreate());

  CHECK(TSHttpSsnArgIndexReserve(PLUGIN_NAME, "session data", &ssn_data_ind));
  CHECK(TSHttpTxnArgIndexReserve(PLUGIN_NAME, "transaction data", &txn_data_ind));

  TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, global_ssn_contp);
  TSHttpHookAdd(TS_HTTP_SSN_CLOSE_HOOK, global_ssn_contp);

  // TSHttpHookAdd(TS_HTTP_TXN_START_HOOK, global_txn_contp);
  // TSHttpHookAdd(TS_HTTP_POST_REMAP_HOOK, global_txn_contp);
  // TSHttpHookAdd(TS_HTTP_TXN_CLOSE_HOOK, global_txn_contp);

  TSDebug(PLUGIN_NAME, "initialization finish");
  return;

error:
  TSDebug(PLUGIN_NAME, " Plugin not initialized");
  TSError("[%s] Plugin not initialized", PLUGIN_NAME);
}
