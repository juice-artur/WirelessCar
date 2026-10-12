#include "LogModel.h"

#include "../util/HexUtils.h"

LogModel::LogModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LogModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

QVariant LogModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const Entry &e = m_entries.at(index.row());
    switch (role) {
    case TimeRole:
        return e.time.toString(QStringLiteral("HH:mm:ss.zzz"));
    case DirectionRole:
        return static_cast<int>(e.direction);
    case HexRole:
        return e.hex;
    case AsciiRole:
        return e.ascii;
    case MessageRole:
        return e.message;
    case LevelRole:
        return static_cast<int>(e.level);
    default:
        return {};
    }
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        {TimeRole, QByteArrayLiteral("time")},
        {DirectionRole, QByteArrayLiteral("direction")},
        {HexRole, QByteArrayLiteral("hex")},
        {AsciiRole, QByteArrayLiteral("ascii")},
        {MessageRole, QByteArrayLiteral("message")},
        {LevelRole, QByteArrayLiteral("level")},
    };
}

void LogModel::clear()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
}

void LogModel::appendData(Direction direction, const QByteArray &data)
{
    Entry e;
    e.time = QDateTime::currentDateTime();
    e.direction = direction;
    e.hex = HexUtils::toSpacedHex(data);
    e.ascii = HexUtils::toAscii(data);
    e.level = Normal;
    addEntry(e);
}

void LogModel::appendInfo(const QString &message, Level level)
{
    Entry e;
    e.time = QDateTime::currentDateTime();
    e.direction = Info;
    e.message = message;
    e.level = level;
    addEntry(e);
}

void LogModel::addEntry(Entry entry)
{
    const int row = m_entries.size();
    beginInsertRows(QModelIndex(), row, row);
    m_entries.append(std::move(entry));
    endInsertRows();
    emit countChanged();
    trim();
}

void LogModel::trim()
{
    const int excess = m_entries.size() - kMaxEntries;
    if (excess <= 0)
        return;
    beginRemoveRows(QModelIndex(), 0, excess - 1);
    m_entries.remove(0, excess);
    endRemoveRows();
    emit countChanged();
}
