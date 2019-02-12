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

#include <sys/types.h>
#include <netinet/in.h>
#include "tscore/ink_defs.h"

#include "TxnSM.h"

#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>

extern TSTextLogObject protocol_plugin_log;

// extern EcPeer ec_peers[MAX_EC_NODES];
// extern int EC_n;
// extern int EC_k;
// extern int EC_x;
// extern int n_peers;
// extern int current_node_EC_index;

static void EC_contact_one_peer(TSCont main_contp, TxnData *txn_data, int peer_index);
static int peer_conn_handle(TSCont contp, TSEvent event, void *edata);

/* Obtain a backtrace and print it to stdout. */
inline static void
print_trace(void)
{
  void *array[10];
  size_t size;
  char **strings;
  size_t i;

  size    = backtrace(array, 10);
  strings = backtrace_symbols(array, size);

  printf("Obtained %zd stack frames.\n", size);

  for (i = 0; i < size; i++)
    printf("%s\n", strings[i]);

  free(strings);
}

void
setup_ec(TSCont main_contp, TSMutex mtx, TSHttpTxn txn)
{
  // TSCont contp;

  TxnData *txn_data       = (TxnData *)TSmalloc(sizeof(TxnData));
  txn_data->txn           = txn;
  txn_data->mtx           = mtx;
  txn_data->status        = EC_STATUS_BEGIN;
  txn_data->peers         = ec_peers;
  txn_data->peer_resp_buf = (char **)TSmalloc(sizeof(char *) * EC_n);
  memset(txn_data->peer_resp_buf, 0, sizeof(char *) * EC_n);
  txn_data->n_available_peers = 0;
  tnx_data->ready_peers = 0; 

  // when there are k_peers are back, call main_contp
  // contp = TSContCreate(EC_main_handler, txn_data->mtx);
  // TSContDataSet(contp, txn_data);
  TSContDataSet(main_contp, txn_data);

  EC_retrieve_from_peers(main_contp, txn_data);
}

/*
int
EC_main_handler(TSCont contp, TSEvent event, void *data)
{
  switch (event) {
  case EC_EVENT_K_BACK: {
    TxnData *txn_data = (TxnData *)TSContDataGet(contp);
    // check k event
    int available_peers = 0, final_size = 0;
    for (int i = 0; i < EC_n; i++) {
      if (txn_data->peer_resp_buf[i] != NULL) {
        available_peers++;
        final_size += strlen(txn_data->peer_resp_buf[i]);
      }
    }
    // DO RS

    txn_data->final_resp    = TSmalloc(sizeof(char) * final_size);
    txn_data->final_resp[0] = '\0';
    for (int i = 0; i < EC_n; i++) {
      if (txn_data->peer_resp_buf[i] != NULL) {
        TSstrlcat(txn_data->final_resp, txn_data->peer_resp_buf[i], final_size);
      }
    }
    TSDebug(PLUGIN_NAME, "final resp %s", txn_data->final_resp);
  }

  break;
  case TS_EVENT_ERROR:
  default:
    TSDebug(PLUGIN_NAME, "EC_main_handler receives unknown handler or error %d", event);
    break;
  }
  // handle when the results of k peers are back

  return TS_SUCCESS;
}
*/

void
EC_retrieve_from_peers(TSCont main_contp, TxnData *txn_data)
{
  TSHttpTxn txnp = txn_data->txn;
  TSMBuffer bufp;
  TSMLoc hdr_loc;
  TSMLoc url_loc;
  const char *path;
  int path_length;

  // get header
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header", PLUGIN_NAME);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  // get url
  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url", PLUGIN_NAME);
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return;
  }

  // Jason::Debug
  path                             = TSUrlPathGet(bufp, url_loc, &path_length);
  txn_data->request_path_component = TSstrndup(path, path_length);
  TSDebug(PLUGIN_NAME, "request path %s", txn_data->request_path_component);
  // 256 is large enough
  txn_data->request_string = (char *)TSmalloc(sizeof(char) * (path_length + 256));
  sprintf(txn_data->request_string, "GET /%s-peerConn HTTP/1.1\r\n\r\n", txn_data->request_path_component);
  txn_data->request_buffer = TSIOBufferCreate();
  CHECKNULL(txn_data->request_buffer);
  TSIOBufferWrite(txn_data->request_buffer, txn_data->request_string, strlen(txn_data->request_string));

  // clone url
  // bufp2 = TSMBufferCreate();
  // if (TSUrlClone(bufp2, bufp, url_loc, &url_loc2) != TS_SUCCESS) {
  //   TSError("[%s] Couldn't clone url", PLUGIN_NAME);
  //   TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  //   TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  //   TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  //   return;
  // }
  // TSIOBufferDestroy(bufp2);

  // TSVConn TSHttpConnect(struct sockaddr const *addr)

  // TSUrlHostSet(bufp2, url_loc2, const char *value, int length);
  // TSUrlPortSet(bufp2, url_loc2, int port);

  TSDebug(PLUGIN_NAME, "I will connect to peers, request %s", txn_data->request_string);
  for (int i = 0; i < EC_k + EC_x; i++) {
    EC_contact_one_peer(main_contp, txn_data, i);
  }

  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  return;
}

/*
static void
EC_contact_one_peer(TSCont main_contp, TxnData *txn_data, int peer_index)
{
  PeerConnData *pcd = (PeerConnData *)TSmalloc(sizeof(PeerConnData));
  TSCont contp      = TSContCreate(peer_conn_handle, TSMutexCreate());
  CHECKNULL(contp);
  pcd->contp = contp;
  TSContDataSet(contp, pcd);

  // initialize pcd
  pcd->peer              = txn_data->peers + peer_index;
  pcd->txn_data          = txn_data;
  pcd->main_contp        = main_contp;
  pcd->content_has_began = false;
  // pcd->my_peer_index = peer_index;

  pcd->request_buffer_reader = TSIOBufferReaderAlloc(txn_data->request_buffer);
  CHECKNULL(pcd->request_buffer_reader);
  pcd->response_buffer = TSIOBufferCreate();
  CHECKNULL(pcd->response_buffer);
  pcd->response_buffer_reader = TSIOBufferReaderAlloc(pcd->response_buffer);
  CHECKNULL(pcd->response_buffer_reader);

  // I guess since we are using TSHttpConnect, so I don't have to wait for TS_EVENT_NET_CONNECT
  // pcd->vconn = TSHttpConnect((struct sockaddr *)&(peer->addr));
  // CHECKNULL(pcd->vconn);

  TSAction action = TSNetConnect(contp, (struct sockaddr const *)&(pcd->peer->addr));
  if (TSActionDone(action)) {
    TSDebug(PLUGIN_NAME, "Network connection already opened");
  }

  // pcd->write_vio = TSVConnWrite(pcd->vconn, contp, pcd->request_buffer_reader, strlen(txn_data->request_string));

  // if (ti > 0) {
  //   TSDebug(PLUGIN_NAME, "ats::get Setting active timeout to: %"
  //   PRId64
  //   ", but does nothing now", ti);
  //   // transaction->timeout(ti);
  // }

  TSDebug(PLUGIN_NAME, "connect to peer %s", pcd->peer->addr_str);
}

static int
peer_conn_handle_old(TSCont contp, TSEvent event, void *edata)
{
  PeerConnData *pcd = (PeerConnData *)TSContDataGet(contp);
  int my_peer_index = pcd->peer->index;
  TxnData *txn_data = pcd->txn_data;

  switch (event) {
  case TS_EVENT_ERROR:
    TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d ERROR", my_peer_index);
    cleanup_peer_coon(contp);
    break;
  case TS_EVENT_NET_CONNECT:
    TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d Net Connect", my_peer_index);
    pcd->vconn     = edata;
    pcd->write_vio = TSVConnWrite(pcd->vconn, contp, pcd->request_buffer_reader, strlen(pcd->txn_data->request_string));
    break;
  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_READ_COMPLETE:
    TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d Read Complete (%d) or EOS (%d) %d", my_peer_index, TS_EVENT_VCONN_READ_COMPLETE,
            TS_EVENT_VCONN_EOS, event);

    cleanup_peer_coon(contp);
    // TSMutexLockTry(txn_data->mtx); // TODO: why should it not check if we got the lock??
    // txn_data->n_available_peers++;
    // txn_data->ready_peers = txn_data->ready_peers | (1<<my_peer_index); 
    // TSMutexUnlock(txn_data->mtx);
    int n_available = __sync_add_and_fetch(&(txn_data->n_available_peers), 1);
    __sync_fetch_and_xor(&(txn_data->ready_peers), (1 << my_peer_index));

    // might have problem due to data race
    if (n_available == EC_k + EC_x - 1) {
      // now wake up main continuation
      TSContCall(pcd->main_contp, EC_EVENT_K_BACK, NULL);
    }
    else if (n_available > EC_k + EC_x - 1){
      TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d response is not needed", my_peer_index); 
    }
    break;
  case TS_EVENT_VCONN_READ_READY:
    TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d Read Ready", my_peer_index);
    {
      int64_t n_read = 0, n_left, resp_content_size, available = TSIOBufferReaderAvail(pcd->response_buffer_reader);
      resp_content_size = 0;
      if (available > 0) {
        // TSVIONDoneSet(pcd->read_vio, available + TSVIONDoneGet(pcd->read_vio) + 2);

        TSIOBufferBlock block;
        const char *response_str = NULL;
        n_left                   = available;

        while (n_left > 0) {
          if (response_str == NULL) // first read
            block = TSIOBufferReaderStart(pcd->response_buffer_reader);
          else
            block = TSIOBufferBlockNext(block);
          response_str = TSIOBufferBlockReadStart(block, pcd->response_buffer_reader, &n_read);
          n_left       = available - n_read;

          if (txn_data->peer_resp_buf[my_peer_index] == NULL) {
            // now find the size of incoming request
            char *cp = strstr(response_str, "Content-Length");
            if (cp) {
              resp_content_size                      = strtol(cp + 15, NULL, 10) + 1; // add one for NULL
              txn_data->peer_resp_buf[my_peer_index] = TSmalloc(sizeof(char) * resp_content_size);
              CHECKNULL(txn_data->peer_resp_buf[my_peer_index]);
              txn_data->peer_resp_buf[my_peer_index][0] = '\0';
              TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d find and allocate size %" PRId64 " buffer for peer response",
                      my_peer_index, resp_content_size);
            } else
            // we didn't get Content-Length in the first read, maybe because first read is too small
            // txn_data->peer_resp_buf[my_peer_index] = TSrealloc(txn_data->peer_resp_buf[my_peer_index], sizeof(char) *
            // resp_content_size);
            // CHECKNULL(txn_data->peer_resp_buf[my_peer_index]);
            {
              char *temp = TSstrndup(response_str, n_read);
              TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d didn't find Content-Length, received %s", my_peer_index, temp);
              TSfree(temp);
            }
          }

          if (txn_data->peer_resp_buf[my_peer_index] != NULL) {
            // char *temp = TSstrndup(response_str, n_read);
            // TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d find and maybe copy for %s", my_peer_index, temp);
            // TSfree(temp);

            if ((!pcd->content_has_began) && (strstr(response_str, "\r\n\r\n"))) {
              // TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d response_str %p %p %p %" PRId64, my_peer_index, response_str,
              //         strstr(response_str, "\r\n\r\n"), strstr(response_str, "\r\n\r\n") + 4,
              //         strstr(response_str, "\r\n\r\n") + 4 - response_str);

              pcd->content_has_began = true;
              // TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d n_read update %" PRId64 " minus %" PRId64, my_peer_index, n_read,
              //         strstr(response_str, "\r\n\r\n") + 4 - response_str);
              n_read -= strstr(response_str, "\r\n\r\n") + 4 - response_str;
              response_str = strstr(response_str, "\r\n\r\n") + 4;
            }

            if (pcd->content_has_began && n_read > 0) {
              TSstrlcat(txn_data->peer_resp_buf[my_peer_index], response_str, n_read + 1); // because it appends NULL
              TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d copy size %" PRId64 " content", my_peer_index, n_read);
            } else {
              if (n_read > 0) {
                char *temp = TSstrndup(response_str, n_read);
                TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d content has began %d, n_read %" PRId64 ", read in %s",
                        my_peer_index, pcd->content_has_began, n_read, temp);
                TSfree(temp);
              } else {
                TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d n_read %" PRId64, my_peer_index, n_read);
              }
            }

            // TSDebug(PLUGIN_NAME,
            //         "peer_conn_handle: peer %d read and save response %" PRId64 " available, %" PRId64 " read, %" PRId64
            //         " left, total size %" PRId64,
            //         my_peer_index, available, n_read, n_left, resp_content_size);
          }

          // TSstrlcat(txn_data->peer_resp_buf[my_peer_index], response_str, resp_content_size > 0 ? resp_content_size : available);
        }

        TSVIONDoneSet(pcd->read_vio, available + TSVIONDoneGet(pcd->read_vio));
        TSIOBufferReaderConsume(pcd->response_buffer_reader, available);
        // TSVIONDoneSet(pcd->read_vio, n_read + TSVIONDoneGet(pcd->read_vio));
        // TSIOBufferReaderConsume(pcd->response_buffer_reader, n_read);
      }
      TSVIOReenable(pcd->read_vio);
    }
    break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d Write Complete", my_peer_index);
    pcd->read_vio = TSVConnRead(pcd->vconn, contp, pcd->response_buffer, INT64_MAX);
    CHECKNULL(pcd->read_vio);
    TSVConnShutdown(pcd->vconn, 0, 1);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(PLUGIN_NAME, "peer_conn_handle: peer %d Write Ready (Done: %" PRId64 " Todo: %" PRId64 ")", my_peer_index,
            TSVIONDoneGet(pcd->write_vio), TSVIONTodoGet(pcd->write_vio));
    TSVIOReenable(pcd->write_vio);
    break;
  case 106:
  case TS_EVENT_TIMEOUT:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    TSDebug(PLUGIN_NAME, "HttpTransaction: Timeout");
    break;
  default:
    TSDebug(PLUGIN_NAME, "peer_conn_handler: peer %d unknown TS_EVENT %d", my_peer_index, event);
    assert(false); // UNRECHEABLE.
  }
  return TS_SUCCESS;
}
*/

void
cleanup_peer_coon(TSCont contp)
{
  PeerConnData *pcd = (PeerConnData *)TSContDataGet(contp);
  TSContDataSet(contp, NULL);
  ;
  int my_peer_index = pcd->peer->index;
  // cleanup pcd

  TSVConnClose(pcd->vconn);

  TSIOBufferReaderFree(pcd->request_buffer_reader);
  TSIOBufferReaderFree(pcd->response_buffer_reader);
  TSIOBufferDestroy(pcd->response_buffer);
  // TSfree(pcd->request_string);
  // TSfree(pcd->request);
  // TSfree(pcd->response);

  TSfree(pcd);
  TSContDestroy(contp);
  pcd = NULL;
  TSDebug(PLUGIN_NAME, "peer conn %d destroyed", my_peer_index);
}

void
cleanup_main_contp(TSCont contp)
{
  TSDebug(PLUGIN_NAME, "begin to destroy mainCont");
  TxnData *txn_data = (TxnData *)TSContDataGet(contp);

  TSMutexDestroy(txn_data->mtx);
  TSIOBufferDestroy(txn_data->request_buffer);
  TSfree(txn_data->request_path_component);
  TSfree(txn_data->request_string);
  for (int i = 0; i < EC_k + EC_x; i++)
    TSfree(txn_data->peer_resp_buf[i]);

  TSfree(txn_data->peer_resp_buf);
  TSfree(txn_data->final_resp);

  TSfree(txn_data);
  TSContDestroy(contp);
  TSDebug(PLUGIN_NAME, "mainCont destroyed");
}
