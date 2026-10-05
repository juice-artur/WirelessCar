#include "BootloaderStateMachine.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct BootloaderSM
{
	BootloaderState_t currentState;
	uint32_t fwSize;
	uint32_t bytesReceived;
} BootloaderSM_t;

static BootloaderSM_t instance;

static bool BootloaderSMIsAborted(BootloaderEvent_t event)
{
	return event == EVENT_TIMEOUT || event == EVENT_CANCEL;
}

void BootloaderSMDispatch(BootloaderEvent_t event, void *eventData)
{
	switch (instance.currentState)
	{
	case IDLE:
		if (event == EVENT_CONN_ESTABLISHED)
		{
			instance.currentState = WAIT_SIZE;
		}
		else if (BootloaderSMIsAborted(event))
		{
			instance.currentState = STOPPED;
		}
		break;

	case WAIT_SIZE:
		if (event == EVENT_SIZE_RECEIVED)
		{
			instance.fwSize = *((uint32_t *)eventData);
			instance.bytesReceived = 0;
			instance.currentState = ERASING;
		}
		else if (BootloaderSMIsAborted(event))
		{
			instance.currentState = STOPPED;
		}
		break;

	case ERASING:
		if (event == EVENT_ERASE_DONE)
		{
			instance.currentState = RECEIVING;
		}
		else if (BootloaderSMIsAborted(event))
		{
			instance.currentState = STOPPED;
		}
		break;

	case RECEIVING:
		if (event == EVENT_DATA_PACKET_RCVD)
		{
			uint32_t const bytesReceived = *((uint32_t *)eventData);

			instance.bytesReceived += bytesReceived;

			if (instance.bytesReceived >= instance.fwSize)
			{
				instance.currentState = COMPLETE_RECEIVING;
			}
		}
		else if (event == EVENT_LAST_PACKET_RCVD)
		{
			instance.currentState = COMPLETE_RECEIVING;
		}
		else if (BootloaderSMIsAborted(event))
		{
			instance.currentState = STOPPED;
		}
		break;

	case COMPLETE_RECEIVING:
		if (event == EVENT_CHECKSUM_OK)
		{
			instance.currentState = UPDATE_FIRMWARE;
		}
		else if (event == EVENT_CHECKSUM_ERROR || BootloaderSMIsAborted(event))
		{
			instance.currentState = STOPPED;
		}
		break;

	case UPDATE_FIRMWARE:
		if (event == EVENT_UPDATE_SUCCESS)
		{
			instance.currentState = JUMP_TO_APPLICATION;
		}
		else if (event == EVENT_HARDWARE_ERROR || BootloaderSMIsAborted(event))
		{
			instance.currentState = STOPPED;
		}
		break;

	case JUMP_TO_APPLICATION:
	case STOPPED:
	default:
		// No transitions from these states
		break;
	}
}

void BootloaderSMInit(void)
{
	instance.currentState = IDLE;
	instance.fwSize = 0;
	instance.bytesReceived = 0;
}

BootloaderState_t BootloaderSMGetState(void)
{
	return instance.currentState;
}