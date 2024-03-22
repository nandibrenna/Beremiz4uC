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
LOG_MODULE_REGISTER(plc_settings, LOG_LEVEL_INF);

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include "plc_settings.h"

bool mount_sd_card = true;
bool plc_autostart = true;
bool plc_autostart_source = false;
char hostname[64] = "default_hostname";
bool dhcp_active = true;
int dhcp_timeout_sec = 0;
bool fallback_ip = false;
char ip_address[17] = "0.0.0.0";
char netmask[17] = "0.0.0.0";
char gateway[17] = "0.0.0.0";
char mac_address[18] = "00:00:00:00:00:00";
bool ntp_server = false;
char ntp_address[64] = "pool.ntp.org";
int ntp_timeout = 0;
int rpc_server_port = 0;

/****************************************************************************************************************************************
 * @brief      		Sets the new network settings based on the input parameters.
 *
 * @param name 		The name of the setting.
 * @param len  		The length of the setting's value.
 * @param read_cb 	Callback function for reading the setting values.
 * @param cb_arg 	Argument for the callback function.
 * @return      	0 on success.
 ****************************************************************************************************************************************/
static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (settings_name_steq(name, "mount_sd_card", NULL))
	{
		read_cb(cb_arg, &mount_sd_card, sizeof(mount_sd_card));
	}
	else if (settings_name_steq(name, "plc_autostart", NULL))
	{
		read_cb(cb_arg, &plc_autostart, sizeof(plc_autostart));
	}
	else if (settings_name_steq(name, "plc_autostart_source", NULL))
	{
		read_cb(cb_arg, &plc_autostart_source, sizeof(plc_autostart_source));
	}
	else if (settings_name_steq(name, "hostname", NULL))
	{
		read_cb(cb_arg, &hostname, sizeof(hostname));
	}
	else if (settings_name_steq(name, "dhcp_active", NULL))
	{
		read_cb(cb_arg, &dhcp_active, sizeof(dhcp_active));
	}
	else if (settings_name_steq(name, "dhcp_timeout_sec", NULL))
	{
		read_cb(cb_arg, &dhcp_timeout_sec, sizeof(dhcp_timeout_sec));
	}
	else if (settings_name_steq(name, "fallback_ip", NULL))
	{
		read_cb(cb_arg, &fallback_ip, sizeof(fallback_ip));
	}
	else if (settings_name_steq(name, "ip_address", NULL))
	{
		read_cb(cb_arg, &ip_address, sizeof(ip_address));
	}
	else if (settings_name_steq(name, "netmask", NULL))
	{
		read_cb(cb_arg, &netmask, sizeof(netmask));
	}
	else if (settings_name_steq(name, "gateway", NULL))
	{
		read_cb(cb_arg, &gateway, sizeof(gateway));
	}
	else if (settings_name_steq(name, "mac_address", NULL))
	{
		read_cb(cb_arg, &mac_address, sizeof(mac_address));
	}
	else if (settings_name_steq(name, "ntp_server", NULL))
	{
		read_cb(cb_arg, &ntp_server, sizeof(ntp_server));
	}
	else if (settings_name_steq(name, "ntp_address", NULL))
	{
		read_cb(cb_arg, &ntp_address, sizeof(ntp_address));
	}
	else if (settings_name_steq(name, "ntp_timeout", NULL))
	{
		read_cb(cb_arg, &ntp_timeout, sizeof(ntp_timeout));
	}
	else if (settings_name_steq(name, "rpc_server_port", NULL))
	{
		read_cb(cb_arg, &rpc_server_port, sizeof(rpc_server_port));
	}
	return 0;
}

/****************************************************************************************************************************************
 * @brief      		Exports settings for persistence.
 *
 * @param cb   		Callback function for writing setting names and values.
 * @return     		0 on success.
 ****************************************************************************************************************************************/
static int settings_export(int (*cb)(const char *name, const void *val, size_t val_len))
{
	cb("plc/mount_sd_card", &mount_sd_card, sizeof(mount_sd_card));
	cb("plc/start_plc_at_boot", &plc_autostart, sizeof(plc_autostart));
	cb("network/hostname", &hostname, sizeof(hostname));
	cb("network/dhcp_active", &dhcp_active, sizeof(dhcp_active));
	cb("network/dhcp_timeout_sec", &dhcp_timeout_sec, sizeof(dhcp_timeout_sec));
	cb("network/fallback_ip", &fallback_ip, sizeof(fallback_ip));
	cb("network/ip_address", &ip_address, sizeof(ip_address));
	cb("network/netmask", &netmask, sizeof(netmask));
	cb("network/gateway", &gateway, sizeof(gateway));
	cb("network/mac_address", &mac_address, sizeof(mac_address));
	cb("network/ntp_server", &ntp_server, sizeof(ntp_server));
	cb("network/ntp_address", &ntp_address, sizeof(ntp_address));
	cb("network/ntp_timeout", &ntp_timeout, sizeof(ntp_timeout));
	cb("network/rpc_server_port", &rpc_server_port, sizeof(rpc_server_port));
	return 0;
}

static struct settings_handler cfg = {
	.name = "plc",
	.h_set = settings_set,
	.h_export = settings_export,
};

void plc_settings_init(void)
{
	settings_subsys_init();
	settings_register(&cfg);
	plc_settings_load();
	printk("Settings system initialized and settings loaded\n");
}

void plc_settings_load(void) { settings_load(); }

void plc_settings_save(void) { settings_save(); }

/****************************************************************************************************************************************
 * @brief  Returns whether the SD card should be mounted.
 * @return true if the SD card should be mounted, otherwise false.
 ****************************************************************************************************************************************/
bool get_mount_sd_card_setting(void) { return mount_sd_card; }

/****************************************************************************************************************************************
 * @brief  Sets whether the SD card should be mounted.
 * @param value true to mount the SD card, otherwise false.
 ****************************************************************************************************************************************/
void set_mount_sd_card_setting(bool value)
{
	mount_sd_card = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the current autostart setting.
 * @return true if the PLC should start on boot, otherwise false.
 ****************************************************************************************************************************************/
bool get_plc_autostart_setting(void) { return plc_autostart; }

/****************************************************************************************************************************************
 * @brief  Resets the current autostart setting.
 * @param value true to start the PLC on boot, otherwise false.
 ****************************************************************************************************************************************/
void set_plc_autostart_setting(bool value)
{
	plc_autostart = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the current autostart source setting.
 * @return true to start from flash, false to start from file.
 ****************************************************************************************************************************************/
bool get_plc_autostart_source_setting(void) { return plc_autostart_source; }

/****************************************************************************************************************************************
 * @brief  Sets the current autostart source setting.
 * @param value true to start from flash, false to start from file.
 ****************************************************************************************************************************************/
void set_plc_autostart_source_setting(bool value)
{
	plc_autostart_source = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the current hostname.
 * @return The current hostname as a string.
 ****************************************************************************************************************************************/
char *get_hostname_setting(void) { return hostname; }

/****************************************************************************************************************************************
 * @brief  Sets the hostname.
 * @param value The new hostname as a string.
 ****************************************************************************************************************************************/
void set_hostname_setting(const char *value)
{
	strncpy(hostname, value, sizeof(hostname) - 1);
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns whether DHCP is active.
 * @return true if DHCP is active, otherwise false.
 ****************************************************************************************************************************************/
bool get_dhcp_active_setting(void) { return dhcp_active; }

/****************************************************************************************************************************************
 * @brief  Sets whether DHCP should be active.
 * @param value true to activate DHCP, otherwise false.
 ****************************************************************************************************************************************/
void set_dhcp_active_setting(bool value)
{
	dhcp_active = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the DHCP timeout in seconds.
 * @return DHCP timeout as an integer.
 ****************************************************************************************************************************************/
int get_dhcp_timeout_sec_setting(void) { return dhcp_timeout_sec; }

/****************************************************************************************************************************************
 * @brief  Sets the DHCP timeout in seconds.
 * @param value The timeout value in seconds.
 ****************************************************************************************************************************************/
void set_dhcp_timeout_sec_setting(int value)
{
	dhcp_timeout_sec = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns whether the fallback IP is active.
 * @return true if the fallback IP is active, otherwise false.
 ****************************************************************************************************************************************/
bool get_fallback_ip_setting(void) { return fallback_ip; }

/****************************************************************************************************************************************
 * @brief  Sets whether the fallback IP should be active.
 * @param value true to activate the fallback IP, otherwise false.
 ****************************************************************************************************************************************/
void set_fallback_ip_setting(bool value)
{
	fallback_ip = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the IP address.
 * @return The current IP address as a string.
 ****************************************************************************************************************************************/
char *get_ip_address_setting(void) { return ip_address; }

/****************************************************************************************************************************************
 * @brief  Sets the IP address.
 * @param value The new IP address as a string.
 ****************************************************************************************************************************************/
void set_ip_address_setting(const char *value)
{
	strncpy(ip_address, value, sizeof(ip_address) - 1);
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the netmask.
 * @return The current netmask as a string.
 ****************************************************************************************************************************************/
char *get_netmask_setting(void) { return netmask; }

/****************************************************************************************************************************************
 * @brief  Sets the netmask.
 * @param value The new netmask as a string.
 ****************************************************************************************************************************************/
void set_netmask_setting(const char *value)
{
	strncpy(netmask, value, sizeof(netmask) - 1);
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the gateway.
 * @return The current gateway as a string.
 ****************************************************************************************************************************************/
char *get_gateway_setting(void) { return gateway; }

/****************************************************************************************************************************************
 * @brief  Sets the gateway.
 * @param value The new gateway as a string.
 ****************************************************************************************************************************************/
void set_gateway_setting(const char *value)
{
	strncpy(gateway, value, sizeof(gateway) - 1);
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief Returns the MAC address.
 * @return The MAC address as a String.
 ****************************************************************************************************************************************/
char *get_mac_address_setting(void) { return mac_address; }

/****************************************************************************************************************************************
 * @brief  Sets the MAC address.
 * @param  value The new MAC address as a string.
 ****************************************************************************************************************************************/
void set_mac_address_setting(const char *value)
{
	strncpy(mac_address, value, sizeof(mac_address) - 1);
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns whether the NTP server is active.
 * @return true if the NTP server is active, otherwise false.
 ****************************************************************************************************************************************/
bool get_ntp_server_setting(void) { return ntp_server; }

/****************************************************************************************************************************************
 * @brief  Sets whether the NTP server should be active.
 * @param  value true to activate the NTP server, otherwise false.
 ****************************************************************************************************************************************/
void set_ntp_server_setting(bool value)
{
	ntp_server = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the NTP server address.
 * @return The current NTP server address as a string.
 ****************************************************************************************************************************************/
char *get_ntp_address_setting(void) { return ntp_address; }

/****************************************************************************************************************************************
 * @brief  Sets the NTP server address.
 * @param  value The new NTP server address as a string.
 ****************************************************************************************************************************************/
void set_ntp_address_setting(const char *value)
{
	strncpy(ntp_address, value, sizeof(ntp_address) - 1);
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the NTP timeout.
 * @return The current NTP timeout as an integer.
 ****************************************************************************************************************************************/
int get_ntp_timeout_setting(void) { return ntp_timeout; }

/****************************************************************************************************************************************
 * @brief  Sets the NTP timeout.
 * @param  value The new NTP timeout in seconds.
 ****************************************************************************************************************************************/
void set_ntp_timeout_setting(int value)
{
	ntp_timeout = value;
	plc_settings_save();
}

/****************************************************************************************************************************************
 * @brief  Returns the RPC server port.
 * @return The current RPC server port as an integer.
 ****************************************************************************************************************************************/
int get_rpc_server_port_setting(void) { return rpc_server_port; }

/****************************************************************************************************************************************
 * @brief  Sets the RPC server port.
 * @param  value The new port for the RPC server.
 ****************************************************************************************************************************************/
void set_rpc_server_port_setting(int value)
{
	rpc_server_port = value;
	plc_settings_save();
}
