#include "OtaStreamUart.h"
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
    ctx->rxSize = (Size > OTA_UART_RX_BUFFER_SIZE) ? OTA_UART_RX_BUFFER_SIZE
                                                  : Size;
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
    ctx->rxFailed = 1U;
}

static int UartReadImpl(OtaStream_t *self, uint8_t *buffer, uint32_t len,
                        uint32_t timeoutMs)
{
    UartCtx_t *ctx = (UartCtx_t *)self->userData;
    uint32_t filled = 0U;
    uint32_t started = HAL_GetTick();

    if (ctx == NULL || buffer == NULL)
    {
        return -1;
    }

    while (filled < len)
    {
        if (ctx->rxReady != 0U)
        {
            uint16_t chunk = ctx->rxSize;

            if (chunk > (len - filled))
            {
                return -1;
            }

            for (uint16_t i = 0U; i < chunk; i++)
            {
                buffer[filled + i] = ctx->rxBuffer[i];
            }

            filled += chunk;
            ctx->rxReady = 0U;
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

        if ((HAL_GetTick() - started) >= timeoutMs)
        {
            return (int)filled;
        }

        HAL_Delay(1U);
    }

    return (int)filled;
}

int OtaStreamUartAnnounce(UartCtx_t *ctx)
{
    uint8_t cmd = CMD_STM32_READY;

    if (ctx == NULL || ctx->huart == NULL)
    {
        return -1;
    }

    if (HAL_UART_Transmit(ctx->huart, &cmd, 1U, 100U) != HAL_OK)
    {
        return -1;
    }

    ctx->isStarted = 1U;
    return 0;
}

int OtaStreamUartSendAck(UartCtx_t *ctx)
{
    uint8_t ack = ASTRA_ACK;

    if (ctx == NULL || ctx->huart == NULL)
    {
        return -1;
    }

    return (HAL_UART_Transmit(ctx->huart, &ack, 1U, 100U) == HAL_OK) ? 0 : -1;
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
    ctx->isStarted = 0U;

    stream->read = UartReadImpl;
    stream->userData = (void *)ctx;

    activeCtx = ctx;
}