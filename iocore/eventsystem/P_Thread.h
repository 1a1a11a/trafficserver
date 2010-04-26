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

/****************************************************************************

  Basic Threads

  
  
****************************************************************************/
#ifndef _P_Thread_h_
#define _P_Thread_h_

#include "I_Thread.h"

  ///////////////////////////////////////////////
  // Common Interface impl                     //
  ///////////////////////////////////////////////
TS_INLINE
Thread::~
Thread()
{
}

TS_INLINE void
Thread::set_specific()
{
  ink_thread_setspecific(Thread::thread_data_key, this);
}

TS_INLINE Thread *
this_thread()
{
  return (Thread *) ink_thread_getspecific(Thread::thread_data_key);
}

TS_INLINE ink_hrtime
ink_get_hrtime()
{
  return Thread::cur_time;
}

TS_INLINE ink_hrtime
ink_get_based_hrtime()
{
  return Thread::cur_time;
}

#endif //_P_Thread_h_
