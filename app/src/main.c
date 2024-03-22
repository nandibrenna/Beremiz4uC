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
LOG_MODULE_REGISTER(main_app, LOG_LEVEL_DBG);

#include <string.h>
#include <time.h>
#include <fcntl.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/usb/usb_device.h>

#include "plc_loader.h"
#include "plc_settings.h"
#include "plc_usb.h"


/****************************************************************************************************************************/
/*		devicetree																											*/
/****************************************************************************************************************************/
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)
const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(LED2_NODE, gpios);

#define SW1_NODE DT_ALIAS(sw1)
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios, {0});
static struct gpio_callback button1_cb_data;

#define SW2_NODE DT_ALIAS(sw2)
static const struct gpio_dt_spec button2 = GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios, {0});
static struct gpio_callback button2_cb_data;

#define WDT_MAX_WINDOW 2000U
#define WDT_MIN_WINDOW 0U
#define WDT_OPT WDT_OPT_PAUSE_HALTED_BY_DBG

#define SLEEP_TIME_MS 500

uint32_t allow_autostart = 1;

void button1_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins) { return; }

void button2_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins) { return; }

int main(void)
{
	uint32_t cause = 0;
	hwinfo_get_reset_cause(&cause);
	hwinfo_clear_reset_cause();
	if (cause == (RESET_WATCHDOG | RESET_PIN))
	{
		LOG_ERR("The watchdog has been triggered! The autostart of the PLC has been inhibited. To re-enable autostart, please reactivate it in the settings.");
		set_plc_autostart_setting(false);
	}

#if !(IS_ENABLED(CONFIG_QEMU))
	int err;
	int wdt_channel_id;
	const struct device *const wdt = DEVICE_DT_GET(DT_ALIAS(watchdog0));
	if (!device_is_ready(wdt))
	{
		LOG_ERR("%s: device not ready.", wdt->name);
		return 0;
	}

	struct wdt_timeout_cfg wdt_config = {
		.flags = WDT_FLAG_RESET_SOC,
		.window.min = WDT_MIN_WINDOW,
		.window.max = WDT_MAX_WINDOW,
	};

	wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
	if (wdt_channel_id < 0)
	{
		LOG_ERR("Watchdog install error channel=%d %d %s", wdt_channel_id, errno, strerror(errno));
		return 0;
	}

	err = 0; // wdt_setup(wdt, WDT_OPT);
	if (err < 0)
	{
		LOG_ERR("Watchdog setup error %d %d %s", err, errno, strerror(errno));
		return 0;
	}
#endif

	gpio_pin_configure_dt(&button1, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button1_cb_data, button1_pressed, BIT(button1.pin));
	gpio_add_callback(button1.port, &button1_cb_data);
	LOG_INF("Set up button1 at %s pin %d", button1.port->name, button1.pin);

	gpio_pin_configure_dt(&button2, GPIO_INPUT);
	gpio_pin_interrupt_configure_dt(&button2, GPIO_INT_EDGE_TO_ACTIVE);
	gpio_init_callback(&button2_cb_data, button2_pressed, BIT(button2.pin));
	gpio_add_callback(button2.port, &button2_cb_data);
	LOG_INF("Set up button2 at %s pin %d", button2.port->name, button2.pin);

	// LED configuration
	gpio_pin_configure_dt(&led0, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);

	// USB CDC UART
	if (IS_ENABLED(CONFIG_USB_DEVICE_STACK))
	{
		int rc = usb_enable(NULL);
		if (rc != 0 && rc != -EALREADY)
		{
			LOG_ERR("Failed to enable USB");
			return 0;
		}
	}

	// USB MASS STORAGE
	if (IS_ENABLED(CONFIG_USB_DEVICE_STACK_NEXT))
	{
		int ret = enable_usb_device_next();
		if (ret != 0)
		{
			LOG_ERR("Failed to enable USB");
			return 0;
		}
		else
			LOG_INF("The device is put in USB mass storage mode.");
	}

	for (;;)
	{
		k_msleep(SLEEP_TIME_MS);
		wdt_feed(wdt, wdt_channel_id);
		gpio_pin_set_dt(&led0, plc_get_state());	// PLC state
	}

	LOG_INF("exit main");
	return 0;
}

/****************************************************************************************************************************************
 * @brief    Wrapper function for the Zephyr open function to be used in projects where direct usage of open is not feasible due to 
 *           namespace conflicts or when integrating with libraries expecting an _open function signature.
 * 
 * @param    name The pathname of the file.
 * @param    flags Flags that control how the file should be opened.
 * @return   Returns a file descriptor on success, or -1 on failure.
 ****************************************************************************************************************************************/
int open_wrapper(const char *name, int flags, ...) 
{
    va_list args;
    va_start(args, flags);
    int result = open(name, flags);
    va_end(args);
    return result;
}
int _open(const char *name, int flags, ...) __attribute__((alias("open_wrapper")));