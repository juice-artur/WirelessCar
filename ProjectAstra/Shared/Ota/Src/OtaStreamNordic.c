#include "OtaStreamNordic.h"
#include "OtaStream.h"
#include "UARTCommands.h"

static int NordicReadImpl(OtaStream_t *self, uint8_t *buffer, uint32_t len, uint32_t timeoutMs) 
{
    NordicCtx_t *ctx = (NordicCtx_t*)self->userData;

    if (!ctx->isStarted) {
        uint8_t cmd = CMD_STM32_READY;
        if(HAL_UART_Transmit(ctx->huart, &cmd, 1, 100) !=   HAL_OK )
        {
            return -1;
        }

    }
    if (HAL_UART_Receive(ctx->huart, buffer, len, timeoutMs) == HAL_OK) 
    {
        uint8_t ack = ASTRA_ACK;
        HAL_UART_Transmit(ctx->huart, &ack, 1, 100);

        return (int)len;
    }

    return -1;
}

void OtaStreamNordicInit(OtaStream_t *stream, NordicCtx_t *ctx, UART_HandleTypeDef *huart)
{
    if (!stream || !ctx || !huart) 
    {
        return;
    }

    ctx->huart = huart;
    ctx->isStarted = 0;

    stream->read = NordicReadImpl;
    stream->userData = (void*)ctx;
}