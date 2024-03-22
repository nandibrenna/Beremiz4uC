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
LOG_MODULE_REGISTER(plc_time_rtc, LOG_LEVEL_DBG);

#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <zephyr/drivers/rtc.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log_output_custom.h>
#include <zephyr/posix/time.h>

#include "plc_time_rtc.h"

#define STM_RTC DT_INST(0, st_stm32_rtc)

/****************************************************************************************************************************************
 * @brief	Callback function to update the system time from the RTC.
 * @param	timer_id Pointer to the timer instance triggering the callback.
 ****************************************************************************************************************************************/
void update_system_time_callback(struct k_timer *timer_id)
{
	LOG_INF("Updating time from NTP Server"); // Log the update attempt
	set_system_time_from_rtc(); // Function to update the system time from the RTC
	// No return statement needed for void function
}
K_TIMER_DEFINE(timer_systemtime, update_system_time_callback, NULL);

/****************************************************************************************************************************************
 * @brief Convenience function for safely casting a \ref tm pointer
 * to a \ref rtc_time pointer.
 ****************************************************************************************************************************************/
static inline struct rtc_time *tm_to_rtc_time(struct tm *tm_ptr) { return (struct rtc_time *)tm_ptr; }

/****************************************************************************************************************************************
 * @brief	Determines if the current time is within Daylight Saving Time (DST).
 * @param	current_time Pointer to time_t struct containing current time.
 * @return	int 1 if current time is in DST, 0 otherwise.
 ****************************************************************************************************************************************/
int is_dst(const time_t *current_time)
{
	struct tm *time_info = localtime(current_time);

	int year = time_info->tm_year + 1900; // Convert tm_year to full year
	int month = time_info->tm_mon + 1;	  // tm_mon is 0-11, convert to 1-12
	int mday = time_info->tm_mday;		  // Day of the month

	// Calculate day of DST start: last Sunday of March
	int beginDSTDay = 31 - ((5 * year / 4 + 4) % 7);
	// Calculate day of DST end: last Sunday of October
	int endDSTDay = 31 - ((5 * year / 4 + 1) % 7);

	// Determine if current date is within DST period
	if (month < 3 || month > 10)
	{
		return 0; // Jan, Feb, Nov, Dec: not DST
	}
	else if (month > 3 && month < 10)
	{
		return 1; // May through Sep: DST
	}
	else if (month == 3)
	{
		return mday >= beginDSTDay; // In March, check if date is after DST start
	}
	else if (month == 10)
	{
		return mday < endDSTDay; // In October, check if date is before DST end
	}

	return 0; // Default to not DST
}

/****************************************************************************************************************************************
 * @brief	Returns the current timezone offset based on Daylight Saving Time (DST).
 * 			This function checks if the current local time falls within DST. It returns 2 hours for DST (CEST) and 1 hour for
 * 			standard time (CET) to represent the timezone offset from UTC.
 * @return	int Timezone offset: 2 for DST (CEST), 1 for standard time (CET).
 ****************************************************************************************************************************************/
int get_current_timezone_offset()
{
	time_t current_time;
	time(&current_time); // Get current time

	if (is_dst(&current_time))
	{
		return 2; // DST is in effect: CEST
	}
	else
	{
		return 1; // Outside DST: CET
	}
}

/****************************************************************************************************************************************
 * @brief	Prints a timestamp in a custom format including the current timezone offset.
 * 			This function retrieves the current time, adjusts it for the local timezone based on DST, and then prints it using the
 * 			provided log_timestamp_printer function. The format includes date, time, and microseconds.
 * @param	output Pointer to the log output structure.
 * @param	timestamp The log timestamp (unused in this function but typically part of the signature).
 * @param	printer Function pointer to the specific log timestamp printer.
 * @return	int The result of the printer function call.
 ****************************************************************************************************************************************/
int log_ts_print(const struct log_output *output, const log_timestamp_t timestamp, const log_timestamp_printer_t printer)
{
	struct timeval tv;
	struct tm *tm;

	gettimeofday(&tv, NULL); // Get the current time with microseconds
	tm = gmtime(&tv.tv_sec); // Convert to GMT structure
	tm->tm_hour += get_current_timezone_offset(); // Adjust hour for local timezone

	// Ensure tm_hour is within 0-23 range after timezone adjustment
	while (tm->tm_hour >= 24) {
		tm->tm_hour -= 24;
		tm->tm_mday += 1;
	}
	while (tm->tm_hour < 0) {
		tm->tm_hour += 24;
		tm->tm_mday -= 1;
	}

	// Print the timestamp in the specified format
	return printer(output, "[%02d.%02d.%04d %02d:%02d:%02d.%06ld]", 
					tm->tm_mday, tm->tm_mon + 1, tm->tm_year + 1900, 
					tm->tm_hour, tm->tm_min, tm->tm_sec, tv.tv_usec);
}

/****************************************************************************************************************************************
 * @brief	Sets the system time using the Real-Time Clock (RTC) device.
 * 			This function fetches the current time from an RTC device and sets the system clock.
 ****************************************************************************************************************************************/
void set_system_time_from_rtc(void)
{
	const struct device *rtc_dev;
	struct rtc_time rtc_time;
	struct tm *tm_ptr;
	struct timespec ts;
	int ret;

	// Retrieve the RTC device
	rtc_dev = DEVICE_DT_GET(STM_RTC);
	if (!device_is_ready(rtc_dev))
	{
		LOG_ERR("Error: RTC not found.");
		return;
	}

	// Attempt to read the time from the RTC
	ret = rtc_get_time(rtc_dev, &rtc_time);

	if (ret == 0)
	{
		// Successfully read time, convert to tm
		tm_ptr = rtc_time_to_tm(&rtc_time);

		// Convert tm to timespec
		ts.tv_sec = mktime(tm_ptr); // mktime expects local time, consider timezone
		ts.tv_nsec = 0;			   // RTCs usually don't provide nanoseconds

		// Set the system time
		if (clock_settime(CLOCK_REALTIME, &ts) != 0)
		{
			LOG_ERR("Failed to set system time.");
		}
		else
		{
			LOG_INF("System time successfully set from RTC.");
		}
	}
	else if (ret == -ENODATA)
	{
		LOG_WRN("RTC time not set yet.");
	}
	else
	{
		LOG_ERR("Error reading RTC time: %d", ret);
	}
}

/****************************************************************************************************************************************
 * @brief	Sets the Real-Time Clock (RTC) device time from the system clock.
 * 			This function fetches the current system time, converts it into a format suitable for the RTC device, and sets the RTC
 * 			device time accordingly. Additionally, itstarts a timer to periodically update the RTC time, ensuring it stays in 
 * 			sync with the system time.
 ****************************************************************************************************************************************/
void set_rtc_from_system_time(void)
{
	const struct device *rtc_dev;
	struct timespec ts;
	struct tm tm_buffer;
	int ret;

	// Retrieve the RTC device
	rtc_dev = DEVICE_DT_GET(STM_RTC);
	if (!device_is_ready(rtc_dev))
	{
		LOG_ERR("Error: RTC device not found.");
		return;
	}

	// Get the current system time
	if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
	{
		LOG_ERR("Error obtaining system time.");
		return;
	}

	// Convert timespec to tm
	gmtime_r(&(ts.tv_sec), &tm_buffer); // Use gmtime_r for thread safety

	// Convert tm to rtc_time
	struct rtc_time *rtc_ptr = tm_to_rtc_time(&tm_buffer); 

	// Attempt to set the RTC time
	ret = rtc_set_time(rtc_dev, rtc_ptr); 

	if (ret == 0)
	{
		LOG_INF("RTC time successfully set from system time.");
		k_timer_start(&timer_systemtime, K_MINUTES(60), K_MINUTES(60));
	}
	else
	{
		LOG_ERR("Error setting RTC time: %d", ret);
	}
}

/****************************************************************************************************************************************
 * @brief	Gets the current date and time as a string, adjusted for the current timezone.
 * 			This function formats the current date and time adjusted by the timezone offset into a string in the format
 * 			DD.MM.YYYY HH:MM:SS. The result is stored in a static buffer, making the function thread-safe but not reentrant.
 * @return	char* Pointer to a statically allocated buffer containing the formatted date and time.
 ****************************************************************************************************************************************/
const char *get_time_str(void)
{
	static char buffer[20]; // Sufficient size for the format DD.MM.YYYY HH:MM:SS
	struct timeval tv;
	struct tm *tm;

	gettimeofday(&tv, NULL);
	tm = gmtime(&tv.tv_sec); // Convert to GMT time structure
	tm->tm_hour += get_current_timezone_offset(); // Adjust for local timezone

	// Ensure tm_hour is within 0-23 range after timezone adjustment
	while (tm->tm_hour >= 24) {
		tm->tm_hour -= 24;
		tm->tm_mday += 1;
	}
	while (tm->tm_hour < 0) {
		tm->tm_hour += 24;
		tm->tm_mday -= 1;
	}

	strftime(buffer, sizeof(buffer), "%d.%m.%Y %H:%M:%S", tm); // Format date and time

	return buffer;
}

/****************************************************************************************************************************************
 * @brief    Adds two TimeSpec64 variables.
 *
 * @param    t1    The first time structure to add.
 * @param    t2    The second time structure to add.
 * @return   The result of the addition as a TimeSpec64 structure.
 ****************************************************************************************************************************************/
TimeSpec64 addTimeSpec64(TimeSpec64 t1, TimeSpec64 t2)
{
	TimeSpec64 result;
	// Add nanoseconds and handle overflow
	result.nsec = t1.nsec + t2.nsec;
	result.sec = t1.sec + t2.sec + (result.nsec / 1000000000); // Add carry-over seconds if nanoseconds overflow
	result.nsec = result.nsec % 1000000000;					   // Keep the remainder of nanoseconds

	return result;
}

/****************************************************************************************************************************************
 * @brief    Adds two TimeSpec variables.
 *
 * @param    t1    The first time structure to add.
 * @param    t2    The second time structure to add.
 * @return   The result of the addition as a TimeSpec64 structure.
 ****************************************************************************************************************************************/
struct timespec addTimeSpec32(struct timespec  t1, struct timespec  t2)
{
	struct timespec result;
	// Add nanoseconds and handle overflow
	result.tv_nsec = t1.tv_nsec + t2.tv_nsec;
	result.tv_sec = t1.tv_sec + t2.tv_sec + (result.tv_nsec / 1000000000); 	// Add carry-over seconds if nanoseconds overflow
	result.tv_nsec = result.tv_nsec % 1000000000;					   		// Keep the remainder of nanoseconds

	return result;
}