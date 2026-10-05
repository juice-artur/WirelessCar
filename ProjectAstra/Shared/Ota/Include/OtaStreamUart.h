#pragma once

#include "OtaStream.h"
#include "stm32g4xx_hal.h"

#define OTA_UART_RX_BUFFER_SIZE 2048U

typedef struct
{
	UART_HandleTypeDef *huart;

	volatile uint8_t rxArmed;
	volatile uint8_t rxReady;
	volatile uint8_t rxFailed;
	volatile uint16_t rxSize;
	volatile uint16_t rxOffset;

	uint8_t isStarted;

	uint8_t rxBuffer[OTA_UART_RX_BUFFER_SIZE];
} UartCtx_t;

void OtaStreamUartInit(OtaStream_t *stream, UartCtx_t *ctx,
                       UART_HandleTypeDef *huart);

/* Announce the device as CMD_STM32_READY, which puts it into OTA mode. */
int OtaStreamUartAnnounce(UartCtx_t *ctx);

/* Convenience wrapper around the stream `write` callback for status codes. */
int OtaStreamUartSendStatus(UartCtx_t *ctx, uint8_t status);