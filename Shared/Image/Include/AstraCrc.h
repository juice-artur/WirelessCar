#pragma once

#include <stddef.h>
#include <stdint.h>

/* CRC-32/IEEE 802.3, identical to Python zlib.crc32() and the Qt side. */
#define ASTRA_CRC32_INIT 0xFFFFFFFFUL

uint32_t AstraCrc32Update(uint32_t state, const void *data, size_t length);
uint32_t AstraCrc32(const void *data, size_t length);

/* CRC-16/CCITT-FALSE, used by the OTA UART frames. */
uint16_t AstraCrc16(const void *data, size_t length);
