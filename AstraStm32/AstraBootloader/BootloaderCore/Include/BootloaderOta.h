#pragma once

#include <stdbool.h>

#include "stm32g4xx_hal.h"

/* True when the application header asks the bootloader to stay in OTA mode. */
bool BootloaderOta_IsRequested(void);

/*
 * Serves the OTA protocol over the given UART.
 *
 * Blocks until the update is finished (then resets), or, when the application
 * is valid and no update was requested, until the listen window expires.
 */
void BootloaderOta_Run(UART_HandleTypeDef *uart);

/* Called from HAL_UART_RxCpltCallback() for the OTA UART. */
void BootloaderOta_OnRxCplt(UART_HandleTypeDef *uart);

/*
 * Arms interrupt-driven reception.  Called right after the UART is
 * initialised, before the bootloader shows its startup blinks, so bytes that
 * arrive while the application hands over are buffered instead of setting an
 * overrun that would kill reception.
 */
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
