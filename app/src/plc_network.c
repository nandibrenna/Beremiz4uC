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
LOG_MODULE_REGISTER(plc_network_init, LOG_LEVEL_INF);

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_config.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/sntp.h>
#include <zephyr/posix/time.h>

#include "plc_time_rtc.h"


#define LOOP_DIVIDER 10
#define STM_RTC DT_INST(0, st_stm32_rtc)
extern int net_init_clock_via_sntp(void);

static K_SEM_DEFINE(waiter, 0, 1);
static K_SEM_DEFINE(counter, 0, UINT_MAX);
static atomic_t services_flags;
static struct net_mgmt_event_callback mgmt_iface_cb;


/****************************************************************************************************************************************
 * @brief      	Synchronizes the system clock with an SNTP server.
 * @param      	None
 * @return     	int - Result of the synchronization attempt: 0 on success, negative error code on failure.
 ****************************************************************************************************************************************/
int sync_clock_via_sntp(void)
{
	struct sntp_time ts; // SNTP timestamp
	struct timespec tspec; // System timespec structure for setting the clock
	int res = sntp_simple(CONFIG_NET_CONFIG_SNTP_INIT_SERVER, CONFIG_NET_CONFIG_SNTP_INIT_TIMEOUT, &ts); // Attempt to get time from SNTP

	if (res < 0) // Check for SNTP failure
	{
		LOG_ERR("Cannot set time using SNTP"); // Log failure
		return res; // Return error code
	}

	tspec.tv_sec = ts.seconds; // Set seconds for timespec
	tspec.tv_nsec = ((uint64_t)ts.fraction * (1000 * 1000 * 1000)) >> 32; // Convert SNTP fraction to nanoseconds
	res = clock_settime(CLOCK_REALTIME, &tspec); // Set the system clock

	return res;
}

/****************************************************************************************************************************************
 * @brief	Notifies that specific services are ready by setting flags and releasing a semaphore.
 * @param	flags Integer flags indicating which services are ready.
 ****************************************************************************************************************************************/
static inline void services_notify_ready(int flags)
{
	atomic_or(&services_flags, flags); // Set service ready flags atomically
	k_sem_give(&waiter); // Release semaphore to signal readiness
}

/****************************************************************************************************************************************
 * @brief	Checks if specified services are ready based on given flags.
 * @param	flags The flags representing services to check for readiness.
 * @return	bool True if all specified services are ready, false otherwise.
 ****************************************************************************************************************************************/
static inline bool services_are_ready(int flags) 
{
	return (atomic_get(&services_flags) & flags) == flags; // Check if all specified services are ready
}


#if defined(CONFIG_NET_DHCPV4)
static struct net_mgmt_event_callback mgmt4_cb;
/****************************************************************************************************************************************
 * @brief	Handles IPv4 address addition events, logs DHCP info if DHCP-assigned IP is obtained.
 * 			This function is registered as a callback to listen for NET_EVENT_IPV4_ADDR_ADD events. It checks for DHCP-assigned
 * 			IPv4 addresses and logs relevant information such as IP address, lease time, subnet, and router.
 * @param	cb Pointer to the network management event callback structure.
 * @param	mgmt_event The network management event triggering this callback.
 * @param	iface Pointer to the network interface that received the IPv4 address.
 ****************************************************************************************************************************************/
static void ipv4_addr_add_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	char hr_addr[NET_IPV4_ADDR_LEN];

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD)
	{
		return;
	}

	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++)
	{
		struct net_if_addr *if_addr = &iface->config.ip.ipv4->unicast[i];

		if (if_addr->addr_type != NET_ADDR_DHCP || !if_addr->is_used)
		{
			continue;
		}
		NET_INFO("DHCP-assigned IP is obtained");
		NET_INFO("IPv4 address: %s", net_addr_ntop(AF_INET, &if_addr->address.in_addr, hr_addr, sizeof(hr_addr)));
		NET_INFO("Lease time: %u seconds", iface->config.dhcpv4.lease_time);
		NET_INFO("Subnet: %s", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->netmask, hr_addr, sizeof(hr_addr)));
		NET_INFO("Router: %s", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, hr_addr, sizeof(hr_addr)));
		break;
	}

	services_notify_ready(NET_CONFIG_NEED_IPV4);
}

/****************************************************************************************************************************************
 * @brief	Initializes DHCPv4 client on the specified network interface.
 * 			This function sets up a network management event callback for IPv4 address addition and starts the DHCPv4 client.
 * @param	iface Pointer to the network interface on which to start the DHCPv4 client.
 ****************************************************************************************************************************************/
static void setup_dhcpv4(struct net_if *iface)
{
	NET_INFO("Running dhcpv4 client...");

	net_mgmt_init_event_callback(&mgmt4_cb, ipv4_addr_add_handler, NET_EVENT_IPV4_ADDR_ADD);
	net_mgmt_add_event_callback(&mgmt4_cb);
	net_dhcpv4_start(iface);
}
#else
/****************************************************************************************************************************************
 * @brief	Dummy setup function for when DHCPv4 is not configured.
 * @param	iface Unused parameter, present to match the function signature.
 ****************************************************************************************************************************************/
#define setup_dhcpv4(...)
#endif

/****************************************************************************************************************************************
 * @brief	Initializes IPv4 configuration for a network interface, setting up the address if specified.
 * 			This function attempts to set an IPv4 address for the network interface based on the configuration. It validates the 
 * 			address and sets it if valid. If the address is not configured or invalid, the function logs an error and returns early.
 * @param	iface Pointer to the network interface to configure.
 * @param	static_ip Indicates whether the IP should be treated as static.
 ****************************************************************************************************************************************/
static void setup_ipv4(struct net_if *iface, bool static_ip)
{
	char hr_addr[NET_IPV4_ADDR_LEN]; // Buffer for human-readable IP address
	struct in_addr addr;			 // Struct to hold the parsed IP address

	// Check if the configured IP address is empty
	if (sizeof(CONFIG_NET_CONFIG_MY_IPV4_ADDR) == 1)
	{
		// Configuration specifies an empty address, skip setting
		return;
	}

	// Attempt to parse the configured IP address
	if (net_addr_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_ADDR, &addr))
	{
		// Parsing failed, log error and return
		NET_ERR("Invalid address: %s", CONFIG_NET_CONFIG_MY_IPV4_ADDR);
		return;
	}

#if defined(CONFIG_NET_DHCPV4)
	net_if_ipv4_addr_add(iface, &addr, NET_ADDR_OVERRIDABLE, 0); // Add address as overridable (DHCP)
#else
	net_if_ipv4_addr_add(iface, &addr, NET_ADDR_MANUAL, 0); // Add address as manual (Static)
#endif

	// Set netmask if configured
	if (sizeof(CONFIG_NET_CONFIG_MY_IPV4_NETMASK) > 1)
	{
		if (net_addr_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_NETMASK, &addr))
		{
			NET_ERR("Invalid netmask: %s", CONFIG_NET_CONFIG_MY_IPV4_NETMASK);
		}
		else
		{
			net_if_ipv4_set_netmask(iface, &addr);
		}
	}

	// Set gateway if configured
	if (sizeof(CONFIG_NET_CONFIG_MY_IPV4_GW) > 1)
	{
		if (net_addr_pton(AF_INET, CONFIG_NET_CONFIG_MY_IPV4_GW, &addr))
		{
			NET_ERR("Invalid gateway: %s", CONFIG_NET_CONFIG_MY_IPV4_GW);
		}
		else
		{
			net_if_ipv4_set_gw(iface, &addr);
		}
	}

	// Log IP configuration
	if (static_ip)
	{
		NET_INFO("Set static IP address");
	}
	else
	{
		NET_INFO("Setting a static IP address until a DHCP-assigned IP is obtained.");
	}

	NET_INFO("IPv4 address: %s", net_addr_ntop(AF_INET, &addr, hr_addr, sizeof(hr_addr)));
	NET_INFO("Network Mask: %s", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->netmask, hr_addr, sizeof(hr_addr)));
	NET_INFO("Gateway:      %s", net_addr_ntop(AF_INET, &iface->config.ip.ipv4->gw, hr_addr, sizeof(hr_addr)));

	services_notify_ready(NET_CONFIG_NEED_IPV4); // Notify that IPv4 is configured
}

/****************************************************************************************************************************************
 * @brief	Handles network interface up events, signaling readiness and resetting counters.
 * 			When a network interface comes up, this callback logs the event, resets a semaphore counter, and signals another semaphore
 * 			to indicate the interface is ready. It's intended to coordinate activities dependent on the network interface being active.
 * @param	cb Pointer to the net_mgmt event callback structure, unused but required for the callback signature.
 * @param	mgmt_event The network management event type triggering this callback.
 * @param	iface Pointer to the network interface that has come up.
 ****************************************************************************************************************************************/
static void iface_up_handler(struct net_mgmt_event_callback *cb, uint32_t mgmt_event, struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_IF_UP) // Check if the event is for an interface coming up
	{
		// Log the interface coming up
		NET_INFO("Interface %d (%p) coming up", net_if_get_by_iface(iface), iface);

		k_sem_reset(&counter); // Reset semaphore counter
		k_sem_give(&waiter); // Signal semaphore to indicate readiness
	}
}

/****************************************************************************************************************************************
 * @brief	Checks if a network interface is up and ready. If not, sets up a callback to wait for it.
 * 			This function immediately returns true if the interface is already up. Otherwise, it initializes and adds a network
 * 			management event callback to wait for the NET_EVENT_IF_UP event, indicating the interface has come up, and returns false.
 * @param	iface Pointer to the network interface to check.
 * @return	bool True if the interface is already up, false if waiting for the interface to come up.
 ****************************************************************************************************************************************/
static bool check_interface(struct net_if *iface)
{
	if (net_if_is_up(iface)) // Check if interface is already up
	{
		k_sem_reset(&counter); // Reset semaphore counter
		k_sem_give(&waiter); // Signal semaphore to indicate readiness
		return true;
	}

	// Interface is not up, log and set up callback to wait for it
	NET_INFO("Waiting interface %d (%p) to be up...", net_if_get_by_iface(iface), iface);

	net_mgmt_init_event_callback(&mgmt_iface_cb, iface_up_handler, NET_EVENT_IF_UP); // Initialize callback for interface up event
	net_mgmt_add_event_callback(&mgmt_iface_cb); // Add event callback

	return false; // Indicate we're waiting for the interface to come up
}

/****************************************************************************************************************************************
 * @brief	Initializes the network interface and waits for it to become ready.
 * 			This function checks if the default network interface is available and not disabled for auto-start. It then waits for the
 * 			network interface to become ready. The function also handles DHCPv4 or static IP address setup based on configuration.
 * @param	flags Flags indicating specific network readiness conditions to wait for.
 * @param	timeout Time in milliseconds to wait for the network interface to become ready. A negative value waits indefinitely.
 * @return	int Returns 0 on successful network initialization, or a negative error code on failure.
 ****************************************************************************************************************************************/
int initialize_network(uint32_t flags, int32_t timeout)
{
	struct net_if *iface;
	const char *app_info = "Initializing network";
	NET_INFO("%s", app_info);
	iface = net_if_get_default();

	if (!iface) // Check if default interface is available
	{
		return -ENOENT;
	}

	if (net_if_flag_is_set(iface, NET_IF_NO_AUTO_START)) // Check if interface is set to no auto start
	{
		return -ENETDOWN;
	}

	int loop = timeout / LOOP_DIVIDER; // Calculate loop delay based on timeout
	int count = (timeout < 0) ? -1 : (timeout == 0) ? 0 : LOOP_DIVIDER;

	if (check_interface(iface) == false) // Check and wait for the interface to be up
	{
		k_sem_init(&counter, 1, K_SEM_MAX_LIMIT); // Initialize semaphore for synchronization

		while (count-- > 0) // Wait loop
		{
			if (!k_sem_count_get(&counter))
			{
				break; // Exit loop if semaphore count reaches zero
			}

			if (k_sem_take(&waiter, K_MSEC(loop))) // Wait for semaphore
			{
				if (!k_sem_count_get(&counter))
				{
					break; // Exit loop if semaphore count reaches zero
				}
			}
		}

#if defined(CONFIG_NET_NATIVE)
		net_mgmt_del_event_callback(&mgmt_iface_cb); // Clean up network management event callback
#endif
	}

	// Setup IPv4 and DHCP or static IP based on configuration
#if defined(CONFIG_NET_DHCPV4)
	setup_ipv4(iface, false); // Setup IPv4 with DHCP
	setup_dhcpv4(iface); // Start DHCP client
#else
	setup_ipv4(iface, true); // Setup IPv4 with static IP
#endif

	if (timeout > 0 && count < 0) // Check for timeout
	{
		NET_ERR("Timeout while waiting network %s", "interface");
		return -ENETDOWN;
	}

	// Wait for additional network services to be ready if necessary
	while (!services_are_ready(flags) && count-- > 0)
	{
		k_sem_take(&waiter, K_MSEC(loop));
	}

	if (count == -1 && timeout > 0) // Check for setup timeout
	{
		NET_ERR("Timeout while waiting network %s", "setup");
		return -ETIMEDOUT;
	}

	return 0; // Network initialization successful
}

/****************************************************************************************************************************************
 * @brief	Initializes the network and synchronizes the system clock with an SNTP server.
 * 			This function attempts to initialize the network with a specific configuration and timeout. If successful, it proceeds to
 * 			synchronize the system clock using SNTP. If either network initialization or clock synchronization fails, appropriate error
 * 			messages are logged. Additionally, if SNTP synchronization fails, it falls back to setting the system time from the RTC.
 ****************************************************************************************************************************************/
void network_init(void)
{
	int ret = initialize_network(NET_CONFIG_NEED_IPV4, 20000); // Attempt to initialize network with a 5-second timeout
	if (ret == 0)
	{
		LOG_INF("Network initialized");
		ret = sync_clock_via_sntp(); // Attempt to synchronize system clock via SNTP
		if (ret == 0)
		{
			LOG_INF("Clock synchronized via SNTP");
			set_rtc_from_system_time(); // Set the RTC to the newly synchronized system time
		}
		else
		{
			LOG_ERR("Clock synchronization failed: %s", strerror(-ret)); // Log SNTP sync failure
			set_system_time_from_rtc(); // Set the system time from the RTC as a fallback
		}
	}
	else
	{
		LOG_ERR("Network initialization failed: %s", strerror(-ret)); // Log network init failure
	}
}