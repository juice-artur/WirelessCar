#pragma once

#include "stm32g4xx_hal.h"
#include "OtaStream.h"

typedef struct {
    UART_HandleTypeDef *huart;
    uint8_t isStarted;
} NordicCtx_t;

void OtaStreamNordicInit(OtaStream_t *stream, NordicCtx_t *ctx, UART_HandleTypeDef *huart);