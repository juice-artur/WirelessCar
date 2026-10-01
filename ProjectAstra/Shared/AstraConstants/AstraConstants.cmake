add_library(AstraConstants INTERFACE)

target_sources(AstraConstants INTERFACE "${CMAKE_CURRENT_LIST_DIR}/Include/UARTCommands.h")
target_include_directories(AstraConstants INTERFACE "${CMAKE_CURRENT_LIST_DIR}/Include")