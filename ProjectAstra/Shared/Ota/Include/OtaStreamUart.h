#pragma once

#include "stm32g4xx_hal.h"
#include "OtaStream.h"

typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t isStarted;
} UartCtx_t;

void OtaStreamUartInit(OtaStream_t *stream, UartCtx_t *ctx, UART_HandleTypeDef *huart);