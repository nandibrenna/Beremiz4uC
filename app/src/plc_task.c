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

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(plc_thread, LOG_LEVEL_DBG);

#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/posix/time.h>

#include "config.h"
#include "plc_task.h"
#include "plc_debug.h"
#include "plc_io.h"
#include "plc_loader.h"
#include "plc_log_rte.h"
#include "plc_time_rtc.h"

// I'm still waiting for a good solution on how to address the 32/64-bit 
// issue to ensure seamless data transfer between the STM32 (32-bit) and the IDE (64-bit).
TimeSpec64 __CURRENT_TIME;
//struct timespec __CURRENT_TIME;

typedef uint64_t TickType_t;
uint32_t __tick = 0;
uint32_t greatest_tick_count__ = 0;

extern uint64_t cycle_time_ns;		// plc_loader.c (common_cycletime__)
extern uint64_t *common_ticktime__; // plc_loader.c
uint64_t common_cycle_time = 0;

extern struct k_sem sem_plc_run;

// Status Variable
extern uint32_t plc_initialized;
extern uint32_t plc_run;
uint32_t elapsed_time_ms = 0;
uint32_t sleep_ms = 0;

extern struct k_sem plc_cycle_start;
bool plc_in_cycle = false;
K_MUTEX_DEFINE(plc_cycle_mutex);

/*****************************************************************************************************************************/
/*		plc main thread																								     	 */
/*****************************************************************************************************************************/
#define PLC_MAIN_STACK_SIZE 2048
#define PLC_MAIN_PRIORITY 5
extern void plc_main_task(void *, void *, void *);
K_THREAD_DEFINE_CCM(plc_main, PLC_MAIN_STACK_SIZE, plc_main_task, NULL, NULL, NULL, PLC_MAIN_PRIORITY, 0, PLC_TASK_STARTUP_DELAY);

#pragma GCC push_options	// This function invokes dynamically loaded PLC code functions via udynlink, where 
#pragma GCC optimize ("O0")	// disabling optimizations enhances call reliability and ensures stable execution.
void plc_main_task(void *, void *, void *)
{
	LOG_INF("plc_main_thread");
	initialize_logging();
	ResetLogCount();
	for (;;)
	{
		k_sem_take(&sem_plc_run, K_FOREVER); // Wait for run signal

		LOG_INF("plc main_thread starting");
		int64_t start_tick = 0;
		int64_t end_tick = 0;

		plc_init_io();

		config_init__();
		__init_debug();

		common_cycle_time = *common_ticktime__;								 // cycle_time from plc module
		uint32_t cycle_time = (uint32_t)(common_cycle_time / NSEC_PER_MSEC); // to ms
		const TimeSpec64 ticktime = {0, *common_ticktime__};				 // in ns
		clock_gettime(CLOCK_REALTIME, &__CURRENT_TIME);						 // get Time from RTC
		LOG_INF("Starting PLC");
		LOG_INF("common_ticktime=%ums", cycle_time);

		do // run plc
		{
			// TODO: Check whether the cycle time is exceeded
			start_tick = k_uptime_get();

			__tick++;
			if (greatest_tick_count__)
			{
				__tick %= greatest_tick_count__;
			}

			plc_update_inputs();

			k_sem_take(&plc_cycle_start, K_FOREVER); // PLC Cycle start
			config_run__(__tick);
			k_sem_give(&plc_cycle_start); // PLC Cycle end

			plc_update_outputs(plc_run);

			// TODO: Consider using RTC time here 
			__CURRENT_TIME = addTimeSpec64(__CURRENT_TIME, ticktime);

			end_tick = k_uptime_get();
			elapsed_time_ms = end_tick - start_tick;
			sleep_ms = cycle_time - elapsed_time_ms;
			k_msleep(sleep_ms);

		} while (plc_run == 1);

		__cleanup_debug();

		// reset outputs
		plc_update_outputs(plc_run);

		LOG_INF("plc main_thread stopped");
		k_sem_give(&sem_plc_run);
	}
}
#pragma GCC pop_options