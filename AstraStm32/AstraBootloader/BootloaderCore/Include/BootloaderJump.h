#pragma once

#include <stdbool.h>

typedef enum
{
	APPLICATION_VALID,
	APPLICATION_ERR_MAGIC,
	APPLICATION_ERR_RESET_HANDLER,
	APPLICATION_ERR_STACK_POINTER,
	APPLICATION_ERR_SIZE,
	APPLICATION_ERR_CRC
} ApplicationStatus_t;

typedef void (*pFunction)(void);

void JumpToApplication(void);

ApplicationStatus_t IsApplicationValid(void);