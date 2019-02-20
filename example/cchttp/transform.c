
#include "Protocol.h"
#include "transform.h"
#include "ec.h"

extern TSTextLogObject protocol_plugin_log;
extern int txn_data_ind;
extern EcPeer ec_peers[MAX_EC_NODES];
extern int EC_n, EC_k, EC_x;
extern int n_peers;
extern int current_node_EC_index;
extern int ssn_data_ind, txn_data_ind;


int
RS_resp_transform_handler(TSCont contp, TSEvent event, void *edata)
{
  // Jason::DEBUG::WEIRD what is edata here? it is not txnp
  TxnData *txn_data = TSContDataGet(contp);

  // if (TSMutexLockTry(txn_data->txnp_mtx) != TS_SUCCESS){
  //   // Jason::DEBUG:: I don't this is not necessary
  //   // TSContSchedule(contp, 1, TS_THREAD_POOL_DEFAULT);
  //   return TS_SUCCESS;
  // }
  
  // if I don't have this, I will not get Connection:close in debug version, but I get them in release version 

  TSMutexLock(txn_data->txnp_mtx);
  if (TSVConnClosedGet(contp)) {
    TSDebug(PLUGIN_NAME, "RS_resp_transform: txn %s VConn is closed event %s (%d)", txn_data->ssn_txn_id, TSHttpEventNameLookup(event), event);
    TSContDestroy(contp);
    TSMutexUnlock(txn_data->txnp_mtx);
    return 0;
  }
  else 
    // unknow event is from EC_K_BACK 
    TSDebug(PLUGIN_NAME, "RS_resp_transform: txn %s event %s (%d)", txn_data->ssn_txn_id, TSHttpEventNameLookup(event), event);

  switch (event) {
  case TS_EVENT_ERROR: {
    TSVIO input_vio;

    //   TSDebug(PLUGIN_NAME, "\tEvent is TS_EVENT_ERROR");
    /* Get the write VIO for the write operation that was
     * performed on ourself. This VIO contains the continuation of
     * our parent transformation. This is the input VIO.
     */
    input_vio = TSVConnWriteVIOGet(contp);

    /* Call back the write VIO continuation to let it know that we
     * have completed the write operation.
     */
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_ERROR, input_vio);
  } break;
  case TS_EVENT_VCONN_WRITE_COMPLETE:
    /* When our output connection says that it has finished
     * reading all the data we've written to it then we should
     * shutdown the write portion of its connection to
     * indicate that we don't want to hear about it anymore.
     */
    // Jason::Debug:: this might be the problem causing closed conn in the header
      // TSVConnShutdown(TSTransformOutputVConnGet(contp), 0, 1);
    TSVConnClose(TSTransformOutputVConnGet(contp));
    break;

  /* If we get a WRITE_READY event or any other type of
   * event (sent, perhaps, because we were re-enabled) then
   * we'll attempt to transform more data.
   */
  case TS_EVENT_VCONN_WRITE_READY:
  case TS_EVENT_VCONN_START: // this is sent by EC_K_BACK
  case TS_EVENT_IMMEDIATE: // the first time it arrives
  case 2:                  // EVENT_INTERVAL when called by TS_Cont_Schedule
    handle_transform(contp, txn_data);
    break;
  default:
    TSDebug(PLUGIN_NAME, "\t(event is %d)", event);
    handle_transform(contp, txn_data);
    break;
  }

  TSMutexUnlock(txn_data->txnp_mtx);
  return 0;
}

void
handle_transform0(TSCont contp, TxnData *txn_data)
{
  TSVConn output_conn;
  TSIOBuffer buf_test;
  TSVIO input_vio;
  int64_t towrite;
  int64_t avail;

  // int64_t txn_id = -1;
  // if (txn_data != NULL && txn_data->txnp != NULL)
  //   txn_id = TSHttpTxnIdGet(txn_data->txnp);

  /* Get the output (downstream) vconnection where we'll write data to. */

  output_conn = TSTransformOutputVConnGet(contp);

  /* Get the write VIO for the write operation that was performed on
   * ourself. This VIO contains the buffer that we are to read from
   * as well as the continuation we are to call when the buffer is
   * empty. This is the input VIO (the write VIO for the upstream
   * vconnection).
   */
  input_vio = TSVConnWriteVIOGet(contp);

  /* Get our data structure for this operation. The private data
   * structure contains the output VIO and output buffer. If the
   * private data structure pointer is NULL, then we'll create it
   * and initialize its internals.
   */

  buf_test = TSVIOBufferGet(input_vio);

  if (!buf_test) {
    TSDebug(PLUGIN_NAME, "handle_transform: txn %s buf_test", txn_data->ssn_txn_id);
    // TSVIONBytesSet(txn_data->output_vio, txn_data->osize); 
    TSVIOReenable(txn_data->output_vio);
    return;
  }

  if (txn_data->local_finish_ts == 0)
    record_time(protocol_plugin_log, txn_data, LOCAL_FINISH, NULL);
  TSDebug(PLUGIN_NAME, "handle_transform: txn %s local finish", txn_data->ssn_txn_id);

  TSMutexLock(txn_data->transform_mtx);


  if (txn_data->status != EC_STATUS_PEER_RESP_READY) {
    txn_data->status = EC_STATUS_LOCAL_FINISH;
    TSDebug(PLUGIN_NAME, "handle_transform: txn %s wait %d ms for peer response", txn_data->ssn_txn_id, txn_data->reschedule_wait+1);
    TSMutexUnlock(txn_data->transform_mtx);

    // use schedule will bring huge latency 
    // txn_data->reschedule_wait ++;
    // if (unlikely(txn_data->reschedule_wait > 125))
    //   txn_data->reschedule_wait = 125; 
    // TSContSchedule(contp, txn_data->reschedule_wait, TS_THREAD_POOL_DEFAULT);
    return;
  }
  TSMutexUnlock(txn_data->transform_mtx);

  if (txn_data->my_temp_reader == NULL) {
    if (MY_DEBUG_LEVEL>3)
      TSDebug(PLUGIN_NAME, "handle_transform: txn %s create response temp buffer, peer final response %s", txn_data->ssn_txn_id,
            txn_data->final_resp);
    txn_data->my_temp_buffer = TSIOBufferCreate();
    txn_data->my_temp_reader = TSIOBufferReaderAlloc(txn_data->my_temp_buffer);
    txn_data->output_vio     = TSVConnWrite(output_conn, contp, txn_data->my_temp_reader, INT64_MAX);
    txn_data->osize          = strlen(txn_data->final_resp) + 2;
    TSIOBufferWrite(txn_data->my_temp_buffer, "||", 2);
    TSIOBufferWrite(txn_data->my_temp_buffer, txn_data->final_resp, strlen(txn_data->final_resp));
    TSDebug(PLUGIN_NAME, "handle_transform: txn %s done creating response temp buffer, current buffer ", txn_data->ssn_txn_id);

    record_time(protocol_plugin_log, txn_data, DECODING_START, NULL);
    // DO RS
    record_time(protocol_plugin_log, txn_data, DECODING_FINISH, NULL);
  }

  record_time(protocol_plugin_log, txn_data, RESPONSE_BEGIN, NULL);

  towrite = TSVIONTodoGet(input_vio);
  // need to check here, if towrite is 0, there is no reader to get from input_vio
  if (towrite > 0) {
    avail = TSIOBufferReaderAvail(TSVIOReaderGet(input_vio));
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */
    TSDebug(PLUGIN_NAME, "handle_transform: txn %s upstream vio towrite %" PRId64 " avail %" PRId64, txn_data->ssn_txn_id, towrite,
            avail);
    if (towrite > avail)
      towrite = avail;
  }

  if (towrite > 0) {
    TSIOBufferCopy(txn_data->my_temp_buffer, TSVIOReaderGet(input_vio), towrite, 0);
    // TSIOBufferWrite(txn_data->my_temp_buffer, TSVIOReaderGet(input_vio), towrite);
    // TSIOBufferCopy(TSVIOBufferGet(txn_data->output_vio), TSVIOReaderGet(input_vio), towrite, 0);
    TSIOBufferReaderConsume(TSVIOReaderGet(input_vio), towrite);
    TSVIONDoneSet(input_vio, TSVIONDoneGet(input_vio) + towrite);

    txn_data->osize += towrite;
    // TSDebug(PLUGIN_NAME, "write %ld Bytes", TSIOBufferWrite(TSVIOBufferGet(input_vio), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", 31));
    // print_reader(PLUGIN_NAME, TSIOBufferReaderAlloc(txn_data->my_temp_buffer));

    TSVIONBytesSet(txn_data->output_vio, txn_data->osize);
    TSVIOReenable(txn_data->output_vio);

    TSDebug(
      PLUGIN_NAME,
      "handle_transform: txn %s set input NDone from %ld to %ld, consume %ld Bytes, set output NBytes %ld, total output size %ld",
      txn_data->ssn_txn_id, TSVIONDoneGet(input_vio) - towrite, TSVIONDoneGet(input_vio), towrite, txn_data->osize,
      txn_data->osize);
  }

  // if (towrite <= 0){
  //   TSDebug(PLUGIN_NAME, "begin to write ");
  //   print_reader(PLUGIN_NAME, TSIOBufferReaderAlloc(txn_data->my_temp_buffer));
  //   txn_data->output_vio = TSVConnWrite(output_conn, contp, txn_data->my_temp_reader,
  //   TSIOBufferReaderAvail(txn_data->my_temp_reader));
  // }

  if (TSVIONTodoGet(input_vio) > 0) {
    if (towrite > 0) {
      TSDebug(PLUGIN_NAME, "handle_transform: txn %s more to read from input_vio", txn_data->ssn_txn_id);
      // TSVIONBytesSet(txn_data->output_vio, txn_data->osize);
      // TSVIOReenable(txn_data->output_vio);
      TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_READY, input_vio);
    } else {
      TSDebug(PLUGIN_NAME, "let me wait ");
      // TSAssert(false);
    }
  } else {
    TSDebug(PLUGIN_NAME, "handle_transform: txn %s none to read from input_vio, all writes done, set %" PRId64 " bytes for output",
            txn_data->ssn_txn_id, txn_data->osize);
    // TSVIONBytesSet(txn_data->output_vio, txn_data->osize);
    // TSVIOReenable(txn_data->output_vio);
    TSContCall(TSVIOContGet(input_vio), TS_EVENT_VCONN_WRITE_COMPLETE, input_vio);
  }

  return;
}

void
handle_transform2(TSCont contp, TxnData *txn_data)
{
  TSVIO write_vio;
  int towrite;
  int avail;
  TSVConn output_conn;

  /* Get the write VIO for the write operation that was performed on
     ourself. This VIO contains the buffer that we are to read from
     as well as the continuation we are to call when the buffer is
     empty. */
  write_vio = TSVConnWriteVIOGet(contp);

  /* Create the output buffer and its associated reader */
  if (!txn_data->output_buffer) {
    txn_data->output_buffer = TSIOBufferCreate();
    TSAssert(txn_data->output_buffer);
    txn_data->output_reader = TSIOBufferReaderAlloc(txn_data->output_buffer);
    TSAssert(txn_data->output_reader);
  }

  /* We also check to see if the write VIO's buffer is non-NULL. A
     NULL buffer indicates that the write operation has been
     shutdown and that the continuation does not want us to send any
     more WRITE_READY or WRITE_COMPLETE events. For this buffered
     transformation that means we're done buffering data. */

  if (!TSVIOBufferGet(write_vio)) {
    /* Get the output connection where we'll write data to. */
    output_conn = TSTransformOutputVConnGet(contp);
    txn_data->output_vio =
      TSVConnWrite(output_conn, contp, txn_data->output_reader, TSIOBufferReaderAvail(txn_data->output_reader));

    return;
  }

  /* Determine how much data we have left to read. For this bnull
     transform plugin this is also the amount of data we have left
     to write to the output connection. */

  towrite = TSVIONTodoGet(write_vio);
  if (towrite > 0) {
    /* The amount of data left to read needs to be truncated by
       the amount of data actually in the read buffer. */

    avail = TSIOBufferReaderAvail(TSVIOReaderGet(write_vio));
    if (towrite > avail) {
      towrite = avail;
    }

    if (towrite > 0) {
      /* Copy the data from the read buffer to the input buffer. */
      TSIOBufferCopy(txn_data->output_buffer, TSVIOReaderGet(write_vio), towrite, 0);

      /* Tell the read buffer that we have read the data and are no
         longer interested in it. */
      TSIOBufferReaderConsume(TSVIOReaderGet(write_vio), towrite);

      /* Modify the write VIO to reflect how much data we've
         completed. */
      TSVIONDoneSet(write_vio, TSVIONDoneGet(write_vio) + towrite);
    }
  }

  /* Now we check the write VIO to see if there is data left to read. */
  if (TSVIONTodoGet(write_vio) > 0) {
    if (towrite > 0) {
      /* Call back the write VIO continuation to let it know that we
         are ready for more data. */
      TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_READY, write_vio);
    }
  } else {
    output_conn = TSTransformOutputVConnGet(contp);
    txn_data->output_vio =
      TSVConnWrite(output_conn, contp, txn_data->output_reader, TSIOBufferReaderAvail(txn_data->output_reader));

    /* Call back the write VIO continuation to let it know that we
       have completed the write operation. */
    TSContCall(TSVIOContGet(write_vio), TS_EVENT_VCONN_WRITE_COMPLETE, write_vio);
  }

  return;
}

void
handle_transform(TSCont contp, TxnData *txn_data)
{
  handle_transform0(contp, txn_data);
}
