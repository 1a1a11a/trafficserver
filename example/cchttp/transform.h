
#ifndef TRANSFORM_H
#define TRANSFORM_H


#include <stdlib.h>
#include <string.h>
#include "ts/ts.h"
#include "util.h"
#include "ec.h"


void handle_transform(TSCont contp, TxnData *txn_data);
int RS_resp_transform_handler(TSCont contp, TSEvent event, void *edata); 

#endif 