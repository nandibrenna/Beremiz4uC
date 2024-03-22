/****************************************************************************
#  Project Name: Beremiz 4 uC                                               #
#  Author(s): nandibrenna                                                   #
#  Created: 2024-03-15                                                      #
#  ======================================================================== #
#  Copyright © 2024 nandibrenna                                             #
#                                                                           #
#  Licensed under the Apache License, Version 2.0 (the "License");          #
#  you may not use this file except in compliance with the License.         #
#  You may obtain a copy of the License at                                  #
#                                                                           #
#      http://www.apache.org/licenses/LICENSE-2.0                           #
#                                                                           #
#  Unless required by applicable law or agreed to in writing, software      #
#  distributed under the License is distributed on an "AS IS" BASIS,        #
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or          #
#  implied. See the License for the specific language governing             #
#  permissions and limitations under the License.                           #
#                                                                           #
****************************************************************************/

#ifndef PLC_DEBUG_H
#define PLC_DEBUG_H
#include "erpc_PLCObject_common.h"

// uint32_t get_current_memory_usage();
// void deinitialize_trace_variables();
// void initialize_trace_variables(TraceVariables *traces);
// void remove_trace_sample(uint32_t index);
// void add_trace_sample(uint32_t tick, uint8_t *data, uint32_t dataLength);
// void FreeDebugData(void);
// int WaitDebugData(uint32_t *tick);
// void InitiateDebugTransfer(void);
// int TryEnterDebugSection(void);
// void LeaveDebugSection(void);
// int suspendDebug(int disable);
// void resumeDebug(void);
// int TracesSwap(bool swap);
PLCstatus_enum get_PLCStatus(void);
void plc_debug_thread(void *, void *, void *);


void __init_debug    (void);
void __cleanup_debug (void);
void __retrieve_debug(void);
int publish_debug (void);

#endif
