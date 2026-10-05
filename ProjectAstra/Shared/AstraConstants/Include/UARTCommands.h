#pragma once

/*
 * One byte status codes exchanged between the host and the bootloader.
 *
 * The handshake is strictly sequential:
 *
 *   device -> host   CMD_STM32_READY   once after reset
 *   host   -> device 20 bytes          AppHeader_t
 *   device -> host   ASTRA_ACK         header accepted
 *                    ASTRA_NACK        header rejected (magic or size)
 *   device -> host   ASTRA_ERASE_DONE  application slot erased, send the body
 *   device -> host   ASTRA_ACK         one Flash page of the body is stored
 *   device -> host   ASTRA_IMAGE_OK    CRC matched, header page committed
 *                    ASTRA_IMAGE_BAD   CRC mismatch, the slot stays unusable
 *
 * ASTRA_ERASE_DONE is mandatory: erasing the slot blocks all Flash access on
 * this part, so the host has to be told when the device is able to receive
 * again instead of guessing a delay.
 */

/* Sent once after reset when an update has been requested. */
#define CMD_STM32_READY 0xA5

/* The header was accepted, or one page of the body has been stored. */
#define ASTRA_ACK 0x06

/* The header was rejected, so nothing has been erased or written. */
#define ASTRA_NACK 0x15

/* The application slot is erased and the host may start sending the body. */
#define ASTRA_ERASE_DONE 0xA6

/* The body CRC matched the header and the header page has been rewritten. */
#define ASTRA_IMAGE_OK 0x4F

/* The body CRC did not match the header.  The slot stays unusable. */
#define ASTRA_IMAGE_BAD 0xFE