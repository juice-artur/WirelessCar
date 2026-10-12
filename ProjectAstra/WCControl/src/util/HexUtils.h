#pragma once

#include <QByteArray>
#include <QRegularExpression>
#include <QString>

namespace HexUtils {

// "A5 06 00" / "A50600" / "0xA5,0x06" -> QByteArray
inline bool fromHex(const QString &text, QByteArray *out, QString *error = nullptr)
{
    if (out)
        out->clear();

    QString cleaned = text;
    cleaned.remove(QRegularExpression(QStringLiteral("0[xX]")));
    cleaned.remove(QRegularExpression(QStringLiteral("[\\s,;:_\\-]+")));

    if (cleaned.isEmpty()) {
        if (error)
            *error = QStringLiteral("Nothing to send.");
        return false;
    }
    if (cleaned.size() % 2 != 0) {
        if (error)
            *error = QStringLiteral("Hex string must contain an even number of digits.");
        return false;
    }

    QByteArray result;
    result.reserve(cleaned.size() / 2);
    for (int i = 0; i + 1 < cleaned.size(); i += 2) {
        bool ok = false;
        const uint value = cleaned.mid(i, 2).toUInt(&ok, 16);
        if (!ok) {
            if (error)
                *error = QStringLiteral("Invalid hex byte: \"%1\".").arg(cleaned.mid(i, 2));
            return false;
        }
        result.append(static_cast<char>(value));
    }

    if (out)
        *out = result;
    return true;
}

// QByteArray -> "A5 06 00"
inline QString toSpacedHex(const QByteArray &data)
{
    static const char digits[] = "0123456789ABCDEF";
    QString out;
    out.reserve(data.size() * 3);
    for (int i = 0; i < data.size(); ++i) {
        if (i > 0)
            out += QLatin1Char(' ');
        const uchar b = static_cast<uchar>(data.at(i));
        out += QLatin1Char(digits[b >> 4]);
        out += QLatin1Char(digits[b & 0x0F]);
    }
    return out;
}

// QByteArray -> printable ASCII, non printable replaced with '.'
inline QString toAscii(const QByteArray &data)
{
    QString out;
    out.reserve(data.size());
    for (char c : data) {
        const uchar b = static_cast<uchar>(c);
        out += (b >= 0x20 && b < 0x7F) ? QLatin1Char(c) : QLatin1Char('.');
    }
    return out;
}

} // namespace HexUtils
