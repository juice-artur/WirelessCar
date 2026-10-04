#pragma once

#include "OtaStream.h"
#include <stdint.h>

int OtaEngineRun(OtaStream_t *stream, uint32_t appFlashStartAddr,
                  uint32_t *firmwareSize);