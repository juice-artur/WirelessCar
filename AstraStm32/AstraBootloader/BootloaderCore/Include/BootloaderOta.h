#pragma once

#include <stdbool.h>

#include "stm32g4xx_hal.h"

/* True when the application header asks the bootloader to stay in OTA mode. */
bool BootloaderOta_IsRequested(void);

void BootloaderOta_Run(UART_HandleTypeDef *uart);

void BootloaderOta_OnRxCplt(UART_HandleTypeDef *uart);

void BootloaderOta_PrimeRx(UART_HandleTypeDef *uart);

/* Called from HAL_UART_ErrorCallback() for the OTA UART. */
void BootloaderOta_OnError(UART_HandleTypeDef *uart);

/*
 * Link diagnostic: samples USART1 RX (PA10) as a pulled-down input before the
 * UART is configured.  A connected bridge holds the line high, so a low level
 * means the wire, the pad or the bridge TX is at fault.  Must run before
 * MX_USART1_UART_Init() and leaves PA10 released for the UART.
 */
bool BootloaderOta_SniffRxLine(void);
