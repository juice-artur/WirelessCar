#include "AstraCrc.h"

/* CRC-32/IEEE 802.3 with the standard nibble table. */
static uint32_t const s_nibble[16] = {
	0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
	0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
	0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
	0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL,
};

uint32_t AstraCrc32Update(uint32_t state, const void *data, size_t length)
{
	uint8_t const *bytes = (uint8_t const *)data;

	while (length-- > 0u)
	{
		state ^= *bytes++;
		state = (state >> 4) ^ s_nibble[state & 0x0Fu];
		state = (state >> 4) ^ s_nibble[state & 0x0Fu];
	}

	return state;
}

uint32_t AstraCrc32(const void *data, size_t length)
{
	return AstraCrc32Update(ASTRA_CRC32_INIT, data, length) ^ 0xFFFFFFFFUL;
}

uint16_t AstraCrc16(const void *data, size_t length)
{
	uint8_t const *bytes = (uint8_t const *)data;
	uint16_t crc = 0xFFFFu;

	while (length-- > 0u)
	{
		crc ^= (uint16_t)((uint16_t)*bytes++ << 8);

		for (uint32_t bit = 0U; bit < 8U; bit++)
		{
			crc = (crc & 0x8000u) ? (uint16_t)((uint16_t)(crc << 1) ^ 0x1021u)
			                      : (uint16_t)(crc << 1);
		}
	}

	return crc;
}
