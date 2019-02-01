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

#include "Protocol.h"
#include "util.h"
#include <string.h>
#include <arpa/inet.h>

typedef int (*TxnSMHandler)(TSCont contp, TSEvent event, void *data);

// TSCont TxnSMCreate(TSMutex pmutex, TSVConn client_vc, int server_port);
// TSCont TxnSMCreate(TSMutex pmutex, TSHttpTxn txn);
void setup_ec(TSCont contp, TSMutex mtx, TSHttpTxn txn);
void cleanup_peer_coon(TSCont contp);
void cleanup_main_contp(TSCont contp);
void EC_retrieve_from_peers(TSCont main_contp, TxnData *txn_data);