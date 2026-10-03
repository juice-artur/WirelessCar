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
set(CMAKE_PROJECT_NAME AstraBootloader)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Image/AstraImage.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Crc/AstraCrc32.cmake")


# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Generate the shared FlashLayout.h and this target's linker script.
include("${CMAKE_CURRENT_SOURCE_DIR}/../Common/FlashLayout.cmake")
astra_configure_flash_layout(
    TARGET ${CMAKE_PROJECT_NAME}
    IMAGE_NAME "Bootloader"
    IMAGE_REGION "BOOTLOADER"
    GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/AstraFlash/AstraFlash.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/../../Shared/Ota/AstraOta.cmake")

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/BootloaderCore/Src/bootloaderJump.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/BootloaderCore/Src/BootloaderStateMachine.c"
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    "${ASTRA_GENERATED_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/BootloaderCore/Include"   
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    AstraCrc32
    AstraImage
    AstraFlash
    AstraOta
)
