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
LOG_MODULE_REGISTER(plc_filesys, LOG_LEVEL_DBG);

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ff.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>

// SD/MMC card detect pin configuration
#define CD_GPIO_NODE DT_PHANDLE(DT_NODELABEL(sdmmc1), cd_gpios)
#define CD_GPIO_PIN DT_PHA(DT_NODELABEL(sdmmc1), cd_gpios, pin)
#define CD_GPIO_FLAGS GPIO_INPUT | DT_PHA(DT_NODELABEL(sdmmc1), cd_gpios, flags)

// File system type and partition configuration for MKFS
#define MKFS_FS_TYPE FS_LITTLEFS
#define MKFS_DEV_ID FIXED_PARTITION_ID(storage_partition)
#define MKFS_FLAGS 0

// Disk drive configuration
#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"

#define LFS_NAME "lfs"
#define LFS_MOUNT_PT "/" LFS_NAME ":"

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

static struct gpio_callback sd_card_detect_cb;

char *plc_strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *new_str = malloc(len);
	if (new_str == NULL)
	{
		return NULL;
	}
	memcpy(new_str, s, len);
	return new_str;
}

static int create_directories(const char *path)
{
	char tmp[256];
	char *p = NULL;
	struct stat st;
	int directoryCreated = 0;

	snprintf(tmp, sizeof(tmp), "%s", path);

	for (p = tmp + 1; *p; p++)
	{
		if (*p == '/')
		{
			*p = 0;
			if (stat(tmp, &st) == -1)
			{
				if (mkdir(tmp, S_IRWXU) == -1)
				{
					if (errno == EEXIST)
					{
						directoryCreated = -1;
					}
					else
					{
						return -errno; 
					}
				}
				else
				{
					directoryCreated = 1;
				}
			}
			else if (!S_ISDIR(st.st_mode))
			{
				return -ENOTDIR;
			}
			*p = '/';
		}
	}

	if (directoryCreated == 0 && stat(tmp, &st) == -1)
	{
		if (mkdir(tmp, S_IRWXU) == -1)
		{
			if (errno == EEXIST)
			{
				return 1;
			}
			else
			{
				return -errno;
			}
		}
		else
		{
			return 0; 
		}
	}
	else if (!S_ISDIR(st.st_mode))
	{
		return -ENOTDIR;
	}

	return directoryCreated == 1 ? 0 : 1;
}

int check_and_create_path(const char *path)
{
	if (path == NULL || *path == '\0')
	{
		return -EINVAL;
	}

	int result = create_directories(path);
	if (result < 0)
	{
		printk("Error creating path: %s\n", path);
		return result;
	}
	else if (result == 0)
	{
		printk("Path created: %s\n", path);
		return result;
	}
	else
	{
		return result;
	}
}

int check_and_create_file(const char *filepath) {
    struct stat st;
    if (stat(filepath, &st) == 0) {
        if (S_ISREG(st.st_mode)) {
            return 0; 
        }
        return -EEXIST;
    }
	
    char *dir_path = plc_strdup(filepath);
    if (dir_path == NULL) {
        return -ENOMEM;
    }
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        int dir_result = create_directories(dir_path);
        if (dir_result < 0) {
            free(dir_path);
			printk("Error creating directories for file: %s\n", filepath);
            return dir_result; 
        }
    }
    free(dir_path);

    FILE *file = fopen(filepath, "w");
    if (!file) {
		printk("Error creating file: %s\n", filepath);
        return -errno;
    }
	printk("File created: %s\n", filepath);
    fclose(file);
    return 1;
}

/****************************************************************************************************************************************
 * @brief   Unmount the SD card file system.
 * 			This function unmounts the SD card file system by using the global mount point configuration.
 * 			It logs the result of the unmount operation.
 ****************************************************************************************************************************************/
void unmount_sd_fatfs(void)
{
	static const char *disk_mount_pt = DISK_MOUNT_PT;
	mp.mnt_point = disk_mount_pt;

	int res = fs_unmount(&mp);
	if (res == FR_OK)
	{
		printk("Unmounted SD filesystem successfully.\n");
	}
	else
	{
		printk("Error unmounting SD filesystem: %d\n", res);
	}
}

/****************************************************************************************************************************************
 * @brief   Mount the SD card file system.
 * 			This function initializes the disk access for the SD card and mounts the file system.
 * 			It logs the free and total memory of the SD card if the mount operation is successful.
 ****************************************************************************************************************************************/
void mount_sd_fatfs(void)
{
	struct fs_statvfs sbuf;

	static const char *disk_mount_pt = DISK_MOUNT_PT;
	mp.mnt_point = disk_mount_pt;

	static const char *disk_pdrv = DISK_DRIVE_NAME;
	if (disk_access_init(disk_pdrv) != 0)
	{
		printk("SD/MMC Storage init ERROR!\n");
		return;
	}

	int res = fs_mount(&mp);
	if (res == FR_OK)
	{
		fs_statvfs("/SD:", &sbuf);
		uint32_t memory_size_sd = sbuf.f_frsize * sbuf.f_blocks;
		uint32_t memory_free_sd = sbuf.f_frsize * sbuf.f_bfree;
		printk("PLC External Filesystem /SD: free %d MB/%d MB\n", memory_free_sd >> 20, memory_size_sd >> 20);
	}
	else
	{
		printk("Error mounting disk: %d\n", res);
	}
}

/****************************************************************************************************************************************
 * @brief   	Callback function for SD card detection.
 * 				This function is triggered by GPIO interrupt when an SD card is inserted or removed.
 * 				It performs a debounce delay and then checks the card's presence, attempting to mount or unmount the filesystem accordingly.
 * @param dev 	Device structure for the GPIO device.
 * @param cb 	GPIO callback structure.
 * @param pins 	Pin mask that triggered the callback.
 ****************************************************************************************************************************************/
void sd_card_detect_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	k_msleep(1000); // Debounce delay
	int val = gpio_pin_get(dev, CD_GPIO_PIN);

	if (val == 1)
	{
		printk("SD card inserted. Attempting to mount...\n");
		k_msleep(500); // Additional delay before attempting to mount may be necessary
		mount_sd_fatfs();
	}
	else
	{
		printk("SD card removed.\n");
		unmount_sd_fatfs();
	}
}

/****************************************************************************************************************************************
 * @brief   Initializes the PLC file system.
 *
 * This function formats the LittleFS partition if configured to do so. It then checks for an SD card and mounts the filesystem if present.
 * The function also sets up GPIO pin for SD card detection and logs the internal filesystem memory usage.
 ****************************************************************************************************************************************/
void init_plc_filesys(void)
{
	if (!IS_ENABLED(CONFIG_APP_STORAGE_FLASH_LITTLEFS))
	{
		printk("LittleFS not enabled in Kconfig. Unmounting /lfs: now \n");
		static const char *lfs_mount_pt = LFS_MOUNT_PT;
		mp.mnt_point = lfs_mount_pt;

		int res = fs_unmount(&mp);
		if (res == FR_OK)
		{
			printk("Unmounted lfs filesystem successfully.\n");
		}
		else
		{
			printk("Error unmounting lfs filesystem: %d\n", res);
		}
		return;
	}

	if (IS_ENABLED(CONFIG_APP_WIPE_STORAGE))
	{
		printk("Formatting LittleFS partition...\n");
		int ret = fs_mkfs(FS_LITTLEFS, (uintptr_t)MKFS_DEV_ID, NULL, MKFS_FLAGS);
		if (ret < 0)
		{
			printk("Error formatting LittleFS: %d\n", ret);
			return;
		}

		printk("LittleFS partition formatted.\n");
	}

	// LittleFS is automounted from devicetree
	struct fs_statvfs sbuf;
	fs_statvfs("/lfs:", &sbuf);
	uint32_t memory_size_lfs = sbuf.f_frsize * sbuf.f_blocks;
	uint32_t memory_free_lfs = sbuf.f_frsize * sbuf.f_bfree;
	printk("PLC Internal Filesystem /lfs: free %d KB/%d KB\n", memory_free_lfs >> 10, memory_size_lfs >> 10);

	const struct device *cd_gpio_dev = DEVICE_DT_GET(CD_GPIO_NODE);
	if (!device_is_ready(cd_gpio_dev))
	{
		printk("CD GPIO device not ready\n");
	}
	else
	{
		gpio_pin_configure(cd_gpio_dev, CD_GPIO_PIN, CD_GPIO_FLAGS);
		// Setup GPIO interrupt for SD card detection
		// gpio_pin_interrupt_configure(cd_gpio_dev, CD_GPIO_PIN, GPIO_INT_EDGE_BOTH);
		// gpio_init_callback(&sd_card_detect_cb, sd_card_detect_callback, BIT(CD_GPIO_PIN));
		// gpio_add_callback(cd_gpio_dev, &sd_card_detect_cb);

		// Check initial state of SD card and attempt to mount if present
		int val = gpio_pin_get(cd_gpio_dev, CD_GPIO_PIN);
		if (val == 1)
		{
			printk("SD card detected at startup. Attempting to mount...\n");
			mount_sd_fatfs();
		}
		else
		{
			printk("No SD card inserted at startup.\n");
		}
	}
}