

#pragma once






struct peer_conn_handle_data{
    SsnData *ssn_data;
    EcPeer *peer;
};

TSReturnCode setup_ssn(TSHttpSsn ssnp, int n_peers);
TSReturnCode clean_ssn(TSHttpSsn ssnp, int n_peers);
TSReturnCode setup_txn(TSCont contp, TSHttpTxn txnp);
TSReturnCode clean_txn(TSCont contp, TSHttpTxn txnp);
TSReturnCode conn_peer(TSCont contp, TSHttpTxn txnp); 