#pragma once

#include "stm32g4xx_hal.h"
#include "OtaStream.h"

#define OTA_UART_RX_BUFFER_SIZE 512U

typedef struct {
    UART_HandleTypeDef *huart;
    volatile uint8_t rxArmed;   
    volatile uint8_t rxReady;  
    volatile uint8_t rxFailed; 
    volatile uint16_t rxSize;

    uint8_t isStarted;        

    uint8_t rxBuffer[OTA_UART_RX_BUFFER_SIZE];
} UartCtx_t;

void OtaStreamUartInit(OtaStream_t *stream, UartCtx_t *ctx,
                       UART_HandleTypeDef *huart);

int OtaStreamUartAnnounce(UartCtx_t *ctx);

int OtaStreamUartSendAck(UartCtx_t *ctx);