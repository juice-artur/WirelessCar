#include "OtaEngine.h"

#include <stddef.h>
#include <string.h>

#include "AstraCrc32.h"
#include "AstraFlash.h"
#include "FlashLayout.h"
#include "UARTCommands.h"

#define OTA_STATUS_TIMEOUT_MS 100U

static uint32_t pageBuffer[PAGE_SIZE_WORDS];

_Static_assert(PAGE_SIZE_WORDS * sizeof(uint32_t) == FLASH_LAYOUT_PAGE_SIZE,
               "The page buffer must match one complete Flash page");

static OtaStatus_t OtaSendStatus(OtaStream_t *stream, uint8_t status)
{
	if (stream->write(stream, &status, 1U, OTA_STATUS_TIMEOUT_MS) != 1)
	{
		return OTA_ERR_STREAM;
	}

	return OTA_OK;
}

OtaStatus_t OtaReadHeader(OtaStream_t *stream, AppHeader_t *header,
                          uint32_t timeoutMs)
{
	int received;

	if (stream == NULL || stream->read == NULL || header == NULL)
	{
		return OTA_ERR_STREAM;
	}

	received =
	 stream->read(stream, (uint8_t *)header, sizeof(AppHeader_t), timeoutMs);

	if (received < 0)
	{
		return OTA_ERR_STREAM;
	}

	if ((uint32_t)received != sizeof(AppHeader_t))
	{
		(void)OtaSendStatus(stream, ASTRA_NACK);
		return OTA_ERR_TIMEOUT;
	}

	if (header->magic != ASTRA_MAGIC_VALUE)
	{
		(void)OtaSendStatus(stream, ASTRA_NACK);
		return OTA_ERR_MAGIC;
	}

	if (header->size < 2U * sizeof(uint32_t) || header->size > APP_MAX_CODE_SIZE)
	{
		(void)OtaSendStatus(stream, ASTRA_NACK);
		return OTA_ERR_SIZE;
	}

	return OtaSendStatus(stream, ASTRA_ACK);
}

OtaStatus_t OtaEraseAppSlot(OtaStream_t *stream, uint32_t startAddr,
                            uint32_t size)
{
	uint32_t address;

	if (stream == NULL)
	{
		return OTA_ERR_STREAM;
	}

	if (startAddr < APP_START_ADDR ||
	    (startAddr & (FLASH_LAYOUT_PAGE_SIZE - 1U)) != 0U)
	{
		return OTA_ERR_SIZE;
	}

	if (size == 0U || (startAddr + size) > APP_SLOT_END_ADDR)
	{
		return OTA_ERR_SIZE;
	}

	for (address = startAddr; address < (startAddr + size);
	     address += FLASH_LAYOUT_PAGE_SIZE)
	{
		if (FlashErasePage(address) != HAL_OK)
		{
			return OTA_ERR_ERASE;
		}
	}

	return OtaSendStatus(stream, ASTRA_ERASE_DONE);
}

OtaStatus_t OtaReceiveImage(OtaStream_t *stream, uint32_t startAddr,
                            uint32_t size, uint32_t timeoutMs,
                            OtaProgressFn progress, void *userData)
{
	uint32_t written = 0U;

	if (stream == NULL || stream->read == NULL)
	{
		return OTA_ERR_STREAM;
	}

	while (written < size)
	{
		uint32_t chunk = size - written;
		int received;

		if (chunk > FLASH_LAYOUT_PAGE_SIZE)
		{
			chunk = FLASH_LAYOUT_PAGE_SIZE;
		}

		received = stream->read(stream, (uint8_t *)pageBuffer, chunk, timeoutMs);

		if (received < 0)
		{
			return OTA_ERR_STREAM;
		}

		if ((uint32_t)received != chunk)
		{
			return OTA_ERR_TIMEOUT;
		}

		if (FlashWritePageWords(startAddr + written, pageBuffer,
		                        chunk / sizeof(uint32_t)) != HAL_OK)
		{
			return OTA_ERR_FLASH;
		}

		written += chunk;

		if (OtaSendStatus(stream, ASTRA_ACK) != OTA_OK)
		{
			return OTA_ERR_STREAM;
		}

		if (progress != NULL)
		{
			progress(chunk, written >= size, userData);
		}
	}

	return OTA_OK;
}

OtaStatus_t OtaVerifyImage(OtaStream_t *stream, uint32_t startAddr,
                           uint32_t size, uint32_t expectedCrc)
{
	uint32_t const actual = AstraCrc32_Compute((const uint8_t *)startAddr, size);

	if (actual != expectedCrc)
	{
		(void)OtaSendStatus(stream, ASTRA_IMAGE_BAD);
		return OTA_ERR_CRC;
	}

	return OtaSendStatus(stream, ASTRA_IMAGE_OK);
}