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
#include <string.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

#include "plc_util.h"
#include "plc_loader.h"
#include "plc_task.h"
#include "plc_settings.h"


/****************************************************************************************************************************************
 * @brief               get_PLCStatus
 *                      Retrieves the current status of the PLC, such as
 *						whether it's running, stopped, or in an error state. This status is used to
 *						inform the host system of the PLC's operational state.
 * @return              PLCstatus_enum - The current status of the PLC.
 ****************************************************************************************************************************************/
PLCstatus_enum get_PLCStatus(void)
{
	uint32_t plc_state = 0;
	uint32_t plc_loader_state = 0;
	PLCstatus_enum PLCstatus = Broken;

	plc_state = plc_get_state();
	plc_loader_state = plc_get_loader_state();

	if (plc_state == 0)
		PLCstatus = Stopped;
	else if (plc_state == 1)
		PLCstatus = Started;

	if (plc_loader_state == 0)
		PLCstatus = Empty;

	return PLCstatus;
}
/****************************************************************************************************************************************
 * @brief      Get the preferred IPv4 address of the default network interface.
 * @param      None
 * @return     const char* - IPv4 address as string or an error message.
 ****************************************************************************************************************************************/
const char *get_ip_address(void)
{
	static char buf[NET_IPV4_ADDR_LEN]; // Buffer for IPv4 address string
	struct net_if *iface = net_if_get_default(); // Get default network interface

	if (!iface)
	{
		return "No interface"; // No default interface found
	}

    struct net_if_config *config = net_if_get_config(iface); // Get interface configuration
    if (!config || !config->ip.ipv4) {
        return "IPv4 is not configured."; // IPv4 not configured on interface
    }

    struct net_if_addr *unicast = &config->ip.ipv4->unicast[0]; // Get first unicast address
    if (unicast->addr_state != NET_ADDR_PREFERRED) {
        return "No preferred IPv4 address assigned"; // Check if address is preferred
    }

	if(net_addr_ntop(AF_INET, &unicast->address.in_addr, buf, sizeof(buf)) == NULL)
	{
		return "Invalid address"; // Convert address to string representation
	}
	else
	{
		return buf; // Return IPv4 address string
	}
}

/****************************************************************************************************************************************
 * @brief      Get the method used for IP address assignment of the default network interface.
 * @param      None
 * @return     const char* - IP assignment method as string or placeholder if unavailable.
 ****************************************************************************************************************************************/
const char *get_ip_assignment_method(void)
{
	struct net_if *iface = net_if_get_default(); // Get default network interface

	if (!iface)
	{
		return "---"; // Interface not available
	}

    struct net_if_config *config = net_if_get_config(iface); // Get interface configuration
    if (!config || !config->ip.ipv4) {
        return "---"; // IPv4 not configured on interface
    }

    struct net_if_addr *unicast = &config->ip.ipv4->unicast[0]; // Get first unicast address
    const char *addr_type = "Unknown"; // Default address type
    switch (unicast->addr_type) // Determine address assignment method
	{
        case NET_ADDR_AUTOCONF:
            addr_type = "Autoconf"; // Autoconfigured address
            break;
        case NET_ADDR_DHCP:
            addr_type = "DHCP"; // DHCP assigned address
            break;
        case NET_ADDR_MANUAL:
            addr_type = "Static"; // Manually configured address
            break;
        case NET_ADDR_OVERRIDABLE:
            addr_type = "Fallback"; // Fallback or secondary address
            break;
    }
	return addr_type; // Return address assignment method
}

/****************************************************************************************************************************************
 * @brief      
 * @param      None
 * @return     const char* - 
 ****************************************************************************************************************************************/
const char *plc_loader_get_modulename(void)
{
	if (plc_initialized > 0)
	{
		return udynlink_get_module_name(&mod);
	}
	else
	{
		return "no module loaded";
	}
}

/****************************************************************************************************************************************
 * @brief      
 * @param      None
 * @return     const char* - 
 ****************************************************************************************************************************************/
const char *plc_loader_get_autostart_source(void)
{
	if (get_plc_autostart_setting())
	{
		if(get_plc_autostart_source_setting())
			return "FileSystem";
		else
			return "Flash";
	}
	else
	{
		return "no autostart";
	}
}

/****************************************************************************************************************************************
 * @brief      
 * @param      None
 * @return     const char* - 
 ****************************************************************************************************************************************/
const char *plc_task_get_cycle_time(void)
{
	static char buf[20];

	uint32_t cycle_time = (uint32_t) (common_cycle_time / NSEC_PER_MSEC);
	snprintf(buf, sizeof(buf),"%ums tick=%u", cycle_time, __tick);
	return buf;
}