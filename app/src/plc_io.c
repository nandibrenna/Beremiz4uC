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
LOG_MODULE_REGISTER(plc_io, LOG_LEVEL_DBG);

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "config.h"

// PLC Hardware IO Variable, used also in plc_loader for mapping to plc_module
uint16_t QW[QW_COUNT] = {0};
uint16_t IW[IW_COUNT] = {0};
uint8_t QX[QX_COUNT] = {0};
uint8_t IX[IX_COUNT] = {0};

// Hardware IO
#define OUT0_NODE DT_ALIAS(out0)
#define OUT1_NODE DT_ALIAS(out1)
#define OUT2_NODE DT_ALIAS(out2)
#define OUT3_NODE DT_ALIAS(out3)
#define OUT4_NODE DT_ALIAS(out4)
#define OUT5_NODE DT_ALIAS(out5)
#define OUT6_NODE DT_ALIAS(out6)
#define OUT7_NODE DT_ALIAS(out7)

#if IS_ENABLED(CONFIG_IO_TEST_I2C_GPIO)
static const struct gpio_dt_spec out0 = GPIO_DT_SPEC_GET(OUT0_NODE, gpios);
static const struct gpio_dt_spec out1 = GPIO_DT_SPEC_GET(OUT1_NODE, gpios);
static const struct gpio_dt_spec out2 = GPIO_DT_SPEC_GET(OUT2_NODE, gpios);
static const struct gpio_dt_spec out3 = GPIO_DT_SPEC_GET(OUT3_NODE, gpios);
static const struct gpio_dt_spec out4 = GPIO_DT_SPEC_GET(OUT4_NODE, gpios);
static const struct gpio_dt_spec out5 = GPIO_DT_SPEC_GET(OUT5_NODE, gpios);
static const struct gpio_dt_spec out6 = GPIO_DT_SPEC_GET(OUT6_NODE, gpios);
static const struct gpio_dt_spec out7 = GPIO_DT_SPEC_GET(OUT7_NODE, gpios);
#else
extern const struct gpio_dt_spec led1;
extern const struct gpio_dt_spec led2;
#endif

/*****************************************************************************************************************************/
/*		plc io thread																								     	 */
/*****************************************************************************************************************************/
#define PLC_IO_STACK_SIZE 1024
#define PLC_IO_PRIORITY 5
extern void plc_io_task(void *, void *, void *);
K_THREAD_DEFINE_CCM(plc_io, PLC_IO_STACK_SIZE, plc_io_task, NULL, NULL, NULL, PLC_IO_PRIORITY, 0, PLC_TASK_STARTUP_DELAY);

void plc_io_task(void *, void *, void *)
{
	LOG_INF("plc_io_thread started");
	for (;;)
	{
		k_sleep(K_MSEC(100));
	}
}

void plc_init_io(void)
{
	LOG_INF("plc_init_io");
	memset(QW, 0, HOLDING_REG_COUNT * sizeof(uint16_t));
	memset(IW, 0, INPUT_REG_COUNT * sizeof(uint16_t));
	memset(QX, 0, COIL_COUNT * sizeof(uint8_t));
	memset(IX, 0, DISCRETE_COUNT * sizeof(uint8_t));

	// init GPIO
#if IS_ENABLED(CONFIG_IO_TEST_I2C_GPIO)	
	int ret = 0;
	ret = gpio_pin_configure_dt(&out0, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out1, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out2, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out3, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out4, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out5, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out6, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&out7, GPIO_OUTPUT_INACTIVE);
#else
	int ret = 0;
	ret = gpio_pin_configure_dt(&led1, GPIO_OUTPUT_INACTIVE);
	ret += gpio_pin_configure_dt(&led2, GPIO_OUTPUT_INACTIVE);
#endif

	if (ret == 0)
		LOG_INF("Setup GPIO OK");
	else
		LOG_ERR("Setup GPIO ret=%u", ret);

	return;
}

void plc_update_inputs(void)
{
	// update inputs
	return;
}

void plc_update_outputs(bool plc_run)
{
	if (plc_run)
#if IS_ENABLED(CONFIG_IO_TEST_I2C_GPIO)
	{
		// update outputs
		gpio_pin_set_dt(&out0, QX[0] ? 1 : 0);
		gpio_pin_set_dt(&out1, QX[1] ? 1 : 0);
		gpio_pin_set_dt(&out2, QX[2] ? 1 : 0);
		gpio_pin_set_dt(&out3, QX[3] ? 1 : 0);
		gpio_pin_set_dt(&out4,QX[4]?1:0);
		gpio_pin_set_dt(&out5,QX[5]?1:0);
		gpio_pin_set_dt(&out6,QX[6]?1:0);
		gpio_pin_set_dt(&out7,QX[7]?1:0);
	}
	else
	{
		gpio_pin_set_dt(&out0, 0);
		gpio_pin_set_dt(&out1, 0);
		gpio_pin_set_dt(&out2, 0);
		gpio_pin_set_dt(&out3, 0);
		gpio_pin_set_dt(&out4,0);
		gpio_pin_set_dt(&out5,0);
		gpio_pin_set_dt(&out6,0);
		gpio_pin_set_dt(&out7,0);
	}
#else
	{
		// update outputs
		gpio_pin_set_dt(&led1, QX[0] ? 1 : 0);
		gpio_pin_set_dt(&led2, QX[1] ? 1 : 0);
	}
	else
	{
		gpio_pin_set_dt(&led1, 0);
		gpio_pin_set_dt(&led2, 0);
	}
#endif	
	return;
}