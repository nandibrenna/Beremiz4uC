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
LOG_MODULE_REGISTER(plc_loader, LOG_LEVEL_DBG);

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/posix/time.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/shell/shell.h>


#include "config.h"
#include "udynlink.h"
#include "plc_network.h"
#include "plc_loader.h"
#include "plc_log_rte.h"
#include "plc_settings.h"
#include "plc_task.h"
#include "plc_time_rtc.h"

#define TESTING

// udynlink can run from __ccm_noinit_section if we use load_mode copy code
__ccm_noinit_section __aligned(4) uint8_t elf_buffer[CONFIG_UDYNLINK_BUFFER_SIZE];

// PLC Hardware IO Variable, defined in plc_task
extern uint16_t QW[QW_COUNT];
extern uint16_t IW[IW_COUNT];
extern uint8_t QX[QX_COUNT];
extern uint8_t IX[IX_COUNT];

// mapping from plc_module to rte variable
uint8_t *__QX0_0_0 = &QX[0];
uint8_t *__QX0_0_1 = &QX[1];
uint8_t *__QX0_0_2 = &QX[2];
uint8_t *__QX0_0_3 = &QX[3];
uint8_t *__QX0_0_4 = &QX[4];
uint8_t *__QX0_0_5 = &QX[5];
uint8_t *__QX0_0_6 = &QX[6];
uint8_t *__QX0_0_7 = &QX[7];

register void *mod_base __asm__("r9");

udynlink_module_t mod;

udynlink_sym_t sym_config_init__;
udynlink_sym_t sym_config_run__;
udynlink_sym_t sym_GetDebugVariable;
udynlink_sym_t sym_RegisterDebugVariable;
udynlink_sym_t sym_force_var;
udynlink_sym_t sym_set_trace;
udynlink_sym_t sym_trace_reset;
udynlink_sym_t sym_common_ticktime__;

// imported functions from plc module
void (*plc_config_init__)(void) = NULL;
void (*plc_config_run__)(uint32_t) = NULL;
int (*plc_GetDebugVariable)(dbgvardsc_index_t, void *, size_t *) = NULL;
int (*plc_RegisterDebugVariable)(dbgvardsc_index_t, void *, size_t *) = NULL;
void (*plc_force_var)(size_t, bool, void *) = NULL;
void (*plc_set_trace)(size_t, bool, void *) = NULL;
void (*plc_trace_reset)(void) = NULL;

uint64_t *common_ticktime__ = NULL;
uint64_t cycle_time_ns;

extern TimeSpec64 __CURRENT_TIME;
extern uint32_t __tick;
extern uint32_t __debugtoken; // RTE debug token
extern uint32_t __debug_tick; // RTE debug cycle

uint32_t plc_initialized = 0;
uint32_t plc_run = 0;
uint32_t plc_force_stop = 0;

extern uint32_t elapsed_time_ms;
extern uint32_t sleep_ms;
extern uint32_t allow_autostart;
uint32_t reload_plc_file = 0;

K_SEM_DEFINE(sem_plc_run, 0, 1);

#define MAX_RAM_BUFFER_SIZE 128
#define PLC_PARTITION plc_partition
#define PLC_PARTITION_ID FIXED_PARTITION_ID(PLC_PARTITION)
#define SIGN (((uint32_t)'M' << 24) | ((uint32_t)'L' << 16) | ((uint32_t)'D' << 8) | (uint32_t)'U')
#define READ_BLOCK_SIZE 1024

// udynlink file header
typedef struct
{
	uint32_t sign;
	uint32_t crc;
} file_header_t;

/****************************************************************************************************************************************
 * @brief    Checks the CRC of the given buffer against the CRC stored in the buffer's header. This function assumes the first part
 *           of the buffer is a file_header_t struct that contains a CRC value for comparison.
 * @param    buffer The buffer containing the data and its CRC in the header.
 * @param    bufferSize The total size of the buffer, including the header.
 * @return   Returns 0 if the CRC matches, -1 otherwise.
 ****************************************************************************************************************************************/
int check_crc(const uint8_t *buffer, size_t bufferSize)
{
	const file_header_t *header = (const file_header_t *)buffer;
	uint32_t calculatedCrc = crc32_ieee(buffer + sizeof(file_header_t), bufferSize - sizeof(file_header_t));
	if (calculatedCrc == header->crc)
	{
		return 0;
	}
	else
	{
		return -1;
	}
}

/*****************************************************************************************************************************/
/*		plc loader thread																									 */
/*****************************************************************************************************************************/
#define PLC_LOADER_STACK_SIZE 4096
#define PLC_LOADER_PRIORITY 5
void plc_loader_task(void *, void *, void *);
K_THREAD_DEFINE_CCM(plc_loader, PLC_LOADER_STACK_SIZE, plc_loader_task, NULL, NULL, NULL, PLC_LOADER_PRIORITY, 0, PLC_LOADER_STARTUP_DELAY);

void plc_loader_task(void *, void *, void *)
{
	if (get_plc_autostart_setting())
	{
		LOG_INF("PLC autostart enabled");
		int ret = load_plc_module(PLC_BIN_FILE);
		if (ret == 0)
		{
			LOG_INF("PLC loaded, now try to start");
			plc_set_start();
			if (plc_run == 1)
				LOG_INF("PLC started");
			else
				LOG_ERR("PLC start failed");
		}
		else
		{
			LOG_ERR("PLC autostart failed, %s", strerror(ret));
		}
	}
	else
	{
		LOG_INF("PLC autostart disabled");
	}

	// now wait to reload plc file if needed
	while (true)
	{
		if (reload_plc_file == 1) 				// reload plc file
		{
			if (plc_run == 0)
			{
				int ret = unload_plc_module(); // unload plc module and set
											   // plc_initialized to 0 for state
				if (ret != 0)
				{
					LOG_WRN("Failed to unload plc module");
				}
				load_plc_module(PLC_BIN_FILE);
			}
			else
				LOG_WRN("plc running, cant load new plc module");

			reload_plc_file = 0;
		}
		k_msleep(500);
	}
}

/****************************************************************************************************************************************
 *
 * PLC internal functions for target debugging
 *
 ****************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief               	relocation function for udynlink loaded module
 *                      	I thought it would be easier, but apparently this has
 *							to be done before every function call.
 * @param void
 * @return void
 ****************************************************************************************************************************************/
static inline void set_reloc_r9(void)
{
	mod_base = mod.p_ram;
	__asm__ volatile("\n\t" ::"r"(mod_base));
}

/****************************************************************************************************************************************
 * @brief               	initialize plc programm
 * @param void
 * @return void
 ****************************************************************************************************************************************/
void config_init__(void)
{
	set_reloc_r9();
	plc_config_init__();

	return;
}

/****************************************************************************************************************************************
 * @brief               		cycle plc programm
 * @param unsigned long tick	plc cycle
 * @return void
 ****************************************************************************************************************************************/
void config_run__(unsigned long tick)
{
	set_reloc_r9();
	plc_config_run__(tick);

	return;
}

/****************************************************************************************************************************************
 * @brief               	function to set variable to trace list and force
 * @param size_t idx		index of debug_variable
 * @param bool   forded		true if forced
 * @param void*  val		value
 * @return void
 ****************************************************************************************************************************************/
void set_trace(size_t idx, bool trace, void *val)
{
	set_reloc_r9();
	plc_set_trace(idx, trace, val);
}

/****************************************************************************************************************************************
 * @brief               	function to reset trace
 * @param void
 * @return void
 ****************************************************************************************************************************************/
void trace_reset(void)
{
	set_reloc_r9();
	return plc_trace_reset();
}

/****************************************************************************************************************************************
 * @brief               	function to force value of variable
 * @param size_t idx		index of debug_variable
 * @param bool   forced		true if forced
 * @param void*  val		value
 * @return void
 ****************************************************************************************************************************************/
void force_var(size_t idx, bool forced, void *val)
{
	set_reloc_r9();
	plc_force_var(idx, forced, val);
	return;
}

/****************************************************************************************************************************************
 * @brief               	function to get value and size of variable
 *
 * @param 	size_t idx		index of debug_variable
 * @param 	void*  value	pointer to value variable
 * @param 	void*  size		pointer to size variable
 * @return 	int				0 OK
 * 							1 no value or size
 * 							2 idx out of bounds
 ****************************************************************************************************************************************/
int GetDebugVariable(dbgvardsc_index_t idx, void *value, size_t *size)
{
	set_reloc_r9();
	return plc_GetDebugVariable(idx, value, size);
}

/****************************************************************************************************************************************
 * @brief               	function to force value of variable
 *                      	currently not used
 * @param size_t idx		index of debug_variable
 * @param bool   forded		true if forced
 * @param void*  val		value
 * @return void
 ****************************************************************************************************************************************/
int RegisterDebugVariable(dbgvardsc_index_t idx, void *forced, size_t *size)
{
	set_reloc_r9();
	plc_RegisterDebugVariable(idx, forced, size);
	return 0;
}

/****************************************************************************************************************************************
 *
 * PLC management functions
 *
 ****************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief               		write_plc_programm_flash
 * 								function is actually in cmd_plc_flash_module and has to be
 *								seperated from shell function
 * @param	int	state
 *
 * @return 	int
 *
 ****************************************************************************************************************************************/
int write_plc_programm_flash(int state)
{
	// not yet implemented
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		erase plc_partition
 *
 * @param	int	state			0 erase flash, delete plc.bin and extra_files
 * 								1 erase flash only
 *
 * @return 	int					0   if module succesfully flashed
 * 								< 0 error
 *
 ****************************************************************************************************************************************/
int erase_plc_programm_flash(int state)
{
	const struct flash_area *plc_partition_area;
	const char *plc_file = PLC_BIN_FILE;	   // plc module file
	const char *md5_file = PLC_MD5_FILE;	   // md5sum of actual plc module
	const char *start_file = PLC_STARTUP_FILE; // startup.scr file

	int err;

	if (plc_initialized > 0)
	{
		if (plc_run > 0)
		{
			LOG_WRN("stopping plc");
			plc_set_stop();
		}
		udynlink_unload_module(&mod);
		plc_initialized = 0;
	}

	err = flash_area_open(PLC_PARTITION_ID, &plc_partition_area);
	if (err)
	{
		LOG_ERR("Error while opening the flash partition: %d", err);
		return -EAGAIN;
	}

	LOG_INF("erase plc_partition");

	err = flash_area_erase(plc_partition_area, 0, plc_partition_area->fa_size);
	if (err)
	{
		LOG_ERR("error while erasing plc_partition: %d %d %s", err, errno, strerror(errno));
		return -EAGAIN;
	}

	LOG_INF("plc_partition erased.");
	flash_area_close(plc_partition_area);

	if (state == 1) // erase also plc.bin and extra_files
	{
		struct fs_dirent dirent;
		struct fs_file_t file;
		fs_file_t_init(&file);
		int rc = 0;

		rc = fs_stat(md5_file, &dirent);
		if (rc == 0)
			fs_unlink(md5_file); // remove md5_file

		rc = fs_stat(plc_file, &dirent);
		if (rc == 0)
			fs_unlink(plc_file); // remove plc_file

		rc = fs_stat(start_file, &dirent);
		if (rc == 0)
			fs_unlink(start_file); // remove start_file

		ResetLogCount(); // clear plc log

		return rc;
	}
	return err;
}

/****************************************************************************************************************************************
 * @brief               		load plc programm from filesystem
 * @param void*  char *filename	file to load
 * @return int					0   if module succesfully loaded
 * 								< 0 error
 ****************************************************************************************************************************************/
int load_plc_module(char *filename)
{
	int32_t flash_loader_ret = -1;
	int32_t file_loader_ret = -1;
	struct fs_file_t file;
	struct fs_dirent dirent;
	ssize_t bytesRead;
	size_t totalBytesRead = 0;

	fs_file_t_init(&file);

	int ret = 0;
	if (strcmp(filename, "FLASH")) // try to load file
	{
		int ret = fs_stat(filename, &dirent);
		if (ret == 0)
		{
			LOG_INF("Loading module: %s (size = %u bytes) to RAM", filename, dirent.size);
			if (dirent.size < sizeof(elf_buffer))
			{
				ret = fs_open(&file, filename, FS_O_READ);
				if (ret != 0)
				{
					LOG_ERR("Failed to open file %s", filename);
					return ret;
				}

				while (totalBytesRead < sizeof(elf_buffer) && (bytesRead = fs_read(&file, elf_buffer + totalBytesRead, READ_BLOCK_SIZE)) > 0)
				{
					totalBytesRead += bytesRead;
					if (totalBytesRead + READ_BLOCK_SIZE > sizeof(elf_buffer))
					{
						LOG_INF("Buffer is full, stopping");
						break;
					}
				}
				fs_close(&file);

				LOG_INF("Module %s loaded, total read size: %zu bytes", filename, totalBytesRead);
				
				if(check_crc(elf_buffer, totalBytesRead) < 0)
				{
					LOG_ERR("CRC check failed");
					return -EIO;
				}

				LOG_INF("CRC check passed, load module");
				if (!udynlink_load_module(&mod, elf_buffer, NULL, 0, UDYNLINK_LOAD_MODE_COPY_ALL))
				{
					const char *mod_name = udynlink_get_module_name(&mod);
					LOG_INF("loaded plc module %s from ram", mod_name);
					file_loader_ret = 1;
				}
			}
			else
			{
				LOG_ERR("Failed to load file %s: not enough ram", filename);
				return -EFBIG;
			}
		}
		else
		{
			return ret;
		}
	}
	else if (!strcmp(filename, "FLASH")) 					// try to load flash
	{
		const struct flash_area *pfa;
		if (flash_area_open(PLC_PARTITION_ID, &pfa) != 0) 	// Prepare the flash partition
		{
			LOG_ERR("Error opening flash partition");
			return -EIO;
		}

		file_header_t header;
		ret = flash_area_read(pfa, 0, &header, sizeof(header));
		// TODO: check also crc
		if ((ret == 0) && (header.sign == SIGN)) // test for udynlink header in flash 
		{
			// try to load flash area with udynlink
			if (!udynlink_load_module(&mod, (void *)pfa->fa_off, NULL, 0, UDYNLINK_LOAD_MODE_XIP))
			{
				const char *mod_name = udynlink_get_module_name(&mod);
				LOG_INF("loaded plc module %s from flash partition at 0x%06x", mod_name, (uint32_t)pfa->fa_off);
				flash_loader_ret = 1;
			}
		}
		else
		{
			LOG_ERR("No PLC module in flash");
			flash_loader_ret = 0;
			return -ENOEXEC;
		}
	}

	// module is loaded, try to lookup symbols
	if (file_loader_ret || flash_loader_ret)
	{
		if (!udynlink_lookup_symbol(&mod, "config_init__", &sym_config_init__))
			return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "config_run__", &sym_config_run__))
			return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "GetDebugVariable", &sym_GetDebugVariable))
			return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "RegisterDebugVariable", &sym_RegisterDebugVariable))
			; // return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "force_var", &sym_force_var))
			; // return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "set_trace", &sym_set_trace))
			return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "trace_reset", &sym_trace_reset))
			return -EAGAIN;
		if (!udynlink_lookup_symbol(&mod, "common_ticktime__", &sym_common_ticktime__))
			return -EAGAIN;

		set_reloc_r9();
		/****************************************************************************************************************************************
		 * @brief	Assigning function pointers to the corresponding functions from a symbolic table.
		 ***************************************************************************************************************************************/
		// Function Pointer          Return Type  | Parameters                              | Symbolic Address
		//---------------------------------------------------------------------------------------------------------------------------------------
		plc_config_init__          = (void     (*)(void))                           		(uintptr_t)sym_config_init__.val;
		plc_config_run__           = (void     (*)(uint32_t))                       		(uintptr_t)sym_config_run__.val;
		plc_GetDebugVariable       = (int      (*)(dbgvardsc_index_t, void *, size_t *)) 	(uintptr_t)sym_GetDebugVariable.val;
		plc_RegisterDebugVariable  = (int      (*)(dbgvardsc_index_t, void *, size_t *)) 	(uintptr_t)sym_RegisterDebugVariable.val;
		plc_force_var              = (void     (*)(size_t, bool, void *))           		(uintptr_t)sym_force_var.val;
		plc_set_trace              = (void     (*)(size_t, bool, void *))           		(uintptr_t)sym_set_trace.val;
		plc_trace_reset            = (void     (*)(void))                           		(uintptr_t)sym_trace_reset.val;
		common_ticktime__          = (uint64_t  *)                                  		(uintptr_t)sym_common_ticktime__.val;

		cycle_time_ns = *common_ticktime__;
		LOG_INF("initialized plc functions, ready to run plc code");
		plc_initialized = 1;
	}
	else
	{
		LOG_ERR("FAIL: cant load bin file with udynlink");
		return -ENOTSUP;
	}

	return 0;
}

/****************************************************************************************************************************************
 * @brief               		unload plc programm
 * @param
 * @return int					0   if module succesfully unloaded	< 0 error
 ****************************************************************************************************************************************/
int unload_plc_module(void)
{
	int ret = 0;
	// unload plc module only if plc is stopped and module is loaded
	if ((plc_run == 0) && (plc_initialized == 1))
	{
		plc_initialized = 0;
		ret = udynlink_unload_module(&mod);
		if (ret == UDYNLINK_OK)
			LOG_INF("plc module unloaded");
		else
			LOG_ERR("error unloading plc");
		return 0;
	}

	return ret;
}

/****************************************************************************************************************************************
 * @brief               		function to run plc programm
 * @param void
 * @return int					0   if set to run, < 0 error
 ****************************************************************************************************************************************/
void plc_set_start(void)
{
	if (!(plc_initialized > 0))
	{
		LOG_ERR("no plc programm loaded!");
		return;
	}
	else
	{
		if (plc_run == 0)
		{
			plc_run = 1;
			LOG_INF("starting plc thread");
			k_sem_give(&sem_plc_run);
		}
		else
		{
			LOG_WRN("plc already running");
		}
	}
}

/****************************************************************************************************************************************
 * @brief               		function to stop plc programm
 *                      		takes no arguments
 * @param void
 * @return int					0   if plc stopped, < 0 error
 ****************************************************************************************************************************************/
void plc_set_stop(void)
{
	if (plc_run == 1)
	{
		plc_run = 0;
		LOG_INF("stopping plc thread");
		int ret = k_sem_take(&sem_plc_run, K_MSEC(1000));
		if (ret == -EAGAIN)
		{
			LOG_ERR("can not stop plc thread");
			k_sem_reset(&sem_plc_run);
		}
		else
		{
			LOG_INF("stopped plc thread");
		}
	}
	else
	{
		LOG_INF("plc not running");
	}
}

/****************************************************************************************************************************************
 * @brief               		function to get running state of plc programm
 * @param void
 * @return int					1 if plc is running, 0 if plc is stopped
 ****************************************************************************************************************************************/
int plc_get_state(void) { return plc_run; }

/****************************************************************************************************************************************
 * @brief               		function to get loader state of plc programm
 * @param void
 * @return int					1 if plc is loaded, 0 if not
 ****************************************************************************************************************************************/
int plc_get_loader_state(void) { return plc_initialized; }

/****************************************************************************************************************************************
 *
 * udynlink functions
 *
 ***************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief               	callback function for udynlink to resolve external symbols
 * @param const char *name	name of external symbol
 * @return uint32_t     	address of external symbol or NULL if not found
 ****************************************************************************************************************************************/
uint32_t udynlink_external_resolve_symbol(const char *name)
{
	if (!strcmp(name, "printf"))
		return (uint32_t)&printf;
	else if (!strcmp(name, "printk"))
		return (uint32_t)&printk;
	else if (!strcmp(name, "rte_log_inf"))
		return (uint32_t)&rte_log_inf;
	else if (!strcmp(name, "puts"))
		return (uint32_t)&puts;
	else if (!strcmp(name, "putchar"))
		return (uint32_t)&putchar;
	else if (!strcmp(name, "memcpy"))
		return (uint32_t)&memcpy;
	else if (!strcmp(name, "memset"))
		return (uint32_t)&memset;
	else if (!strcmp(name, "LogMessage"))
		return (uint32_t)&LogMessage;
	else if (!strcmp(name, "__CURRENT_TIME"))
		return (uint32_t)&__CURRENT_TIME;
	else if (!strcmp(name, "__QX0_0_0"))
		return (uint32_t)&__QX0_0_0;
	else if (!strcmp(name, "__QX0_0_1"))
		return (uint32_t)&__QX0_0_1;
	else if (!strcmp(name, "__QX0_0_2"))
		return (uint32_t)&__QX0_0_2;
	else if (!strcmp(name, "__QX0_0_3"))
		return (uint32_t)&__QX0_0_3;
	else if (!strcmp(name, "__QX0_0_4"))
		return (uint32_t)&__QX0_0_4;
	else if (!strcmp(name, "__QX0_0_5"))
		return (uint32_t)&__QX0_0_5;
	else if (!strcmp(name, "__QX0_0_6"))
		return (uint32_t)&__QX0_0_6;
	else if (!strcmp(name, "__QX0_0_7"))
		return (uint32_t)&__QX0_0_7;
	else
	{
		LOG_ERR("udynling module is missing symbol: %s", name);
		return (uint32_t)NULL;
	}
}

/****************************************************************************************************************************************
 * @brief               callback function for udynlink to free allocated memory
 *                      simple calls k_free()
 * @param void* p		ptr to memory block
 ****************************************************************************************************************************************/
void udynlink_external_free(void *p) { k_free(p); }

/****************************************************************************************************************************************
 * @brief               callback function for udynlink to allocate memory
 *                      simple calls k_malloc()
 * @param size_t size   size of memory block
 ****************************************************************************************************************************************/
void *udynlink_external_malloc(size_t size) { return k_malloc(size); }

/****************************************************************************************************************************************
 *
 * shell commands to call internal plc (management) functions from the shell
 *
 ***************************************************************************************************************************************/

/****************************************************************************************************************************************
 * @brief               		erase plc_partition
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					0   if module succesfully flashed
 * 								< 0 error
 ****************************************************************************************************************************************/
static int cmd_plc_erase_plc_partition(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int err = 0;

	err = erase_plc_programm_flash(0); // erase flash, delete plc.bin and extra_files

	return err;
}

/****************************************************************************************************************************************
 * @brief               		shell command to flash plc programm from filesystem
 * 								to plc partition arg[1] = path to module
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					0   if module succesfully flashed < 0 error
 ****************************************************************************************************************************************/
static int cmd_plc_flash_module(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);

	const struct flash_area *pfa;
	struct fs_file_t file;
	fs_file_t_init(&file);
	uint32_t calculated_crc = 0;
	file_header_t header;
	off_t file_size;

	// Open the file with read flag
	if (fs_open(&file, argv[1], FS_O_READ) < 0)
	{
		shell_error(sh, "Error opening file %s", argv[1]);
		return -1;
	}

	// Get file size
	fs_seek(&file, 0, FS_SEEK_END);
	file_size = fs_tell(&file);
	fs_seek(&file, 0, FS_SEEK_SET);

	// Read the header
	if (fs_read(&file, &header, sizeof(header)) != sizeof(header))
	{
		shell_error(sh, "Error reading header from file");
		fs_close(&file);
		return -2;
	}

	// TODO Verify CRC
	// Verify the signature
	if (header.sign != SIGN)
	{
		shell_error(sh, "Invalid file signature");
		fs_close(&file);
		return -3;
	}

	shell_info(sh, "Starting flash process...");

	// Prepare the flash partition
	if (flash_area_open(PLC_PARTITION_ID, &pfa) != 0)
	{
		shell_error(sh, "Error opening flash partition");
		fs_close(&file);
		return -4;
	}

	// Erase the partition
	if (flash_area_erase(pfa, 0, pfa->fa_size) != 0)
	{
		shell_error(sh, "Error erasing flash partition");
		flash_area_close(pfa);
		fs_close(&file);
		return -5;
	}

	// Write the header to flash first
	if (flash_area_write(pfa, 0, &header, sizeof(header)) != 0)
	{
		shell_error(sh, "Error writing header to flash partition");
		flash_area_close(pfa);
		fs_close(&file);
		return -6;
	}

	uint8_t buffer[MAX_RAM_BUFFER_SIZE];
	ssize_t bytes_read;
	off_t offset = sizeof(header); // Start offset for flash writing after the header

	// Read and write the rest of the file in parts
	shell_fprintf(sh, SHELL_VT100_COLOR_GREEN, "flashing");
	while ((bytes_read = fs_read(&file, buffer, sizeof(buffer))) > 0)
	{
		if (flash_area_write(pfa, offset, buffer, bytes_read) != 0)
		{
			shell_error(sh, "Error writing to flash partition");
			flash_area_close(pfa);
			fs_close(&file);
			return -7;
		}

		calculated_crc = crc32_ieee_update(calculated_crc, buffer, bytes_read);
		offset += bytes_read;
		shell_fprintf(sh, SHELL_VT100_COLOR_GREEN, ".");
	}
	shell_fprintf(sh, SHELL_VT100_COLOR_DEFAULT, "\n");
	// CRC verification and final messages...
	// if (calculated_crc != header.crc)
	// {
	// 	shell_error(sh, "CRC verification failed");
	// 	flash_area_close(pfa);
	// 	fs_close(&file);
	// 	return -8;
	// }

	flash_area_close(pfa);
	fs_close(&file);
	shell_info(sh, "File flashed successfully with verified CRC");

	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to load plc programm from
 *								filesystem arg[1] = path to module
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					0   if module succesfully loaded < 0 error
 ****************************************************************************************************************************************/
static int cmd_plc_load_module(const struct shell *sh, size_t argc, char **argv)
{
	load_plc_module(argv[1]);
	return 0;
}

static int cmd_plc_unload_module(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!(plc_initialized > 0))
	{
		// LOG_WRN("no plc module loaded");
		LOG_INF("no plc module loaded");
		return -EAGAIN;
	}
	else
	{
		if (plc_run == 1)
		{
			LOG_INF("plc is running, you cannot unload while plc is running");
			return -EAGAIN;
		}
		else
		{
			unload_plc_module();
			return 0;
		}
	}
}

/****************************************************************************************************************************************
 * @brief               		shell command to run plc programm
 *                      		takes no arguments
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					0 if plc running
 ****************************************************************************************************************************************/
static int cmd_plc_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	plc_set_start();
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to stop plc programm
 *                      		takes no arguments
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					0 if plc stopped
 ****************************************************************************************************************************************/
static int cmd_plc_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	plc_set_stop();
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command get plc status
 * 								prints some common status messages
 * 								if TESTING defined, prints more
 *                       		takes no arguments
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					always 0
 ****************************************************************************************************************************************/
static int cmd_plc_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	set_reloc_r9();
	shell_print(sh, "*************** PLC Status ***************");
	shell_print(sh, "*---       %s        ---*",get_time_str());
	shell_print(sh, "******************************************");
	shell_print(sh, "%-25s %u", "PLC initialized:", plc_initialized);
	shell_print(sh, "%-25s %u", "PLC running:", plc_run);
	shell_print(sh, "%-25s %ums", "actual runtime:", elapsed_time_ms);
	shell_print(sh, "%-25s %ums", "actual sleeptime:", sleep_ms);
#ifdef PLC_SHELL_TEST_COMMANDS
	shell_print(sh, "%-25s %p", "mod.p_ram at", mod.p_ram);
	shell_print(sh, "%-25s %p", "config_init__ at", plc_config_init__);
	shell_print(sh, "%-25s %p", "config_run__ at", plc_config_run__);
	shell_print(sh, "%-25s %p", "common_ticktime__ at", common_ticktime__);
	shell_print(sh, "%-25s %llums", "common_ticktime__=", (*common_ticktime__) / NSEC_PER_MSEC);
	shell_print(sh, "%-25s %u", "__tick=", __tick);
	shell_print(sh, "%-25s %u %u %u %u %u %u %u %u", "QX[]= ", QX[0], QX[1], QX[2], QX[3], QX[4], QX[5], QX[6], QX[7]);
	shell_print(sh, "%-25s %u %u %u %u %u %u %u %u", "IX[]= ", IX[0], IX[1], IX[2], IX[3], IX[4], IX[5], IX[6], IX[7]);
#endif
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to update time with NTP
 *                      		takes no arguments
 * @param struct shell *sh		shell
 * @param size_t argc			argument count
 * @param void*  char **argv	argument list
 * @return int					always 0
 *								SNTP client logs error messages
 ****************************************************************************************************************************************/
static int cmd_plc_update_ntp_time(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct timespec ts;

	sync_clock_via_sntp();
	clock_gettime(CLOCK_REALTIME, &ts);
	struct tm *now = localtime(&ts.tv_sec);
	LOG_INF("%s", asctime(now));
	return 0;
}

/****************************************************************************************************************************************
 *
 * PLC testing functions for shell
 *
 ***************************************************************************************************************************************/

#ifdef PLC_SHELL_TEST_COMMANDS
/****************************************************************************************************************************************
 * @brief               		shell command to log a message in PLC
 * 								only available if TESTING is defined
 *                       		argv[1] level argv[2] msg argv[3] size
 ****************************************************************************************************************************************/
static int cmd_plc_test_log(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	size_t size = strlen(argv[2]);
	LogMessage(atoi(argv[1]), argv[2], size);

	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to reset plc log
 * 								only available if TESTING is defined
 ****************************************************************************************************************************************/
static int cmd_plc_reset_log(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	ResetLogCount();
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to call any PLC function
 * 								only available if TESTING is defined
 ****************************************************************************************************************************************/
static int cmd_plc_call_func(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	
	if (!(mod.ram_base > 0))
	{
		LOG_INF("load module first!");
		return -EAGAIN;
	}

	LOG_INF("lookup function %s", argv[1]);
	udynlink_sym_t sym_func;
	if (!udynlink_lookup_symbol(&mod, argv[1], &sym_func))
	{
		LOG_INF("%s not found in module!", argv[1]);
		return -EAGAIN;
	}
	else
	{
		set_reloc_r9();
		int (*func)(void) = (int (*)(void))sym_func.val;
		return func();
	}
}

/****************************************************************************************************************************************
 * @brief               		shell command to call force_var
 * 								only available if TESTING is defined
 ****************************************************************************************************************************************/
static int cmd_plc_force_var(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint32_t idx = atoi(argv[1]);
	bool forced = atoi(argv[2]);
	uint32_t val = atoi(argv[3]);

	force_var(idx, forced, &val);
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to get tick value (cycle counter)
 * 								only available if TESTING is defined
 ****************************************************************************************************************************************/
static int cmd_plc_get_tick(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	size_t (*func)(size_t) = NULL;
	udynlink_sym_t sym_func;
	unsigned long long common_ticktime;

	if (!(mod.ram_base > 0))
	{
		LOG_INF("load module first!");
		return -EAGAIN;
	}

	if (!udynlink_lookup_symbol(&mod, "common_ticktime__", &sym_func))
	{
		// LOG_INF("%s not found in module!", "common_ticktime__");
		return -EAGAIN;
	}
	else
	{
		common_ticktime = (uint64_t *)sym_func.val;
		LOG_INF("common_ticktime=%llu", common_ticktime);
		return 0;
	}
}

/****************************************************************************************************************************************
 * @brief               		shell command to dump memory region
 * 								only available if TESTING is defined
 ****************************************************************************************************************************************/
static int cmd_plc_dump_mem(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	char buffer[1024];
	char *endptr;

	uint32_t address = strtol(argv[1], &endptr, 0);
	if (*endptr != '\0')
	{
		shell_print(sh, "Invalid input: %s", argv[1]);
		return -1;
	}

	size_t length = strtol(argv[2], &endptr, 0);
	if (*endptr != '\0')
	{
		shell_print(sh, "Invalid input: %s", argv[2]);
		return -1;
	}

	if (length >= 1024)
		length = 1024;

	memcpy(buffer, (void *)address, length);

	for (size_t i = 0; i < length; i += 16)
	{
		size_t chunk_size = (length - i < 16) ? (length - i) : 16;
		shell_hexdump_line(sh, address + i, buffer + i, chunk_size);
	}
	return 0;
}

/****************************************************************************************************************************************
 * @brief               		shell command to reboot system
 * 								only available if TESTING is defined
  ****************************************************************************************************************************************/
static int cmd_plc_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	sys_reboot(1);
	return 0;
}

#endif

/****************************************************************************************************************************************
 * @brief               		definition of shell sub commands for shell command
 *								PLC partly only available if TESTING is defined
 ****************************************************************************************************************************************/
SHELL_STATIC_SUBCMD_SET_CREATE(
	udynlink_sub,
	SHELL_CMD_ARG(status, NULL, "PLC status", cmd_plc_status, 1, 0), 
	SHELL_CMD_ARG(flash, NULL, "write plc module to flash", cmd_plc_flash_module, 2, 0),
	SHELL_CMD_ARG(flash_erase, NULL, "erase plc module in flash", cmd_plc_erase_plc_partition, 1, 0), 
	SHELL_CMD_ARG(load, NULL, "load plc module ", cmd_plc_load_module, 2, 0),
	SHELL_CMD_ARG(unload, NULL, "unload plc module from ram", cmd_plc_unload_module, 1, 0), 
	SHELL_CMD_ARG(run, NULL, "start plc programm", cmd_plc_start, 1, 0),
	SHELL_CMD_ARG(stop, NULL, "stop plc programm", cmd_plc_stop, 1, 0), 
	SHELL_CMD_ARG(update_time, NULL, "update time via sntp", cmd_plc_update_ntp_time, 1, 0),
#ifdef PLC_SHELL_TEST_COMMANDS
	SHELL_CMD_ARG(reboot, NULL, "reboot system", cmd_plc_reboot, 1, 0),
	SHELL_CMD_ARG(reset_log, NULL, "reset_log function", cmd_plc_reset_log, 1, 2), 
	SHELL_CMD_ARG(test_log, NULL, "test_log LogLevel msg", cmd_plc_test_log, 1, 3),
	SHELL_CMD_ARG(call, NULL, "call plc module function", cmd_plc_call_func, 1, 2), 
	SHELL_CMD_ARG(force_var, NULL, "PLC debug force var", cmd_plc_force_var, 1, 4), 
	SHELL_CMD_ARG(get_tick, NULL, "PLC get cycle counter", cmd_plc_get_tick, 1, 2),
	SHELL_CMD_ARG(dump, NULL, "dump memory area", cmd_plc_dump_mem, 1, 2), 
#endif
	SHELL_SUBCMD_SET_END);

/****************************************************************************************************************************************
 * @brief               		definition of main shell commands PLC
 *
 ****************************************************************************************************************************************/
SHELL_CMD_ARG_REGISTER(plc, &udynlink_sub, "plc module loader", NULL, 3, 0);
