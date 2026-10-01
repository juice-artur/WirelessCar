#pragma once

#include <stdint.h>

typedef struct OtaStream {
    int (*read)(struct OtaStream*, uint8_t *buffer, uint32_t len, uint32_t timeoutMs);

    void *userData;
} OtaStream_t;