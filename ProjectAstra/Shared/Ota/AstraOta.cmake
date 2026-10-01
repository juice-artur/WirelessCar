cmake_minimum_required(VERSION 3.22)

if(DEFINED ASTRA_OTA_INCLUDED)
    return()
endif()
set(ASTRA_OTA_INCLUDED TRUE)

set(LIBRARY_NAME AstraOta)

if(NOT ASTRA_GENERATED_DIR)
    message(FATAL_ERROR
            "AstraOta must be included after astra_configure_flash_layout()")
endif()


if(NOT TARGET AstraFlash)
    include("${CMAKE_CURRENT_LIST_DIR}/../AstraFlash/AstraFlash.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../AstraConstants/AstraConstants.cmake")

add_library(${LIBRARY_NAME} STATIC 
                            "${CMAKE_CURRENT_LIST_DIR}/Src/OtaRequest.c"
                            "${CMAKE_CURRENT_LIST_DIR}/Src/OtaEngine.c"
                            "${CMAKE_CURRENT_LIST_DIR}/Src/OtaStreamNordic.c"
)

target_sources(${LIBRARY_NAME} PRIVATE 
                                "${CMAKE_CURRENT_LIST_DIR}/Include/OtaRequest.h"
                                "${CMAKE_CURRENT_LIST_DIR}/Include/OtaStream.h"
                                "${CMAKE_CURRENT_LIST_DIR}/Include/OtaEngine.h"
                                "${CMAKE_CURRENT_LIST_DIR}/Include/OtaStreamNordic.h"
)

# FlashLayout.h and AppHeader.h are implementation details of OtaRequest.c, so
# they stay private and are never exposed to the consumers of OtaRequest.h.
target_include_directories(${LIBRARY_NAME}
    PUBLIC "${CMAKE_CURRENT_LIST_DIR}/Include"
    PRIVATE "${ASTRA_GENERATED_DIR}"
)

target_link_libraries(${LIBRARY_NAME}
    PRIVATE AstraFlash
    PRIVATE AstraImage
    PRIVATE AstraConstants
)