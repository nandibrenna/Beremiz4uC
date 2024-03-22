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

#ifndef PLC_TIME_RTC_H
#define PLC_TIME_RTC_H
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/logging/log_ctrl.h>

int log_ts_print(const struct log_output *output, const log_timestamp_t timestamp,
                 const log_timestamp_printer_t printer);

void set_system_time_from_rtc(void);
void set_rtc_from_system_time(void);
const char* get_time_str(void);

/****************************************************************************************************************************************
 * @brief    Represents time in seconds and nanoseconds for compatibility with 64-bit systems.
 *
 * @param    sec     Seconds since epoch, 64-bit for compatibility.
 * @param    nsec    Nanoseconds since the beginning of the last second, 64-bit for compatibility.
 * @return   None.
 ****************************************************************************************************************************************/
typedef struct
{
	uint64_t sec;  // Seconds since epoch, ensuring 64-bit compatibility
	uint64_t nsec; // Nanoseconds past the second, also 64-bit for compatibility
} TimeSpec64;


TimeSpec64 addTimeSpec64(TimeSpec64 t1, TimeSpec64 t2);

#endif