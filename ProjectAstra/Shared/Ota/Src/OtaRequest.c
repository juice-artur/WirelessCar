#include "OtaRequest.h"

#include <stdint.h>

#include "stm32g4xx.h"
#include "AppHeader.h"
#include "AstraFlash.h"
#include "FlashLayout.h"

static uint32_t headerPage[PAGE_SIZE_WORDS];

bool IsOtaRequested(void)
{
	AppHeader_t const *const header =
		(AppHeader_t const *)APP_HEADER_ADDR;

	return header->otaRequest == (uint32_t)OTA_REQUESTED;
}

static bool OtaWriteRequestStatus(uint32_t const value)
{
	AppHeader_t *const header = (AppHeader_t *)headerPage;

	FlashReadPage(APP_HEADER_ADDR, headerPage);

	if (header->otaRequest == value)
	{
		return true;
	}

	header->otaRequest = value;

	uint32_t const primask = __get_PRIMASK();

	__disable_irq();

	HAL_StatusTypeDef const status =
		(FlashErasePage(APP_HEADER_ADDR) == HAL_OK &&
		 FlashWritePage(APP_HEADER_ADDR, headerPage) == HAL_OK)
			? HAL_OK
			: HAL_ERROR;

	if (primask == 0U)
	{
		__enable_irq();
	}

	return status == HAL_OK;
}

bool OtaRequestSet(void)
{
	return OtaWriteRequestStatus((uint32_t)OTA_REQUESTED);
}

bool OtaRequestClear(void)
{
	return OtaWriteRequestStatus((uint32_t)OTA_NOT_REQUESTED);
}