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
LOG_MODULE_REGISTER(stack_monitor, LOG_LEVEL_DBG);

#include <zephyr/kernel.h>

#include "config.h"

#define WARNING_THRESHOLD (70U)
#define CRITICAL_THRESHOLD (95U)

#define STACK_MONITOR_INTERVAL K_SECONDS(5)

void check_thread_stack_space(const struct k_thread *thread, void *user_data)
{
	size_t unused;
	size_t size = thread->stack_info.size;
	unsigned int pcnt;
	int ret;

	ret = k_thread_stack_space_get(thread, &unused);
	if (ret)
	{
		LOG_ERR("Unable to determine unused stack size for thread %p (name: %s), error: %d", thread, k_thread_name_get((k_tid_t)thread), ret);
	}
	else
	{
		pcnt = ((size - unused) * 100U) / size;

		if (pcnt >= CRITICAL_THRESHOLD)
		{
			LOG_ERR("Thread %p (name: %s) stack usage critical: %u%% used", thread, k_thread_name_get((k_tid_t)thread), pcnt);
		}
		else if (pcnt >= WARNING_THRESHOLD)
		{
			LOG_WRN("Thread %p (name: %s) stack usage high: %u%% used", thread, k_thread_name_get((k_tid_t)thread), pcnt);
		}
	}
}

void stack_monitor_thread(void)
{
	LOG_INF("StackMonitor started");
	while (true)
	{
		k_thread_foreach(check_thread_stack_space, NULL);
		k_sleep(STACK_MONITOR_INTERVAL);
	}
}

K_THREAD_DEFINE_CCM(stack_monitor_tid, 1024, stack_monitor_thread, NULL, NULL, NULL, 5, 0, 0);