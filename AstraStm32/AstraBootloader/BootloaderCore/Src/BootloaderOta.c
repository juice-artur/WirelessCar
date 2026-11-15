#include "BootloaderOta.h"

#include <string.h>

#include "AppHeader.h"
#include "AstraCrc.h"
#include "AstraFlash.h"
#include "AstraOtaProtocol.h"
#include "BootloaderJump.h"
#include "FlashLayout.h"

#define OTA_LISTEN_TIMEOUT_MS 5000u
#define OTA_BYTE_TIMEOUT_MS   1000u
#define OTA_RX_RING_SIZE      4096u

/* ---------------------------------------------------------------------------
 * Link indication and diagnostics
 *
 * The two LEDs on PB7/PB9 are always driven, they are the only indication the
 * board has:
 *   - PB7 blinks once per second while the OTA loop is running (liveness),
 *   - PB9 latches on as soon as one byte arrives from the bridge.
 *
 * ASTRA_OTA_LINK_DEBUG additionally turns the UART itself into a probe: every
 * received byte is echoed back raw until the first valid frame is parsed
 * (proves both wires and the baud rate) and an unsolicited INFO frame is sent
 * every two seconds (proves STM32 -> nRF -> BLE -> Qt).  It keeps the 250 ms
 * idle poll and costs Flash, so it stays off outside bring-up.
 * ------------------------------------------------------------------------- */
#define ASTRA_OTA_LINK_DEBUG 0

#define OTA_HEARTBEAT_MS   1000u
#define OTA_HEARTBEAT_POLL 250u

static uint32_t s_nextHeartMs;

#if ASTRA_OTA_LINK_DEBUG
#define OTA_DEBUG_INFO_MS 2000u
static uint32_t s_nextInfoMs;
static bool s_echoRaw;
#endif

static UART_HandleTypeDef *s_uart;
static uint8_t s_rxByte;
static uint8_t s_ring[OTA_RX_RING_SIZE];
static volatile uint32_t s_ringHead;
static volatile uint32_t s_ringTail;

static bool s_began;
static uint32_t s_codeSize;
static uint32_t s_crcExpected;
static uint32_t s_version;
static uint32_t s_offset;

static uint32_t s_headerPage[PAGE_SIZE_WORDS];
static uint8_t s_frame[3u + ASTRA_OTA_MAX_PAYLOAD];

/* ------------------------------------------------------------------ UART RX */

void BootloaderOta_OnRxCplt(UART_HandleTypeDef *uart)
{
	if (uart != s_uart)
	{
		return;
	}

	uint32_t const next = (s_ringHead + 1u) % OTA_RX_RING_SIZE;
	if (next != s_ringTail)
	{
		s_ring[s_ringHead] = s_rxByte;
		s_ringHead = next;
	}

	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET);

#if ASTRA_OTA_LINK_DEBUG
	if (s_echoRaw && uart->gState == HAL_UART_STATE_READY)
	{
		while ((uart->Instance->ISR & USART_ISR_TXE_TXFNF) == 0u)
		{
		}
		uart->Instance->TDR = s_rxByte;
	}
#endif

	HAL_UART_Receive_IT(uart, &s_rxByte, 1u);
}

void BootloaderOta_OnError(UART_HandleTypeDef *uart)
{
	if (uart != s_uart)
	{
		return;
	}

	uart->ErrorCode = HAL_UART_ERROR_NONE;
	(void)HAL_UART_Receive_IT(uart, &s_rxByte, 1u);
}

void BootloaderOta_PrimeRx(UART_HandleTypeDef *uart)
{
	s_uart = uart;
	s_ringHead = 0u;
	s_ringTail = 0u;

	(void)uart->Instance->ISR;
	(void)uart->Instance->RDR;
	uart->ErrorCode = HAL_UART_ERROR_NONE;

	(void)HAL_UART_Receive_IT(s_uart, &s_rxByte, 1u);
}

bool BootloaderOta_SniffRxLine(void)
{
	GPIO_InitTypeDef gpio = {0};
	uint32_t high = 0u;

	__HAL_RCC_GPIOA_CLK_ENABLE();

	gpio.Pin = GPIO_PIN_10;
	gpio.Mode = GPIO_MODE_INPUT;
	gpio.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(GPIOA, &gpio);

	for (uint32_t sample = 0u; sample < 50u; sample++)
	{
		HAL_Delay(1u);
		if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET)
		{
			high++;
		}
	}

	HAL_GPIO_DeInit(GPIOA, GPIO_PIN_10);

	return high > 25u;
}

static bool ringPop(uint8_t *byte, uint32_t timeoutMs)
{
	uint32_t const start = HAL_GetTick();

	while (s_ringHead == s_ringTail)
	{
		if ((HAL_GetTick() - start) >= timeoutMs)
		{
			return false;
		}
	}

	*byte = s_ring[s_ringTail];
	s_ringTail = (s_ringTail + 1u) % OTA_RX_RING_SIZE;
	return true;
}

/* -------------------------------------------------------------------- frames */

static void sendFrame(uint8_t type, const void *payload, uint16_t length)
{
	static uint8_t tx[ASTRA_OTA_FRAME_OVERHEAD + ASTRA_OTA_MAX_PAYLOAD];

	if (length > ASTRA_OTA_MAX_PAYLOAD)
	{
		return;
	}

	tx[0] = ASTRA_OTA_SOF;
	tx[1] = type;
	tx[2] = (uint8_t)(length & 0xFFu);
	tx[3] = (uint8_t)(length >> 8);

	if (length > 0u)
	{
		memcpy(&tx[4], payload, length);
	}

	uint16_t const crc = AstraCrc16(&tx[1], 3u + length);
	tx[4u + length] = (uint8_t)(crc & 0xFFu);
	tx[5u + length] = (uint8_t)(crc >> 8);

	HAL_UART_Transmit(s_uart, tx, (uint16_t)(ASTRA_OTA_FRAME_OVERHEAD + length),
	                  1000u);
}

static void sendStatus(AstraOtaResult code, uint32_t offset)
{
	AstraOtaStatus const status = { .code = (uint8_t)code, .offset = offset };
	sendFrame(ASTRA_MSG_STATUS, &status, sizeof(status));
}

static bool readFrame(uint8_t *type, uint8_t *payload, uint16_t capacity,
                      uint16_t *length, uint32_t timeoutMs)
{
	uint8_t byte;

	for (;;)
	{
		if (!ringPop(&byte, timeoutMs))
		{
			return false;
		}
		if (byte == ASTRA_OTA_SOF)
		{
			break;
		}
	}

	uint8_t head[3];
	for (uint32_t i = 0U; i < sizeof(head); i++)
	{
		if (!ringPop(&head[i], OTA_BYTE_TIMEOUT_MS))
		{
			return false;
		}
	}

	uint16_t const len = (uint16_t)head[1] | ((uint16_t)head[2] << 8);
	if (len > capacity || len > ASTRA_OTA_MAX_PAYLOAD)
	{
		return false;
	}

	s_frame[0] = head[0];
	s_frame[1] = head[1];
	s_frame[2] = head[2];

	for (uint16_t i = 0U; i < len; i++)
	{
		if (!ringPop(&s_frame[3u + i], OTA_BYTE_TIMEOUT_MS))
		{
			return false;
		}
	}

	uint8_t crcBytes[2];
	if (!ringPop(&crcBytes[0], OTA_BYTE_TIMEOUT_MS) ||
	    !ringPop(&crcBytes[1], OTA_BYTE_TIMEOUT_MS))
	{
		return false;
	}

	uint16_t const crc = (uint16_t)crcBytes[0] | ((uint16_t)crcBytes[1] << 8);
	if (AstraCrc16(s_frame, 3u + len) != crc)
	{
		return false;
	}

	*type = head[0];
	*length = len;

	if (len > 0u)
	{
		memcpy(payload, &s_frame[3], len);
	}

	return true;
}

/* --------------------------------------------------------------------- flash */

static void buildHeader(uint32_t size, uint32_t crc, uint32_t version,
                        uint32_t otaRequest)
{
	memset(s_headerPage, 0, sizeof(s_headerPage));

	AppHeader_t *header = (AppHeader_t *)s_headerPage;
	header->magic = ASTRA_MAGIC_VALUE;
	header->size = size;
	header->crc = crc;
	header->version = version;
	header->otaRequest = otaRequest;
}

static AstraOtaResult otaBegin(AstraOtaBegin const *begin)
{
	if (begin->codeSize == 0u || begin->codeSize > APP_MAX_CODE_SIZE ||
	    (begin->codeSize % 8u) != 0u)
	{
		return ASTRA_OTA_ERR_SIZE;
	}

	for (uint32_t addr = APP_HEADER_ADDR;
	     addr < (APP_START_ADDR + begin->codeSize);
	     addr += FLASH_LAYOUT_PAGE_SIZE)
	{
		if (FlashErasePage(addr) != HAL_OK)
		{
			return ASTRA_OTA_ERR_FLASH;
		}
	}

	buildHeader(0u, 0u, begin->version, OTA_REQUESTED);
	if (FlashWritePage(APP_HEADER_ADDR, s_headerPage) != HAL_OK)
	{
		return ASTRA_OTA_ERR_FLASH;
	}

	s_codeSize = begin->codeSize;
	s_crcExpected = begin->crc32;
	s_version = begin->version;
	s_offset = 0u;
	s_began = true;
	return ASTRA_OTA_OK;
}

static AstraOtaResult otaData(uint32_t offset, uint8_t const *data,
                              uint16_t length)
{
	if (!s_began)
	{
		return ASTRA_OTA_ERR_STATE;
	}

	if (offset != s_offset || (offset % 8u) != 0u || (length % 8u) != 0u ||
	    (offset + length) > s_codeSize)
	{
		return ASTRA_OTA_ERR_OFFSET;
	}

	if (FlashWriteDoubleWords(APP_START_ADDR + offset, data, length) != HAL_OK)
	{
		return ASTRA_OTA_ERR_FLASH;
	}

	s_offset += length;
	return ASTRA_OTA_OK;
}

static void otaEnd(void)
{
	if (s_offset != s_codeSize)
	{
		sendStatus(ASTRA_OTA_ERR_SIZE, s_offset);
		return;
	}

	uint32_t const crc = AstraCrc32((void const *)APP_START_ADDR, s_codeSize);
	if (crc != s_crcExpected)
	{
		sendStatus(ASTRA_OTA_ERR_CRC, s_offset);
		s_began = false;
		return;
	}

	buildHeader(s_codeSize, crc, s_version, OTA_NOT_REQUESTED);
	if (FlashErasePage(APP_HEADER_ADDR) != HAL_OK ||
	    FlashWritePage(APP_HEADER_ADDR, s_headerPage) != HAL_OK)
	{
		sendStatus(ASTRA_OTA_ERR_FLASH, s_offset);
		s_began = false;
		return;
	}

	AstraOtaDone const done = { .code = (uint8_t)ASTRA_OTA_OK };
	sendFrame(ASTRA_MSG_DONE, &done, sizeof(done));

	HAL_Delay(100u);
	NVIC_SystemReset();
}

/* -------------------------------------------------------------------- frames */

bool BootloaderOta_IsRequested(void)
{
	AppHeader_t const *const header = (AppHeader_t const *)APP_HEADER_ADDR;
	return header->magic == ASTRA_MAGIC_VALUE &&
	       header->otaRequest == OTA_REQUESTED;
}

static void handleFrame(uint8_t type, uint8_t const *payload, uint16_t length)
{
#if ASTRA_OTA_LINK_DEBUG
	/* Real traffic started: stop polluting the stream with the raw echo. */
	s_echoRaw = false;
#endif

	switch (type)
	{
	case ASTRA_MSG_GET_INFO:
	{
		AppHeader_t const *const header = (AppHeader_t const *)APP_HEADER_ADDR;
		AstraOtaInfo const info = { .activeVersion = header->version,
			                        .maxCodeSize = APP_MAX_CODE_SIZE,
			                        .state = s_began ? 1u : 0u };
		sendFrame(ASTRA_MSG_INFO, &info, sizeof(info));
		break;
	}

	case ASTRA_MSG_BEGIN:
	{
		if (length != sizeof(AstraOtaBegin))
		{
			sendStatus(ASTRA_OTA_ERR_FRAME, 0u);
			break;
		}

		AstraOtaBegin begin;
		memcpy(&begin, payload, sizeof(begin));

		AstraOtaResult const result = otaBegin(&begin);
		if (result == ASTRA_OTA_OK)
		{
			AstraOtaAck const ack = { .nextOffset = 0u };
			sendFrame(ASTRA_MSG_ACK, &ack, sizeof(ack));
		}
		else
		{
			sendStatus(result, 0u);
		}
		break;
	}

	case ASTRA_MSG_DATA:
	{
		if (length < sizeof(uint32_t))
		{
			sendStatus(ASTRA_OTA_ERR_FRAME, 0u);
			break;
		}

		uint32_t offset;
		memcpy(&offset, payload, sizeof(offset));

		AstraOtaResult const result =
		 otaData(offset, payload + sizeof(offset),
		         (uint16_t)(length - sizeof(offset)));
		if (result == ASTRA_OTA_OK)
		{
			AstraOtaAck const ack = { .nextOffset = s_offset };
			sendFrame(ASTRA_MSG_ACK, &ack, sizeof(ack));
		}
		else
		{
			sendStatus(result, offset);
		}
		break;
	}

	case ASTRA_MSG_END:
		otaEnd();
		break;

	case ASTRA_MSG_ABORT:
		s_began = false;
		sendStatus(ASTRA_OTA_OK, 0u);
		break;

	default:
		sendStatus(ASTRA_OTA_ERR_FRAME, 0u);
		break;
	}
}

static void indicationService(void)
{
	uint32_t const now = HAL_GetTick();

	if ((int32_t)(now - s_nextHeartMs) >= 0)
	{
		s_nextHeartMs = now + OTA_HEARTBEAT_MS;
		HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_7);
	}

#if ASTRA_OTA_LINK_DEBUG
	if (s_began || (int32_t)(now - s_nextInfoMs) < 0)
	{
		return;
	}

	s_nextInfoMs = now + OTA_DEBUG_INFO_MS;

	AppHeader_t const *const header = (AppHeader_t const *)APP_HEADER_ADDR;
	AstraOtaInfo const info = { .activeVersion = header->version,
		                        .maxCodeSize = APP_MAX_CODE_SIZE,
		                        .state = s_began ? 1u : 0u };
	sendFrame(ASTRA_MSG_INFO, &info, sizeof(info));
#endif
}

void BootloaderOta_Run(UART_HandleTypeDef *uart)
{
	static uint8_t payload[ASTRA_OTA_MAX_PAYLOAD];

	if (s_uart != uart)
	{
		BootloaderOta_PrimeRx(uart);
	}

	s_began = false;
	s_nextHeartMs = HAL_GetTick();
#if ASTRA_OTA_LINK_DEBUG
	s_echoRaw = true;
	s_nextInfoMs = HAL_GetTick() + OTA_DEBUG_INFO_MS;
#endif

	bool const appValid = (IsApplicationValid() == APPLICATION_VALID);
	bool const requested = BootloaderOta_IsRequested();
	uint32_t const listenStart = HAL_GetTick();

	for (;;)
	{
		bool const listening = !s_began && appValid && !requested &&
		                       (HAL_GetTick() - listenStart) <
		                        OTA_LISTEN_TIMEOUT_MS;

		uint8_t type = 0u;
		uint16_t length = 0u;

		uint32_t const timeout = listening ? 100u : OTA_HEARTBEAT_POLL;

		if (!readFrame(&type, payload, sizeof(payload), &length, timeout))
		{
			if (!s_began && appValid && !requested &&
			    (HAL_GetTick() - listenStart) >= OTA_LISTEN_TIMEOUT_MS)
			{
				return;
			}
			indicationService();
			continue;
		}

		indicationService();
		handleFrame(type, payload, length);
	}
}
