#pragma once

#include <stddef.h>
#include <stdint.h>

/* "ASTR" as it appears in memory on a little-endian target. */
#define ASTRA_MAGIC_VALUE 0x52545341UL

typedef enum
{
	OTA_NOT_REQUESTED = 0,
	OTA_REQUESTED = 1
} OtaRequest;

typedef struct
{
	uint32_t magic;
	uint32_t size;
	uint32_t crc;
	/* Encoded as major * 10000 + minor * 100 + patch (see AstraImage.cmake). */
	uint32_t version;
	uint32_t otaRequest;
} AppHeader_t;

_Static_assert(sizeof(AppHeader_t) == 20U,
               "Application header must stay 20 bytes wide");

_Static_assert(offsetof(AppHeader_t, magic) == 0U,
               "magic must stay at offset 0");
_Static_assert(offsetof(AppHeader_t, otaRequest) == 16U,
               "otaRequest must stay at offset 16");

extern const AppHeader_t appHeader;

const AppHeader_t *GetAppHeader(void);