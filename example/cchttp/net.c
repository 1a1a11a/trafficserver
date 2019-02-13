

#include <string.h>
#include <arpa/inet.h>
#include <inttypes.h>

#include "ts/ts.h"
#include "Protocol.h"
#include "util.h"
#include "net.h"

extern TSTextLogObject protocol_plugin_log;
extern int EC_n;
extern int EC_k;
extern int EC_x;
EcPeer ec_peers[MAX_EC_NODES];
extern int ssn_data_ind;
extern int txn_data_ind;

static TSReturnCode peer_conn_handler(TSCont contp, TSEvent event, void *edata);
static void EC_K_back(TSHttpTxn txnp);

TSReturnCode
setup_ssn(TSHttpSsn ssnp, int n_peers)
{
  /* start TCP connection to each peer */
  SsnData *ssn_data           = (SsnData *)TSmalloc(sizeof(SsnData));
  ssn_data->n_connected_peers = 0;
  ssn_data->current_txn_data  = NULL;
  ssn_data->pcds              = (PeerConnData *)TSmalloc(sizeof(PeerConnData) * n_peers);

  for (int i = 0; i < n_peers; i++) {
    ssn_data->pcds[i].peer                   = ec_peers + i;
    ssn_data->pcds[i].response_buffer        = NULL;
    ssn_data->pcds[i].response_buffer_reader = NULL;

    // Jason::TODO::Do I have to do this?
    ssn_data->pcds[i].response_buffer = TSIOBufferCreate();
    CHECKNULL(ssn_data->pcds[i].response_buffer);
    ssn_data->pcds[i].response_buffer_reader = TSIOBufferReaderAlloc(ssn_data->pcds[i].response_buffer);
    CHECKNULL(ssn_data->pcds[i].response_buffer_reader);
  }
  TSHttpSsnArgSet(ssnp, ssn_data_ind, ssn_data);

  // now setup connection to each peer
  TSCont contp;
  struct peer_conn_handle_data *pchd;

  TSAction action;
  for (int i = 0; i < n_peers; i++) {
    pchd           = TSmalloc(sizeof(struct peer_conn_handle_data));
    pchd->ssn_data = ssn_data;
    pchd->peer     = ec_peers + i;

    // Jason::Question::why do we need mutex?
    contp                   = TSContCreate(peer_conn_handler, TSMutexCreate());
    ssn_data->pcds[i].contp = contp;

    TSContDataSet(contp, pchd);
    action = TSNetConnect(contp, (struct sockaddr const *)&(ec_peers[i].addr));

    if (MY_DEBUG_LEVEL > 3 && TSActionDone(action)) {
      TSDebug(PLUGIN_NAME, "setup_peer_conns: Network connection finish fast (%s)", ec_peers[i].addr_str);
    }
  }
  return TS_SUCCESS;
}

TSReturnCode
clean_ssn(TSHttpSsn ssnp, int n_peers)
{
  /* close TCP connection to each peer */
  SsnData *ssn_data = TSHttpSsnArgGet(ssnp, ssn_data_ind);
  for (int i = 0; i < n_peers; i++) {
    TSVConnClose(ssn_data->pcds[i].vconn);
    TSContDestroy(ssn_data->pcds[i].contp);
    TSIOBufferReaderFree(ssn_data->pcds[i].response_buffer_reader);
    TSIOBufferDestroy(ssn_data->pcds[i].response_buffer);
    // TSIOBufferReaderFree(ssn_data->pcds[i].request_buffer_reader);
    ssn_data->n_connected_peers--;
  }
  TSAssert(ssn_data->n_connected_peers == 0);
  return TS_SUCCESS;
}

TSReturnCode
setup_txn(TSCont contp, TSHttpTxn txnp)
{
  TSHttpSsn ssnp             = TSHttpTxnSsnGet(txnp);
  SsnData *ssn_data          = TSHttpSsnArgGet(ssnp, ssn_data_ind);
  TxnData *txn_data          = (TxnData *)TSmalloc(sizeof(TxnData));
  memset(txn_data, 0, sizeof(TxnData)); 
  ssn_data->current_txn_data = txn_data;

  txn_data->txnp = txnp;
  sprintf(txn_data->ssn_txn_id, "%ld-%ld", TSHttpSsnIdGet(ssnp), TSHttpTxnIdGet(txnp));
  txn_data->status                 = EC_STATUS_BEGIN;
  txn_data->final_resp             = NULL;
  txn_data->request_path_component = NULL;
  txn_data->peer_resp_buf          = (char **)TSmalloc(sizeof(char *) * (EC_k + EC_x - 1));
  // Jason::Optimize:: move to ssn should improve performance
  // txn_data->peer_resp_readers      = (TSIOBufferReader *)TSmalloc(sizeof(TSIOBufferReader) * (EC_k + EC_x - 1));
  txn_data->request_buffer_readers = (TSIOBufferReader *)TSmalloc(sizeof(TSIOBufferReader) * (EC_k + EC_x - 1));
  memset(txn_data->peer_resp_buf, 0, sizeof(char *) * (EC_k + EC_x - 1));
  txn_data->n_available_peers = 0;
  txn_data->ready_peers       = 0;

  txn_data->output_buffer = TSIOBufferCreate();
  txn_data->output_reader = TSIOBufferReaderAlloc(txn_data->output_buffer);
  txn_data->output_vio    = NULL;

  // txn_data->my_temp_buffer = NULL;
  // txn_data->my_temp_reader = NULL;
  // txn_data->osize          = 0;

  TSHttpTxnArgSet(txnp, txn_data_ind, txn_data);

  for (int i = 0; i < EC_k + EC_x - 1; i++) {
    // initialize per transaction structure
    ssn_data->pcds[i].content_has_began = false;
    // txn_data->peer_resp_readers[i]      = TSIOBufferReaderAlloc(ssn_data->pcds[i].response_buffer);
    ssn_data->pcds[i].content_length = 0;
    ssn_data->pcds[i].read_in_length = 0;
    // Jason::TODO::Do I have to do this?
    // ssn_data->pcds[i].response_buffer   = TSIOBufferCreate();
    // CHECKNULL(ssn_data->pcds[i].response_buffer);
    // ssn_data->pcds[i].response_buffer_reader = TSIOBufferReaderAlloc(ssn_data->pcds[i].response_buffer);
    // CHECKNULL(ssn_data->pcds[i].response_buffer_reader);
  }
  TSMBuffer bufp;
  TSMLoc hdr_loc, url_loc;
  const char *path;
  int path_length;

  // get header
  if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve client request header txn %" PRId64, PLUGIN_NAME, TSHttpTxnIdGet(txnp));
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_ERROR;
  }
  // get url
  if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
    TSError("[%s] Couldn't retrieve request url txn %" PRId64, PLUGIN_NAME, TSHttpTxnIdGet(txnp));
    TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
    TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
    return TS_ERROR;
  }

  // Jason::Debug
  path                             = TSUrlPathGet(bufp, url_loc, &path_length);
  txn_data->request_path_component = TSstrndup(path, path_length);
  if (txn_data->request_path_component == NULL || *(txn_data->request_path_component) == '\0')
    txn_data->request_path_component = TSstrdup("root");
  TSDebug(PLUGIN_NAME, "setup_txn: txn %" PRId64 " request path %s", TSHttpTxnIdGet(txnp), txn_data->request_path_component);
  // 256 is large enough
  txn_data->request_string = (char *)TSmalloc(sizeof(char) * (path_length + 256));
  // sprintf(txn_data->request_string, "GET /size/12800 HTTP/1.1\r\nHost: d30.jasony.me:8080\r\n\r\n",
  //         txn_data->request_path_component);
  sprintf(txn_data->request_string, "GET /%s-peerConn HTTP/1.1\r\nHost: d30.jasony.me:8080\r\n\r\n",
          txn_data->request_path_component);
  txn_data->request_buffer = TSIOBufferCreate();
  CHECKNULL(txn_data->request_buffer);
  TSIOBufferWrite(txn_data->request_buffer, txn_data->request_string, strlen(txn_data->request_string));

  TSDebug(PLUGIN_NAME, "setup_txn: txn %" PRId64 " I will connect to %d peers, request %s", TSHttpTxnIdGet(txnp), EC_k + EC_x - 1,
          txn_data->request_string);
  for (int i = 0; i < EC_k + EC_x - 1; i++) {
    //   TSDebug(PLUGIN_NAME, "call %p for peer %s", ssn_data->pcds[i].contp, ssn_data->pcds[i].peer->addr_str);
    TSContCall(ssn_data->pcds[i].contp, EC_EVENT_PEER_BEGIN_WRITE, txn_data);
  }

  TSHandleMLocRelease(bufp, hdr_loc, url_loc);
  TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
  // TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
  TSDebug(PLUGIN_NAME, "setup_txn: txn %" PRId64 " request setup finished", TSHttpTxnIdGet(txnp));
  return TS_SUCCESS;
}

TSReturnCode
clean_txn(TSCont contp, TSHttpTxn txnp)
{
  TxnData *txn_data = TSHttpTxnArgGet(txnp, txn_data_ind);
  //   TSHttpSsn ssnp    = TSHttpTxnSsnGet(txnp);
  //   SsnData *ssn_data = TSHttpSsnArgGet(ssnp, ssn_data_ind);
  TSIOBufferDestroy(txn_data->request_buffer);
  for (int i = 0; i < EC_k + EC_x - 1; i++) {
    if (txn_data->peer_resp_buf[i])
      TSfree(txn_data->peer_resp_buf[i]);
    TSIOBufferReaderFree(txn_data->request_buffer_readers[i]);
    // TSIOBufferReaderFree(txn_data->peer_resp_readers[i]);
    // Jason::TODO::Do I have to do this?
    // TSIOBufferDestroy(ssn_data->pcds[i].response_buffer);
  }

  TSfree(txn_data->request_buffer_readers);
  if (txn_data->final_resp)
    TSfree(txn_data->final_resp);
  if (txn_data->peer_resp_buf)
    TSfree(txn_data->peer_resp_buf);
  if (txn_data->request_path_component)
    TSfree(txn_data->request_path_component);
  if (txn_data->request_string)
    TSfree(txn_data->request_string);
  TSIOBufferReaderFree(txn_data->output_reader);
  TSIOBufferDestroy(txn_data->output_buffer);
  TSfree(txn_data);

  return TS_SUCCESS;
}

static TSReturnCode
peer_conn_handler(TSCont contp, TSEvent event, void *edata)
{
  struct peer_conn_handle_data *pchd = TSContDataGet(contp);
  SsnData *ssn_data                  = pchd->ssn_data;
  EcPeer *peer                       = pchd->peer;
  PeerConnData *pcd                  = &(ssn_data->pcds[peer->index]);
  TxnData *txn_data                  = ssn_data->current_txn_data;
  // int64_t txn_id                     = -1;
  // if (txn_data != NULL && txn_data->txnp != NULL)
  //   txn_id = TSHttpTxnIdGet(txn_data->txnp);

  if (event == EC_EVENT_PEER_BEGIN_WRITE)
    TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d receives EC_EVENT_PEER_BEGIN_WRITE", txn_data->ssn_txn_id,
            peer->index);
  else{
    if (txn_data == NULL)
      TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d receives %s", "x-x", peer->index,
              TSHttpEventNameLookup(event));
    else
      TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d receives %s", txn_data->ssn_txn_id, peer->index,
            TSHttpEventNameLookup(event));
  }

  switch (event) {
  case TS_EVENT_ERROR:
    TSError("[%s] peer_conn_handler: txn %s peer %d ERROR", PLUGIN_NAME, txn_data->ssn_txn_id, peer->index);
    // cleanup_peer_coon(contp);
    break;
  case TS_EVENT_NET_CONNECT:
    // the mutex is needed only when we want to operate on ssn_data
    // TSMutexLock(ssn_data->mtx);
    // TSDebug(PLUGIN_NAME, "peer_conn_handler: peer %d %s connect", peer->index, peer->addr_str);
    ;
    int n_connected_peers = __sync_add_and_fetch(&(ssn_data->n_connected_peers), 1);
    pcd->vconn            = (TSVConn)edata;
    // TSMutexUnlock(ssn_data->mtx);
    TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d (%s) Net Connect, %d peers have connected", "x-x",
            peer->index, peer->addr_str, n_connected_peers);
    break;

  case EC_EVENT_PEER_BEGIN_WRITE:
    txn_data = (TxnData *)edata;
    TSAssert(txn_data == ssn_data->current_txn_data);

    txn_data->request_buffer_readers[peer->index] = TSIOBufferReaderAlloc(txn_data->request_buffer);
    CHECKNULL(txn_data->request_buffer_readers[peer->index]);

    pcd->write_vio =
      TSVConnWrite(pcd->vconn, contp, txn_data->request_buffer_readers[peer->index], strlen(txn_data->request_string));
    break;

  case TS_EVENT_VCONN_WRITE_COMPLETE:
    pcd->read_vio = TSVConnRead(pcd->vconn, contp, pcd->response_buffer, INT64_MAX);
    CHECKNULL(pcd->read_vio);
    break;

  case TS_EVENT_VCONN_WRITE_READY:
    TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d Write Ready (Done: %" PRId64 " Todo: %" PRId64 ")",
            txn_data->ssn_txn_id, peer->index, TSVIONDoneGet(pcd->write_vio), TSVIONTodoGet(pcd->write_vio));
    TSVIOReenable(pcd->write_vio);
    break;

  case TS_EVENT_VCONN_EOS:
  case TS_EVENT_VCONN_READ_COMPLETE:
    ;
    // when the other peers support persistent connection, this can also be called
    int n_available     = __sync_add_and_fetch(&(txn_data->n_available_peers), 1);
    int64_t ready_peers = __sync_or_and_fetch(&(txn_data->ready_peers), (1 << peer->index));

    TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d receive all response, %d available, ready_peers %s",
            txn_data->ssn_txn_id, peer->index, n_available, int64_to_bitstring_static(ready_peers));

    // might have problem due to data race
    if (n_available == EC_k + EC_x - 1) {
      // now wake up main continuation
      // Jason::Optimize::maybe we should still use event
      // TSContCall(txn_data->contp, EC_EVENT_K_BACK, txn_data->txnp);
      EC_K_back(txn_data->txnp);
    } else if (n_available > EC_k + EC_x - 1) {
      TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d response is not needed", txn_data->ssn_txn_id, peer->index);
    }

    // TSIOBufferDestroy(pcd->response_buffer);
    // TSIOBufferReaderFree(txn_data->request_buffer_readers[peer->index]);
    // TSIOBufferReaderFree(pcd->response_buffer_reader);

    break;
  case TS_EVENT_VCONN_READ_READY: {
    TxnData *txn_data = ssn_data->current_txn_data;
    int64_t n_read = 0, n_left, available = TSIOBufferReaderAvail(pcd->response_buffer_reader);
    if (available > 0) {
      TSIOBufferBlock block;
      const char *response_str = NULL;
      n_left                   = available;
      while (n_left > 0) {
        if (response_str == NULL) { // first read
          block = TSIOBufferReaderStart(pcd->response_buffer_reader);
        } else {
          TSDebug(PLUGIN_NAME, "blockNext called");
          block = TSIOBufferBlockNext(block);
        }
        // may be we don't want to extract the string now as we can pass the reader around?
        response_str = TSIOBufferBlockReadStart(block, pcd->response_buffer_reader, &n_read);
        n_left       = available - n_read;
        if (MY_DEBUG_LEVEL > 3) {
          char *temp = TSstrndup(response_str, n_read);
          TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d received %s", txn_data->ssn_txn_id, peer->index, temp);
          TSfree(temp);
        }
        if (txn_data->peer_resp_buf[peer->index] == NULL) {
          // now find the size of incoming request
          char *cp = strstr(response_str, "Content-Length");
          if (cp && cp - response_str + 15 < n_read) {
            pcd->content_length                  = strtol(cp + 15, NULL, 10);
            txn_data->peer_resp_buf[peer->index] = TSmalloc(sizeof(char) * pcd->content_length + 1); // add one for NULL
            CHECKNULL(txn_data->peer_resp_buf[peer->index]);
            txn_data->peer_resp_buf[peer->index][0] = '\0';
            TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d find and allocate size %" PRId64 " buffer for peer response",
                    txn_data->ssn_txn_id, peer->index, pcd->content_length);
          } else {
            // char *temp = TSstrndup(response_str, n_read);
            TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d didn't find Content-Length", txn_data->ssn_txn_id, peer->index);
            // TSfree(temp);
          }
        }

        if (txn_data->peer_resp_buf[peer->index] != NULL) {
          char *find_loc = strstr(response_str, "\r\n\r\n");
          // new
          if ((!pcd->content_has_began) && (find_loc != NULL) && (find_loc - response_str <= n_read)) {
            pcd->content_has_began = true;
            n_read -= strstr(response_str, "\r\n\r\n") + 4 - response_str;
            response_str = strstr(response_str, "\r\n\r\n") + 4;
          }

          if (pcd->content_has_began && n_read > 0) {
            TSstrlcat(txn_data->peer_resp_buf[peer->index], response_str, n_read + 1); // because it appends NULL
            TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d copy size %" PRId64 " content", txn_data->ssn_txn_id,
                    peer->index, n_read);
            pcd->read_in_length += n_read;
          } else {
            if (n_read > 0) {
              char *temp = TSstrndup(response_str, n_read);
              TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d content has began %d, n_read %" PRId64 ", read in %s",
                      txn_data->ssn_txn_id, peer->index, pcd->content_has_began, n_read, temp);
              TSfree(temp);
            } else if (n_read == 0)
              ;
            else {
              TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d ERROR n_read less than 0, n_read %" PRId64,
                      txn_data->ssn_txn_id, peer->index, n_read);
              response_str = TSIOBufferBlockReadStart(block, pcd->response_buffer_reader, &n_read);
              char *temp   = TSstrndup(response_str, n_read);
              TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d n_read %" PRId64 ", read in %s", txn_data->ssn_txn_id,
                      peer->index, n_read, temp);
              TSfree(temp);
            }
          }
        }
      }

      // TSVIONDoneSet(pcd->read_vio, available + TSVIONDoneGet(pcd->read_vio));
      // Jason::DEBUG::disable this for reading in ec
      TSIOBufferReaderConsume(pcd->response_buffer_reader, available);
    }

    if (pcd->content_length != 0 && pcd->content_length == pcd->read_in_length) {
      TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d read finish %" PRId64 " Bytes/%" PRId64 " Bytes",
              txn_data->ssn_txn_id, peer->index, pcd->read_in_length, pcd->content_length);
      TSContCall(contp, TS_EVENT_VCONN_READ_COMPLETE, txn_data);
    } else if (pcd->content_length != 0 && pcd->content_length < pcd->read_in_length) {
      TSDebug(PLUGIN_NAME,
              "peer_conn_handler: txn %s peer %d read error, read in more than available %" PRId64 " Bytes/%" PRId64 " Bytes",
              txn_data->ssn_txn_id, peer->index, pcd->read_in_length, pcd->content_length);
    } else {
      TSDebug(PLUGIN_NAME,
              "peer_conn_handler: txn %s peer %d read not finish %" PRId64 " Bytes/%" PRId64 " Bytes, reenable read_vio",
              txn_data->ssn_txn_id, peer->index, pcd->read_in_length, pcd->content_length);
      TSVIOReenable(pcd->read_vio);
    }
  } break;
  case 106:
  case TS_EVENT_TIMEOUT:
  case TS_EVENT_VCONN_INACTIVITY_TIMEOUT:
    TSDebug(PLUGIN_NAME, "HttpTransaction: Timeout");
    break;
  default:
    TSDebug(PLUGIN_NAME, "peer_conn_handler: txn %s peer %d unknown TS_EVENT %s", txn_data->ssn_txn_id, peer->index,
            TSHttpEventNameLookup(event));
    assert(false); // UNRECHEABLE.
  }
  return TS_SUCCESS;
}

static void
EC_K_back(TSHttpTxn txnp)
{
  TxnData *txn_data = TSHttpTxnArgGet(txnp, txn_data_ind);
  TSDebug(PLUGIN_NAME, "EC_K_back: txn %s K+X-1 resp back! available peers %d %s", txn_data->ssn_txn_id,
          txn_data->n_available_peers, int64_to_bitstring_static(txn_data->ready_peers));
  // check k event
  int64_t available_peers = 0, final_size = 0;
  int current_peer_available = 0;
  for (int i = 0; i < EC_n; i++) {
    // current_peer_available = __sync_and_and_fetch(&(txn_data->ready_peers), (1 << i));
    current_peer_available = txn_data->ready_peers & (1 << i);
    // current_peer_available = txn_data->n_available_peers & (1 << i);
    // if (txn_data->peer_resp_buf[i] != NULL) {

    if (current_peer_available > 0) {
      available_peers++;
      final_size += strlen(txn_data->peer_resp_buf[i]);
      TSDebug(PLUGIN_NAME, "EC_K_back: txn %s response %d %s", txn_data->ssn_txn_id, i, txn_data->peer_resp_buf[i]);
    }
  }
  TSAssert(available_peers >= EC_k - 1);
  TSDebug(PLUGIN_NAME, "EC_K_back: txn %s K %d X %d, response size sum is %" PRId64, txn_data->ssn_txn_id, EC_k, EC_x, final_size);

  txn_data->final_resp    = TSmalloc(sizeof(char) * final_size);
  txn_data->final_resp[0] = '\0';
  for (int i = 0; i < EC_k + EC_x - 1; i++) {
    if (txn_data->peer_resp_buf[i] != NULL) {
      TSstrlcat(txn_data->final_resp, txn_data->peer_resp_buf[i], final_size + 1);
    }
  }
  TSDebug(PLUGIN_NAME, "EC_K_back: txn %s final resp %s", txn_data->ssn_txn_id, txn_data->final_resp);
  txn_data->status = EC_STATUS_PEER_RESP_READY;
}