#pragma once

typedef enum BootloaderState{
    IDLE,
    WAIT_SIZE,
    ERASING,
    RECEIVING,
    COMPLETE_RECEIVING,
    UPDATE_FIRMWARE,
    JUMP_TO_APPLICATION,
    STOPPED,

} BootloaderState_t;

typedef enum BootloaderEvent{
    EVENT_CONN_ESTABLISHED,   
    EVENT_TIMEOUT,            
    EVENT_CANCEL,             

    EVENT_SIZE_RECEIVED,      
    EVENT_ERASE_DONE,      
    EVENT_DATA_PACKET_RCVD,  
    EVENT_LAST_PACKET_RCVD, 

    EVENT_CHECKSUM_OK,       
    EVENT_CHECKSUM_ERROR,     
    EVENT_UPDATE_SUCCESS,     
    
    EVENT_HARDWARE_ERROR     
} BootloaderEvent_t;

void BootloaderSMDispatch(BootloaderEvent_t event, void *eventData);

void BootloaderSMInit(BootloaderEvent_t event, void *eventData);

