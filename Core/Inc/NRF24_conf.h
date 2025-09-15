/*
 * 25-JUL-2024
 * STM32 HAL NRF24 LIBRARY
 */
#include "main.h"

#ifndef _NRF24_CONF_H_
#define _NRF24_CONF_H_

#define hspiX hspi1
#define spi_w_timeout 1000
#define spi_r_timeout 1000
#define spi_rw_timeout 1000

#define csn_gpio_port    CSN_Pin_GPIO_Port
#define csn_gpio_pin     CSN_Pin_Pin

#define ce_gpio_port     CE_Pin_GPIO_Port
#define ce_gpio_pin      CE_Pin_Pin

#endif

