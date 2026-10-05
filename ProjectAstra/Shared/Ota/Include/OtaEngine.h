#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "AppHeader.h"
#include "OtaStream.h"

typedef enum OtaStatus
{
	OTA_OK = 0,
	OTA_ERR_STREAM = -1,
	OTA_ERR_MAGIC = -2,
	OTA_ERR_SIZE = -3,
	OTA_ERR_ERASE = -4,
	OTA_ERR_FLASH = -5,
	OTA_ERR_CRC = -6,
	OTA_ERR_TIMEOUT = -7
} OtaStatus_t;

typedef void (*OtaProgressFn)(uint32_t chunkBytes, bool isLast, void *userData);

OtaStatus_t OtaReadHeader(OtaStream_t *stream, AppHeader_t *header,
                          uint32_t timeoutMs);

OtaStatus_t OtaEraseAppSlot(OtaStream_t *stream, uint32_t startAddr,
                            uint32_t size);

OtaStatus_t OtaReceiveImage(OtaStream_t *stream, uint32_t startAddr,
                            uint32_t size, uint32_t timeoutMs,
                            OtaProgressFn progress, void *userData);

OtaStatus_t OtaVerifyImage(OtaStream_t *stream, uint32_t startAddr,
                           uint32_t size, uint32_t expectedCrc);