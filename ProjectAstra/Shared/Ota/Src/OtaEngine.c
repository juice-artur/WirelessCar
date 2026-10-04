#include "OtaEngine.h"
#include "AppHeader.h"
#include "FlashLayout.h"

int OtaEngineRun(OtaStream_t *stream, uint32_t appFlashStartAddr,
                  uint32_t *firmwareSize)
{
    if(!stream || !stream->read)
    {
        return -1;
    }

    AppHeader_t header;
    if (stream->read(stream, (uint8_t*)&header, sizeof(AppHeader_t), 5000) != sizeof(AppHeader_t)) 
    {
        return -1;
    }

    if (header.magic != ASTRA_MAGIC_VALUE) 
    {
        return -3; 
    }

    if (header.size == 0U || header.size > APP_MAX_CODE_SIZE)
    {
        return -4;
    }

    if (firmwareSize != NULL)
    {
        *firmwareSize = header.size;
    }

    return 0;
}