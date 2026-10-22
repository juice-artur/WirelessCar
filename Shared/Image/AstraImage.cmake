cmake_minimum_required(VERSION 3.22)

# Version of the image, stored in the application header (see AppHeader.h).
#
# The value is a semantic version "major.minor.patch" and is encoded into a
# single monotonic 32-bit number:
#
#     major * 10000 + minor * 100 + patch
#
# so that comparing two versions is a plain '>'.  The application sets
# ASTRA_APP_VERSION; the bootloader keeps the default, because its own copy
# of the header is not part of its image.
#
# Override from the command line:
#   cmake --preset Debug -DASTRA_APP_VERSION=1.2.3
set(ASTRA_APP_VERSION "0.0.0" CACHE STRING "Version of the image (major.minor.patch)")

if(NOT ASTRA_APP_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
    message(FATAL_ERROR
            "AstraImage: ASTRA_APP_VERSION must be 'major.minor.patch', got '${ASTRA_APP_VERSION}'")
endif()

string(LENGTH "${CMAKE_MATCH_1}" _major_length)
if(_major_length GREATER 6)
    message(FATAL_ERROR
            "AstraImage: major version is too large: '${ASTRA_APP_VERSION}'")
endif()

# minor and patch stay below 100, otherwise the encoding is not monotonic
# (1.100.0 would collide with 2.0.0).
if(CMAKE_MATCH_2 GREATER 99 OR CMAKE_MATCH_3 GREATER 99)
    message(FATAL_ERROR
            "AstraImage: minor and patch must be below 100, got '${ASTRA_APP_VERSION}'")
endif()

math(EXPR ASTRA_APP_VERSION_VALUE
     "${CMAKE_MATCH_1} * 10000 + ${CMAKE_MATCH_2} * 100 + ${CMAKE_MATCH_3}")

if(ASTRA_APP_VERSION_VALUE GREATER 4294967295)
    message(FATAL_ERROR
            "AstraImage: ASTRA_APP_VERSION does not fit into a 32-bit field, got '${ASTRA_APP_VERSION}'")
endif()

add_library(AstraImage STATIC "${CMAKE_CURRENT_LIST_DIR}/Src/AppHeader.c")
target_sources(AstraImage PRIVATE 
        "${CMAKE_CURRENT_LIST_DIR}/Include/AppHeader.h"
)

target_include_directories(AstraImage PUBLIC 
        "${CMAKE_CURRENT_LIST_DIR}/Include"
)

target_compile_definitions(AstraImage PRIVATE
        ASTRA_APP_VERSION=${ASTRA_APP_VERSION_VALUE}U
)
