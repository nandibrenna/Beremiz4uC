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

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/logging/log_output_custom.h>
#include <app_version.h>

#include "config.h"
#include "plc_time_rtc.h"
#include "plc_filesys.h"
#include "plc_settings.h"
#include "plc_network.h"

void pre_app_setup(void) {
    printk("** System initialisation Beremit4uC v%s **\n", APP_VERSION_EXTENDED_STRING);
	
	printk("Get SystemTime from RTC\n");
    set_system_time_from_rtc();
	log_custom_timestamp_set(log_ts_print);

	printk("Setup Filesystem\n");
	init_plc_filesys();
	
	printk("Check SystemFiles\n");
	check_and_create_path(FILESYSTEM_PATH PLC_PATH);
	check_and_create_path(FILESYSTEM_PATH TMP_PATH);
	check_and_create_path(FILESYSTEM_PATH WEB_PATH);
	check_and_create_path(FILESYSTEM_PATH LOG_PATH);
	check_and_create_file(CONFIG_SETTINGS_FILE_PATH);

	printk("Setup Settings system\n");
	plc_settings_init();
	plc_settings_load();
	
	printk("setup Network\n");
	network_init();
}

SYS_INIT(pre_app_setup, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);