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

#ifndef PLC_LOADER_H
#define PLC_LOADER_H
typedef unsigned int dbgvardsc_index_t;
#include "udynlink.h"

// RTE Functions
void config_init__(void);
void config_run__(unsigned long);
int GetDebugVariable(dbgvardsc_index_t idx, void *value, size_t *size);
int RegisterDebugVariable(dbgvardsc_index_t idx, void *forced, size_t *size);
size_t get_var_size(size_t idx);
void force_var(size_t idx, bool forced, void *val);
void set_trace(size_t idx, bool trace, void *val);
void trace_reset(void);

void plc_set_start(void);
void plc_set_stop(void);
int plc_get_state(void);
int plc_get_loader_state(void);
int erase_plc_programm_flash(int state);
void reload_plc(void);
int load_plc_module(char *filename);
int unload_plc_module(void);

extern uint32_t plc_initialized;
extern uint32_t plc_run;
extern udynlink_module_t mod;
#endif