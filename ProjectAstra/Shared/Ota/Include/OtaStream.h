#pragma once

#include <stdint.h>

typedef struct OtaStream
{
	int (*read)(struct OtaStream *self, uint8_t *buffer, uint32_t len,
	            uint32_t timeoutMs);

	int (*write)(struct OtaStream *self, const uint8_t *buffer, uint32_t len,
	             uint32_t timeoutMs);

	void *userData;
} OtaStream_t;