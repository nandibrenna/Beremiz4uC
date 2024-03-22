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
#ifdef CONFIG_PLC_SHELL_COMMANDS
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(plc_cli, LOG_LEVEL_DBG);

#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <sys/stat.h>
#include <time.h>

#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_core.h>

#include "plc_settings.h"
#include "plc_filesys.h"

#define MKFS_DEV_ID FIXED_PARTITION_ID(storage_partition)
#define MKFS_FLAGS 0

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"
static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

extern struct sys_heap _system_heap;

// ANSI Farbencodes
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_RESET "\x1b[0m"
#define ANSI_COLOR_BLUE "\x1b[94m"

#define VAL_TRUE "\x1b[32mtrue\x1b[0m"
#define VAL_FALSE "\x1b[33mfalse\x1b[0m"

#define MAX_PATH_LEN 128
#define MAX_FILENAME_LEN 128
#define MAX_INPUT_LEN 20
#define NAME_COLUMN_WIDTH 25
#define SIZE_COLUMN_WIDTH 10

static char cwd[MAX_PATH_LEN] = "/";

static int cmd_show_or_set_settings(const struct shell *shell, size_t argc, char **argv)
{
	if (argc == 1)
	{
		shell_fprintf(shell, SHELL_NORMAL, "Setting              Value\n");
		shell_fprintf(shell, SHELL_NORMAL, "-------------------- ------------------------------\n");
		shell_fprintf(shell, SHELL_NORMAL, "%-20s %s\n", "mount_sd_card", get_mount_sd_card_setting() ? VAL_TRUE : VAL_FALSE);
		shell_fprintf(shell, SHELL_NORMAL, "%-20s %s\n", "start_plc_at_boot", get_plc_autostart_setting() ? VAL_TRUE : VAL_FALSE);
	}
	else if (argc == 3)
	{
		if (strcmp(argv[1], "mount_sd_card") == 0)
		{
			bool val = strcmp(argv[2], "true") == 0 ? true : false;
			set_mount_sd_card_setting(val);
			shell_fprintf(shell, SHELL_NORMAL, "Setting '%s' updated to '%s'.\n", argv[1], argv[2]);
		}
		else if (strcmp(argv[1], "start_plc_at_boot") == 0)
		{
			bool val = strcmp(argv[2], "true") == 0 ? true : false;
			set_plc_autostart_setting(val);
			shell_fprintf(shell, SHELL_NORMAL, "Setting '%s' updated to '%s'.\n", argv[1], argv[2]);
		}
		else
		{
			shell_fprintf(shell, SHELL_NORMAL, "Unknown setting '%s'.\n", argv[1]);
		}
		// Optional: Save settings after updating
		plc_settings_save();
	}
	else
	{
		shell_fprintf(shell, SHELL_NORMAL, "Usage: settings [setting value]\n");
	}

	return 0;
}

static void create_full_path(const char *base_path, const char *name, char *full_path, size_t max_len)
{
	if (strcmp(base_path, "/") == 0)
	{
		snprintf(full_path, max_len, "/%s", name);
	}
	else
	{
		snprintf(full_path, max_len, "%s/%s", base_path, name);
	}
}

static void create_abs_path(const char *name, char *path, size_t len)
{
	if (name[0] == '/')
	{
		strncpy(path, name, len);
		path[len - 1] = '\0';
	}
	else
	{
		if (cwd[1] == '\0')
		{
			__ASSERT_NO_MSG(len >= 2);
			*path++ = '/';
			--len;

			strncpy(path, name, len);
			path[len - 1] = '\0';
		}
		else
		{
			strncpy(path, cwd, len);
			path[len - 1] = '\0';

			size_t plen = strlen(path);

			if (plen < len)
			{
				path += plen;
				*path++ = '/';
				len -= plen + 1U;
				strncpy(path, name, len);
				path[len - 1] = '\0';
			}
		}
	}
}

static int cmd_cd(const struct shell *sh, size_t argc, char **argv)
{
	char path[MAX_PATH_LEN];
	struct fs_dirent entry;
	int err;

	if (argc < 2)
	{
		strcpy(cwd, "/");
		return 0;
	}

	if (strcmp(argv[1], "..") == 0)
	{
		char *prev = strrchr(cwd, '/');

		if (!prev || prev == cwd)
		{
			strcpy(cwd, "/");
		}
		else
		{
			*prev = '\0';
		}

		/* No need to test that a parent exists */
		return 0;
	}

	create_abs_path(argv[1], path, sizeof(path));

	err = fs_stat(path, &entry);
	if (err)
	{
		shell_error(sh, "%s doesn't exist", path);
		return -ENOEXEC;
	}

	if (entry.type != FS_DIR_ENTRY_DIR)
	{
		shell_error(sh, "%s is not a directory", path);
		return -ENOEXEC;
	}

	strncpy(cwd, path, sizeof(cwd));
	cwd[sizeof(cwd) - 1] = '\0';

	return 0;
}

static int cmd_ls(const struct shell *sh, size_t argc, char **argv)
{
	char path[MAX_PATH_LEN];
	char full_path[MAX_PATH_LEN + NAME_MAX];
	if (argc >= 2)
	{
		strncpy(path, argv[1], sizeof(path));
	}
	else
	{
		strncpy(path, cwd, sizeof(path));
	}
	path[sizeof(path) - 1] = '\0';

	struct fs_dir_t dir;
	struct fs_dirent entry;
	struct stat stat_buf;
	int err;
	char mod_time_str[32];

	fs_dir_t_init(&dir);

	err = fs_opendir(&dir, path);
	if (err)
	{
		shell_error(sh, "Unable to open %s (err %d)", path, err);
		return -ENOEXEC;
	}

	shell_print(sh, "Listing directory: %s", path);
	shell_print(sh, "%-*s %-*s %-19s %s", NAME_COLUMN_WIDTH, "Name", SIZE_COLUMN_WIDTH, "Size", "Last Modified", "Permissions");

	while (fs_readdir(&dir, &entry) == 0)
	{

		if (entry.name[0] == '\0')
		{
			break;
		}

		create_full_path(path, entry.name, full_path, sizeof(full_path));
		if (stat(full_path, &stat_buf) == 0)
		{
			strftime(mod_time_str, sizeof(mod_time_str), "%Y-%m-%d %H:%M", localtime(&stat_buf.st_mtime));

			char permissions[12];
			snprintf(permissions, sizeof(permissions), "%c%c%c%c%c%c%c%c%c%c", (S_ISDIR(stat_buf.st_mode)) ? 'd' : '-', (stat_buf.st_mode & S_IRUSR) ? 'r' : '-',
					 (stat_buf.st_mode & S_IWUSR) ? 'w' : '-', (stat_buf.st_mode & S_IXUSR) ? 'x' : '-', (stat_buf.st_mode & S_IRGRP) ? 'r' : '-',
					 (stat_buf.st_mode & S_IWGRP) ? 'w' : '-', (stat_buf.st_mode & S_IXGRP) ? 'x' : '-', (stat_buf.st_mode & S_IROTH) ? 'r' : '-',
					 (stat_buf.st_mode & S_IWOTH) ? 'w' : '-', (stat_buf.st_mode & S_IXOTH) ? 'x' : '-');
					  permissions[11] = '\0'; 
			if (S_ISDIR(stat_buf.st_mode))
			{
				shell_print(sh, ANSI_COLOR_BLUE "%-*s" ANSI_COLOR_RESET " %-*s %-19s %s", NAME_COLUMN_WIDTH, entry.name, SIZE_COLUMN_WIDTH, "<DIR>", mod_time_str, permissions);
			}
			else
			{
				shell_print(sh, ANSI_COLOR_GREEN "%-*s" ANSI_COLOR_RESET " %-*ld %-19s %s", NAME_COLUMN_WIDTH, entry.name, SIZE_COLUMN_WIDTH, stat_buf.st_size, mod_time_str,
							permissions);
			}
		}
		else
		{
			shell_error(sh, "Could not get stats for %s", full_path);
		}
	}
	fs_closedir(&dir);
	shell_print(sh, "");
	return 0;
}

static int cmd_pwd(const struct shell *sh, size_t argc, char **argv)
{
	shell_print(sh, "%s", cwd);

	return 0;
}

static int cmd_touch(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2)
	{
		shell_print(sh, "Usage: touch <filename>");
		return -EINVAL;
	}

	char full_path[MAX_PATH_LEN];
	create_full_path(cwd, argv[1], full_path, sizeof(full_path));
	struct fs_file_t file;
	fs_file_t_init(&file);
	int err = fs_open(&file, full_path, FS_O_CREATE);
	if (err)
	{
		shell_error(sh, "Unable to create file %s (err %d)", full_path, err);
		return err;
	}

	fs_close(&file);
	return 0;
}

static int cmd_rm(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2)
	{
		shell_print(sh, "Usage: rm <filename>");
		return -EINVAL;
	}

	char full_path[MAX_PATH_LEN];
	create_full_path(cwd, argv[1], full_path, sizeof(full_path));

	int err = fs_unlink(full_path);
	if (err)
	{
		shell_error(sh, "Unable to delete file %s (err %d)", full_path, err);
		return err;
	}

	return 0;
}

static int cmd_cat(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2)
	{
		shell_print(sh, "Usage: cat <filename>");
		return -EINVAL;
	}

	char full_path[MAX_PATH_LEN];
	create_full_path(cwd, argv[1], full_path, sizeof(full_path));

	struct fs_file_t file;
	fs_file_t_init(&file);

	int err = fs_open(&file, full_path, FS_O_READ);
	if (err)
	{
		shell_error(sh, "Unable to open file %s (err %d)", full_path, err);
		return err;
	}

	char buffer[128];
	ssize_t read;
	while ((read = fs_read(&file, buffer, sizeof(buffer) - 1)) > 0)
	{
		buffer[read] = '\0';
		shell_print(sh, "%s", buffer);
	}

	fs_close(&file);
	return 0;
}

static int cmd_format_lfs(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Format LittleFS partition.\n");
	int ret = fs_mkfs(FS_LITTLEFS, (uintptr_t)MKFS_DEV_ID, NULL, MKFS_FLAGS);
	if (ret < 0)
	{
		shell_error(sh, "Error formatting the LittleFS partition. %d\n", ret);
		return ret;
	}

	shell_print(sh, "LittleFS partition formatted.\n");
	struct fs_statvfs sbuf;
	fs_statvfs("/lfs:", &sbuf);
	uint32_t memory_size_lfs = sbuf.f_frsize * sbuf.f_blocks;
	uint32_t memory_free_lfs = sbuf.f_frsize * sbuf.f_bfree;
	shell_print(sh, "PLC Internal Filesystem /lfs: free %d KB/%d KB\n", memory_free_lfs >> 10, memory_size_lfs >> 10);
	return 0;
}

static int cmd_mkdir(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2)
	{
		shell_error(sh, "Usage: mkdir <directory>");
		return -EINVAL;
	}

	char full_path[MAX_PATH_LEN];
	create_full_path(cwd, argv[1], full_path, sizeof(full_path));

	int err = fs_mkdir(full_path);
	if (err)
	{
		shell_error(sh, "Unable to create directory %s (err %d)", full_path, err);
		return err;
	}
	return 0;
}

static int cmd_rmdir(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2)
	{
		shell_error(sh, "Usage: rmdir <directory>");
		return -EINVAL;
	}

	char full_path[MAX_PATH_LEN];
	create_full_path(cwd, argv[1], full_path, sizeof(full_path));

	int err = fs_unlink(full_path);
	if (err)
	{
		shell_error(sh, "Unable to remove directory %s (err %d)", full_path, err);
		return err;
	}
	return 0;
}

static int cmd_mv(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 3)
	{
		shell_error(sh, "Usage: mv <source> <destination>");
		return -EINVAL;
	}

	const char *source_path = argv[1];
	const char *destination_path = argv[2];

	int err = fs_rename(source_path, destination_path);
	if (err)
	{
		shell_error(sh, "Unable to move %s to %s (err %d)", source_path, destination_path, err);
		return err;
	}

	shell_print(sh, "%s moved to %s", source_path, destination_path);
	return 0;
}

static int cmd_mount(const struct shell *shell, size_t argc, char **argv)
{
	// mount External SD/MMC
	static const char *disk_mount_pt = DISK_MOUNT_PT;
	mp.mnt_point = disk_mount_pt;

	static const char *disk_pdrv = DISK_DRIVE_NAME;
	if (disk_access_init(disk_pdrv) != 0)
	{
		LOG_ERR("SD/MMC Storage init ERROR!");
		return -1;
	}

	int res = fs_mount(&mp);
	return res;
}

static int cmd_unmount(const struct shell *shell, size_t argc, char **argv)
{
	// unount External SD/MMC
	static const char *disk_mount_pt = DISK_MOUNT_PT;
	mp.mnt_point = disk_mount_pt;

	int res = fs_unmount(&mp);
	return res;
}

static int cmd_run(const struct shell *shell, size_t argc, char **argv)
{
	if (argc != 2)
	{
		shell_error(shell, "Usage: run <path_to_script_file>");
		return -EINVAL;
	}

	const char *script_path = argv[1];
	FILE *fp = fopen(script_path, "r");

	if (!fp)
	{
		shell_error(shell, "Can't open script file: %s", script_path);
		return -ENOENT;
	}

	char line[128];
	if (!fgets(line, sizeof(line), fp) || strncmp(line, "#!", 2) != 0)
	{
		shell_error(shell, "File is not a valid script or missing shebang (#!): %s", script_path);
		fclose(fp);
		return -EINVAL;
	}

	int ret = 0;
	while (fgets(line, sizeof(line), fp) != NULL)
	{
		size_t len = strlen(line);
		if (line[0] == '#' || len == 0)
			continue; // Skip comments and empty lines

		if (len > 0 && line[len - 1] == '\n')
		{
			line[len - 1] = '\0'; // Remove newline character
		}

		ret = shell_execute_cmd(shell, line);
		if (ret < 0)
		{
			shell_error(shell, "Failed to execute command: %s", line);
			break;
		}
	}

	fclose(fp);
	return ret;
}

static void print_heap_usage(const struct shell *shell)
{
#if IS_ENABLED(CONFIG_SYS_HEAP_RUNTIME_STATS)
	struct sys_memory_stats stats;
	uint32_t heap_size = CONFIG_HEAP_MEM_POOL_SIZE;
	sys_heap_runtime_stats_get(&_system_heap, &stats);
	uint32_t usage_percent = (stats.allocated_bytes * 100U) / heap_size;
	const char *color = usage_percent > 95 ? "\x1B[31m" : (usage_percent > 80 ? "\x1B[33m" : "");

	shell_fprintf(shell, SHELL_NORMAL, "Heap Memory:  ");
	shell_fprintf(shell, SHELL_NORMAL, "%sUsed: %zu bytes  ", color, stats.allocated_bytes);
	shell_fprintf(shell, SHELL_NORMAL, "%sFree: %zu bytes\n\x1B[0m", color, heap_size - stats.allocated_bytes);
#endif
}

static void print_thread_stack_usage(const struct k_thread *thread, void *user_data)
{
	const struct shell *shell = user_data;
	size_t unused_space;
	int ret = k_thread_stack_space_get(thread, &unused_space);

	if (ret)
	{
		shell_error(shell, "Error getting stack space for thread %p", thread);
		return;
	}

	size_t total_stack_size = thread->stack_info.size;
	size_t used_space = total_stack_size - unused_space;
	uint32_t usage_percent = (used_space * 100U) / total_stack_size;

	const char *color = usage_percent > 95 ? "\x1B[31m" : (usage_percent > 80 ? "\x1B[33m" : "");
	const char *thread_name = k_thread_name_get((k_tid_t)thread);
	if (thread_name == NULL || strlen(thread_name) == 0)
	{
		thread_name = "Unnamed";
	}

	shell_fprintf(shell, SHELL_NORMAL, "%s%-20s : %5zu / %5zu bytes (%3u%%)\n\x1B[0m", color, thread_name, used_space, total_stack_size, usage_percent);
}

static void print_filesystem_usage(const struct shell *shell)
{
	struct fs_statvfs sbuf;
	int ret = fs_statvfs("/lfs:", &sbuf);
	if (ret < 0)
	{
		shell_error(shell, "Unable to get filesystem stats");
		return;
	}

	uint32_t memory_size_lfs = sbuf.f_frsize * sbuf.f_blocks;
	uint32_t memory_free_lfs = sbuf.f_frsize * sbuf.f_bfree;
	uint32_t usage_percent_lfs = ((memory_size_lfs - memory_free_lfs) * 100U) / memory_size_lfs;

	const char *color = usage_percent_lfs > 95 ? "\x1B[31m" : (usage_percent_lfs > 80 ? "\x1B[33m" : "");

	shell_fprintf(shell, SHELL_NORMAL, "PLC Internal Filesystem /lfs:\n");
	shell_fprintf(shell, SHELL_NORMAL, "%sFree: %d KB / %d KB (%u%%)\n\x1B[0m", color, memory_free_lfs >> 10, memory_size_lfs >> 10, usage_percent_lfs);
}

static int cmd_free(const struct shell *shell, size_t argc, char **argv)
{
	print_heap_usage(shell);
	shell_print(shell, "----------------------------------");
	print_filesystem_usage(shell);
	shell_print(shell, "----------------------------------");
	shell_print(shell, "Thread Stack Usage:");
	k_thread_foreach(print_thread_stack_usage, (void *)shell);

	return 0;
}

static int cmd_check_path(const struct shell *shell, size_t argc, char **argv)
{
	check_and_create_path(argv[1]);
	return 0;
}

static int cmd_check_file(const struct shell *shell, size_t argc, char **argv)
{
	check_and_create_file(argv[1]);
	return 0;
}

static int cmd_show_ip(const struct shell *shell, size_t argc, char **argv)
{
    struct net_if *iface = net_if_get_default();
    if (!iface) {
        shell_print(shell, "No default network interface found.");
        return -ENOEXEC;
    }

    struct net_if_config *config = net_if_get_config(iface);
    if (!config || !config->ip.ipv4) {
        shell_print(shell, "IPv4 is not configured.");
        return -ENOEXEC;
    }

    struct net_if_addr *unicast = &config->ip.ipv4->unicast[0];
    if (unicast->addr_state != NET_ADDR_PREFERRED) {
        shell_print(shell, "IPv4 Network Configuration: No preferred IPv4 address assigned to the default interface.");
        return 0;
    }

    char ip_addr[NET_IPV4_ADDR_LEN];
    char netmask[NET_IPV4_ADDR_LEN];
    char gw[NET_IPV4_ADDR_LEN];

    net_addr_ntop(AF_INET, &unicast->address.in_addr, ip_addr, sizeof(ip_addr));
    net_addr_ntop(AF_INET, &config->ip.ipv4->netmask, netmask, sizeof(netmask));
    net_addr_ntop(AF_INET, &config->ip.ipv4->gw, gw, sizeof(gw));

    const char *addr_type = "Unknown";
    switch (unicast->addr_type) {
        case NET_ADDR_AUTOCONF:
            addr_type = "Autoconf";
            break;
        case NET_ADDR_DHCP:
            addr_type = "DHCP";
            break;
        case NET_ADDR_MANUAL:
            addr_type = "Manual";
            break;
        case NET_ADDR_OVERRIDABLE:
            addr_type = "Overridable";
            break;
    }

    shell_print(shell, "IPv4 Network Configuration:");
    shell_print(shell, "  IP Address:    %s", ip_addr);
    shell_print(shell, "  Netmask:       %s", netmask);
    shell_print(shell, "  Gateway:       %s", gw);
    shell_print(shell, "  Address Type:  %s", addr_type);

    return 0;
}

SHELL_CMD_ARG_REGISTER(ls, NULL, "List directory", cmd_ls, 1, 1);
SHELL_CMD_ARG_REGISTER(format, NULL, "format lfs:/", cmd_format_lfs, 1, 0);
SHELL_CMD_ARG_REGISTER(pwd, NULL, "Show actual directory", cmd_pwd, 1, 0);
SHELL_CMD_ARG_REGISTER(touch, NULL, "Create a file", cmd_touch, 2, 0);
SHELL_CMD_ARG_REGISTER(rm, NULL, "Delete a file", cmd_rm, 2, 0);
SHELL_CMD_ARG_REGISTER(cat, NULL, "Show file content", cmd_cat, 2, 0);
SHELL_CMD_ARG_REGISTER(cd, NULL, "Change directory", cmd_cd, 2, 0);
SHELL_CMD_ARG_REGISTER(mount, NULL, "Mount filesystem", cmd_mount, 1, 0);
SHELL_CMD_ARG_REGISTER(unmount, NULL, "Unmount filesystem", cmd_unmount, 1, 0);
SHELL_CMD_ARG_REGISTER(check_path, NULL, "check path", cmd_check_path, 2, 0);
SHELL_CMD_ARG_REGISTER(check_file, NULL, "check file", cmd_check_file, 2, 0);
SHELL_CMD_ARG_REGISTER(settings, NULL, "Show all settings or set a specific setting.", cmd_show_or_set_settings, 1, 0);
SHELL_CMD_ARG_REGISTER(mkdir, NULL, "Create directory", cmd_mkdir, 1, 0);
SHELL_CMD_ARG_REGISTER(rmdir, NULL, "Remove directory", cmd_rmdir, 1, 0);
SHELL_CMD_ARG_REGISTER(mv, NULL, "Move (rename) files", cmd_mv, 1, 0);
SHELL_CMD_ARG_REGISTER(run, NULL, "Execute a shell script file", cmd_run, 1, 0);
SHELL_CMD_ARG_REGISTER(free, NULL, "Show memory usage", cmd_free, 1, 0);
SHELL_CMD_ARG_REGISTER(show_ip, NULL, "Show the IPv4 address of the default network interface.", cmd_show_ip, 1, 0);
#endif