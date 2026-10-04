cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME AstraApplication)

set(ASTRA_APP_VERSION 1 CACHE STRING "Application image version recorded in the header")

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Image/AstraImage.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/AstraFlash/AstraFlash.cmake")

# Generate the shared FlashLayout.h and this target's linker script.
include("${CMAKE_CURRENT_SOURCE_DIR}/../Common/FlashLayout.cmake")
astra_configure_flash_layout(
    TARGET ${CMAKE_PROJECT_NAME}
    IMAGE_NAME "Application"
    IMAGE_REGION "APPLICATION"
    GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
)


set(ASTRA_OTA_ENABLE_UART OFF)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Ota/AstraOta.cmake")

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Core/Src/system_stm32g4xx.c includes the generated FlashLayout.h to relocate
# the vector table.  Keep this here instead of in the CubeMX-generated file so
# it survives regeneration.
target_include_directories(stm32cubemx INTERFACE "${ASTRA_GENERATED_DIR}")

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${ASTRA_GENERATED_DIR}"
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    ASTRA_APP_VERSION=${ASTRA_APP_VERSION}
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    AstraImage
    AstraFlash
    AstraOta
)


find_package(Python3 REQUIRED COMPONENTS Interpreter)


find_program(ASTRA_OBJCOPY NAMES ${CMAKE_OBJCOPY} REQUIRED)

set(ASTRA_IMAGE_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/../../../scripts/astra_image.py")
set(ASTRA_IMAGE_BIN    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin")
set(ASTRA_IMAGE_ELF    "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.packed.elf")

add_custom_command(
    OUTPUT "${ASTRA_IMAGE_BIN}" "${ASTRA_IMAGE_ELF}"
    COMMAND "${Python3_EXECUTABLE}" "${ASTRA_IMAGE_SCRIPT}"
            --elf "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>"
            --bin "${ASTRA_IMAGE_BIN}"
            --patched-elf "${ASTRA_IMAGE_ELF}"
            --nm "${CMAKE_NM}"
            --objcopy "${ASTRA_OBJCOPY}"
            --header-addr "${APP_HEADER_ADDR}"
            --image-start "${APP_START_ADDR}"
            --version "${ASTRA_APP_VERSION}"
            --max-code-size "${APP_MAX_CODE_SIZE}"
    DEPENDS ${CMAKE_PROJECT_NAME} "${ASTRA_IMAGE_SCRIPT}"
    COMMENT "Packing ${CMAKE_PROJECT_NAME} image header (size and CRC)"
    VERBATIM
)

add_custom_target(astra_image ALL DEPENDS "${ASTRA_IMAGE_BIN}" "${ASTRA_IMAGE_ELF}")
