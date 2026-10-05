#include "OtaStreamUart.h"

#include <stddef.h>

#include "OtaStream.h"
#include "UARTCommands.h"

static UartCtx_t *activeCtx;

static int UartStartReceive(UartCtx_t *ctx)
{
	if (ctx->rxArmed != 0U)
	{
		return 0;
	}

	ctx->rxFailed = 0U;

	if (HAL_UARTEx_ReceiveToIdle_IT(ctx->huart, ctx->rxBuffer,
	                                OTA_UART_RX_BUFFER_SIZE) != HAL_OK)
	{
		return -1;
	}

	ctx->rxArmed = 1U;
	return 0;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
	UartCtx_t *ctx = activeCtx;

	if (ctx == NULL || ctx->huart != huart)
	{
		return;
	}

	ctx->rxArmed = 0U;
	ctx->rxOffset = 0U;
	ctx->rxSize =
	 (Size > OTA_UART_RX_BUFFER_SIZE) ? OTA_UART_RX_BUFFER_SIZE : Size;
	ctx->rxReady = 1U;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	UartCtx_t *ctx = activeCtx;

	if (ctx == NULL || ctx->huart != huart)
	{
		return;
	}

	ctx->rxArmed = 0U;
	ctx->rxReady = 0U;
	ctx->rxOffset = 0U;
	ctx->rxSize = 0U;
	ctx->rxFailed = 1U;
}

static int UartReadImpl(OtaStream_t *self, uint8_t *buffer, uint32_t len,
                        uint32_t timeoutMs)
{
	UartCtx_t *const ctx = (UartCtx_t *)self->userData;
	uint32_t filled = 0U;
	uint32_t lastProgress = HAL_GetTick();

	if (ctx == NULL || buffer == NULL)
	{
		return -1;
	}

	while (filled < len)
	{
		uint32_t available = 0U;

		if (ctx->rxReady != 0U)
		{
			ctx->rxReady = 0U;
			available = (uint32_t)ctx->rxSize - (uint32_t)ctx->rxOffset;
		}

		if (available > 0U)
		{
			uint32_t const room = len - filled;
			uint32_t const taken = (available < room) ? available : room;

			/*
			 * A byte loop instead of memcpy: the latter pulls the newlib
			 * stub chain into an image that is only ever asked to move
			 * data between two internal buffers.
			 */
			for (uint32_t i = 0U; i < taken; i++)
			{
				buffer[filled + i] = ctx->rxBuffer[ctx->rxOffset + i];
			}

			filled += taken;
			ctx->rxOffset = (uint16_t)(ctx->rxOffset + taken);
			lastProgress = HAL_GetTick();
			continue;
		}

		if (ctx->rxFailed != 0U)
		{
			return -1;
		}

		if (UartStartReceive(ctx) != 0)
		{
			return -1;
		}

		if ((HAL_GetTick() - lastProgress) >= timeoutMs)
		{
			break;
		}

		HAL_Delay(1U);
	}

	return (int)filled;
}

static int UartWriteImpl(OtaStream_t *self, const uint8_t *buffer, uint32_t len,
                         uint32_t timeoutMs)
{
	UartCtx_t *const ctx = (UartCtx_t *)self->userData;

	if (ctx == NULL || buffer == NULL || len == 0U)
	{
		return -1;
	}

	return (HAL_UART_Transmit(ctx->huart, (uint8_t *)buffer, (uint16_t)len,
	                          timeoutMs) == HAL_OK)
	        ? (int)len
									: -1;
}

int OtaStreamUartSendStatus(UartCtx_t *ctx, uint8_t status)
{
	if (ctx == NULL || ctx->huart == NULL)
	{
		return -1;
	}

	return (HAL_UART_Transmit(ctx->huart, &status, 1U, 100U) == HAL_OK) ? 0 : -1;
}

int OtaStreamUartAnnounce(UartCtx_t *ctx)
{
	if (OtaStreamUartSendStatus(ctx, CMD_STM32_READY) != 0)
	{
		return -1;
	}

	ctx->isStarted = 1U;
	return 0;
}

void OtaStreamUartInit(OtaStream_t *stream, UartCtx_t *ctx,
                       UART_HandleTypeDef *huart)
{
	if (stream == NULL || ctx == NULL || huart == NULL)
	{
		return;
	}

	ctx->huart = huart;
	ctx->rxArmed = 0U;
	ctx->rxReady = 0U;
	ctx->rxFailed = 0U;
	ctx->rxSize = 0U;
	ctx->rxOffset = 0U;
	ctx->isStarted = 0U;

	stream->read = UartReadImpl;
	stream->write = UartWriteImpl;
	stream->userData = (void *)ctx;

	activeCtx = ctx;
}