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
LOG_MODULE_REGISTER(PLC_RTE_LOGGING, LOG_LEVEL_DBG);

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <zephyr/kernel.h>
#include <zephyr/posix/time.h>

#include "c_erpc_PLCObject_server.h"
#include "erpc_PLCObject_common.h"

#include "config.h"
#include "plc_log_rte.h"
#include "plc_task.h"

static uint32_t messageIndexes[LOG_LEVELS] = {0}; // Sequential indexes for log messages per log level
uint32_t plc_logCounts[LOG_LEVELS] = {0};		  // Number of log entries per level

extern uint32_t __tick; // Current PLC tick from plc_task.c

struct k_timer logging_timer;

/****************************************************************************************************************************************
 * @brief    Timer callback function for logging timer
 *
 * @param    timer_id Pointer to the timer instance calling the callback
 * @return   void
 ****************************************************************************************************************************************/
void logging_timer_callback(struct k_timer *timer_id)
{
	backup_logging(); // Backup LogCounter and messageIndex
	return;
}

/****************************************************************************************************************************************
 * @brief    Initializes the logging system
 *
 * @param    None
 * @return   void
 ****************************************************************************************************************************************/
void initialize_logging(void)
{
	LOG_INF("Initialized PLC logging");
	ResetLogCount();
	LoadLogState(plc_logCounts); // Also loads the messageIndex
#if BACKUP_PERIOD_SEC > 0		 // 0 = log backup disabled
	k_timer_init(&logging_timer, logging_timer_callback, NULL);
	k_timer_start(&logging_timer, K_SECONDS(BACKUP_PERIOD_SEC), K_SECONDS(BACKUP_PERIOD_SEC));
#endif
	return;
}

/****************************************************************************************************************************************
 * @brief    Deinitializes the logging system
 *
 * @param    None
 * @return   void
 ****************************************************************************************************************************************/
void deinitialize_logging(void)
{
#if BACKUP_PERIOD_SEC > 0
	k_timer_stop(&logging_timer);
#endif
	return;
}

/****************************************************************************************************************************************
 * @brief    Backs up logging metadata
 *
 * @param    None
 * @return   void
 ****************************************************************************************************************************************/
void backup_logging(void)
{
	LOG_INF("Backup logging metadata");
#if BACKUP_PERIOD_SEC > 0
	k_timer_stop(&logging_timer);
#endif
	SaveLogState(plc_logCounts); // Also saves the messageIndex
#if BACKUP_PERIOD_SEC > 0
	k_timer_start(&logging_timer, K_SECONDS(BACKUP_PERIOD_SEC), K_SECONDS(BACKUP_PERIOD_SEC));
#endif
	return;
}

/****************************************************************************************************************************************
 * @brief    Saves the current state of log counts and message indexes
 *
 * @param    logCounts Array of log counts for each level
 * @return   void
 ****************************************************************************************************************************************/
void SaveLogState(uint32_t *logCounts)
{
	FILE *file = fopen(STATE_FILE_NAME, "w");
	if (file)
	{
		// Saves messageIndexes and the number of log entries per log level
		for (int i = 0; i < LOG_LEVELS; i++)
		{
			fprintf(file, "%u %u\n", messageIndexes[i], logCounts[i]);
		}
		fclose(file);
		LOG_INF("Log state saved successfully.");
	}
	else
	{
		LOG_ERR("Failed to save log state %d %s", errno, strerror(errno));
	}
}

/****************************************************************************************************************************************
 * @brief    Loads the saved state of log counts and message indexes
 *
 * @param    logCounts Array to store the loaded log counts for each level
 * @return   void
 ****************************************************************************************************************************************/
void LoadLogState(uint32_t *logCounts)
{
	FILE *file = fopen(STATE_FILE_NAME, "r");
	if (file)
	{
		// Loads messageIndexes and the number of log entries per log level
		for (int i = 0; i < LOG_LEVELS; i++)
		{
			if (fscanf(file, "%u %u", &messageIndexes[i], &logCounts[i]) != 2)
			{
				LOG_ERR("Failed to load log state for level %d", i);
				break;
			}
		}
		fclose(file);
		LOG_INF("Log state loaded successfully.");
	}
	else
	{
		LOG_ERR("Failed to load log state %d %s", errno, strerror(errno));
	}
}

/****************************************************************************************************************************************
 * @brief    Logs a message with the specified level and message buffer
 *
 * @param    level Log level of the message
 * @param    buf Buffer containing the message to log
 * @param    size Size of the message buffer
 * @return   int Returns 1 on success, 0 on failure
 ****************************************************************************************************************************************/
int LogMessage(uint8_t level, char *buf, int8_t size)
{
	if (level >= LOG_LEVELS)
	{
		return 0; // Invalid log level
	}

	FILE *file = fopen(LOG_FILE_NAME, "a+");
	if (!file)
	{
		LOG_ERR("Failed to save message to logfile %d %s", errno, strerror(errno));
		return 0; // Error opening the file
	}

	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);

	char localBuf[256];
	snprintf(localBuf, 256, "[%u] [%u] [%lld] [%ld] [%u] ",
			 messageIndexes[level]++, // Use and update the index for the specific log level
			 __tick, (long long)ts.tv_sec, ts.tv_nsec, level);
	strcat(localBuf, buf);
	fprintf(file, "%s\n", localBuf);

	fclose(file);

	plc_logCounts[level]++; // Updates the number of log entries for the specific log level
	LOG_INF("Saved log message msg=%s", localBuf);
	return 1; // Success
}

/****************************************************************************************************************************************
 * @brief    Retrieves a log message by its ID and level
 *
 * @param    level Log level of the message to retrieve
 * @param    msgID ID of the message to retrieve
 * @param    message Struct to store the retrieved message
 * @return   uint32_t Returns 0 on success, 1 on failure
 ****************************************************************************************************************************************/
uint32_t GetLogMessage(uint8_t level, uint32_t msgID, log_message *message)
{
	message->msg = NULL;
	FILE *file = fopen(LOG_FILE_NAME, "r");
	if (!file)
	{
		LOG_ERR("GetLogMessage can't open logfile");
		return 0; // Error opening the file
	}

	char line[512];
	uint32_t found = 1; // Starts with an error value, 0 is set if the message was found.

	while (fgets(line, sizeof(line), file) != NULL)
	{
		uint32_t lineID, lineTick, lineSec, lineNsec, lineLevel;
		if (sscanf(line, "[%u] [%u] [%u] [%u] [%u]", &lineID, &lineTick, &lineSec, &lineNsec, &lineLevel) == 5)
		{
			if (lineID == msgID && lineLevel == level)
			{
				// Extracts the message by skipping all metadata fields
				char *msgStart = line;
				for (int i = 0; i < 5; ++i)
				{									  // For each of the five metadata fields
					msgStart = strchr(msgStart, ']'); // Find the next ']' character
					if (!msgStart)
						break;	   // If no further ']' found, break
					msgStart += 2; // Skips the ']' and the following space
				}

				if (msgStart)
				{
					size_t msgLen = strlen(msgStart);
					message->msg = (char *)k_malloc(msgLen + 1); // Allocates memory for the message
					if (message->msg == NULL)
					{
						found = 1; // Error allocating memory
						LOG_ERR("Error allocating memory for the message");
						break;
					}
					strncpy(message->msg, msgStart, msgLen);
					message->msg[msgLen] = '\0'; // Ensures the message is properly terminated
					// Copies the metadata
					message->tick = lineTick;
					message->sec = lineSec;
					message->nsec = lineNsec;

					found = 0; // Message successfully found and copied
					break;
				}
			}
		}
	}

	fclose(file);

	if (found == 0)
	{
		LOG_INF("Found LogMessage in file: msgid=%u level=%u tick=%u sec=%u nsec=%u msg=%s", msgID, level, message->tick, message->sec, message->nsec, message->msg);
		plc_logCounts[level]--; // Updates the number of log entries for the specific log level
	}
	else
	{
		message->msg = (char *)k_malloc(20);
		strcpy(message->msg, "Message not found!\0");
		message->tick = 0;
		message->sec = 0;
		message->nsec = 0;
		LOG_INF("LogMessage not found: msgid=%u level=%u", msgID, level);
	}
	return 0;
}

/****************************************************************************************************************************************
 * @brief    Resets the log counts and message indexes for all levels
 *
 * @param    None
 * @return   uint32_t Returns 0 always
 ****************************************************************************************************************************************/
uint32_t ResetLogCount(void)
{
	struct fs_dirent dirent;
	int rc = fs_stat(LOG_FILE_NAME, &dirent);
	if (rc == 0)
	{
		if (fs_unlink(LOG_FILE_NAME) == 0)
			LOG_INF("Log file deleted successfully.");
		else
			LOG_ERR("Failed to delete log file.");
	}

	rc = fs_stat(STATE_FILE_NAME, &dirent);
	if (rc == 0)
	{
		if (fs_unlink(STATE_FILE_NAME) == 0)
			LOG_INF("LogState file deleted successfully.");
		else
			LOG_ERR("Failed to delete logState file.");
	}

	// Resets the count of log messages and messageIndexes for each level
	for (int i = 0; i < LOG_LEVELS; ++i)
	{
		plc_logCounts[i] = 0;
		messageIndexes[i] = 0; // Resets the message index for each log level
	}

	// Saves the current state, including reset messageIndexes and logCounts
	SaveLogState(plc_logCounts);

	return 0;
}

/****************************************************************************************************************************************
 * @brief    Gets the count of log messages for a given level
 *
 * @param    level Log level to query
 * @return   uint32_t Number of log messages for the specified level
 ****************************************************************************************************************************************/
uint32_t GetLogCount(uint8_t level)
{
	if (level >= LOG_LEVELS)
	{
		return 0; // Invalid log level
	}
	return plc_logCounts[level];
}

/****************************************************************************************************************************************
 * @brief    log from rte to zephyr logging system
 *
 * @param
 * @return
 ****************************************************************************************************************************************/
void rte_log_inf(const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	char buffer[128];
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	LOG_WRN("%s", buffer);
	va_end(args);
}
