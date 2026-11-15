#include "AstraFlash.h"

void FlashReadPage(uint32_t pageAddress, uint32_t *flashBuffer)
{
	uint32_t *flashPtr = (uint32_t *)pageAddress;

	for (uint32_t i = 0; i < PAGE_SIZE_WORDS; i++)
	{
		flashBuffer[i] = flashPtr[i];
	}
}

HAL_StatusTypeDef FlashErasePage(uint32_t pageAddress)
{
	HAL_StatusTypeDef status;
	FLASH_EraseInitTypeDef eraseInit;
	uint32_t pageError = 0;

	uint32_t pageNumber = (pageAddress - FLASH_BASE) / FLASH_PAGE_SIZE;

	HAL_FLASH_Unlock();

	eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
	eraseInit.Banks = FLASH_BANK_1;
	eraseInit.Page = pageNumber;
	eraseInit.NbPages = 1;

	status = HAL_FLASHEx_Erase(&eraseInit, &pageError);

	HAL_FLASH_Lock();

	return status;
}

HAL_StatusTypeDef FlashWritePageWords(uint32_t pageAddress,
                                      const uint32_t *words, uint32_t wordCount)
{
	HAL_StatusTypeDef status = HAL_OK;

	if (words == NULL || wordCount > PAGE_SIZE_WORDS)
	{
		return HAL_ERROR;
	}

	HAL_FLASH_Unlock();

	/* This part programs a double word at a time.  An odd word count is
	 * padded with 0xFFFFFFFF, which leaves those Flash bytes erased. */
	for (uint32_t i = 0; i < wordCount; i += 2)
	{
		uint32_t const high =
		 (i + 1u < wordCount) ? words[i + 1u] : 0xFFFFFFFFu;
		uint64_t doubleWord = (uint64_t)words[i] | ((uint64_t)high << 32);

		status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD,
		                           pageAddress + (i * 4), doubleWord);

		if (status != HAL_OK)
		{
			break;
		}
	}

	HAL_FLASH_Lock();

	return status;
}

HAL_StatusTypeDef FlashWritePage(uint32_t pageAddress,
                                 const uint32_t *flashBuffer)
{
	return FlashWritePageWords(pageAddress, flashBuffer, PAGE_SIZE_WORDS);
}

HAL_StatusTypeDef FlashWriteDoubleWords(uint32_t address, const void *data,
                                        uint32_t length)
{
	uint8_t const *bytes = (uint8_t const *)data;
	HAL_StatusTypeDef status = HAL_OK;

	if (data == NULL || (length % 8u) != 0u || (address % 8u) != 0u)
	{
		return HAL_ERROR;
	}

	HAL_FLASH_Unlock();

	for (uint32_t i = 0; i < length; i += 8u)
	{
		uint64_t const doubleWord =
		 (uint64_t)bytes[i + 0u] | ((uint64_t)bytes[i + 1u] << 8) |
		 ((uint64_t)bytes[i + 2u] << 16) | ((uint64_t)bytes[i + 3u] << 24) |
		 ((uint64_t)bytes[i + 4u] << 32) | ((uint64_t)bytes[i + 5u] << 40) |
		 ((uint64_t)bytes[i + 6u] << 48) | ((uint64_t)bytes[i + 7u] << 56);

		status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, address + i,
		                           doubleWord);
		if (status != HAL_OK)
		{
			break;
		}
	}

	HAL_FLASH_Lock();

	return status;
}