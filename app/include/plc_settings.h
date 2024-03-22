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

#ifndef PLC_SETTINGS_H
#define PLC_SETTINGS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialisierungsfunktion
void plc_settings_init(void);
void plc_settings_load(void);
void plc_settings_save(void);

// Getter und Setter für die Einstellungen
bool get_mount_sd_card_setting(void);
void set_mount_sd_card_setting(bool value);

bool get_plc_autostart_setting(void);
void set_plc_autostart_setting(bool value);

bool get_plc_autostart_source_setting(void) ;
void set_plc_autostart_setting(bool value);

char* get_hostname_setting(void);
void set_hostname_setting(const char* value);

bool get_dhcp_active_setting(void);
void set_dhcp_active_setting(bool value);

int get_dhcp_timeout_sec_setting(void);
void set_dhcp_timeout_sec_setting(int value);

bool get_fallback_ip_setting(void);
void set_fallback_ip_setting(bool value);

char* get_ip_address_setting(void);
void set_ip_address_setting(const char* value);

char* get_netmask_setting(void);
void set_netmask_setting(const char* value);

char* get_gateway_setting(void);
void set_gateway_setting(const char* value);

char* get_mac_address_setting(void);
void set_mac_address_setting(const char* value);

bool get_ntp_server_setting(void);
void set_ntp_server_setting(bool value);

char* get_ntp_address_setting(void);
void set_ntp_address_setting(const char* value);

int get_ntp_timeout_setting(void);
void set_ntp_timeout_setting(int value);

int get_rpc_server_port_setting(void);
void set_rpc_server_port_setting(int value);

#ifdef __cplusplus
}
#endif

#endif // PLC_SETTINGS_H