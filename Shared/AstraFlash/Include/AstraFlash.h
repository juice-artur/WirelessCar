#pragma once

#include "stm32g4xx_hal.h"
#include "stm32g4xx_hal_flash_ex.h"

#define PAGE_SIZE_WORDS 512

void FlashReadPage(uint32_t pageAddress, uint32_t *flashBuffer);
HAL_StatusTypeDef FlashErasePage(uint32_t pageAddress);
HAL_StatusTypeDef FlashWritePage(uint32_t pageAddress,
                                 const uint32_t *flashBuffer);

HAL_StatusTypeDef FlashWritePageWords(uint32_t pageAddress,
                                      const uint32_t *words,
                                      uint32_t wordCount);

/* Programs a range that is already erased.  The address and the length must
 * both be 8-byte aligned (STM32G4 programs double words). */
HAL_StatusTypeDef FlashWriteDoubleWords(uint32_t address, const void *data,
                                        uint32_t length);