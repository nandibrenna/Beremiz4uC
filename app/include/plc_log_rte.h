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

#ifndef PLC_LOG_RTE_H
#define PLC_LOG_RTE_H
#include "erpc_PLCObject_common.h"

// helper functions
void initialize_logging(void);
void deinitialize_logging(void);
void backup_logging(void);
void LoadLogState(uint32_t *logCounts);
void SaveLogState(uint32_t *logCounts);
void logging_timer_callback(struct k_timer *timer_id);

// erpc functions
uint32_t GetLogCount(uint8_t level);
uint32_t ResetLogCount(void);
uint32_t GetLogMessage(uint8_t level, uint32_t msgID, log_message *message);

// rte function
int LogMessage(uint8_t level, char* buf, int8_t size);

// rte function to log in zephyr
void rte_log_inf(const char* fmt, ...);

#endif