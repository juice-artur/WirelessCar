#pragma once

#include <stddef.h>
#include <stdint.h>

/*
 * Astra OTA protocol, spoken between the Qt host (over BLE -> nRF -> UART) and
 * the STM32 bootloader.
 *
 * Frame: 0xA5 | type u8 | length u16 LE | payload[length] | crc16 u16 LE
 * crc16 = CRC-16/CCITT-FALSE over type + length bytes + payload.
 */

#define ASTRA_OTA_SOF              0xA5u
#define ASTRA_OTA_HEADER_SIZE      4u
#define ASTRA_OTA_FRAME_OVERHEAD   6u
#define ASTRA_OTA_BLOCK_SIZE       2048u
#define ASTRA_OTA_MAX_PAYLOAD      (ASTRA_OTA_BLOCK_SIZE + 8u)

/* Manifest at the beginning of the .ota file ("AOTA" as file bytes). */
#define ASTRA_OTA_MANIFEST_MAGIC   0x41544F41UL

typedef enum
{
	ASTRA_MSG_GET_INFO = 0x02,
	ASTRA_MSG_INFO     = 0x82,
	ASTRA_MSG_BEGIN    = 0x10,
	ASTRA_MSG_DATA     = 0x11,
	ASTRA_MSG_END      = 0x12,
	ASTRA_MSG_ABORT    = 0x13,
	ASTRA_MSG_ACK      = 0x90,
	ASTRA_MSG_STATUS   = 0x91,
	ASTRA_MSG_DONE     = 0x92
} AstraOtaMessageType;

typedef enum
{
	ASTRA_OTA_OK = 0,
	ASTRA_OTA_ERR_STATE  = 1,
	ASTRA_OTA_ERR_SIZE   = 2,
	ASTRA_OTA_ERR_CRC    = 3,
	ASTRA_OTA_ERR_FLASH  = 4,
	ASTRA_OTA_ERR_OFFSET = 5,
	ASTRA_OTA_ERR_FRAME  = 6
} AstraOtaResult;

typedef struct __attribute__((packed))
{
	uint32_t activeVersion;
	uint32_t maxCodeSize;
	uint8_t state; /* 1 = update in progress */
} AstraOtaInfo;

typedef struct __attribute__((packed))
{
	uint32_t codeSize;
	uint32_t crc32;
	uint32_t version;
} AstraOtaBegin;

typedef struct __attribute__((packed))
{
	uint32_t nextOffset;
} AstraOtaAck;

typedef struct __attribute__((packed))
{
	uint8_t code;
	uint32_t offset;
} AstraOtaStatus;

typedef struct __attribute__((packed))
{
	uint8_t code;
} AstraOtaDone;

typedef struct __attribute__((packed))
{
	uint32_t magic; /* ASTRA_OTA_MANIFEST_MAGIC */
	uint32_t version;
	uint32_t codeSize;
	uint32_t crc32; /* over codeSize bytes of code */
} AstraOtaManifest;

_Static_assert(sizeof(AstraOtaManifest) == 16u, "Manifest must stay 16 bytes");
_Static_assert(sizeof(AstraOtaBegin) == 12u, "BEGIN payload must stay 12 bytes");
_Static_assert(sizeof(AstraOtaInfo) == 9u, "INFO payload must stay 9 bytes");
_Static_assert(sizeof(AstraOtaStatus) == 5u, "STATUS payload must stay 5 bytes");
