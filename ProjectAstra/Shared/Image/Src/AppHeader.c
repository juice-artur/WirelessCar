#include "AppHeader.h"

#ifndef ASTRA_APP_VERSION
#define ASTRA_APP_VERSION 0U
#endif

__attribute__((section(".header"), used))
const AppHeader_t app_header = {.magic = ASTRA_MAGIC_VALUE,
                                .size = 0U,
                                .crc = 0U,
                                .version = ASTRA_APP_VERSION,
                                .otaRequest = OTA_NOT_REQUESTED};

const AppHeader_t *GetAppHeader(void)
{
	return &app_header;
}
