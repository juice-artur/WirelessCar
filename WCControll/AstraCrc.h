#pragma once

#include <QByteArray>
#include <QtGlobal>

// CRC helpers shared with the STM32 firmware (see Shared/Image/Src/AstraCrc.c).
// Both are byte-for-byte compatible with the C implementation and with
// Python's zlib.crc32() for the 32-bit variant.
namespace Astra {

inline quint32 crc32Update(quint32 state, const char *data, qsizetype length)
{
    auto const *bytes = reinterpret_cast<const quint8 *>(data);

    for (qsizetype i = 0; i < length; ++i) {
        state ^= bytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            state = (state & 1u) ? (state >> 1) ^ 0xEDB88320u : (state >> 1);
        }
    }

    return state;
}

inline quint32 crc32(const QByteArray &data)
{
    return crc32Update(0xFFFFFFFFu, data.constData(), data.size()) ^ 0xFFFFFFFFu;
}

inline quint16 crc16(const QByteArray &data)
{
    quint16 crc = 0xFFFFu;

    for (char const byte : data) {
        crc ^= static_cast<quint16>(static_cast<quint8>(byte) << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000u) ? static_cast<quint16>((crc << 1) ^ 0x1021u)
                                  : static_cast<quint16>(crc << 1);
        }
    }

    return crc;
}

} // namespace Astra
