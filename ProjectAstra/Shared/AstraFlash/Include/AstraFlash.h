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
