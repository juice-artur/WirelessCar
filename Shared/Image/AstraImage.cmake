cmake_minimum_required(VERSION 3.22)

add_library(AstraImage STATIC "${CMAKE_CURRENT_LIST_DIR}/Src/AppHeader.c")
target_sources(AstraImage PRIVATE 
        "${CMAKE_CURRENT_LIST_DIR}/Include/AppHeader.h"
)

target_include_directories(AstraImage PUBLIC 
        "${CMAKE_CURRENT_LIST_DIR}/Include"
)
