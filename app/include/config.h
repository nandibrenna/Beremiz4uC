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

#ifndef CONFIG_H
#define CONFIG_H

/*****************************************************************************************************************************/
/*											common configuration			 											     */
/*****************************************************************************************************************************/
// littlefs doesnt support directories
#define FILESYSTEM_PATH					"/lfs:/"										// filesystem root
#define TMP_PATH						"tmp/"											// path for temporary files
#define PLC_PATH						"plc/"											// path for plc files
#define LOG_PATH						"log/"											// path for log files
#define WEB_PATH						"www/"											// path for www files
#define PLC_ID							"AAA"
#define PLC_PSK							"BBB"
#define PLC_TASK_STARTUP_DELAY			100
#define PLC_LOADER_STARTUP_DELAY		200
#define RPC_SERVER_STARTUP_DELAY		100
#define HTTP_SERVER_STARTUP_DELAY		200
#define HTTP_SERVER_PORT 				80
#define HTTP_ROOT						FILESYSTEM_PATH WEB_PATH

#define SNTP_SERVER 					"0.de.pool.ntp.org"
#define SNTP_TIMEOUT 					SYS_FOREVER_MS								// MSEC_PER_SEC * 30

#define ERPC_SERVER_PORT				1042
#define HOSTNAME						"Beremiz4uC"	
/*****************************************************************************************************************************/
/*									Configuration file paths											     				 */
/*****************************************************************************************************************************/
#define PLC_MD5_FILE					FILESYSTEM_PATH PLC_PATH "md5.txt"			// plc md5 file
#define PLC_BIN_FILE					FILESYSTEM_PATH PLC_PATH "plc.bin"			// plc module file
#define PLC_STARTUP_FILE				FILESYSTEM_PATH PLC_PATH "startup.scr"		// plc startup script
#define TMP_FILE_PATH 					FILESYSTEM_PATH TMP_PATH					// path for temporary files
#define PLC_ROOT_PATH					FILESYSTEM_PATH PLC_PATH					// path for plc files

#define TMP_FILE_EXTENSION 				".tmp"										// extension for temporary files

#define MAX_FILE_UPLOADS 				20
#define MAX_FILENAME_LENGTH 			8

/*****************************************************************************************************************************/
/*									Configuration rte logging															     */
/*****************************************************************************************************************************/
#define LOG_LEVELS 						4                              				// Number of supported log levels
#define RTE_LOGLEVEL_CRITICAL			0
#define RTE_LOGLEVEL_WARNING			1
#define RTE_LOGLEVEL_INFO				2
#define RTE_LOGLEVEL_DEBUG				3

#define MAX_LOG_MESSAGE_SIZE 			1024                 						// Maximum size of a log message
#define BACKUP_PERIOD_SEC 				0											// Backup log state every x seconds, 0 to disable
#define LOG_FILE_NAME 					FILESYSTEM_PATH LOG_PATH "plc.log"			// Logfile name
#define STATE_FILE_NAME 				FILESYSTEM_PATH LOG_PATH "logstate.log"		// Name of log state file


/*****************************************************************************************************************************/
/*									Configuration rte debugging															     */
/*****************************************************************************************************************************/

#define TRACE_SAMPLE_BUFFER_SIZE 		8192										// TraceSample Buffer Size for ringbuffer

// Configuration for the maximum count of traceable and forcible variables
#define TRACE_VARS_MAX_COUNT 			64											// Maximum number of variables to trace
#define FORCE_VARS_MAX_COUNT 			64											// Maximum number of variables to force
#define DEBUG_TIMEOUT 					(3 * MSEC_PER_SEC)							// Debug timeout in milliseconds

/*****************************************************************************************************************************/
#define NUM(a) (sizeof(a) / sizeof(*a))
#define ct_assert(e) ((void)sizeof(char[1 - 2*!(e)]))

#ifndef HOLDING_REG_COUNT
#define HOLDING_REG_COUNT       16
#endif

#ifndef INPUT_REG_COUNT
#define INPUT_REG_COUNT         8
#endif

#ifndef COIL_COUNT
#define COIL_COUNT              16
#endif

#ifndef DISCRETE_COUNT
#define DISCRETE_COUNT          16
#endif

#ifdef SLAVE_HOLDING_REG_COUNT
#undef SLAVE_HOLDING_REG_COUNT
#endif

#ifdef SLAVE_INPUT_REG_COUNT
#undef SLAVE_INPUT_REG_COUNT
#endif

#ifdef SLAVE_COIL_COUNT
#undef SLAVE_COIL_COUNT
#endif

#ifdef SLAVE_DISCRETE_COUNT
#undef SLAVE_DISCRETE_COUNT
#endif

#define SLAVE_HOLDING_REG_COUNT 0
#define SLAVE_INPUT_REG_COUNT   0
#define SLAVE_COIL_COUNT        0
#define SLAVE_DISCRETE_COUNT    0

#define QW_BASE                 0
#define IW_BASE                 0
#define QX_BASE                 0
#define IX_BASE                 0

#define QW_COUNT                (HOLDING_REG_COUNT + SLAVE_HOLDING_REG_COUNT)
#define IW_COUNT                (INPUT_REG_COUNT + SLAVE_INPUT_REG_COUNT)
#define QX_COUNT                (COIL_COUNT + SLAVE_COIL_COUNT)
#define IX_COUNT                (DISCRETE_COUNT + SLAVE_DISCRETE_COUNT)

#define CONFIG_MODBUS_PORT       502
#define CONFIG_RPC_PORT 		1042
#endif


#define K_THREAD_DEFINE_CCM(name, stack_size,                   \
			entry, p1, p2, p3,                                  \
			prio, options, delay)                               \
		Z_KERNEL_STACK_DEFINE_IN(_k_thread_stack_##name, stack_size, __ccm_noinit_section);	\
		Z_THREAD_COMMON_DEFINE(name, stack_size, entry, p1, p2, p3,	\
			       prio, options, delay)
