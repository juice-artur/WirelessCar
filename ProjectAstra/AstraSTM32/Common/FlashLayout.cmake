# STM32G431CBT6 Flash layout for Astra.
#
# This file is the single source of truth for the C header and both linker
# scripts.  The generated files are written to the build tree and are not
# intended to be edited or committed.

if(DEFINED ASTRA_FLASH_LAYOUT_INCLUDED)
    return()
endif()
set(ASTRA_FLASH_LAYOUT_INCLUDED TRUE)

# Device memory constants.
set(FLASH_BASE_ADDR             0x08000000)
set(FLASH_SIZE_BYTES            0x00020000)
set(FLASH_LAYOUT_PAGE_SIZE      0x00000800)
set(RAM_ORIGIN                  0x20000000)
set(RAM_LENGTH                  0x00008000)
set(VECTOR_TABLE_ALIGNMENT      0x00000200)

# Physical layout: bootloader, application header/code, then two metadata pages.
set(BOOTLOADER_START_ADDR       ${FLASH_BASE_ADDR})
set(BOOTLOADER_SIZE             0x00006000)
set(APP_HEADER_SIZE             ${FLASH_LAYOUT_PAGE_SIZE})
set(META_PAGE_SIZE              ${FLASH_LAYOUT_PAGE_SIZE})
set(META_A_START_ADDR           0x0801F000)

# Derived addresses.  CMake's hexadecimal output is valid in both C and GNU ld.
math(EXPR FLASH_END_ADDR
     "${FLASH_BASE_ADDR} + ${FLASH_SIZE_BYTES}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR FLASH_LAYOUT_PAGE_COUNT
     "${FLASH_SIZE_BYTES} / ${FLASH_LAYOUT_PAGE_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR VECTOR_TABLE_ALIGNMENT_MASK
     "${VECTOR_TABLE_ALIGNMENT} - 1"
     OUTPUT_FORMAT HEXADECIMAL)

math(EXPR BOOTLOADER_END_ADDR
     "${BOOTLOADER_START_ADDR} + ${BOOTLOADER_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR BOOTLOADER_PAGE_COUNT
     "${BOOTLOADER_SIZE} / ${FLASH_LAYOUT_PAGE_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)

set(APP_SLOT_START_ADDR         ${BOOTLOADER_END_ADDR})
set(APP_HEADER_ADDR             ${APP_SLOT_START_ADDR})
math(EXPR APP_HEADER_END_ADDR
     "${APP_HEADER_ADDR} + ${APP_HEADER_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)

set(APP_START_ADDR              ${APP_HEADER_END_ADDR})
set(APP_VECTOR_TABLE_ADDR       ${APP_START_ADDR})
math(EXPR APP_VECTOR_TABLE_OFFSET
     "${APP_VECTOR_TABLE_ADDR} - ${FLASH_BASE_ADDR}"
     OUTPUT_FORMAT HEXADECIMAL)

set(APP_SLOT_END_ADDR           ${META_A_START_ADDR})
math(EXPR APP_SLOT_SIZE
     "${APP_SLOT_END_ADDR} - ${APP_SLOT_START_ADDR}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR APP_MAX_CODE_SIZE
     "${APP_SLOT_END_ADDR} - ${APP_START_ADDR}"
     OUTPUT_FORMAT HEXADECIMAL)
set(APP_MAX_IMAGE_SIZE          ${APP_SLOT_SIZE})

math(EXPR META_B_START_ADDR
     "${META_A_START_ADDR} + ${META_PAGE_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR FLASH_METADATA_END_ADDR
     "${META_B_START_ADDR} + ${META_PAGE_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)

# Validate the physical layout at CMake configure time as well as in C and ld.
math(EXPR FLASH_SIZE_REMAINDER
     "${FLASH_SIZE_BYTES} % ${FLASH_LAYOUT_PAGE_SIZE}")

if(NOT FLASH_BASE_ADDR EQUAL 0x08000000)
    message(FATAL_ERROR "Unexpected STM32G431 Flash base address")
endif()
if(NOT FLASH_SIZE_BYTES EQUAL 0x00020000)
    message(FATAL_ERROR "Unexpected STM32G431 Flash size")
endif()
if(NOT FLASH_LAYOUT_PAGE_SIZE EQUAL 0x00000800)
    message(FATAL_ERROR "Unexpected STM32G431 Flash page size")
endif()
if(NOT RAM_ORIGIN EQUAL 0x20000000)
    message(FATAL_ERROR "Unexpected STM32G431 RAM origin")
endif()
if(NOT RAM_LENGTH EQUAL 0x00008000)
    message(FATAL_ERROR "Unexpected STM32G431 RAM length")
endif()
if(NOT FLASH_SIZE_REMAINDER EQUAL 0)
    message(FATAL_ERROR "Flash size must be an integral number of pages")
endif()
if(NOT FLASH_LAYOUT_PAGE_COUNT EQUAL 64)
    message(FATAL_ERROR "Unexpected STM32G431 Flash page count")
endif()

if(NOT BOOTLOADER_START_ADDR EQUAL FLASH_BASE_ADDR)
    message(FATAL_ERROR "Bootloader must start at the beginning of Flash")
endif()
if(NOT BOOTLOADER_SIZE EQUAL 0x00006000)
    message(FATAL_ERROR "Unexpected bootloader size")
endif()
if(NOT BOOTLOADER_PAGE_COUNT EQUAL 12)
    message(FATAL_ERROR "Bootloader must occupy twelve Flash pages")
endif()
if(NOT BOOTLOADER_END_ADDR EQUAL APP_HEADER_ADDR)
    message(FATAL_ERROR "Application header must follow the bootloader")
endif()
if(NOT APP_HEADER_SIZE EQUAL FLASH_LAYOUT_PAGE_SIZE)
    message(FATAL_ERROR "Application header must occupy one complete page")
endif()
if(NOT APP_HEADER_END_ADDR EQUAL APP_START_ADDR)
    message(FATAL_ERROR "Application code must follow the application header")
endif()
if(NOT APP_VECTOR_TABLE_ADDR EQUAL APP_START_ADDR)
    message(FATAL_ERROR "Application vector table must be at application start")
endif()
if(NOT APP_VECTOR_TABLE_OFFSET EQUAL 0x00006800)
    message(FATAL_ERROR "Unexpected application vector-table offset")
endif()
if(NOT APP_SLOT_END_ADDR EQUAL META_A_START_ADDR)
    message(FATAL_ERROR "Application slot must end at Metadata A")
endif()
if(NOT APP_SLOT_SIZE EQUAL 0x00019000)
    message(FATAL_ERROR "Unexpected application slot size")
endif()
if(NOT APP_MAX_CODE_SIZE EQUAL 0x00018800)
    message(FATAL_ERROR "Unexpected maximum application code size")
endif()
if(NOT APP_MAX_IMAGE_SIZE EQUAL APP_SLOT_SIZE)
    message(FATAL_ERROR "Maximum image size must include the application header")
endif()
if(NOT APP_START_ADDR LESS APP_SLOT_END_ADDR)
    message(FATAL_ERROR "Application start is outside the application slot")
endif()

if(NOT META_A_START_ADDR EQUAL 0x0801F000)
    message(FATAL_ERROR "Unexpected Metadata A address")
endif()
if(NOT META_B_START_ADDR EQUAL 0x0801F800)
    message(FATAL_ERROR "Unexpected Metadata B address")
endif()
if(NOT FLASH_METADATA_END_ADDR EQUAL FLASH_END_ADDR)
    message(FATAL_ERROR "Metadata pages must end at the end of Flash")
endif()

# The module directory is captured while the file is included.  This remains
# stable when the helper function below is called from a project CMakeLists.
set(_ASTRA_FLASH_LAYOUT_DIR "${CMAKE_CURRENT_LIST_DIR}")

# Generate the C header and a target-specific linker script.
#
# Required arguments:
#   TARGET             name of the executable target
#   IMAGE_NAME         human-readable target name used in linker diagnostics
#   IMAGE_REGION       BOOTLOADER or APPLICATION; the range is derived here
#   GENERATED_DIR      output directory in the build tree
function(astra_configure_flash_layout)
    cmake_parse_arguments(FLASH_LAYOUT "" "TARGET;IMAGE_NAME;IMAGE_REGION;GENERATED_DIR" "" ${ARGN})

    if(FLASH_LAYOUT_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR
                "astra_configure_flash_layout: unknown arguments: ${FLASH_LAYOUT_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT DEFINED FLASH_LAYOUT_TARGET OR
       "${FLASH_LAYOUT_TARGET}" STREQUAL "")
        message(FATAL_ERROR
                "astra_configure_flash_layout: TARGET is required")
    endif()
    if(NOT TARGET "${FLASH_LAYOUT_TARGET}")
        message(FATAL_ERROR
                "astra_configure_flash_layout: target does not exist: ${FLASH_LAYOUT_TARGET}")
    endif()
    if(NOT DEFINED FLASH_LAYOUT_IMAGE_NAME OR
       NOT DEFINED FLASH_LAYOUT_IMAGE_REGION OR
       NOT DEFINED FLASH_LAYOUT_GENERATED_DIR)
        message(FATAL_ERROR
                "astra_configure_flash_layout: IMAGE_NAME, IMAGE_REGION, "
                "and GENERATED_DIR are required")
    endif()
    if("${FLASH_LAYOUT_IMAGE_NAME}" STREQUAL "" OR
       "${FLASH_LAYOUT_IMAGE_REGION}" STREQUAL "" OR
       "${FLASH_LAYOUT_GENERATED_DIR}" STREQUAL "")
        message(FATAL_ERROR
                "astra_configure_flash_layout: arguments must not be empty")
    endif()

    if("${FLASH_LAYOUT_IMAGE_REGION}" STREQUAL "BOOTLOADER")
        set(IMAGE_FLASH_ORIGIN "${BOOTLOADER_START_ADDR}")
        set(IMAGE_FLASH_LENGTH "${BOOTLOADER_SIZE}")
        set(IMAGE_FLASH_END    "${BOOTLOADER_END_ADDR}")
    elseif("${FLASH_LAYOUT_IMAGE_REGION}" STREQUAL "APPLICATION")
        set(IMAGE_FLASH_ORIGIN "${APP_START_ADDR}")
        set(IMAGE_FLASH_LENGTH "${APP_MAX_CODE_SIZE}")
        set(IMAGE_FLASH_END    "${APP_SLOT_END_ADDR}")
    else()
        message(FATAL_ERROR
                "astra_configure_flash_layout: IMAGE_REGION must be BOOTLOADER or APPLICATION")
    endif()

    math(EXPR _derived_image_length
         "${IMAGE_FLASH_END} - ${IMAGE_FLASH_ORIGIN}")
    if(NOT _derived_image_length EQUAL IMAGE_FLASH_LENGTH)
        message(FATAL_ERROR
                "astra_configure_flash_layout: inconsistent ${FLASH_LAYOUT_IMAGE_REGION} Flash range")
    endif()

    set(IMAGE_HEADER_REGION "")
    set(IMAGE_HEADER_TERM   "0")
    set(IMAGE_HEADER_SECTION "")

        if("${FLASH_LAYOUT_IMAGE_REGION}" STREQUAL "APPLICATION")
        set(IMAGE_HEADER_REGION
            "  APPHEADER (rx)   : ORIGIN = ${APP_HEADER_ADDR}, LENGTH = ${APP_HEADER_SIZE}")
        set(IMAGE_HEADER_TERM
            "LOADADDR(.header) + SIZEOF(.header)")
        set(IMAGE_HEADER_SECTION

    "  /* One complete Flash page reserved for the image header, defined in
         Shared/Image/Src/AppHeader.c.  KEEP() protects it from --gc-sections,
         which is enabled for this target.

         The page is padded with real zero bytes instead of being left
         undefined.  scripts/astra_image.py computes the header CRC over a
         gap-filled binary, so every byte of the page must be deterministic
         for an ELF programmed directly by a debug adapter to yield the same
         CRC as the packed .bin. */
      .header ORIGIN(APPHEADER) :
      {
        . = ALIGN(4);
        KEEP(*(.header))
        . = ALIGN(4);
        FILL(0x00);
        . = ORIGIN(APPHEADER) + LENGTH(APPHEADER);
      } >APPHEADER

      /* Read back from the ELF by scripts/astra_image.py to derive the image
         size and the CRC range.  These must be plain assignments: PROVIDE()
         omits a definition unless something references it, and the only
         consumer is an external tool, not the linker itself. */
      __app_header_addr = ORIGIN(APPHEADER);
      __app_image_start = ORIGIN(FLASH);

      ASSERT(ORIGIN(APPHEADER) == ${BOOTLOADER_END_ADDR},
             \"${FLASH_LAYOUT_IMAGE_NAME} application header must follow the bootloader\")
      ASSERT(ORIGIN(APPHEADER) + LENGTH(APPHEADER) == ORIGIN(FLASH),
             \"${FLASH_LAYOUT_IMAGE_NAME} application header must end at the vector table\")
    ")

    endif()

    set(_generated_dir "${FLASH_LAYOUT_GENERATED_DIR}")
    file(MAKE_DIRECTORY "${_generated_dir}")

    # configure_file() sees these aliases while expanding the templates.
    set(IMAGE_NAME         "${FLASH_LAYOUT_IMAGE_NAME}")

    set(_header_file "${_generated_dir}/FlashLayout.h")
    set(_linker_script "${_generated_dir}/${FLASH_LAYOUT_TARGET}.ld")

    configure_file("${_ASTRA_FLASH_LAYOUT_DIR}/FlashLayout.h.in"
                   "${_header_file}" @ONLY)
    configure_file("${_ASTRA_FLASH_LAYOUT_DIR}/STM32G431xx_FLASH.ld.in"
                   "${_linker_script}" @ONLY)

    # Make the generated paths visible to the parent project and to the
    # STM32CubeMX object library, which is added by the caller afterwards.
    set(ASTRA_GENERATED_DIR  "${_generated_dir}" PARENT_SCOPE)
    set(ASTRA_LINKER_SCRIPT  "${_linker_script}" PARENT_SCOPE)

    target_include_directories("${FLASH_LAYOUT_TARGET}" PRIVATE
                               "${_generated_dir}")
    target_link_options("${FLASH_LAYOUT_TARGET}" PRIVATE
                        "-T${_linker_script}")
    set_property(TARGET "${FLASH_LAYOUT_TARGET}" APPEND PROPERTY
                 LINK_DEPENDS "${_linker_script}")
endfunction()
