cmake_minimum_required(VERSION 3.22)

set(LIBRARY_NAME  AstraFlash)

add_library(${LIBRARY_NAME} STATIC "${CMAKE_CURRENT_LIST_DIR}/Src/AstraFlash.c")
target_sources(${LIBRARY_NAME} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Include/AstraFlash.h")

target_include_directories(${LIBRARY_NAME} PUBLIC "${CMAKE_CURRENT_LIST_DIR}/Include")

target_link_libraries(${LIBRARY_NAME} PUBLIC stm32cubemx)