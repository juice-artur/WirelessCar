#include "AppHeader.h"

#ifndef ASTRA_APP_VERSION
#error "ASTRA_APP_VERSION must be defined by the build system - see Shared/Image/AstraImage.cmake"
#endif

_Static_assert((uint64_t)ASTRA_APP_VERSION <= (uint64_t)UINT32_MAX,
               "ASTRA_APP_VERSION does not fit into AppHeader_t::version");

__attribute__((section(".header"), used))
const AppHeader_t appHeader = { .magic = ASTRA_MAGIC_VALUE,
                                .size = 0U,
                                .crc = 0U,
                                .version = ASTRA_APP_VERSION,
                                .otaRequest = OTA_NOT_REQUESTED };

const AppHeader_t *GetAppHeader(void)
{
	return &appHeader;
}