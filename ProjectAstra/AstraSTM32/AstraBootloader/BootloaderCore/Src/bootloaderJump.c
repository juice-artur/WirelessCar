#include "bootloaderJump.h"

#include <stdint.h>

#include "stm32g4xx.h"
#include "stm32g4xx_hal.h"
#include "AstraCrc32.h"
#include "FlashLayout.h"
#include "AppHeader.h"

ApplicationStatus_t IsApplicationValid(void)
{
	AppHeader_t const *const header = (AppHeader_t const *)APP_HEADER_ADDR;

	if (header->magic != ASTRA_MAGIC_VALUE)
	{
		return APPLICATION_ERR_MAGIC;
	}

	uint32_t const stack = *(volatile uint32_t const *)APP_VECTOR_TABLE_ADDR;
	uint32_t const reset =
	 *(volatile uint32_t const *)(APP_VECTOR_TABLE_ADDR + 4U);

	if ((reset & 1U) == 0U || (reset & ~1U) < APP_START_ADDR ||
	    (reset & ~1U) >= APP_SLOT_END_ADDR)
	{
		return APPLICATION_ERR_RESET_HANDLER;
	}

	if (stack < RAM_ORIGIN || stack > RAM_ORIGIN + RAM_LENGTH)
	{
		return APPLICATION_ERR_STACK_POINTER;
	}

	if (header->size < 2U * sizeof(uint32_t) || header->size > APP_MAX_CODE_SIZE)
	{
		return APPLICATION_ERR_SIZE;
	}

	uint32_t const calcCrc =
	 AstraCrc32_Compute((const uint8_t *)APP_START_ADDR, header->size);

	if (calcCrc != header->crc)
	{
		return APPLICATION_ERR_CRC;
	}

	return APPLICATION_VALID;
}

void JumpToApplication(void)
{
	uint32_t const appStack = *(volatile uint32_t const *)APP_VECTOR_TABLE_ADDR;
	uint32_t const appResetHandler =
	 *(volatile uint32_t const *)(APP_VECTOR_TABLE_ADDR + 4U);
	pFunction const appEntry = (pFunction)appResetHandler;

	HAL_RCC_DeInit();
	HAL_DeInit();

	__disable_irq();

	/* Stop SysTick */
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;

	for (uint32_t i = 0U; i < 8U; i++)
	{
		NVIC->ICER[i] = 0xFFFFFFFFU; // Disable interrupts
		NVIC->ICPR[i] = 0xFFFFFFFFU; // Clear pending interrupts
	}

	SCB->VTOR = APP_VECTOR_TABLE_ADDR;
	__DSB();
	__ISB();

	__set_MSP(appStack);
	__set_CONTROL(0U);
	__ISB();

	appEntry();

	/* The application reset handler must never return. */
	while (true)
	{
	}
}
