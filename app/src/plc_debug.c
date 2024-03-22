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
LOG_MODULE_REGISTER(plc_debug, LOG_LEVEL_INF);

#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include "config.h"
#include "plc_debug.h"
#include "plc_loader.h"
#include "plc_util.h"

/************************************************************************************************************************/
/*		rte debugging																									*/
/************************************************************************************************************************/

// Definition of error codes for debugging functionalities
#define TOO_MANY_TRACED -1
#define TOO_MANY_FORCED -2
#define FORCE_VAR_SIZE_OVERFLOW -3
#define INVALID_FORCE_VALUE -4
#define DEBUG_SUSPENDED -5

// Ringbuffer for storing trace samples
static uint8_t __attribute__((section(".ccm_noinit"))) _ring_buffer_data_trace_samples[TRACE_SAMPLE_BUFFER_SIZE];
struct ring_buf trace_samples = {.buffer = _ring_buffer_data_trace_samples, .size = TRACE_SAMPLE_BUFFER_SIZE};

volatile uint32_t last_trace_sent_timestamp = 0;							// Last time the trace was send in milliseconds
uint32_t last_trace_timestamp = 0;											// Last tick when data was published

uint32_t __debugtoken = 0;													// Debug token for session management
uint32_t __debug_tick;														// Tick count for debugging

uint32_t traced_vars_count = 0;												// Count of currently traced vars
size_t traced_vars_total_size = 0;											// Total size in bytes of traced variables
uint32_t __ccm_noinit_section traced_vars_idxs[TRACE_VARS_MAX_COUNT] = {0};	// Indexes of traced variables
uint32_t __ccm_noinit_section traced_vars_size[TRACE_VARS_MAX_COUNT] = {0};	// List of variable sizes that are being traced

uint32_t forced_vars_count = 0;												// Count of currently forced vars
size_t forced_vars_total_size = 0;											// Total size in bytes of forced variables
uint32_t __ccm_noinit_section forced_vars_idxs[FORCE_VARS_MAX_COUNT] = {0}; // Indexes of forced variables
uint32_t __ccm_noinit_section forced_vars_size[FORCE_VARS_MAX_COUNT] = {0}; // Sizes of forced variables

K_SEM_DEFINE(plc_cycle_start, 0, 1);										// Semaphore for synchronizing with the PLC cycle
extern uint32_t __tick;														// Current PLC tick from plc_task.c
extern uint32_t plc_run;													// PLC running state from plc_loader

int stop_debug_thread = 0;													// Flag to request stopping of the debug thread
int debug_thread_state = 0;													// State of the debug thread: 0 = not running, 1 = running

// Function to print a buffer in hexadecimal representation
const char *print_buf(binary_t binary)
{
	static char staticBuffer[128]; // Größe an Datenmenge anpassen
	char *buffer = staticBuffer;
	int bytesWritten = 0;

	for (uint32_t i = 0; i < binary.dataLength; i++)
	{
		bytesWritten += sprintf(buffer + bytesWritten, "%02x ", binary.data[i]);
		if (bytesWritten >= sizeof(staticBuffer) - 3)
		{
			printk("Error: Buffer overflow in print_buf\n");
			break;
		}
	}

	return staticBuffer;
}

/****************************************************************************************************************************************
 * @brief                plc_debug_thread
 *                       Starts when debugging is active and ends when debugging
 *                       gets disabled. Calls publish_debug to get the traced
 *                       variables from PLC to the traceBuffer and initiates the
 *                       transfer to the host.
 * @param
 * @return
 ****************************************************************************************************************************************/
#define PLC_DEBUG_STACK_SIZE 2048
#define PLC_DEBUG_PRIORITY 5

Z_KERNEL_STACK_DEFINE_IN(plc_debug_stack_area, PLC_DEBUG_STACK_SIZE, __ccm_noinit_section);
struct k_thread plc_debug_thread_data;
k_tid_t plc_debug_tid;
void plc_debug_thread(void *, void *, void *)
{
	LOG_DBG("debug_thread: started");

	while (plc_run) // Loop as long as the PLC is running
	{
		debug_thread_state = 1;							   // Mark the debug thread as active
		int ret = k_sem_take(&plc_cycle_start, K_MSEC(1)); // Wait for the PLC cycle to end
		if (ret == -EAGAIN)
		{
			if (stop_debug_thread == 1) // überprüfe, ob der Thread beendet werden soll
			{
				LOG_DBG("debug_thread: break due to stop_debug_thread");
				break; // Beende den Thread aufgrund von stop_debug_thread
			}
			continue;
		}

		if (__tick > last_trace_timestamp) // Only publish once per tick
		{
			last_trace_timestamp = __tick;
			if (publish_debug() == 0) // Read data from PLC
			{
				__debug_tick = __tick;						 // Remember the tick when data was copied into traceSample
				int64_t now = k_uptime_get();				 // Get current uptime in milliseconds
				if ((now - last_trace_sent_timestamp) > DEBUG_TIMEOUT) // Check if timeout has occurred for the IDE to fetch data
				{
					LOG_DBG("debug_thread: timeout. exiting now");
					break; // Exit the thread due to timeout
				}
			}
			else
			{
				LOG_DBG("debug_thread: error reading data from plc. exiting now");
				break; // Exit the thread due to error
			}
		}

		k_sem_give(&plc_cycle_start); // Unlock the PLC cycle
		k_msleep(1);				  // Wait to let PLC thread lock the semaphore
	}

	// Debug thread exited
	k_sem_give(&plc_cycle_start); // Unlock the PLC cycle
	stop_debug_thread = 0;
	last_trace_timestamp = 0;
	debug_thread_state = 0; // Mark the debug thread as not running
	LOG_DBG("debug_thread: exited");
}

/****************************************************************************************************************************************
 * @brief                plc_debug_thread_start
 *                       Starts the debugging thread.
 * @param
 * @return
 ****************************************************************************************************************************************/
void plc_debug_thread_start(void)
{
	if (debug_thread_state == 0)
	{
		LOG_DBG("Starting debug thread");
		plc_debug_tid = k_thread_create(&plc_debug_thread_data, plc_debug_stack_area, K_THREAD_STACK_SIZEOF(plc_debug_stack_area), plc_debug_thread, NULL, NULL, NULL,
										PLC_DEBUG_PRIORITY, 0, K_NO_WAIT);
		k_thread_name_set(plc_debug_tid, "plc_debug_thread");
	}
}

/****************************************************************************************************************************************
 * @brief                __init_debug
 *                       Called before the PLC starts to initialize debugging components.
 * @param
 * @return
 ****************************************************************************************************************************************/
void __init_debug(void)
{
	k_sem_give(&plc_cycle_start);
	ring_buf_init(&trace_samples, TRACE_SAMPLE_BUFFER_SIZE, trace_samples.buffer);

	return;
}

/****************************************************************************************************************************************
 * @brief                __cleanup_debug
 *                       Called when the PLC transitions from run to stop to
 *                       clean up debugging components.
 * @param
 * @return
 ****************************************************************************************************************************************/
void __cleanup_debug(void) { return; }

/****************************************************************************************************************************************
 * @brief                publish_debug
 *                       Called every PLC cycle to manage and update the
 *                       debugging data. This includes applying forces to variables, tracing variable
 *                       values, and managing the debug transfer.
 * @param
 * @return               1 on success, -1 on error.
 ****************************************************************************************************************************************/
#pragma GCC push_options	// This function invokes dynamically loaded PLC code functions via udynlink, where 
#pragma GCC optimize("O0")	// disabling optimizations enhances call reliability and ensures stable execution.
int publish_debug(void)
{
    LOG_DBG("publish_debug: start");

    // Check if the ring buffer has enough space for the data
    LOG_DBG("publish_debug: free space in ringbuffer: %u", ring_buf_space_get(&trace_samples));

    volatile size_t bytes_written = 0;
    size_t var_size = 0;
    void *var_value = NULL;
    uint8_t *data_ptr;
    uint32_t tick = __tick;

    // Calculate the total size of data: size of 'tick' + total size of variables
    size_t total_size = sizeof(tick) + (traced_vars_total_size > 0 ? traced_vars_total_size : 1);

    uint32_t ret = ring_buf_put_claim(&trace_samples, &data_ptr, total_size);
    if ((size_t)ret < total_size)
    {
        // Fill the rest of the buffer with 0 if there isn't enough space for all data
        memset(data_ptr, 0, ret);
        ring_buf_put_finish(&trace_samples, ret);

        // Immediately read the filled bytes to "wrap" the buffer
        //uint8_t temp_buf[ret]; // Temporary buffer to discard the filled data
        ring_buf_get(&trace_samples, NULL, ret);

        // Try again to claim the necessary space, now from the start of the buffer
        ret = ring_buf_put_claim(&trace_samples, &data_ptr, total_size);
        if (ret < 0 || (size_t)ret < total_size)
        {
            LOG_ERR("publish_debug: Not enough space in the RingBuffer after wrap-around, ret=%d", ret);
            return -1; // Error handling on retry failure
        }
    }

    // Write 'tick' first into the buffer
    memcpy(data_ptr, &tick, sizeof(tick));
    bytes_written += sizeof(tick);

    if (traced_vars_total_size > 0)
    {
        // Write the data of all monitored variables into the buffer
        for (uint32_t i = 0; i < traced_vars_count; i++)
        {
            // Read variable from PLC
            int ret = GetDebugVariable(traced_vars_idxs[i], &var_value, &var_size);
            if (ret == 0)
            {
                // Check if the read size matches the expected size
                if (var_size != traced_vars_size[i])
                {
                    LOG_WRN("publish_debug: Unexpected size for variable %u, expected: %u but read: %u", traced_vars_idxs[i], traced_vars_size[i], var_size);
                }

                // Check if var_value is NULL after calling GetDebugVariable
                if (var_value == NULL)
                {
                    LOG_ERR("publish_debug: Variable value is NULL for index %u", traced_vars_idxs[i]);
                    ring_buf_put_finish(&trace_samples, bytes_written); // Abort but keep the data already written
                    return -1;
                }

                // Copy data into the ring buffer
                memcpy(data_ptr + bytes_written, var_value, var_size);
                bytes_written += var_size;
            }
            else if (ret > 0)
            {
                LOG_ERR("publish_debug: Error reading variable type for idx %u", traced_vars_idxs[i]);
                ring_buf_put_finish(&trace_samples, bytes_written); // Abort but keep the data already written
                return -1;
            }
            else if (ret < 0)
            {
                LOG_ERR("publish_debug: Error reading variable idx %u out of bounds", traced_vars_idxs[i]);
                ring_buf_put_finish(&trace_samples, bytes_written); // Abort but keep the data already written
                return -1;
            }
        }
    }
    else
    {
        // Add a 0-byte if no variables are being monitored
        *(data_ptr + bytes_written) = 0;
        bytes_written += 1;
    }

    // Complete the write operation
    ring_buf_put_finish(&trace_samples, bytes_written);
    LOG_DBG("publish_debug: end, bytes written to ringbuffer: %u", bytes_written);
    return 0;
}
#pragma GCC pop_options

/****************************************************************************************************************************************
 * @brief               SetTraceVariablesList
 *                      Configures the list of variables to be traced or forced, based on input from the host.
 * 						This setup is crucial for determining which variables are included in debugging sessions.
 * @param orders        A structure containing the list of variables to trace or force, along with their configurations.
 * @param debugtoken    A pointer to store the updated debug token, which is used for session management.
 * @return              uint32_t - 0 on successful configuration, non-zero if an error occurred.
 ****************************************************************************************************************************************/
#pragma GCC push_options	// This function invokes dynamically loaded PLC code functions via udynlink, where 
#pragma GCC optimize("O0")	// disabling optimizations enhances call reliability and ensures stable execution.
uint32_t SetTraceVariablesList(const list_trace_order_1_t *orders, uint32_t *debugtoken)
{
	// TODO: 	Restarting everything when IDE forces a variable feels like a crash due to the delay. 
	//     		Need a better solution to handle this situation.
	// 
	size_t var_size = 0;
	void *var_value = NULL;
	traced_vars_total_size = 0;

	// Setting stop_debug_thread to 1 when debug_thread is inactive may cause indefinite
	// waiting due to reliance on its termination to reset debug_thread_state to 0.
	if (debug_thread_state == 1) 				// Check if debug_thread is running to safely attempt a stop
	{
		LOG_DBG("SetTraceVariablesList: try to stop debug_thread");
		stop_debug_thread = 1;        			// Signal to stop debug thread to allow changes to traced variables
		while (debug_thread_state == 1) 		// Wait until the debug thread has confirmed to stop
		{
			k_msleep(1); 						// Sleep briefly to prevent busy waiting, checking debug_thread's termination
		}
	}

	__debugtoken++;								// on every new set trace we take a new debugtoken
	ring_buf_reset(&trace_samples); 			// and reset ringbuffer

	if ((orders->elementsCount > 0) && (orders->elements != NULL))
	{
		if (orders->elementsCount > TRACE_VARS_MAX_COUNT)
		{
			LOG_ERR("SetTraceVariablesList: too many traced variables");
			*debugtoken = TOO_MANY_TRACED;
			return 0;
		}

		trace_reset(); // call PLC Function to reset traced variables
		traced_vars_total_size = 0;
		for (uint32_t i = 0; i < orders->elementsCount; ++i)
		{
			traced_vars_idxs[i] = orders->elements[i].idx; // Var idx to List of traced variables
			if (GetDebugVariable(traced_vars_idxs[i], &var_value, &var_size) == 0)
			{
				traced_vars_size[i] = var_size;		// Variable size in bytes
				traced_vars_total_size += var_size; // add Variable size to total_size
				traced_vars_count = i + 1;			// count of traced variables
			}
			else
			{
				LOG_ERR("SetTraceVariablesList: error reading size of variable idx %u", traced_vars_idxs[i]);
				*debugtoken = TOO_MANY_TRACED;
				return 0;
			}

			set_trace(orders->elements[i].idx, orders->elements[i].force.data ? true : false, orders->elements[i].force.data);
			LOG_DBG("SetTraceVariablesList: var %u, size=%u forced: %s", traced_vars_idxs[i], traced_vars_size[i], orders->elements[i].force.data ? "true" : "false");
		}

		LOG_DBG("SetTraceVariablesList: try to start debug_thread, traced_vars_total_size=%u", traced_vars_total_size);
		if (plc_run)
			plc_debug_thread_start(); // start debug_thread to read variables from PLC into ringbuffer

		*debugtoken = __debugtoken; // return debugtoken to ide
		return 0;
	}
	else
	{
		// dont start debug_thread, when no variables are traced
		*debugtoken = DEBUG_SUSPENDED;
		return 0;
	}

	return 0;
}
#pragma GCC pop_options

/****************************************************************************************************************************************
 * @brief				GetTraceVariables
 *                      Retrieves the currently traced variables and their values for transmission to the host.
 * 						This function is typically called by the host to fetch debugging data from the PLC.
 * @param debugToken    The debug token provided by the host, used to validate the session.
 * @param traces        A pointer to a structure where the traced variables and their values will be stored for transmission.
 * @return              uint32_t - 0, always return 0, Indicate error with traces->PLCstatus
 ****************************************************************************************************************************************/
uint32_t GetTraceVariables(uint32_t debugToken, TraceVariables *traces)
{
	size_t total_bytes_available = 0;
	uint32_t estimated_element_count = 0;
	size_t block_size = 0;

	last_trace_sent_timestamp = k_uptime_get(); 				// get time in ms for timeout in debug_thread

	if (plc_run && debug_thread_state == 0) 					// (re)start debug_thread to read variables from PLC into ringbuffer
	{
		plc_debug_thread_start();
	}

	if (debugToken != __debugtoken) 							// Check if DebugToken matches the current one
	{
		LOG_ERR("GetTraceVariables error: debugToken doesn't match, PLC connection broken");
		traces->traces.elements = (trace_sample *)k_malloc(1 * sizeof(trace_sample));
		traces->traces.elements[0].TraceBuffer.data = (uint8_t *)k_malloc(1 * sizeof(uint8_t));
		traces->traces.elements[0].TraceBuffer.dataLength = 0; // we have no data!
		traces->PLCstatus = Broken;							   // actual PLC Status
		traces->traces.elements[0].tick = __tick;			   // Setting the tick for the combined trace element
		traces->traces.elementsCount = 1;					   // set trace elements
		return 0;											   // always return 0, Indicate error with traces->PLCstatus
	}

	// Determine the total size of a data block Including the size of the tick
	block_size = traced_vars_total_size + sizeof(uint32_t);

	uint32_t element_count = 0;
	uint8_t *data_block;

	while (true) // Wait until data is available in the ring buffer
	{
		total_bytes_available = ring_buf_size_get(&trace_samples);
		estimated_element_count = total_bytes_available / block_size;
		if (estimated_element_count > 0)
			break;

		k_msleep(1);
	}
	
	// // Allocate memory for trace samples
	traces->traces.elements = (trace_sample *)k_malloc(estimated_element_count * sizeof(trace_sample));
	if (traces->traces.elements == NULL)
	{
		LOG_ERR("GetTraceVariables error: failed to allocate memory for trace elements");
		traces->PLCstatus = Broken; // error message to ide
		return 0;					// always return 0, Indicate error with traces->PLCstatus
	}

	// Dynamically allocate memory for trace_sample elements
	while (ring_buf_get_claim(&trace_samples, &data_block, block_size) == block_size)
	{
		uint32_t tick;
		memcpy(&tick, data_block, sizeof(uint32_t));	// Read tick from trace sample

		// Dynamically allocate memory for the data in trace_sample
		traces->traces.elements[element_count].TraceBuffer.data = (uint8_t *)k_malloc(traced_vars_total_size);
		if (traces->traces.elements[element_count].TraceBuffer.data == NULL)
		{
			LOG_ERR("GetTraceVariables error: failed to allocate memory for trace data");
			element_count++;
			continue; // Attempt to read more blocks or abort
		}

		memcpy(traces->traces.elements[element_count].TraceBuffer.data, data_block + sizeof(uint32_t), traced_vars_total_size);
		traces->traces.elements[element_count].TraceBuffer.dataLength = traced_vars_total_size;
		traces->traces.elements[element_count].tick = tick;

		// Mark the read block as finished processing
		ring_buf_get_finish(&trace_samples, block_size);

		element_count++; // Read next element
	}
	traces->traces.elementsCount = element_count;
	traces->PLCstatus = get_PLCStatus();

	return 0;
}
