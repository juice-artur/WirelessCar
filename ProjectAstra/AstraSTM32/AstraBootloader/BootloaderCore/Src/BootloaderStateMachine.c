#include "BootloaderStateMachine.h"


typedef struct BootloaderSM{
    BootloaderState currentState;
    uint32_t fwSize;
    uint32_t bytesReceived;
} BootloaderSM_t;

static BootloaderSM_t instance; 


void BootloaderSMDispatch(BootloaderEvent_t event, void *eventData)
{
    switch (instance.currentState)
    {
        case IDLE:
            if (event == EVENT_CONN_ESTABLISHED)
            {
                instance.currentState = WAIT_SIZE;
            }
            break;

        case WAIT_SIZE:
            if (event == EVENT_SIZE_RECEIVED)
            {
                instance.fwSize = *((uint32_t *)eventData);
                instance.bytesReceived = 0;
                instance.currentState = ERASING;
            }
            break;

        case ERASING:
            if (event == EVENT_ERASE_DONE)
            {
                instance.currentState = RECEIVING;
            }
            break;

        case RECEIVING:
            if (event == EVENT_DATA_PACKET_RCVD)
            {
                uint32_t bytesReceived = *((uint32_t *)eventData);
                instance.bytesReceived += bytesReceived;

                if (instance.bytesReceived >= instance.fwSize)
                {
                    instance.currentState = COMPLETE_RECEIVING;
                }
            }
            break;

        case COMPLETE_RECEIVING:
            if (event == EVENT_CHECKSUM_OK)
            {
                instance.currentState = UPDATE_FIRMWARE;
            }
            else if (event == EVENT_CHECKSUM_ERROR)
            {
                instance.currentState = STOPPED;
            }
            break;

        case UPDATE_FIRMWARE:
            if (event == EVENT_UPDATE_SUCCESS)
            {
                instance.currentState = JUMP_TO_APPLICATION;
            }
            else if (event == EVENT_HARDWARE_ERROR)
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


void BootloaderSMInit(BootloaderEvent_t event, void *eventData)
{
    instance.currentState = IDLE;
    instance.fwSize = 0;
    instance.bytesReceived = 0;
}